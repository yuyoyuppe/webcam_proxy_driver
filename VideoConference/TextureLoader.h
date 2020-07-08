#pragma once

#include <d3d11.h>

#include <memory>
#include <string_view>
#include <optional>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4005)
#include <stdint.h>
#pragma warning(pop)

HRESULT CreateWICTextureFromMemory(_In_ ID3D11Device * d3dDevice,
  _In_opt_ ID3D11DeviceContext * d3dContext,
  _In_bytecount_(wicDataSize) const uint8_t * wicData,
  _In_ size_t wicDataSize,
  _Out_opt_ ID3D11Resource ** texture,
  _Out_opt_ ID3D11ShaderResourceView ** textureView,
  _In_ size_t maxsize = 0
);

HRESULT CreateWICTextureFromFile(_In_ ID3D11Device * d3dDevice,
  _In_opt_ ID3D11DeviceContext * d3dContext,
  _In_z_ const wchar_t * szFileName,
  _Out_opt_ ID3D11Resource ** texture,
  _Out_opt_ ID3D11ShaderResourceView ** textureView,
  _In_ size_t maxsize = 0
);


struct LoadedMJPG
{
  std::vector<uint8_t> buffer;
  size_t bufferSize;
  UINT width;
  UINT height;
};

std::optional<LoadedMJPG> LoadImageAsSample(std::wstring_view fileName, const UINT targetWidth, const UINT targetHeight);
