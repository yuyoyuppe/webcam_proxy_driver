#include <dxgiformat.h>
#include <assert.h>
#include <winrt/base.h>

#pragma warning(push)
#pragma warning(disable : 4005)
#include <wincodec.h>
#pragma warning(pop)

#include <memory>
#include <mfapi.h>
#include <shcore.h>
#include <algorithm>

#include <wil/resource.h>

#include "TextureLoader.h"

#if (_WIN32_WINNT >= 0x0602 /*_WIN32_WINNT_WIN8*/) && !defined(DXGI_1_2_FORMATS)
#define DXGI_1_2_FORMATS
#endif

#pragma optimize("", off) // TODO: also disable exceptions

static IWICImagingFactory * _GetWIC()
{
  static IWICImagingFactory * s_Factory = nullptr;

  if(s_Factory)
    return s_Factory;

  HRESULT hr = CoCreateInstance(
    CLSID_WICImagingFactory,
    nullptr,
    CLSCTX_INPROC_SERVER,
    __uuidof(IWICImagingFactory),
    (LPVOID *)&s_Factory
  );

  if(FAILED(hr))
  {
    s_Factory = nullptr;
    return nullptr;
  }

  return s_Factory;
}


#ifdef RETURN_IF_FAILED
#undef RETURN_IF_FAILED
#endif
#define RETURN_IF_FAILED( val )                                                     \
    if ( FAILED( val ) )                                                            \
    {                                                                               \
        return nullptr;                                                        \
    }

using Microsoft::WRL::ComPtr;

