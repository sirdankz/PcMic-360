#include "pch.h"
#include "streamer.h"
#include "xexscope_probe.h"
#include <winsockx.h>

#define V704_TCP_PORT 36000

// v7.04 keeps the proven v7.03 network direction:
//   Xbox 360 = TCP server/listener
//   PC GUI   = TCP client
// This removes the hardcoded PC IP and lets each user enter only their Xbox IP.
static void ConfigureSocket(SOCKET s)
{
    BOOL yes=TRUE;
    setsockopt(s,SOL_SOCKET,SO_REUSEADDR,(const char*)&yes,sizeof(yes));

    // Preserve the Xbox-specific socket options used by the proven v7.02
    // transport.  Unsupported options are harmless here; successful titles
    // already used these settings on the client socket.
    setsockopt(s,SOL_SOCKET,0x5802,(const char*)&yes,sizeof(yes));
    setsockopt(s,SOL_SOCKET,0x5801,(const char*)&yes,sizeof(yes));
    setsockopt(s,IPPROTO_TCP,TCP_NODELAY,(const char*)&yes,sizeof(yes));
}

static DWORD WINAPI ServerThread(LPVOID)
{
    while(!(XNetGetEthernetLinkStatus() & XNET_ETHERNET_LINK_ACTIVE))
        Sleep(500);

    XNetStartupParams xn;
    ZeroMemory(&xn,sizeof(xn));
    xn.cfgSizeOfStruct=sizeof(xn);
    xn.cfgSockDefaultRecvBufsizeInK=128;
    xn.cfgSockDefaultSendBufsizeInK=128;
    xn.cfgFlags=XNET_STARTUP_BYPASS_SECURITY;
    if(XNetStartup(&xn)!=0) return 1;

    WSADATA wd;
    if(WSAStartup(MAKEWORD(2,2),&wd)!=0) return 2;

    Sleep(500);

    for(;;){
        SOCKET listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
        if(listener==INVALID_SOCKET){
            Sleep(1000);
            continue;
        }

        ConfigureSocket(listener);

        sockaddr_in a;
        ZeroMemory(&a,sizeof(a));
        a.sin_family=AF_INET;
        a.sin_addr.s_addr=0; // INADDR_ANY
        a.sin_port=htons(V704_TCP_PORT);

        if(bind(listener,(sockaddr*)&a,sizeof(a))!=0){
            closesocket(listener);
            Sleep(1000);
            continue;
        }

        if(listen(listener,1)!=0){
            closesocket(listener);
            Sleep(1000);
            continue;
        }

        // Stay alive for the lifetime of the loaded XEX.  The PC can
        // disconnect/reconnect without reloading the module.
        for(;;){
            SOCKET s=accept(listener,NULL,NULL);
            if(s==INVALID_SOCKET){
                closesocket(listener);
                Sleep(500);
                break;
            }

            ConfigureSocket(s);
            SendXexScopeProbe(s);

            closesocket(s);
            Sleep(100);
        }
    }
}

bool StreamerStart()
{
    DWORD tid=0;
    return CreateThread(NULL,0,ServerThread,NULL,0,&tid)!=NULL;
}
