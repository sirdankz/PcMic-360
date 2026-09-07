#include "pch.h"
#include "streamer.h"

extern "C" __declspec(dllexport) DWORD Xbox360Stream_Ping()
{
    return 0x56373034; // V704
}

BOOL APIENTRY DllMain(HANDLE hModule,DWORD reason,LPVOID reserved)
{
    if(reason==DLL_PROCESS_ATTACH)
        StreamerStart();
    return TRUE;
}
