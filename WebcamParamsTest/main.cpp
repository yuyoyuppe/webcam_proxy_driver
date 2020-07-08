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

std::vector<GUID> videoFormats = {
  MFVideoFormat_Base,
  MFVideoFormat_RGB32,
  MFVideoFormat_ARGB32,
  MFVideoFormat_RGB24,
  MFVideoFormat_RGB555,
  MFVideoFormat_RGB565,
  MFVideoFormat_RGB8,
  MFVideoFormat_L8,
  MFVideoFormat_L16,
  MFVideoFormat_D16,
  MFVideoFormat_AI44,
  MFVideoFormat_AYUV,
  MFVideoFormat_YUY2,
  MFVideoFormat_YVYU,
  MFVideoFormat_YVU9,
  MFVideoFormat_UYVY,
  MFVideoFormat_NV11,
  MFVideoFormat_NV12,
  MFVideoFormat_YV12,
  MFVideoFormat_I420,
  MFVideoFormat_IYUV,
  MFVideoFormat_Y210,
  MFVideoFormat_Y216,
  MFVideoFormat_Y410,
  MFVideoFormat_Y416,
  MFVideoFormat_Y41P,
  MFVideoFormat_Y41T,
  MFVideoFormat_Y42T,
  MFVideoFormat_P210,
  MFVideoFormat_P216,
  MFVideoFormat_P010,
  MFVideoFormat_P016,
  MFVideoFormat_v210,
  MFVideoFormat_v216,
  MFVideoFormat_v410,
  MFVideoFormat_MP43,
  MFVideoFormat_MP4S,
  MFVideoFormat_M4S2,
  MFVideoFormat_MP4V,
  MFVideoFormat_WMV1,
  MFVideoFormat_WMV2,
  MFVideoFormat_WMV3,
  MFVideoFormat_WVC1,
  MFVideoFormat_MSS1,
  MFVideoFormat_MSS2,
  MFVideoFormat_MPG1,
  MFVideoFormat_DVSL,
  MFVideoFormat_DVSD,
  MFVideoFormat_DVHD,
  MFVideoFormat_DV25,
  MFVideoFormat_DV50,
  MFVideoFormat_DVH1,
  MFVideoFormat_DVC,
  MFVideoFormat_H264,
};

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


  static const auto image = LoadImageFromFile(LR"(P:\wecam_test_1920.jpg)", 320, 240);
  //FILE * f{};
  //_wfopen_s(&f, LR"(R:\reencoded.jpg)", L"wb");
  //fwrite(image->buffer.data(), image->bufferSize, 1, f);
  //fclose(f);

  MFT_REGISTER_TYPE_INFO outputFilter = {MFMediaType_Video, MFVideoFormat_YUY2};
  //MFT_REGISTER_TYPE_INFO inputFilter = {MFMediaType_Video, };
  UINT32 unFlags = MFT_ENUM_FLAG_ALL;

  IMFActivate ** ppActivate = NULL;
  UINT32 count = 0;

  HRESULT r = MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER, unFlags, nullptr, &outputFilter, &ppActivate, &count);
  std::vector<ComPtr<IMFMediaType>> supportedInputTypes;
  for(UINT32 i = 0; i < count; ++i)
  {
      ComPtr<IMFTransform> decoder;
      ppActivate[i]->ActivateObject(IID_PPV_ARGS(&decoder));
      /*decoder->GetInputAvailableType(0,);*/
      for(DWORD tyIdx = 0;; ++tyIdx)
      {
        ComPtr<IMFMediaType> next_type;
        HRESULT hr = decoder->GetInputAvailableType(0, tyIdx, &next_type);
        if(next_type)
          supportedInputTypes.emplace_back(std::move(next_type));
        if(hr == MF_E_NO_MORE_TYPES || FAILED(hr) || !next_type)
        {
          break;
        }
      continue;
      }
  }
  for(const auto sit : supportedInputTypes)
  {
    GUID sourceSubtype = {};
    RETURN_IF_FAILED(sit->GetGUID(MF_MT_SUBTYPE, &sourceSubtype));
    UINT32 sourceFourCC = *((DWORD *)&sourceSubtype); 
    uint8_t * fourCCbytes = (uint8_t * )&sourceFourCC;
    printf("%c%c%c%c \n", fourCCbytes[0], fourCCbytes[1], fourCCbytes[2], fourCCbytes[3]);

  }
  return 0;
}