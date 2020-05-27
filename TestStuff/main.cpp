#include "../VideoConference/TextureLoader.h"

#include <iostream>
#include <exception>

#pragma optimize("", off)
int main()
{
  const wchar_t shmemEndpoint[] = L"Global\\PowerToysWebcamMuteSwitch";
  auto hMapFile = OpenFileMappingW(
    FILE_MAP_ALL_ACCESS,   
    FALSE,                 
    shmemEndpoint);        
  if(!hMapFile)
  {
    std::cout << "couldn't open shmem channel: " << std::system_category().message(GetLastError()) << std::endl;
    return 1;
  }
  auto pBuf = (uint8_t * )MapViewOfFile(hMapFile, 
    FILE_MAP_ALL_ACCESS,  
    0,
    0,
    1);

  if(!pBuf)
  {
    std::cout << "couldn't map shmem channel: " << std::system_category().message(GetLastError()) << std::endl;
    return 1;
  }
  while(1)
  {
    const size_t val = *pBuf;
    std::cout << "input toggle value (currently " << val  << ")> ";
    size_t toggle_val = 0;
    std::cin >> toggle_val;
    *pBuf = toggle_val;
    FlushViewOfFile(pBuf, 1);
  }
  
  return 0;
}