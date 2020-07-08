#include "../VideoConference/SimpleMediaStream.h"
#include "../VideoConference/TextureLoader.h"

#include <iostream>
#include <vector>
#include <algorithm>
#include <comutil.h>

#pragma optimize("", off)

ComPtr<IMFMediaType> SelectBestMediaType(IMFSourceReader * reader);

int main()
{
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  MFStartup(MF_VERSION);

  DeviceList dl;
  dl.Clear();
  dl.EnumerateDevices();
  const auto cnt = dl.Count();
  IMFActivate * activate = nullptr;
  dl.GetDevice(0, &activate);

  IMFMediaSource * pSource = nullptr;

  // Create the media source for the device.
  HRESULT hr = activate->ActivateObject(
    __uuidof(IMFMediaSource),
    (void **)&pSource);

  IMFAttributes * pAttributes = nullptr;

  hr = MFCreateAttributes(&pAttributes, 2);

  IMFSourceReader * reader = nullptr;
  hr = MFCreateSourceReaderFromMediaSource(
    pSource,
    pAttributes,
    &reader);

  auto type = SelectBestMediaType(reader);

  
  ComPtr<IMFMediaType> webcamNativeMediaType;
  MFCreateMediaType(&webcamNativeMediaType);
  webcamNativeMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  webcamNativeMediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_YUY2);
  webcamNativeMediaType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
  webcamNativeMediaType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
  MFSetAttributeSize(webcamNativeMediaType.Get(), MF_MT_FRAME_SIZE, 1920, 1080);
  MFSetAttributeRatio(webcamNativeMediaType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);

  const auto imageSample = LoadImageAsSample(LR"(P:\wecam_test_1920.jpg)", webcamNativeMediaType.Get());

  MFShutdown();
  return 0;
}