ComPtr<IMFSample> LoadImageAsSample(std::wstring_view fileName, IMFMediaType * sampleMediaType)
{
    // Get target sample frame dimensions
    UINT targetWidth = 0;
    UINT targetHeight = 0;
    RETURN_IF_FAILED(MFGetAttributeSize(sampleMediaType, MF_MT_FRAME_SIZE, &targetWidth, &targetHeight));

    IWICImagingFactory * pWIC = _GetWIC();
    if(!pWIC)
    {
        return nullptr;
    }
    
    // Initialize image bitmap decoder from filename and get the image frame
    ComPtr<IWICBitmapDecoder> bitmapDecoder;
    RETURN_IF_FAILED(pWIC->CreateDecoderFromFilename(fileName.data(), 0, GENERIC_READ, WICDecodeMetadataCacheOnDemand, bitmapDecoder.GetAddressOf()));

    ComPtr<IWICBitmapFrameDecode> decodedFrame;
    RETURN_IF_FAILED(bitmapDecoder->GetFrame(0, decodedFrame.GetAddressOf()));
    
    UINT imageWidth = 0, imageHeight = 0;
    RETURN_IF_FAILED(decodedFrame->GetSize(&imageWidth, &imageHeight));
    
    // Scale the image if required
    ComPtr<IWICBitmapSource> sourceImageFrame;
    if(targetWidth != imageWidth || targetHeight != imageHeight)
    {
      ComPtr<IWICBitmapScaler> scaler;
      RETURN_IF_FAILED(pWIC->CreateBitmapScaler(scaler.GetAddressOf()));
      RETURN_IF_FAILED(scaler->Initialize(decodedFrame.Get(), targetWidth, targetHeight, WICBitmapInterpolationModeHighQualityCubic));
      sourceImageFrame.Attach(scaler.Get());

      WICPixelFormatGUID pfScaler;
      RETURN_IF_FAILED(scaler->GetPixelFormat(&pfScaler));
    }
    else
    {
      sourceImageFrame.Attach(decodedFrame.Get());
    }

    // We need to encode the image as jpg, since it's always supported by MT video decoders and could be converted to
    // other formats
    ComPtr<IWICBitmapEncoder> jpgEncoder;
    RETURN_IF_FAILED(pWIC->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, jpgEncoder.GetAddressOf()));
    
    // Prepare the encoder output memory stream and encoding params
    ComPtr<IStream> jpgStream;
    RETURN_IF_FAILED(CreateStreamOnHGlobal(nullptr, true, jpgStream.GetAddressOf()));
    RETURN_IF_FAILED(jpgEncoder->Initialize(jpgStream.Get(), WICBitmapEncoderNoCache));
    ComPtr<IWICBitmapFrameEncode> jpgFrame;
    RETURN_IF_FAILED(jpgEncoder->CreateNewFrame(jpgFrame.GetAddressOf(), nullptr));
    RETURN_IF_FAILED(jpgFrame->Initialize(nullptr));
    
    WICPixelFormatGUID jpgFormat = GUID_WICPixelFormat24bppBGR;
    RETURN_IF_FAILED(jpgFrame->SetPixelFormat(&jpgFormat));
    RETURN_IF_FAILED(jpgFrame->SetSize(targetWidth, targetHeight));
    
    // Commit the image encoding
    RETURN_IF_FAILED(jpgFrame->WriteSource(sourceImageFrame.Get(), nullptr));
    RETURN_IF_FAILED(jpgFrame->Commit());
    RETURN_IF_FAILED(jpgEncoder->Commit());

    // Obtain stream size and lock its memory pointer
    STATSTG jpgStreamStat {};
    RETURN_IF_FAILED(jpgStream->Stat(&jpgStreamStat, STATFLAG_NONAME));
    const size_t jpgStreamSize = jpgStreamStat.cbSize.QuadPart;
    HGLOBAL streamMemoryHandle{};
    RETURN_IF_FAILED(GetHGlobalFromStream(jpgStream.Get(), &streamMemoryHandle));

    auto jpgStreamMemory = static_cast<uint8_t *>(GlobalLock(streamMemoryHandle));
    auto unlockJpgStreamMemory = wil::scope_exit([jpgStreamMemory]{ GlobalUnlock(jpgStreamMemory); });

    // Create a sample from the input image buffer
    ComPtr<IMFSample> inputSample;
    RETURN_IF_FAILED(MFCreateSample(&inputSample));

    IMFMediaBuffer * inputMediaBuffer = nullptr;
    RETURN_IF_FAILED(MFCreateAlignedMemoryBuffer(static_cast<DWORD>(jpgStreamSize), MF_64_BYTE_ALIGNMENT, &inputMediaBuffer));
    BYTE * inputBuf = nullptr;
    DWORD max_length = 0, current_length = 0;
    RETURN_IF_FAILED(inputMediaBuffer->Lock(&inputBuf, &max_length, &current_length));
    if(max_length < jpgStreamSize)
    {
      return nullptr;
    }
    std::copy(jpgStreamMemory, jpgStreamMemory + jpgStreamSize, inputBuf);
    unlockJpgStreamMemory.reset();
    RETURN_IF_FAILED(inputMediaBuffer->Unlock());
    RETURN_IF_FAILED(inputMediaBuffer->SetCurrentLength(static_cast<DWORD>(jpgStreamSize)));
    RETURN_IF_FAILED(inputSample->AddBuffer(inputMediaBuffer));

    // TODO: check if we don't need to convert it and just return (or possibly w/ also setting attrs)

    // Now we are ready to convert it to the requested media type, so we need to find a suitable jpg encoder
    MFT_REGISTER_TYPE_INFO inputFilter = {MFMediaType_Video, MFVideoFormat_MJPG};
    MFT_REGISTER_TYPE_INFO outputFilter = {MFMediaType_Video, {}};
    RETURN_IF_FAILED(sampleMediaType->GetGUID(MF_MT_SUBTYPE, &outputFilter.guidSubtype));

    //if(!(reinterpret_cast<uintptr_t>(&inputFilter) & 0x1)) return nullptr; // TODO: DEBUG ONLY

    IMFActivate ** ppActivate = nullptr;
    UINT32 count = 0;

    RETURN_IF_FAILED(MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER, MFT_ENUM_FLAG_SYNCMFT, &inputFilter, &outputFilter, &ppActivate, &count));
    ComPtr<IMFTransform> decoder;

    bool decoderActivated = false;
    for(UINT32 i = 0; i < count; ++i)
    {
      if(!decoderActivated && !FAILED(ppActivate[i]->ActivateObject(IID_PPV_ARGS(&decoder))))
      {
        decoderActivated = true;
      }
      ppActivate[i]->Release();
    }
    if(count)
    {
      CoTaskMemFree(ppActivate);
    }
    if(!decoderActivated)
    {
      return nullptr;
    }

    // Set input/output types for the decoder
    ComPtr<IMFMediaType> jpgFrameMediaType;
    RETURN_IF_FAILED(MFCreateMediaType(&jpgFrameMediaType));
    RETURN_IF_FAILED(jpgFrameMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
    RETURN_IF_FAILED(jpgFrameMediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_MJPG));
    RETURN_IF_FAILED(jpgFrameMediaType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
    RETURN_IF_FAILED(jpgFrameMediaType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE));
    RETURN_IF_FAILED(MFSetAttributeSize(jpgFrameMediaType.Get(), MF_MT_FRAME_SIZE, targetWidth, targetHeight));
    RETURN_IF_FAILED(MFSetAttributeRatio(jpgFrameMediaType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1));
    RETURN_IF_FAILED(decoder->SetInputType(0, jpgFrameMediaType.Get(), 0));
    RETURN_IF_FAILED(decoder->SetOutputType(0, sampleMediaType, 0));

    // Process the input sample
    RETURN_IF_FAILED(decoder->ProcessInput(0, inputSample.Get(), 0));

    // Check whether we need to allocate output sample and buffer ourselves
    MFT_OUTPUT_STREAM_INFO outputStreamInfo{};
    RETURN_IF_FAILED(decoder->GetOutputStreamInfo(0, &outputStreamInfo));
    const bool onlyProvidesSamples = outputStreamInfo.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES;
    const bool canProvideSamples = outputStreamInfo.dwFlags & MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES;
    const bool mustAllocateSample = (!onlyProvidesSamples && !canProvideSamples)
      || (!onlyProvidesSamples && (outputStreamInfo.dwFlags & MFT_PROCESS_OUTPUT_DISCARD_WHEN_NO_BUFFER));

    MFT_OUTPUT_DATA_BUFFER outputSamples{};
    IMFSample * outputSample = nullptr;

    // If so, do the allocation
    if(mustAllocateSample)
    {
      RETURN_IF_FAILED(MFCreateSample(&outputSample));
      RETURN_IF_FAILED(outputSample->SetSampleDuration(333333));
      IMFMediaBuffer * outputMediaBuffer = nullptr;
      RETURN_IF_FAILED(MFCreateAlignedMemoryBuffer(outputStreamInfo.cbSize, outputStreamInfo.cbAlignment - 1, &outputMediaBuffer));
      RETURN_IF_FAILED(outputMediaBuffer->SetCurrentLength(outputStreamInfo.cbSize));
      RETURN_IF_FAILED(outputSample->AddBuffer(outputMediaBuffer));
      outputSamples.pSample = outputSample;
    }

    // Finally, produce the output sample 
    DWORD processStatus = 0;
    RETURN_IF_FAILED(decoder->ProcessOutput(0, 1, &outputSamples, &processStatus));
    if(outputSamples.pEvents)
    {
      outputSamples.pEvents->Release();
    }
    MFShutdownObject(decoder.Get());
    return ComPtr<IMFSample>{outputSamples.pSample};
}