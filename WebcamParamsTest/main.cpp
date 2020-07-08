#include "../VideoConference/SimpleMediaStream.h"
#include "../VideoConference/TextureLoader.h"

#include <iostream>
#include <vector>
#include <algorithm>
#include <comutil.h>

#pragma optimize("", off)

ComPtr<IMFMediaType> SelectBestMediaType(IMFSourceReader * reader);

//ComPtr<IMFSample> OpenImage(std::wstring_view filePath, ComPtr<IMFMediaType> outputFormat)
//{
//    ComPtr<IMFSourceReader> imageReader;
//    HRESULT hr = MFCreateSourceReaderFromURL(filePath.data(), nullptr, &imageReader);
//
//    return {};
//}

#ifdef RETURN_IF_FAILED
#undef RETURN_IF_FAILED
#endif
#define RETURN_IF_FAILED( val )                                                     \
    if ( FAILED( val ) )                                                            \
    {                                                                               \
        return std::nullopt;                                                        \
    }

std::optional<ComPtr<IMFSample>> EncodeJpgFrame(const LoadedMJPG & image, IMFMediaType * outputSampleMediaType)
{
  MFT_REGISTER_TYPE_INFO inputFilter = {MFMediaType_Video, MFVideoFormat_MJPG};
  MFT_REGISTER_TYPE_INFO outputFilter = {MFMediaType_Video, {}};
  RETURN_IF_FAILED(outputSampleMediaType->GetGUID(MF_MT_SUBTYPE, &outputFilter.guidSubtype));

  IMFActivate ** ppActivate = NULL;
  UINT32 count = 0;

  // Find a suitable encoder
  HRESULT r = MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER, MFT_ENUM_FLAG_SYNCMFT, &inputFilter, &outputFilter, &ppActivate, &count);
  ComPtr<IMFTransform> decoder;

  bool activated = false;
  for(UINT32 i = 0; i < count; ++i)
  {
    if(!activated && !FAILED(ppActivate[i]->ActivateObject(IID_PPV_ARGS(&decoder))))
    {
      activated = true;
    }
    ppActivate[i]->Release();
  }
  CoTaskMemFree(ppActivate);
  if(!activated)
  {
    return std::nullopt;
  }

  // Set input/output types for the encoder
  ComPtr<IMFMediaType> jpgFrameMediaType;
  RETURN_IF_FAILED(MFCreateMediaType(&jpgFrameMediaType));
  RETURN_IF_FAILED(jpgFrameMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
  RETURN_IF_FAILED(jpgFrameMediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_MJPG));
  RETURN_IF_FAILED(jpgFrameMediaType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
  RETURN_IF_FAILED(jpgFrameMediaType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE));
  RETURN_IF_FAILED(MFSetAttributeSize(jpgFrameMediaType.Get(), MF_MT_FRAME_SIZE, image.width, image.height));
  RETURN_IF_FAILED(MFSetAttributeRatio(jpgFrameMediaType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1));
  RETURN_IF_FAILED(decoder->SetInputType(0, jpgFrameMediaType.Get(), 0));
  RETURN_IF_FAILED(decoder->SetOutputType(0, outputSampleMediaType, 0));

  // Create a sample from the input image buffer
  ComPtr<IMFSample> inputSample;
  RETURN_IF_FAILED(MFCreateSample(&inputSample));

  IMFMediaBuffer * inputMediaBuffer = nullptr;
  RETURN_IF_FAILED(MFCreateAlignedMemoryBuffer(static_cast<DWORD>(image.bufferSize), MF_64_BYTE_ALIGNMENT, &inputMediaBuffer));
  BYTE * inputBuf = nullptr;
  DWORD max_length = 0, current_length = 0;
  RETURN_IF_FAILED(inputMediaBuffer->Lock(&inputBuf, &max_length, &current_length));
  if(max_length < image.bufferSize)
  {
    return std::nullopt;
  }
  std::copy(image.buffer.data(), image.buffer.data() + image.bufferSize, inputBuf);
  RETURN_IF_FAILED(inputMediaBuffer->Unlock());
  RETURN_IF_FAILED(inputMediaBuffer->SetCurrentLength(image.bufferSize));
  RETURN_IF_FAILED(inputSample->AddBuffer(inputMediaBuffer));
  
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
  return ComPtr<IMFSample>{outputSamples.pSample};
}

int main()
{
  CoInitialize(nullptr);
  MFStartup(MF_VERSION);

  DeviceList dl;
  dl.Clear();
  dl.EnumerateDevices();
  const auto cnt = dl.Count();
  IMFActivate * activate = nullptr;
  dl.GetDevice(0, &activate);

  IMFMediaSource * pSource = NULL;

  // Create the media source for the device.
  HRESULT hr = activate->ActivateObject(
    __uuidof(IMFMediaSource),
    (void **)&pSource);

  IMFAttributes * pAttributes = NULL;

  hr = MFCreateAttributes(&pAttributes, 2);

  IMFSourceReader * reader = nullptr;
  hr = MFCreateSourceReaderFromMediaSource(
    pSource,
    pAttributes,
    &reader);

  auto type = SelectBestMediaType(reader);

  static const auto image = LoadImageAsSample(LR"(P:\wecam_test_1920.jpg)", 1920, 1080);
  //FILE * f{};
  //_wfopen_s(&f, LR"(R:\reencoded.jpg)", L"wb");
  //fwrite(image->buffer.data(), image->bufferSize, 1, f);
  //fclose(f);

  ComPtr<IMFMediaType> webcamNativeMediaType;
  MFCreateMediaType(&webcamNativeMediaType);
  webcamNativeMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  webcamNativeMediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_YUY2);
  webcamNativeMediaType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
  webcamNativeMediaType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
  MFSetAttributeSize(webcamNativeMediaType.Get(), MF_MT_FRAME_SIZE, image->width, image->height);
  MFSetAttributeRatio(webcamNativeMediaType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);

  EncodeJpgFrame(*image, webcamNativeMediaType.Get());
  return 0;
}