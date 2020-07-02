#include "../VideoConference/SimpleMediaStream.h"

#include <iostream>
#include <vector>
#include <algorithm>
#include <comutil.h>

#pragma optimize("", off)

ComPtr<IMFMediaType> SelectBestMediaType(IMFSourceReader * reader);

int main()
{
  CoInitialize(nullptr);
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

  return 0;
}