#include "pch.h"
#include "xexscope_probe.h"
#include <winsockx.h>
#include <stdio.h>
#include <stdarg.h>
#pragma warning(disable:4505)

extern "C" {
    DWORD XexGetModuleHandle(IN PSZ moduleName, IN OUT PHANDLE hand);
    DWORD XexGetProcedureAddress(IN HANDLE hand, IN DWORD ordinal, OUT PVOID* address);
    BOOL MmIsAddressValid(unsigned __int64 Address);
}

static void SendAll(SOCKET s,const char* text)
{
    if(!text) return;
    int len=(int)strlen(text), sent=0;
    while(sent<len) { int n=send(s,text+sent,len-sent,0); if(n<=0) return; sent+=n; }
}

static void SendLine(SOCKET s,const char* fmt,...)
{
    char line[896]; va_list ap; va_start(ap,fmt);
    _vsnprintf_s(line,sizeof(line),_TRUNCATE,fmt,ap); va_end(ap);
    strcat_s(line,sizeof(line),"\r\n"); SendAll(s,line);
}

// Confirmed by v4 on this RuntimeHost build.
static volatile DWORD* const STUB_CREATE  = (volatile DWORD*)0x8A7B4434;
static volatile DWORD* const STUB_SUBMIT  = (volatile DWORD*)0x8A7B4424; // observe only in v5
static volatile DWORD* const STUB_CLOSE   = (volatile DWORD*)0x8A7B4414;
static volatile DWORD* const STUB_HEADSET = (volatile DWORD*)0x8A7B4404;

static const DWORD EXPECT_CREATE[4]  = {0x3D608170,0x396BC098,0x7D6903A6,0x4E800420};
static const DWORD EXPECT_SUBMIT[4]  = {0x3D608170,0x396BB498,0x7D6903A6,0x4E800420};
static const DWORD EXPECT_CLOSE[4]   = {0x3D608170,0x396BC1C8,0x7D6903A6,0x4E800420};
static const DWORD EXPECT_HEADSET[4] = {0x3D608170,0x396BB418,0x7D6903A6,0x4E800420};

static const DWORD ADDR_CREATE  = 0x816FC098;
static const DWORD ADDR_CLOSE   = 0x816FC1C8;
static const DWORD ADDR_HEADSET = 0x816FB418;

typedef DWORD (*PFN_XAMVOICECREATE)(DWORD,DWORD,DWORD*);
typedef DWORD (*PFN_XAMVOICECLOSE)(VOID*);
typedef DWORD (*PFN_XAMVOICEHEADSETPRESENT)(VOID*);
typedef VOID (*PFN_SWEEP)(VOID*,DWORD);

static PFN_XAMVOICECREATE g_origCreate=(PFN_XAMVOICECREATE)ADDR_CREATE;
static PFN_XAMVOICECLOSE g_origClose=(PFN_XAMVOICECLOSE)ADDR_CLOSE;
static PFN_XAMVOICEHEADSETPRESENT g_origHeadset=(PFN_XAMVOICEHEADSETPRESENT)ADDR_HEADSET;
static PFN_SWEEP g_sweepD=0;
static PFN_SWEEP g_sweepI=0;

static volatile LONG g_installed=0;
static volatile LONG g_createCalls=0;
static volatile LONG g_closeCalls=0;
static volatile LONG g_headsetCalls=0;
static volatile LONG g_submitCalls=0;
static volatile DWORD g_submitR3=0;
static volatile DWORD g_submitR4=0;
static volatile DWORD g_submitR5=0;
static volatile DWORD g_submitR6=0;
static volatile DWORD g_submitR7=0;
static volatile DWORD g_submitR8=0;
static volatile DWORD g_submitRet=0;
static volatile LONG g_submitChanged=0;
static volatile LONG g_phase=0; // 0 idle, 1 disconnected, 2 connected-quiet, 3 talking
static volatile DWORD g_r5Prev=0;
static volatile LONG g_r5Stride1C=0;
static volatile LONG g_r5Same=0;
static volatile LONG g_r5OtherDelta=0;
static volatile DWORD g_r5Min=0xFFFFFFFF;
static volatile DWORD g_r5Max=0;
static volatile LONG g_r5In4025Window=0;
static volatile LONG g_r7_02009030=0;
static volatile LONG g_r7_0200B030=0;
static volatile LONG g_r7_4024B760=0;
static volatile LONG g_activeFamilyCalls=0;
static volatile DWORD g_activeLastR3=0,g_activeLastR5=0,g_activeLastR7=0,g_activeLastR8=0;
static volatile LONG g_activeR5Valid=0,g_activeR7Valid=0,g_activeR8Valid=0;
static volatile LONG g_activeR5PageValid=0,g_activeR7PageValid=0,g_activeR8PageValid=0;
static volatile DWORD g_activeR8First=0;
static volatile DWORD g_activeR8Last=0;
static volatile LONG g_activeR8Changes=0;
static SOCKET g_diagSocket=INVALID_SOCKET;
static volatile LONG g_descSnapshots=0;
static volatile DWORD g_descLastR5=0;
static volatile DWORD g_descLastR8=0;
static volatile LONG g_payloadSamples=0;
static volatile LONG g_payloadHashChanges=0;
static volatile LONG g_payloadNonZero=0;
static volatile LONG g_payloadUniqueApprox=0;
static volatile DWORD g_payloadLastHash=0;
static volatile DWORD g_payloadLastPtr=0;
static volatile DWORD g_payloadLastSize=0;
static volatile DWORD g_payloadLastA0=0;
static volatile LONG g_zeroGraphSamples=0;
static volatile DWORD g_zeroLastR5=0;
static volatile DWORD g_zeroLastR7=0;
static volatile DWORD g_zeroHashR5=0;
static volatile DWORD g_zeroHashR7=0;
static volatile LONG g_zeroHashR5Changes=0;
static volatile LONG g_zeroHashR7Changes=0;
static volatile LONG g_captureRegCalls=0;
static volatile DWORD g_captureRegR3=0,g_captureRegR4=0,g_captureRegR5=0,g_captureRegR6=0,g_captureRegR7=0,g_captureRegR8=0;
static volatile DWORD g_captureRegRet=0;
static volatile DWORD g_captureStub=0;
static volatile DWORD g_captureStubOriginal[4]={0,0,0,0};
static volatile LONG g_captureInstalled=0;








struct FAMILY_STATS {
    volatile LONG calls;
    volatile DWORD lastR3,lastR4,lastR5,lastR6,lastR7,lastR8,lastRet;
    volatile DWORD lastReadable5,lastReadable7;
    volatile DWORD hash5,hash7;
    volatile LONG hash5Changes,hash7Changes;
    volatile LONG samples5,samples7;
};

static FAMILY_STATS g_family0={0}; // r4==0
static FAMILY_STATS g_family1={0}; // r4==1
static FAMILY_STATS g_familyOther={0};

static FAMILY_STATS* PickFamily(DWORD r4)
{
    if(r4==0) return &g_family0;
    if(r4==1) return &g_family1;
    return &g_familyOther;
}

static DWORD Hash64Safe(DWORD ptr, BOOL* ok)
{
    *ok=FALSE;
    if(ptr<0x80000000 || !MmIsAddressValid(ptr) || !MmIsAddressValid(ptr+63)) return 0;
    DWORD h=2166136261u;
    __try {
        const BYTE* p=(const BYTE*)ptr;
        for(int i=0;i<64;i++) { h^=p[i]; h*=16777619u; }
        *ok=TRUE;
        return h;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

static volatile LONG g_lastCreateRc=0;
static volatile LONG g_lastHeadsetRc=0;
static volatile LONG g_lastHeadsetChanged=0;
static volatile DWORD g_lastVoicePtr=0;
static volatile DWORD g_lastCreateUnk1=0;
static volatile DWORD g_lastCreateUnk2=0;
static volatile DWORD g_lastClosePtr=0;
static volatile DWORD g_lastHeadsetPtr=0;

static DWORD Hook_XamVoiceCreate(DWORD a,DWORD b,DWORD* outVoice)
{
    InterlockedIncrement(&g_createCalls); g_lastCreateUnk1=a; g_lastCreateUnk2=b;
    DWORD rc=g_origCreate ? g_origCreate(a,b,outVoice) : 0xFFFFFFFF;
    g_lastCreateRc=(LONG)rc;
    if(outVoice && MmIsAddressValid((DWORD)outVoice)) { __try { g_lastVoicePtr=*outVoice; } __except(EXCEPTION_EXECUTE_HANDLER) {} }
    return rc;
}
static DWORD Hook_XamVoiceClose(VOID* voice)
{
    InterlockedIncrement(&g_closeCalls); g_lastClosePtr=(DWORD)voice;
    return g_origClose ? g_origClose(voice) : 0xFFFFFFFF;
}
static DWORD Hook_XamVoiceHeadsetPresent(VOID* voice)
{
    InterlockedIncrement(&g_headsetCalls); g_lastHeadsetPtr=(DWORD)voice;
    DWORD rc=g_origHeadset ? g_origHeadset(voice) : 0;
    LONG old=InterlockedExchange(&g_lastHeadsetRc,(LONG)rc);
    if((DWORD)old!=rc) InterlockedExchange(&g_lastHeadsetChanged,1);
    return rc;
}


extern "C" 

static DWORD HashMappedBuffer(DWORD addr,DWORD bytes,LONG* nonZero,LONG* transitions)
{
    if(nonZero) *nonZero=0;
    if(transitions) *transitions=0;
    if(bytes==0 || bytes>0x280) return 0;

    DWORD h=2166136261u;
    BYTE prev=0;
    BOOL havePrev=FALSE;

    for(DWORD i=0;i<bytes;i++) {
        DWORD a=addr+i;
        if(!MmIsAddressValid(a)) return 0;
        BYTE v=*(volatile BYTE*)a;
        h^=v; h*=16777619u;
        if(nonZero && v) (*nonZero)++;
        if(transitions && havePrev && v!=prev) (*transitions)++;
        prev=v; havePrev=TRUE;
    }
    return h;
}

static void AnalyzePayload(SOCKET s,DWORD desc)
{
    if(s==INVALID_SOCKET || !MmIsAddressValid(desc) || !MmIsAddressValid(desc+0x0C)) return;

    DWORD size=*(volatile DWORD*)(desc+0x04);
    DWORD ptr =*(volatile DWORD*)(desc+0x08);
    DWORD a0  =*(volatile DWORD*)(desc+0x0C);

    if(size==0 || size>0x280) return;
    if(!MmIsAddressValid(ptr) || !MmIsAddressValid(ptr+size-1)) return;

    LONG nz=0, trans=0;
    DWORD h=HashMappedBuffer(ptr,size,&nz,&trans);
    if(!h) return;

    InterlockedIncrement(&g_payloadSamples);
    if(g_payloadLastHash && g_payloadLastHash!=h) InterlockedIncrement(&g_payloadHashChanges);
    g_payloadLastHash=h;
    g_payloadLastPtr=ptr;
    g_payloadLastSize=size;
    g_payloadLastA0=a0;
    g_payloadNonZero=nz;
    g_payloadUniqueApprox=trans;

    SendLine(s,"PAYLOAD_SAMPLE phase=%ld desc=0x%08X ptr=0x%08X size=0x%X a0=0x%X hash=0x%08X nonzero=%ld transitions=%ld",
        g_phase,desc,ptr,size,a0,h,nz,trans);
}


static void SendMappedWords(SOCKET s,const char* tag,DWORD addr,DWORD bytes);

static DWORD HashMappedRegion(DWORD addr,DWORD bytes)
{
    if(bytes==0 || bytes>0x100) return 0;
    if(!MmIsAddressValid(addr) || !MmIsAddressValid(addr+bytes-1)) return 0;
    DWORD h=2166136261u;
    for(DWORD i=0;i<bytes;i++) {
        BYTE v=*(volatile BYTE*)(addr+i);
        h^=v; h*=16777619u;
    }
    return h;
}

static void InspectPointerFields(SOCKET s,const char* tag,DWORD base,DWORD bytes)
{
    if(s==INVALID_SOCKET || !MmIsAddressValid(base) || bytes>0x80) return;
    for(DWORD off=0;off<bytes;off+=4) {
        DWORD a=base+off;
        if(!MmIsAddressValid(a)) continue;
        DWORD v=*(volatile DWORD*)a;
        if(v>=0x40000000 && MmIsAddressValid(v)) {
            DWORD h=HashMappedRegion(v,0x40);
            SendLine(s,"%s_PTR base=0x%08X off=0x%02X ptr=0x%08X hash40=0x%08X",tag,base,off,v,h);
        }
    }
}

static void InspectR4Zero(SOCKET s,DWORD r5,DWORD r7)
{
    if(s==INVALID_SOCKET) return;
    if(!MmIsAddressValid(r5) || !MmIsAddressValid(r7)) return;

    LONG sample=InterlockedIncrement(&g_zeroGraphSamples);
    if(sample>24) return;

    DWORD h5=HashMappedRegion(r5,0x80);
    DWORD h7=HashMappedRegion(r7,0x80);

    if(g_zeroHashR5 && h5 && g_zeroHashR5!=h5) InterlockedIncrement(&g_zeroHashR5Changes);
    if(g_zeroHashR7 && h7 && g_zeroHashR7!=h7) InterlockedIncrement(&g_zeroHashR7Changes);

    g_zeroHashR5=h5;
    g_zeroHashR7=h7;
    g_zeroLastR5=r5;
    g_zeroLastR7=r7;

    SendLine(s,"R4ZERO_SAMPLE phase=%ld id=%ld r5=0x%08X r7=0x%08X hashR5=0x%08X hashR7=0x%08X",
        g_phase,sample,r5,r7,h5,h7);

    SendMappedWords(s,"R4ZERO_R5_WORDS",r5,0x40);
    SendMappedWords(s,"R4ZERO_R7_WORDS",r7,0x40);

    InspectPointerFields(s,"R4ZERO_R5",r5,0x40);
    InspectPointerFields(s,"R4ZERO_R7",r7,0x40);
}

static void SendMappedWords(SOCKET s,const char* tag,DWORD addr,DWORD bytes)
{
    if(bytes>0x40) bytes=0x40;
    if((bytes&3)!=0) bytes&=~3;
    if(bytes==0) return;

    // Every DWORD is individually validity-checked before reading.
    char line[640];
    int n=_snprintf(line,sizeof(line)-1,"%s base=0x%08X bytes=0x%X words=",tag,addr,bytes);
    if(n<0) return;
    line[sizeof(line)-1]=0;

    for(DWORD off=0;off<bytes;off+=4) {
        DWORD a=addr+off;
        if(!MmIsAddressValid(a)) {
            n += _snprintf(line+n,sizeof(line)-1-n,"%s????????",(off?",":""));
        } else {
            volatile DWORD* p=(volatile DWORD*)a;
            DWORD v=*p;
            n += _snprintf(line+n,sizeof(line)-1-n,"%s%08X",(off?",":""),v);
        }
        if(n<0 || n>(int)sizeof(line)-20) break;
    }
    line[sizeof(line)-1]=0;
    SendLine(s,"%s",line);
}

static void InspectActiveDescriptor(SOCKET s,DWORD r5,DWORD r8)
{
    if(s==INVALID_SOCKET) return;
    // Only inspect exact mapped addresses that passed the structural matcher.
    if(!MmIsAddressValid(r5) || !MmIsAddressValid(r8)) return;

    // Avoid flooding: snapshot only when the rotating r5 slot changes.
    if(g_descLastR5==r5 && g_descLastR8==r8) return;
    g_descLastR5=r5; g_descLastR8=r8;
    LONG snap=InterlockedIncrement(&g_descSnapshots);
    if(snap>24) return;

    SendLine(s,"DESC_SNAPSHOT id=%ld r5=0x%08X r8=0x%08X",snap,r5,r8);

    // 0x20 bytes at r5: enough to cover the apparent 0x1C record plus one DWORD.
    SendMappedWords(s,"R5_WORDS",r5,0x20);

    // Small stable-object window at r8; no recursive pointer following.
    SendMappedWords(s,"R8_WORDS",r8,0x40);
    AnalyzePayload(s,r5);
}

void RecordSubmitRegs(DWORD a,DWORD b,DWORD c,DWORD d,DWORD e,DWORD f)
{
    InterlockedIncrement(&g_submitCalls);
    g_submitR3=a; g_submitR4=b; g_submitR5=c;
    g_submitR6=d; g_submitR7=e; g_submitR8=f;

    DWORD prev=g_r5Prev;
    if(prev) {
        if(c==prev) InterlockedIncrement(&g_r5Same);
        else {
            LONG delta=(LONG)c-(LONG)prev;
            if(delta==0x1C || delta==-0x1C) InterlockedIncrement(&g_r5Stride1C);
            else InterlockedIncrement(&g_r5OtherDelta);
        }
    }
    g_r5Prev=c;
    if(c<g_r5Min) g_r5Min=c;
    if(c>g_r5Max) g_r5Max=c;
    if(c>=0x40250290 && c<=0x4025038C) InterlockedIncrement(&g_r5In4025Window);
    if(e==0x02009030) InterlockedIncrement(&g_r7_02009030);
    else if(e==0x0200B030) InterlockedIncrement(&g_r7_0200B030);
    else if(e==0x4024B760) InterlockedIncrement(&g_r7_4024B760);

    if(b==1 && d==0xFFFFFFFF && e==0x02009030 && c>=0x40250290 && c<=0x4025038C) {
        InterlockedIncrement(&g_activeFamilyCalls);
        if(g_activeR8First==0) g_activeR8First=f;
        if(g_activeR8Last!=0 && g_activeR8Last!=f) InterlockedIncrement(&g_activeR8Changes);
        g_activeR8Last=f;
        g_activeLastR3=a; g_activeLastR5=c; g_activeLastR7=e; g_activeLastR8=f;
        if(MmIsAddressValid(c)) InterlockedIncrement(&g_activeR5Valid);
        if(MmIsAddressValid(e)) InterlockedIncrement(&g_activeR7Valid);
        if(MmIsAddressValid(f)) InterlockedIncrement(&g_activeR8Valid);
        if(MmIsAddressValid(c & 0xFFFFF000)) InterlockedIncrement(&g_activeR5PageValid);
        if(MmIsAddressValid(e & 0xFFFFF000)) InterlockedIncrement(&g_activeR7PageValid);
        if(MmIsAddressValid(f & 0xFFFFF000)) InterlockedIncrement(&g_activeR8PageValid);
        InspectActiveDescriptor(g_diagSocket,c,f);
    }

    if(b==0 && d==0 && f==0x0000003F) {
        InspectR4Zero(g_diagSocket,c,e);
    }

    FAMILY_STATS* fs=PickFamily(b);
    InterlockedIncrement(&fs->calls);
    fs->lastR3=a; fs->lastR4=b; fs->lastR5=c; fs->lastR6=d;
    fs->lastR7=e; fs->lastR8=f;
    InterlockedExchange(&g_submitChanged,1);
}

extern "C" void RecordSubmitRet(DWORD rc)
{
    g_submitRet=rc;
    FAMILY_STATS* fs=PickFamily(g_submitR4);
    fs->lastRet=rc;
}

extern "C" __declspec(naked) DWORD Hook_XamVoiceSubmitPacket()
{
    __asm {
        stwu r1,-0x90(r1)
        mflr r0
        stw r0,0x94(r1)

        stw r3,0x20(r1)
        stw r4,0x24(r1)
        stw r5,0x28(r1)
        stw r6,0x2C(r1)
        stw r7,0x30(r1)
        stw r8,0x34(r1)

        bl RecordSubmitRegs

        lwz r3,0x20(r1)
        lwz r4,0x24(r1)
        lwz r5,0x28(r1)
        lwz r6,0x2C(r1)
        lwz r7,0x30(r1)
        lwz r8,0x34(r1)

        lis r11,0x8170
        addi r11,r11,-0x4B68
        mtctr r11
        bctrl

        stw r3,0x38(r1)
        bl RecordSubmitRet
        lwz r3,0x38(r1)

        lwz r0,0x94(r1)
        mtlr r0
        addi r1,r1,0x90
        blr
    }
}


static BOOL ReadWords(volatile DWORD* p,DWORD out[4]);
static BOOL PatchStub(volatile DWORD* stub,DWORD target);
static DWORD HashMappedRegion(DWORD addr,DWORD bytes);

extern "C" void RecordCaptureRegArgs(DWORD a,DWORD b,DWORD c,DWORD d,DWORD e,DWORD f)
{
    InterlockedIncrement(&g_captureRegCalls);
    g_captureRegR3=a; g_captureRegR4=b; g_captureRegR5=c;
    g_captureRegR6=d; g_captureRegR7=e; g_captureRegR8=f;
}

extern "C" void RecordCaptureRegRet(DWORD rc)
{
    g_captureRegRet=rc;
}

// Direct target resolved on the tested runtime.
#define XAM_VOICE_SET_AUDIO_CAPTURE_ROUTINE 0x816FBD58

extern "C" __declspec(naked) DWORD Hook_XamVoiceSetAudioCaptureRoutine()
{
    __asm {
        stwu r1,-0x90(r1)
        mflr r0
        stw r0,0x94(r1)

        stw r3,0x20(r1)
        stw r4,0x24(r1)
        stw r5,0x28(r1)
        stw r6,0x2C(r1)
        stw r7,0x30(r1)
        stw r8,0x34(r1)

        bl RecordCaptureRegArgs

        lwz r3,0x20(r1)
        lwz r4,0x24(r1)
        lwz r5,0x28(r1)
        lwz r6,0x2C(r1)
        lwz r7,0x30(r1)
        lwz r8,0x34(r1)

        lis r11,0x8170
        addi r11,r11,-0x42A8
        mtctr r11
        bctrl

        stw r3,0x38(r1)
        bl RecordCaptureRegRet
        lwz r3,0x38(r1)

        lwz r0,0x94(r1)
        mtlr r0
        addi r1,r1,0x90
        blr
    }
}

static BOOL LooksLikeImportStub(DWORD addr,DWORD target)
{
    if(!MmIsAddressValid(addr) || !MmIsAddressValid(addr+12)) return FALSE;
    DWORD w[4];
    if(!ReadWords((volatile DWORD*)addr,w)) return FALSE;
    // Match lis r11, hi ; addi r11,r11,lo ; mtctr r11 ; bctr
    if((w[0]&0xFFFF0000)!=0x3D600000) return FALSE;
    if((w[1]&0xFFFF0000)!=0x396B0000) return FALSE;
    if(w[2]!=0x7D6903A6 || w[3]!=0x4E800420) return FALSE;
    DWORD hi=w[0]&0xFFFF;
    SHORT lo=(SHORT)(w[1]&0xFFFF);
    DWORD rebuilt=(hi<<16)+(LONG)lo;
    return rebuilt==target;
}

static DWORD FindCaptureStub()
{
    // RuntimeHost image range observed on this title: 0x8A000000..0x8A880000.
    // Search code pages in 4-byte steps for the exact import-thunk shape targeting 0x816FBD58.
    for(DWORD a=0x8A000000;a<0x8A880000;a+=4) {
        if(LooksLikeImportStub(a,XAM_VOICE_SET_AUDIO_CAPTURE_ROUTINE)) return a;
    }
    return 0;
}

static BOOL InstallCaptureHook(SOCKET s)
{
    if(g_captureInstalled) {
        SendLine(s,"CAPTURE_HOOK_INSTALL already=1 stub=0x%08X",g_captureStub);
        return TRUE;
    }

    DWORD stub=FindCaptureStub();
    if(!stub) {
        SendLine(s,"CAPTURE_STUB_DISCOVERY found=0 target=0x%08X",XAM_VOICE_SET_AUDIO_CAPTURE_ROUTINE);
        return FALSE;
    }

    DWORD words[4];
    if(!ReadWords((volatile DWORD*)stub,words)) {
        SendLine(s,"CAPTURE_STUB_DISCOVERY found=1 stub=0x%08X read=0",stub);
        return FALSE;
    }

    g_captureStub=stub;
    for(int i=0;i<4;i++) g_captureStubOriginal[i]=words[i];

    SendLine(s,"CAPTURE_STUB_DISCOVERY found=1 target=0x%08X stub=0x%08X words=%08X,%08X,%08X,%08X",
        XAM_VOICE_SET_AUDIO_CAPTURE_ROUTINE,stub,words[0],words[1],words[2],words[3]);

    BOOL ok=PatchStub((volatile DWORD*)stub,(DWORD)&Hook_XamVoiceSetAudioCaptureRoutine);
    if(ok) {
        InterlockedExchange(&g_captureInstalled,1);
        SendLine(s,"CAPTURE_HOOK_INSTALL ok=1 stub=0x%08X hook=0x%08X",stub,(DWORD)&Hook_XamVoiceSetAudioCaptureRoutine);
        return TRUE;
    }

    SendLine(s,"CAPTURE_HOOK_INSTALL ok=0 stub=0x%08X",stub);
    return FALSE;
}

static BOOL RestoreCaptureHook(SOCKET s)
{
    if(!g_captureInstalled) {
        SendLine(s,"CAPTURE_HOOK_RESTORE already=0");
        return TRUE;
    }

    DWORD stub=g_captureStub;
    if(!stub) return FALSE;

    volatile DWORD* p=(volatile DWORD*)stub;
    for(int i=0;i<4;i++) p[i]=g_captureStubOriginal[i];

    // Use the same cache maintenance path as the known-good stub patcher.
    PatchStub((volatile DWORD*)stub,(DWORD)XAM_VOICE_SET_AUDIO_CAPTURE_ROUTINE);

    InterlockedExchange(&g_captureInstalled,0);
    SendLine(s,"CAPTURE_HOOK_RESTORE ok=1 stub=0x%08X",stub);
    return TRUE;
}

static void ReportCaptureStatus(SOCKET s)
{
    SendLine(s,"CAPTURE_STATUS installed=%ld calls=%ld stub=0x%08X ret=0x%08X r3=0x%08X r4=0x%08X r5=0x%08X r6=0x%08X r7=0x%08X r8=0x%08X",
        g_captureInstalled,g_captureRegCalls,g_captureStub,g_captureRegRet,
        g_captureRegR3,g_captureRegR4,g_captureRegR5,g_captureRegR6,g_captureRegR7,g_captureRegR8);

    DWORD vals[6]={g_captureRegR3,g_captureRegR4,g_captureRegR5,g_captureRegR6,g_captureRegR7,g_captureRegR8};
    for(int i=0;i<6;i++) {
        DWORD v=vals[i];
        BOOL mapped=MmIsAddressValid(v);
        BOOL runtimeCode=(v>=0x8A000000 && v<0x8A880000 && mapped);
        BOOL xamCode=(v>=0x81600000 && v<0x81800000 && mapped);
        SendLine(s,"CAPTURE_ARG index=r%d value=0x%08X mapped=%u runtimeCode=%u xamRange=%u",
            i+3,v,mapped?1:0,runtimeCode?1:0,xamCode?1:0);
        if(mapped && v>=0x40000000) {
            DWORD h=HashMappedRegion(v,0x40);
            SendLine(s,"CAPTURE_ARG_MEM index=r%d ptr=0x%08X hash40=0x%08X",i+3,v,h);
        }
    }
}


static BOOL ReadWords(volatile DWORD* p,DWORD out[4])
{
    if(!p) return FALSE;
    for(int i=0;i<4;i++) if(!MmIsAddressValid((DWORD)(p+i))) return FALSE;
    __try { for(int i=0;i<4;i++) out[i]=p[i]; return TRUE; } __except(EXCEPTION_EXECUTE_HANDLER) { return FALSE; }
}
static BOOL Same4(const DWORD a[4],const DWORD b[4]) { return a[0]==b[0]&&a[1]==b[1]&&a[2]==b[2]&&a[3]==b[3]; }

static void ResolveSweeps()
{
    HANDLE h=0; PVOID p=0;
    if(XexGetModuleHandle((PSZ)"xboxkrnl.exe",&h)==0 && h) {
        if(XexGetProcedureAddress(h,0xAA,&p)==0) g_sweepD=(PFN_SWEEP)p;
        p=0; if(XexGetProcedureAddress(h,0xAB,&p)==0) g_sweepI=(PFN_SWEEP)p;
    }
}
static void FlushCode(volatile DWORD* p,DWORD bytes)
{
    MemoryBarrier();
    if(g_sweepD) g_sweepD((VOID*)p,bytes);
    if(g_sweepI) g_sweepI((VOID*)p,bytes);
    MemoryBarrier();
}
static void EncodeTarget(DWORD target,DWORD* lis,DWORD* addi)
{
    DWORD hi=(target+0x8000)>>16; DWORD lo=target&0xFFFF;
    *lis=0x3D600000|(hi&0xFFFF);      // lis r11,hi
    *addi=0x396B0000|(lo&0xFFFF);    // addi r11,r11,lo
}
static BOOL PatchStub(volatile DWORD* stub,DWORD target)
{
    if(!stub || !MmIsAddressValid((DWORD)stub) || !MmIsAddressValid((DWORD)(stub+1))) return FALSE;
    DWORD a,b; EncodeTarget(target,&a,&b);
    __try { stub[0]=a; stub[1]=b; FlushCode(stub,16); return TRUE; } __except(EXCEPTION_EXECUTE_HANDLER) { return FALSE; }
}
static BOOL RestoreStub(volatile DWORD* stub,const DWORD orig[4])
{
    if(!stub) return FALSE;
    __try { stub[0]=orig[0]; stub[1]=orig[1]; FlushCode(stub,16); return TRUE; } __except(EXCEPTION_EXECUTE_HANDLER) { return FALSE; }
}

static void ReportStub(SOCKET s,const char* name,volatile DWORD* p,const DWORD expected[4])
{
    DWORD w[4]={0}; BOOL ok=ReadWords(p,w);
    SendLine(s,"VOICE_STUB name=%s address=0x%08X words=%08X,%08X,%08X,%08X valid=%u expected=%u",
        name,(DWORD)p,w[0],w[1],w[2],w[3],ok?1:0,(ok&&Same4(w,expected))?1:0);
}
static BOOL InstallHooks(SOCKET s)
{
    if(InterlockedCompareExchange(&g_installed,0,0)) { SendLine(s,"HOOK_INSTALL already=1"); return TRUE; }
    DWORD a[4],b[4],c[4],d[4];
    if(!ReadWords(STUB_CREATE,a)||!ReadWords(STUB_SUBMIT,b)||!ReadWords(STUB_CLOSE,c)||!ReadWords(STUB_HEADSET,d)) {
        SendLine(s,"HOOK_INSTALL ok=0 reason=STUB_READ_FAILED"); return FALSE;
    }
    if(!Same4(a,EXPECT_CREATE)||!Same4(b,EXPECT_SUBMIT)||!Same4(c,EXPECT_CLOSE)||!Same4(d,EXPECT_HEADSET)) {
        SendLine(s,"HOOK_INSTALL ok=0 reason=STUB_SIGNATURE_MISMATCH"); return FALSE;
    }
    BOOL pa=PatchStub(STUB_CREATE,(DWORD)&Hook_XamVoiceCreate);
    BOOL pb=PatchStub(STUB_SUBMIT,(DWORD)&Hook_XamVoiceSubmitPacket);
    BOOL pc=PatchStub(STUB_CLOSE,(DWORD)&Hook_XamVoiceClose);
    BOOL pd=PatchStub(STUB_HEADSET,(DWORD)&Hook_XamVoiceHeadsetPresent);
    if(!(pa&&pb&&pc&&pd)) {
        RestoreStub(STUB_CREATE,EXPECT_CREATE);
        RestoreStub(STUB_SUBMIT,EXPECT_SUBMIT);
        RestoreStub(STUB_CLOSE,EXPECT_CLOSE);
        RestoreStub(STUB_HEADSET,EXPECT_HEADSET);
        SendLine(s,"HOOK_INSTALL ok=0 reason=STUB_PATCH_FAILED create=%u submit=%u close=%u headset=%u",
            pa?1:0,pb?1:0,pc?1:0,pd?1:0); return FALSE;
    }
    InterlockedExchange(&g_installed,1);
    SendLine(s,"HOOK_INSTALL ok=1 mode=RUNTIMEHOST_STUB_V5_3 createStub=0x%08X createHook=0x%08X submitStub=0x%08X submitHook=0x%08X closeStub=0x%08X closeHook=0x%08X headsetStub=0x%08X headsetHook=0x%08X",
        (DWORD)STUB_CREATE,(DWORD)&Hook_XamVoiceCreate,(DWORD)STUB_SUBMIT,(DWORD)&Hook_XamVoiceSubmitPacket,
        (DWORD)STUB_CLOSE,(DWORD)&Hook_XamVoiceClose,(DWORD)STUB_HEADSET,(DWORD)&Hook_XamVoiceHeadsetPresent);
    return TRUE;
}
static void RestoreHooks(SOCKET s)
{
    if(!InterlockedCompareExchange(&g_installed,0,0)) { SendLine(s,"HOOK_RESTORE already=0"); return; }
    BOOL a=RestoreStub(STUB_CREATE,EXPECT_CREATE);
    BOOL b=RestoreStub(STUB_SUBMIT,EXPECT_SUBMIT);
    BOOL c=RestoreStub(STUB_CLOSE,EXPECT_CLOSE);
    BOOL d=RestoreStub(STUB_HEADSET,EXPECT_HEADSET);
    InterlockedExchange(&g_installed,0);
    SendLine(s,"HOOK_RESTORE ok=%u create=%u submit=%u close=%u headset=%u",
        (a&&b&&c&&d)?1:0,a?1:0,b?1:0,c?1:0,d?1:0);
}

static void SampleFamily(FAMILY_STATS* fs)
{
    BOOL ok5=FALSE,ok7=FALSE;
    DWORD h5=Hash64Safe(fs->lastR5,&ok5);
    DWORD h7=Hash64Safe(fs->lastR7,&ok7);
    if(ok5) {
        if(fs->samples5>0 && fs->hash5!=h5) InterlockedIncrement(&fs->hash5Changes);
        fs->hash5=h5; fs->lastReadable5=fs->lastR5; InterlockedIncrement(&fs->samples5);
    }
    if(ok7) {
        if(fs->samples7>0 && fs->hash7!=h7) InterlockedIncrement(&fs->hash7Changes);
        fs->hash7=h7; fs->lastReadable7=fs->lastR7; InterlockedIncrement(&fs->samples7);
    }
}

static void ReportFamily(SOCKET s,const char* name,FAMILY_STATS* fs)
{
    SendLine(s,"FAMILY name=%s phase=%ld calls=%ld r3=0x%08X r4=0x%08X r5=0x%08X r6=0x%08X r7=0x%08X r8=0x%08X ret=0x%08X r5Readable=0x%08X r7Readable=0x%08X r5Samples=%ld r5HashChanges=%ld r7Samples=%ld r7HashChanges=%ld",
        name,g_phase,fs->calls,fs->lastR3,fs->lastR4,fs->lastR5,fs->lastR6,fs->lastR7,fs->lastR8,fs->lastRet,
        fs->lastReadable5,fs->lastReadable7,fs->samples5,fs->hash5Changes,fs->samples7,fs->hash7Changes);
}

static void ReportStatus(SOCKET s)
{
    SendLine(s,"VOICE_STATUS installed=%ld createCalls=%ld submitCalls=%ld closeCalls=%ld headsetCalls=%ld voice=0x%08X createRc=0x%08X createA=0x%08X createB=0x%08X closePtr=0x%08X headsetRc=0x%08X headsetPtr=0x%08X",
        g_installed,g_createCalls,g_submitCalls,g_closeCalls,g_headsetCalls,g_lastVoicePtr,(DWORD)g_lastCreateRc,g_lastCreateUnk1,g_lastCreateUnk2,g_lastClosePtr,(DWORD)g_lastHeadsetRc,g_lastHeadsetPtr);
    SendLine(s,"SUBMIT_STATUS calls=%ld r3=0x%08X r4=0x%08X r5=0x%08X r6=0x%08X r7=0x%08X r8=0x%08X ret=0x%08X",
        g_submitCalls,g_submitR3,g_submitR4,g_submitR5,g_submitR6,g_submitR7,g_submitR8,g_submitRet);
    ReportFamily(s,"R4_ZERO",&g_family0);
    ReportFamily(s,"R4_ONE",&g_family1);
    ReportFamily(s,"R4_OTHER",&g_familyOther);
    SendLine(s,"R5_STRIDE phase=%ld min=0x%08X max=0x%08X in4025Window=%ld stride1C=%ld same=%ld otherDelta=%ld r7_02009030=%ld r7_0200B030=%ld r7_4024B760=%ld",
        g_phase,g_r5Min,g_r5Max,g_r5In4025Window,g_r5Stride1C,g_r5Same,g_r5OtherDelta,
        g_r7_02009030,g_r7_0200B030,g_r7_4024B760);
    SendLine(s,"ACTIVE_FAMILY phase=%ld calls=%ld r3=0x%08X r5=0x%08X r7=0x%08X r8=0x%08X r5Valid=%ld r7Valid=%ld r8Valid=%ld r5PageValid=%ld r7PageValid=%ld r8PageValid=%ld r8First=0x%08X r8Last=0x%08X r8Changes=%ld",
        g_phase,g_activeFamilyCalls,g_activeLastR3,g_activeLastR5,g_activeLastR7,g_activeLastR8,
        g_activeR5Valid,g_activeR7Valid,g_activeR8Valid,g_activeR5PageValid,g_activeR7PageValid,g_activeR8PageValid,
        g_activeR8First,g_activeR8Last,g_activeR8Changes);
    SendLine(s,"PAYLOAD_STATUS phase=%ld samples=%ld hashChanges=%ld ptr=0x%08X size=0x%X a0=0x%X lastHash=0x%08X nonzero=%ld transitions=%ld",
        g_phase,g_payloadSamples,g_payloadHashChanges,g_payloadLastPtr,g_payloadLastSize,g_payloadLastA0,
        g_payloadLastHash,g_payloadNonZero,g_payloadUniqueApprox);
    SendLine(s,"R4ZERO_STATUS phase=%ld samples=%ld r5=0x%08X r7=0x%08X hashR5=0x%08X hashR7=0x%08X hashR5Changes=%ld hashR7Changes=%ld",
        g_phase,g_zeroGraphSamples,g_zeroLastR5,g_zeroLastR7,g_zeroHashR5,g_zeroHashR7,
        g_zeroHashR5Changes,g_zeroHashR7Changes);
}


static void ResetFamily(FAMILY_STATS* fs)
{
    memset((void*)fs,0,sizeof(FAMILY_STATS));
}
static void StartPhase(SOCKET s,LONG phase)
{
    g_phase=phase;
    ResetFamily(&g_family0); ResetFamily(&g_family1); ResetFamily(&g_familyOther);
    g_r5Prev=0; g_r5Stride1C=0; g_r5Same=0; g_r5OtherDelta=0;
    g_r5Min=0xFFFFFFFF; g_r5Max=0; g_r5In4025Window=0;
    g_r7_02009030=0; g_r7_0200B030=0; g_r7_4024B760=0;
    g_activeFamilyCalls=0; g_activeLastR3=0; g_activeLastR5=0; g_activeLastR7=0; g_activeLastR8=0;
    g_activeR5Valid=0; g_activeR7Valid=0; g_activeR8Valid=0;
    g_activeR5PageValid=0; g_activeR7PageValid=0; g_activeR8PageValid=0;
    g_activeR8First=0; g_activeR8Last=0; g_activeR8Changes=0;
    g_descSnapshots=0; g_descLastR5=0; g_descLastR8=0;
    g_payloadSamples=0; g_payloadHashChanges=0; g_payloadNonZero=0; g_payloadUniqueApprox=0;
    g_payloadLastHash=0; g_payloadLastPtr=0; g_payloadLastSize=0; g_payloadLastA0=0;
    g_zeroGraphSamples=0; g_zeroLastR5=0; g_zeroLastR7=0;
    g_zeroHashR5=0; g_zeroHashR7=0; g_zeroHashR5Changes=0; g_zeroHashR7Changes=0;
    SendLine(s,"PHASE_START id=%ld",phase);
}


static const DWORD CAPTURE_TARGETS[4] = {
    0x816FBD58, // XamVoiceSetAudioCaptureRoutine
    0x816FBBE8, // XamVoiceGetMicArrayStatus
    0x816FBCA0, // XamVoiceGetMicArrayAudio
    0x816FBCF8  // XamVoiceGetMicArrayAudioEx
};

static const char* CaptureTargetName(DWORD v)
{
    if(v==0x816FBD58) return "XamVoiceSetAudioCaptureRoutine";
    if(v==0x816FBBE8) return "XamVoiceGetMicArrayStatus";
    if(v==0x816FBCA0) return "XamVoiceGetMicArrayAudio";
    if(v==0x816FBCF8) return "XamVoiceGetMicArrayAudioEx";
    return "UNKNOWN";
}

static const char* PtrClass(DWORD v)
{
    if(v>=0x80000000 && v<0x80200000) return "KERNEL";
    if(v>=0x81600000 && v<0x81800000) return "XAM";
    if(v>=0x82000000 && v<0x83000000) return "TITLE";
    if(v>=0x8A000000 && v<0x8A880000) return "RUNTIMEHOST";
    if(v>=0x40000000 && v<0x80000000) return "USERDATA";
    if(v>=0x90000000) return "HIGH";
    return "OTHER";
}

static void MapOneVoiceObject(SOCKET s,DWORD base,const char* tag)
{
    SendLine(s,"VOICEOBJ_BEGIN tag=%s base=0x%08X mapped=%u",tag,base,MmIsAddressValid(base)?1:0);
    if(!MmIsAddressValid(base)) return;

    DWORD hash=HashMappedRegion(base,0x100);
    SendLine(s,"VOICEOBJ_HASH tag=%s hash100=0x%08X",tag,hash);

    for(DWORD off=0;off<0x100;off+=4) {
        DWORD a=base+off;
        if(!MmIsAddressValid(a)) continue;
        DWORD v=*(volatile DWORD*)a;
        if(v>=0x40000000 && MmIsAddressValid(v)) {
            DWORD h=HashMappedRegion(v,0x40);
            SendLine(s,"VOICEOBJ_PTR tag=%s off=0x%02X value=0x%08X class=%s hash40=0x%08X",
                tag,off,v,PtrClass(v),h);
        }
        else if(v>=0x80000000) {
            SendLine(s,"VOICEOBJ_CODELIKE tag=%s off=0x%02X value=0x%08X class=%s mapped=%u",
                tag,off,v,PtrClass(v),MmIsAddressValid(v)?1:0);
        }
    }

    SendMappedWords(s,"VOICEOBJ_WORDS",base,0x40);
    SendLine(s,"VOICEOBJ_END tag=%s",tag);
}

static BOOL RebuildThunkTarget(DWORD a,DWORD* target)
{
    if(!target) return FALSE;
    if(!MmIsAddressValid(a) || !MmIsAddressValid(a+12)) return FALSE;
    DWORD w0=*(volatile DWORD*)(a+0);
    DWORD w1=*(volatile DWORD*)(a+4);
    DWORD w2=*(volatile DWORD*)(a+8);
    DWORD w3=*(volatile DWORD*)(a+12);

    if((w0&0xFFFF0000)!=0x3D600000) return FALSE;
    if((w1&0xFFFF0000)!=0x396B0000) return FALSE;
    if(w2!=0x7D6903A6 || w3!=0x4E800420) return FALSE;

    DWORD hi=w0&0xFFFF;
    SHORT lo=(SHORT)(w1&0xFFFF);
    *target=(hi<<16)+(LONG)lo;
    return TRUE;
}

static BOOL IsCaptureTarget(DWORD v)
{
    for(int i=0;i<4;i++) if(CAPTURE_TARGETS[i]==v) return TRUE;
    return FALSE;
}

static void ScanMappedCaptureReferences(SOCKET s)
{
    LONG validPages=0;
    LONG directHits=0;
    LONG thunkHits=0;
    LONG outputHits=0;

    SendLine(s,"CAPTURE_SCAN_BEGIN range=0x82000000-0x90000000 targets=4");

    for(DWORD page=0x82000000;page<0x90000000;page+=0x1000) {
        if(!MmIsAddressValid(page)) continue;
        validPages++;

        for(DWORD off=0;off<0x1000;off+=4) {
            DWORD a=page+off;
            if(!MmIsAddressValid(a)) continue;

            DWORD v=*(volatile DWORD*)a;
            if(IsCaptureTarget(v)) {
                directHits++;
                if(outputHits<96) {
                    SendLine(s,"CAPTURE_REF type=DIRECT address=0x%08X target=0x%08X name=%s pageClass=%s",
                        a,v,CaptureTargetName(v),PtrClass(a));
                    outputHits++;
                }
            }

            if(off<=0xFF0) {
                DWORD target=0;
                if(RebuildThunkTarget(a,&target) && IsCaptureTarget(target)) {
                    thunkHits++;
                    if(outputHits<96) {
                        SendLine(s,"CAPTURE_REF type=THUNK address=0x%08X target=0x%08X name=%s pageClass=%s",
                            a,target,CaptureTargetName(target),PtrClass(a));
                        outputHits++;
                    }
                }
            }
        }
    }

    SendLine(s,"CAPTURE_SCAN_SUMMARY validPages=%ld directHits=%ld thunkHits=%ld reported=%ld",
        validPages,directHits,thunkHits,outputHits);
}

static void RunV62Map(SOCKET s)
{
    SendLine(s,"V62_MAP_BEGIN");
    MapOneVoiceObject(s,0x81AACAD0,"PRIMARY_81AACAD0");
    MapOneVoiceObject(s,0x81AACA28,"ALT_81AACA28");
    ScanMappedCaptureReferences(s);
    SendLine(s,"V62_MAP_DONE");
}



static const DWORD XVOICED_ACTIVATE_TARGET = 0x80101DC8;
static const DWORD XVOICED_SUBMIT_TARGET   = 0x80102048;

static volatile DWORD g_xvActivateStub=0;
static volatile DWORD g_xvSubmitStub=0;
static volatile LONG g_xvInstalled=0;

static volatile LONG g_xvActivateCalls=0;
static volatile DWORD g_xvActivateR3=0,g_xvActivateR4=0,g_xvActivateR5=0,g_xvActivateR6=0,g_xvActivateR7=0,g_xvActivateR8=0,g_xvActivateRet=0;

static volatile LONG g_xvSubmitCalls=0;
static volatile DWORD g_xvSubmitR3=0,g_xvSubmitR4=0,g_xvSubmitR5=0,g_xvSubmitR6=0,g_xvSubmitR7=0,g_xvSubmitR8=0,g_xvSubmitRet=0;

extern "C" void RecordXVActivate(DWORD a,DWORD b,DWORD c,DWORD d,DWORD e,DWORD f)
{
    InterlockedIncrement(&g_xvActivateCalls);
    g_xvActivateR3=a; g_xvActivateR4=b; g_xvActivateR5=c;
    g_xvActivateR6=d; g_xvActivateR7=e; g_xvActivateR8=f;
}
extern "C" void RecordXVActivateRet(DWORD rc){ g_xvActivateRet=rc; }

extern "C" void RecordXVSubmit(DWORD a,DWORD b,DWORD c,DWORD d,DWORD e,DWORD f)
{
    InterlockedIncrement(&g_xvSubmitCalls);
    g_xvSubmitR3=a; g_xvSubmitR4=b; g_xvSubmitR5=c;
    g_xvSubmitR6=d; g_xvSubmitR7=e; g_xvSubmitR8=f;
}
extern "C" void RecordXVSubmitRet(DWORD rc){ g_xvSubmitRet=rc; }

extern "C" __declspec(naked) DWORD Hook_XVoicedActivate()
{
    __asm {
        stwu r1,-0x90(r1)
        mflr r0
        stw r0,0x94(r1)
        stw r3,0x20(r1)
        stw r4,0x24(r1)
        stw r5,0x28(r1)
        stw r6,0x2C(r1)
        stw r7,0x30(r1)
        stw r8,0x34(r1)
        bl RecordXVActivate
        lwz r3,0x20(r1)
        lwz r4,0x24(r1)
        lwz r5,0x28(r1)
        lwz r6,0x2C(r1)
        lwz r7,0x30(r1)
        lwz r8,0x34(r1)
        lis r11,0x8010
        addi r11,r11,0x1DC8
        mtctr r11
        bctrl
        stw r3,0x38(r1)
        bl RecordXVActivateRet
        lwz r3,0x38(r1)
        lwz r0,0x94(r1)
        mtlr r0
        addi r1,r1,0x90
        blr
    }
}

extern "C" __declspec(naked) DWORD Hook_XVoicedSubmitPacket()
{
    __asm {
        stwu r1,-0x90(r1)
        mflr r0
        stw r0,0x94(r1)
        stw r3,0x20(r1)
        stw r4,0x24(r1)
        stw r5,0x28(r1)
        stw r6,0x2C(r1)
        stw r7,0x30(r1)
        stw r8,0x34(r1)
        bl RecordXVSubmit
        lwz r3,0x20(r1)
        lwz r4,0x24(r1)
        lwz r5,0x28(r1)
        lwz r6,0x2C(r1)
        lwz r7,0x30(r1)
        lwz r8,0x34(r1)
        lis r11,0x8010
        addi r11,r11,0x2048
        mtctr r11
        bctrl
        stw r3,0x38(r1)
        bl RecordXVSubmitRet
        lwz r3,0x38(r1)
        lwz r0,0x94(r1)
        mtlr r0
        addi r1,r1,0x90
        blr
    }
}

static DWORD FindThunkInRange(DWORD start,DWORD end,DWORD target)
{
    for(DWORD a=start;a<end;a+=4) {
        if(!MmIsAddressValid(a) || !MmIsAddressValid(a+12)) continue;
        DWORD rebuilt=0;
        if(RebuildThunkTarget(a,&rebuilt) && rebuilt==target) return a;
    }
    return 0;
}

static void ReportArgClass(SOCKET s,const char* tag,int reg,DWORD v)
{
    BOOL mapped=MmIsAddressValid(v);
    SendLine(s,"%s_ARG r%d=0x%08X mapped=%u class=%s",tag,reg,v,mapped?1:0,PtrClass(v));
    if(mapped && v>=0x40000000) {
        DWORD h=HashMappedRegion(v,0x40);
        SendLine(s,"%s_MEM r%d ptr=0x%08X hash40=0x%08X",tag,reg,v,h);
    }
}

static BOOL InstallXVHooks(SOCKET s)
{
    if(g_xvInstalled) {
        SendLine(s,"XVOICED_HOOK_INSTALL already=1 activateStub=0x%08X submitStub=0x%08X",g_xvActivateStub,g_xvSubmitStub);
        return TRUE;
    }

    DWORD act=FindThunkInRange(0x81600000,0x81800000,XVOICED_ACTIVATE_TARGET);
    DWORD sub=FindThunkInRange(0x81600000,0x81800000,XVOICED_SUBMIT_TARGET);

    g_xvActivateStub=act;
    g_xvSubmitStub=sub;

    SendLine(s,"XVOICED_STUB_DISCOVERY activate=0x%08X submit=0x%08X",act,sub);

    BOOL okA=TRUE,okS=TRUE;
    if(act) okA=PatchStub((volatile DWORD*)act,(DWORD)&Hook_XVoicedActivate);
    if(sub) okS=PatchStub((volatile DWORD*)sub,(DWORD)&Hook_XVoicedSubmitPacket);

    if((!act && !sub) || !okA || !okS) {
        SendLine(s,"XVOICED_HOOK_INSTALL ok=0 actFound=%u subFound=%u actPatch=%u subPatch=%u",
            act?1:0,sub?1:0,okA?1:0,okS?1:0);
        return FALSE;
    }

    InterlockedExchange(&g_xvInstalled,1);
    SendLine(s,"XVOICED_HOOK_INSTALL ok=1 activateStub=0x%08X activateHook=0x%08X submitStub=0x%08X submitHook=0x%08X",
        act,(DWORD)&Hook_XVoicedActivate,sub,(DWORD)&Hook_XVoicedSubmitPacket);
    return TRUE;
}

static void RestoreXVHooks(SOCKET s)
{
    if(!g_xvInstalled) {
        SendLine(s,"XVOICED_HOOK_RESTORE already=0");
        return;
    }
    if(g_xvActivateStub) PatchStub((volatile DWORD*)g_xvActivateStub,XVOICED_ACTIVATE_TARGET);
    if(g_xvSubmitStub) PatchStub((volatile DWORD*)g_xvSubmitStub,XVOICED_SUBMIT_TARGET);
    InterlockedExchange(&g_xvInstalled,0);
    SendLine(s,"XVOICED_HOOK_RESTORE ok=1");
}

static void ReportXVStatus(SOCKET s)
{
    SendLine(s,"XVOICED_ACTIVATE_STATUS calls=%ld ret=0x%08X r3=0x%08X r4=0x%08X r5=0x%08X r6=0x%08X r7=0x%08X r8=0x%08X",
        g_xvActivateCalls,g_xvActivateRet,g_xvActivateR3,g_xvActivateR4,g_xvActivateR5,g_xvActivateR6,g_xvActivateR7,g_xvActivateR8);
    ReportArgClass(s,"XVOICED_ACTIVATE",3,g_xvActivateR3);
    ReportArgClass(s,"XVOICED_ACTIVATE",4,g_xvActivateR4);
    ReportArgClass(s,"XVOICED_ACTIVATE",5,g_xvActivateR5);
    ReportArgClass(s,"XVOICED_ACTIVATE",6,g_xvActivateR6);
    ReportArgClass(s,"XVOICED_ACTIVATE",7,g_xvActivateR7);
    ReportArgClass(s,"XVOICED_ACTIVATE",8,g_xvActivateR8);

    SendLine(s,"XVOICED_SUBMIT_STATUS calls=%ld ret=0x%08X r3=0x%08X r4=0x%08X r5=0x%08X r6=0x%08X r7=0x%08X r8=0x%08X",
        g_xvSubmitCalls,g_xvSubmitRet,g_xvSubmitR3,g_xvSubmitR4,g_xvSubmitR5,g_xvSubmitR6,g_xvSubmitR7,g_xvSubmitR8);
    ReportArgClass(s,"XVOICED_SUBMIT",3,g_xvSubmitR3);
    ReportArgClass(s,"XVOICED_SUBMIT",4,g_xvSubmitR4);
    ReportArgClass(s,"XVOICED_SUBMIT",5,g_xvSubmitR5);
    ReportArgClass(s,"XVOICED_SUBMIT",6,g_xvSubmitR6);
    ReportArgClass(s,"XVOICED_SUBMIT",7,g_xvSubmitR7);
    ReportArgClass(s,"XVOICED_SUBMIT",8,g_xvSubmitR8);
}


static DWORD ResolveKernelOrdinalV64(DWORD ordinal)
{
    HANDLE h=0; PVOID p=0;
    if(XexGetModuleHandle((PSZ)"xboxkrnl.exe",&h)!=0 || !h) return 0;
    if(XexGetProcedureAddress(h,ordinal,&p)!=0 || !p) return 0;
    return (DWORD)p;
}

static DWORD DecodeBranchTargetV64(DWORD pc,DWORD insn,BOOL* isCall)
{
    if(isCall) *isCall=FALSE;
    if((insn & 0xFC000000) != 0x48000000) return 0;

    if(isCall) *isCall=(insn&1)?TRUE:FALSE;

    LONG disp=(LONG)(insn & 0x03FFFFFC);
    if(disp & 0x02000000) disp |= 0xFC000000;

    if(insn & 0x00000002) return (DWORD)disp; // absolute branch
    return pc + disp;
}

static BOOL DecodeLisAddiV64(DWORD a,DWORD* target)
{
    if(!target) return FALSE;
    if(!MmIsAddressValid(a) || !MmIsAddressValid(a+4)) return FALSE;

    DWORD w0=*(volatile DWORD*)a;
    DWORD w1=*(volatile DWORD*)(a+4);

    // addis RT,r0,IMM ("lis")
    if((w0 & 0xFC1F0000) != 0x3C000000) return FALSE;
    // addi
    if((w1 & 0xFC000000) != 0x38000000) return FALSE;

    DWORD rt0=(w0>>21)&31;
    DWORD rt1=(w1>>21)&31;
    DWORD ra1=(w1>>16)&31;
    if(rt0!=rt1 || rt0!=ra1) return FALSE;

    DWORD hi=w0&0xFFFF;
    SHORT lo=(SHORT)(w1&0xFFFF);
    *target=(hi<<16)+(LONG)lo;
    return TRUE;
}

static BOOL IsV64Target(DWORD v,DWORD mic,DWORD* which)
{
    const DWORD targets[5]={
        0x80101C40, // XVoicedHeadsetPresent
        0x80102048, // XVoicedSubmitPacket
        0x80102230, // XVoicedClose
        0x80101DC8, // XVoicedActivate
        mic
    };
    for(DWORD i=0;i<5;i++) {
        if(targets[i] && v==targets[i]) {
            if(which) *which=i;
            return TRUE;
        }
    }
    return FALSE;
}

static const char* V64TargetName(DWORD which)
{
    switch(which) {
    case 0: return "XVoicedHeadsetPresent";
    case 1: return "XVoicedSubmitPacket";
    case 2: return "XVoicedClose";
    case 3: return "XVoicedActivate";
    case 4: return "MicDeviceRequest";
    default:return "UNKNOWN";
    }
}

static void ScanXamDirectCallsV64(SOCKET s)
{
    DWORD mic=ResolveKernelOrdinalV64(0x2B1);
    SendLine(s,"V64_RESOLVE ordinal=0x2B1 name=MicDeviceRequest address=0x%08X mapped=%u",
        mic,mic?MmIsAddressValid(mic):0);

    LONG pages=0,rawHits=0,branchHits=0,lisAddiHits=0,thunkHits=0,reported=0;

    SendLine(s,"V64_SCAN_BEGIN range=0x81600000-0x81800000");

    for(DWORD page=0x81600000;page<0x81800000;page+=0x1000) {
        if(!MmIsAddressValid(page)) continue;
        pages++;

        for(DWORD off=0;off<0x1000;off+=4) {
            DWORD a=page+off;
            if(!MmIsAddressValid(a)) continue;
            DWORD w=*(volatile DWORD*)a;

            DWORD which=0;
            if(IsV64Target(w,mic,&which)) {
                rawHits++;
                if(reported<128) {
                    SendLine(s,"V64_HIT type=RAW address=0x%08X target=0x%08X name=%s",
                        a,w,V64TargetName(which));
                    reported++;
                }
            }

            BOOL isCall=FALSE;
            DWORD bt=DecodeBranchTargetV64(a,w,&isCall);
            if(bt && IsV64Target(bt,mic,&which)) {
                branchHits++;
                if(reported<128) {
                    SendLine(s,"V64_HIT type=BRANCH address=0x%08X instruction=0x%08X target=0x%08X name=%s lk=%u",
                        a,w,bt,V64TargetName(which),isCall?1:0);
                    reported++;
                }
            }

            if(off<=0xFF8) {
                DWORD at=0;
                if(DecodeLisAddiV64(a,&at) && IsV64Target(at,mic,&which)) {
                    lisAddiHits++;
                    if(reported<128) {
                        DWORD w1=*(volatile DWORD*)(a+4);
                        SendLine(s,"V64_HIT type=LIS_ADDI address=0x%08X words=%08X,%08X target=0x%08X name=%s",
                            a,w,w1,at,V64TargetName(which));
                        reported++;
                    }
                }
            }

            if(off<=0xFF0) {
                DWORD tt=0;
                if(RebuildThunkTarget(a,&tt) && IsV64Target(tt,mic,&which)) {
                    thunkHits++;
                    if(reported<128) {
                        SendLine(s,"V64_HIT type=THUNK address=0x%08X target=0x%08X name=%s",
                            a,tt,V64TargetName(which));
                        reported++;
                    }
                }
            }
        }
    }

    SendLine(s,"V64_SCAN_SUMMARY mappedPages=%ld rawHits=%ld branchHits=%ld lisAddiHits=%ld thunkHits=%ld reported=%ld mic=0x%08X",
        pages,rawHits,branchHits,lisAddiHits,thunkHits,reported,mic);
    SendLine(s,"V64_SCAN_DONE");
}


static void DumpCallGraphOneV65(SOCKET s,const char* name,DWORD base)
{
    SendLine(s,"V65_FUNC_BEGIN name=%s base=0x%08X mapped=%u",name,base,MmIsAddressValid(base)?1:0);
    if(!MmIsAddressValid(base)) return;

    DWORD start=base;
    DWORD end=base+0x200;
    LONG calls=0,branches=0,addrBuilds=0,rawMapped=0;

    for(DWORD a=start;a<end;a+=4) {
        if(!MmIsAddressValid(a)) continue;
        DWORD w=*(volatile DWORD*)a;

        BOOL isCall=FALSE;
        DWORD bt=DecodeBranchTargetV64(a,w,&isCall);
        if(bt && MmIsAddressValid(bt)) {
            if(isCall) calls++; else branches++;
            SendLine(s,"V65_EDGE func=%s pc=0x%08X insn=0x%08X type=%s target=0x%08X class=%s",
                name,a,w,isCall?"CALL":"BRANCH",bt,PtrClass(bt));
        }

        if(a+4<end && MmIsAddressValid(a+4)) {
            DWORD at=0;
            if(DecodeLisAddiV64(a,&at) && MmIsAddressValid(at)) {
                addrBuilds++;
                SendLine(s,"V65_ADDR func=%s pc=0x%08X target=0x%08X class=%s hash40=0x%08X",
                    name,a,at,PtrClass(at),HashMappedRegion(at,0x40));
            }
        }

        if(w>=0x80000000 && MmIsAddressValid(w)) {
            rawMapped++;
            if(rawMapped<=24) {
                SendLine(s,"V65_RAW func=%s pc=0x%08X value=0x%08X class=%s hash40=0x%08X",
                    name,a,w,PtrClass(w),HashMappedRegion(w,0x40));
            }
        }
    }

    SendLine(s,"V65_FUNC_SUMMARY name=%s calls=%ld branches=%ld addrBuilds=%ld rawMapped=%ld",
        name,calls,branches,addrBuilds,rawMapped);
    SendLine(s,"V65_FUNC_END name=%s",name);
}

static void RunV65CallGraph(SOCKET s)
{
    DWORD mic=ResolveKernelOrdinalV64(0x2B1);
    SendLine(s,"V65_BEGIN mic=0x%08X",mic);
    DumpCallGraphOneV65(s,"MicDeviceRequest",mic);
    DumpCallGraphOneV65(s,"XVoicedHeadsetPresent",0x80101C40);
    DumpCallGraphOneV65(s,"XVoicedActivate",0x80101DC8);
    DumpCallGraphOneV65(s,"XVoicedSubmitPacket",0x80102048);
    DumpCallGraphOneV65(s,"XVoicedClose",0x80102230);
    SendLine(s,"V65_DONE");
}


#define V66_MAX_NODES 96
#define V66_MAX_DEPTH 3
#define V66_FUNC_BYTES 0x180

static DWORD g_v66Visited[V66_MAX_NODES];
static LONG g_v66VisitedCount=0;

static BOOL V66Visited(DWORD a)
{
    for(LONG i=0;i<g_v66VisitedCount;i++) if(g_v66Visited[i]==a) return TRUE;
    return FALSE;
}

static BOOL V66Mark(DWORD a)
{
    if(V66Visited(a)) return FALSE;
    if(g_v66VisitedCount>=V66_MAX_NODES) return FALSE;
    g_v66Visited[g_v66VisitedCount++]=a;
    return TRUE;
}

static BOOL V66InterestingKernelTarget(DWORD a)
{
    if(a<0x80000000 || a>=0x80200000) return FALSE;
    if(!MmIsAddressValid(a)) return FALSE;
    return TRUE;
}

static void V66Explore(SOCKET s,DWORD base,const char* root,LONG depth)
{
    if(depth>V66_MAX_DEPTH) return;
    if(!V66InterestingKernelTarget(base)) return;
    if(!V66Mark(base)) return;

    SendLine(s,"V66_NODE root=%s depth=%ld base=0x%08X",root,depth,base);

    LONG calls=0,branches=0,addrBuilds=0;
    DWORD end=base+V66_FUNC_BYTES;

    for(DWORD pc=base;pc<end;pc+=4) {
        if(!MmIsAddressValid(pc)) continue;
        DWORD insn=*(volatile DWORD*)pc;

        BOOL isCall=FALSE;
        DWORD bt=DecodeBranchTargetV64(pc,insn,&isCall);

        if(bt && V66InterestingKernelTarget(bt)) {
            if(isCall) calls++; else branches++;

            SendLine(s,"V66_EDGE root=%s depth=%ld from=0x%08X pc=0x%08X type=%s to=0x%08X",
                root,depth,base,pc,isCall?"CALL":"BRANCH",bt);

            if(isCall && depth<V66_MAX_DEPTH)
                V66Explore(s,bt,root,depth+1);
        }

        if(pc+4<end && MmIsAddressValid(pc+4)) {
            DWORD at=0;
            if(DecodeLisAddiV64(pc,&at) && MmIsAddressValid(at)) {
                addrBuilds++;
                SendLine(s,"V66_ADDR root=%s depth=%ld func=0x%08X pc=0x%08X target=0x%08X class=%s hash40=0x%08X",
                    root,depth,base,pc,at,PtrClass(at),HashMappedRegion(at,0x40));
            }
        }
    }

    SendLine(s,"V66_NODE_SUMMARY root=%s depth=%ld base=0x%08X calls=%ld branches=%ld addrBuilds=%ld",
        root,depth,base,calls,branches,addrBuilds);
}

static void RunV66Recursive(SOCKET s)
{
    g_v66VisitedCount=0;

    SendLine(s,"V66_BEGIN maxDepth=%d maxNodes=%d bytesPerNode=0x%X",
        V66_MAX_DEPTH,V66_MAX_NODES,V66_FUNC_BYTES);

    V66Explore(s,0x80102918,"SHARED_80102918",0);

    // Explore several other high-value roots independently.
    g_v66VisitedCount=0;
    V66Explore(s,0x80101C28,"COMMON_80101C28",0);

    g_v66VisitedCount=0;
    V66Explore(s,0x800FDA38,"COMMON_800FDA38",0);

    g_v66VisitedCount=0;
    V66Explore(s,0x801065B0,"COMMON_801065B0",0);

    g_v66VisitedCount=0;
    V66Explore(s,0x80101DE8,"SUBMIT_ACTIVATE_BRIDGE_80101DE8",0);

    g_v66VisitedCount=0;
    V66Explore(s,0x8014AD58,"MIC_INTERNAL_8014AD58",0);

    g_v66VisitedCount=0;
    V66Explore(s,0x8010CBCC,"MIC_INTERNAL_8010CBCC",0);

    SendLine(s,"V66_DONE");
}


static volatile LONG g_v67Phase=0;

static DWORD V67Hash(DWORD addr,DWORD bytes,LONG* nonZero,LONG* transitions)
{
    if(nonZero) *nonZero=0;
    if(transitions) *transitions=0;
    if(bytes==0 || bytes>0x100) return 0;
    if(!MmIsAddressValid(addr) || !MmIsAddressValid(addr+bytes-1)) return 0;

    DWORD h=2166136261u;
    BYTE prev=0;
    BOOL havePrev=FALSE;
    for(DWORD i=0;i<bytes;i++) {
        BYTE v=*(volatile BYTE*)(addr+i);
        h^=v; h*=16777619u;
        if(nonZero && v) (*nonZero)++;
        if(transitions && havePrev && v!=prev) (*transitions)++;
        prev=v; havePrev=TRUE;
    }
    return h;
}

static void V67DumpOne(SOCKET s,const char* name,DWORD base)
{
    LONG nz=0,tr=0;
    BOOL mapped=MmIsAddressValid(base) && MmIsAddressValid(base+0xFF);
    DWORD h=mapped?V67Hash(base,0x100,&nz,&tr):0;

    SendLine(s,"V67_STATE phase=%ld name=%s base=0x%08X mapped=%u hash100=0x%08X nonzero=%ld transitions=%ld",
        g_v67Phase,name,base,mapped?1:0,h,nz,tr);

    if(!mapped) return;

    SendMappedWords(s,name,base,0x40);

    for(DWORD off=0;off<0x100;off+=4) {
        DWORD a=base+off;
        if(!MmIsAddressValid(a)) continue;
        DWORD v=*(volatile DWORD*)a;

        if(v>=0x40000000 && MmIsAddressValid(v)) {
            DWORD h40=HashMappedRegion(v,0x40);
            SendLine(s,"V67_PTR phase=%ld name=%s off=0x%02X value=0x%08X class=%s hash40=0x%08X",
                g_v67Phase,name,off,v,PtrClass(v),h40);
        }
    }
}

static void V67Sample(SOCKET s,LONG phase)
{
    g_v67Phase=phase;
    SendLine(s,"V67_SAMPLE_BEGIN phase=%ld",phase);

    V67DumpOne(s,"GLOBAL_801A58CC",0x801A58CC);
    V67DumpOne(s,"GLOBAL_801A5780",0x801A5780);
    V67DumpOne(s,"GLOBAL_801AB3C0",0x801AB3C0);

    SendLine(s,"V67_SAMPLE_END phase=%ld",phase);
}


#define V68_REGION_BYTES 0x100
#define V68_REGION_COUNT 9

static const DWORD g_v68Bases[V68_REGION_COUNT] = {
    0x801A58CC,
    0x801B2000,
    0x801B0100,
    0x801B1100,
    0x801B2100,
    0x801B3100,
    0x801B4100,
    0x801B5100,
    0x801A5780
};

static const char* g_v68Names[V68_REGION_COUNT] = {
    "GLOBAL_801A58CC",
    "DEVICE_801B2000",
    "SLOT_801B0100",
    "SLOT_801B1100",
    "SLOT_801B2100",
    "SLOT_801B3100",
    "SLOT_801B4100",
    "SLOT_801B5100",
    "TABLE_801A5780"
};

static BYTE g_v68Baseline[V68_REGION_COUNT][V68_REGION_BYTES];
static BOOL g_v68BaselineValid[V68_REGION_COUNT]={FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE};

static BOOL V68ReadRegion(DWORD base,BYTE* out)
{
    if(!out) return FALSE;
    if(!MmIsAddressValid(base) || !MmIsAddressValid(base+V68_REGION_BYTES-1)) return FALSE;
    for(DWORD i=0;i<V68_REGION_BYTES;i++)
        out[i]=*(volatile BYTE*)(base+i);
    return TRUE;
}

static DWORD V68HashBytes(const BYTE* p,DWORD n)
{
    DWORD h=2166136261u;
    for(DWORD i=0;i<n;i++){ h^=p[i]; h*=16777619u; }
    return h;
}

static void V68SetBaseline(SOCKET s)
{
    SendLine(s,"V68_BASELINE_BEGIN");
    for(int r=0;r<V68_REGION_COUNT;r++) {
        BYTE cur[V68_REGION_BYTES];
        BOOL ok=V68ReadRegion(g_v68Bases[r],cur);
        g_v68BaselineValid[r]=ok;
        if(ok) {
            for(DWORD i=0;i<V68_REGION_BYTES;i++) g_v68Baseline[r][i]=cur[i];
            SendLine(s,"V68_BASELINE name=%s base=0x%08X hash100=0x%08X",
                g_v68Names[r],g_v68Bases[r],V68HashBytes(cur,V68_REGION_BYTES));
        } else {
            SendLine(s,"V68_BASELINE name=%s base=0x%08X mapped=0",
                g_v68Names[r],g_v68Bases[r]);
        }
    }
    SendLine(s,"V68_BASELINE_END");
}

static void V68Diff(SOCKET s,LONG phase)
{
    SendLine(s,"V68_DIFF_BEGIN phase=%ld",phase);

    for(int r=0;r<V68_REGION_COUNT;r++) {
        BYTE cur[V68_REGION_BYTES];
        BOOL ok=V68ReadRegion(g_v68Bases[r],cur);
        if(!ok || !g_v68BaselineValid[r]) {
            SendLine(s,"V68_DIFF_SUMMARY phase=%ld name=%s base=0x%08X valid=0",
                phase,g_v68Names[r],g_v68Bases[r]);
            continue;
        }

        LONG byteDiffs=0,wordDiffs=0;
        for(DWORD i=0;i<V68_REGION_BYTES;i++)
            if(cur[i]!=g_v68Baseline[r][i]) byteDiffs++;

        for(DWORD off=0;off<V68_REGION_BYTES;off+=4) {
            DWORD oldv =
                ((DWORD)g_v68Baseline[r][off+0]<<24) |
                ((DWORD)g_v68Baseline[r][off+1]<<16) |
                ((DWORD)g_v68Baseline[r][off+2]<<8) |
                ((DWORD)g_v68Baseline[r][off+3]);

            DWORD newv =
                ((DWORD)cur[off+0]<<24) |
                ((DWORD)cur[off+1]<<16) |
                ((DWORD)cur[off+2]<<8) |
                ((DWORD)cur[off+3]);

            if(oldv!=newv) {
                wordDiffs++;
                if(wordDiffs<=32) {
                    SendLine(s,"V68_WORD_DIFF phase=%ld name=%s off=0x%02X old=0x%08X new=0x%08X xor=0x%08X",
                        phase,g_v68Names[r],off,oldv,newv,oldv^newv);
                }
            }
        }

        SendLine(s,"V68_DIFF_SUMMARY phase=%ld name=%s base=0x%08X byteDiffs=%ld wordDiffs=%ld hash100=0x%08X",
            phase,g_v68Names[r],g_v68Bases[r],byteDiffs,wordDiffs,V68HashBytes(cur,V68_REGION_BYTES));
    }

    SendLine(s,"V68_DIFF_END phase=%ld",phase);
}


#define V69_BYTES 0x100
static const DWORD V69_BASES[2]={0x801B2000,0x801B2100};
static const char* V69_NAMES[2]={"DEVICE_801B2000","SLOT_801B2100"};
static BYTE g_v69Prev[2][V69_BYTES];
static BOOL g_v69PrevValid[2]={FALSE,FALSE};

static BOOL V69Read(DWORD base,BYTE* out)
{
    if(!out) return FALSE;
    if(!MmIsAddressValid(base)||!MmIsAddressValid(base+V69_BYTES-1)) return FALSE;
    for(DWORD i=0;i<V69_BYTES;i++) out[i]=*(volatile BYTE*)(base+i);
    return TRUE;
}

static DWORD V69WordBE(const BYTE* p,DWORD off)
{
    return ((DWORD)p[off]<<24)|((DWORD)p[off+1]<<16)|((DWORD)p[off+2]<<8)|p[off+3];
}

static void V69Snapshot(SOCKET s,LONG phase,const char* label)
{
    SendLine(s,"V69_SNAPSHOT_BEGIN phase=%ld label=%s",phase,label);
    for(int r=0;r<2;r++) {
        BYTE cur[V69_BYTES];
        BOOL ok=V69Read(V69_BASES[r],cur);
        if(!ok) {
            SendLine(s,"V69_SUMMARY phase=%ld label=%s name=%s valid=0",phase,label,V69_NAMES[r]);
            continue;
        }

        LONG diffs=0;
        if(g_v69PrevValid[r]) {
            for(DWORD off=0;off<V69_BYTES;off+=4) {
                DWORD ov=V69WordBE(g_v69Prev[r],off), nv=V69WordBE(cur,off);
                if(ov!=nv) {
                    diffs++;
                    if(diffs<=40) {
                        SendLine(s,"V69_TRANSITION phase=%ld label=%s name=%s off=0x%02X old=0x%08X new=0x%08X xor=0x%08X",
                            phase,label,V69_NAMES[r],off,ov,nv,ov^nv);
                    }
                }
            }
        }

        for(DWORD i=0;i<V69_BYTES;i++) g_v69Prev[r][i]=cur[i];
        g_v69PrevValid[r]=TRUE;

        SendLine(s,"V69_SUMMARY phase=%ld label=%s name=%s wordDiffsFromPrev=%ld",
            phase,label,V69_NAMES[r],diffs);
    }
    SendLine(s,"V69_SNAPSHOT_END phase=%ld label=%s",phase,label);
}



static volatile LONG g_v6101HeadsetCalls=0;
static volatile DWORD g_v6101HeadsetLastArg=0;
static volatile DWORD g_v6101HeadsetLastRet=0;

extern "C" void RecordV6101HeadsetArg(DWORD a)
{
    InterlockedIncrement(&g_v6101HeadsetCalls);
    g_v6101HeadsetLastArg=a;
}
extern "C" void RecordV6101HeadsetRet(DWORD rc)
{
    g_v6101HeadsetLastRet=rc;
}

extern "C" __declspec(naked) DWORD Hook_V6101_XamVoiceHeadsetPresent()
{
    __asm {
        stwu r1,-0x70(r1)
        mflr r0
        stw r0,0x74(r1)
        stw r3,0x20(r1)

        bl RecordV6101HeadsetArg

        lwz r3,0x20(r1)
        lis r11,0x8170
        addi r11,r11,-0x4BE8
        mtctr r11
        bctrl

        stw r3,0x24(r1)
        bl RecordV6101HeadsetRet
        lwz r3,0x24(r1)

        lwz r0,0x74(r1)
        mtlr r0
        addi r1,r1,0x70
        blr
    }
}

static BOOL InstallV6101HeadsetObserver(SOCKET s)
{
    if(g_v6101HeadsetCalls<0) return FALSE;
    BOOL ok=PatchStub((volatile DWORD*)0x8A7B4404,(DWORD)&Hook_V6101_XamVoiceHeadsetPresent);
    SendLine(s,"V6101_OBSERVER_INSTALL ok=%u stub=0x8A7B4404 hook=0x%08X",ok?1:0,(DWORD)&Hook_V6101_XamVoiceHeadsetPresent);
    return ok;
}

static void RestoreV6101HeadsetObserver(SOCKET s)
{
    PatchStub((volatile DWORD*)0x8A7B4404,0x816FB418);
    SendLine(s,"V6101_OBSERVER_RESTORE ok=1");
}

static DWORD V6101ReadField()
{
    DWORD a=0x801B2160;
    if(!MmIsAddressValid(a)) return 0xFFFFFFFF;
    return *(volatile DWORD*)a;
}

static void V6101Sample(SOCKET s,LONG phase,const char* label)
{
    DWORD field=V6101ReadField();
    SendLine(s,"V6101_CORRELATE phase=%ld label=%s fieldAddr=0x801B2160 field=0x%08X observedCalls=%ld lastArg=0x%08X lastRet=0x%08X",
        phase,label,field,g_v6101HeadsetCalls,g_v6101HeadsetLastArg,g_v6101HeadsetLastRet);

    if(MmIsAddressValid(0x801B2100) && MmIsAddressValid(0x801B217F))
        SendMappedWords(s,"V6101_SLOT2100",0x801B2100,0x80);
}



#define V611_OBJ_BASE 0x81AACAD0
#define V611_OBJ_BYTES 0x100

static volatile LONG  g_v611Calls=0;
static volatile DWORD g_v611R3=0,g_v611R4=0,g_v611R5=0,g_v611R6=0,g_v611R7=0,g_v611R8=0;
static volatile DWORD g_v611Ret=0;
static volatile DWORD g_v611BeforeHash=0,g_v611AfterHash=0;

static DWORD V611HashRegion(DWORD base,DWORD bytes)
{
    if(!MmIsAddressValid(base) || !MmIsAddressValid(base+bytes-1)) return 0;
    DWORD h=2166136261u;
    for(DWORD i=0;i<bytes;i++){
        BYTE v=*(volatile BYTE*)(base+i);
        h^=v; h*=16777619u;
    }
    return h;
}

extern "C" void V611RecordBefore(DWORD a,DWORD b,DWORD c,DWORD d,DWORD e,DWORD f)
{
    InterlockedIncrement(&g_v611Calls);
    g_v611R3=a; g_v611R4=b; g_v611R5=c;
    g_v611R6=d; g_v611R7=e; g_v611R8=f;

    if(a && MmIsAddressValid(a) && MmIsAddressValid(a+V611_OBJ_BYTES-1))
        g_v611BeforeHash=V611HashRegion(a,V611_OBJ_BYTES);
    else
        g_v611BeforeHash=0;
}

extern "C" void V611RecordAfter(DWORD rc)
{
    g_v611Ret=rc;
    DWORD a=g_v611R3;
    if(a && MmIsAddressValid(a) && MmIsAddressValid(a+V611_OBJ_BYTES-1))
        g_v611AfterHash=V611HashRegion(a,V611_OBJ_BYTES);
    else
        g_v611AfterHash=0;
}

extern "C" __declspec(naked) DWORD Hook_V611_XamVoiceHeadsetPresent()
{
    __asm {
        stwu r1,-0x90(r1)
        mflr r0
        stw r0,0x94(r1)

        stw r3,0x20(r1)
        stw r4,0x24(r1)
        stw r5,0x28(r1)
        stw r6,0x2C(r1)
        stw r7,0x30(r1)
        stw r8,0x34(r1)

        bl V611RecordBefore

        lwz r3,0x20(r1)
        lwz r4,0x24(r1)
        lwz r5,0x28(r1)
        lwz r6,0x2C(r1)
        lwz r7,0x30(r1)
        lwz r8,0x34(r1)

        lis r11,0x8170
        addi r11,r11,-0x4BE8
        mtctr r11
        bctrl

        stw r3,0x38(r1)
        bl V611RecordAfter
        lwz r3,0x38(r1)

        lwz r0,0x94(r1)
        mtlr r0
        addi r1,r1,0x90
        blr
    }
}

static BOOL V611Install(SOCKET s)
{
    BOOL ok=PatchStub((volatile DWORD*)0x8A7B4404,(DWORD)&Hook_V611_XamVoiceHeadsetPresent);
    SendLine(s,"V611_OBSERVER_INSTALL ok=%u stub=0x8A7B4404 hook=0x%08X",ok?1:0,(DWORD)&Hook_V611_XamVoiceHeadsetPresent);
    return ok;
}

static void V611Restore(SOCKET s)
{
    PatchStub((volatile DWORD*)0x8A7B4404,0x816FB418);
    SendLine(s,"V611_OBSERVER_RESTORE ok=1");
}

static void V611Snapshot(SOCKET s,LONG phase,const char* label)
{
    SendLine(s,"V611_STATUS phase=%ld label=%s calls=%ld r3=0x%08X r4=0x%08X r5=0x%08X r6=0x%08X r7=0x%08X r8=0x%08X ret=0x%08X beforeHash=0x%08X afterHash=0x%08X",
        phase,label,g_v611Calls,g_v611R3,g_v611R4,g_v611R5,g_v611R6,g_v611R7,g_v611R8,g_v611Ret,g_v611BeforeHash,g_v611AfterHash);

    DWORD base=g_v611R3 ? g_v611R3 : V611_OBJ_BASE;
    if(MmIsAddressValid(base) && MmIsAddressValid(base+0xFF)) {
        SendMappedWords(s,"V611_VOICEOBJ",base,0x100);

        for(DWORD off=0;off<0x100;off+=4) {
            DWORD a=base+off;
            DWORD v=*(volatile DWORD*)a;
            if(v>=0x40000000 && MmIsAddressValid(v)) {
                SendLine(s,"V611_PTR phase=%ld label=%s off=0x%02X value=0x%08X class=%s hash40=0x%08X",
                    phase,label,off,v,PtrClass(v),HashMappedRegion(v,0x40));
            }
        }
    }
}


static DWORD V612ReadWord(DWORD a)
{
    if(!MmIsAddressValid(a)) return 0xFFFFFFFF;
    return *(volatile DWORD*)a;
}

static void V612Snapshot(SOCKET s,LONG phase,const char* label)
{
    DWORD obj = g_v611R3 ? g_v611R3 : 0x81AACAD0;

    DWORD f28 = V612ReadWord(obj+0x28);
    DWORD f2C = V612ReadWord(obj+0x2C);
    DWORD f38 = V612ReadWord(obj+0x38);
    DWORD f3C = V612ReadWord(obj+0x3C);

    DWORD child = V612ReadWord(obj+0x1C);
    DWORD childHash = 0;
    if(child != 0xFFFFFFFF && MmIsAddressValid(child) && MmIsAddressValid(child+0x3F))
        childHash = HashMappedRegion(child,0x40);

    SendLine(s,
        "V612_PRESENCE phase=%ld label=%s calls=%ld obj=0x%08X "
        "f28=0x%08X f2C=0x%08X f38=0x%08X f3C=0x%08X "
        "child=0x%08X childHash40=0x%08X "
        "r4=0x%08X r5=0x%08X r6=0x%08X r7=0x%08X r8=0x%08X ret=0x%08X",
        phase,label,g_v611Calls,obj,
        f28,f2C,f38,f3C,
        child,childHash,
        g_v611R4,g_v611R5,g_v611R6,g_v611R7,g_v611R8,g_v611Ret);

    if(MmIsAddressValid(obj) && MmIsAddressValid(obj+0x3F))
        SendMappedWords(s,"V612_OBJ40",obj,0x40);
}


static volatile LONG g_v613CreateCalls=0;
static volatile LONG g_v613SubmitCalls=0;
static volatile LONG g_v613CloseCalls=0;
static volatile LONG g_v613HeadsetCalls=0;

static volatile DWORD g_v613CreateBefore=0,g_v613CreateAfter=0;
static volatile DWORD g_v613SubmitBefore=0,g_v613SubmitAfter=0;
static volatile DWORD g_v613CloseBefore=0,g_v613CloseAfter=0;
static volatile DWORD g_v613HeadsetBefore=0,g_v613HeadsetAfter=0;

static DWORD V613Field()
{
    DWORD a=0x81AACAD0+0x38;
    if(!MmIsAddressValid(a)) return 0xFFFFFFFF;
    return *(volatile DWORD*)a;
}

extern "C" void V613CreateBefore(){ InterlockedIncrement(&g_v613CreateCalls); g_v613CreateBefore=V613Field(); }
extern "C" void V613CreateAfter(){ g_v613CreateAfter=V613Field(); }

extern "C" void V613SubmitBefore(){ InterlockedIncrement(&g_v613SubmitCalls); g_v613SubmitBefore=V613Field(); }
extern "C" void V613SubmitAfter(){ g_v613SubmitAfter=V613Field(); }

extern "C" void V613CloseBefore(){ InterlockedIncrement(&g_v613CloseCalls); g_v613CloseBefore=V613Field(); }
extern "C" void V613CloseAfter(){ g_v613CloseAfter=V613Field(); }

extern "C" void V613HeadsetBefore(){ InterlockedIncrement(&g_v613HeadsetCalls); g_v613HeadsetBefore=V613Field(); }
extern "C" void V613HeadsetAfter(){ g_v613HeadsetAfter=V613Field(); }

extern "C" __declspec(naked) DWORD Hook_V613_Create()
{
    __asm {
        stwu r1,-0x80(r1)
        mflr r0
        stw r0,0x84(r1)
        stw r3,0x20(r1); stw r4,0x24(r1); stw r5,0x28(r1); stw r6,0x2C(r1); stw r7,0x30(r1); stw r8,0x34(r1)
        bl V613CreateBefore
        lwz r3,0x20(r1); lwz r4,0x24(r1); lwz r5,0x28(r1); lwz r6,0x2C(r1); lwz r7,0x30(r1); lwz r8,0x34(r1)
        lis r11,0x8170
        addi r11,r11,-0x3F68
        mtctr r11
        bctrl
        stw r3,0x38(r1)
        bl V613CreateAfter
        lwz r3,0x38(r1)
        lwz r0,0x84(r1); mtlr r0; addi r1,r1,0x80; blr
    }
}

extern "C" __declspec(naked) DWORD Hook_V613_Submit()
{
    __asm {
        stwu r1,-0x80(r1)
        mflr r0
        stw r0,0x84(r1)
        stw r3,0x20(r1); stw r4,0x24(r1); stw r5,0x28(r1); stw r6,0x2C(r1); stw r7,0x30(r1); stw r8,0x34(r1)
        bl V613SubmitBefore
        lwz r3,0x20(r1); lwz r4,0x24(r1); lwz r5,0x28(r1); lwz r6,0x2C(r1); lwz r7,0x30(r1); lwz r8,0x34(r1)
        lis r11,0x8170
        addi r11,r11,-0x4B68
        mtctr r11
        bctrl
        stw r3,0x38(r1)
        bl V613SubmitAfter
        lwz r3,0x38(r1)
        lwz r0,0x84(r1); mtlr r0; addi r1,r1,0x80; blr
    }
}

extern "C" __declspec(naked) DWORD Hook_V613_Close()
{
    __asm {
        stwu r1,-0x80(r1)
        mflr r0
        stw r0,0x84(r1)
        stw r3,0x20(r1); stw r4,0x24(r1); stw r5,0x28(r1); stw r6,0x2C(r1); stw r7,0x30(r1); stw r8,0x34(r1)
        bl V613CloseBefore
        lwz r3,0x20(r1); lwz r4,0x24(r1); lwz r5,0x28(r1); lwz r6,0x2C(r1); lwz r7,0x30(r1); lwz r8,0x34(r1)
        lis r11,0x8170
        addi r11,r11,-0x3E38
        mtctr r11
        bctrl
        stw r3,0x38(r1)
        bl V613CloseAfter
        lwz r3,0x38(r1)
        lwz r0,0x84(r1); mtlr r0; addi r1,r1,0x80; blr
    }
}

extern "C" __declspec(naked) DWORD Hook_V613_Headset()
{
    __asm {
        stwu r1,-0x80(r1)
        mflr r0
        stw r0,0x84(r1)
        stw r3,0x20(r1); stw r4,0x24(r1); stw r5,0x28(r1); stw r6,0x2C(r1); stw r7,0x30(r1); stw r8,0x34(r1)
        bl V613HeadsetBefore
        lwz r3,0x20(r1); lwz r4,0x24(r1); lwz r5,0x28(r1); lwz r6,0x2C(r1); lwz r7,0x30(r1); lwz r8,0x34(r1)
        lis r11,0x8170
        addi r11,r11,-0x4BE8
        mtctr r11
        bctrl
        stw r3,0x38(r1)
        bl V613HeadsetAfter
        lwz r3,0x38(r1)
        lwz r0,0x84(r1); mtlr r0; addi r1,r1,0x80; blr
    }
}

static BOOL V613Install(SOCKET s)
{
    BOOL a=PatchStub((volatile DWORD*)0x8A7B4434,(DWORD)&Hook_V613_Create);
    BOOL b=PatchStub((volatile DWORD*)0x8A7B4424,(DWORD)&Hook_V613_Submit);
    BOOL c=PatchStub((volatile DWORD*)0x8A7B4414,(DWORD)&Hook_V613_Close);
    BOOL d=PatchStub((volatile DWORD*)0x8A7B4404,(DWORD)&Hook_V613_Headset);
    SendLine(s,"V613_INSTALL create=%u submit=%u close=%u headset=%u",a?1:0,b?1:0,c?1:0,d?1:0);
    return a&&b&&c&&d;
}

static void V613Restore(SOCKET s)
{
    PatchStub((volatile DWORD*)0x8A7B4434,0x816FC098);
    PatchStub((volatile DWORD*)0x8A7B4424,0x816FB498);
    PatchStub((volatile DWORD*)0x8A7B4414,0x816FC1C8);
    PatchStub((volatile DWORD*)0x8A7B4404,0x816FB418);
    SendLine(s,"V613_RESTORE ok=1");
}

static void V613Report(SOCKET s,LONG phase,const char* label)
{
    SendLine(s,
        "V613_STATUS phase=%ld label=%s field=0x%08X "
        "createCalls=%ld createBefore=0x%08X createAfter=0x%08X "
        "submitCalls=%ld submitBefore=0x%08X submitAfter=0x%08X "
        "closeCalls=%ld closeBefore=0x%08X closeAfter=0x%08X "
        "headsetCalls=%ld headsetBefore=0x%08X headsetAfter=0x%08X",
        phase,label,V613Field(),
        g_v613CreateCalls,g_v613CreateBefore,g_v613CreateAfter,
        g_v613SubmitCalls,g_v613SubmitBefore,g_v613SubmitAfter,
        g_v613CloseCalls,g_v613CloseBefore,g_v613CloseAfter,
        g_v613HeadsetCalls,g_v613HeadsetBefore,g_v613HeadsetAfter);
}


static volatile LONG g_v614SharedCalls=0;
static volatile LONG g_v614CommonCalls=0;
static volatile LONG g_v614DevCalls=0;

static volatile DWORD g_v614SharedBefore=0,g_v614SharedAfter=0;
static volatile DWORD g_v614CommonBefore=0,g_v614CommonAfter=0;
static volatile DWORD g_v614DevBefore=0,g_v614DevAfter=0;

static DWORD V614Field()
{
    DWORD a=0x81AACAD0+0x38;
    if(!MmIsAddressValid(a)) return 0xFFFFFFFF;
    return *(volatile DWORD*)a;
}

extern "C" void V614SharedBefore(){ InterlockedIncrement(&g_v614SharedCalls); g_v614SharedBefore=V614Field(); }
extern "C" void V614SharedAfter(){ g_v614SharedAfter=V614Field(); }

extern "C" void V614CommonBefore(){ InterlockedIncrement(&g_v614CommonCalls); g_v614CommonBefore=V614Field(); }
extern "C" void V614CommonAfter(){ g_v614CommonAfter=V614Field(); }

extern "C" void V614DevBefore(){ InterlockedIncrement(&g_v614DevCalls); g_v614DevBefore=V614Field(); }
extern "C" void V614DevAfter(){ g_v614DevAfter=V614Field(); }

extern "C" __declspec(naked) DWORD Hook_V614_Shared()
{
    __asm {
        stwu r1,-0x90(r1)
        mflr r0
        stw r0,0x94(r1)
        stw r3,0x20(r1); stw r4,0x24(r1); stw r5,0x28(r1); stw r6,0x2C(r1)
        stw r7,0x30(r1); stw r8,0x34(r1)

        bl V614SharedBefore

        lwz r3,0x20(r1); lwz r4,0x24(r1); lwz r5,0x28(r1); lwz r6,0x2C(r1)
        lwz r7,0x30(r1); lwz r8,0x34(r1)

        lis r11,0x8010
        addi r11,r11,0x2918
        mtctr r11
        bctrl

        stw r3,0x38(r1)
        bl V614SharedAfter
        lwz r3,0x38(r1)

        lwz r0,0x94(r1)
        mtlr r0
        addi r1,r1,0x90
        blr
    }
}

extern "C" __declspec(naked) DWORD Hook_V614_Common()
{
    __asm {
        stwu r1,-0x90(r1)
        mflr r0
        stw r0,0x94(r1)
        stw r3,0x20(r1); stw r4,0x24(r1); stw r5,0x28(r1); stw r6,0x2C(r1)
        stw r7,0x30(r1); stw r8,0x34(r1)

        bl V614CommonBefore

        lwz r3,0x20(r1); lwz r4,0x24(r1); lwz r5,0x28(r1); lwz r6,0x2C(r1)
        lwz r7,0x30(r1); lwz r8,0x34(r1)

        lis r11,0x8007
        addi r11,r11,0x01C0
        mtctr r11
        bctrl

        stw r3,0x38(r1)
        bl V614CommonAfter
        lwz r3,0x38(r1)

        lwz r0,0x94(r1)
        mtlr r0
        addi r1,r1,0x90
        blr
    }
}

extern "C" __declspec(naked) DWORD Hook_V614_Dev()
{
    __asm {
        stwu r1,-0x90(r1)
        mflr r0
        stw r0,0x94(r1)
        stw r3,0x20(r1); stw r4,0x24(r1); stw r5,0x28(r1); stw r6,0x2C(r1)
        stw r7,0x30(r1); stw r8,0x34(r1)

        bl V614DevBefore

        lwz r3,0x20(r1); lwz r4,0x24(r1); lwz r5,0x28(r1); lwz r6,0x2C(r1)
        lwz r7,0x30(r1); lwz r8,0x34(r1)

        lis r11,0x8007
        addi r11,r11,0x020C
        mtctr r11
        bctrl

        stw r3,0x38(r1)
        bl V614DevAfter
        lwz r3,0x38(r1)

        lwz r0,0x94(r1)
        mtlr r0
        addi r1,r1,0x90
        blr
    }
}


typedef void (__cdecl *PFN_V614_CACHE_SWEEP)(PVOID,DWORD);

static void V614FlushCode(DWORD address,DWORD bytes)
{
    PFN_V614_CACHE_SWEEP d=(PFN_V614_CACHE_SWEEP)0x800738C8;
    PFN_V614_CACHE_SWEEP i=(PFN_V614_CACHE_SWEEP)0x8007388C;
    d((PVOID)address,bytes);
    i((PVOID)address,bytes);
}

static BOOL V614PatchDirectBranchCallSite(DWORD callsite,DWORD hook,DWORD* original)
{
    if(!MmIsAddressValid(callsite)) return FALSE;
    DWORD insn=*(volatile DWORD*)callsite;
    if(original) *original=insn;

    LONG delta=(LONG)hook-(LONG)callsite;
    if(delta < -0x02000000 || delta > 0x01FFFFFC) return FALSE;

    DWORD patched=0x48000001 | ((DWORD)delta & 0x03FFFFFC);
    *(volatile DWORD*)callsite=patched;

    // Reuse known cache sweep helpers from this project.
    V614FlushCode(callsite,4);

    return TRUE;
}

static DWORD g_v614OrigShared=0,g_v614OrigCommon=0,g_v614OrigDev=0;
static DWORD g_v614SiteShared=0,g_v614SiteCommon=0,g_v614SiteDev=0;

static DWORD V614FindCallSite(DWORD start,DWORD end,DWORD target)
{
    for(DWORD pc=start;pc<end;pc+=4){
        if(!MmIsAddressValid(pc)) continue;
        DWORD insn=*(volatile DWORD*)pc;
        BOOL isCall=FALSE;
        DWORD bt=DecodeBranchTargetV64(pc,insn,&isCall);
        if(isCall && bt==target) return pc;
    }
    return 0;
}

static BOOL V614Install(SOCKET s)
{
    // Find one natural call site in each already-mapped high-value function family.
    g_v614SiteShared = V614FindCallSite(0x80101C40,0x80102430,0x80102918);
    g_v614SiteCommon = V614FindCallSite(0x801065B0,0x80106730,0x800701C0);
    g_v614SiteDev    = V614FindCallSite(0x8014AD58,0x8014AED8,0x8007020C);

    BOOL a=FALSE,b=FALSE,c=FALSE;
    if(g_v614SiteShared) a=V614PatchDirectBranchCallSite(g_v614SiteShared,(DWORD)&Hook_V614_Shared,&g_v614OrigShared);
    if(g_v614SiteCommon) b=V614PatchDirectBranchCallSite(g_v614SiteCommon,(DWORD)&Hook_V614_Common,&g_v614OrigCommon);
    if(g_v614SiteDev)    c=V614PatchDirectBranchCallSite(g_v614SiteDev,(DWORD)&Hook_V614_Dev,&g_v614OrigDev);

    SendLine(s,
        "V614_INSTALL sharedSite=0x%08X sharedOk=%u commonSite=0x%08X commonOk=%u devSite=0x%08X devOk=%u",
        g_v614SiteShared,a?1:0,g_v614SiteCommon,b?1:0,g_v614SiteDev,c?1:0);

    return a||b||c;
}

static void V614RestoreOne(DWORD site,DWORD orig)
{
    if(site && orig && MmIsAddressValid(site)){
        *(volatile DWORD*)site=orig;
        V614FlushCode(site,4);
    }
}

static void V614Restore(SOCKET s)
{
    V614RestoreOne(g_v614SiteShared,g_v614OrigShared);
    V614RestoreOne(g_v614SiteCommon,g_v614OrigCommon);
    V614RestoreOne(g_v614SiteDev,g_v614OrigDev);
    SendLine(s,"V614_RESTORE ok=1");
}

static void V614Report(SOCKET s,LONG phase,const char* label)
{
    SendLine(s,
        "V614_STATUS phase=%ld label=%s field=0x%08X "
        "sharedCalls=%ld sharedBefore=0x%08X sharedAfter=0x%08X "
        "commonCalls=%ld commonBefore=0x%08X commonAfter=0x%08X "
        "devCalls=%ld devBefore=0x%08X devAfter=0x%08X",
        phase,label,V614Field(),
        g_v614SharedCalls,g_v614SharedBefore,g_v614SharedAfter,
        g_v614CommonCalls,g_v614CommonBefore,g_v614CommonAfter,
        g_v614DevCalls,g_v614DevBefore,g_v614DevAfter);
}


static BOOL V615IsStoreTo38(DWORD insn)
{
    // stw/stb/sth/stfs/stfd family with displacement +0x38
    DWORD op=(insn>>26)&0x3F;
    SHORT d=(SHORT)(insn&0xFFFF);
    if(d!=0x38) return FALSE;
    if(op==36 || op==38 || op==44 || op==52 || op==54) return TRUE;
    return FALSE;
}

static void V615ScanFunction(SOCKET s,const char* name,DWORD base,DWORD bytes)
{
    LONG stores38=0, objRefs=0, calls=0;
    SendLine(s,"V615_FUNC_BEGIN name=%s base=0x%08X bytes=0x%X",name,base,bytes);

    for(DWORD pc=base;pc<base+bytes;pc+=4){
        if(!MmIsAddressValid(pc)) continue;
        DWORD insn=*(volatile DWORD*)pc;

        if(V615IsStoreTo38(insn)){
            DWORD ra=(insn>>16)&31;
            DWORD rs=(insn>>21)&31;
            stores38++;
            SendLine(s,"V615_STORE38 func=%s pc=0x%08X insn=0x%08X ra=r%u rs=r%u",
                name,pc,insn,ra,rs);
        }

        BOOL isCall=FALSE;
        DWORD bt=DecodeBranchTargetV64(pc,insn,&isCall);
        if(isCall && bt && MmIsAddressValid(bt)){
            calls++;
            SendLine(s,"V615_CALL func=%s pc=0x%08X target=0x%08X",name,pc,bt);
        }

        if(pc+4<base+bytes && MmIsAddressValid(pc+4)){
            DWORD at=0;
            if(DecodeLisAddiV64(pc,&at)){
                if(at==0x81AACAD0 || at==0x81AACB08){
                    objRefs++;
                    SendLine(s,"V615_OBJREF func=%s pc=0x%08X target=0x%08X",name,pc,at);
                }
            }
        }
    }

    SendLine(s,"V615_FUNC_SUMMARY name=%s stores38=%ld objRefs=%ld calls=%ld",
        name,stores38,objRefs,calls);
    SendLine(s,"V615_FUNC_END name=%s",name);
}

static void RunV615WriterScan(SOCKET s)
{
    SendLine(s,"V615_BEGIN confirmedObject=0x81AACAD0 confirmedField=0x81AACB08");

    V615ScanFunction(s,"XVoicedHeadsetPresent",0x80101C40,0x400);
    V615ScanFunction(s,"XVoicedActivate",0x80101DC8,0x500);
    V615ScanFunction(s,"XVoicedSubmitPacket",0x80102048,0x600);
    V615ScanFunction(s,"XVoicedClose",0x80102230,0x500);

    V615ScanFunction(s,"Shared_80102918",0x80102918,0x600);
    V615ScanFunction(s,"Common_80101C28",0x80101C28,0x300);
    V615ScanFunction(s,"Common_800FDA38",0x800FDA38,0x500);
    V615ScanFunction(s,"Common_801065B0",0x801065B0,0x500);

    V615ScanFunction(s,"Device_800701C0",0x800701C0,0x700);
    V615ScanFunction(s,"Device_8007020C",0x8007020C,0x700);
    V615ScanFunction(s,"MicDeviceRequest",0x8014ADD0,0x700);
    V615ScanFunction(s,"MicInternal_8014AD58",0x8014AD58,0x500);

    SendLine(s,"V615_DONE");
}


static volatile LONG g_v616SubmitCalls=0;
static volatile DWORD g_v616R3=0,g_v616R4=0,g_v616R5=0,g_v616R6=0,g_v616R7=0,g_v616R8=0,g_v616Ret=0;

extern "C" void V616Before(DWORD a,DWORD b,DWORD c,DWORD d,DWORD e,DWORD f)
{
    InterlockedIncrement(&g_v616SubmitCalls);
    g_v616R3=a; g_v616R4=b; g_v616R5=c; g_v616R6=d; g_v616R7=e; g_v616R8=f;
}
extern "C" void V616After(DWORD rc){ g_v616Ret=rc; }

extern "C" __declspec(naked) DWORD Hook_V616_Submit()
{
    __asm {
        stwu r1,-0x90(r1)
        mflr r0
        stw r0,0x94(r1)

        stw r3,0x20(r1); stw r4,0x24(r1); stw r5,0x28(r1); stw r6,0x2C(r1)
        stw r7,0x30(r1); stw r8,0x34(r1)

        bl V616Before

        lwz r3,0x20(r1); lwz r4,0x24(r1); lwz r5,0x28(r1); lwz r6,0x2C(r1)
        lwz r7,0x30(r1); lwz r8,0x34(r1)

        lis r11,0x8170
        addi r11,r11,-0x4B68
        mtctr r11
        bctrl

        stw r3,0x38(r1)
        bl V616After
        lwz r3,0x38(r1)

        lwz r0,0x94(r1)
        mtlr r0
        addi r1,r1,0x90
        blr
    }
}

static BOOL V616Install(SOCKET s)
{
    BOOL ok=PatchStub((volatile DWORD*)0x8A7B4424,(DWORD)&Hook_V616_Submit);
    SendLine(s,"V616_INSTALL ok=%u stub=0x8A7B4424 hook=0x%08X",ok?1:0,(DWORD)&Hook_V616_Submit);
    return ok;
}

static void V616Restore(SOCKET s)
{
    PatchStub((volatile DWORD*)0x8A7B4424,0x816FB498);
    SendLine(s,"V616_RESTORE ok=1");
}

static void V616ProbePointer(SOCKET s,LONG phase,const char* reg,DWORD v)
{
    if(v<0x40000000 || !MmIsAddressValid(v)) {
        SendLine(s,"V616_PTR phase=%ld reg=%s value=0x%08X mapped=0",phase,reg,v);
        return;
    }

    DWORD h40=HashMappedRegion(v,0x40);
    DWORD h100=HashMappedRegion(v,0x100);

    SendLine(s,"V616_PTR phase=%ld reg=%s value=0x%08X class=%s hash40=0x%08X hash100=0x%08X",
        phase,reg,v,PtrClass(v),h40,h100);

    // Dump first 0x40 bytes.
    char tag[32];
    sprintf(tag,"V616_%s",reg);
    SendMappedWords(s,tag,v,0x40);

    // Follow pointer-like words one level deep from first 0x40 bytes.
    for(DWORD off=0;off<0x40;off+=4){
        DWORD child=*(volatile DWORD*)(v+off);
        if(child>=0x40000000 && MmIsAddressValid(child)){
            SendLine(s,"V616_CHILD phase=%ld reg=%s off=0x%02X child=0x%08X class=%s hash40=0x%08X hash100=0x%08X",
                phase,reg,off,child,PtrClass(child),HashMappedRegion(child,0x40),HashMappedRegion(child,0x100));
        }
    }
}

static void V616Snapshot(SOCKET s,LONG phase,const char* label)
{
    SendLine(s,
        "V616_STATUS phase=%ld label=%s calls=%ld r3=0x%08X r4=0x%08X r5=0x%08X r6=0x%08X r7=0x%08X r8=0x%08X ret=0x%08X",
        phase,label,g_v616SubmitCalls,g_v616R3,g_v616R4,g_v616R5,g_v616R6,g_v616R7,g_v616R8,g_v616Ret);

    V616ProbePointer(s,phase,"R3",g_v616R3);
    V616ProbePointer(s,phase,"R4",g_v616R4);
    V616ProbePointer(s,phase,"R5",g_v616R5);
    V616ProbePointer(s,phase,"R6",g_v616R6);
    V616ProbePointer(s,phase,"R7",g_v616R7);
    V616ProbePointer(s,phase,"R8",g_v616R8);
}


#define V617_MAX_CLASSES 8

typedef struct _V617_CLASS {
    DWORD keyR4;
    DWORD keyLen;
    DWORD calls;
    DWORD lastR5;
    DWORD lastChild;
    DWORD lastChildLen;
    DWORD lastHash40;
    DWORD lastHash100;
    DWORD lastAbs;
    DWORD lastNz;
} V617_CLASS;

static volatile LONG g_v617Phase=0;
static V617_CLASS g_v617[V617_MAX_CLASSES];

static DWORD V617HashBytes(DWORD addr,DWORD bytes,DWORD* nz,DWORD* absSum)
{
    if(nz) *nz=0;
    if(absSum) *absSum=0;
    if(bytes==0 || bytes>0x400) return 0;
    if(!MmIsAddressValid(addr) || !MmIsAddressValid(addr+bytes-1)) return 0;

    DWORD h=2166136261u;
    LONG centered=0;
    for(DWORD i=0;i<bytes;i++){
        BYTE v=*(volatile BYTE*)(addr+i);
        h^=v; h*=16777619u;
        if(nz && v) (*nz)++;
        centered=(LONG)v-128;
        if(centered<0) centered=-centered;
        if(absSum) *absSum += (DWORD)centered;
    }
    return h;
}

static int V617GetClass(DWORD r4,DWORD len)
{
    for(int i=0;i<V617_MAX_CLASSES;i++){
        if(g_v617[i].calls && g_v617[i].keyR4==r4 && g_v617[i].keyLen==len)
            return i;
    }
    for(int i=0;i<V617_MAX_CLASSES;i++){
        if(!g_v617[i].calls){
            g_v617[i].keyR4=r4;
            g_v617[i].keyLen=len;
            return i;
        }
    }
    return -1;
}

extern "C" void V617Observe(DWORD r3,DWORD r4,DWORD r5,DWORD r6,DWORD r7,DWORD r8)
{
    DWORD len=0, child=0, childLen=0;

    if(r5>=0x40000000 && MmIsAddressValid(r5) && MmIsAddressValid(r5+0x3F)){
        DWORD w1=*(volatile DWORD*)(r5+0x04);
        DWORD w2=*(volatile DWORD*)(r5+0x08);
        DWORD w3=*(volatile DWORD*)(r5+0x0C);

        // Common observed descriptor patterns:
        // [tag][len][ptr][len] or [type][...][ptr][len]
        if(w1>0 && w1<=0x400) len=w1;
        if(w2>=0x40000000 && MmIsAddressValid(w2)) child=w2;
        if(w3>0 && w3<=0x400) childLen=w3;

        if(!child && MmIsAddressValid(r5+0x28)){
            DWORD c=*(volatile DWORD*)(r5+0x28);
            DWORD l=*(volatile DWORD*)(r5+0x2C);
            if(c>=0x40000000 && MmIsAddressValid(c)){
                child=c;
                if(l>0 && l<=0x400) childLen=l;
            }
        }
    }

    if(!len) len=childLen;
    int cls=V617GetClass(r4,len);
    if(cls<0) return;

    V617_CLASS* c=&g_v617[cls];
    c->calls++;
    c->lastR5=r5;
    c->lastChild=child;
    c->lastChildLen=childLen;

    if(child && childLen){
        DWORD nz=0,absSum=0;
        c->lastHash40=V617HashBytes(child,childLen<0x40?childLen:0x40,&nz,&absSum);
        c->lastHash100=V617HashBytes(child,childLen<0x100?childLen:0x100,&nz,&absSum);
        c->lastAbs=absSum;
        c->lastNz=nz;
    }
}

extern "C" __declspec(naked) DWORD Hook_V617_Submit()
{
    __asm {
        stwu r1,-0xA0(r1)
        mflr r0
        stw r0,0xA4(r1)

        stw r3,0x20(r1); stw r4,0x24(r1); stw r5,0x28(r1); stw r6,0x2C(r1)
        stw r7,0x30(r1); stw r8,0x34(r1)

        bl V617Observe

        lwz r3,0x20(r1); lwz r4,0x24(r1); lwz r5,0x28(r1); lwz r6,0x2C(r1)
        lwz r7,0x30(r1); lwz r8,0x34(r1)

        lis r11,0x8170
        addi r11,r11,-0x4B68
        mtctr r11
        bctrl

        lwz r0,0xA4(r1)
        mtlr r0
        addi r1,r1,0xA0
        blr
    }
}

static BOOL V617Install(SOCKET s)
{
    for(int i=0;i<V617_MAX_CLASSES;i++) ZeroMemory(&g_v617[i],sizeof(V617_CLASS));
    BOOL ok=PatchStub((volatile DWORD*)0x8A7B4424,(DWORD)&Hook_V617_Submit);
    SendLine(s,"V617_INSTALL ok=%u stub=0x8A7B4424 hook=0x%08X",ok?1:0,(DWORD)&Hook_V617_Submit);
    return ok;
}

static void V617Restore(SOCKET s)
{
    PatchStub((volatile DWORD*)0x8A7B4424,0x816FB498);
    SendLine(s,"V617_RESTORE ok=1");
}

static void V617Report(SOCKET s,LONG phase,const char* label)
{
    g_v617Phase=phase;
    SendLine(s,"V617_PHASE phase=%ld label=%s",phase,label);
    for(int i=0;i<V617_MAX_CLASSES;i++){
        V617_CLASS* c=&g_v617[i];
        if(!c->calls) continue;
        SendLine(s,
            "V617_CLASS phase=%ld slot=%d r4=0x%08X len=0x%X calls=%u r5=0x%08X child=0x%08X childLen=0x%X hash40=0x%08X hash100=0x%08X abs=%u nz=%u",
            phase,i,c->keyR4,c->keyLen,c->calls,c->lastR5,c->lastChild,c->lastChildLen,c->lastHash40,c->lastHash100,c->lastAbs,c->lastNz);
    }
}


#define V618_MAX_ROOTS 4
#define V618_MAX_NODES 24
#define V618_MAX_DEPTH 3

typedef struct _V618_NODE {
    DWORD addr;
    DWORD len;
    DWORD hash40;
    DWORD hash100;
    DWORD nz;
    DWORD abs;
    DWORD depth;
    DWORD parent;
    DWORD parentOff;
} V618_NODE;

static V618_NODE g_v618Nodes[V618_MAX_NODES];
static LONG g_v618NodeCount=0;
static volatile DWORD g_v618R3=0,g_v618R4=0,g_v618R5=0,g_v618R6=0,g_v618R7=0,g_v618R8=0;
static volatile LONG g_v618Calls=0;

static DWORD V618Hash(DWORD addr,DWORD bytes,DWORD* nz,DWORD* absSum)
{
    if(nz) *nz=0;
    if(absSum) *absSum=0;
    if(!bytes || bytes>0x400) return 0;
    if(!MmIsAddressValid(addr) || !MmIsAddressValid(addr+bytes-1)) return 0;
    DWORD h=2166136261u;
    for(DWORD i=0;i<bytes;i++){
        BYTE v=*(volatile BYTE*)(addr+i);
        h^=v; h*=16777619u;
        if(nz && v) (*nz)++;
        LONG c=(LONG)v-128; if(c<0)c=-c;
        if(absSum) *absSum += (DWORD)c;
    }
    return h;
}

static BOOL V618Seen(DWORD addr)
{
    for(LONG i=0;i<g_v618NodeCount;i++) if(g_v618Nodes[i].addr==addr) return TRUE;
    return FALSE;
}

static void V618AddNode(DWORD addr,DWORD len,DWORD depth,DWORD parent,DWORD parentOff)
{
    if(g_v618NodeCount>=V618_MAX_NODES) return;
    if(V618Seen(addr)) return;
    if(addr<0x40000000 || !MmIsAddressValid(addr)) return;

    if(len==0 || len>0x400) len=0x100;
    DWORD safeLen=len;
    if(!MmIsAddressValid(addr+safeLen-1)){
        safeLen=0x40;
        if(!MmIsAddressValid(addr+safeLen-1)) return;
    }

    V618_NODE* n=&g_v618Nodes[g_v618NodeCount++];
    ZeroMemory(n,sizeof(V618_NODE));
    n->addr=addr; n->len=safeLen; n->depth=depth; n->parent=parent; n->parentOff=parentOff;
    n->hash40=V618Hash(addr,safeLen<0x40?safeLen:0x40,&n->nz,&n->abs);
    n->hash100=V618Hash(addr,safeLen<0x100?safeLen:0x100,&n->nz,&n->abs);

    if(depth>=V618_MAX_DEPTH) return;

    DWORD scan = safeLen<0x80?safeLen:0x80;
    for(DWORD off=0;off+4<=scan;off+=4){
        DWORD child=*(volatile DWORD*)(addr+off);
        if(child>=0x40000000 && MmIsAddressValid(child)){
            DWORD childLen=0x100;
            if(off+8<=scan){
                DWORD maybeLen=*(volatile DWORD*)(addr+off+4);
                if(maybeLen>0 && maybeLen<=0x400) childLen=maybeLen;
            }
            V618AddNode(child,childLen,depth+1,addr,off);
        }
    }
}

extern "C" void V618Observe(DWORD a,DWORD b,DWORD c,DWORD d,DWORD e,DWORD f)
{
    InterlockedIncrement(&g_v618Calls);
    g_v618R3=a;g_v618R4=b;g_v618R5=c;g_v618R6=d;g_v618R7=e;g_v618R8=f;
}

extern "C" __declspec(naked) DWORD Hook_V618_Submit()
{
    __asm {
        stwu r1,-0x90(r1)
        mflr r0
        stw r0,0x94(r1)
        stw r3,0x20(r1); stw r4,0x24(r1); stw r5,0x28(r1); stw r6,0x2C(r1)
        stw r7,0x30(r1); stw r8,0x34(r1)
        bl V618Observe
        lwz r3,0x20(r1); lwz r4,0x24(r1); lwz r5,0x28(r1); lwz r6,0x2C(r1)
        lwz r7,0x30(r1); lwz r8,0x34(r1)
        lis r11,0x8170
        addi r11,r11,-0x4B68
        mtctr r11
        bctrl
        lwz r0,0x94(r1)
        mtlr r0
        addi r1,r1,0x90
        blr
    }
}

static BOOL V618Install(SOCKET s)
{
    BOOL ok=PatchStub((volatile DWORD*)0x8A7B4424,(DWORD)&Hook_V618_Submit);
    SendLine(s,"V618_INSTALL ok=%u stub=0x8A7B4424 hook=0x%08X",ok?1:0,(DWORD)&Hook_V618_Submit);
    return ok;
}

static void V618Restore(SOCKET s)
{
    PatchStub((volatile DWORD*)0x8A7B4424,0x816FB498);
    SendLine(s,"V618_RESTORE ok=1");
}

static void V618Snapshot(SOCKET s,LONG phase,const char* label)
{
    g_v618NodeCount=0;

    SendLine(s,"V618_STATUS phase=%ld label=%s calls=%ld r3=0x%08X r4=0x%08X r5=0x%08X r6=0x%08X r7=0x%08X r8=0x%08X",
        phase,label,g_v618Calls,g_v618R3,g_v618R4,g_v618R5,g_v618R6,g_v618R7,g_v618R8);

    // Start with mapped register roots. R5/R8 are the most important from v6.16.
    DWORD roots[V618_MAX_ROOTS]={g_v618R5,g_v618R8,g_v618R3,g_v618R7};
    for(int i=0;i<V618_MAX_ROOTS;i++){
        DWORD a=roots[i];
        if(a>=0x40000000 && MmIsAddressValid(a))
            V618AddNode(a,0x100,0,0,(DWORD)i);
    }

    for(LONG i=0;i<g_v618NodeCount;i++){
        V618_NODE* n=&g_v618Nodes[i];
        SendLine(s,
            "V618_NODE phase=%ld idx=%ld depth=%u addr=0x%08X len=0x%X parent=0x%08X parentOff=0x%X hash40=0x%08X hash100=0x%08X abs=%u nz=%u",
            phase,i,n->depth,n->addr,n->len,n->parent,n->parentOff,n->hash40,n->hash100,n->abs,n->nz);
    }

    SendLine(s,"V618_DONE phase=%ld nodes=%ld",phase,g_v618NodeCount);
}


#define V619_RING 256

typedef struct _V619_SAMPLE {
    DWORD seq;
    DWORD r4;
    DWORD r5;
    DWORD r8;
    DWORD r5w0,r5w1,r5w2,r5w3;
    DWORD r5c0,r5c1,r5c2,r5c3;
    DWORD r5h0,r5h1,r5h2,r5h3;
    DWORD r8h40,r8h100;
} V619_SAMPLE;

static V619_SAMPLE g_v619[V619_RING];
static volatile LONG g_v619Write=0;
static volatile LONG g_v619Seq=0;

static DWORD V619HashSafe(DWORD addr,DWORD bytes)
{
    if(addr<0x40000000 || !MmIsAddressValid(addr) || !MmIsAddressValid(addr+bytes-1)) return 0;
    return HashMappedRegion(addr,bytes);
}

extern "C" void V619Observe(DWORD r3,DWORD r4,DWORD r5,DWORD r6,DWORD r7,DWORD r8)
{
    LONG wi=InterlockedIncrement(&g_v619Write)-1;
    LONG seq=InterlockedIncrement(&g_v619Seq);
    V619_SAMPLE* s=&g_v619[wi & (V619_RING-1)];
    ZeroMemory(s,sizeof(V619_SAMPLE));
    s->seq=(DWORD)seq;
    s->r4=r4; s->r5=r5; s->r8=r8;

    if(r5>=0x40000000 && MmIsAddressValid(r5) && MmIsAddressValid(r5+0x7F)){
        s->r5w0=*(volatile DWORD*)(r5+0x00);
        s->r5w1=*(volatile DWORD*)(r5+0x04);
        s->r5w2=*(volatile DWORD*)(r5+0x08);
        s->r5w3=*(volatile DWORD*)(r5+0x0C);

        DWORD offs[4]={0x08,0x24,0x40,0x5C};
        for(int i=0;i<4;i++){
            DWORD c=*(volatile DWORD*)(r5+offs[i]);
            DWORD h=0;
            if(c>=0x40000000 && MmIsAddressValid(c) && MmIsAddressValid(c+0x3F))
                h=HashMappedRegion(c,0x40);
            if(i==0){s->r5c0=c;s->r5h0=h;}
            if(i==1){s->r5c1=c;s->r5h1=h;}
            if(i==2){s->r5c2=c;s->r5h2=h;}
            if(i==3){s->r5c3=c;s->r5h3=h;}
        }
    }

    if(r8>=0x40000000 && MmIsAddressValid(r8)){
        s->r8h40=V619HashSafe(r8,0x40);
        s->r8h100=V619HashSafe(r8,0x100);
    }
}

extern "C" __declspec(naked) DWORD Hook_V619_Submit()
{
    __asm {
        stwu r1,-0x90(r1)
        mflr r0
        stw r0,0x94(r1)
        stw r3,0x20(r1); stw r4,0x24(r1); stw r5,0x28(r1); stw r6,0x2C(r1)
        stw r7,0x30(r1); stw r8,0x34(r1)

        bl V619Observe

        lwz r3,0x20(r1); lwz r4,0x24(r1); lwz r5,0x28(r1); lwz r6,0x2C(r1)
        lwz r7,0x30(r1); lwz r8,0x34(r1)

        lis r11,0x8170
        addi r11,r11,-0x4B68
        mtctr r11
        bctrl

        lwz r0,0x94(r1)
        mtlr r0
        addi r1,r1,0x90
        blr
    }
}

static BOOL V619Install(SOCKET s)
{
    ZeroMemory(g_v619,sizeof(g_v619));
    g_v619Write=0; g_v619Seq=0;
    BOOL ok=PatchStub((volatile DWORD*)0x8A7B4424,(DWORD)&Hook_V619_Submit);
    SendLine(s,"V619_INSTALL ok=%u stub=0x8A7B4424 hook=0x%08X",ok?1:0,(DWORD)&Hook_V619_Submit);
    return ok;
}

static void V619Restore(SOCKET s)
{
    PatchStub((volatile DWORD*)0x8A7B4424,0x816FB498);
    SendLine(s,"V619_RESTORE ok=1");
}

static void V619Mark(SOCKET s,LONG phase,const char* label)
{
    SendLine(s,"V619_MARK phase=%ld label=%s write=%ld seq=%ld",phase,label,g_v619Write,g_v619Seq);
}

static void V619DumpRecent(SOCKET s,LONG phase,const char* label)
{
    LONG total=g_v619Write;
    LONG count=total<V619_RING?total:V619_RING;
    LONG start=total-count;

    SendLine(s,"V619_DUMP_BEGIN phase=%ld label=%s count=%ld total=%ld",phase,label,count,total);

    for(LONG j=0;j<count;j++){
        V619_SAMPLE* x=&g_v619[(start+j)&(V619_RING-1)];
        SendLine(s,
            "V619_SAMPLE phase=%ld seq=%u r4=0x%08X r5=0x%08X r8=0x%08X "
            "w0=0x%08X w1=0x%08X w2=0x%08X w3=0x%08X "
            "c0=0x%08X h0=0x%08X c1=0x%08X h1=0x%08X "
            "c2=0x%08X h2=0x%08X c3=0x%08X h3=0x%08X "
            "r8h40=0x%08X r8h100=0x%08X",
            phase,x->seq,x->r4,x->r5,x->r8,
            x->r5w0,x->r5w1,x->r5w2,x->r5w3,
            x->r5c0,x->r5h0,x->r5c1,x->r5h1,
            x->r5c2,x->r5h2,x->r5c3,x->r5h3,
            x->r8h40,x->r8h100);
    }

    SendLine(s,"V619_DUMP_END phase=%ld label=%s",phase,label);
}


#define V620_WINDOW 128

typedef struct _V620_SAMPLE {
    DWORD seq;
    DWORD r4;
    DWORD r5;
    DWORD r8;
    DWORD w0,w1,w2,w3;
    DWORD c0,h0,c1,h1,c2,h2,c3,h3;
    DWORD r8h40,r8h100;
} V620_SAMPLE;

static V620_SAMPLE g_v620Live[V620_WINDOW];
static V620_SAMPLE g_v620Phase1[V620_WINDOW];
static V620_SAMPLE g_v620Phase2[V620_WINDOW];
static V620_SAMPLE g_v620Phase3[V620_WINDOW];

static volatile LONG g_v620Write=0;
static volatile LONG g_v620Seq=0;

static DWORD V620HashSafe(DWORD addr,DWORD bytes)
{
    if(addr<0x40000000 || !MmIsAddressValid(addr) || !MmIsAddressValid(addr+bytes-1)) return 0;
    return HashMappedRegion(addr,bytes);
}

extern "C" void V620Observe(DWORD r3,DWORD r4,DWORD r5,DWORD r6,DWORD r7,DWORD r8)
{
    LONG wi=InterlockedIncrement(&g_v620Write)-1;
    LONG seq=InterlockedIncrement(&g_v620Seq);
    V620_SAMPLE* s=&g_v620Live[wi & (V620_WINDOW-1)];
    ZeroMemory(s,sizeof(V620_SAMPLE));
    s->seq=(DWORD)seq;
    s->r4=r4; s->r5=r5; s->r8=r8;

    if(r5>=0x40000000 && MmIsAddressValid(r5) && MmIsAddressValid(r5+0x7F)){
        s->w0=*(volatile DWORD*)(r5+0x00);
        s->w1=*(volatile DWORD*)(r5+0x04);
        s->w2=*(volatile DWORD*)(r5+0x08);
        s->w3=*(volatile DWORD*)(r5+0x0C);

        DWORD offs[4]={0x08,0x24,0x40,0x5C};
        for(int i=0;i<4;i++){
            DWORD c=*(volatile DWORD*)(r5+offs[i]);
            DWORD h=0;
            if(c>=0x40000000 && MmIsAddressValid(c) && MmIsAddressValid(c+0x3F))
                h=HashMappedRegion(c,0x40);
            if(i==0){s->c0=c;s->h0=h;}
            if(i==1){s->c1=c;s->h1=h;}
            if(i==2){s->c2=c;s->h2=h;}
            if(i==3){s->c3=c;s->h3=h;}
        }
    }

    if(r8>=0x40000000 && MmIsAddressValid(r8)){
        s->r8h40=V620HashSafe(r8,0x40);
        s->r8h100=V620HashSafe(r8,0x100);
    }
}

extern "C" __declspec(naked) DWORD Hook_V620_Submit()
{
    __asm {
        stwu r1,-0x90(r1)
        mflr r0
        stw r0,0x94(r1)
        stw r3,0x20(r1); stw r4,0x24(r1); stw r5,0x28(r1); stw r6,0x2C(r1)
        stw r7,0x30(r1); stw r8,0x34(r1)
        bl V620Observe
        lwz r3,0x20(r1); lwz r4,0x24(r1); lwz r5,0x28(r1); lwz r6,0x2C(r1)
        lwz r7,0x30(r1); lwz r8,0x34(r1)
        lis r11,0x8170
        addi r11,r11,-0x4B68
        mtctr r11
        bctrl
        lwz r0,0x94(r1)
        mtlr r0
        addi r1,r1,0x90
        blr
    }
}

static BOOL V620Install(SOCKET s)
{
    ZeroMemory(g_v620Live,sizeof(g_v620Live));
    ZeroMemory(g_v620Phase1,sizeof(g_v620Phase1));
    ZeroMemory(g_v620Phase2,sizeof(g_v620Phase2));
    ZeroMemory(g_v620Phase3,sizeof(g_v620Phase3));
    g_v620Write=0; g_v620Seq=0;
    BOOL ok=PatchStub((volatile DWORD*)0x8A7B4424,(DWORD)&Hook_V620_Submit);
    SendLine(s,"V620_INSTALL ok=%u stub=0x8A7B4424 hook=0x%08X",ok?1:0,(DWORD)&Hook_V620_Submit);
    return ok;
}

static void V620Restore(SOCKET s)
{
    PatchStub((volatile DWORD*)0x8A7B4424,0x816FB498);
    SendLine(s,"V620_RESTORE ok=1");
}

static void V620Freeze(V620_SAMPLE* dst,SOCKET s,LONG phase,const char* label)
{
    LONG total=g_v620Write;
    LONG count=total<V620_WINDOW?total:V620_WINDOW;
    LONG start=total-count;

    ZeroMemory(dst,sizeof(V620_SAMPLE)*V620_WINDOW);
    for(LONG j=0;j<count;j++)
        dst[j]=g_v620Live[(start+j)&(V620_WINDOW-1)];

    SendLine(s,"V620_FREEZE phase=%ld label=%s count=%ld total=%ld seq=%ld",
        phase,label,count,total,g_v620Seq);
}

static void V620DumpPhase(SOCKET s,LONG phase,const char* label,V620_SAMPLE* src)
{
    SendLine(s,"V620_DUMP_BEGIN phase=%ld label=%s",phase,label);
    for(int j=0;j<V620_WINDOW;j++){
        V620_SAMPLE* x=&src[j];
        if(!x->seq) continue;
        SendLine(s,
            "V620_SAMPLE phase=%ld seq=%u r4=0x%08X r5=0x%08X r8=0x%08X "
            "w0=0x%08X w1=0x%08X w2=0x%08X w3=0x%08X "
            "c0=0x%08X h0=0x%08X c1=0x%08X h1=0x%08X "
            "c2=0x%08X h2=0x%08X c3=0x%08X h3=0x%08X "
            "r8h40=0x%08X r8h100=0x%08X",
            phase,x->seq,x->r4,x->r5,x->r8,
            x->w0,x->w1,x->w2,x->w3,
            x->c0,x->h0,x->c1,x->h1,
            x->c2,x->h2,x->c3,x->h3,
            x->r8h40,x->r8h100);
    }
    SendLine(s,"V620_DUMP_END phase=%ld label=%s",phase,label);
}

static void V620DumpAll(SOCKET s)
{
    V620DumpPhase(s,1,"QUIET_A",g_v620Phase1);
    V620DumpPhase(s,2,"TALK",g_v620Phase2);
    V620DumpPhase(s,3,"QUIET_B",g_v620Phase3);
    SendLine(s,"V620_DONE");
}


#define V621_MAX_CAND 32
#define V621_DEPTH 4

typedef struct _V621_CAND {
    DWORD addr;
    DWORD parent;
    DWORD off;
    DWORD depth;
    DWORD h40;
    DWORD h100;
    DWORD nz;
    DWORD abs;
} V621_CAND;

static V621_CAND g_v621[V621_MAX_CAND];
static LONG g_v621Count=0;

static BOOL V621UserPtr(DWORD a)
{
    return (a>=0x40000000 && a<0x80000000 && MmIsAddressValid(a));
}

static DWORD V621Hash(DWORD addr,DWORD bytes,DWORD* nz,DWORD* absSum)
{
    if(nz) *nz=0;
    if(absSum) *absSum=0;
    if(!V621UserPtr(addr)) return 0;
    if(!MmIsAddressValid(addr+bytes-1)) return 0;

    DWORD h=2166136261u;
    for(DWORD i=0;i<bytes;i++){
        BYTE v=*(volatile BYTE*)(addr+i);
        h^=v; h*=16777619u;
        if(nz && v) (*nz)++;
        LONG c=(LONG)v-128; if(c<0)c=-c;
        if(absSum) *absSum += (DWORD)c;
    }
    return h;
}

static BOOL V621Seen(DWORD addr)
{
    for(LONG i=0;i<g_v621Count;i++) if(g_v621[i].addr==addr) return TRUE;
    return FALSE;
}

static void V621Walk(DWORD addr,DWORD depth,DWORD parent,DWORD off)
{
    if(g_v621Count>=V621_MAX_CAND || depth>V621_DEPTH) return;
    if(!V621UserPtr(addr) || V621Seen(addr)) return;
    if(!MmIsAddressValid(addr+0x7F)) return;

    V621_CAND* c=&g_v621[g_v621Count++];
    ZeroMemory(c,sizeof(V621_CAND));
    c->addr=addr;c->depth=depth;c->parent=parent;c->off=off;
    c->h40=V621Hash(addr,0x40,&c->nz,&c->abs);
    c->h100=V621Hash(addr,0x100,&c->nz,&c->abs);

    if(depth>=V621_DEPTH) return;

    for(DWORD o=0;o<0x80;o+=4){
        DWORD child=*(volatile DWORD*)(addr+o);
        if(V621UserPtr(child))
            V621Walk(child,depth+1,addr,o);
    }
}

static void V621ScoreSnapshot(SOCKET s,LONG phase,const char* label,DWORD root)
{
    g_v621Count=0;
    SendLine(s,"V621_BEGIN phase=%ld label=%s root=0x%08X",phase,label,root);

    if(V621UserPtr(root)) V621Walk(root,0,0,0);

    for(LONG i=0;i<g_v621Count;i++){
        V621_CAND* c=&g_v621[i];
        SendLine(s,
            "V621_CAND phase=%ld idx=%ld depth=%u addr=0x%08X parent=0x%08X off=0x%X h40=0x%08X h100=0x%08X abs=%u nz=%u",
            phase,i,c->depth,c->addr,c->parent,c->off,c->h40,c->h100,c->abs,c->nz);
    }

    SendLine(s,"V621_DONE phase=%ld count=%ld",phase,g_v621Count);
}

static volatile DWORD g_v621LastR5=0;
static volatile DWORD g_v621LastR4=0;
static volatile LONG g_v621Calls=0;

extern "C" void V621Observe(DWORD r3,DWORD r4,DWORD r5,DWORD r6,DWORD r7,DWORD r8)
{
    InterlockedIncrement(&g_v621Calls);
    g_v621LastR4=r4;
    g_v621LastR5=r5;
}

extern "C" __declspec(naked) DWORD Hook_V621_Submit()
{
    __asm {
        stwu r1,-0x90(r1)
        mflr r0
        stw r0,0x94(r1)
        stw r3,0x20(r1); stw r4,0x24(r1); stw r5,0x28(r1); stw r6,0x2C(r1)
        stw r7,0x30(r1); stw r8,0x34(r1)

        bl V621Observe

        lwz r3,0x20(r1); lwz r4,0x24(r1); lwz r5,0x28(r1); lwz r6,0x2C(r1)
        lwz r7,0x30(r1); lwz r8,0x34(r1)

        lis r11,0x8170
        addi r11,r11,-0x4B68
        mtctr r11
        bctrl

        lwz r0,0x94(r1)
        mtlr r0
        addi r1,r1,0x90
        blr
    }
}

static BOOL V621Install(SOCKET s)
{
    BOOL ok=PatchStub((volatile DWORD*)0x8A7B4424,(DWORD)&Hook_V621_Submit);
    SendLine(s,"V621_INSTALL ok=%u stub=0x8A7B4424 hook=0x%08X",ok?1:0,(DWORD)&Hook_V621_Submit);
    return ok;
}

static void V621Restore(SOCKET s)
{
    PatchStub((volatile DWORD*)0x8A7B4424,0x816FB498);
    SendLine(s,"V621_RESTORE ok=1");
}

static void V621Capture(SOCKET s,LONG phase,const char* label)
{
    DWORD root=g_v621LastR5;
    SendLine(s,"V621_STATUS phase=%ld label=%s calls=%ld r4=0x%08X r5=0x%08X",
        phase,label,g_v621Calls,g_v621LastR4,g_v621LastR5);
    V621ScoreSnapshot(s,phase,label,root);
}


typedef struct _V622_METRIC {
    DWORD addr;
    DWORD hash;
    DWORD nz;
    DWORD abs;
    DWORD minv;
    DWORD maxv;
    DWORD sum;
    DWORD sumsq;
} V622_METRIC;

static volatile DWORD g_v622R5_0=0;
static volatile DWORD g_v622R5_1=0;
static volatile LONG g_v622Calls0=0;
static volatile LONG g_v622Calls1=0;

extern "C" void V622Observe(DWORD r3,DWORD r4,DWORD r5,DWORD r6,DWORD r7,DWORD r8)
{
    if(r4==0){
        g_v622R5_0=r5;
        InterlockedIncrement(&g_v622Calls0);
    } else if(r4==1){
        g_v622R5_1=r5;
        InterlockedIncrement(&g_v622Calls1);
    }
}

extern "C" __declspec(naked) DWORD Hook_V622_Submit()
{
    __asm {
        stwu r1,-0x90(r1)
        mflr r0
        stw r0,0x94(r1)
        stw r3,0x20(r1); stw r4,0x24(r1); stw r5,0x28(r1); stw r6,0x2C(r1)
        stw r7,0x30(r1); stw r8,0x34(r1)

        bl V622Observe

        lwz r3,0x20(r1); lwz r4,0x24(r1); lwz r5,0x28(r1); lwz r6,0x2C(r1)
        lwz r7,0x30(r1); lwz r8,0x34(r1)

        lis r11,0x8170
        addi r11,r11,-0x4B68
        mtctr r11
        bctrl

        lwz r0,0x94(r1)
        mtlr r0
        addi r1,r1,0x90
        blr
    }
}

static BOOL V622Install(SOCKET s)
{
    g_v622R5_0=0; g_v622R5_1=0; g_v622Calls0=0; g_v622Calls1=0;
    BOOL ok=PatchStub((volatile DWORD*)0x8A7B4424,(DWORD)&Hook_V622_Submit);
    SendLine(s,"V622_INSTALL ok=%u stub=0x8A7B4424 hook=0x%08X",ok?1:0,(DWORD)&Hook_V622_Submit);
    return ok;
}

static void V622Restore(SOCKET s)
{
    PatchStub((volatile DWORD*)0x8A7B4424,0x816FB498);
    SendLine(s,"V622_RESTORE ok=1");
}

static BOOL V622UserPtr(DWORD a)
{
    return (a>=0x40000000 && a<0x80000000 && MmIsAddressValid(a));
}

static void V622Metric(DWORD addr,DWORD bytes,V622_METRIC* m)
{
    ZeroMemory(m,sizeof(V622_METRIC));
    m->addr=addr;
    if(!V622UserPtr(addr) || !MmIsAddressValid(addr+bytes-1)) return;

    DWORD h=2166136261u;
    DWORD minv=255,maxv=0,nz=0,abs=0,sum=0,sumsq=0;
    for(DWORD i=0;i<bytes;i++){
        BYTE v=*(volatile BYTE*)(addr+i);
        h^=v; h*=16777619u;
        if(v) nz++;
        if(v<minv) minv=v;
        if(v>maxv) maxv=v;
        sum += v;
        sumsq += ((DWORD)v)*((DWORD)v);
        LONG c=(LONG)v-128; if(c<0)c=-c;
        abs += (DWORD)c;
    }
    m->hash=h;m->nz=nz;m->abs=abs;m->minv=minv;m->maxv=maxv;m->sum=sum;m->sumsq=sumsq;
}

static void V622ReportTree(SOCKET s,LONG phase,const char* label,DWORD cls,DWORD root)
{
    SendLine(s,"V622_CLASS phase=%ld label=%s class=%u root=0x%08X",phase,label,cls,root);
    if(!V622UserPtr(root) || !MmIsAddressValid(root+0x7F)) return;

    // Score root and every mapped pointer in the first 0x80 bytes.
    V622_METRIC m;
    V622Metric(root,0x100,&m);
    SendLine(s,"V622_NODE phase=%ld class=%u path=root addr=0x%08X hash=0x%08X nz=%u abs=%u min=%u max=%u sum=%u sumsq=%u",
        phase,cls,m.addr,m.hash,m.nz,m.abs,m.minv,m.maxv,m.sum,m.sumsq);

    for(DWORD off=0;off<0x80;off+=4){
        DWORD child=*(volatile DWORD*)(root+off);
        if(!V622UserPtr(child)) continue;

        V622Metric(child,0x100,&m);
        SendLine(s,"V622_NODE phase=%ld class=%u path=root+0x%02X addr=0x%08X hash=0x%08X nz=%u abs=%u min=%u max=%u sum=%u sumsq=%u",
            phase,cls,off,m.addr,m.hash,m.nz,m.abs,m.minv,m.maxv,m.sum,m.sumsq);

        if(!MmIsAddressValid(child+0x7F)) continue;
        for(DWORD off2=0;off2<0x80;off2+=4){
            DWORD grand=*(volatile DWORD*)(child+off2);
            if(!V622UserPtr(grand)) continue;

            V622Metric(grand,0x100,&m);
            SendLine(s,"V622_NODE phase=%ld class=%u path=root+0x%02X+0x%02X addr=0x%08X hash=0x%08X nz=%u abs=%u min=%u max=%u sum=%u sumsq=%u",
                phase,cls,off,off2,m.addr,m.hash,m.nz,m.abs,m.minv,m.maxv,m.sum,m.sumsq);
        }
    }
}

static void V622Capture(SOCKET s,LONG phase,const char* label)
{
    SendLine(s,"V622_STATUS phase=%ld label=%s calls0=%ld r5_0=0x%08X calls1=%ld r5_1=0x%08X",
        phase,label,g_v622Calls0,g_v622R5_0,g_v622Calls1,g_v622R5_1);

    V622ReportTree(s,phase,label,0,g_v622R5_0);
    V622ReportTree(s,phase,label,1,g_v622R5_1);
    SendLine(s,"V622_DONE phase=%ld",phase);
}


#define V623_MAX_NODES 20

typedef struct _V623_NODE {
    DWORD pathA;
    DWORD pathB;
    DWORD addr;
    DWORD hash;
    DWORD nz;
    DWORD abs;
    DWORD sum;
    DWORD sumsq;
} V623_NODE;

static volatile DWORD g_v623R5_1=0;
static volatile LONG g_v623Calls1=0;

extern "C" void V623Observe(DWORD r3,DWORD r4,DWORD r5,DWORD r6,DWORD r7,DWORD r8)
{
    if(r4==1){
        g_v623R5_1=r5;
        InterlockedIncrement(&g_v623Calls1);
    }
}

extern "C" __declspec(naked) DWORD Hook_V623_Submit()
{
    __asm {
        stwu r1,-0x90(r1)
        mflr r0
        stw r0,0x94(r1)
        stw r3,0x20(r1); stw r4,0x24(r1); stw r5,0x28(r1); stw r6,0x2C(r1)
        stw r7,0x30(r1); stw r8,0x34(r1)

        bl V623Observe

        lwz r3,0x20(r1); lwz r4,0x24(r1); lwz r5,0x28(r1); lwz r6,0x2C(r1)
        lwz r7,0x30(r1); lwz r8,0x34(r1)

        lis r11,0x8170
        addi r11,r11,-0x4B68
        mtctr r11
        bctrl

        lwz r0,0x94(r1)
        mtlr r0
        addi r1,r1,0x90
        blr
    }
}

static BOOL V623Install(SOCKET s)
{
    g_v623R5_1=0; g_v623Calls1=0;
    BOOL ok=PatchStub((volatile DWORD*)0x8A7B4424,(DWORD)&Hook_V623_Submit);
    SendLine(s,"V623_INSTALL ok=%u stub=0x8A7B4424 hook=0x%08X",ok?1:0,(DWORD)&Hook_V623_Submit);
    return ok;
}

static void V623Restore(SOCKET s)
{
    PatchStub((volatile DWORD*)0x8A7B4424,0x816FB498);
    SendLine(s,"V623_RESTORE ok=1");
}

static BOOL V623UserPtr(DWORD a)
{
    return (a>=0x40000000 && a<0x80000000 && MmIsAddressValid(a));
}

static void V623Metric(DWORD addr,DWORD bytes,DWORD* hash,DWORD* nz,DWORD* abs,DWORD* sum,DWORD* sumsq)
{
    *hash=*nz=*abs=*sum=*sumsq=0;
    if(!V623UserPtr(addr) || !MmIsAddressValid(addr+bytes-1)) return;

    DWORD h=2166136261u;
    for(DWORD i=0;i<bytes;i++){
        BYTE v=*(volatile BYTE*)(addr+i);
        h^=v; h*=16777619u;
        if(v) (*nz)++;
        LONG c=(LONG)v-128; if(c<0)c=-c;
        *abs += (DWORD)c;
        *sum += v;
        *sumsq += ((DWORD)v)*((DWORD)v);
    }
    *hash=h;
}

static void V623EmitNode(SOCKET s,LONG phase,const char* label,const char* path,DWORD addr)
{
    DWORD h,nz,ab,su,ss;
    V623Metric(addr,0x100,&h,&nz,&ab,&su,&ss);
    SendLine(s,
        "V623_NODE phase=%ld label=%s path=%s addr=0x%08X hash=0x%08X nz=%u abs=%u sum=%u sumsq=%u",
        phase,label,path,addr,h,nz,ab,su,ss);
}

static void V623Capture(SOCKET s,LONG phase,const char* label)
{
    DWORD root=g_v623R5_1;
    SendLine(s,"V623_STATUS phase=%ld label=%s calls1=%ld root=0x%08X",
        phase,label,g_v623Calls1,root);

    if(!V623UserPtr(root) || !MmIsAddressValid(root+0x7F)){
        SendLine(s,"V623_DONE phase=%ld nodes=0",phase);
        return;
    }

    // Always score the root.
    V623EmitNode(s,phase,label,"root",root);

    // Specifically follow the speech-interest branch discovered in v6.22:
    // root + 0x48 -> 0x40200180 family.
    DWORD p48=*(volatile DWORD*)(root+0x48);
    if(V623UserPtr(p48) && MmIsAddressValid(p48+0x7F)){
        V623EmitNode(s,phase,label,"root+48",p48);

        // Score every pointer-like field in first 0x80 bytes of the +0x48 node.
        for(DWORD off=0;off<0x80;off+=4){
            DWORD child=*(volatile DWORD*)(p48+off);
            if(V623UserPtr(child)){
                char path[32];
                sprintf(path,"root+48+%02X",off);
                V623EmitNode(s,phase,label,path,child);
            }
        }
    }

    // Also retain immediate root children so we don't miss a neighboring branch.
    for(DWORD off=0;off<0x80;off+=4){
        if(off==0x48) continue;
        DWORD child=*(volatile DWORD*)(root+off);
        if(V623UserPtr(child)){
            char path[32];
            sprintf(path,"root+%02X",off);
            V623EmitNode(s,phase,label,path,child);
        }
    }

    SendLine(s,"V623_DONE phase=%ld",phase);
}


static volatile DWORD g_v624R5_1=0;
static volatile LONG g_v624Calls1=0;

extern "C" void V624Observe(DWORD r3,DWORD r4,DWORD r5,DWORD r6,DWORD r7,DWORD r8)
{
    if(r4==1){
        g_v624R5_1=r5;
        InterlockedIncrement(&g_v624Calls1);
    }
}

extern "C" __declspec(naked) DWORD Hook_V624_Submit()
{
    __asm {
        stwu r1,-0x90(r1)
        mflr r0
        stw r0,0x94(r1)
        stw r3,0x20(r1); stw r4,0x24(r1); stw r5,0x28(r1); stw r6,0x2C(r1)
        stw r7,0x30(r1); stw r8,0x34(r1)

        bl V624Observe

        lwz r3,0x20(r1); lwz r4,0x24(r1); lwz r5,0x28(r1); lwz r6,0x2C(r1)
        lwz r7,0x30(r1); lwz r8,0x34(r1)

        lis r11,0x8170
        addi r11,r11,-0x4B68
        mtctr r11
        bctrl

        lwz r0,0x94(r1)
        mtlr r0
        addi r1,r1,0x90
        blr
    }
}

static BOOL V624Install(SOCKET s)
{
    g_v624R5_1=0; g_v624Calls1=0;
    BOOL ok=PatchStub((volatile DWORD*)0x8A7B4424,(DWORD)&Hook_V624_Submit);
    SendLine(s,"V624_INSTALL ok=%u stub=0x8A7B4424 hook=0x%08X",ok?1:0,(DWORD)&Hook_V624_Submit);
    return ok;
}

static void V624Restore(SOCKET s)
{
    PatchStub((volatile DWORD*)0x8A7B4424,0x816FB498);
    SendLine(s,"V624_RESTORE ok=1");
}

static BOOL V624UserPtr(DWORD a)
{
    return (a>=0x40000000 && a<0x80000000 && MmIsAddressValid(a));
}

static void V624Metric(SOCKET s,LONG phase,const char* label,const char* path,DWORD addr,DWORD bytes)
{
    if(!V624UserPtr(addr) || !MmIsAddressValid(addr+bytes-1)){
        SendLine(s,"V624_METRIC phase=%ld label=%s path=%s addr=0x%08X mapped=0",phase,label,path,addr);
        return;
    }

    DWORD h=2166136261u,nz=0,sum=0,sumsq=0,minv=255,maxv=0;
    for(DWORD i=0;i<bytes;i++){
        BYTE v=*(volatile BYTE*)(addr+i);
        h^=v; h*=16777619u;
        if(v) nz++;
        if(v<minv) minv=v;
        if(v>maxv) maxv=v;
        sum+=v;
        sumsq+=((DWORD)v)*((DWORD)v);
    }

    SendLine(s,
        "V624_METRIC phase=%ld label=%s path=%s addr=0x%08X bytes=0x%X hash=0x%08X nz=%u min=%u max=%u sum=%u sumsq=%u",
        phase,label,path,addr,bytes,h,nz,minv,maxv,sum,sumsq);
}

static void V624Capture(SOCKET s,LONG phase,const char* label)
{
    DWORD root=g_v624R5_1;
    SendLine(s,"V624_STATUS phase=%ld label=%s calls1=%ld root=0x%08X",phase,label,g_v624Calls1,root);

    if(!V624UserPtr(root) || !MmIsAddressValid(root+0x7F)){
        SendLine(s,"V624_DONE phase=%ld",phase);
        return;
    }

    DWORD p48=*(volatile DWORD*)(root+0x48);
    DWORD p00=0;
    if(V624UserPtr(p48) && MmIsAddressValid(p48+0x7F))
        p00=*(volatile DWORD*)(p48+0x00);

    SendLine(s,"V624_CHAIN phase=%ld root=0x%08X p48=0x%08X p00=0x%08X",phase,root,p48,p00);

    V624Metric(s,phase,label,"root",root,0x100);
    V624Metric(s,phase,label,"root+48",p48,0x100);
    V624Metric(s,phase,label,"root+48+00",p00,0x100);

    if(V624UserPtr(p00) && MmIsAddressValid(p00+0x7F)){
        for(DWORD off=0;off<0x80;off+=4){
            DWORD child=*(volatile DWORD*)(p00+off);
            if(V624UserPtr(child)){
                char path[40];
                sprintf(path,"root+48+00+%02X",off);
                V624Metric(s,phase,label,path,child,0x100);
            }
        }

        // Also dump first 0x80 bytes for structure inspection.
        SendMappedWords(s,"V624_P00_WORDS",p00,0x80);
    }

    SendLine(s,"V624_DONE phase=%ld",phase);
}


#define V625_RING 96

typedef struct _V625_SAMPLE {
    DWORD seq;
    DWORD r3,r4,r5,r6,r7,r8;
    DWORD w[16];
    DWORD hash100;
} V625_SAMPLE;

static V625_SAMPLE g_v625[V625_RING];
static volatile LONG g_v625Write=0;
static volatile LONG g_v625Seq=0;

extern "C" void V625Observe(DWORD r3,DWORD r4,DWORD r5,DWORD r6,DWORD r7,DWORD r8)
{
    if(r4!=1) return;

    LONG wi=InterlockedIncrement(&g_v625Write)-1;
    LONG seq=InterlockedIncrement(&g_v625Seq);
    V625_SAMPLE* s=&g_v625[wi % V625_RING];
    ZeroMemory(s,sizeof(V625_SAMPLE));

    s->seq=(DWORD)seq;
    s->r3=r3;s->r4=r4;s->r5=r5;s->r6=r6;s->r7=r7;s->r8=r8;

    if(r5>=0x40000000 && r5<0x80000000 && MmIsAddressValid(r5) && MmIsAddressValid(r5+0xFF)){
        for(int i=0;i<16;i++) s->w[i]=*(volatile DWORD*)(r5+i*4);
        s->hash100=HashMappedRegion(r5,0x100);
    }
}

extern "C" __declspec(naked) DWORD Hook_V625_Submit()
{
    __asm {
        stwu r1,-0x90(r1)
        mflr r0
        stw r0,0x94(r1)
        stw r3,0x20(r1); stw r4,0x24(r1); stw r5,0x28(r1); stw r6,0x2C(r1)
        stw r7,0x30(r1); stw r8,0x34(r1)

        bl V625Observe

        lwz r3,0x20(r1); lwz r4,0x24(r1); lwz r5,0x28(r1); lwz r6,0x2C(r1)
        lwz r7,0x30(r1); lwz r8,0x34(r1)

        lis r11,0x8170
        addi r11,r11,-0x4B68
        mtctr r11
        bctrl

        lwz r0,0x94(r1)
        mtlr r0
        addi r1,r1,0x90
        blr
    }
}

static BOOL V625Install(SOCKET s)
{
    ZeroMemory(g_v625,sizeof(g_v625));
    g_v625Write=0;g_v625Seq=0;
    BOOL ok=PatchStub((volatile DWORD*)0x8A7B4424,(DWORD)&Hook_V625_Submit);
    SendLine(s,"V625_INSTALL ok=%u stub=0x8A7B4424 hook=0x%08X",ok?1:0,(DWORD)&Hook_V625_Submit);
    return ok;
}

static void V625Restore(SOCKET s)
{
    PatchStub((volatile DWORD*)0x8A7B4424,0x816FB498);
    SendLine(s,"V625_RESTORE ok=1");
}

static void V625Dump(SOCKET s,LONG phase,const char* label)
{
    LONG total=g_v625Write;
    LONG count=total<V625_RING?total:V625_RING;
    LONG start=total-count;

    SendLine(s,"V625_DUMP_BEGIN phase=%ld label=%s count=%ld total=%ld",phase,label,count,total);

    for(LONG i=0;i<count;i++){
        V625_SAMPLE* x=&g_v625[(start+i)%V625_RING];
        SendLine(s,
            "V625_SAMPLE phase=%ld seq=%u r3=0x%08X r5=0x%08X r6=0x%08X r7=0x%08X r8=0x%08X hash100=0x%08X "
            "w0=0x%08X w1=0x%08X w2=0x%08X w3=0x%08X w4=0x%08X w5=0x%08X w6=0x%08X w7=0x%08X "
            "w8=0x%08X w9=0x%08X wA=0x%08X wB=0x%08X wC=0x%08X wD=0x%08X wE=0x%08X wF=0x%08X",
            phase,x->seq,x->r3,x->r5,x->r6,x->r7,x->r8,x->hash100,
            x->w[0],x->w[1],x->w[2],x->w[3],x->w[4],x->w[5],x->w[6],x->w[7],
            x->w[8],x->w[9],x->w[10],x->w[11],x->w[12],x->w[13],x->w[14],x->w[15]);
    }

    SendLine(s,"V625_DUMP_END phase=%ld label=%s",phase,label);
}


#define V626_RING 96

typedef struct _V626_SAMPLE {
    DWORD seq;
    DWORD r3;
    DWORD r5;
    DWORD w2,w3,w9,wA;
    DWORD h2_40,h2_A0;
    DWORD h9_40,h9_A0;
    DWORD nz2,sum2,sumsq2;
    DWORD nz9,sum9,sumsq9;
} V626_SAMPLE;

static V626_SAMPLE g_v626[V626_RING];
static volatile LONG g_v626Write=0;
static volatile LONG g_v626Seq=0;

static void V626Metrics(DWORD addr,DWORD bytes,DWORD* h,DWORD* nz,DWORD* sum,DWORD* sumsq)
{
    *h=*nz=*sum=*sumsq=0;
    if(addr<0x40000000 || addr>=0x80000000) return;
    if(!MmIsAddressValid(addr) || !MmIsAddressValid(addr+bytes-1)) return;

    DWORD hh=2166136261u;
    for(DWORD i=0;i<bytes;i++){
        BYTE v=*(volatile BYTE*)(addr+i);
        hh^=v; hh*=16777619u;
        if(v) (*nz)++;
        *sum += v;
        *sumsq += ((DWORD)v)*((DWORD)v);
    }
    *h=hh;
}

extern "C" void V626Observe(DWORD r3,DWORD r4,DWORD r5,DWORD r6,DWORD r7,DWORD r8)
{
    if(r4!=1) return;
    if(r5<0x40000000 || r5>=0x80000000 || !MmIsAddressValid(r5) || !MmIsAddressValid(r5+0x2B)) return;

    LONG wi=InterlockedIncrement(&g_v626Write)-1;
    LONG seq=InterlockedIncrement(&g_v626Seq);

    V626_SAMPLE* s=&g_v626[wi%V626_RING];
    ZeroMemory(s,sizeof(V626_SAMPLE));

    s->seq=(DWORD)seq;
    s->r3=r3;
    s->r5=r5;
    s->w2=*(volatile DWORD*)(r5+0x08);
    s->w3=*(volatile DWORD*)(r5+0x0C);
    s->w9=*(volatile DWORD*)(r5+0x24);
    s->wA=*(volatile DWORD*)(r5+0x28);

    DWORD dummy=0;
    V626Metrics(s->w2,0x40,&s->h2_40,&dummy,&dummy,&dummy);
    V626Metrics(s->w2,0xA0,&s->h2_A0,&s->nz2,&s->sum2,&s->sumsq2);
    V626Metrics(s->w9,0x40,&s->h9_40,&dummy,&dummy,&dummy);
    V626Metrics(s->w9,0xA0,&s->h9_A0,&s->nz9,&s->sum9,&s->sumsq9);
}

extern "C" __declspec(naked) DWORD Hook_V626_Submit()
{
    __asm {
        stwu r1,-0x90(r1)
        mflr r0
        stw r0,0x94(r1)
        stw r3,0x20(r1); stw r4,0x24(r1); stw r5,0x28(r1); stw r6,0x2C(r1)
        stw r7,0x30(r1); stw r8,0x34(r1)

        bl V626Observe

        lwz r3,0x20(r1); lwz r4,0x24(r1); lwz r5,0x28(r1); lwz r6,0x2C(r1)
        lwz r7,0x30(r1); lwz r8,0x34(r1)

        lis r11,0x8170
        addi r11,r11,-0x4B68
        mtctr r11
        bctrl

        lwz r0,0x94(r1)
        mtlr r0
        addi r1,r1,0x90
        blr
    }
}

static BOOL V626Install(SOCKET s)
{
    ZeroMemory(g_v626,sizeof(g_v626));
    g_v626Write=0;g_v626Seq=0;
    BOOL ok=PatchStub((volatile DWORD*)0x8A7B4424,(DWORD)&Hook_V626_Submit);
    SendLine(s,"V626_INSTALL ok=%u stub=0x8A7B4424 hook=0x%08X",ok?1:0,(DWORD)&Hook_V626_Submit);
    return ok;
}

static void V626Restore(SOCKET s)
{
    PatchStub((volatile DWORD*)0x8A7B4424,0x816FB498);
    SendLine(s,"V626_RESTORE ok=1");
}

static void V626Dump(SOCKET s,LONG phase,const char* label)
{
    LONG total=g_v626Write;
    LONG count=total<V626_RING?total:V626_RING;
    LONG start=total-count;

    SendLine(s,"V626_DUMP_BEGIN phase=%ld label=%s count=%ld total=%ld",phase,label,count,total);

    for(LONG i=0;i<count;i++){
        V626_SAMPLE* x=&g_v626[(start+i)%V626_RING];
        SendLine(s,
            "V626_SAMPLE phase=%ld seq=%u r3=0x%08X r5=0x%08X "
            "w2=0x%08X w3=0x%08X h2_40=0x%08X h2_A0=0x%08X nz2=%u sum2=%u sumsq2=%u "
            "w9=0x%08X wA=0x%08X h9_40=0x%08X h9_A0=0x%08X nz9=%u sum9=%u sumsq9=%u",
            phase,x->seq,x->r3,x->r5,
            x->w2,x->w3,x->h2_40,x->h2_A0,x->nz2,x->sum2,x->sumsq2,
            x->w9,x->wA,x->h9_40,x->h9_A0,x->nz9,x->sum9,x->sumsq9);
    }

    SendLine(s,"V626_DUMP_END phase=%ld label=%s",phase,label);
}


#define V627_FRAMES 24
#define V627_BYTES 0xA0

typedef struct _V627_FRAME {
    DWORD seq;
    DWORD r5;
    DWORD w2;
    DWORD w9;
    BYTE b2[V627_BYTES];
    BYTE b9[V627_BYTES];
} V627_FRAME;

static V627_FRAME g_v627[V627_FRAMES];
static volatile LONG g_v627Write=0;
static volatile LONG g_v627Seq=0;

static BOOL V627ValidUserRange(DWORD a,DWORD bytes)
{
    return (a>=0x40000000 && a<0x80000000 &&
            MmIsAddressValid(a) && MmIsAddressValid(a+bytes-1));
}

extern "C" void V627Observe(DWORD r3,DWORD r4,DWORD r5,DWORD r6,DWORD r7,DWORD r8)
{
    if(r4!=1 || r3!=0x81AACA28) return;
    if(!V627ValidUserRange(r5,0x2C)) return;

    DWORD w2=*(volatile DWORD*)(r5+0x08);
    DWORD w9=*(volatile DWORD*)(r5+0x24);
    if(!V627ValidUserRange(w2,V627_BYTES)) return;

    LONG wi=InterlockedIncrement(&g_v627Write)-1;
    LONG seq=InterlockedIncrement(&g_v627Seq);
    V627_FRAME* f=&g_v627[wi%V627_FRAMES];
    ZeroMemory(f,sizeof(V627_FRAME));

    f->seq=(DWORD)seq;
    f->r5=r5;
    f->w2=w2;
    f->w9=w9;

    for(DWORD i=0;i<V627_BYTES;i++)
        f->b2[i]=*(volatile BYTE*)(w2+i);

    if(V627ValidUserRange(w9,V627_BYTES)){
        for(DWORD i=0;i<V627_BYTES;i++)
            f->b9[i]=*(volatile BYTE*)(w9+i);
    }
}

extern "C" __declspec(naked) DWORD Hook_V627_Submit()
{
    __asm {
        stwu r1,-0x90(r1)
        mflr r0
        stw r0,0x94(r1)
        stw r3,0x20(r1); stw r4,0x24(r1); stw r5,0x28(r1); stw r6,0x2C(r1)
        stw r7,0x30(r1); stw r8,0x34(r1)

        bl V627Observe

        lwz r3,0x20(r1); lwz r4,0x24(r1); lwz r5,0x28(r1); lwz r6,0x2C(r1)
        lwz r7,0x30(r1); lwz r8,0x34(r1)

        lis r11,0x8170
        addi r11,r11,-0x4B68
        mtctr r11
        bctrl

        lwz r0,0x94(r1)
        mtlr r0
        addi r1,r1,0x90
        blr
    }
}

static BOOL V627Install(SOCKET s)
{
    ZeroMemory(g_v627,sizeof(g_v627));
    g_v627Write=0; g_v627Seq=0;
    BOOL ok=PatchStub((volatile DWORD*)0x8A7B4424,(DWORD)&Hook_V627_Submit);
    SendLine(s,"V627_INSTALL ok=%u stub=0x8A7B4424 hook=0x%08X",ok?1:0,(DWORD)&Hook_V627_Submit);
    return ok;
}

static void V627Restore(SOCKET s)
{
    PatchStub((volatile DWORD*)0x8A7B4424,0x816FB498);
    SendLine(s,"V627_RESTORE ok=1");
}

static void V627HexLine(SOCKET s,const char* tag,LONG phase,DWORD seq,DWORD offset,const BYTE* p,DWORD n)
{
    char hex[16*2+1];
    DWORD pos=0;
    for(DWORD i=0;i<n;i++){
        static const char* digs="0123456789ABCDEF";
        hex[pos++]=digs[(p[i]>>4)&0xF];
        hex[pos++]=digs[p[i]&0xF];
    }
    hex[pos]=0;
    SendLine(s,"V627_DATA phase=%ld seq=%u tag=%s off=0x%02X hex=%s",
        phase,seq,tag,offset,hex);
}

static void V627Dump(SOCKET s,LONG phase,const char* label)
{
    LONG total=g_v627Write;
    LONG count=total<V627_FRAMES?total:V627_FRAMES;
    LONG start=total-count;

    SendLine(s,"V627_DUMP_BEGIN phase=%ld label=%s count=%ld total=%ld",phase,label,count,total);

    for(LONG i=0;i<count;i++){
        V627_FRAME* f=&g_v627[(start+i)%V627_FRAMES];
        SendLine(s,"V627_FRAME phase=%ld seq=%u r5=0x%08X w2=0x%08X w9=0x%08X",
            phase,f->seq,f->r5,f->w2,f->w9);

        for(DWORD off=0;off<V627_BYTES;off+=16){
            V627HexLine(s,"W2",phase,f->seq,off,&f->b2[off],16);
            V627HexLine(s,"W9",phase,f->seq,off,&f->b9[off],16);
        }
    }

    SendLine(s,"V627_DUMP_END phase=%ld label=%s",phase,label);
}


#define V628_BYTES 0xA0
#define V628_RING 96

typedef struct _V628_FRAME {
    DWORD seq;
    BYTE pcm[V628_BYTES];
} V628_FRAME;

static V628_FRAME g_v628[V628_RING];
static volatile LONG g_v628Write=0;
static volatile LONG g_v628Seq=0;

static BOOL V628ValidUserRange(DWORD a,DWORD bytes)
{
    return (a>=0x40000000 && a<0x80000000 &&
            MmIsAddressValid(a) && MmIsAddressValid(a+bytes-1));
}

extern "C" void V628Observe(DWORD r3,DWORD r4,DWORD r5,DWORD r6,DWORD r7,DWORD r8)
{
    if(r4!=1 || r3!=0x81AACA28) return;
    if(!V628ValidUserRange(r5,0x0C)) return;

    DWORD w2=*(volatile DWORD*)(r5+0x08);
    if(!V628ValidUserRange(w2,V628_BYTES)) return;

    LONG wi=InterlockedIncrement(&g_v628Write)-1;
    LONG seq=InterlockedIncrement(&g_v628Seq);
    V628_FRAME* f=&g_v628[wi%V628_RING];
    f->seq=(DWORD)seq;

    for(DWORD i=0;i<V628_BYTES;i++)
        f->pcm[i]=*(volatile BYTE*)(w2+i);
}

extern "C" __declspec(naked) DWORD Hook_V628_Submit()
{
    __asm {
        stwu r1,-0x90(r1)
        mflr r0
        stw r0,0x94(r1)
        stw r3,0x20(r1); stw r4,0x24(r1); stw r5,0x28(r1); stw r6,0x2C(r1)
        stw r7,0x30(r1); stw r8,0x34(r1)

        bl V628Observe

        lwz r3,0x20(r1); lwz r4,0x24(r1); lwz r5,0x28(r1); lwz r6,0x2C(r1)
        lwz r7,0x30(r1); lwz r8,0x34(r1)

        lis r11,0x8170
        addi r11,r11,-0x4B68
        mtctr r11
        bctrl

        lwz r0,0x94(r1)
        mtlr r0
        addi r1,r1,0x90
        blr
    }
}

static BOOL V628Install(SOCKET s)
{
    ZeroMemory(g_v628,sizeof(g_v628));
    g_v628Write=0; g_v628Seq=0;
    BOOL ok=PatchStub((volatile DWORD*)0x8A7B4424,(DWORD)&Hook_V628_Submit);
    SendLine(s,"V628_INSTALL ok=%u stub=0x8A7B4424 hook=0x%08X",ok?1:0,(DWORD)&Hook_V628_Submit);
    return ok;
}

static void V628Restore(SOCKET s)
{
    PatchStub((volatile DWORD*)0x8A7B4424,0x816FB498);
    SendLine(s,"V628_RESTORE ok=1");
}

static void V628HexLine(SOCKET s,DWORD seq,DWORD offset,const BYTE* p,DWORD n)
{
    char hex[16*2+1];
    DWORD pos=0;
    static const char* digs="0123456789ABCDEF";
    for(DWORD i=0;i<n;i++){
        hex[pos++]=digs[(p[i]>>4)&0xF];
        hex[pos++]=digs[p[i]&0xF];
    }
    hex[pos]=0;
    SendLine(s,"V628_PCM seq=%u off=0x%02X hex=%s",seq,offset,hex);
}

static void V628Dump(SOCKET s)
{
    LONG total=g_v628Write;
    LONG count=total<V628_RING?total:V628_RING;
    LONG start=total-count;

    SendLine(s,"V628_DUMP_BEGIN count=%ld total=%ld",count,total);

    for(LONG i=0;i<count;i++){
        V628_FRAME* f=&g_v628[(start+i)%V628_RING];
        SendLine(s,"V628_FRAME seq=%u",f->seq);
        for(DWORD off=0;off<V628_BYTES;off+=16)
            V628HexLine(s,f->seq,off,&f->pcm[off],16);
    }

    SendLine(s,"V628_DUMP_END");
}


#define V630_BYTES 0xA0
#define V630_RING 256

typedef struct _V630_FRAME {
    DWORD seq;
    DWORD tick;
    DWORD r5;
    DWORD w2;
    DWORD w9;
    BYTE b2[V630_BYTES];
    BYTE b9[V630_BYTES];
} V630_FRAME;

static V630_FRAME g_v630[V630_RING];
static volatile LONG g_v630Write=0;
static volatile LONG g_v630Seq=0;

static BOOL V630ValidUserRange(DWORD a,DWORD bytes)
{
    return (a>=0x40000000 && a<0x80000000 &&
            MmIsAddressValid(a) && MmIsAddressValid(a+bytes-1));
}

extern "C" void V630Observe(DWORD r3,DWORD r4,DWORD r5,DWORD r6,DWORD r7,DWORD r8)
{
    if(r4!=1 || r3!=0x81AACA28) return;
    if(!V630ValidUserRange(r5,0x2C)) return;

    DWORD w2=*(volatile DWORD*)(r5+0x08);
    DWORD w9=*(volatile DWORD*)(r5+0x24);
    if(!V630ValidUserRange(w2,V630_BYTES)) return;

    LONG wi=InterlockedIncrement(&g_v630Write)-1;
    LONG seq=InterlockedIncrement(&g_v630Seq);
    V630_FRAME* f=&g_v630[wi%V630_RING];
    ZeroMemory(f,sizeof(V630_FRAME));

    f->seq=(DWORD)seq;
    f->tick=GetTickCount();
    f->r5=r5;
    f->w2=w2;
    f->w9=w9;

    for(DWORD i=0;i<V630_BYTES;i++)
        f->b2[i]=*(volatile BYTE*)(w2+i);

    if(V630ValidUserRange(w9,V630_BYTES)){
        for(DWORD i=0;i<V630_BYTES;i++)
            f->b9[i]=*(volatile BYTE*)(w9+i);
    }
}

extern "C" __declspec(naked) DWORD Hook_V630_Submit()
{
    __asm {
        stwu r1,-0x90(r1)
        mflr r0
        stw r0,0x94(r1)
        stw r3,0x20(r1); stw r4,0x24(r1); stw r5,0x28(r1); stw r6,0x2C(r1)
        stw r7,0x30(r1); stw r8,0x34(r1)

        bl V630Observe

        lwz r3,0x20(r1); lwz r4,0x24(r1); lwz r5,0x28(r1); lwz r6,0x2C(r1)
        lwz r7,0x30(r1); lwz r8,0x34(r1)

        lis r11,0x8170
        addi r11,r11,-0x4B68
        mtctr r11
        bctrl

        lwz r0,0x94(r1)
        mtlr r0
        addi r1,r1,0x90
        blr
    }
}

static BOOL V630Install(SOCKET s)
{
    ZeroMemory(g_v630,sizeof(g_v630));
    g_v630Write=0; g_v630Seq=0;
    BOOL ok=PatchStub((volatile DWORD*)0x8A7B4424,(DWORD)&Hook_V630_Submit);
    SendLine(s,"V630_INSTALL ok=%u stub=0x8A7B4424 hook=0x%08X",ok?1:0,(DWORD)&Hook_V630_Submit);
    return ok;
}

static void V630Restore(SOCKET s)
{
    PatchStub((volatile DWORD*)0x8A7B4424,0x816FB498);
    SendLine(s,"V630_RESTORE ok=1");
}

static void V630HexLine(SOCKET s,DWORD seq,const char* tag,DWORD offset,const BYTE* p,DWORD n)
{
    char hex[16*2+1];
    DWORD pos=0;
    static const char* digs="0123456789ABCDEF";
    for(DWORD i=0;i<n;i++){
        hex[pos++]=digs[(p[i]>>4)&0xF];
        hex[pos++]=digs[p[i]&0xF];
    }
    hex[pos]=0;
    SendLine(s,"V630_DATA seq=%u tag=%s off=0x%02X hex=%s",seq,tag,offset,hex);
}

static void V630Dump(SOCKET s)
{
    LONG total=g_v630Write;
    LONG count=total<V630_RING?total:V630_RING;
    LONG start=total-count;

    SendLine(s,"V630_DUMP_BEGIN count=%ld total=%ld",count,total);

    for(LONG i=0;i<count;i++){
        V630_FRAME* f=&g_v630[(start+i)%V630_RING];
        SendLine(s,
            "V630_FRAME seq=%u tick=%u r5=0x%08X w2=0x%08X w9=0x%08X",
            f->seq,f->tick,f->r5,f->w2,f->w9);

        for(DWORD off=0;off<V630_BYTES;off+=16){
            V630HexLine(s,f->seq,"W2",off,&f->b2[off],16);
            V630HexLine(s,f->seq,"W9",off,&f->b9[off],16);
        }
    }

    SendLine(s,"V630_DUMP_END");
}


static DWORD V631ResolveXamOrdinal(DWORD ord)
{
    HANDLE h=0;
    PVOID p=0;
    if(XexGetModuleHandle((PSZ)"xam.xex",&h)!=0 || !h) return 0;
    if(XexGetProcedureAddress(h,ord,&p)!=0 || !p) return 0;
    return (DWORD)p;
}

static DWORD V631BranchTarget(DWORD pc,DWORD ins)
{
    // PPC relative b/bl: opcode 18.
    if((ins>>26)!=18) return 0;
    LONG li=(LONG)(ins & 0x03FFFFFC);
    if(li & 0x02000000) li |= 0xFC000000;
    if(ins & 2) return (DWORD)li; // AA=1 absolute
    return pc + li;
}

static DWORD V631LisAddiTarget(DWORD w0,DWORD w1)
{
    if((w0&0xFFFF0000)!=0x3D600000) return 0; // lis r11,imm
    if((w1&0xFFFF0000)!=0x396B0000) return 0; // addi r11,r11,imm
    DWORD hi=w0&0xFFFF;
    SHORT lo=(SHORT)(w1&0xFFFF);
    return (hi<<16)+(LONG)lo;
}

static BOOL V631LooksThunk(DWORD a,DWORD target)
{
    if(!MmIsAddressValid(a) || !MmIsAddressValid(a+12)) return FALSE;
    DWORD w0=*(volatile DWORD*)(a+0);
    DWORD w1=*(volatile DWORD*)(a+4);
    DWORD w2=*(volatile DWORD*)(a+8);
    DWORD w3=*(volatile DWORD*)(a+12);
    DWORD rebuilt=V631LisAddiTarget(w0,w1);
    return rebuilt==target && w2==0x7D6903A6 &&
           (w3==0x4E800420 || w3==0x4E800421);
}

static void V631DumpCode(SOCKET s,const char* tag,DWORD addr,DWORD bytes)
{
    SendLine(s,"V631_CODE_BEGIN tag=%s addr=0x%08X bytes=0x%X mapped=%u",
             tag,addr,bytes,(addr&&MmIsAddressValid(addr))?1:0);
    if(addr && MmIsAddressValid(addr))
        SendMappedWords(s,tag,addr,bytes);
    SendLine(s,"V631_CODE_END tag=%s",tag);
}

static void V631ScanRange(SOCKET s,const char* tag,DWORD start,DWORD end,
                          DWORD cap,DWORD micaud)
{
    DWORD mappedPages=0,branchCap=0,branchMic=0,lisCap=0,lisMic=0;
    DWORD rawCap=0,rawMic=0,thunkCap=0,thunkMic=0,reported=0;

    SendLine(s,"V631_SCAN_BEGIN tag=%s range=0x%08X-0x%08X cap=0x%08X micaud=0x%08X",
             tag,start,end,cap,micaud);

    for(DWORD page=start; page<end; page+=0x1000) {
        if(!MmIsAddressValid(page) || !MmIsAddressValid(page+0xFFF)) continue;
        mappedPages++;

        for(DWORD a=page; a<page+0x1000-16; a+=4) {
            DWORD w0=*(volatile DWORD*)a;

            if(w0==cap) {
                rawCap++;
                if(reported<80){ SendLine(s,"V631_HIT tag=%s kind=RAW_CAP addr=0x%08X",tag,a); reported++; }
            }
            if(w0==micaud) {
                rawMic++;
                if(reported<80){ SendLine(s,"V631_HIT tag=%s kind=RAW_MICAUDIO addr=0x%08X",tag,a); reported++; }
            }

            DWORD bt=V631BranchTarget(a,w0);
            if(bt==cap) {
                branchCap++;
                if(reported<80){ SendLine(s,"V631_HIT tag=%s kind=BRANCH_CAP addr=0x%08X ins=0x%08X",tag,a,w0); reported++; }
            }
            if(bt==micaud) {
                branchMic++;
                if(reported<80){ SendLine(s,"V631_HIT tag=%s kind=BRANCH_MICAUDIO addr=0x%08X ins=0x%08X",tag,a,w0); reported++; }
            }

            DWORD w1=*(volatile DWORD*)(a+4);
            DWORD rebuilt=V631LisAddiTarget(w0,w1);
            if(rebuilt==cap) {
                lisCap++;
                BOOL thunk=V631LooksThunk(a,cap);
                if(thunk) thunkCap++;
                if(reported<80){ SendLine(s,"V631_HIT tag=%s kind=%s addr=0x%08X",tag,thunk?"THUNK_CAP":"LIS_CAP",a); reported++; }
            }
            if(rebuilt==micaud) {
                lisMic++;
                BOOL thunk=V631LooksThunk(a,micaud);
                if(thunk) thunkMic++;
                if(reported<80){ SendLine(s,"V631_HIT tag=%s kind=%s addr=0x%08X",tag,thunk?"THUNK_MICAUDIO":"LIS_MICAUDIO",a); reported++; }
            }
        }
    }

    SendLine(s,
        "V631_SCAN_SUMMARY tag=%s mappedPages=%u branchCap=%u branchMic=%u lisCap=%u lisMic=%u rawCap=%u rawMic=%u thunkCap=%u thunkMic=%u reported=%u",
        tag,mappedPages,branchCap,branchMic,lisCap,lisMic,rawCap,rawMic,thunkCap,thunkMic,reported);
}

static void V631Map(SOCKET s)
{
    DWORD cap=V631ResolveXamOrdinal(0x319);
    DWORD micaud=V631ResolveXamOrdinal(0x3DC);
    DWORD micstatus=V631ResolveXamOrdinal(0x318);

    SendLine(s,"V631_RESOLVE ordinal=0x319 name=XamVoiceSetAudioCaptureRoutine addr=0x%08X mapped=%u",
             cap,(cap&&MmIsAddressValid(cap))?1:0);
    SendLine(s,"V631_RESOLVE ordinal=0x3DC name=XamVoiceGetMicArrayAudio addr=0x%08X mapped=%u",
             micaud,(micaud&&MmIsAddressValid(micaud))?1:0);
    SendLine(s,"V631_RESOLVE ordinal=0x318 name=XamVoiceGetMicArrayStatus addr=0x%08X mapped=%u",
             micstatus,(micstatus&&MmIsAddressValid(micstatus))?1:0);

    V631DumpCode(s,"CAPTURE_319",cap,0x100);
    V631DumpCode(s,"MICAUDIO_3DC",micaud,0x100);
    V631DumpCode(s,"MICSTATUS_318",micstatus,0x80);

    // XAM runtime code window observed on this dashboard/runtime.
    V631ScanRange(s,"XAM",0x81600000,0x81800000,cap,micaud);

    // Title/runtime-host memory window used successfully by earlier probes.
    V631ScanRange(s,"TITLE",0x82000000,0x90000000,cap,micaud);

    SendLine(s,"V631_DONE");
}


static DWORD V632BranchTarget(DWORD pc,DWORD ins)
{
    if((ins>>26)!=18) return 0;
    LONG li=(LONG)(ins & 0x03FFFFFC);
    if(li & 0x02000000) li |= 0xFC000000;
    if(ins & 2) return (DWORD)li;
    return pc + li;
}

static DWORD V632FollowExportBranch(DWORD exportAddr)
{
    if(!exportAddr || !MmIsAddressValid(exportAddr)) return 0;
    DWORD ins=*(volatile DWORD*)exportAddr;
    return V632BranchTarget(exportAddr,ins);
}

static DWORD V632ResolveXamOrdinal(DWORD ord)
{
    HANDLE h=0; PVOID p=0;
    if(XexGetModuleHandle((PSZ)"xam.xex",&h)!=0 || !h) return 0;
    if(XexGetProcedureAddress(h,ord,&p)!=0 || !p) return 0;
    return (DWORD)p;
}

static void V632DumpCode(SOCKET s,const char* tag,DWORD addr,DWORD bytes)
{
    SendLine(s,"V632_CODE_BEGIN tag=%s addr=0x%08X bytes=0x%X mapped=%u",
             tag,addr,bytes,(addr&&MmIsAddressValid(addr))?1:0);
    if(addr && MmIsAddressValid(addr))
        SendMappedWords(s,tag,addr,bytes);
    SendLine(s,"V632_CODE_END tag=%s",tag);
}

static void V632ScanBranchesTo(SOCKET s,const char* tag,DWORD start,DWORD end,DWORD target)
{
    DWORD mapped=0,hits=0,reported=0;
    SendLine(s,"V632_SCAN_BEGIN tag=%s range=0x%08X-0x%08X target=0x%08X",
             tag,start,end,target);

    for(DWORD page=start; page<end; page+=0x1000){
        if(!MmIsAddressValid(page) || !MmIsAddressValid(page+0xFFF)) continue;
        mapped++;
        for(DWORD a=page; a<page+0x1000-4; a+=4){
            DWORD ins=*(volatile DWORD*)a;
            DWORD bt=V632BranchTarget(a,ins);
            if(bt==target){
                hits++;
                if(reported<128){
                    SendLine(s,"V632_CALLER tag=%s addr=0x%08X ins=0x%08X target=0x%08X",
                             tag,a,ins,target);
                    reported++;
                }
            }
        }
    }

    SendLine(s,"V632_SCAN_SUMMARY tag=%s mappedPages=%u hits=%u reported=%u",
             tag,mapped,hits,reported);
}

static void V632ScanImplInternals(SOCKET s,DWORD impl)
{
    if(!impl || !MmIsAddressValid(impl)) return;

    DWORD branches=0,reported=0;
    SendLine(s,"V632_IMPL_BRANCH_BEGIN impl=0x%08X",impl);

    for(DWORD a=impl; a<impl+0x400; a+=4){
        if(!MmIsAddressValid(a)) break;
        DWORD ins=*(volatile DWORD*)a;
        DWORD bt=V632BranchTarget(a,ins);
        if(bt){
            branches++;
            if(reported<96){
                SendLine(s,"V632_IMPL_BRANCH pc=0x%08X ins=0x%08X target=0x%08X mapped=%u",
                         a,ins,bt,MmIsAddressValid(bt)?1:0);
                reported++;
            }
        }
    }

    SendLine(s,"V632_IMPL_BRANCH_END branches=%u reported=%u",branches,reported);
}

static void V632Map(SOCKET s)
{
    DWORD cap=V632ResolveXamOrdinal(0x319);
    DWORD micaud=V632ResolveXamOrdinal(0x3DC);
    DWORD micstatus=V632ResolveXamOrdinal(0x318);

    DWORD capImpl=V632FollowExportBranch(cap);
    DWORD micImpl=V632FollowExportBranch(micaud);
    DWORD statusImpl=V632FollowExportBranch(micstatus);

    SendLine(s,"V632_RESOLVE ordinal=0x319 export=0x%08X impl=0x%08X",cap,capImpl);
    SendLine(s,"V632_RESOLVE ordinal=0x3DC export=0x%08X impl=0x%08X",micaud,micImpl);
    SendLine(s,"V632_RESOLVE ordinal=0x318 export=0x%08X impl=0x%08X",micstatus,statusImpl);

    V632DumpCode(s,"CAP_EXPORT",cap,0x80);
    V632DumpCode(s,"CAP_IMPL",capImpl,0x200);
    V632DumpCode(s,"MICAUDIO_EXPORT",micaud,0x100);
    if(micImpl && micImpl!=micaud) V632DumpCode(s,"MICAUDIO_IMPL",micImpl,0x100);

    V632ScanBranchesTo(s,"XAM_TO_CAP_IMPL",0x81600000,0x82000000,capImpl);
    V632ScanBranchesTo(s,"TITLE_TO_CAP_IMPL",0x82000000,0x90000000,capImpl);

    V632ScanImplInternals(s,capImpl);

    SendLine(s,"V632_DONE");
}


static DWORD V633BranchTarget(DWORD pc,DWORD ins)
{
    if((ins>>26)!=18) return 0;
    LONG li=(LONG)(ins & 0x03FFFFFC);
    if(li & 0x02000000) li |= 0xFC000000;
    if(ins & 2) return (DWORD)li;
    return pc + li;
}

static void V633Dump(SOCKET s,const char* tag,DWORD addr,DWORD bytes)
{
    SendLine(s,"V633_CODE_BEGIN tag=%s addr=0x%08X bytes=0x%X mapped=%u",
             tag,addr,bytes,(addr&&MmIsAddressValid(addr))?1:0);
    if(addr && MmIsAddressValid(addr))
        SendMappedWords(s,tag,addr,bytes);
    SendLine(s,"V633_CODE_END tag=%s",tag);
}

static void V633ScanBackrefs(SOCKET s,const char* tag,DWORD target,DWORD start,DWORD end)
{
    DWORD mapped=0,hits=0,reported=0;
    SendLine(s,"V633_BACKREF_BEGIN tag=%s target=0x%08X range=0x%08X-0x%08X",
             tag,target,start,end);
    for(DWORD page=start;page<end;page+=0x1000){
        if(!MmIsAddressValid(page)||!MmIsAddressValid(page+0xFFF)) continue;
        mapped++;
        for(DWORD a=page;a<page+0xFFC;a+=4){
            DWORD ins=*(volatile DWORD*)a;
            DWORD bt=V633BranchTarget(a,ins);
            if(bt==target){
                hits++;
                if(reported<64){
                    SendLine(s,"V633_BACKREF tag=%s caller=0x%08X ins=0x%08X",tag,a,ins);
                    reported++;
                }
            }
        }
    }
    SendLine(s,"V633_BACKREF_END tag=%s mappedPages=%u hits=%u reported=%u",
             tag,mapped,hits,reported);
}

static void V633Map(SOCKET s)
{
    // Most interesting helpers reached from XamVoiceSetAudioCaptureRoutine implementation.
    const DWORD targets[] = {
        0x8172D3C4,
        0x819848F0,
        0x81984538,
        0x81982E60,
        0x81982EF0,
        0x81A72194,
        0x8172D414,
        0x816B7B68,
        0x81A73654,
        0x8172D4F0,
        0x8172E278,
        0x816EE478,
        0x81984468,
        0x81983268,
        0x819832F0,
        0x81983380,
        0x816EEB78,
        0x81982D28,
        0x81721C58
    };
    const char* names[] = {
        "H_8172D3C4","H_819848F0","H_81984538","H_81982E60","H_81982EF0",
        "H_81A72194","H_8172D414","H_816B7B68","H_81A73654","H_8172D4F0",
        "H_8172E278","H_816EE478","H_81984468","H_81983268","H_819832F0",
        "H_81983380","H_816EEB78","H_81982D28","H_81721C58"
    };

    SendLine(s,"V633_MAP_BEGIN helpers=%u",(DWORD)(sizeof(targets)/sizeof(targets[0])));

    for(DWORD i=0;i<sizeof(targets)/sizeof(targets[0]);i++){
        V633Dump(s,names[i],targets[i],0x100);
    }

    // Backrefs within broad XAM/runtime area for the most repeated/high-interest helpers.
    V633ScanBackrefs(s,"BR_81984538",0x81984538,0x81600000,0x82000000);
    V633ScanBackrefs(s,"BR_81A72194",0x81A72194,0x81600000,0x82000000);
    V633ScanBackrefs(s,"BR_81984468",0x81984468,0x81600000,0x82000000);
    V633ScanBackrefs(s,"BR_81982D28",0x81982D28,0x81600000,0x82000000);
    V633ScanBackrefs(s,"BR_8172D3C4",0x8172D3C4,0x81600000,0x82000000);

    SendLine(s,"V633_MAP_DONE");
}


static void V634DumpWindow(SOCKET s,const char* tag,DWORD addr,DWORD before,DWORD after)
{
    DWORD start=addr-before;
    DWORD bytes=before+after;
    SendLine(s,"V634_WINDOW_BEGIN tag=%s site=0x%08X start=0x%08X bytes=0x%X mapped=%u",
             tag,addr,start,bytes,(MmIsAddressValid(start)&&MmIsAddressValid(start+bytes-4))?1:0);
    if(MmIsAddressValid(start)&&MmIsAddressValid(start+bytes-4))
        SendMappedWords(s,tag,start,bytes);
    SendLine(s,"V634_WINDOW_END tag=%s",tag);
}

static void V634Map(SOCKET s)
{
    SendLine(s,"V634_MAP_BEGIN");

    // Exact callsites from v6.33.
    V634DumpWindow(s,"SITE_81984538_A",0x817F8D0C,0x40,0x50);
    V634DumpWindow(s,"SITE_81984538_B",0x817F8D6C,0x40,0x50);
    V634DumpWindow(s,"SITE_81984538_C",0x817F8F54,0x40,0x50);

    V634DumpWindow(s,"SITE_81984468_A",0x817F8EDC,0x40,0x50);
    V634DumpWindow(s,"SITE_81984468_B",0x817F8F00,0x40,0x50);

    V634DumpWindow(s,"SITE_81982D28_A",0x817F8FEC,0x40,0x50);
    V634DumpWindow(s,"SITE_81982D28_B",0x817F901C,0x40,0x50);

    // Wider capture implementation slices for control flow and data setup.
    V634DumpWindow(s,"CAP_IMPL_D00",0x817F8D00,0x00,0x100);
    V634DumpWindow(s,"CAP_IMPL_EC0",0x817F8EC0,0x00,0x180);
    V634DumpWindow(s,"CAP_IMPL_FC0",0x817F8FC0,0x00,0x100);

    // Helper prologues/bodies, larger than v6.33.
    V634DumpWindow(s,"HELPER_81984538",0x81984538,0x00,0x180);
    V634DumpWindow(s,"HELPER_81984468",0x81984468,0x00,0x180);
    V634DumpWindow(s,"HELPER_81982D28",0x81982D28,0x00,0x100);

    // Global area referenced right at 0x817F8C80:
    // lis r11,0x81AD / addi r11,r11,0x1264 -> 0x81AD1264.
    V634DumpWindow(s,"CAP_GLOBAL_81AD1264",0x81AD1264,0x40,0xC0);

    SendLine(s,"V634_MAP_DONE");
}


static BOOL V635MatchLisAddi(DWORD a,DWORD target)
{
    if(!MmIsAddressValid(a) || !MmIsAddressValid(a+4)) return FALSE;
    DWORD w0=*(volatile DWORD*)a;
    DWORD w1=*(volatile DWORD*)(a+4);

    DWORD op0=w0>>26;
    DWORD op1=w1>>26;
    if(op0!=15) return FALSE; // addis/lis
    if(op1!=14) return FALSE; // addi

    DWORD rt=(w0>>21)&31;
    DWORD ra=(w1>>16)&31;
    DWORD rt1=(w1>>21)&31;
    if(rt!=ra || rt1!=rt) return FALSE;

    DWORD hi=w0&0xFFFF;
    SHORT lo=(SHORT)(w1&0xFFFF);
    DWORD rebuilt=(hi<<16)+(LONG)lo;
    return rebuilt==target;
}

static void V635DumpWindow(SOCKET s,const char* tag,DWORD site)
{
    DWORD start=site>=0x30?site-0x30:site;
    SendLine(s,"V635_WINDOW_BEGIN tag=%s site=0x%08X start=0x%08X",tag,site,start);
    if(MmIsAddressValid(start)&&MmIsAddressValid(start+0x7C))
        SendMappedWords(s,tag,start,0x80);
    SendLine(s,"V635_WINDOW_END tag=%s",tag);
}

static void V635ScanRefs(SOCKET s,DWORD target,DWORD start,DWORD end)
{
    DWORD mapped=0,hits=0,reported=0;
    SendLine(s,"V635_SCAN_BEGIN target=0x%08X range=0x%08X-0x%08X",target,start,end);

    for(DWORD page=start;page<end;page+=0x1000){
        if(!MmIsAddressValid(page)||!MmIsAddressValid(page+0xFFF)) continue;
        mapped++;

        for(DWORD a=page;a<page+0xFF8;a+=4){
            DWORD w=*(volatile DWORD*)a;

            if(w==target){
                hits++;
                if(reported<128){
                    SendLine(s,"V635_REF kind=RAW site=0x%08X target=0x%08X",a,target);
                    V635DumpWindow(s,"RAW_REF_WINDOW",a);
                    reported++;
                }
            }

            if(V635MatchLisAddi(a,target)){
                hits++;
                if(reported<128){
                    SendLine(s,"V635_REF kind=LIS_ADDI site=0x%08X target=0x%08X",a,target);
                    V635DumpWindow(s,"LIS_ADDI_WINDOW",a);
                    reported++;
                }
            }
        }
    }

    SendLine(s,"V635_SCAN_END mappedPages=%u hits=%u reported=%u",mapped,hits,reported);
}

static void V635Map(SOCKET s)
{
    DWORD slot=0x81AD1264;
    DWORD neighbor=0x81AD1270;

    DWORD slotVal=MmIsAddressValid(slot)?*(volatile DWORD*)slot:0;
    DWORD neighVal=MmIsAddressValid(neighbor)?*(volatile DWORD*)neighbor:0;

    SendLine(s,"V635_SLOT addr=0x%08X value=0x%08X mapped=%u valueMapped=%u",
             slot,slotVal,MmIsAddressValid(slot)?1:0,(slotVal&&MmIsAddressValid(slotVal))?1:0);
    SendLine(s,"V635_NEIGHBOR addr=0x%08X value=0x%08X mapped=%u valueMapped=%u",
             neighbor,neighVal,MmIsAddressValid(neighbor)?1:0,(neighVal&&MmIsAddressValid(neighVal))?1:0);

    if(slotVal && MmIsAddressValid(slotVal))
        V635DumpWindow(s,"CURRENT_CALLBACK_CODE",slotVal);

    // Scan broad XAM/runtime memory for code/data references to callback slot.
    V635ScanRefs(s,slot,0x81600000,0x82000000);

    // Also scan for the nearby 0x81AD1270 global seen in adjacent function.
    V635ScanRefs(s,neighbor,0x81600000,0x82000000);

    SendLine(s,"V635_DONE");
}


static volatile DWORD g_v636Original=0;
static volatile LONG g_v636Calls=0;
static volatile DWORD g_v636R3=0,g_v636R4=0,g_v636R5=0,g_v636R6=0,g_v636R7=0,g_v636R8=0;

extern "C" void V636Observe(DWORD r3,DWORD r4,DWORD r5,DWORD r6,DWORD r7,DWORD r8)
{
    g_v636R3=r3; g_v636R4=r4; g_v636R5=r5; g_v636R6=r6; g_v636R7=r7; g_v636R8=r8;
    InterlockedIncrement(&g_v636Calls);
}

extern "C" DWORD V636GetOriginal()
{
    return g_v636Original;
}

extern "C" __declspec(naked) DWORD Hook_V636_Capture()
{
    __asm {
        stwu r1,-0xA0(r1)
        mflr r0
        stw r0,0xA4(r1)

        stw r3,0x20(r1); stw r4,0x24(r1); stw r5,0x28(r1); stw r6,0x2C(r1)
        stw r7,0x30(r1); stw r8,0x34(r1)

        bl V636Observe

        bl V636GetOriginal
        mr r11,r3

        lwz r3,0x20(r1); lwz r4,0x24(r1); lwz r5,0x28(r1); lwz r6,0x2C(r1)
        lwz r7,0x30(r1); lwz r8,0x34(r1)

        mtctr r11
        bctrl

        lwz r0,0xA4(r1)
        mtlr r0
        addi r1,r1,0xA0
        blr
    }
}

static BOOL V636Mapped(DWORD a)
{
    return a && MmIsAddressValid(a);
}

static DWORD V636Hash(DWORD a,DWORD bytes)
{
    if(!a || !MmIsAddressValid(a) || !MmIsAddressValid(a+bytes-1)) return 0;
    DWORD h=2166136261u;
    for(DWORD i=0;i<bytes;i++){
        BYTE v=*(volatile BYTE*)(a+i);
        h^=v; h*=16777619u;
    }
    return h;
}

static BOOL V636Install(SOCKET s)
{
    volatile LONG* slot=(volatile LONG*)0x81AD1264;
    DWORD cur=*(volatile DWORD*)slot;
    g_v636Original=cur;
    g_v636Calls=0;

    if(!cur || !MmIsAddressValid(cur)){
        SendLine(s,"V636_INSTALL ok=0 reason=bad_original value=0x%08X",cur);
        return FALSE;
    }

    DWORD prev=(DWORD)InterlockedExchange(slot,(LONG)&Hook_V636_Capture);
    SendLine(s,"V636_INSTALL ok=1 slot=0x81AD1264 original=0x%08X prev=0x%08X hook=0x%08X",
             cur,prev,(DWORD)&Hook_V636_Capture);
    return TRUE;
}

static void V636Status(SOCKET s,const char* label)
{
    DWORD regs[6]={g_v636R3,g_v636R4,g_v636R5,g_v636R6,g_v636R7,g_v636R8};
    SendLine(s,"V636_STATUS label=%s calls=%ld r3=0x%08X r4=0x%08X r5=0x%08X r6=0x%08X r7=0x%08X r8=0x%08X",
        label,g_v636Calls,regs[0],regs[1],regs[2],regs[3],regs[4],regs[5]);

    for(int i=0;i<6;i++){
        DWORD a=regs[i];
        SendLine(s,"V636_ARG label=%s reg=r%d value=0x%08X mapped=%u hash40=0x%08X hash100=0x%08X",
            label,i+3,a,V636Mapped(a)?1:0,V636Hash(a,0x40),V636Hash(a,0x100));
    }
}

static void V636Restore(SOCKET s)
{
    volatile LONG* slot=(volatile LONG*)0x81AD1264;
    DWORD cur=*(volatile DWORD*)slot;
    if(cur==(DWORD)&Hook_V636_Capture && g_v636Original){
        InterlockedExchange(slot,(LONG)g_v636Original);
        SendLine(s,"V636_RESTORE ok=1 restored=0x%08X",g_v636Original);
    } else {
        SendLine(s,"V636_RESTORE ok=0 current=0x%08X original=0x%08X",cur,g_v636Original);
    }
}


static DWORD V637BranchTarget(DWORD pc,DWORD ins)
{
    if((ins>>26)!=18) return 0;
    LONG li=(LONG)(ins & 0x03FFFFFC);
    if(li & 0x02000000) li |= 0xFC000000;
    if(ins & 2) return (DWORD)li;
    return pc + li;
}

static void V637DumpWindow(SOCKET s,const char* tag,DWORD site)
{
    DWORD start=site>=0x30?site-0x30:site;
    SendLine(s,"V637_WINDOW_BEGIN tag=%s site=0x%08X start=0x%08X",tag,site,start);
    if(MmIsAddressValid(start)&&MmIsAddressValid(start+0x7C))
        SendMappedWords(s,tag,start,0x80);
    SendLine(s,"V637_WINDOW_END tag=%s",tag);
}

static void V637ScanCode(SOCKET s,DWORD target,DWORD start,DWORD end,const char* tag)
{
    DWORD mapped=0,branchHits=0,rawHits=0,reported=0;
    SendLine(s,"V637_CODE_SCAN_BEGIN tag=%s target=0x%08X range=0x%08X-0x%08X",
             tag,target,start,end);

    for(DWORD page=start; page<end; page+=0x1000){
        if(!MmIsAddressValid(page)||!MmIsAddressValid(page+0xFFF)) continue;
        mapped++;
        for(DWORD a=page;a<page+0xFFC;a+=4){
            DWORD w=*(volatile DWORD*)a;
            if(w==target){
                rawHits++;
                if(reported<96){
                    SendLine(s,"V637_HIT tag=%s kind=RAW_PTR addr=0x%08X",tag,a);
                    V637DumpWindow(s,"RAW_PTR_WINDOW",a);
                    reported++;
                }
            }
            DWORD bt=V637BranchTarget(a,w);
            if(bt==target){
                branchHits++;
                if(reported<96){
                    SendLine(s,"V637_HIT tag=%s kind=DIRECT_BRANCH addr=0x%08X ins=0x%08X",tag,a,w);
                    V637DumpWindow(s,"DIRECT_BRANCH_WINDOW",a);
                    reported++;
                }
            }
        }
    }

    SendLine(s,"V637_CODE_SCAN_END tag=%s mappedPages=%u branchHits=%u rawHits=%u reported=%u",
             tag,mapped,branchHits,rawHits,reported);
}

static void V637ScanDataCopies(SOCKET s,DWORD target,DWORD start,DWORD end,const char* tag)
{
    DWORD mapped=0,hits=0,reported=0;
    SendLine(s,"V637_DATA_SCAN_BEGIN tag=%s target=0x%08X range=0x%08X-0x%08X",
             tag,target,start,end);

    for(DWORD page=start; page<end; page+=0x1000){
        if(!MmIsAddressValid(page)||!MmIsAddressValid(page+0xFFF)) continue;
        mapped++;
        for(DWORD a=page;a<page+0x1000;a+=4){
            DWORD w=*(volatile DWORD*)a;
            if(w==target){
                hits++;
                if(reported<128){
                    SendLine(s,"V637_COPY tag=%s addr=0x%08X value=0x%08X",tag,a,w);
                    reported++;
                }
            }
        }
    }

    SendLine(s,"V637_DATA_SCAN_END tag=%s mappedPages=%u hits=%u reported=%u",
             tag,mapped,hits,reported);
}

static void V637Map(SOCKET s)
{
    DWORD callback=0x817F91C8;
    DWORD slot=0x81AD1264;
    DWORD slotVal=MmIsAddressValid(slot)?*(volatile DWORD*)slot:0;

    SendLine(s,"V637_STATE slot=0x%08X slotValue=0x%08X callback=0x%08X",
             slot,slotVal,callback);

    V637DumpWindow(s,"CALLBACK_ENTRY",callback);

    // Search executable XAM/runtime and title for direct calls or literal pointers.
    V637ScanCode(s,callback,0x81600000,0x82000000,"XAM");
    V637ScanCode(s,callback,0x82000000,0x90000000,"TITLE");

    // Search likely data/object regions for cached copies of the callback pointer.
    V637ScanDataCopies(s,callback,0x81A00000,0x81B00000,"XAM_DATA");
    V637ScanDataCopies(s,callback,0x40000000,0x41000000,"USER_HEAP_A");
    V637ScanDataCopies(s,callback,0x8A000000,0x8B000000,"TITLE_HIGH");

    SendLine(s,"V637_DONE");
}


static void V638DumpWindow(SOCKET s,const char* tag,DWORD site)
{
    DWORD start=site>=0x30?site-0x30:site;
    SendLine(s,"V638_WINDOW_BEGIN tag=%s site=0x%08X start=0x%08X",tag,site,start);
    if(MmIsAddressValid(start)&&MmIsAddressValid(start+0xBC))
        SendMappedWords(s,tag,start,0xC0);
    SendLine(s,"V638_WINDOW_END tag=%s",tag);
}

static void V638ScanSlotReaders(SOCKET s,DWORD start,DWORD end)
{
    DWORD mapped=0,hits=0,reported=0;
    SendLine(s,"V638_SCAN_BEGIN range=0x%08X-0x%08X slot=0x81AD1264",start,end);

    for(DWORD page=start;page<end;page+=0x1000){
        if(!MmIsAddressValid(page)||!MmIsAddressValid(page+0xFFF)) continue;
        mapped++;

        for(DWORD a=page;a<page+0xFC0;a+=4){
            DWORD w0=*(volatile DWORD*)a;

            // addis/lis rD,r0,0x81AD
            if((w0>>26)!=15) continue;
            DWORD ra0=(w0>>16)&31;
            DWORD rd0=(w0>>21)&31;
            WORD imm=(WORD)(w0&0xFFFF);
            if(ra0!=0 || imm!=0x81AD) continue;

            // Look ahead up to 12 instructions for lwz/ld using that base and offset 0x1264.
            for(DWORD k=1;k<=12;k++){
                DWORD pc=a+k*4;
                DWORD w=*(volatile DWORD*)pc;
                DWORD op=w>>26;
                DWORD ra=(w>>16)&31;
                WORD off=(WORD)(w&0xFFFF);

                // lwz opcode 32, ld opcode 58 (DS-form; low 2 bits flags)
                BOOL match=FALSE;
                if(op==32 && ra==rd0 && off==0x1264) match=TRUE;
                if(op==58 && ra==rd0 && (off&0xFFFC)==0x1264) match=TRUE;

                if(match){
                    hits++;
                    if(reported<96){
                        SendLine(s,"V638_READER lis=0x%08X load=0x%08X lisIns=0x%08X loadIns=0x%08X baseReg=r%u",
                                 a,pc,w0,w,rd0);
                        V638DumpWindow(s,"SLOT_READER_WINDOW",pc);
                        reported++;
                    }
                }
            }
        }
    }

    SendLine(s,"V638_SCAN_END mappedPages=%u hits=%u reported=%u",mapped,hits,reported);
}

static void V638DumpCallback(SOCKET s)
{
    DWORD cb=0x817F91C8;
    SendLine(s,"V638_CALLBACK addr=0x%08X mapped=%u",cb,MmIsAddressValid(cb)?1:0);
    if(MmIsAddressValid(cb))
        SendMappedWords(s,"CALLBACK_FULL",cb,0x400);
}

static void V638Map(SOCKET s)
{
    DWORD slot=0x81AD1264;
    DWORD val=MmIsAddressValid(slot)?*(volatile DWORD*)slot:0;
    SendLine(s,"V638_STATE slot=0x%08X value=0x%08X",slot,val);

    V638DumpCallback(s);
    V638ScanSlotReaders(s,0x81600000,0x82000000);

    SendLine(s,"V638_DONE");
}


static DWORD V639BranchTarget(DWORD pc,DWORD ins)
{
    if((ins>>26)!=18) return 0;
    LONG li=(LONG)(ins & 0x03FFFFFC);
    if(li & 0x02000000) li |= 0xFC000000;
    if(ins & 2) return (DWORD)li;
    return pc + li;
}

static void V639Dump(SOCKET s,const char* tag,DWORD addr,DWORD bytes)
{
    SendLine(s,"V639_CODE_BEGIN tag=%s addr=0x%08X bytes=0x%X mapped=%u",
             tag,addr,bytes,(addr&&MmIsAddressValid(addr)&&MmIsAddressValid(addr+bytes-4))?1:0);
    if(addr&&MmIsAddressValid(addr)&&MmIsAddressValid(addr+bytes-4))
        SendMappedWords(s,tag,addr,bytes);
    SendLine(s,"V639_CODE_END tag=%s",tag);
}

static void V639Backrefs(SOCKET s,const char* tag,DWORD target,DWORD start,DWORD end)
{
    DWORD mapped=0,hits=0,reported=0;
    SendLine(s,"V639_BACKREF_BEGIN tag=%s target=0x%08X range=0x%08X-0x%08X",
             tag,target,start,end);

    for(DWORD page=start;page<end;page+=0x1000){
        if(!MmIsAddressValid(page)||!MmIsAddressValid(page+0xFFF)) continue;
        mapped++;
        for(DWORD a=page;a<page+0xFFC;a+=4){
            DWORD ins=*(volatile DWORD*)a;
            DWORD bt=V639BranchTarget(a,ins);
            if(bt==target){
                hits++;
                if(reported<96){
                    SendLine(s,"V639_BACKREF tag=%s caller=0x%08X ins=0x%08X",tag,a,ins);
                    DWORD ws=a>=0x30?a-0x30:a;
                    if(MmIsAddressValid(ws)&&MmIsAddressValid(ws+0x7C))
                        SendMappedWords(s,"CALLER_WINDOW",ws,0x80);
                    reported++;
                }
            }
        }
    }

    SendLine(s,"V639_BACKREF_END tag=%s mappedPages=%u hits=%u reported=%u",
             tag,mapped,hits,reported);
}

static void V639Map(SOCKET s)
{
    const DWORD dispatcher=0x817F9220;
    const DWORD reader=0x817F9248;
    const DWORD callback=0x817F91C8;
    const DWORD slot=0x81AD1264;

    SendLine(s,"V639_STATE dispatcher=0x%08X reader=0x%08X callback=0x%08X slotValue=0x%08X",
             dispatcher,reader,callback,*(volatile DWORD*)slot);

    V639Dump(s,"DISPATCHER_FULL",dispatcher,0x240);
    V639Dump(s,"CALLBACK_FULL",callback,0x180);

    V639Backrefs(s,"TO_DISPATCHER",dispatcher,0x81600000,0x82000000);
    V639Backrefs(s,"TO_CALLBACK",callback,0x81600000,0x82000000);

    SendLine(s,"V639_DONE");
}


static DWORD V640BranchTarget(DWORD pc,DWORD ins)
{
    if((ins>>26)!=18) return 0;
    LONG li=(LONG)(ins & 0x03FFFFFC);
    if(li & 0x02000000) li |= 0xFC000000;
    if(ins & 2) return (DWORD)li;
    return pc + li;
}

static void V640Dump(SOCKET s,const char* tag,DWORD addr,DWORD bytes)
{
    SendLine(s,"V640_CODE_BEGIN tag=%s addr=0x%08X bytes=0x%X mapped=%u",
             tag,addr,bytes,(addr&&MmIsAddressValid(addr)&&MmIsAddressValid(addr+bytes-4))?1:0);
    if(addr&&MmIsAddressValid(addr)&&MmIsAddressValid(addr+bytes-4))
        SendMappedWords(s,tag,addr,bytes);
    SendLine(s,"V640_CODE_END tag=%s",tag);
}

static void V640BranchesInFunction(SOCKET s,const char* tag,DWORD start,DWORD bytes)
{
    DWORD reported=0;
    SendLine(s,"V640_BRANCH_BEGIN tag=%s start=0x%08X bytes=0x%X",tag,start,bytes);

    for(DWORD a=start;a<start+bytes;a+=4){
        if(!MmIsAddressValid(a)) break;
        DWORD ins=*(volatile DWORD*)a;
        DWORD bt=V640BranchTarget(a,ins);
        if(bt){
            SendLine(s,"V640_BRANCH tag=%s pc=0x%08X ins=0x%08X target=0x%08X mapped=%u",
                     tag,a,ins,bt,MmIsAddressValid(bt)?1:0);
            reported++;
        }
    }
    SendLine(s,"V640_BRANCH_END tag=%s count=%u",tag,reported);
}

static void V640FindCallsTo(SOCKET s,const char* tag,DWORD target,DWORD start,DWORD end)
{
    DWORD mapped=0,hits=0,reported=0;
    SendLine(s,"V640_CALLSCAN_BEGIN tag=%s target=0x%08X range=0x%08X-0x%08X",
             tag,target,start,end);
    for(DWORD page=start;page<end;page+=0x1000){
        if(!MmIsAddressValid(page)||!MmIsAddressValid(page+0xFFF)) continue;
        mapped++;
        for(DWORD a=page;a<page+0xFFC;a+=4){
            DWORD ins=*(volatile DWORD*)a;
            DWORD bt=V640BranchTarget(a,ins);
            if(bt==target){
                hits++;
                if(reported<64){
                    SendLine(s,"V640_CALL tag=%s caller=0x%08X ins=0x%08X",tag,a,ins);
                    DWORD ws=a>=0x30?a-0x30:a;
                    if(MmIsAddressValid(ws)&&MmIsAddressValid(ws+0x7C))
                        SendMappedWords(s,"CALL_WINDOW",ws,0x80);
                    reported++;
                }
            }
        }
    }
    SendLine(s,"V640_CALLSCAN_END tag=%s mappedPages=%u hits=%u reported=%u",
             tag,mapped,hits,reported);
}

static void V640Map(SOCKET s)
{
    const DWORD xamSubmit=0x816FB498;
    const DWORD xamCreate=0x816FC098;
    const DWORD xvoicedSubmit=0x80102048;
    const DWORD xvoicedActivate=0x80101DC8;

    SendLine(s,"V640_STATE xamSubmit=0x%08X xamCreate=0x%08X xvoicedSubmit=0x%08X xvoicedActivate=0x%08X",
             xamSubmit,xamCreate,xvoicedSubmit,xvoicedActivate);

    V640Dump(s,"XAM_SUBMIT_FULL",xamSubmit,0x500);
    V640BranchesInFunction(s,"XAM_SUBMIT",xamSubmit,0x500);

    V640Dump(s,"XAM_CREATE_FULL",xamCreate,0x300);
    V640BranchesInFunction(s,"XAM_CREATE",xamCreate,0x300);

    V640Dump(s,"XVOICED_SUBMIT_FULL",xvoicedSubmit,0x180);
    V640Dump(s,"XVOICED_ACTIVATE_FULL",xvoicedActivate,0x180);

    V640FindCallsTo(s,"XAM_TO_XVOICED_SUBMIT",xvoicedSubmit,0x81600000,0x82000000);
    V640FindCallsTo(s,"XAM_TO_XVOICED_ACTIVATE",xvoicedActivate,0x81600000,0x82000000);

    SendLine(s,"V640_DONE");
}


static DWORD V641BranchTarget(DWORD pc,DWORD ins)
{
    if((ins>>26)!=18) return 0;
    LONG li=(LONG)(ins & 0x03FFFFFC);
    if(li & 0x02000000) li |= 0xFC000000;
    if(ins & 2) return (DWORD)li;
    return pc + li;
}

static void V641Dump(SOCKET s,const char* tag,DWORD addr,DWORD bytes)
{
    SendLine(s,"V641_CODE_BEGIN tag=%s addr=0x%08X bytes=0x%X mapped=%u",
             tag,addr,bytes,(addr&&MmIsAddressValid(addr)&&MmIsAddressValid(addr+bytes-4))?1:0);
    if(addr&&MmIsAddressValid(addr)&&MmIsAddressValid(addr+bytes-4))
        SendMappedWords(s,tag,addr,bytes);
    SendLine(s,"V641_CODE_END tag=%s",tag);
}

static void V641ScanIndirects(SOCKET s,const char* tag,DWORD start,DWORD bytes)
{
    DWORD mtctrCount=0,bctrlCount=0,reported=0;
    SendLine(s,"V641_INDIRECT_BEGIN tag=%s start=0x%08X bytes=0x%X",tag,start,bytes);

    for(DWORD a=start;a<start+bytes;a+=4){
        if(!MmIsAddressValid(a)) break;
        DWORD ins=*(volatile DWORD*)a;

        // mtctr rS => mtspr 9,rS. Mask fields not relevant to source reg.
        if((ins & 0xFC1FFFFF)==0x7C0903A6){
            mtctrCount++;
            DWORD rs=(ins>>21)&31;
            if(reported<128){
                SendLine(s,"V641_INDIRECT tag=%s kind=MTCTR pc=0x%08X ins=0x%08X src=r%u",
                         tag,a,ins,rs);
                DWORD ws=a>=0x30?a-0x30:a;
                if(MmIsAddressValid(ws)&&MmIsAddressValid(ws+0x7C))
                    SendMappedWords(s,"INDIRECT_WINDOW",ws,0x80);
                reported++;
            }
        }

        if(ins==0x4E800421 || ins==0x4E800420){
            bctrlCount++;
            if(reported<128){
                SendLine(s,"V641_INDIRECT tag=%s kind=%s pc=0x%08X",
                         tag,ins==0x4E800421?"BCTRL":"BCTR",a);
                DWORD ws=a>=0x30?a-0x30:a;
                if(MmIsAddressValid(ws)&&MmIsAddressValid(ws+0x7C))
                    SendMappedWords(s,"INDIRECT_WINDOW",ws,0x80);
                reported++;
            }
        }
    }

    SendLine(s,"V641_INDIRECT_END tag=%s mtctr=%u bctr_or_bctrl=%u reported=%u",
             tag,mtctrCount,bctrlCount,reported);
}

static void V641Branches(SOCKET s,const char* tag,DWORD start,DWORD bytes)
{
    DWORD count=0;
    SendLine(s,"V641_BRANCH_BEGIN tag=%s start=0x%08X bytes=0x%X",tag,start,bytes);
    for(DWORD a=start;a<start+bytes;a+=4){
        if(!MmIsAddressValid(a)) break;
        DWORD ins=*(volatile DWORD*)a;
        DWORD bt=V641BranchTarget(a,ins);
        if(bt){
            SendLine(s,"V641_BRANCH tag=%s pc=0x%08X ins=0x%08X target=0x%08X mapped=%u",
                     tag,a,ins,bt,MmIsAddressValid(bt)?1:0);
            count++;
        }
    }
    SendLine(s,"V641_BRANCH_END tag=%s count=%u",tag,count);
}

static void V641Map(SOCKET s)
{
    const DWORD submit=0x816FB498;
    const DWORD h1=0x81A721C4;
    const DWORD h2=0x81A731E4;
    const DWORD h3=0x81A731F4;
    const DWORD h4=0x81A72EE4;
    const DWORD low1=0x816D8A58;
    const DWORD low2=0x816D8CD8;
    const DWORD low3=0x816D8F50;

    SendLine(s,"V641_STATE submit=0x%08X h1=0x%08X h2=0x%08X h3=0x%08X h4=0x%08X",
             submit,h1,h2,h3,h4);

    V641Dump(s,"SUBMIT_ENTRY_CONTEXT",submit,0x180);

    V641Dump(s,"H_81A721C4",h1,0x240);
    V641Branches(s,"H_81A721C4",h1,0x240);
    V641ScanIndirects(s,"H_81A721C4",h1,0x240);

    V641Dump(s,"H_81A731E4",h2,0x300);
    V641Branches(s,"H_81A731E4",h2,0x300);
    V641ScanIndirects(s,"H_81A731E4",h2,0x300);

    V641Dump(s,"H_81A731F4",h3,0x180);
    V641Branches(s,"H_81A731F4",h3,0x180);
    V641ScanIndirects(s,"H_81A731F4",h3,0x180);

    V641Dump(s,"H_81A72EE4",h4,0x280);
    V641Branches(s,"H_81A72EE4",h4,0x280);
    V641ScanIndirects(s,"H_81A72EE4",h4,0x280);

    V641Dump(s,"LOW_816D8A58",low1,0x240);
    V641ScanIndirects(s,"LOW_816D8A58",low1,0x240);

    V641Dump(s,"LOW_816D8CD8",low2,0x240);
    V641ScanIndirects(s,"LOW_816D8CD8",low2,0x240);

    V641Dump(s,"LOW_816D8F50",low3,0x240);
    V641ScanIndirects(s,"LOW_816D8F50",low3,0x240);

    SendLine(s,"V641_DONE");
}


static void V642Dump(SOCKET s,const char* tag,DWORD addr,DWORD bytes)
{
    SendLine(s,"V642_CODE_BEGIN tag=%s addr=0x%08X bytes=0x%X mapped=%u",
             tag,addr,bytes,(addr&&MmIsAddressValid(addr)&&MmIsAddressValid(addr+bytes-4))?1:0);
    if(addr&&MmIsAddressValid(addr)&&MmIsAddressValid(addr+bytes-4))
        SendMappedWords(s,tag,addr,bytes);
    SendLine(s,"V642_CODE_END tag=%s",tag);
}

static void V642Map(SOCKET s)
{
    const DWORD fn=0x816D8F50;
    const DWORD dispatchPc=0x816D8FEC;
    const DWORD globalAddr=0x81A83014;
    const DWORD xvoicedSubmit=0x80102048;
    const DWORD xvoicedActivate=0x80101DC8;
    const DWORD xvoicedClose=0x80102230;
    const DWORD xvoicedHeadset=0x80101C40;

    DWORD tableBase = MmIsAddressValid(globalAddr) ? *(volatile DWORD*)globalAddr : 0;

    SendLine(s,
        "V642_STATE fn=0x%08X dispatchPc=0x%08X global=0x%08X tableBase=0x%08X mapped=%u",
        fn,dispatchPc,globalAddr,tableBase,(tableBase&&MmIsAddressValid(tableBase))?1:0);

    V642Dump(s,"LOW_816D8F50_FULL",fn,0x300);

    if(tableBase && MmIsAddressValid(tableBase)){
        for(DWORD i=0;i<64;i++){
            DWORD a=tableBase+i*4;
            if(!MmIsAddressValid(a)) break;
            DWORD v=*(volatile DWORD*)a;
            if(v || i<16){
                SendLine(s,
                    "V642_ENTRY index=%u addr=0x%08X value=0x%08X mapped=%u matchSubmit=%u matchActivate=%u matchClose=%u matchHeadset=%u",
                    i,a,v,(v&&MmIsAddressValid(v))?1:0,
                    v==xvoicedSubmit?1:0,
                    v==xvoicedActivate?1:0,
                    v==xvoicedClose?1:0,
                    v==xvoicedHeadset?1:0);
                if(v && MmIsAddressValid(v) && i<24)
                    V642Dump(s,"ENTRY_TARGET",v,0x40);
            }
        }
    }

    // Also inspect the global neighborhood in case 0x3014 sits inside a larger driver interface block.
    V642Dump(s,"GLOBAL_81A83014_NEIGHBORHOOD",0x81A82FD0,0x100);

    SendLine(s,"V642_DONE");
}


static void V643Dump(SOCKET s,const char* tag,DWORD addr,DWORD bytes)
{
    SendLine(s,"V643_DUMP_BEGIN tag=%s addr=0x%08X bytes=0x%X mapped=%u",
             tag,addr,bytes,(addr&&MmIsAddressValid(addr)&&MmIsAddressValid(addr+bytes-1))?1:0);
    if(addr&&MmIsAddressValid(addr)&&MmIsAddressValid(addr+bytes-1))
        SendMappedWords(s,tag,addr,bytes);
    SendLine(s,"V643_DUMP_END tag=%s",tag);
}

static DWORD V643DispatchTarget(DWORD r4)
{
    DWORD tablePtrAddr=0x81A83014;
    if(!MmIsAddressValid(tablePtrAddr)) return 0;
    DWORD table=*(volatile DWORD*)tablePtrAddr;
    DWORD index=r4 & 0xFF;
    DWORD entry=table + index*4;
    if(!table || !MmIsAddressValid(entry)) return 0;
    return *(volatile DWORD*)entry;
}

static void V643AdaptiveReport(SOCKET s,LONG phase,const char* label)
{
    V617Report(s,phase,label);

    DWORD bestSlot=0xFFFFFFFF,bestScore=0;
    SendLine(s,"V643_ADAPTIVE_BEGIN phase=%ld label=%s",phase,label);

    for(int i=0;i<V617_MAX_CLASSES;i++){
        V617_CLASS* c=&g_v617[i];
        if(!c->calls) continue;

        DWORD target=V643DispatchTarget(c->keyR4);
        DWORD index=c->keyR4 & 0xFF;
        DWORD mappedTarget=(target&&MmIsAddressValid(target))?1:0;
        DWORD score=c->lastAbs + c->lastNz*32;

        SendLine(s,
          "V643_CLASS phase=%ld slot=%d r4=0x%08X dispatchIndex=%u target=0x%08X targetMapped=%u calls=%u r5=0x%08X child=0x%08X childLen=0x%X hash100=0x%08X abs=%u nz=%u score=%u",
          phase,i,c->keyR4,index,target,mappedTarget,c->calls,c->lastR5,c->lastChild,
          c->lastChildLen,c->lastHash100,c->lastAbs,c->lastNz,score);

        if(target && MmIsAddressValid(target))
            V643Dump(s,"DISPATCH_TARGET",target,0x100);

        if(c->lastR5 && MmIsAddressValid(c->lastR5))
            V643Dump(s,"R5_DESCRIPTOR",c->lastR5,0x80);

        if(c->lastChild && c->lastChildLen && MmIsAddressValid(c->lastChild)){
            DWORD n=c->lastChildLen;
            if(n>0x200) n=0x200;
            V643Dump(s,"CHILD_BUFFER",c->lastChild,n);
        }

        if(score>bestScore){bestScore=score;bestSlot=i;}
    }

    SendLine(s,"V643_BEST phase=%ld slot=%u score=%u",
             phase,bestSlot,bestScore);
    SendLine(s,"V643_ADAPTIVE_END phase=%ld",phase);
}


#define V645_MAX_SNAPS 64
#define V645_CHILD_MAX 0xA0

typedef struct {
    DWORD seq;
    DWORD r3,r4,r5,r6,r7,r8;
    DWORD len;
    DWORD child;
    DWORD childLen;
    DWORD hashDesc;
    DWORD hashChild;
    DWORD nzChild;
    DWORD absChild;
    BYTE desc[0x40];
    BYTE childBytes[V645_CHILD_MAX];
} V645_SNAP;

static volatile LONG g_v645Count=0;
static V645_SNAP g_v645Snaps[V645_MAX_SNAPS];

static DWORD V645HashBytes(const BYTE* p,DWORD n)
{
    DWORD h=2166136261u;
    for(DWORD i=0;i<n;i++){ h^=p[i]; h*=16777619u; }
    return h;
}

static DWORD V645NzBytes(const BYTE* p,DWORD n)
{
    DWORD c=0;
    for(DWORD i=0;i<n;i++) if(p[i]) c++;
    return c;
}

static DWORD V645Abs16Bytes(const BYTE* p,DWORD n)
{
    DWORD sum=0;
    for(DWORD i=0;i+1<n;i+=2){
        SHORT v=*(SHORT*)(p+i);
        sum += (v<0)?(DWORD)(-v):(DWORD)v;
    }
    return sum;
}

extern "C" void V645Capture(DWORD r3,DWORD r4,DWORD r5,DWORD r6,DWORD r7,DWORD r8)
{
    if(r4!=1) return;

    LONG ix=g_v645Count;
    if(ix<0 || ix>=V645_MAX_SNAPS) return;
    ix=InterlockedIncrement(&g_v645Count)-1;
    if(ix<0 || ix>=V645_MAX_SNAPS) return;

    V645_SNAP* s=&g_v645Snaps[ix];
    ZeroMemory(s,sizeof(*s));
    s->seq=(DWORD)ix;
    s->r3=r3;s->r4=r4;s->r5=r5;s->r6=r6;s->r7=r7;s->r8=r8;

    if(r5 && MmIsAddressValid(r5) && MmIsAddressValid(r5+0x3F)){
        memcpy(s->desc,(void*)r5,0x40);
        s->hashDesc=V645HashBytes(s->desc,0x40);

        s->len=*(volatile DWORD*)(r5+0x04);
        s->child=*(volatile DWORD*)(r5+0x08);
        s->childLen=*(volatile DWORD*)(r5+0x0C);

        DWORD n=s->childLen;
        if(n>V645_CHILD_MAX) n=V645_CHILD_MAX;
        if(s->child && n && MmIsAddressValid(s->child) && MmIsAddressValid(s->child+n-1)){
            memcpy(s->childBytes,(void*)s->child,n);
            s->hashChild=V645HashBytes(s->childBytes,n);
            s->nzChild=V645NzBytes(s->childBytes,n);
            s->absChild=V645Abs16Bytes(s->childBytes,n);
        }
    }
}

extern "C" __declspec(naked) DWORD Hook_V645_Submit()
{
    __asm {
        stwu r1,-0xA0(r1)
        mflr r0
        stw r0,0xA4(r1)

        stw r3,0x20(r1); stw r4,0x24(r1); stw r5,0x28(r1); stw r6,0x2C(r1)
        stw r7,0x30(r1); stw r8,0x34(r1)

        bl V645Capture

        lwz r3,0x20(r1); lwz r4,0x24(r1); lwz r5,0x28(r1); lwz r6,0x2C(r1)
        lwz r7,0x30(r1); lwz r8,0x34(r1)

        lis r11,0x8170
        addi r11,r11,-0x4B68
        mtctr r11
        bctrl

        lwz r0,0xA4(r1)
        mtlr r0
        addi r1,r1,0xA0
        blr
    }
}


static void V645FlushCode(DWORD addr,DWORD bytes)
{
    typedef VOID (__cdecl *PFN_SWEEP)(PVOID,DWORD);
    static PFN_SWEEP d=0;
    static PFN_SWEEP i=0;
    if(!d || !i){
        HANDLE h=0;
        if(XexGetModuleHandle((PSZ)"xboxkrnl.exe",&h)==0 && h){
            // Known runtime addresses/ordinals are already resolved elsewhere in these builds;
            // fall back to the observed cache-sweep addresses if procedure lookup is unavailable.
            d=(PFN_SWEEP)0x800738C8;
            i=(PFN_SWEEP)0x8007388C;
        }
    }
    if(d) d((PVOID)addr,bytes);
    if(i) i((PVOID)addr,bytes);
}

static BOOL V645Install(SOCKET s)
{
    DWORD stub=0x8A7B4424;
    if(!MmIsAddressValid(stub) || !MmIsAddressValid(stub+0x0F)){
        SendLine(s,"V645_INSTALL ok=0 reason=stub_unmapped");
        return FALSE;
    }

    g_v645Count=0;
    ZeroMemory(g_v645Snaps,sizeof(g_v645Snaps));

    DWORD hook=(DWORD)&Hook_V645_Submit;
    DWORD hi=(hook+0x8000)>>16;
    SHORT lo=(SHORT)(hook&0xFFFF);

    DWORD patch[4];
    patch[0]=0x3D600000 | (hi&0xFFFF);
    patch[1]=0x396B0000 | ((WORD)lo);
    patch[2]=0x7D6903A6;
    patch[3]=0x4E800420;

    memcpy((void*)stub,patch,16);
    V645FlushCode(stub,16);

    SendLine(s,"V645_INSTALL ok=1 stub=0x%08X hook=0x%08X",stub,hook);
    return TRUE;
}

static void V645Reset(SOCKET s,const char* label)
{
    g_v645Count=0;
    ZeroMemory(g_v645Snaps,sizeof(g_v645Snaps));
    SendLine(s,"V645_RESET label=%s",label);
}

static void V645DumpSnaps(SOCKET s,const char* label)
{
    LONG n=g_v645Count;
    if(n>V645_MAX_SNAPS) n=V645_MAX_SNAPS;

    SendLine(s,"V645_PHASE label=%s count=%ld",label,n);

    for(LONG i=0;i<n;i++){
        V645_SNAP* p=&g_v645Snaps[i];
        SendLine(s,
          "V645_SNAP label=%s seq=%u r3=0x%08X r4=0x%08X r5=0x%08X len=0x%X child=0x%08X childLen=0x%X hashDesc=0x%08X hashChild=0x%08X nz=%u abs=%u",
          label,p->seq,p->r3,p->r4,p->r5,p->len,p->child,p->childLen,
          p->hashDesc,p->hashChild,p->nzChild,p->absChild);

        if(i<8){
            SendLine(s,
              "V645_CHILD_WORDS label=%s seq=%u w0=%08X w1=%08X w2=%08X w3=%08X w4=%08X w5=%08X w6=%08X w7=%08X",
              label,p->seq,
              *(DWORD*)&p->childBytes[0x00],*(DWORD*)&p->childBytes[0x04],
              *(DWORD*)&p->childBytes[0x08],*(DWORD*)&p->childBytes[0x0C],
              *(DWORD*)&p->childBytes[0x10],*(DWORD*)&p->childBytes[0x14],
              *(DWORD*)&p->childBytes[0x18],*(DWORD*)&p->childBytes[0x1C]);
        }
    }

    SendLine(s,"V645_PHASE_END label=%s",label);
}

static void V645Restore(SOCKET s)
{
    DWORD stub=0x8A7B4424;
    DWORD orig[4]={0x3D608170,0x396BB498,0x7D6903A6,0x4E800420};
    if(MmIsAddressValid(stub)&&MmIsAddressValid(stub+0x0F)){
        memcpy((void*)stub,orig,16);
        V645FlushCode(stub,16);
        SendLine(s,"V645_RESTORE ok=1");
    } else SendLine(s,"V645_RESTORE ok=0");
}




static volatile LONG g_v648StreamEnabled=0;
static volatile LONG g_v648FrameSeq=0;
static volatile LONG g_v648Phase=0;
static SOCKET g_v648Socket=INVALID_SOCKET;

static const char g_v648Hex[]="0123456789ABCDEF";

static void V648SendFrame(DWORD r3,DWORD r4,DWORD r5)
{
    if(!g_v648StreamEnabled || g_v648Socket==INVALID_SOCKET) return;
    if(r4!=1) return;

    if(!r5 || !MmIsAddressValid(r5) || !MmIsAddressValid(r5+0x0F)) return;
    DWORD child=*(volatile DWORD*)(r5+0x08);
    DWORD childLen=*(volatile DWORD*)(r5+0x0C);
    if(!child || childLen<0xA0) return;
    if(!MmIsAddressValid(child) || !MmIsAddressValid(child+0x9F)) return;

    BYTE payload[0xA0];
    memcpy(payload,(void*)child,0xA0);

    DWORD seq=(DWORD)InterlockedIncrement(&g_v648FrameSeq)-1;
    DWORD phase=(DWORD)g_v648Phase;

    // Entire frame is printable ASCII on one line.
    // 160 bytes -> 320 hex chars; avoids any binary/text interleaving ambiguity.
    char line[420];
    int n=_snprintf(line,sizeof(line)-1,
        "V650_FRAME phase=%u seq=%u r3=%08X len=160 data=",
        phase,seq,r3);
    if(n<0) return;

    int p=n;
    for(int i=0;i<0xA0 && p+2<(int)sizeof(line)-2;i++){
        BYTE b=payload[i];
        line[p++]=g_v648Hex[b>>4];
        line[p++]=g_v648Hex[b&0x0F];
    }
    line[p++]='\r';
    line[p++]='\n';

    send(g_v648Socket,line,p,0);
}

extern "C" void V648Capture(DWORD r3,DWORD r4,DWORD r5,DWORD r6,DWORD r7,DWORD r8)
{
    V648SendFrame(r3,r4,r5);
}

extern "C" __declspec(naked) DWORD Hook_V648_Submit()
{
    __asm {
        stwu r1,-0xA0(r1)
        mflr r0
        stw r0,0xA4(r1)

        stw r3,0x20(r1); stw r4,0x24(r1); stw r5,0x28(r1); stw r6,0x2C(r1)
        stw r7,0x30(r1); stw r8,0x34(r1)

        bl V648Capture

        lwz r3,0x20(r1); lwz r4,0x24(r1); lwz r5,0x28(r1); lwz r6,0x2C(r1)
        lwz r7,0x30(r1); lwz r8,0x34(r1)

        lis r11,0x8170
        addi r11,r11,-0x4B68
        mtctr r11
        bctrl

        lwz r0,0xA4(r1)
        mtlr r0
        addi r1,r1,0xA0
        blr
    }
}

static BOOL V648Install(SOCKET s)
{
    DWORD stub=0x8A7B4424;
    if(!MmIsAddressValid(stub)||!MmIsAddressValid(stub+0x0F)){
        SendLine(s,"V648_INSTALL ok=0 reason=stub_unmapped");
        return FALSE;
    }

    DWORD hook=(DWORD)&Hook_V648_Submit;
    DWORD hi=(hook+0x8000)>>16;
    SHORT lo=(SHORT)(hook&0xFFFF);

    DWORD patch[4];
    patch[0]=0x3D600000 | (hi&0xFFFF);
    patch[1]=0x396B0000 | ((WORD)lo);
    patch[2]=0x7D6903A6;
    patch[3]=0x4E800420;

    memcpy((void*)stub,patch,16);
    V645FlushCode(stub,16);

    g_v648Socket=s;
    g_v648FrameSeq=0;
    g_v648Phase=0;
    g_v648StreamEnabled=0;
    SendLine(s,"V648_INSTALL ok=1 stub=0x%08X hook=0x%08X",stub,hook);
    return TRUE;
}

static void V648StartPhase(SOCKET s,DWORD phase)
{
    g_v648Phase=(LONG)phase;
    g_v648FrameSeq=0;
    g_v648StreamEnabled=1;
    SendLine(s,"V648_PHASE_START phase=%u",phase);
}

static void V648StopPhase(SOCKET s)
{
    g_v648StreamEnabled=0;
    DWORD phase=(DWORD)g_v648Phase;
    LONG frames=g_v648FrameSeq;
    g_v648Phase=0;
    SendLine(s,"V648_PHASE_STOP phase=%u frames=%ld",phase,frames);
}

static void V648Restore(SOCKET s)
{
    g_v648StreamEnabled=0;
    DWORD stub=0x8A7B4424;
    DWORD orig[4]={0x3D608170,0x396BB498,0x7D6903A6,0x4E800420};
    if(MmIsAddressValid(stub)&&MmIsAddressValid(stub+0x0F)){
        memcpy((void*)stub,orig,16);
        V645FlushCode(stub,16);
        SendLine(s,"V648_RESTORE ok=1");
    } else SendLine(s,"V648_RESTORE ok=0");
}


#define V651_MAX_SNAPS 24
#define V651_DESC_BYTES 0x80
#define V651_MAX_CANDS 6
#define V651_CAND_BYTES 0x280

typedef struct {
    DWORD off;
    DWORD ptr;
    DWORD declaredLen;
    DWORD copied;
    BYTE data[V651_CAND_BYTES];
} V651_CAND;

typedef struct {
    DWORD seq;
    DWORD r3,r4,r5;
    BYTE desc[V651_DESC_BYTES];
    DWORD candCount;
    V651_CAND cand[V651_MAX_CANDS];
} V651_SNAP;

static volatile LONG g_v651Enabled=0;
static volatile LONG g_v651Count=0;
static V651_SNAP g_v651Snaps[V651_MAX_SNAPS];

static BOOL V651LooksPtr(DWORD v)
{
    // Voice descriptors/buffers in this title commonly live around 0x4024xxxx,
    // not just kernel/high virtual memory.  Reject obvious small scalar values
    // and require the address to actually be mapped.
    if(v < 0x01000000) return FALSE;
    if((v & 0x3) != 0) return FALSE;
    return MmIsAddressValid(v);
}

static BOOL V651CanRead(DWORD p,DWORD n)
{
    if(!p || !n) return FALSE;
    return MmIsAddressValid(p) && MmIsAddressValid(p+n-1);
}

extern "C" void V651Capture(DWORD r3,DWORD r4,DWORD r5,DWORD r6,DWORD r7,DWORD r8)
{
    if(!g_v651Enabled) return;
    if(r4!=1 || r3!=0x81AACA28) return;
    if(!r5 || !V651CanRead(r5,V651_DESC_BYTES)) return;

    LONG ix=g_v651Count;
    if(ix>=V651_MAX_SNAPS) return;
    ix=InterlockedIncrement(&g_v651Count)-1;
    if(ix<0 || ix>=V651_MAX_SNAPS) return;

    V651_SNAP* s=&g_v651Snaps[ix];
    ZeroMemory(s,sizeof(*s));
    s->seq=(DWORD)ix;
    s->r3=r3; s->r4=r4; s->r5=r5;
    memcpy(s->desc,(void*)r5,V651_DESC_BYTES);

    DWORD* w=(DWORD*)s->desc;
    DWORD words=V651_DESC_BYTES/4;

    for(DWORD i=0;i<words && s->candCount<V651_MAX_CANDS;i++){
        DWORD p=w[i];
        if(!V651LooksPtr(p)) continue;

        BOOL dup=FALSE;
        for(DWORD j=0;j<s->candCount;j++){
            if(s->cand[j].ptr==p){dup=TRUE;break;}
        }
        if(dup) continue;

        DWORD declared=(i+1<words)?w[i+1]:0;
        DWORD copy=V651_CAND_BYTES;
        if(!V651CanRead(p,copy)){
            copy=0xA0;
            if(!V651CanRead(p,copy)) continue;
        }

        V651_CAND* c=&s->cand[s->candCount++];
        c->off=i*4;
        c->ptr=p;
        c->declaredLen=declared;
        c->copied=copy;
        memcpy(c->data,(void*)p,copy);
    }
}

extern "C" __declspec(naked) DWORD Hook_V651_Submit()
{
    __asm {
        stwu r1,-0xA0(r1)
        mflr r0
        stw r0,0xA4(r1)

        stw r3,0x20(r1); stw r4,0x24(r1); stw r5,0x28(r1); stw r6,0x2C(r1)
        stw r7,0x30(r1); stw r8,0x34(r1)

        bl V651Capture

        lwz r3,0x20(r1); lwz r4,0x24(r1); lwz r5,0x28(r1); lwz r6,0x2C(r1)
        lwz r7,0x30(r1); lwz r8,0x34(r1)

        lis r11,0x8170
        addi r11,r11,-0x4B68
        mtctr r11
        bctrl

        lwz r0,0xA4(r1)
        mtlr r0
        addi r1,r1,0xA0
        blr
    }
}

static BOOL V651Install(SOCKET s)
{
    DWORD stub=0x8A7B4424;
    if(!MmIsAddressValid(stub)||!MmIsAddressValid(stub+0x0F)){
        SendLine(s,"V651_INSTALL ok=0 reason=stub_unmapped");
        return FALSE;
    }

    DWORD hook=(DWORD)&Hook_V651_Submit;
    DWORD hi=(hook+0x8000)>>16;
    SHORT lo=(SHORT)(hook&0xFFFF);

    DWORD patch[4];
    patch[0]=0x3D600000 | (hi&0xFFFF);
    patch[1]=0x396B0000 | ((WORD)lo);
    patch[2]=0x7D6903A6;
    patch[3]=0x4E800420;

    memcpy((void*)stub,patch,16);
    V645FlushCode(stub,16);

    g_v651Enabled=0;
    g_v651Count=0;
    ZeroMemory(g_v651Snaps,sizeof(g_v651Snaps));

    SendLine(s,"V651_INSTALL ok=1 stub=0x%08X hook=0x%08X",stub,hook);
    return TRUE;
}

static void V651Start(SOCKET s)
{
    g_v651Enabled=0;
    g_v651Count=0;
    ZeroMemory(g_v651Snaps,sizeof(g_v651Snaps));
    g_v651Enabled=1;
    SendLine(s,"V651_CAPTURE_START max=%u lane=0x81AACA28",V651_MAX_SNAPS);
}

static void V651HexLine(SOCKET s,const char* prefix,const BYTE* data,DWORD n)
{
    static const char hx[]="0123456789ABCDEF";
    // Largest line: 0x280 bytes = 1280 hex chars + prefix.
    char line[1600];
    int p=_snprintf(line,sizeof(line)-1,"%s",prefix);
    if(p<0) return;
    for(DWORD i=0;i<n && p+2<(int)sizeof(line)-3;i++){
        BYTE b=data[i];
        line[p++]=hx[b>>4];
        line[p++]=hx[b&0xF];
    }
    line[p++]='\r'; line[p++]='\n';
    send(s,line,p,0);
}

static void V651StopDump(SOCKET s)
{
    g_v651Enabled=0;
    LONG n=g_v651Count;
    if(n>V651_MAX_SNAPS) n=V651_MAX_SNAPS;
    SendLine(s,"V651_CAPTURE_STOP snaps=%ld",n);

    for(LONG i=0;i<n;i++){
        V651_SNAP* p=&g_v651Snaps[i];
        DWORD* w=(DWORD*)p->desc;
        SendLine(s,
          "V651_DESC_META seq=%u r5=0x%08X w0=%08X w1=%08X w2=%08X w3=%08X w4=%08X w5=%08X w6=%08X w7=%08X w8=%08X w9=%08X wA=%08X wB=%08X",
          p->seq,p->r5,w[0],w[1],w[2],w[3],w[4],w[5],w[6],w[7],w[8],w[9],w[10],w[11]);

        char prefix[96];
        _snprintf(prefix,sizeof(prefix)-1,"V651_DESC_HEX seq=%u data=",p->seq);
        V651HexLine(s,prefix,p->desc,V651_DESC_BYTES);

        for(DWORD j=0;j<p->candCount;j++){
            V651_CAND* c=&p->cand[j];
            SendLine(s,
              "V651_CAND_META seq=%u slot=%u off=0x%X ptr=0x%08X declared=0x%X copied=0x%X",
              p->seq,j,c->off,c->ptr,c->declaredLen,c->copied);
            _snprintf(prefix,sizeof(prefix)-1,
              "V651_CAND_HEX seq=%u slot=%u off=%X data=",p->seq,j,c->off);
            V651HexLine(s,prefix,c->data,c->copied);
        }
    }
    SendLine(s,"V651_DUMP_DONE snaps=%ld",n);
}

static void V651Restore(SOCKET s)
{
    g_v651Enabled=0;
    DWORD stub=0x8A7B4424;
    DWORD orig[4]={0x3D608170,0x396BB498,0x7D6903A6,0x4E800420};
    if(MmIsAddressValid(stub)&&MmIsAddressValid(stub+0x0F)){
        memcpy((void*)stub,orig,16);
        V645FlushCode(stub,16);
        SendLine(s,"V651_RESTORE ok=1");
    } else SendLine(s,"V651_RESTORE ok=0");
}


#define V652_MAX_SNAPS 16
#define V652_CODE_BACK 0x80
#define V652_CODE_FWD  0x40
#define V652_CODE_BYTES (V652_CODE_BACK+V652_CODE_FWD)
#define V652_STACK_BYTES 0x100

typedef struct {
    DWORD seq;
    DWORD r3,r4,r5;
    DWORD callerLR;
    DWORD callerSP;
    DWORD child;
    DWORD childLen;
    DWORD codeBase;
    DWORD codeCopied;
    BYTE code[V652_CODE_BYTES];
    DWORD stackCopied;
    BYTE stack[V652_STACK_BYTES];
} V652_SNAP;

static volatile LONG g_v652Enabled=0;
static volatile LONG g_v652Count=0;
static V652_SNAP g_v652Snaps[V652_MAX_SNAPS];

static BOOL V652CanRead(DWORD p,DWORD n)
{
    if(!p || !n) return FALSE;
    return MmIsAddressValid(p) && MmIsAddressValid(p+n-1);
}

extern "C" void V652Capture(DWORD r3,DWORD r4,DWORD r5,DWORD callerLR,DWORD callerSP)
{
    if(!g_v652Enabled) return;
    if(r4!=1 || r3!=0x81AACA28) return;
    if(!r5 || !V652CanRead(r5,0x10)) return;

    LONG ix=g_v652Count;
    if(ix>=V652_MAX_SNAPS) return;
    ix=InterlockedIncrement(&g_v652Count)-1;
    if(ix<0 || ix>=V652_MAX_SNAPS) return;

    V652_SNAP* s=&g_v652Snaps[ix];
    ZeroMemory(s,sizeof(*s));
    s->seq=(DWORD)ix;
    s->r3=r3; s->r4=r4; s->r5=r5;
    s->callerLR=callerLR;
    s->callerSP=callerSP;
    s->child=*(volatile DWORD*)(r5+0x08);
    s->childLen=*(volatile DWORD*)(r5+0x0C);

    DWORD base=callerLR-V652_CODE_BACK;
    s->codeBase=base;
    if(V652CanRead(base,V652_CODE_BYTES)){
        memcpy(s->code,(void*)base,V652_CODE_BYTES);
        s->codeCopied=V652_CODE_BYTES;
    }

    if(callerSP && V652CanRead(callerSP,V652_STACK_BYTES)){
        memcpy(s->stack,(void*)callerSP,V652_STACK_BYTES);
        s->stackCopied=V652_STACK_BYTES;
    }
}

extern "C" __declspec(naked) DWORD Hook_V652_Submit()
{
    __asm {
        stwu r1,-0xA0(r1)
        mflr r0
        stw r0,0xA4(r1)

        stw r3,0x20(r1); stw r4,0x24(r1); stw r5,0x28(r1); stw r6,0x2C(r1)
        stw r7,0x30(r1); stw r8,0x34(r1)

        // Helper args:
        // r3,r4,r5 unchanged
        // r6 = caller LR captured before helper call
        // r7 = original caller SP (hook SP + 0xA0)
        mr r6,r0
        addi r7,r1,0xA0
        bl V652Capture

        lwz r3,0x20(r1); lwz r4,0x24(r1); lwz r5,0x28(r1); lwz r6,0x2C(r1)
        lwz r7,0x30(r1); lwz r8,0x34(r1)

        lis r11,0x8170
        addi r11,r11,-0x4B68
        mtctr r11
        bctrl

        lwz r0,0xA4(r1)
        mtlr r0
        addi r1,r1,0xA0
        blr
    }
}

static BOOL V652Install(SOCKET s)
{
    DWORD stub=0x8A7B4424;
    if(!MmIsAddressValid(stub)||!MmIsAddressValid(stub+0x0F)){
        SendLine(s,"V652_INSTALL ok=0 reason=stub_unmapped");
        return FALSE;
    }

    DWORD hook=(DWORD)&Hook_V652_Submit;
    DWORD hi=(hook+0x8000)>>16;
    SHORT lo=(SHORT)(hook&0xFFFF);

    DWORD patch[4];
    patch[0]=0x3D600000 | (hi&0xFFFF);
    patch[1]=0x396B0000 | ((WORD)lo);
    patch[2]=0x7D6903A6;
    patch[3]=0x4E800420;

    memcpy((void*)stub,patch,16);
    V645FlushCode(stub,16);

    g_v652Enabled=0;
    g_v652Count=0;
    ZeroMemory(g_v652Snaps,sizeof(g_v652Snaps));
    SendLine(s,"V652_INSTALL ok=1 stub=0x%08X hook=0x%08X",stub,hook);
    return TRUE;
}

static void V652Start(SOCKET s)
{
    g_v652Enabled=0;
    g_v652Count=0;
    ZeroMemory(g_v652Snaps,sizeof(g_v652Snaps));
    g_v652Enabled=1;
    SendLine(s,"V652_CAPTURE_START max=%u lane=0x81AACA28",V652_MAX_SNAPS);
}

static void V652HexLine(SOCKET s,const char* prefix,const BYTE* data,DWORD n)
{
    static const char hx[]="0123456789ABCDEF";
    char line[1200];
    int p=_snprintf(line,sizeof(line)-1,"%s",prefix);
    if(p<0) return;
    for(DWORD i=0;i<n && p+2<(int)sizeof(line)-3;i++){
        BYTE b=data[i];
        line[p++]=hx[b>>4];
        line[p++]=hx[b&0x0F];
    }
    line[p++]='\r'; line[p++]='\n';
    send(s,line,p,0);
}

static void V652StopDump(SOCKET s)
{
    g_v652Enabled=0;
    LONG n=g_v652Count;
    if(n>V652_MAX_SNAPS) n=V652_MAX_SNAPS;
    SendLine(s,"V652_CAPTURE_STOP snaps=%ld",n);

    for(LONG i=0;i<n;i++){
        V652_SNAP* p=&g_v652Snaps[i];
        SendLine(s,
          "V652_META seq=%u lr=0x%08X sp=0x%08X r5=0x%08X child=0x%08X childLen=0x%X codeBase=0x%08X codeCopied=0x%X stackCopied=0x%X",
          p->seq,p->callerLR,p->callerSP,p->r5,p->child,p->childLen,
          p->codeBase,p->codeCopied,p->stackCopied);

        char prefix[128];
        if(p->codeCopied){
            _snprintf(prefix,sizeof(prefix)-1,
              "V652_CODE_HEX seq=%u base=%08X data=",p->seq,p->codeBase);
            V652HexLine(s,prefix,p->code,p->codeCopied);
        }
        if(p->stackCopied){
            _snprintf(prefix,sizeof(prefix)-1,
              "V652_STACK_HEX seq=%u sp=%08X data=",p->seq,p->callerSP);
            V652HexLine(s,prefix,p->stack,p->stackCopied);
        }
    }
    SendLine(s,"V652_DUMP_DONE snaps=%ld",n);
}

static void V652Restore(SOCKET s)
{
    g_v652Enabled=0;
    DWORD stub=0x8A7B4424;
    DWORD orig[4]={0x3D608170,0x396BB498,0x7D6903A6,0x4E800420};
    if(MmIsAddressValid(stub)&&MmIsAddressValid(stub+0x0F)){
        memcpy((void*)stub,orig,16);
        V645FlushCode(stub,16);
        SendLine(s,"V652_RESTORE ok=1");
    } else SendLine(s,"V652_RESTORE ok=0");
}


#define V653_MAX_SNAPS 8
#define V653_CODE_BACK 0x300
#define V653_CODE_FWD  0x80
#define V653_CODE_BYTES (V653_CODE_BACK+V653_CODE_FWD)
#define V653_R30_BYTES 0x180
#define V653_CTX_BYTES 0x180
#define V653_DESC_BYTES 0x80
#define V653_STACK_BYTES 0x180

typedef struct {
    DWORD seq;
    DWORD r3,r4,r5;
    DWORD r29,r30,r31;
    DWORD callerLR;
    DWORD callerSP;
    DWORD ctx;
    DWORD voiceFromCtx;
    DWORD ctxFlagsD0;
    DWORD child;
    DWORD childLen;

    DWORD codeBase;
    DWORD codeCopied;
    BYTE code[V653_CODE_BYTES];

    DWORD r30Copied;
    BYTE r30mem[V653_R30_BYTES];

    DWORD ctxCopied;
    BYTE ctxmem[V653_CTX_BYTES];

    DWORD descCopied;
    BYTE desc[V653_DESC_BYTES];

    DWORD stackCopied;
    BYTE stack[V653_STACK_BYTES];
} V653_SNAP;

static volatile LONG g_v653Enabled=0;
static volatile LONG g_v653Count=0;
static V653_SNAP g_v653Snaps[V653_MAX_SNAPS];

static BOOL V653CanRead(DWORD p,DWORD n)
{
    if(!p || !n) return FALSE;
    return MmIsAddressValid(p) && MmIsAddressValid(p+n-1);
}

extern "C" void V653Capture(
    DWORD r3,DWORD r4,DWORD r5,DWORD r30,DWORD r31,DWORD r29,DWORD callerLR,DWORD callerSP)
{
    if(!g_v653Enabled) return;
    if(r4!=1 || r3!=0x81AACA28) return;
    if(!r5 || !V653CanRead(r5,0x10)) return;

    LONG ix=g_v653Count;
    if(ix>=V653_MAX_SNAPS) return;
    ix=InterlockedIncrement(&g_v653Count)-1;
    if(ix<0 || ix>=V653_MAX_SNAPS) return;

    V653_SNAP* s=&g_v653Snaps[ix];
    ZeroMemory(s,sizeof(*s));
    s->seq=(DWORD)ix;
    s->r3=r3; s->r4=r4; s->r5=r5;
    s->r29=r29; s->r30=r30; s->r31=r31;
    s->callerLR=callerLR; s->callerSP=callerSP;

    s->child=*(volatile DWORD*)(r5+0x08);
    s->childLen=*(volatile DWORD*)(r5+0x0C);

    if(r30 && V653CanRead(r30+4,4)){
        s->ctx=*(volatile DWORD*)(r30+4);
        if(s->ctx && V653CanRead(s->ctx+0xD3,4)){
            s->voiceFromCtx=*(volatile DWORD*)(s->ctx+0x0C);
            s->ctxFlagsD0=*(volatile DWORD*)(s->ctx+0xD0);
        }
    }

    DWORD codeBase=callerLR-V653_CODE_BACK;
    s->codeBase=codeBase;
    if(V653CanRead(codeBase,V653_CODE_BYTES)){
        memcpy(s->code,(void*)codeBase,V653_CODE_BYTES);
        s->codeCopied=V653_CODE_BYTES;
    }

    if(r30 && V653CanRead(r30,V653_R30_BYTES)){
        memcpy(s->r30mem,(void*)r30,V653_R30_BYTES);
        s->r30Copied=V653_R30_BYTES;
    }

    if(s->ctx && V653CanRead(s->ctx,V653_CTX_BYTES)){
        memcpy(s->ctxmem,(void*)s->ctx,V653_CTX_BYTES);
        s->ctxCopied=V653_CTX_BYTES;
    }

    if(V653CanRead(r5,V653_DESC_BYTES)){
        memcpy(s->desc,(void*)r5,V653_DESC_BYTES);
        s->descCopied=V653_DESC_BYTES;
    }

    if(callerSP && V653CanRead(callerSP,V653_STACK_BYTES)){
        memcpy(s->stack,(void*)callerSP,V653_STACK_BYTES);
        s->stackCopied=V653_STACK_BYTES;
    }
}

extern "C" __declspec(naked) DWORD Hook_V653_Submit()
{
    __asm {
        stwu r1,-0xA0(r1)
        mflr r0

        // Keep all saves INSIDE our new frame so the caller's frame is not modified.
        stw r0,0x40(r1)
        stw r3,0x20(r1); stw r4,0x24(r1); stw r5,0x28(r1); stw r6,0x2C(r1)
        stw r7,0x30(r1); stw r8,0x34(r1); stw r9,0x38(r1); stw r10,0x3C(r1)

        // V653Capture(r3,r4,r5,r30,r31,r29,callerLR,callerSP)
        mr r6,r30
        mr r7,r31
        mr r8,r29
        lwz r9,0x40(r1)
        addi r10,r1,0xA0
        bl V653Capture

        lwz r3,0x20(r1); lwz r4,0x24(r1); lwz r5,0x28(r1); lwz r6,0x2C(r1)
        lwz r7,0x30(r1); lwz r8,0x34(r1); lwz r9,0x38(r1); lwz r10,0x3C(r1)

        lis r11,0x8170
        addi r11,r11,-0x4B68
        mtctr r11
        bctrl

        lwz r0,0x40(r1)
        mtlr r0
        addi r1,r1,0xA0
        blr
    }
}

static BOOL V653Install(SOCKET s)
{
    DWORD stub=0x8A7B4424;
    if(!MmIsAddressValid(stub)||!MmIsAddressValid(stub+0x0F)){
        SendLine(s,"V653_INSTALL ok=0 reason=stub_unmapped");
        return FALSE;
    }

    DWORD hook=(DWORD)&Hook_V653_Submit;
    DWORD hi=(hook+0x8000)>>16;
    SHORT lo=(SHORT)(hook&0xFFFF);

    DWORD patch[4];
    patch[0]=0x3D600000 | (hi&0xFFFF);
    patch[1]=0x396B0000 | ((WORD)lo);
    patch[2]=0x7D6903A6;
    patch[3]=0x4E800420;

    memcpy((void*)stub,patch,16);
    V645FlushCode(stub,16);

    g_v653Enabled=0;
    g_v653Count=0;
    ZeroMemory(g_v653Snaps,sizeof(g_v653Snaps));
    SendLine(s,"V653_INSTALL ok=1 stub=0x%08X hook=0x%08X",stub,hook);
    return TRUE;
}

static void V653Start(SOCKET s)
{
    g_v653Enabled=0;
    g_v653Count=0;
    ZeroMemory(g_v653Snaps,sizeof(g_v653Snaps));
    g_v653Enabled=1;
    SendLine(s,"V653_CAPTURE_START max=%u lane=0x81AACA28",V653_MAX_SNAPS);
}

static void V653HexLine(SOCKET s,const char* prefix,const BYTE* data,DWORD n)
{
    static const char hx[]="0123456789ABCDEF";
    char line[2400];
    int p=_snprintf(line,sizeof(line)-1,"%s",prefix);
    if(p<0) return;
    for(DWORD i=0;i<n && p+2<(int)sizeof(line)-3;i++){
        BYTE b=data[i];
        line[p++]=hx[b>>4];
        line[p++]=hx[b&0xF];
    }
    line[p++]='\r'; line[p++]='\n';
    send(s,line,p,0);
}

static void V653StopDump(SOCKET s)
{
    g_v653Enabled=0;
    LONG n=g_v653Count;
    if(n>V653_MAX_SNAPS) n=V653_MAX_SNAPS;
    SendLine(s,"V653_CAPTURE_STOP snaps=%ld",n);

    for(LONG i=0;i<n;i++){
        V653_SNAP* p=&g_v653Snaps[i];
        SendLine(s,
          "V653_META seq=%u lr=0x%08X sp=0x%08X r29=0x%08X r30=0x%08X r31=0x%08X r5=0x%08X ctx=0x%08X voice=0x%08X flagsD0=0x%08X child=0x%08X childLen=0x%X",
          p->seq,p->callerLR,p->callerSP,p->r29,p->r30,p->r31,p->r5,
          p->ctx,p->voiceFromCtx,p->ctxFlagsD0,p->child,p->childLen);

        char prefix[128];
        if(p->codeCopied){
            _snprintf(prefix,sizeof(prefix)-1,
              "V653_CODE_HEX seq=%u base=%08X data=",p->seq,p->codeBase);
            V653HexLine(s,prefix,p->code,p->codeCopied);
        }
        if(p->r30Copied){
            _snprintf(prefix,sizeof(prefix)-1,
              "V653_R30_HEX seq=%u base=%08X data=",p->seq,p->r30);
            V653HexLine(s,prefix,p->r30mem,p->r30Copied);
        }
        if(p->ctxCopied){
            _snprintf(prefix,sizeof(prefix)-1,
              "V653_CTX_HEX seq=%u base=%08X data=",p->seq,p->ctx);
            V653HexLine(s,prefix,p->ctxmem,p->ctxCopied);
        }
        if(p->descCopied){
            _snprintf(prefix,sizeof(prefix)-1,
              "V653_DESC_HEX seq=%u base=%08X data=",p->seq,p->r5);
            V653HexLine(s,prefix,p->desc,p->descCopied);
        }
        if(p->stackCopied){
            _snprintf(prefix,sizeof(prefix)-1,
              "V653_STACK_HEX seq=%u base=%08X data=",p->seq,p->callerSP);
            V653HexLine(s,prefix,p->stack,p->stackCopied);
        }
    }
    SendLine(s,"V653_DUMP_DONE snaps=%ld",n);
}

static void V653Restore(SOCKET s)
{
    g_v653Enabled=0;
    DWORD stub=0x8A7B4424;
    DWORD orig[4]={0x3D608170,0x396BB498,0x7D6903A6,0x4E800420};
    if(MmIsAddressValid(stub)&&MmIsAddressValid(stub+0x0F)){
        memcpy((void*)stub,orig,16);
        V645FlushCode(stub,16);
        SendLine(s,"V653_RESTORE ok=1");
    } else SendLine(s,"V653_RESTORE ok=0");
}


#define V654_MAX_SNAPS 6
#define V654_STACK_BYTES 0x180
#define V654_CALLER_CODE_BYTES 0x300
#define V654_MAX_CALLSITES 12
#define V654_SCAN_START 0x8A480000
#define V654_SCAN_END   0x8A4A0000

typedef struct {
    DWORD seq;
    DWORD lr,sp;
    DWORD r29,r30,r31,r5;
    DWORD child,childLen;
    DWORD node0,node4;
    DWORD stackCopied;
    BYTE stack[V654_STACK_BYTES];
} V654_SNAP;

typedef struct {
    DWORD callsite;
    DWORD target;
    DWORD codeBase;
    DWORD copied;
    BYTE code[V654_CALLER_CODE_BYTES];
} V654_CALLSITE;

static volatile LONG g_v654Enabled=0;
static volatile LONG g_v654Count=0;
static V654_SNAP g_v654Snaps[V654_MAX_SNAPS];

static BOOL V654CanRead(DWORD p,DWORD n)
{
    if(!p || !n) return FALSE;
    return MmIsAddressValid(p) && MmIsAddressValid(p+n-1);
}

static LONG V654Sign26(DWORD v)
{
    v &= 0x03FFFFFC;
    if(v & 0x02000000) v |= 0xFC000000;
    return (LONG)v;
}

static BOOL V654DirectCallTarget(DWORD addr,DWORD w,DWORD* targetOut)
{
    if((w>>26)!=18) return FALSE;       // b/bl
    if((w&1)==0) return FALSE;          // LK must be set -> bl
    DWORD aa=(w>>1)&1;
    LONG disp=V654Sign26(w);
    DWORD target=aa ? (DWORD)disp : (DWORD)(addr+disp);
    if(targetOut) *targetOut=target;
    return TRUE;
}

extern "C" void V654Capture(
    DWORD r3,DWORD r4,DWORD r5,DWORD r30,DWORD r31,DWORD r29,DWORD callerLR,DWORD callerSP)
{
    if(!g_v654Enabled) return;
    if(r4!=1 || r3!=0x81AACA28) return;
    if(!r5 || !V654CanRead(r5,0x10)) return;

    LONG ix=g_v654Count;
    if(ix>=V654_MAX_SNAPS) return;
    ix=InterlockedIncrement(&g_v654Count)-1;
    if(ix<0 || ix>=V654_MAX_SNAPS) return;

    V654_SNAP* s=&g_v654Snaps[ix];
    ZeroMemory(s,sizeof(*s));
    s->seq=(DWORD)ix;
    s->lr=callerLR;s->sp=callerSP;
    s->r29=r29;s->r30=r30;s->r31=r31;s->r5=r5;
    s->child=*(volatile DWORD*)(r5+8);
    s->childLen=*(volatile DWORD*)(r5+0x0C);

    if(r29 && V654CanRead(r29,8)){
        s->node0=*(volatile DWORD*)(r29+0);
        s->node4=*(volatile DWORD*)(r29+4);
    }

    if(callerSP && V654CanRead(callerSP,V654_STACK_BYTES)){
        memcpy(s->stack,(void*)callerSP,V654_STACK_BYTES);
        s->stackCopied=V654_STACK_BYTES;
    }
}

extern "C" __declspec(naked) DWORD Hook_V654_Submit()
{
    __asm {
        stwu r1,-0xA0(r1)
        mflr r0
        stw r0,0x40(r1)
        stw r3,0x20(r1); stw r4,0x24(r1); stw r5,0x28(r1); stw r6,0x2C(r1)
        stw r7,0x30(r1); stw r8,0x34(r1); stw r9,0x38(r1); stw r10,0x3C(r1)

        mr r6,r30
        mr r7,r31
        mr r8,r29
        lwz r9,0x40(r1)
        addi r10,r1,0xA0
        bl V654Capture

        lwz r3,0x20(r1); lwz r4,0x24(r1); lwz r5,0x28(r1); lwz r6,0x2C(r1)
        lwz r7,0x30(r1); lwz r8,0x34(r1); lwz r9,0x38(r1); lwz r10,0x3C(r1)

        lis r11,0x8170
        addi r11,r11,-0x4B68
        mtctr r11
        bctrl

        lwz r0,0x40(r1)
        mtlr r0
        addi r1,r1,0xA0
        blr
    }
}

static BOOL V654Install(SOCKET s)
{
    DWORD stub=0x8A7B4424;
    if(!MmIsAddressValid(stub)||!MmIsAddressValid(stub+0x0F)){
        SendLine(s,"V654_INSTALL ok=0 reason=stub_unmapped");
        return FALSE;
    }
    DWORD hook=(DWORD)&Hook_V654_Submit;
    DWORD hi=(hook+0x8000)>>16;
    SHORT lo=(SHORT)(hook&0xFFFF);
    DWORD patch[4];
    patch[0]=0x3D600000 | (hi&0xFFFF);
    patch[1]=0x396B0000 | ((WORD)lo);
    patch[2]=0x7D6903A6;
    patch[3]=0x4E800420;
    memcpy((void*)stub,patch,16);
    V645FlushCode(stub,16);

    g_v654Enabled=0;g_v654Count=0;
    ZeroMemory(g_v654Snaps,sizeof(g_v654Snaps));
    SendLine(s,"V654_INSTALL ok=1 stub=0x%08X hook=0x%08X",stub,hook);
    return TRUE;
}

static void V654Start(SOCKET s)
{
    g_v654Enabled=0;g_v654Count=0;
    ZeroMemory(g_v654Snaps,sizeof(g_v654Snaps));
    g_v654Enabled=1;
    SendLine(s,"V654_CAPTURE_START max=%u lane=0x81AACA28",V654_MAX_SNAPS);
}

static void V654HexLine(SOCKET s,const char* prefix,const BYTE* data,DWORD n)
{
    static const char hx[]="0123456789ABCDEF";
    char line[2000];
    int p=_snprintf(line,sizeof(line)-1,"%s",prefix);
    if(p<0) return;
    for(DWORD i=0;i<n && p+2<(int)sizeof(line)-3;i++){
        BYTE b=data[i]; line[p++]=hx[b>>4];line[p++]=hx[b&0xF];
    }
    line[p++]='\r';line[p++]='\n';
    send(s,line,p,0);
}

static void V654DumpCodeWindow(SOCKET s,DWORD seq,DWORD center)
{
    DWORD base=center-0x100;
    if(!V654CanRead(base,V654_CALLER_CODE_BYTES)){
        SendLine(s,"V654_CODE_FAIL seq=%u center=0x%08X",seq,center);
        return;
    }
    BYTE buf[V654_CALLER_CODE_BYTES];
    memcpy(buf,(void*)base,V654_CALLER_CODE_BYTES);
    char prefix[128];
    _snprintf(prefix,sizeof(prefix)-1,
      "V654_CODE_HEX seq=%u center=%08X base=%08X data=",seq,center,base);
    V654HexLine(s,prefix,buf,V654_CALLER_CODE_BYTES);
}

static void V654StopDump(SOCKET s)
{
    g_v654Enabled=0;
    LONG n=g_v654Count;if(n>V654_MAX_SNAPS)n=V654_MAX_SNAPS;
    SendLine(s,"V654_CAPTURE_STOP snaps=%ld",n);

    for(LONG i=0;i<n;i++){
        V654_SNAP* p=&g_v654Snaps[i];
        SendLine(s,
          "V654_META seq=%u lr=0x%08X sp=0x%08X r29=0x%08X r30=0x%08X r31=0x%08X r5=0x%08X node0=0x%08X node4=0x%08X child=0x%08X len=0x%X",
          p->seq,p->lr,p->sp,p->r29,p->r30,p->r31,p->r5,p->node0,p->node4,p->child,p->childLen);
        if(p->stackCopied){
            char prefix[128];
            _snprintf(prefix,sizeof(prefix)-1,
              "V654_STACK_HEX seq=%u base=%08X data=",p->seq,p->sp);
            V654HexLine(s,prefix,p->stack,p->stackCopied);
        }
    }

    // Verify the strongest parent candidate from v6.53.
    DWORD w=0;
    DWORD target=0;
    if(V654CanRead(0x8A497200,4)){
        w=*(volatile DWORD*)0x8A497200;
        BOOL isbl=V654DirectCallTarget(0x8A497200,w,&target);
        SendLine(s,"V654_PARENT_VERIFY addr=0x8A497200 word=0x%08X isBL=%u target=0x%08X",
                 w,isbl?1:0,isbl?target:0);
        V654DumpCodeWindow(s,100,0x8A497204);
    }

    // Also dump other clean stack return candidates from v6.53.
    V654DumpCodeWindow(s,101,0x8A48FB18);
    V654DumpCodeWindow(s,102,0x8A491B9C);

    // Search nearby title code for every direct call into the submit-worker entry area.
    DWORD hits=0;
    for(DWORD addr=V654_SCAN_START;addr<V654_SCAN_END;addr+=4){
        if(!MmIsAddressValid(addr)) continue;
        DWORD iw=*(volatile DWORD*)addr;
        DWORD tg=0;
        if(V654DirectCallTarget(addr,iw,&tg)){
            if(tg>=0x8A496D20 && tg<=0x8A496D40){
                SendLine(s,"V654_DIRECT_CALLER hit=%u callsite=0x%08X word=0x%08X target=0x%08X",
                         hits,addr,iw,tg);
                if(hits<V654_MAX_CALLSITES) V654DumpCodeWindow(s,200+hits,addr+4);
                hits++;
            }
        }
    }
    SendLine(s,"V654_DIRECT_SCAN_DONE hits=%u range=0x%08X-0x%08X",
             hits,V654_SCAN_START,V654_SCAN_END);
    SendLine(s,"V654_DUMP_DONE snaps=%ld",n);
}

static void V654Restore(SOCKET s)
{
    g_v654Enabled=0;
    DWORD stub=0x8A7B4424;
    DWORD orig[4]={0x3D608170,0x396BB498,0x7D6903A6,0x4E800420};
    if(MmIsAddressValid(stub)&&MmIsAddressValid(stub+0x0F)){
        memcpy((void*)stub,orig,16);V645FlushCode(stub,16);
        SendLine(s,"V654_RESTORE ok=1");
    }else SendLine(s,"V654_RESTORE ok=0");
}


#define V655_MAX_SNAPS 8
#define V655_CTX_BYTES 0x180
#define V655_QUEUE_BYTES 0x40

typedef struct {
    DWORD seq;
    DWORD r3,r4,r5;
    DWORD r23,r24,r29,r30,r31;
    DWORD child,childLen;
    DWORD node0,node4;
    DWORD q0,q4,q8,qC;
    DWORD ctxCopied;
    BYTE ctx[V655_CTX_BYTES];
    DWORD queueCopied;
    BYTE queue[V655_QUEUE_BYTES];
} V655_SNAP;

static volatile LONG g_v655Enabled=0;
static volatile LONG g_v655Count=0;
static V655_SNAP g_v655Snaps[V655_MAX_SNAPS];

static BOOL V655CanRead(DWORD p,DWORD n)
{
    if(!p || !n) return FALSE;
    return MmIsAddressValid(p) && MmIsAddressValid(p+n-1);
}

extern "C" void V655Capture(
    DWORD r3,DWORD r4,DWORD r5,DWORD r23,DWORD r24,DWORD r29,DWORD r30,DWORD r31)
{
    if(!g_v655Enabled) return;
    if(r4!=1 || r3!=0x81AACA28) return;
    if(!r5 || !V655CanRead(r5,0x10)) return;

    LONG ix=g_v655Count;
    if(ix>=V655_MAX_SNAPS) return;
    ix=InterlockedIncrement(&g_v655Count)-1;
    if(ix<0 || ix>=V655_MAX_SNAPS) return;

    V655_SNAP* s=&g_v655Snaps[ix];
    ZeroMemory(s,sizeof(*s));
    s->seq=(DWORD)ix;
    s->r3=r3;s->r4=r4;s->r5=r5;
    s->r23=r23;s->r24=r24;s->r29=r29;s->r30=r30;s->r31=r31;
    s->child=*(volatile DWORD*)(r5+8);
    s->childLen=*(volatile DWORD*)(r5+0x0C);

    if(r29 && V655CanRead(r29,V655_CTX_BYTES)){
        memcpy(s->ctx,(void*)r29,V655_CTX_BYTES);
        s->ctxCopied=V655_CTX_BYTES;
    }

    if(r23 && V655CanRead(r23,V655_QUEUE_BYTES)){
        s->q0=*(volatile DWORD*)(r23+0);
        s->q4=*(volatile DWORD*)(r23+4);
        s->q8=*(volatile DWORD*)(r23+8);
        s->qC=*(volatile DWORD*)(r23+0x0C);
        memcpy(s->queue,(void*)r23,V655_QUEUE_BYTES);
        s->queueCopied=V655_QUEUE_BYTES;
    }

    if(r24 && V655CanRead(r24,8)){
        s->node0=*(volatile DWORD*)(r24+0);
        s->node4=*(volatile DWORD*)(r24+4);
    }
}

extern "C" __declspec(naked) DWORD Hook_V655_Submit()
{
    __asm {
        stwu r1,-0xA0(r1)
        mflr r0
        stw r0,0x40(r1)
        stw r3,0x20(r1); stw r4,0x24(r1); stw r5,0x28(r1); stw r6,0x2C(r1)
        stw r7,0x30(r1); stw r8,0x34(r1); stw r9,0x38(r1); stw r10,0x3C(r1)

        // V655Capture(r3,r4,r5,r23,r24,r29,r30,r31)
        mr r6,r23
        mr r7,r24
        mr r8,r29
        mr r9,r30
        mr r10,r31
        bl V655Capture

        lwz r3,0x20(r1); lwz r4,0x24(r1); lwz r5,0x28(r1); lwz r6,0x2C(r1)
        lwz r7,0x30(r1); lwz r8,0x34(r1); lwz r9,0x38(r1); lwz r10,0x3C(r1)

        lis r11,0x8170
        addi r11,r11,-0x4B68
        mtctr r11
        bctrl

        lwz r0,0x40(r1)
        mtlr r0
        addi r1,r1,0xA0
        blr
    }
}

static BOOL V655Install(SOCKET s)
{
    DWORD stub=0x8A7B4424;
    if(!MmIsAddressValid(stub)||!MmIsAddressValid(stub+0x0F)){
        SendLine(s,"V655_INSTALL ok=0 reason=stub_unmapped");
        return FALSE;
    }
    DWORD hook=(DWORD)&Hook_V655_Submit;
    DWORD hi=(hook+0x8000)>>16;
    SHORT lo=(SHORT)(hook&0xFFFF);
    DWORD patch[4];
    patch[0]=0x3D600000 | (hi&0xFFFF);
    patch[1]=0x396B0000 | ((WORD)lo);
    patch[2]=0x7D6903A6;
    patch[3]=0x4E800420;
    memcpy((void*)stub,patch,16);
    V645FlushCode(stub,16);
    g_v655Enabled=0;g_v655Count=0;
    ZeroMemory(g_v655Snaps,sizeof(g_v655Snaps));
    SendLine(s,"V655_INSTALL ok=1 stub=0x%08X hook=0x%08X",stub,hook);
    return TRUE;
}

static void V655Start(SOCKET s)
{
    g_v655Enabled=0;g_v655Count=0;
    ZeroMemory(g_v655Snaps,sizeof(g_v655Snaps));
    g_v655Enabled=1;
    SendLine(s,"V655_CAPTURE_START max=%u lane=0x81AACA28",V655_MAX_SNAPS);
}

static void V655HexLine(SOCKET s,const char* prefix,const BYTE* data,DWORD n)
{
    static const char hx[]="0123456789ABCDEF";
    char line[5000];
    int p=_snprintf(line,sizeof(line)-1,"%s",prefix);
    if(p<0) return;
    for(DWORD i=0;i<n && p+2<(int)sizeof(line)-3;i++){
        BYTE b=data[i];line[p++]=hx[b>>4];line[p++]=hx[b&0xF];
    }
    line[p++]='\r';line[p++]='\n';
    send(s,line,p,0);
}

static void V655DumpRange(SOCKET s,const char* tag,DWORD base,DWORD bytes)
{
    if(!V655CanRead(base,bytes)){
        SendLine(s,"V655_RANGE_FAIL tag=%s base=0x%08X bytes=0x%X",tag,base,bytes);
        return;
    }
    char prefix[160];
    _snprintf(prefix,sizeof(prefix)-1,
      "V655_RANGE_HEX tag=%s base=%08X bytes=%X data=",tag,base,bytes);
    V655HexLine(s,prefix,(const BYTE*)base,bytes);
}

static void V655StopDump(SOCKET s)
{
    g_v655Enabled=0;
    LONG n=g_v655Count;if(n>V655_MAX_SNAPS)n=V655_MAX_SNAPS;
    SendLine(s,"V655_CAPTURE_STOP snaps=%ld",n);

    for(LONG i=0;i<n;i++){
        V655_SNAP* p=&g_v655Snaps[i];
        SendLine(s,
          "V655_META seq=%u r23=0x%08X r24=0x%08X r29=0x%08X r30=0x%08X r31=0x%08X r5=0x%08X node0=0x%08X node4=0x%08X child=0x%08X len=0x%X q0=0x%08X q4=0x%08X q8=0x%08X qC=0x%08X",
          p->seq,p->r23,p->r24,p->r29,p->r30,p->r31,p->r5,
          p->node0,p->node4,p->child,p->childLen,p->q0,p->q4,p->q8,p->qC);

        char prefix[128];
        if(p->ctxCopied){
            _snprintf(prefix,sizeof(prefix)-1,
              "V655_CTX_HEX seq=%u base=%08X data=",p->seq,p->r29);
            V655HexLine(s,prefix,p->ctx,p->ctxCopied);
        }
        if(p->queueCopied){
            _snprintf(prefix,sizeof(prefix)-1,
              "V655_QUEUE_HEX seq=%u base=%08X data=",p->seq,p->r23);
            V655HexLine(s,prefix,p->queue,p->queueCopied);
        }
    }

    // Whole main processing function containing active queue drain.
    V655DumpRange(s,"MAIN_496F40",0x8A496F40,0x490);

    // Immediate helpers near producer/consumer boundary.
    V655DumpRange(s,"HELPER_496E00",0x8A496E00,0x80);
    V655DumpRange(s,"HELPER_496E80",0x8A496E80,0xC0);
    V655DumpRange(s,"HELPER_496AA8",0x8A496AA8,0xA8);
    V655DumpRange(s,"HELPER_496B50",0x8A496B50,0x1E0);

    SendLine(s,"V655_DUMP_DONE snaps=%ld",n);
}

static void V655Restore(SOCKET s)
{
    g_v655Enabled=0;
    DWORD stub=0x8A7B4424;
    DWORD orig[4]={0x3D608170,0x396BB498,0x7D6903A6,0x4E800420};
    if(MmIsAddressValid(stub)&&MmIsAddressValid(stub+0x0F)){
        memcpy((void*)stub,orig,16);V645FlushCode(stub,16);
        SendLine(s,"V655_RESTORE ok=1");
    }else SendLine(s,"V655_RESTORE ok=0");
}


#define V656_SCAN_START 0x8A000000
#define V656_SCAN_END   0x8B000000
#define V656_MAX_MATCHES 8
#define V656_MAX_CALLERS 32
#define V656_CALL_DUMP_BACK 0x60
#define V656_CALL_DUMP_FWD  0x30
#define V656_CALL_DUMP_BYTES (V656_CALL_DUMP_BACK+V656_CALL_DUMP_FWD)

static DWORD g_v656Matches[V656_MAX_MATCHES];
static DWORD g_v656MatchCount=0;

static BOOL V656CanRead(DWORD p,DWORD n)
{
    if(!p || !n) return FALSE;
    return MmIsAddressValid(p) && MmIsAddressValid(p+n-1);
}

static LONG V656Sign26(DWORD v)
{
    v &= 0x03FFFFFC;
    if(v & 0x02000000) v |= 0xFC000000;
    return (LONG)v;
}

static BOOL V656DirectCallTarget(DWORD addr,DWORD w,DWORD* targetOut)
{
    if((w>>26)!=18) return FALSE;
    if((w&1)==0) return FALSE;
    DWORD aa=(w>>1)&1;
    LONG disp=V656Sign26(w);
    DWORD target=aa ? (DWORD)disp : (DWORD)(addr+disp);
    if(targetOut) *targetOut=target;
    return TRUE;
}

static void V656HexLine(SOCKET s,const char* prefix,const BYTE* data,DWORD n)
{
    static const char hx[]="0123456789ABCDEF";
    char line[900];
    int p=_snprintf(line,sizeof(line)-1,"%s",prefix);
    if(p<0) return;
    for(DWORD i=0;i<n && p+2<(int)sizeof(line)-3;i++){
        BYTE b=data[i];
        line[p++]=hx[b>>4];
        line[p++]=hx[b&0x0F];
    }
    line[p++]='\r';
    line[p++]='\n';
    send(s,line,p,0);
}

static BOOL V656MatchesG726(DWORD entry)
{
    // Xenia commit 9ee793f / 71af154:
    // xhv2.lib g726adpcm entry:
    // +0x00 mflr r12 = 7D 88 02 A6
    // +0x08 fixed 28-byte signature:
    // li r14,0
    // stw r4,0x1C(r1)
    // mr r8,r4
    // stw r5,0x24(r1)
    // stw r6,0x2C(r1)
    // cmpwi cr6,r6,0
    // stb r14,-0xA0(r1)
    static const BYTE mflr[4]={0x7D,0x88,0x02,0xA6};
    static const BYTE sig[28]={
        0x39,0xC0,0x00,0x00,
        0x90,0x81,0x00,0x1C,
        0x7C,0x88,0x23,0x78,
        0x90,0xA1,0x00,0x24,
        0x90,0xC1,0x00,0x2C,
        0x2F,0x06,0x00,0x00,
        0x99,0xC1,0xFF,0x60
    };

    if(!V656CanRead(entry,36)) return FALSE;
    if(memcmp((const void*)entry,mflr,4)!=0) return FALSE;
    if(memcmp((const void*)(entry+8),sig,28)!=0) return FALSE;
    return TRUE;
}

static void V656DumpCaller(SOCKET s,DWORD index,DWORD callsite)
{
    DWORD base=callsite-V656_CALL_DUMP_BACK;
    if(!V656CanRead(base,V656_CALL_DUMP_BYTES)){
        SendLine(s,"V656_CALLER_DUMP_FAIL index=%u callsite=0x%08X",index,callsite);
        return;
    }
    char prefix[128];
    _snprintf(prefix,sizeof(prefix)-1,
      "V656_CALLER_HEX index=%u callsite=%08X base=%08X data=",
      index,callsite,base);
    V656HexLine(s,prefix,(const BYTE*)base,V656_CALL_DUMP_BYTES);
}

static void V656ScanCallers(SOCKET s,DWORD target)
{
    DWORD count=0;
    SendLine(s,"V656_CALL_SCAN_START target=0x%08X range=0x%08X-0x%08X",
             target,V656_SCAN_START,V656_SCAN_END);

    for(DWORD page=V656_SCAN_START; page<V656_SCAN_END; page+=0x1000){
        if(!MmIsAddressValid(page)) continue;
        DWORD pageEnd=page+0x1000;
        for(DWORD a=page; a+4<=pageEnd; a+=4){
            DWORD w=*(volatile DWORD*)a;
            DWORD tg=0;
            if(V656DirectCallTarget(a,w,&tg) && tg==target){
                SendLine(s,"V656_CALLER index=%u callsite=0x%08X word=0x%08X target=0x%08X",
                         count,a,w,tg);
                if(count<V656_MAX_CALLERS) V656DumpCaller(s,count,a);
                count++;
            }
        }
    }
    SendLine(s,"V656_CALL_SCAN_DONE target=0x%08X callers=%u",target,count);
}

static void V656Scan(SOCKET s)
{
    g_v656MatchCount=0;
    ZeroMemory(g_v656Matches,sizeof(g_v656Matches));

    SendLine(s,
      "V656_SCAN_START kind=XHV2_G726_XBADPCM range=0x%08X-0x%08X",
      V656_SCAN_START,V656_SCAN_END);

    DWORD pages=0;
    DWORD readablePages=0;
    for(DWORD page=V656_SCAN_START; page<V656_SCAN_END; page+=0x1000){
        pages++;
        if(!MmIsAddressValid(page)) continue;
        readablePages++;

        // Keep the whole 36-byte signature in one mapped page.
        DWORD end=page+0x1000-36;
        for(DWORD a=page; a<=end; a+=4){
            if(*(volatile DWORD*)a!=0x7D8802A6) continue;
            if(*(volatile DWORD*)(a+8)!=0x39C00000) continue;
            if(!V656MatchesG726(a)) continue;

            DWORD index=g_v656MatchCount;
            if(index<V656_MAX_MATCHES) g_v656Matches[index]=a;
            g_v656MatchCount++;

            DWORD word4=*(volatile DWORD*)(a+4);
            DWORD helper=0;
            BOOL isbl=V656DirectCallTarget(a+4,word4,&helper);

            SendLine(s,
              "V656_MATCH index=%u entry=0x%08X word4=0x%08X prologueBL=%u helper=0x%08X",
              index,a,word4,isbl?1:0,isbl?helper:0);

            if(V656CanRead(a,0x100)){
                char prefix[128];
                _snprintf(prefix,sizeof(prefix)-1,
                  "V656_ENTRY_HEX index=%u entry=%08X data=",index,a);
                V656HexLine(s,prefix,(const BYTE*)a,0x100);
            }
        }
    }

    SendLine(s,
      "V656_SCAN_DONE matches=%u pages=%u readablePages=%u",
      g_v656MatchCount,pages,readablePages);

    DWORD n=g_v656MatchCount;
    if(n>V656_MAX_MATCHES) n=V656_MAX_MATCHES;
    for(DWORD i=0;i<n;i++){
        if(g_v656Matches[i]) V656ScanCallers(s,g_v656Matches[i]);
    }
    SendLine(s,"V656_ALL_DONE matches=%u",g_v656MatchCount);
}


#define V659_G726_ENTRY 0x8A495510
#define V659_CALLSITE_A 0x8A497084
#define V659_CALLSITE_B 0x8A4970A0
#define V659_GATEWAY_STUB 0x8A7B4424
#define V659_REAL_XAM_SUBMIT 0x816FB498
#define V659_MAX_CALLS 96
#define V659_INPUT_MAX 0x100
#define V659_OUTPUT_MAX 0x800

typedef struct {
    DWORD seq;
    DWORD callsite;
    DWORD mode;
    DWORD inPtr;
    DWORD outPtr;
    DWORD count;
    DWORD statePtr;
    DWORD inputBytes;
    BYTE input[V659_INPUT_MAX];
    DWORD outputBytes;
    BYTE output[V659_OUTPUT_MAX];
} V659_CALL;

static volatile LONG g_v659Enabled=0;
static volatile LONG g_v659Count=0;
static BOOL g_v659Installed=FALSE;
static DWORD g_v659OrigStub[4]={0,0,0,0};
static DWORD g_v659OrigCallA=0;
static DWORD g_v659OrigCallB=0;
static V659_CALL g_v659Calls[V659_MAX_CALLS];

// v6.60.1 controlled PCM replacement state.
static volatile LONG g_v660InjectEnabled=0;
static volatile LONG g_v660InjectedCalls=0;
static volatile LONG g_v660InjectedSamples=0;
static DWORD g_v660TonePhase=0;

// 1 kHz sine sampled at 16 kHz, approximately +/-6000 PCM16.
static const SHORT g_v660Tone[16]={
     0, 2296, 4243, 5543, 6000, 5543, 4243, 2296,
     0,-2296,-4243,-5543,-6000,-5543,-4243,-2296
};


static BOOL V659CanRead(DWORD p,DWORD n)
{
    if(!p || !n) return FALSE;
    return MmIsAddressValid(p) && MmIsAddressValid(p+n-1);
}

static void V660InjectTone(DWORD outPtr,DWORD count)
{
    if(!g_v660InjectEnabled || !outPtr || !count) return;
    if(count>1024) return;
    if(!V659CanRead(outPtr,count*2)) return;

    volatile BYTE* out=(volatile BYTE*)outPtr;
    DWORD ph=g_v660TonePhase;
    for(DWORD i=0;i<count;i++){
        SHORT sample=g_v660Tone[ph&15];
        WORD u=(WORD)sample;
        out[i*2+0]=(BYTE)(u>>8);
        out[i*2+1]=(BYTE)(u&0xFF);
        ph++;
    }
    g_v660TonePhase=ph&15;
    InterlockedIncrement(&g_v660InjectedCalls);
    InterlockedExchangeAdd(&g_v660InjectedSamples,(LONG)count);
}


#define V661_FRAME_SAMPLES 160
#define V661_FRAME_BYTES   320
#define V661_RING_FRAMES   32

static BYTE g_v661Ring[V661_RING_FRAMES][V661_FRAME_BYTES];
static volatile LONG g_v661WriteSeq=0;
static volatile LONG g_v661ReadSeq=0;
static volatile LONG g_v661RxFrames=0;
static volatile LONG g_v661RxBad=0;
static volatile LONG g_v661OverrunDrops=0;
static volatile LONG g_v661Consumed=0;
static volatile LONG g_v661Underruns=0;
static volatile LONG g_v661SilenceBlocks=0;
static volatile LONG g_v661StreamEnabled=0;

static void V661ResetRing()
{
    g_v661StreamEnabled=0;
    g_v661WriteSeq=0;
    g_v661ReadSeq=0;
    g_v661RxFrames=0;
    g_v661RxBad=0;
    g_v661OverrunDrops=0;
    g_v661Consumed=0;
    g_v661Underruns=0;
    g_v661SilenceBlocks=0;
    ZeroMemory(g_v661Ring,sizeof(g_v661Ring));
}

static LONG V661Depth()
{
    LONG w=g_v661WriteSeq;
    LONG r=g_v661ReadSeq;
    LONG d=w-r;
    if(d<0) d=0;
    if(d>V661_RING_FRAMES) d=V661_RING_FRAMES;
    return d;
}

static void V661PushFrame(const BYTE* data,DWORD bytes)
{
    if(!data || bytes!=V661_FRAME_BYTES){
        InterlockedIncrement(&g_v661RxBad);
        return;
    }

    LONG w=g_v661WriteSeq;
    LONG r=g_v661ReadSeq;
    if((w-r)>=V661_RING_FRAMES){
        // Drop the oldest frame so latency stays bounded.
        InterlockedIncrement(&g_v661ReadSeq);
        InterlockedIncrement(&g_v661OverrunDrops);
        r++;
    }

    DWORD slot=(DWORD)w & (V661_RING_FRAMES-1);
    memcpy(g_v661Ring[slot],data,V661_FRAME_BYTES);

    // Publish only after the full frame copy is complete.
    InterlockedExchange(&g_v661WriteSeq,w+1);
    InterlockedIncrement(&g_v661RxFrames);
}

extern "C" void V661InjectStream(DWORD outPtr,DWORD count)
{
    if(!g_v661StreamEnabled) return;
    if(!outPtr || count!=V661_FRAME_SAMPLES) return;
    if(!V659CanRead(outPtr,V661_FRAME_BYTES)) return;

    LONG w=g_v661WriteSeq;
    LONG r=g_v661ReadSeq;
    LONG depth=w-r;

    // If transport stalls and then catches up, throw away stale audio.
    // Keep at most ~50 ms queued.
    if(depth>5){
        LONG newR=w-5;
        LONG dropped=newR-r;
        if(dropped>0){
            InterlockedExchange(&g_v661ReadSeq,newR);
            InterlockedExchangeAdd(&g_v661OverrunDrops,dropped);
            r=newR;
            depth=w-r;
        }
    }

    volatile BYTE* out=(volatile BYTE*)outPtr;

    if(depth<=0){
        // Never leak the damaged/physical microphone during a LAN underrun.
        // Emit silence until PC audio catches up.
        memset((void*)out,0,V661_FRAME_BYTES);
        InterlockedIncrement(&g_v661Underruns);
        InterlockedIncrement(&g_v661SilenceBlocks);
        return;
    }

    DWORD slot=(DWORD)r & (V661_RING_FRAMES-1);
    memcpy((void*)out,g_v661Ring[slot],V661_FRAME_BYTES);
    InterlockedExchange(&g_v661ReadSeq,r+1);
    InterlockedIncrement(&g_v661Consumed);
}

static void V661Start(SOCKET s)
{
    g_v660InjectEnabled=0;
    g_v659Enabled=0;

    LONG depth=V661Depth();
    g_v661StreamEnabled=1;

    SendLine(s,
      "V661_STREAM_START sampleRate=16000 samplesPerBlock=160 bytesPerBlock=320 prebuffer=%ld",
      depth);
}

static void V661Stop(SOCKET s)
{
    g_v661StreamEnabled=0;
    SendLine(s,
      "V661_STREAM_STOP rxFrames=%ld consumed=%ld underruns=%ld silence=%ld overrunDrops=%ld badFrames=%ld depth=%ld",
      g_v661RxFrames,g_v661Consumed,g_v661Underruns,g_v661SilenceBlocks,
      g_v661OverrunDrops,g_v661RxBad,V661Depth());
}

static BOOL V659MakeBL(DWORD from,DWORD to,DWORD* out)
{
    LONG delta=(LONG)(to-from);
    if((delta & 3)!=0) return FALSE;
    if(delta < (LONG)0xFE000000 || delta > (LONG)0x01FFFFFC) return FALSE;
    if(out) *out=0x48000001 | ((DWORD)delta & 0x03FFFFFC);
    return TRUE;
}

extern "C" LONG V659CapturePre(
    DWORD callsite,DWORD mode,DWORD inPtr,DWORD outPtr,DWORD count,DWORD statePtr)
{
    if(!g_v659Enabled) return -1;

    LONG ix=g_v659Count;
    if(ix>=V659_MAX_CALLS) return -1;
    ix=InterlockedIncrement(&g_v659Count)-1;
    if(ix<0 || ix>=V659_MAX_CALLS) return -1;

    V659_CALL* p=&g_v659Calls[ix];
    ZeroMemory(p,sizeof(*p));
    p->seq=(DWORD)ix;
    p->callsite=callsite;
    p->mode=mode;
    p->inPtr=inPtr;
    p->outPtr=outPtr;
    p->count=count;
    p->statePtr=statePtr;

    if(mode==1 && inPtr && count){
        DWORD bytes=(count+1)/2;
        if(bytes>V659_INPUT_MAX) bytes=V659_INPUT_MAX;
        if(bytes && V659CanRead(inPtr,bytes)){
            memcpy(p->input,(const void*)inPtr,bytes);
            p->inputBytes=bytes;
        }
    }
    return ix;
}

extern "C" void V659CapturePost(LONG ix)
{
    if(ix<0 || ix>=V659_MAX_CALLS) return;

    V659_CALL* p=&g_v659Calls[ix];
    if(p->mode!=1 || !p->outPtr || !p->count) return;

    // Preserve the untouched Microsoft decoder output for analysis.
    DWORD bytes=p->count*2;
    if(bytes>V659_OUTPUT_MAX) bytes=V659_OUTPUT_MAX;
    if(bytes && V659CanRead(p->outPtr,bytes)){
        memcpy(p->output,(const void*)p->outPtr,bytes);
        p->outputBytes=bytes;
    }

    // Controlled replacement occurs only AFTER Microsoft's g726 returns and
    // BEFORE the original XHV2 caller resumes.
    V660InjectTone(p->outPtr,p->count);
}

extern "C" __declspec(naked) DWORD Hook_V659_Gateway()
{
    __asm {
        // This hook is reached in two ways:
        // 1) normal XamVoiceSubmitPacket callers -> tail-route to real XAM
        // 2) our two redirected g726 mode-1 callsites -> observe + call real g726

        mflr r0

        // Check LR == callsite A + 4.
        lis r11,0x8A49
        ori r11,r11,0x7088
        cmpw cr6,r0,r11
        beq cr6,V659_G726_A

        // Check LR == callsite B + 4.
        lis r11,0x8A49
        ori r11,r11,0x70A4
        cmpw cr6,r0,r11
        beq cr6,V659_G726_B

        // Normal XamVoiceSubmitPacket call. Preserve original caller LR and
        // tail-route directly to the real implementation.
        lis r11,0x8170
        addi r11,r11,-0x4B68
        mtctr r11
        bctr

V659_G726_A:
        lis r11,0x8A49
        ori r11,r11,0x7084
        b V659_G726_COMMON

V659_G726_B:
        lis r11,0x8A49
        ori r11,r11,0x70A0

V659_G726_COMMON:
        // r11 = exact original g726 callsite.
        stwu r1,-0xC0(r1)
        stw r0,0x70(r1)

        stw r3,0x20(r1)
        stw r4,0x24(r1)
        stw r5,0x28(r1)
        stw r6,0x2C(r1)
        stw r7,0x30(r1)
        stw r8,0x34(r1)
        stw r9,0x38(r1)
        stw r10,0x3C(r1)
        stw r11,0x40(r1)
        stw r12,0x44(r1)

        // V659CapturePre(callsite, mode, in, out, count, state)
        // PPC ABI: first 6 args in r3-r8.
        mr r8,r7
        mr r7,r6
        mr r6,r5
        mr r5,r4
        mr r4,r3
        mr r3,r11
        bl V659CapturePre
        stw r3,0x48(r1)

        // Restore the original g726 arguments.
        lwz r3,0x20(r1)
        lwz r4,0x24(r1)
        lwz r5,0x28(r1)
        lwz r6,0x2C(r1)
        lwz r7,0x30(r1)
        lwz r8,0x34(r1)
        lwz r9,0x38(r1)
        lwz r10,0x3C(r1)
        lwz r11,0x40(r1)
        lwz r12,0x44(r1)

        // Call Microsoft's untouched g726adpcm.
        lis r0,0x8A49
        ori r0,r0,0x5510
        mtctr r0
        bctrl

        // Preserve Microsoft's original codec return value.
        stw r3,0x4C(r1)

        // v6.61: replace the decoded 160-sample PCM block from the LAN ring.
        // Saved original g726 arguments:
        //   +0x28 = out pointer (original r5)
        //   +0x2C = sample count (original r6)
        lwz r3,0x28(r1)
        lwz r4,0x2C(r1)
        bl V661InjectStream

        lwz r3,0x4C(r1)

        // Restore the original caller LR (callsite+4) and return exactly as a
        // normal g726 call would.
        lwz r0,0x70(r1)
        mtlr r0
        addi r1,r1,0xC0
        blr
    }
}

static BOOL V659Install(SOCKET s)
{
    DWORD stub=V659_GATEWAY_STUB;
    DWORD ca=V659_CALLSITE_A;
    DWORD cb=V659_CALLSITE_B;

    if(!V659CanRead(stub,16) || !V659CanRead(ca,4) || !V659CanRead(cb,4)){
        SendLine(s,"V659_INSTALL ok=0 reason=unmapped");
        return FALSE;
    }

    DWORD sw0=*(volatile DWORD*)(stub+0);
    DWORD sw1=*(volatile DWORD*)(stub+4);
    DWORD sw2=*(volatile DWORD*)(stub+8);
    DWORD sw3=*(volatile DWORD*)(stub+12);
    DWORD wa=*(volatile DWORD*)ca;
    DWORD wb=*(volatile DWORD*)cb;

    SendLine(s,
      "V659_VERIFY stub=0x%08X words=%08X,%08X,%08X,%08X callA=0x%08X wordA=%08X callB=0x%08X wordB=%08X",
      stub,sw0,sw1,sw2,sw3,ca,wa,cb,wb);

    if(sw0!=0x3D608170 || sw1!=0x396BB498 ||
       sw2!=0x7D6903A6 || sw3!=0x4E800420 ||
       wa!=0x4BFFE48D || wb!=0x4BFFE471){
        SendLine(s,"V659_INSTALL ok=0 reason=unexpected_original_code");
        return FALSE;
    }

    DWORD bla=0,blb=0;
    if(!V659MakeBL(ca,stub,&bla) || !V659MakeBL(cb,stub,&blb)){
        SendLine(s,"V659_INSTALL ok=0 reason=branch_range");
        return FALSE;
    }

    g_v659OrigStub[0]=sw0;
    g_v659OrigStub[1]=sw1;
    g_v659OrigStub[2]=sw2;
    g_v659OrigStub[3]=sw3;
    g_v659OrigCallA=wa;
    g_v659OrigCallB=wb;

    // First make the nearby XAM stub our gateway. This stub has already been
    // safely patched by many previous probes.
    DWORD hook=(DWORD)&Hook_V659_Gateway;
    DWORD hi=(hook+0x8000)>>16;
    SHORT lo=(SHORT)(hook&0xFFFF);
    DWORD patch[4];
    patch[0]=0x3D600000 | (hi&0xFFFF);
    patch[1]=0x396B0000 | ((WORD)lo);
    patch[2]=0x7D6903A6;
    patch[3]=0x4E800420;
    memcpy((void*)stub,patch,16);
    V645FlushCode(stub,16);

    // Redirect only the two known mode-1 g726 BL instructions to the nearby
    // gateway. Microsoft's codec body remains untouched.
    *(volatile DWORD*)ca=bla;
    *(volatile DWORD*)cb=blb;
    V645FlushCode(ca,4);
    V645FlushCode(cb,4);

    g_v659Enabled=0;
    g_v659Count=0;
    g_v660InjectEnabled=0;
    g_v660InjectedCalls=0;
    g_v660InjectedSamples=0;
    g_v660TonePhase=0;
    ZeroMemory(g_v659Calls,sizeof(g_v659Calls));
    V661ResetRing();
    g_v659Installed=TRUE;

    SendLine(s,
      "V659_INSTALL ok=1 gateway=0x%08X hook=0x%08X callApatch=%08X callBpatch=%08X g726Untouched=0x%08X",
      stub,hook,bla,blb,V659_G726_ENTRY);
    return TRUE;
}

static void V659Start(SOCKET s)
{
    g_v659Enabled=0;
    g_v660InjectEnabled=0;
    g_v659Count=0;
    g_v660InjectedCalls=0;
    g_v660InjectedSamples=0;
    g_v660TonePhase=0;
    ZeroMemory(g_v659Calls,sizeof(g_v659Calls));

    g_v659Enabled=1;
    g_v660InjectEnabled=1;
    SendLine(s,"V660_INJECT_START toneHz=1000 sampleRate=16000 amplitude=6000 blockSamples=160");
}

static void V659SendHex(SOCKET s,const char* prefix,const BYTE* data,DWORD n)
{
    static const char hx[]="0123456789ABCDEF";
    char line[4500];
    int p=_snprintf(line,sizeof(line)-1,"%s",prefix);
    if(p<0) return;

    for(DWORD i=0;i<n && p+2<(int)sizeof(line)-3;i++){
        BYTE b=data[i];
        line[p++]=hx[b>>4];
        line[p++]=hx[b&0x0F];
    }
    line[p++]='\r';
    line[p++]='\n';

    int sent=0;
    while(sent<p){
        int r=send(s,line+sent,p-sent,0);
        if(r<=0) break;
        sent+=r;
    }
}

static void V659StopDump(SOCKET s)
{
    g_v660InjectEnabled=0;
    g_v659Enabled=0;
    SendLine(s,"V660_INJECT_STOP injectedCalls=%ld injectedSamples=%ld",
             g_v660InjectedCalls,g_v660InjectedSamples);
    LONG n=g_v659Count;
    if(n>V659_MAX_CALLS) n=V659_MAX_CALLS;

    DWORD a=0,b=0,other=0;
    for(LONG i=0;i<n;i++){
        if(g_v659Calls[i].callsite==V659_CALLSITE_A) a++;
        else if(g_v659Calls[i].callsite==V659_CALLSITE_B) b++;
        else other++;
    }

    SendLine(s,
      "V659_CAPTURE_STOP calls=%ld callA=%u callB=%u other=%u",
      n,a,b,other);

    for(LONG i=0;i<n;i++){
        V659_CALL* p=&g_v659Calls[i];
        SendLine(s,
          "V659_META seq=%u callsite=0x%08X mode=%u in=0x%08X out=0x%08X count=%u state=0x%08X inputBytes=%u outputBytes=%u",
          p->seq,p->callsite,p->mode,p->inPtr,p->outPtr,p->count,p->statePtr,
          p->inputBytes,p->outputBytes);

        char prefix[120];
        if(p->inputBytes){
            _snprintf(prefix,sizeof(prefix)-1,"V659_INPUT_HEX seq=%u data=",p->seq);
            V659SendHex(s,prefix,p->input,p->inputBytes);
        }
        if(p->outputBytes){
            _snprintf(prefix,sizeof(prefix)-1,"V659_OUTPUT_HEX seq=%u data=",p->seq);
            V659SendHex(s,prefix,p->output,p->outputBytes);
        }
    }
    SendLine(s,"V659_DUMP_DONE calls=%ld",n);
}

static void V659Restore(SOCKET s)
{
    g_v661StreamEnabled=0;
    g_v660InjectEnabled=0;
    g_v659Enabled=0;
    if(!g_v659Installed){
        SendLine(s,"V659_RESTORE already=0");
        return;
    }

    BOOL ok=TRUE;

    // Restore redirected callsites FIRST so nothing new enters the gateway.
    if(V659CanRead(V659_CALLSITE_A,4)){
        *(volatile DWORD*)V659_CALLSITE_A=g_v659OrigCallA;
        V645FlushCode(V659_CALLSITE_A,4);
    }else ok=FALSE;

    if(V659CanRead(V659_CALLSITE_B,4)){
        *(volatile DWORD*)V659_CALLSITE_B=g_v659OrigCallB;
        V645FlushCode(V659_CALLSITE_B,4);
    }else ok=FALSE;

    if(V659CanRead(V659_GATEWAY_STUB,16)){
        memcpy((void*)V659_GATEWAY_STUB,g_v659OrigStub,16);
        V645FlushCode(V659_GATEWAY_STUB,16);
    }else ok=FALSE;

    g_v659Installed=FALSE;
    SendLine(s,"V659_RESTORE ok=%u",ok?1:0);
}


#define V662_SCAN_START 0x82000000
#define V662_SCAN_END   0x90000000
#define V662_MAX_CALLS  4

static volatile DWORD g_v662G726=0;
static volatile DWORD g_v662GatewayStub=0;
static volatile DWORD g_v662Callsites[V662_MAX_CALLS]={0,0,0,0};
static DWORD g_v662OrigCalls[V662_MAX_CALLS]={0,0,0,0};
static DWORD g_v662CallCount=0;
static DWORD g_v662OrigStub[4]={0,0,0,0};
static BOOL g_v662Installed=FALSE;

static BOOL V662CanRead(DWORD p,DWORD n)
{
    if(!p || !n) return FALSE;
    return MmIsAddressValid(p) && MmIsAddressValid(p+n-1);
}

static BOOL V662MakeBL(DWORD from,DWORD to,DWORD* out)
{
    LONG delta=(LONG)(to-from);
    if((delta&3)!=0) return FALSE;
    if(delta<(LONG)0xFE000000 || delta>(LONG)0x01FFFFFC) return FALSE;
    if(out) *out=0x48000001 | ((DWORD)delta&0x03FFFFFC);
    return TRUE;
}

static BOOL V662StubMatchesSubmit(DWORD a)
{
    if(!V662CanRead(a,16)) return FALSE;
    return
      *(volatile DWORD*)(a+0x00)==0x3D608170 &&
      *(volatile DWORD*)(a+0x04)==0x396BB498 &&
      *(volatile DWORD*)(a+0x08)==0x7D6903A6 &&
      *(volatile DWORD*)(a+0x0C)==0x4E800420;
}

static BOOL V662AllCallsReach(DWORD stub)
{
    if(!stub || !g_v662CallCount) return FALSE;
    for(DWORD i=0;i<g_v662CallCount;i++){
        DWORD tmp=0;
        if(!V662MakeBL(g_v662Callsites[i],stub,&tmp)) return FALSE;
    }
    return TRUE;
}

static DWORD V662FindG726(SOCKET s)
{
    SendLine(s,"V662_SCAN_G726 range=0x%08X-0x%08X",V662_SCAN_START,V662_SCAN_END);

    DWORD readable=0;
    for(DWORD page=V662_SCAN_START; page<V662_SCAN_END; page+=0x1000){
        if(!MmIsAddressValid(page)) continue;
        readable++;

        DWORD end=page+0x1000-36;
        for(DWORD a=page; a<=end; a+=4){
            if(*(volatile DWORD*)a!=0x7D8802A6) continue;
            if(*(volatile DWORD*)(a+8)!=0x39C00000) continue;
            if(V656MatchesG726(a)){
                SendLine(s,"V662_G726_FOUND entry=0x%08X readablePages=%u",a,readable);
                return a;
            }
        }
    }

    SendLine(s,"V662_G726_NOT_FOUND readablePages=%u",readable);
    return 0;
}

static DWORD V662FindMode1Calls(SOCKET s,DWORD target)
{
    ZeroMemory((void*)g_v662Callsites,sizeof(g_v662Callsites));
    g_v662CallCount=0;

    SendLine(s,"V662_SCAN_CALLERS target=0x%08X",target);

    for(DWORD page=V662_SCAN_START; page<V662_SCAN_END; page+=0x1000){
        if(!MmIsAddressValid(page)) continue;
        DWORD end=page+0x1000;

        for(DWORD a=page+4; a+4<=end; a+=4){
            DWORD w=*(volatile DWORD*)a;
            DWORD tg=0;
            if(!V656DirectCallTarget(a,w,&tg) || tg!=target) continue;

            DWORD prev=*(volatile DWORD*)(a-4);
            DWORD mode1=(prev==0x38600001)?1:0;

            SendLine(s,
              "V662_G726_CALL callsite=0x%08X word=0x%08X prev=0x%08X mode1=%u",
              a,w,prev,mode1);

            if(mode1 && g_v662CallCount<V662_MAX_CALLS){
                g_v662Callsites[g_v662CallCount++]=a;
            }
        }
    }

    SendLine(s,"V662_MODE1_CALLS count=%u",g_v662CallCount);
    return g_v662CallCount;
}

static DWORD V662FindGatewayStub(SOCKET s)
{
    SendLine(s,"V662_SCAN_GATEWAY target=XamVoiceSubmitPacket");

    for(DWORD page=V662_SCAN_START; page<V662_SCAN_END; page+=0x1000){
        if(!MmIsAddressValid(page)) continue;
        DWORD end=page+0x1000-16;

        for(DWORD a=page; a<=end; a+=4){
            if(*(volatile DWORD*)a!=0x3D608170) continue;
            if(!V662StubMatchesSubmit(a)) continue;

            DWORD reachable=V662AllCallsReach(a)?1:0;
            SendLine(s,
              "V662_SUBMIT_STUB candidate=0x%08X reachable=%u",
              a,reachable);

            if(reachable) return a;
        }
    }

    return 0;
}

extern "C" DWORD V662IsOurCaller(DWORD lr)
{
    DWORD count=g_v662CallCount;
    if(count>V662_MAX_CALLS) count=V662_MAX_CALLS;
    for(DWORD i=0;i<count;i++){
        DWORD c=g_v662Callsites[i];
        if(c && lr==(c+4)) return 1;
    }
    return 0;
}

typedef DWORD (*V662G726FN)(DWORD,DWORD,DWORD,DWORD,DWORD);

extern "C" DWORD V662InvokeG726(
    DWORD mode,DWORD inPtr,DWORD outPtr,DWORD count,DWORD statePtr)
{
    DWORD addr=g_v662G726;
    if(!addr) return 0;

    V662G726FN fn=(V662G726FN)addr;
    DWORD result=fn(mode,inPtr,outPtr,count,statePtr);

    // Same proven v6.61 replacement point.
    V661InjectStream(outPtr,count);
    return result;
}

extern "C" __declspec(naked) DWORD Hook_V662_Gateway()
{
    __asm {
        mflr r0

        stwu r1,-0xC0(r1)
        stw r0,0x70(r1)

        stw r3,0x20(r1)
        stw r4,0x24(r1)
        stw r5,0x28(r1)
        stw r6,0x2C(r1)
        stw r7,0x30(r1)
        stw r8,0x34(r1)
        stw r9,0x38(r1)
        stw r10,0x3C(r1)
        stw r11,0x40(r1)
        stw r12,0x44(r1)

        mr r3,r0
        bl V662IsOurCaller
        cmpwi cr6,r3,0
        beq cr6,V662_NORMAL_XAM

        // Restore original G726 args and invoke the dynamically located,
        // completely untouched Microsoft codec through a normal C call.
        lwz r3,0x20(r1)
        lwz r4,0x24(r1)
        lwz r5,0x28(r1)
        lwz r6,0x2C(r1)
        lwz r7,0x30(r1)
        bl V662InvokeG726

        lwz r0,0x70(r1)
        mtlr r0
        addi r1,r1,0xC0
        blr

V662_NORMAL_XAM:
        // Restore original XamVoiceSubmitPacket args / LR and preserve the
        // import stub's normal behavior.
        lwz r3,0x20(r1)
        lwz r4,0x24(r1)
        lwz r5,0x28(r1)
        lwz r6,0x2C(r1)
        lwz r7,0x30(r1)
        lwz r8,0x34(r1)
        lwz r9,0x38(r1)
        lwz r10,0x3C(r1)
        lwz r11,0x40(r1)
        lwz r12,0x44(r1)
        lwz r0,0x70(r1)
        mtlr r0
        addi r1,r1,0xC0

        // Real XAM target represented by the import stub signature:
        // 0x816FB498.
        lis r11,0x8170
        addi r11,r11,-0x4B68
        mtctr r11
        bctr
    }
}

static void V662ClearMap()
{
    g_v662G726=0;
    g_v662GatewayStub=0;
    ZeroMemory((void*)g_v662Callsites,sizeof(g_v662Callsites));
    ZeroMemory(g_v662OrigCalls,sizeof(g_v662OrigCalls));
    g_v662CallCount=0;
    ZeroMemory(g_v662OrigStub,sizeof(g_v662OrigStub));
}

static BOOL V662Install(SOCKET s)
{
    if(g_v662Installed){
        SendLine(s,"V662_INSTALL ok=1 already=1 g726=0x%08X gateway=0x%08X calls=%u",
                 g_v662G726,g_v662GatewayStub,g_v662CallCount);
        return TRUE;
    }

    V662ClearMap();

    DWORD g726=V662FindG726(s);
    if(!g726){
        SendLine(s,"V662_INSTALL ok=0 reason=g726_not_found hint=launch_voice_enabled_title_then_rescan");
        return FALSE;
    }
    g_v662G726=g726;

    if(V662FindMode1Calls(s,g726)==0){
        SendLine(s,"V662_INSTALL ok=0 reason=no_mode1_callers");
        V662ClearMap();
        return FALSE;
    }

    DWORD stub=V662FindGatewayStub(s);
    if(!stub){
        SendLine(s,"V662_INSTALL ok=0 reason=no_reachable_submit_stub");
        V662ClearMap();
        return FALSE;
    }
    g_v662GatewayStub=stub;

    DWORD sw0=*(volatile DWORD*)(stub+0);
    DWORD sw1=*(volatile DWORD*)(stub+4);
    DWORD sw2=*(volatile DWORD*)(stub+8);
    DWORD sw3=*(volatile DWORD*)(stub+12);
    g_v662OrigStub[0]=sw0;
    g_v662OrigStub[1]=sw1;
    g_v662OrigStub[2]=sw2;
    g_v662OrigStub[3]=sw3;

    for(DWORD i=0;i<g_v662CallCount;i++){
        g_v662OrigCalls[i]=*(volatile DWORD*)g_v662Callsites[i];
    }

    DWORD hook=(DWORD)&Hook_V662_Gateway;
    DWORD hi=(hook+0x8000)>>16;
    SHORT lo=(SHORT)(hook&0xFFFF);

    DWORD patch[4];
    patch[0]=0x3D600000 | (hi&0xFFFF);
    patch[1]=0x396B0000 | ((WORD)lo);
    patch[2]=0x7D6903A6;
    patch[3]=0x4E800420;

    memcpy((void*)stub,patch,16);
    V645FlushCode(stub,16);

    for(DWORD i=0;i<g_v662CallCount;i++){
        DWORD bl=0;
        if(!V662MakeBL(g_v662Callsites[i],stub,&bl)){
            SendLine(s,"V662_INSTALL ok=0 reason=late_branch_range");
            // Restore anything already changed.
            for(DWORD j=0;j<i;j++){
                *(volatile DWORD*)g_v662Callsites[j]=g_v662OrigCalls[j];
                V645FlushCode(g_v662Callsites[j],4);
            }
            memcpy((void*)stub,g_v662OrigStub,16);
            V645FlushCode(stub,16);
            V662ClearMap();
            return FALSE;
        }

        *(volatile DWORD*)g_v662Callsites[i]=bl;
        V645FlushCode(g_v662Callsites[i],4);
        SendLine(s,
          "V662_PATCH callIndex=%u callsite=0x%08X original=0x%08X patched=0x%08X",
          i,g_v662Callsites[i],g_v662OrigCalls[i],bl);
    }

    V661ResetRing();
    g_v662Installed=TRUE;

    SendLine(s,
      "V662_INSTALL ok=1 g726=0x%08X gateway=0x%08X calls=%u hook=0x%08X",
      g_v662G726,g_v662GatewayStub,g_v662CallCount,hook);
    return TRUE;
}

static void V662Restore(SOCKET s)
{
    g_v661StreamEnabled=0;

    if(!g_v662Installed){
        SendLine(s,"V662_RESTORE already=0");
        return;
    }

    BOOL ok=TRUE;

    // Restore G726 callsites first so nothing new enters our gateway.
    for(DWORD i=0;i<g_v662CallCount;i++){
        DWORD a=g_v662Callsites[i];
        if(a && V662CanRead(a,4)){
            *(volatile DWORD*)a=g_v662OrigCalls[i];
            V645FlushCode(a,4);
        }else{
            ok=FALSE;
        }
    }

    DWORD stub=g_v662GatewayStub;
    if(stub && V662CanRead(stub,16)){
        memcpy((void*)stub,g_v662OrigStub,16);
        V645FlushCode(stub,16);
    }else{
        ok=FALSE;
    }

    g_v662Installed=FALSE;
    SendLine(s,"V662_RESTORE ok=%u",ok?1:0);
    V662ClearMap();
}


#define V663_XAM_START 0x81600000
#define V663_XAM_END   0x81800000

#define V663_XAM_VOICE_CREATE   0x816FC098
#define V663_XAM_VOICE_SUBMIT   0x816FB498
#define V663_XAM_VOICE_CLOSE    0x816FC1C8
#define V663_XAM_VOICE_HEADSET  0x816FB418

#define V663_XVOICED_HEADSET    0x80101C40
#define V663_XVOICED_SUBMIT     0x80102048
#define V663_XVOICED_CLOSE      0x80102230
#define V663_XVOICED_ACTIVATE   0x80101DC8

typedef struct {
    const char* name;
    DWORD addr;
} V663_TARGET;

static const V663_TARGET g_v663Targets[] = {
    {"XVoicedHeadsetPresent", V663_XVOICED_HEADSET},
    {"XVoicedSubmitPacket",   V663_XVOICED_SUBMIT},
    {"XVoicedClose",          V663_XVOICED_CLOSE},
    {"XVoicedActivate",       V663_XVOICED_ACTIVATE}
};

static BOOL V663Readable(DWORD p,DWORD n)
{
    if(!p || !n) return FALSE;
    return MmIsAddressValid(p) && MmIsAddressValid(p+n-1);
}

static void V663DumpWords(SOCKET s,const char* tag,DWORD center,DWORD before,DWORD after)
{
    DWORD start=center-before;
    DWORD end=center+after;

    if(start>end) return;

    for(DWORD a=start;a<=end;a+=16){
        if(!V663Readable(a,16)) continue;

        SendLine(s,
          "V663_WORDS tag=%s addr=0x%08X data=%08X,%08X,%08X,%08X",
          tag,a,
          *(volatile DWORD*)(a+0),
          *(volatile DWORD*)(a+4),
          *(volatile DWORD*)(a+8),
          *(volatile DWORD*)(a+12));
    }
}

static void V663DumpKnownFunctions(SOCKET s)
{
    struct F { const char* name; DWORD addr; DWORD bytes; };
    static const F fs[] = {
      {"XamVoiceCreate",V663_XAM_VOICE_CREATE,0x180},
      {"XamVoiceSubmitPacket",V663_XAM_VOICE_SUBMIT,0x200},
      {"XamVoiceClose",V663_XAM_VOICE_CLOSE,0x100},
      {"XamVoiceHeadsetPresent",V663_XAM_VOICE_HEADSET,0x100},
      {"XVoicedHeadsetPresent",V663_XVOICED_HEADSET,0x100},
      {"XVoicedSubmitPacket",V663_XVOICED_SUBMIT,0x180},
      {"XVoicedClose",V663_XVOICED_CLOSE,0x100},
      {"XVoicedActivate",V663_XVOICED_ACTIVATE,0x180}
    };

    for(DWORD i=0;i<sizeof(fs)/sizeof(fs[0]);i++){
        BOOL ok=V663Readable(fs[i].addr,16);
        SendLine(s,
          "V663_FUNC name=%s addr=0x%08X mapped=%u",
          fs[i].name,fs[i].addr,ok?1:0);

        if(ok){
            DWORD end=fs[i].addr+fs[i].bytes;
            for(DWORD a=fs[i].addr;a<end;a+=16){
                if(!V663Readable(a,16)) break;
                SendLine(s,
                  "V663_FUNC_WORDS name=%s addr=0x%08X data=%08X,%08X,%08X,%08X",
                  fs[i].name,a,
                  *(volatile DWORD*)(a+0),
                  *(volatile DWORD*)(a+4),
                  *(volatile DWORD*)(a+8),
                  *(volatile DWORD*)(a+12));
            }
        }
    }
}

static void V663ScanDirectCalls(SOCKET s)
{
    DWORD totals[sizeof(g_v663Targets)/sizeof(g_v663Targets[0])];
    ZeroMemory(totals,sizeof(totals));

    SendLine(s,
      "V663_CALL_SCAN_START range=0x%08X-0x%08X",
      V663_XAM_START,V663_XAM_END);

    for(DWORD page=V663_XAM_START;page<V663_XAM_END;page+=0x1000){
        if(!MmIsAddressValid(page)) continue;

        DWORD end=page+0x1000;
        for(DWORD a=page;a+4<=end;a+=4){
            DWORD w=*(volatile DWORD*)a;
            DWORD target=0;
            if(!V656DirectCallTarget(a,w,&target)) continue;

            for(DWORD i=0;i<sizeof(g_v663Targets)/sizeof(g_v663Targets[0]);i++){
                if(target!=g_v663Targets[i].addr) continue;

                totals[i]++;
                SendLine(s,
                  "V663_DIRECT_CALL target=%s targetAddr=0x%08X callsite=0x%08X word=0x%08X",
                  g_v663Targets[i].name,g_v663Targets[i].addr,a,w);

                V663DumpWords(s,g_v663Targets[i].name,a,0x50,0x40);
            }
        }
    }

    for(DWORD i=0;i<sizeof(g_v663Targets)/sizeof(g_v663Targets[0]);i++){
        SendLine(s,
          "V663_CALL_TOTAL target=%s targetAddr=0x%08X calls=%u",
          g_v663Targets[i].name,g_v663Targets[i].addr,totals[i]);
    }

    SendLine(s,"V663_CALL_SCAN_DONE");
}

static void V663ScanAbsoluteStubs(SOCKET s)
{
    SendLine(s,
      "V663_STUB_SCAN_START range=0x%08X-0x%08X",
      V663_XAM_START,V663_XAM_END);

    DWORD hits=0;

    for(DWORD page=V663_XAM_START;page<V663_XAM_END;page+=0x1000){
        if(!MmIsAddressValid(page)) continue;
        DWORD end=page+0x1000-16;

        for(DWORD a=page;a<=end;a+=4){
            DWORD w0=*(volatile DWORD*)(a+0);
            DWORD w1=*(volatile DWORD*)(a+4);
            DWORD w2=*(volatile DWORD*)(a+8);
            DWORD w3=*(volatile DWORD*)(a+12);

            // Common Xbox 360 absolute export stub:
            // lis r11,hi16 ; addi r11,r11,lo16 ; mtctr r11 ; bctr
            if((w0&0xFFFF0000)!=0x3D600000) continue;
            if((w1&0xFFFF0000)!=0x396B0000) continue;
            if(w2!=0x7D6903A6 || w3!=0x4E800420) continue;

            DWORD hi=w0&0xFFFF;
            SHORT lo=(SHORT)(w1&0xFFFF);
            DWORD target=(hi<<16)+(LONG)lo;

            for(DWORD i=0;i<sizeof(g_v663Targets)/sizeof(g_v663Targets[0]);i++){
                if(target!=g_v663Targets[i].addr) continue;

                hits++;
                SendLine(s,
                  "V663_ABS_STUB target=%s targetAddr=0x%08X stub=0x%08X words=%08X,%08X,%08X,%08X",
                  g_v663Targets[i].name,target,a,w0,w1,w2,w3);
            }
        }
    }

    SendLine(s,"V663_STUB_SCAN_DONE hits=%u",hits);
}

static void V663SystemMap(SOCKET s)
{
    SendLine(s,"V663_SYSTEM_MAP_START");
    V663DumpKnownFunctions(s);
    V663ScanDirectCalls(s);
    V663ScanAbsoluteStubs(s);
    SendLine(s,"V663_SYSTEM_MAP_DONE");
}


#define V664_TITLE_START 0x82000000
#define V664_TITLE_END   0x90000000
#define V664_XAM_SUBMIT  0x816FB498
#define V664_HELPER_COMMON 0x81A721C4
#define V664_HELPER_HEADSET 0x81A731C4
#define V664_HELPER_SUBMIT 0x81A731E4
#define V664_MAX_CALLS 64

typedef struct {
    DWORD seq;
    DWORD r3;
    DWORD r4;
    DWORD r5;
    DWORD r6;
    DWORD r7;
    DWORD stubLR;
    DWORD r3Bytes;
    BYTE r3Data[0x40];
    DWORD r5Bytes;
    BYTE r5Data[0x40];
    DWORD childPtr;
    DWORD childBytes;
    BYTE childData[0x80];
} V664_CALL;

static volatile LONG g_v664CaptureEnabled=0;
static volatile LONG g_v664CallCount=0;
static BOOL g_v664Installed=FALSE;
static DWORD g_v664Stub=0;
static DWORD g_v664OrigStub[4]={0,0,0,0};
static V664_CALL g_v664Calls[V664_MAX_CALLS];

static BOOL V664Readable(DWORD p,DWORD n)
{
    if(!p || !n) return FALSE;
    return MmIsAddressValid(p) && MmIsAddressValid(p+n-1);
}

static BOOL V664IsSubmitStub(DWORD a)
{
    if(!V664Readable(a,16)) return FALSE;
    return
      *(volatile DWORD*)(a+0x00)==0x3D608170 &&
      *(volatile DWORD*)(a+0x04)==0x396BB498 &&
      *(volatile DWORD*)(a+0x08)==0x7D6903A6 &&
      *(volatile DWORD*)(a+0x0C)==0x4E800420;
}

static DWORD V664FindSubmitStub(SOCKET s)
{
    DWORD hits=0;
    DWORD first=0;

    SendLine(s,
      "V664_STUB_SCAN_START range=0x%08X-0x%08X target=0x%08X",
      V664_TITLE_START,V664_TITLE_END,V664_XAM_SUBMIT);

    for(DWORD page=V664_TITLE_START;page<V664_TITLE_END;page+=0x1000){
        if(!MmIsAddressValid(page)) continue;
        DWORD end=page+0x1000-16;

        for(DWORD a=page;a<=end;a+=4){
            if(*(volatile DWORD*)a!=0x3D608170) continue;
            if(!V664IsSubmitStub(a)) continue;

            hits++;
            if(!first) first=a;
            SendLine(s,"V664_SUBMIT_STUB hit=%u addr=0x%08X",hits,a);
        }
    }

    SendLine(s,"V664_STUB_SCAN_DONE hits=%u selected=0x%08X",hits,first);
    return first;
}

static void V664DumpFunction(SOCKET s,const char* name,DWORD addr,DWORD bytes)
{
    BOOL mapped=V664Readable(addr,16);
    SendLine(s,"V664_HELPER name=%s addr=0x%08X mapped=%u",name,addr,mapped?1:0);
    if(!mapped) return;

    DWORD end=addr+bytes;
    for(DWORD a=addr;a<end;a+=16){
        if(!V664Readable(a,16)) break;
        SendLine(s,
          "V664_HELPER_WORDS name=%s addr=0x%08X data=%08X,%08X,%08X,%08X",
          name,a,
          *(volatile DWORD*)(a+0),
          *(volatile DWORD*)(a+4),
          *(volatile DWORD*)(a+8),
          *(volatile DWORD*)(a+12));
    }
}

static void V664MapInternalXam(SOCKET s)
{
    SendLine(s,"V664_XAM_INTERNAL_MAP_START");
    V664DumpFunction(s,"common_81A721C4",V664_HELPER_COMMON,0x200);
    V664DumpFunction(s,"headset_81A731C4",V664_HELPER_HEADSET,0x100);
    V664DumpFunction(s,"submit_81A731E4",V664_HELPER_SUBMIT,0x240);
    SendLine(s,"V664_XAM_INTERNAL_MAP_DONE");
}

extern "C" void V664Capture(
    DWORD r3,DWORD r4,DWORD r5,DWORD r6,DWORD r7,DWORD lr)
{
    if(!g_v664CaptureEnabled) return;

    LONG ix=g_v664CallCount;
    if(ix>=V664_MAX_CALLS) return;
    ix=InterlockedIncrement(&g_v664CallCount)-1;
    if(ix<0 || ix>=V664_MAX_CALLS) return;

    V664_CALL* p=&g_v664Calls[ix];
    ZeroMemory(p,sizeof(*p));

    p->seq=(DWORD)ix;
    p->r3=r3;
    p->r4=r4;
    p->r5=r5;
    p->r6=r6;
    p->r7=r7;
    p->stubLR=lr;

    if(r3 && V664Readable(r3,0x40)){
        memcpy(p->r3Data,(const void*)r3,0x40);
        p->r3Bytes=0x40;
    }

    if(r5 && V664Readable(r5,0x40)){
        memcpy(p->r5Data,(const void*)r5,0x40);
        p->r5Bytes=0x40;

        // Known Voice Changer descriptor had its payload pointer at +0x08.
        // This is observation only; record it if plausible in COD4 too.
        DWORD child=*(volatile DWORD*)(r5+8);
        p->childPtr=child;
        if(child && V664Readable(child,0x80)){
            memcpy(p->childData,(const void*)child,0x80);
            p->childBytes=0x80;
        }
    }
}

extern "C" __declspec(naked) DWORD Hook_V664_XamSubmitStub()
{
    __asm {
        mflr r0
        stwu r1,-0xA0(r1)
        stw r0,0x60(r1)

        stw r3,0x20(r1)
        stw r4,0x24(r1)
        stw r5,0x28(r1)
        stw r6,0x2C(r1)
        stw r7,0x30(r1)
        stw r8,0x34(r1)
        stw r9,0x38(r1)
        stw r10,0x3C(r1)
        stw r11,0x40(r1)
        stw r12,0x44(r1)

        // V664Capture(r3,r4,r5,r6,r7,lr)
        lwz r8,0x60(r1)
        bl V664Capture

        lwz r3,0x20(r1)
        lwz r4,0x24(r1)
        lwz r5,0x28(r1)
        lwz r6,0x2C(r1)
        lwz r7,0x30(r1)
        lwz r8,0x34(r1)
        lwz r9,0x38(r1)
        lwz r10,0x3C(r1)
        lwz r11,0x40(r1)
        lwz r12,0x44(r1)
        lwz r0,0x60(r1)
        mtlr r0
        addi r1,r1,0xA0

        // Preserve the exact normal title import-stub behavior.
        lis r11,0x8170
        addi r11,r11,-0x4B68
        mtctr r11
        bctr
    }
}

static BOOL V664Install(SOCKET s)
{
    if(g_v664Installed){
        SendLine(s,"V664_INSTALL ok=1 already=1 stub=0x%08X",g_v664Stub);
        return TRUE;
    }

    V664MapInternalXam(s);

    DWORD stub=V664FindSubmitStub(s);
    if(!stub){
        SendLine(s,"V664_INSTALL ok=0 reason=xam_submit_stub_not_found");
        return FALSE;
    }

    g_v664Stub=stub;
    for(DWORD i=0;i<4;i++) g_v664OrigStub[i]=*(volatile DWORD*)(stub+i*4);

    DWORD hook=(DWORD)&Hook_V664_XamSubmitStub;
    DWORD hi=(hook+0x8000)>>16;
    SHORT lo=(SHORT)(hook&0xFFFF);

    DWORD patch[4];
    patch[0]=0x3D600000 | (hi&0xFFFF);
    patch[1]=0x396B0000 | ((WORD)lo);
    patch[2]=0x7D6903A6;
    patch[3]=0x4E800420;

    memcpy((void*)stub,patch,16);
    V645FlushCode(stub,16);

    g_v664CaptureEnabled=0;
    g_v664CallCount=0;
    ZeroMemory(g_v664Calls,sizeof(g_v664Calls));
    g_v664Installed=TRUE;

    SendLine(s,
      "V664_INSTALL ok=1 stub=0x%08X hook=0x%08X realXam=0x%08X",
      stub,hook,V664_XAM_SUBMIT);
    return TRUE;
}

static void V664Start(SOCKET s)
{
    if(!g_v664Installed){
        SendLine(s,"V664_CAPTURE_START ok=0 reason=not_installed");
        return;
    }

    g_v664CaptureEnabled=0;
    g_v664CallCount=0;
    ZeroMemory(g_v664Calls,sizeof(g_v664Calls));
    g_v664CaptureEnabled=1;
    SendLine(s,"V664_CAPTURE_START ok=1 maxCalls=%u",V664_MAX_CALLS);
}

static void V664Hex(SOCKET s,const char* prefix,const BYTE* d,DWORD n)
{
    static const char hx[]="0123456789ABCDEF";
    char line[1200];
    int p=_snprintf(line,sizeof(line)-1,"%s",prefix);
    if(p<0) return;

    for(DWORD i=0;i<n && p+2<(int)sizeof(line)-3;i++){
        BYTE b=d[i];
        line[p++]=hx[b>>4];
        line[p++]=hx[b&15];
    }

    line[p++]='\r';
    line[p++]='\n';

    int sent=0;
    while(sent<p){
        int r=send(s,line+sent,p-sent,0);
        if(r<=0) break;
        sent+=r;
    }
}

static void V664StopDump(SOCKET s)
{
    g_v664CaptureEnabled=0;

    LONG n=g_v664CallCount;
    if(n>V664_MAX_CALLS) n=V664_MAX_CALLS;

    DWORD r4zero=0,r4one=0,other=0;
    for(LONG i=0;i<n;i++){
        if(g_v664Calls[i].r4==0) r4zero++;
        else if(g_v664Calls[i].r4==1) r4one++;
        else other++;
    }

    SendLine(s,
      "V664_CAPTURE_STOP calls=%ld r4zero=%u r4one=%u other=%u",
      n,r4zero,r4one,other);

    for(LONG i=0;i<n;i++){
        V664_CALL* p=&g_v664Calls[i];
        SendLine(s,
          "V664_META seq=%u lr=0x%08X r3=0x%08X r4=0x%08X r5=0x%08X r6=0x%08X r7=0x%08X child=0x%08X",
          p->seq,p->stubLR,p->r3,p->r4,p->r5,p->r6,p->r7,p->childPtr);

        char prefix[120];

        if(p->r3Bytes){
            _snprintf(prefix,sizeof(prefix)-1,"V664_R3_HEX seq=%u data=",p->seq);
            V664Hex(s,prefix,p->r3Data,p->r3Bytes);
        }

        if(p->r5Bytes){
            _snprintf(prefix,sizeof(prefix)-1,"V664_R5_HEX seq=%u data=",p->seq);
            V664Hex(s,prefix,p->r5Data,p->r5Bytes);
        }

        if(p->childBytes){
            _snprintf(prefix,sizeof(prefix)-1,"V664_CHILD_HEX seq=%u data=",p->seq);
            V664Hex(s,prefix,p->childData,p->childBytes);
        }
    }

    SendLine(s,"V664_DUMP_DONE calls=%ld",n);
}

static void V664Restore(SOCKET s)
{
    g_v664CaptureEnabled=0;

    if(!g_v664Installed){
        SendLine(s,"V664_RESTORE already=0");
        return;
    }

    if(g_v664Stub && V664Readable(g_v664Stub,16)){
        memcpy((void*)g_v664Stub,g_v664OrigStub,16);
        V645FlushCode(g_v664Stub,16);
        g_v664Installed=FALSE;
        SendLine(s,"V664_RESTORE ok=1");
    }else{
        SendLine(s,"V664_RESTORE ok=0");
    }
}


#define V665_XVOICED_SUBMIT_STUB 0x81A731E4
#define V665_XVOICED_SUBMIT_REAL 0x80102048
#define V665_PACKET_BYTES 0x280
#define V665_PACKET_SAMPLES 320
#define V665_DESCRIPTOR_SIZE 0x10

// 0 = pass-through, 1 = generated 1 kHz tone, 2 = live LAN PC mic
static volatile LONG g_v665Mode=0;
static volatile LONG g_v665SubmitCalls=0;
static volatile LONG g_v665R4One=0;
static volatile LONG g_v665GeometryMatch=0;
static volatile LONG g_v665ToneBlocks=0;
static volatile LONG g_v665LiveBlocks=0;
static volatile LONG g_v665UnderrunBlocks=0;
static volatile LONG g_v665BadPackets=0;
static BOOL g_v665Installed=FALSE;
static DWORD g_v665OrigStub[4]={0,0,0,0};
static DWORD g_v665TonePhase=0;

static const SHORT g_v665Tone16[16]={
       0, 2296, 4243, 5543,
    6000, 5543, 4243, 2296,
       0,-2296,-4243,-5543,
   -6000,-5543,-4243,-2296
};

static BOOL V665Readable(DWORD p,DWORD n)
{
    if(!p || !n) return FALSE;
    return MmIsAddressValid(p) && MmIsAddressValid(p+n-1);
}

static BOOL V665StubValid()
{
    DWORD a=V665_XVOICED_SUBMIT_STUB;
    if(!V665Readable(a,16)) return FALSE;

    return
      *(volatile DWORD*)(a+0x00)==0x3D608010 &&
      *(volatile DWORD*)(a+0x04)==0x396B2048 &&
      *(volatile DWORD*)(a+0x08)==0x7D6903A6 &&
      *(volatile DWORD*)(a+0x0C)==0x4E800420;
}

static void V665FillTone(DWORD outPtr)
{
    volatile BYTE* out=(volatile BYTE*)outPtr;
    DWORD ph=g_v665TonePhase;

    for(DWORD i=0;i<V665_PACKET_SAMPLES;i++){
        SHORT sample=g_v665Tone16[ph&15];
        WORD u=(WORD)sample;
        out[i*2+0]=(BYTE)(u>>8);
        out[i*2+1]=(BYTE)(u&0xFF);
        ph++;
    }

    g_v665TonePhase=ph&15;
}

static BOOL V665PopLivePair(DWORD outPtr)
{
    LONG w=g_v661WriteSeq;
    LONG r=g_v661ReadSeq;
    LONG depth=w-r;

    // Keep latency bounded if the PC got ahead.
    if(depth>8){
        LONG newR=w-6;
        LONG dropped=newR-r;
        if(dropped>0){
            InterlockedExchange(&g_v661ReadSeq,newR);
            InterlockedExchangeAdd(&g_v661OverrunDrops,dropped);
            r=newR;
            depth=w-r;
        }
    }

    volatile BYTE* out=(volatile BYTE*)outPtr;

    // One XVoiced r4=1 packet is 20 ms / 320 samples.
    // The PC wire format remains the proven 10 ms / 160-sample frames,
    // so consume exactly two frames per system packet.
    if(depth<2){
        memset((void*)out,0,V665_PACKET_BYTES);
        InterlockedIncrement(&g_v665UnderrunBlocks);
        return FALSE;
    }

    DWORD slot0=(DWORD)r & (V661_RING_FRAMES-1);
    DWORD slot1=(DWORD)(r+1) & (V661_RING_FRAMES-1);

    memcpy((void*)out,g_v661Ring[slot0],V661_FRAME_BYTES);
    memcpy((void*)(out+V661_FRAME_BYTES),g_v661Ring[slot1],V661_FRAME_BYTES);

    InterlockedExchange(&g_v661ReadSeq,r+2);
    InterlockedExchangeAdd(&g_v661Consumed,2);
    return TRUE;
}

extern "C" void V665Process(
    DWORD r3,DWORD r4,DWORD r5,DWORD r6,DWORD r7)
{
    (void)r3;
    (void)r6;
    (void)r7;

    InterlockedIncrement(&g_v665SubmitCalls);

    if(r4!=1) return;
    InterlockedIncrement(&g_v665R4One);

    if(!r5 || !V665Readable(r5,V665_DESCRIPTOR_SIZE)){
        InterlockedIncrement(&g_v665BadPackets);
        return;
    }

    DWORD packetBytes=*(volatile DWORD*)(r5+0x04);
    DWORD pcmPtr=*(volatile DWORD*)(r5+0x08);
    DWORD field0C=*(volatile DWORD*)(r5+0x0C);

    // Real-hardware geometry independently seen in Voice Changer and COD4:
    //   +04 = 0x280 bytes
    //   +08 = 20 ms PCM16-BE buffer
    //   +0C = 0xA0
    if(packetBytes!=V665_PACKET_BYTES ||
       field0C!=0xA0 ||
       !pcmPtr ||
       !V665Readable(pcmPtr,V665_PACKET_BYTES)){
        InterlockedIncrement(&g_v665BadPackets);
        return;
    }

    InterlockedIncrement(&g_v665GeometryMatch);

    LONG mode=g_v665Mode;
    if(mode==1){
        V665FillTone(pcmPtr);
        InterlockedIncrement(&g_v665ToneBlocks);
    }else if(mode==2){
        if(V665PopLivePair(pcmPtr)){
            InterlockedIncrement(&g_v665LiveBlocks);
        }
    }
}

extern "C" __declspec(naked) DWORD Hook_V665_XVoicedSubmitStub()
{
    __asm {
        mflr r0
        stwu r1,-0xA0(r1)
        stw r0,0x60(r1)

        stw r3,0x20(r1)
        stw r4,0x24(r1)
        stw r5,0x28(r1)
        stw r6,0x2C(r1)
        stw r7,0x30(r1)
        stw r8,0x34(r1)
        stw r9,0x38(r1)
        stw r10,0x3C(r1)
        stw r11,0x40(r1)
        stw r12,0x44(r1)

        bl V665Process

        lwz r3,0x20(r1)
        lwz r4,0x24(r1)
        lwz r5,0x28(r1)
        lwz r6,0x2C(r1)
        lwz r7,0x30(r1)
        lwz r8,0x34(r1)
        lwz r9,0x38(r1)
        lwz r10,0x3C(r1)
        lwz r11,0x40(r1)
        lwz r12,0x44(r1)

        lwz r0,0x60(r1)
        mtlr r0
        addi r1,r1,0xA0

        // Preserve the system stub's exact semantic target.
        lis r11,0x8010
        addi r11,r11,0x2048
        mtctr r11
        bctr
    }
}

static void V665ResetCounters()
{
    g_v665Mode=0;
    g_v665SubmitCalls=0;
    g_v665R4One=0;
    g_v665GeometryMatch=0;
    g_v665ToneBlocks=0;
    g_v665LiveBlocks=0;
    g_v665UnderrunBlocks=0;
    g_v665BadPackets=0;
    g_v665TonePhase=0;
    V661ResetRing();
}

static BOOL V665Install(SOCKET s)
{
    if(g_v665Installed){
        SendLine(s,
          "V665_INSTALL ok=1 already=1 stub=0x%08X real=0x%08X",
          V665_XVOICED_SUBMIT_STUB,V665_XVOICED_SUBMIT_REAL);
        return TRUE;
    }

    if(!V665StubValid()){
        if(V665Readable(V665_XVOICED_SUBMIT_STUB,16)){
            SendLine(s,
              "V665_INSTALL ok=0 reason=stub_signature_mismatch words=%08X,%08X,%08X,%08X",
              *(volatile DWORD*)(V665_XVOICED_SUBMIT_STUB+0),
              *(volatile DWORD*)(V665_XVOICED_SUBMIT_STUB+4),
              *(volatile DWORD*)(V665_XVOICED_SUBMIT_STUB+8),
              *(volatile DWORD*)(V665_XVOICED_SUBMIT_STUB+12));
        }else{
            SendLine(s,"V665_INSTALL ok=0 reason=stub_unmapped");
        }
        return FALSE;
    }

    for(DWORD i=0;i<4;i++){
        g_v665OrigStub[i]=*(volatile DWORD*)(V665_XVOICED_SUBMIT_STUB+i*4);
    }

    DWORD hook=(DWORD)&Hook_V665_XVoicedSubmitStub;
    DWORD hi=(hook+0x8000)>>16;
    SHORT lo=(SHORT)(hook&0xFFFF);

    DWORD patch[4];
    patch[0]=0x3D600000 | (hi&0xFFFF);
    patch[1]=0x396B0000 | ((WORD)lo);
    patch[2]=0x7D6903A6;
    patch[3]=0x4E800420;

    memcpy((void*)V665_XVOICED_SUBMIT_STUB,patch,16);
    V645FlushCode(V665_XVOICED_SUBMIT_STUB,16);

    V665ResetCounters();
    g_v665Installed=TRUE;

    SendLine(s,
      "V665_INSTALL ok=1 SYSTEM_LEVEL=1 stub=0x%08X hook=0x%08X real=0x%08X expectedPacket=0x280 expectedField0C=0xA0",
      V665_XVOICED_SUBMIT_STUB,hook,V665_XVOICED_SUBMIT_REAL);
    return TRUE;
}

static void V665ToneStart(SOCKET s)
{
    if(!g_v665Installed){
        SendLine(s,"V665_TONE_START ok=0 reason=not_installed");
        return;
    }

    g_v665Mode=0;
    g_v665TonePhase=0;
    g_v665ToneBlocks=0;
    g_v665Mode=1;

    SendLine(s,
      "V665_TONE_START ok=1 toneHz=1000 sampleRate=16000 packetSamples=320 packetBytes=0x280 amplitude=6000");
}

static void V665LiveStart(SOCKET s)
{
    if(!g_v665Installed){
        SendLine(s,"V665_LIVE_START ok=0 reason=not_installed");
        return;
    }

    g_v665Mode=0;
    g_v665LiveBlocks=0;
    g_v665UnderrunBlocks=0;

    SendLine(s,
      "V665_LIVE_PREP depth=%ld rxFrames=%ld",
      V661Depth(),g_v661RxFrames);

    g_v665Mode=2;

    SendLine(s,
      "V665_LIVE_START ok=1 sampleRate=16000 systemPacketSamples=320 pcFrameSamples=160");
}

static void V665Stop(SOCKET s)
{
    LONG oldMode=g_v665Mode;
    g_v665Mode=0;

    SendLine(s,
      "V665_STOP oldMode=%ld submit=%ld r4one=%ld geometry=%ld toneBlocks=%ld liveBlocks=%ld underrunBlocks=%ld rxFrames=%ld consumedFrames=%ld overrunDrops=%ld depth=%ld badPackets=%ld",
      oldMode,
      g_v665SubmitCalls,
      g_v665R4One,
      g_v665GeometryMatch,
      g_v665ToneBlocks,
      g_v665LiveBlocks,
      g_v665UnderrunBlocks,
      g_v661RxFrames,
      g_v661Consumed,
      g_v661OverrunDrops,
      V661Depth(),
      g_v665BadPackets);
}

static void V665Restore(SOCKET s)
{
    g_v665Mode=0;

    if(!g_v665Installed){
        SendLine(s,"V665_RESTORE already=0");
        return;
    }

    if(V665Readable(V665_XVOICED_SUBMIT_STUB,16)){
        memcpy((void*)V665_XVOICED_SUBMIT_STUB,g_v665OrigStub,16);
        V645FlushCode(V665_XVOICED_SUBMIT_STUB,16);
        g_v665Installed=FALSE;
        SendLine(s,"V665_RESTORE ok=1");
    }else{
        SendLine(s,"V665_RESTORE ok=0 reason=stub_unmapped");
    }
}


#define V666_STUB 0x81A731E4
#define V666_REAL 0x80102048
#define V666_PACKET_BYTES 0x280
#define V666_PACKET_SAMPLES 320

static volatile LONG g_v666Capture=0;
static volatile LONG g_v666Calls=0;
static volatile LONG g_v666R4One=0;
static volatile LONG g_v666Geometry=0;
static volatile LONG g_v666Blocks=0;
static volatile LONG g_v666AvgAbsSum=0;
static volatile LONG g_v666AvgDeltaSum=0;
static volatile LONG g_v666Peak=0;
static volatile LONG g_v666MinAvgAbs=0x7FFFFFFF;
static volatile LONG g_v666MaxAvgAbs=0;
static volatile LONG g_v666MinAvgDelta=0x7FFFFFFF;
static volatile LONG g_v666MaxAvgDelta=0;
static volatile LONG g_v666HashXor=0;
static BOOL g_v666Installed=FALSE;
static DWORD g_v666OrigStub[4]={0,0,0,0};

static BOOL V666Readable(DWORD p,DWORD n)
{
    if(!p || !n) return FALSE;
    return MmIsAddressValid(p) && MmIsAddressValid(p+n-1);
}

static BOOL V666StubValid()
{
    if(!V666Readable(V666_STUB,16)) return FALSE;
    return
      *(volatile DWORD*)(V666_STUB+0x00)==0x3D608010 &&
      *(volatile DWORD*)(V666_STUB+0x04)==0x396B2048 &&
      *(volatile DWORD*)(V666_STUB+0x08)==0x7D6903A6 &&
      *(volatile DWORD*)(V666_STUB+0x0C)==0x4E800420;
}

static LONG V666Abs16(SHORT v)
{
    LONG x=(LONG)v;
    if(x<0) x=-x;
    return x;
}

static SHORT V666LoadBE16(const volatile BYTE* p)
{
    WORD u=(WORD)(((WORD)p[0]<<8)|(WORD)p[1]);
    return (SHORT)u;
}

static void V666AtomicMax(volatile LONG* dst,LONG value)
{
    for(;;){
        LONG old=*dst;
        if(value<=old) return;
        if(InterlockedCompareExchange(dst,value,old)==old) return;
    }
}

static void V666AtomicMin(volatile LONG* dst,LONG value)
{
    for(;;){
        LONG old=*dst;
        if(value>=old) return;
        if(InterlockedCompareExchange(dst,value,old)==old) return;
    }
}

static void V666ResetMetrics()
{
    g_v666Calls=0;
    g_v666R4One=0;
    g_v666Geometry=0;
    g_v666Blocks=0;
    g_v666AvgAbsSum=0;
    g_v666AvgDeltaSum=0;
    g_v666Peak=0;
    g_v666MinAvgAbs=0x7FFFFFFF;
    g_v666MaxAvgAbs=0;
    g_v666MinAvgDelta=0x7FFFFFFF;
    g_v666MaxAvgDelta=0;
    g_v666HashXor=0;
}

extern "C" void V666Observe(DWORD r3,DWORD r4,DWORD r5,DWORD r6,DWORD r7)
{
    (void)r3;
    (void)r6;
    (void)r7;

    if(!g_v666Capture) return;
    InterlockedIncrement(&g_v666Calls);

    if(r4!=1) return;
    InterlockedIncrement(&g_v666R4One);

    if(!r5 || !V666Readable(r5,16)) return;

    DWORD bytes=*(volatile DWORD*)(r5+0x04);
    DWORD pcm=*(volatile DWORD*)(r5+0x08);
    DWORD f0c=*(volatile DWORD*)(r5+0x0C);

    if(bytes!=V666_PACKET_BYTES ||
       f0c!=0xA0 ||
       !pcm ||
       !V666Readable(pcm,V666_PACKET_BYTES)){
        return;
    }

    InterlockedIncrement(&g_v666Geometry);

    const volatile BYTE* b=(const volatile BYTE*)pcm;
    LONG absSum=0;
    LONG deltaSum=0;
    LONG peak=0;
    DWORD hash=2166136261u;

    SHORT prev=V666LoadBE16(b);
    for(DWORD i=0;i<V666_PACKET_SAMPLES;i++){
        SHORT s=V666LoadBE16(b+i*2);
        LONG a=V666Abs16(s);
        absSum+=a;
        if(a>peak) peak=a;

        if(i){
            LONG d=(LONG)s-(LONG)prev;
            if(d<0) d=-d;
            deltaSum+=d;
        }
        prev=s;

        hash^=(DWORD)(WORD)s;
        hash*=16777619u;
    }

    LONG avgAbs=absSum/(LONG)V666_PACKET_SAMPLES;
    LONG avgDelta=deltaSum/(LONG)(V666_PACKET_SAMPLES-1);

    InterlockedIncrement(&g_v666Blocks);
    InterlockedExchangeAdd(&g_v666AvgAbsSum,avgAbs);
    InterlockedExchangeAdd(&g_v666AvgDeltaSum,avgDelta);
    V666AtomicMax(&g_v666Peak,peak);
    V666AtomicMin(&g_v666MinAvgAbs,avgAbs);
    V666AtomicMax(&g_v666MaxAvgAbs,avgAbs);
    V666AtomicMin(&g_v666MinAvgDelta,avgDelta);
    V666AtomicMax(&g_v666MaxAvgDelta,avgDelta);
    InterlockedXor(&g_v666HashXor,(LONG)hash);
}

extern "C" __declspec(naked) DWORD Hook_V666_SystemSubmit()
{
    __asm {
        mflr r0
        stwu r1,-0xA0(r1)
        stw r0,0x60(r1)

        stw r3,0x20(r1)
        stw r4,0x24(r1)
        stw r5,0x28(r1)
        stw r6,0x2C(r1)
        stw r7,0x30(r1)
        stw r8,0x34(r1)
        stw r9,0x38(r1)
        stw r10,0x3C(r1)
        stw r11,0x40(r1)
        stw r12,0x44(r1)

        bl V666Observe

        lwz r3,0x20(r1)
        lwz r4,0x24(r1)
        lwz r5,0x28(r1)
        lwz r6,0x2C(r1)
        lwz r7,0x30(r1)
        lwz r8,0x34(r1)
        lwz r9,0x38(r1)
        lwz r10,0x3C(r1)
        lwz r11,0x40(r1)
        lwz r12,0x44(r1)

        lwz r0,0x60(r1)
        mtlr r0
        addi r1,r1,0xA0

        lis r11,0x8010
        addi r11,r11,0x2048
        mtctr r11
        bctr
    }
}

static BOOL V666Install(SOCKET s)
{
    if(g_v666Installed){
        SendLine(s,"V666_INSTALL ok=1 already=1");
        return TRUE;
    }

    if(!V666StubValid()){
        SendLine(s,"V666_INSTALL ok=0 reason=system_stub_invalid");
        return FALSE;
    }

    for(DWORD i=0;i<4;i++){
        g_v666OrigStub[i]=*(volatile DWORD*)(V666_STUB+i*4);
    }

    DWORD hook=(DWORD)&Hook_V666_SystemSubmit;
    DWORD hi=(hook+0x8000)>>16;
    SHORT lo=(SHORT)(hook&0xFFFF);

    DWORD patch[4];
    patch[0]=0x3D600000|(hi&0xFFFF);
    patch[1]=0x396B0000|((WORD)lo);
    patch[2]=0x7D6903A6;
    patch[3]=0x4E800420;

    memcpy((void*)V666_STUB,patch,16);
    V645FlushCode(V666_STUB,16);

    V666ResetMetrics();
    g_v666Capture=0;
    g_v666Installed=TRUE;

    SendLine(s,
      "V666_INSTALL ok=1 OBSERVE_ONLY=1 stub=0x%08X real=0x%08X hook=0x%08X",
      V666_STUB,V666_REAL,hook);
    return TRUE;
}

static void V666Start(SOCKET s,DWORD phase)
{
    if(!g_v666Installed){
        SendLine(s,"V666_PHASE_START ok=0 reason=not_installed");
        return;
    }

    g_v666Capture=0;
    V666ResetMetrics();
    g_v666Capture=1;
    SendLine(s,"V666_PHASE_START ok=1 phase=%u",phase);
}

static void V666Stop(SOCKET s,DWORD phase)
{
    g_v666Capture=0;

    LONG blocks=g_v666Blocks;
    LONG avgAbs=(blocks>0)?(g_v666AvgAbsSum/blocks):0;
    LONG avgDelta=(blocks>0)?(g_v666AvgDeltaSum/blocks):0;
    LONG minAbs=(blocks>0)?g_v666MinAvgAbs:0;
    LONG minDelta=(blocks>0)?g_v666MinAvgDelta:0;

    SendLine(s,
      "V666_PHASE_RESULT phase=%u calls=%ld r4one=%ld geometry=%ld blocks=%ld avgAbs=%ld minAbs=%ld maxAbs=%ld avgDelta=%ld minDelta=%ld maxDelta=%ld peak=%ld hash=0x%08X",
      phase,
      g_v666Calls,
      g_v666R4One,
      g_v666Geometry,
      blocks,
      avgAbs,
      minAbs,
      g_v666MaxAvgAbs,
      avgDelta,
      minDelta,
      g_v666MaxAvgDelta,
      g_v666Peak,
      (DWORD)g_v666HashXor);
}

static void V666Restore(SOCKET s)
{
    g_v666Capture=0;

    if(!g_v666Installed){
        SendLine(s,"V666_RESTORE already=0");
        return;
    }

    if(V666Readable(V666_STUB,16)){
        memcpy((void*)V666_STUB,g_v666OrigStub,16);
        V645FlushCode(V666_STUB,16);
        g_v666Installed=FALSE;
        SendLine(s,"V666_RESTORE ok=1");
    }else{
        SendLine(s,"V666_RESTORE ok=0");
    }
}


#define V667_STUB 0x81A731E4
#define V667_REAL 0x80102048
#define V667_PACKET_BYTES 0x280
#define V667_PACKET_SAMPLES 320
#define V667_MAX_BLOCKS 192

typedef struct {
    WORD avgAbs;
    WORD avgDelta;
    WORD peak;
    WORD clipCount;
} V667_METRIC;

static volatile LONG g_v667Capture=0;
static volatile LONG g_v667Calls=0;
static volatile LONG g_v667R4One=0;
static volatile LONG g_v667Geometry=0;
static volatile LONG g_v667BlockCount=0;
static BOOL g_v667Installed=FALSE;
static DWORD g_v667OrigStub[4]={0,0,0,0};
static V667_METRIC g_v667Metrics[V667_MAX_BLOCKS];

static BOOL V667Readable(DWORD p,DWORD n)
{
    if(!p || !n) return FALSE;
    return MmIsAddressValid(p) && MmIsAddressValid(p+n-1);
}

static BOOL V667StubValid()
{
    if(!V667Readable(V667_STUB,16)) return FALSE;
    return
      *(volatile DWORD*)(V667_STUB+0x00)==0x3D608010 &&
      *(volatile DWORD*)(V667_STUB+0x04)==0x396B2048 &&
      *(volatile DWORD*)(V667_STUB+0x08)==0x7D6903A6 &&
      *(volatile DWORD*)(V667_STUB+0x0C)==0x4E800420;
}

static LONG V667Abs16(SHORT v)
{
    LONG x=(LONG)v;
    if(x<0) x=-x;
    return x;
}

static SHORT V667LoadBE16(const volatile BYTE* p)
{
    WORD u=(WORD)(((WORD)p[0]<<8)|(WORD)p[1]);
    return (SHORT)u;
}

static void V667Reset()
{
    g_v667Calls=0;
    g_v667R4One=0;
    g_v667Geometry=0;
    g_v667BlockCount=0;
    ZeroMemory(g_v667Metrics,sizeof(g_v667Metrics));
}

extern "C" void V667Observe(DWORD r3,DWORD r4,DWORD r5,DWORD r6,DWORD r7)
{
    (void)r3;
    (void)r6;
    (void)r7;

    if(!g_v667Capture) return;
    InterlockedIncrement(&g_v667Calls);

    if(r4!=1) return;
    InterlockedIncrement(&g_v667R4One);

    if(!r5 || !V667Readable(r5,16)) return;

    DWORD bytes=*(volatile DWORD*)(r5+0x04);
    DWORD pcm=*(volatile DWORD*)(r5+0x08);
    DWORD f0c=*(volatile DWORD*)(r5+0x0C);

    if(bytes!=V667_PACKET_BYTES ||
       f0c!=0xA0 ||
       !pcm ||
       !V667Readable(pcm,V667_PACKET_BYTES)){
        return;
    }

    InterlockedIncrement(&g_v667Geometry);

    LONG ix=g_v667BlockCount;
    if(ix>=V667_MAX_BLOCKS) return;
    ix=InterlockedIncrement(&g_v667BlockCount)-1;
    if(ix<0 || ix>=V667_MAX_BLOCKS) return;

    const volatile BYTE* b=(const volatile BYTE*)pcm;
    LONG absSum=0;
    LONG deltaSum=0;
    LONG peak=0;
    LONG clipCount=0;

    SHORT prev=V667LoadBE16(b);
    for(DWORD i=0;i<V667_PACKET_SAMPLES;i++){
        SHORT s=V667LoadBE16(b+i*2);
        LONG a=V667Abs16(s);
        absSum+=a;
        if(a>peak) peak=a;
        if(a>=30000) clipCount++;

        if(i){
            LONG d=(LONG)s-(LONG)prev;
            if(d<0) d=-d;
            deltaSum+=d;
        }
        prev=s;
    }

    LONG avgAbs=absSum/(LONG)V667_PACKET_SAMPLES;
    LONG avgDelta=deltaSum/(LONG)(V667_PACKET_SAMPLES-1);

    if(avgAbs>65535) avgAbs=65535;
    if(avgDelta>65535) avgDelta=65535;
    if(peak>65535) peak=65535;
    if(clipCount>65535) clipCount=65535;

    g_v667Metrics[ix].avgAbs=(WORD)avgAbs;
    g_v667Metrics[ix].avgDelta=(WORD)avgDelta;
    g_v667Metrics[ix].peak=(WORD)peak;
    g_v667Metrics[ix].clipCount=(WORD)clipCount;
}

extern "C" __declspec(naked) DWORD Hook_V667_SystemSubmit()
{
    __asm {
        mflr r0
        stwu r1,-0xA0(r1)
        stw r0,0x60(r1)

        stw r3,0x20(r1)
        stw r4,0x24(r1)
        stw r5,0x28(r1)
        stw r6,0x2C(r1)
        stw r7,0x30(r1)
        stw r8,0x34(r1)
        stw r9,0x38(r1)
        stw r10,0x3C(r1)
        stw r11,0x40(r1)
        stw r12,0x44(r1)

        bl V667Observe

        lwz r3,0x20(r1)
        lwz r4,0x24(r1)
        lwz r5,0x28(r1)
        lwz r6,0x2C(r1)
        lwz r7,0x30(r1)
        lwz r8,0x34(r1)
        lwz r9,0x38(r1)
        lwz r10,0x3C(r1)
        lwz r11,0x40(r1)
        lwz r12,0x44(r1)

        lwz r0,0x60(r1)
        mtlr r0
        addi r1,r1,0xA0

        lis r11,0x8010
        addi r11,r11,0x2048
        mtctr r11
        bctr
    }
}

static BOOL V667Install(SOCKET s)
{
    if(g_v667Installed){
        SendLine(s,"V667_INSTALL ok=1 already=1");
        return TRUE;
    }

    if(!V667StubValid()){
        SendLine(s,"V667_INSTALL ok=0 reason=system_stub_invalid");
        return FALSE;
    }

    for(DWORD i=0;i<4;i++){
        g_v667OrigStub[i]=*(volatile DWORD*)(V667_STUB+i*4);
    }

    DWORD hook=(DWORD)&Hook_V667_SystemSubmit;
    DWORD hi=(hook+0x8000)>>16;
    SHORT lo=(SHORT)(hook&0xFFFF);

    DWORD patch[4];
    patch[0]=0x3D600000|(hi&0xFFFF);
    patch[1]=0x396B0000|((WORD)lo);
    patch[2]=0x7D6903A6;
    patch[3]=0x4E800420;

    memcpy((void*)V667_STUB,patch,16);
    V645FlushCode(V667_STUB,16);

    V667Reset();
    g_v667Capture=0;
    g_v667Installed=TRUE;

    SendLine(s,
      "V667_INSTALL ok=1 OBSERVE_ONLY=1 stub=0x%08X real=0x%08X hook=0x%08X",
      V667_STUB,V667_REAL,hook);
    return TRUE;
}

static void V667Start(SOCKET s,DWORD phase)
{
    if(!g_v667Installed){
        SendLine(s,"V667_PHASE_START ok=0 reason=not_installed");
        return;
    }

    g_v667Capture=0;
    V667Reset();
    g_v667Capture=1;
    SendLine(s,"V667_PHASE_START ok=1 phase=%u maxBlocks=%u",phase,V667_MAX_BLOCKS);
}

static void V667StopDump(SOCKET s,DWORD phase)
{
    g_v667Capture=0;

    LONG n=g_v667BlockCount;
    if(n>V667_MAX_BLOCKS) n=V667_MAX_BLOCKS;

    SendLine(s,
      "V667_PHASE_HEADER phase=%u calls=%ld r4one=%ld geometry=%ld blocks=%ld",
      phase,g_v667Calls,g_v667R4One,g_v667Geometry,n);

    for(LONG i=0;i<n;i++){
        V667_METRIC* m=&g_v667Metrics[i];
        SendLine(s,
          "V667_BLOCK phase=%u index=%ld avgAbs=%u avgDelta=%u peak=%u clip=%u",
          phase,i,(DWORD)m->avgAbs,(DWORD)m->avgDelta,(DWORD)m->peak,(DWORD)m->clipCount);
    }

    SendLine(s,"V667_PHASE_DONE phase=%u blocks=%ld",phase,n);
}

static void V667Restore(SOCKET s)
{
    g_v667Capture=0;

    if(!g_v667Installed){
        SendLine(s,"V667_RESTORE already=0");
        return;
    }

    if(V667Readable(V667_STUB,16)){
        memcpy((void*)V667_STUB,g_v667OrigStub,16);
        V645FlushCode(V667_STUB,16);
        g_v667Installed=FALSE;
        SendLine(s,"V667_RESTORE ok=1");
    }else{
        SendLine(s,"V667_RESTORE ok=0");
    }
}


#define V668_STUB 0x81A731E4
#define V668_REAL 0x80102048
#define V668_PACKET_BYTES 0x280
#define V668_MAX_SAMPLES 32

typedef DWORD (*V668RealFn)(DWORD,DWORD,DWORD,DWORD,DWORD);

typedef struct {
    volatile LONG active;
    DWORD seq;
    DWORD pcm;
    DWORD desc;
    DWORD startTick;
    DWORD hPre;
    DWORD hPost;
    DWORD h1;
    DWORD h3;
    DWORD h8;
    DWORD h15;
    DWORD sampledMask;
} V668_SAMPLE;

static BOOL g_v668Installed=FALSE;
static DWORD g_v668OrigStub[4]={0,0,0,0};
static volatile LONG g_v668CaptureEnabled=0;
static volatile LONG g_v668Seq=0;
static volatile LONG g_v668Accepted=0;
static volatile LONG g_v668Rejected=0;
static V668_SAMPLE g_v668Samples[V668_MAX_SAMPLES];

static BOOL V668Readable(DWORD p,DWORD n)
{
    if(!p || !n) return FALSE;
    return MmIsAddressValid(p) && MmIsAddressValid(p+n-1);
}

static BOOL V668StubValid()
{
    if(!V668Readable(V668_STUB,16)) return FALSE;
    return
      *(volatile DWORD*)(V668_STUB+0x00)==0x3D608010 &&
      *(volatile DWORD*)(V668_STUB+0x04)==0x396B2048 &&
      *(volatile DWORD*)(V668_STUB+0x08)==0x7D6903A6 &&
      *(volatile DWORD*)(V668_STUB+0x0C)==0x4E800420;
}

static DWORD V668Hash(DWORD p,DWORD n)
{
    if(!V668Readable(p,n)) return 0;
    const volatile BYTE* b=(const volatile BYTE*)p;
    DWORD h=2166136261u;
    for(DWORD i=0;i<n;i++){
        h^=(DWORD)b[i];
        h*=16777619u;
    }
    return h;
}

static void V668Reset()
{
    g_v668CaptureEnabled=0;
    g_v668Seq=0;
    g_v668Accepted=0;
    g_v668Rejected=0;
    ZeroMemory(g_v668Samples,sizeof(g_v668Samples));
}

static void V668QueueSample(DWORD desc,DWORD pcm,DWORD hPre,DWORD hPost)
{
    LONG seq=InterlockedIncrement(&g_v668Seq)-1;
    if(seq<0 || seq>=V668_MAX_SAMPLES) return;

    V668_SAMPLE* e=&g_v668Samples[seq];
    ZeroMemory(e,sizeof(*e));
    e->seq=(DWORD)seq;
    e->pcm=pcm;
    e->desc=desc;
    e->startTick=GetTickCount();
    e->hPre=hPre;
    e->hPost=hPost;
    e->sampledMask=0;
    MemoryBarrier();
    e->active=1;
    InterlockedIncrement(&g_v668Accepted);
}

extern "C" DWORD V668CallRealAndObserve(
    DWORD r3,DWORD r4,DWORD r5,DWORD r6,DWORD r7)
{
    BOOL track=FALSE;
    DWORD pcm=0;
    DWORD hPre=0;

    if(g_v668CaptureEnabled &&
       g_v668Seq<V668_MAX_SAMPLES &&
       r4==1 &&
       r5 &&
       V668Readable(r5,16)){
        DWORD bytes=*(volatile DWORD*)(r5+0x04);
        pcm=*(volatile DWORD*)(r5+0x08);
        DWORD f0c=*(volatile DWORD*)(r5+0x0C);

        if(bytes==V668_PACKET_BYTES &&
           f0c==0xA0 &&
           pcm &&
           V668Readable(pcm,V668_PACKET_BYTES)){
            hPre=V668Hash(pcm,V668_PACKET_BYTES);
            track=TRUE;
        }else{
            InterlockedIncrement(&g_v668Rejected);
        }
    }

    V668RealFn fn=(V668RealFn)V668_REAL;
    DWORD result=fn(r3,r4,r5,r6,r7);

    if(track && g_v668CaptureEnabled && g_v668Seq<V668_MAX_SAMPLES){
        DWORD hPost=V668Hash(pcm,V668_PACKET_BYTES);
        V668QueueSample(r5,pcm,hPre,hPost);
    }

    return result;
}

extern "C" __declspec(naked) DWORD Hook_V668_SystemSubmit()
{
    __asm {
        mflr r0
        stwu r1,-0x80(r1)
        stw r0,0x60(r1)

        // r3-r7 are already the exact XVoicedSubmitPacket arguments.
        // Call the untouched kernel function inside the C wrapper, then return
        // its real r3 result to XAM/title.
        bl V668CallRealAndObserve

        lwz r0,0x60(r1)
        mtlr r0
        addi r1,r1,0x80
        blr
    }
}

static BOOL V668Install(SOCKET s)
{
    if(g_v668Installed){
        SendLine(s,"V668_INSTALL ok=1 already=1");
        return TRUE;
    }

    if(!V668StubValid()){
        SendLine(s,"V668_INSTALL ok=0 reason=system_stub_invalid");
        return FALSE;
    }

    for(DWORD i=0;i<4;i++){
        g_v668OrigStub[i]=*(volatile DWORD*)(V668_STUB+i*4);
    }

    DWORD hook=(DWORD)&Hook_V668_SystemSubmit;
    DWORD hi=(hook+0x8000)>>16;
    SHORT lo=(SHORT)(hook&0xFFFF);

    DWORD patch[4];
    patch[0]=0x3D600000|(hi&0xFFFF);
    patch[1]=0x396B0000|((WORD)lo);
    patch[2]=0x7D6903A6;
    patch[3]=0x4E800420;

    memcpy((void*)V668_STUB,patch,16);
    V645FlushCode(V668_STUB,16);

    V668Reset();
    g_v668Installed=TRUE;

    SendLine(s,
      "V668_INSTALL ok=1 OBSERVE_ONLY=1 stub=0x%08X real=0x%08X hook=0x%08X",
      V668_STUB,V668_REAL,hook);
    return TRUE;
}

static void V668Start(SOCKET s)
{
    if(!g_v668Installed){
        SendLine(s,"V668_CAPTURE_START ok=0 reason=not_installed");
        return;
    }

    V668Reset();
    g_v668CaptureEnabled=1;

    SendLine(s,
      "V668_CAPTURE_START ok=1 maxSamples=%u delaysMs=1,3,8,15",
      V668_MAX_SAMPLES);
}

static void V668PollSamples(SOCKET s)
{
    if(!g_v668CaptureEnabled && g_v668Accepted==0) return;

    DWORD now=GetTickCount();

    for(DWORD i=0;i<V668_MAX_SAMPLES;i++){
        V668_SAMPLE* e=&g_v668Samples[i];
        if(!e->active) continue;

        DWORD age=now-e->startTick;
        DWORD mask=e->sampledMask;

        if(!(mask&1) && age>=1){
            e->h1=V668Hash(e->pcm,V668_PACKET_BYTES);
            mask|=1;
        }
        if(!(mask&2) && age>=3){
            e->h3=V668Hash(e->pcm,V668_PACKET_BYTES);
            mask|=2;
        }
        if(!(mask&4) && age>=8){
            e->h8=V668Hash(e->pcm,V668_PACKET_BYTES);
            mask|=4;
        }
        if(!(mask&8) && age>=15){
            e->h15=V668Hash(e->pcm,V668_PACKET_BYTES);
            mask|=8;
        }

        e->sampledMask=mask;

        if(mask==0xF){
            SendLine(s,
              "V668_SAMPLE seq=%u desc=0x%08X pcm=0x%08X pre=0x%08X post=0x%08X h1=0x%08X h3=0x%08X h8=0x%08X h15=0x%08X prePostChanged=%u post1Changed=%u post3Changed=%u post8Changed=%u post15Changed=%u",
              e->seq,e->desc,e->pcm,
              e->hPre,e->hPost,e->h1,e->h3,e->h8,e->h15,
              (e->hPre!=e->hPost)?1:0,
              (e->hPost!=e->h1)?1:0,
              (e->hPost!=e->h3)?1:0,
              (e->hPost!=e->h8)?1:0,
              (e->hPost!=e->h15)?1:0);
            e->active=0;
        }
    }
}

static void V668Stop(SOCKET s)
{
    g_v668CaptureEnabled=0;
    SendLine(s,
      "V668_CAPTURE_STOP accepted=%ld rejected=%ld seq=%ld",
      g_v668Accepted,g_v668Rejected,g_v668Seq);
}

static void V668Restore(SOCKET s)
{
    g_v668CaptureEnabled=0;

    if(!g_v668Installed){
        SendLine(s,"V668_RESTORE already=0");
        return;
    }

    if(V668Readable(V668_STUB,16)){
        memcpy((void*)V668_STUB,g_v668OrigStub,16);
        V645FlushCode(V668_STUB,16);
        g_v668Installed=FALSE;
        SendLine(s,"V668_RESTORE ok=1");
    }else{
        SendLine(s,"V668_RESTORE ok=0");
    }
}


#define V669_TITLE_START 0x82000000
#define V669_TITLE_END   0x84000000
#define V669_XAM_SUBMIT  0x816FB498
#define V669_COD4_R4ONE_CALLSITE 0x82116B80
#define V669_MAX_SAMPLES 96
#define V669_DESC_DWORDS 16
#define V669_STACK_DWORDS 24

typedef struct {
    DWORD phase;
    DWORD seq;
    DWORD lr;
    DWORD sp;
    DWORD r3;
    DWORD r4;
    DWORD r5;
    DWORD r6;
    DWORD r7;
    DWORD r8;
    DWORD r9;
    DWORD r10;
    DWORD r11;
    DWORD r12;
    DWORD avgAbs;
    DWORD avgDelta;
    DWORD desc[V669_DESC_DWORDS];
    DWORD stack[V669_STACK_DWORDS];
} V669_SAMPLE;

static BOOL g_v669Installed=FALSE;
static DWORD g_v669Stub=0;
static DWORD g_v669OrigStub[4]={0,0,0,0};
static volatile LONG g_v669Phase=0;
static volatile LONG g_v669Capture=0;
static volatile LONG g_v669Count=0;
static V669_SAMPLE g_v669Samples[V669_MAX_SAMPLES];

static BOOL V669Readable(DWORD p,DWORD n)
{
    if(!p || !n) return FALSE;
    return MmIsAddressValid(p) && MmIsAddressValid(p+n-1);
}

static SHORT V669LoadBE16(const volatile BYTE* p)
{
    WORD u=(WORD)(((WORD)p[0]<<8)|(WORD)p[1]);
    return (SHORT)u;
}

static LONG V669Abs16(SHORT v)
{
    LONG x=(LONG)v;
    if(x<0) x=-x;
    return x;
}

static BOOL V669IsSubmitStub(DWORD a)
{
    if(!V669Readable(a,16)) return FALSE;
    return
      *(volatile DWORD*)(a+0x00)==0x3D608170 &&
      *(volatile DWORD*)(a+0x04)==0x396BB498 &&
      *(volatile DWORD*)(a+0x08)==0x7D6903A6 &&
      *(volatile DWORD*)(a+0x0C)==0x4E800420;
}

static DWORD V669FindSubmitStub(SOCKET s)
{
    DWORD first=0;
    DWORD hits=0;

    for(DWORD page=V669_TITLE_START;page<V669_TITLE_END;page+=0x1000){
        if(!MmIsAddressValid(page)) continue;
        DWORD end=page+0x1000-16;

        for(DWORD a=page;a<=end;a+=4){
            if(*(volatile DWORD*)a!=0x3D608170) continue;
            if(!V669IsSubmitStub(a)) continue;

            hits++;
            if(!first) first=a;
            SendLine(s,"V669_SUBMIT_STUB hit=%u addr=0x%08X",hits,a);
        }
    }

    SendLine(s,"V669_STUB_SCAN_DONE hits=%u selected=0x%08X",hits,first);
    return first;
}

static void V669DumpCode(SOCKET s)
{
    DWORD center=V669_COD4_R4ONE_CALLSITE;
    DWORD start=center-0x240;
    DWORD end=center+0x180;

    SendLine(s,
      "V669_CALLER_CODE_START callsite=0x%08X range=0x%08X-0x%08X",
      center,start,end);

    for(DWORD a=start;a<=end;a+=16){
        if(!V669Readable(a,16)) continue;
        SendLine(s,
          "V669_CODE addr=0x%08X data=%08X,%08X,%08X,%08X",
          a,
          *(volatile DWORD*)(a+0),
          *(volatile DWORD*)(a+4),
          *(volatile DWORD*)(a+8),
          *(volatile DWORD*)(a+12));
    }

    // Also enumerate direct branches/calls in the window so the PC-side analysis
    // can identify likely upstream VAD / packet-builder helpers.
    for(DWORD a=start;a<=end;a+=4){
        if(!V669Readable(a,4)) continue;
        DWORD w=*(volatile DWORD*)a;
        DWORD target=0;
        if(V656DirectCallTarget(a,w,&target)){
            SendLine(s,
              "V669_DIRECT_BRANCH at=0x%08X word=0x%08X target=0x%08X",
              a,w,target);
        }
    }

    SendLine(s,"V669_CALLER_CODE_DONE");
}

static void V669PcmMetrics(DWORD r5,DWORD* outAbs,DWORD* outDelta)
{
    *outAbs=0;
    *outDelta=0;

    if(!r5 || !V669Readable(r5,16)) return;

    DWORD bytes=*(volatile DWORD*)(r5+0x04);
    DWORD pcm=*(volatile DWORD*)(r5+0x08);
    DWORD f0c=*(volatile DWORD*)(r5+0x0C);

    if(bytes!=0x280 || f0c!=0xA0 || !pcm || !V669Readable(pcm,0x280)) return;

    const volatile BYTE* b=(const volatile BYTE*)pcm;
    LONG absSum=0;
    LONG deltaSum=0;
    SHORT prev=V669LoadBE16(b);

    for(DWORD i=0;i<320;i++){
        SHORT x=V669LoadBE16(b+i*2);
        absSum+=V669Abs16(x);
        if(i){
            LONG d=(LONG)x-(LONG)prev;
            if(d<0) d=-d;
            deltaSum+=d;
        }
        prev=x;
    }

    *outAbs=(DWORD)(absSum/320);
    *outDelta=(DWORD)(deltaSum/319);
}

extern "C" void V669Capture(
    DWORD r3,DWORD r4,DWORD r5,DWORD r6,DWORD r7,
    DWORD r8,DWORD r9,DWORD r10,DWORD r11,DWORD r12,
    DWORD lr,DWORD sp)
{
    if(!g_v669Capture || r4!=1) return;

    LONG ix=g_v669Count;
    if(ix>=V669_MAX_SAMPLES) return;
    ix=InterlockedIncrement(&g_v669Count)-1;
    if(ix<0 || ix>=V669_MAX_SAMPLES) return;

    V669_SAMPLE* e=&g_v669Samples[ix];
    ZeroMemory(e,sizeof(*e));

    e->phase=(DWORD)g_v669Phase;
    e->seq=(DWORD)ix;
    e->lr=lr;
    e->sp=sp;
    e->r3=r3;
    e->r4=r4;
    e->r5=r5;
    e->r6=r6;
    e->r7=r7;
    e->r8=r8;
    e->r9=r9;
    e->r10=r10;
    e->r11=r11;
    e->r12=r12;

    V669PcmMetrics(r5,&e->avgAbs,&e->avgDelta);

    if(r5 && V669Readable(r5,V669_DESC_DWORDS*4)){
        for(DWORD i=0;i<V669_DESC_DWORDS;i++){
            e->desc[i]=*(volatile DWORD*)(r5+i*4);
        }
    }

    if(sp && V669Readable(sp,V669_STACK_DWORDS*4)){
        for(DWORD i=0;i<V669_STACK_DWORDS;i++){
            e->stack[i]=*(volatile DWORD*)(sp+i*4);
        }
    }
}

extern "C" __declspec(naked) DWORD Hook_V669_XamSubmitStub()
{
    __asm {
        mflr r0
        mr r31,r1
        stwu r1,-0xD0(r1)
        stw r0,0x80(r1)

        stw r3,0x20(r1)
        stw r4,0x24(r1)
        stw r5,0x28(r1)
        stw r6,0x2C(r1)
        stw r7,0x30(r1)
        stw r8,0x34(r1)
        stw r9,0x38(r1)
        stw r10,0x3C(r1)
        stw r11,0x40(r1)
        stw r12,0x44(r1)
        stw r31,0x48(r1)

        // First 8 args in registers/ABI home area, remaining args spill to stack.
        // C signature:
        // r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,lr,sp
        lwz r3,0x20(r1)
        lwz r4,0x24(r1)
        lwz r5,0x28(r1)
        lwz r6,0x2C(r1)
        lwz r7,0x30(r1)
        lwz r8,0x34(r1)
        lwz r9,0x38(r1)
        lwz r10,0x3C(r1)
        lwz r11,0x40(r1)
        lwz r12,0x44(r1)
        lwz r0,0x80(r1)
        stw r11,0x54(r1)
        stw r12,0x58(r1)
        stw r0,0x5C(r1)
        lwz r0,0x48(r1)
        stw r0,0x60(r1)
        bl V669Capture

        lwz r3,0x20(r1)
        lwz r4,0x24(r1)
        lwz r5,0x28(r1)
        lwz r6,0x2C(r1)
        lwz r7,0x30(r1)
        lwz r8,0x34(r1)
        lwz r9,0x38(r1)
        lwz r10,0x3C(r1)
        lwz r11,0x40(r1)
        lwz r12,0x44(r1)

        lwz r0,0x80(r1)
        mtlr r0
        addi r1,r1,0xD0

        lis r11,0x8170
        addi r11,r11,-0x4B68
        mtctr r11
        bctr
    }
}

static BOOL V669Install(SOCKET s)
{
    if(g_v669Installed){
        SendLine(s,"V669_INSTALL ok=1 already=1 stub=0x%08X",g_v669Stub);
        return TRUE;
    }

    V669DumpCode(s);

    DWORD stub=V669FindSubmitStub(s);
    if(!stub){
        SendLine(s,"V669_INSTALL ok=0 reason=xam_submit_stub_not_found");
        return FALSE;
    }

    g_v669Stub=stub;
    for(DWORD i=0;i<4;i++){
        g_v669OrigStub[i]=*(volatile DWORD*)(stub+i*4);
    }

    DWORD hook=(DWORD)&Hook_V669_XamSubmitStub;
    DWORD hi=(hook+0x8000)>>16;
    SHORT lo=(SHORT)(hook&0xFFFF);

    DWORD patch[4];
    patch[0]=0x3D600000|(hi&0xFFFF);
    patch[1]=0x396B0000|((WORD)lo);
    patch[2]=0x7D6903A6;
    patch[3]=0x4E800420;

    memcpy((void*)stub,patch,16);
    V645FlushCode(stub,16);

    g_v669Phase=0;
    g_v669Capture=0;
    g_v669Count=0;
    ZeroMemory(g_v669Samples,sizeof(g_v669Samples));
    g_v669Installed=TRUE;

    SendLine(s,
      "V669_INSTALL ok=1 OBSERVE_ONLY=1 stub=0x%08X hook=0x%08X realXam=0x%08X",
      stub,hook,V669_XAM_SUBMIT);
    return TRUE;
}

static void V669Start(SOCKET s,DWORD phase)
{
    if(!g_v669Installed){
        SendLine(s,"V669_PHASE_START ok=0 reason=not_installed");
        return;
    }

    g_v669Capture=0;
    g_v669Phase=(LONG)phase;
    g_v669Count=0;
    ZeroMemory(g_v669Samples,sizeof(g_v669Samples));
    g_v669Capture=1;

    SendLine(s,"V669_PHASE_START ok=1 phase=%u maxSamples=%u",phase,V669_MAX_SAMPLES);
}

static void V669StopDump(SOCKET s)
{
    g_v669Capture=0;

    LONG n=g_v669Count;
    if(n>V669_MAX_SAMPLES) n=V669_MAX_SAMPLES;

    SendLine(s,"V669_PHASE_HEADER phase=%ld samples=%ld",g_v669Phase,n);

    for(LONG k=0;k<n;k++){
        V669_SAMPLE* e=&g_v669Samples[k];

        SendLine(s,
          "V669_REG phase=%u seq=%u abs=%u delta=%u lr=%08X sp=%08X r3=%08X r4=%08X r5=%08X r6=%08X r7=%08X r8=%08X r9=%08X r10=%08X r11=%08X r12=%08X",
          e->phase,e->seq,e->avgAbs,e->avgDelta,
          e->lr,e->sp,e->r3,e->r4,e->r5,e->r6,e->r7,e->r8,e->r9,e->r10,e->r11,e->r12);

        SendLine(s,
          "V669_DESC phase=%u seq=%u d=%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X",
          e->phase,e->seq,
          e->desc[0],e->desc[1],e->desc[2],e->desc[3],
          e->desc[4],e->desc[5],e->desc[6],e->desc[7],
          e->desc[8],e->desc[9],e->desc[10],e->desc[11],
          e->desc[12],e->desc[13],e->desc[14],e->desc[15]);

        SendLine(s,
          "V669_STACK phase=%u seq=%u s=%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X",
          e->phase,e->seq,
          e->stack[0],e->stack[1],e->stack[2],e->stack[3],
          e->stack[4],e->stack[5],e->stack[6],e->stack[7],
          e->stack[8],e->stack[9],e->stack[10],e->stack[11],
          e->stack[12],e->stack[13],e->stack[14],e->stack[15],
          e->stack[16],e->stack[17],e->stack[18],e->stack[19],
          e->stack[20],e->stack[21],e->stack[22],e->stack[23]);
    }

    SendLine(s,"V669_PHASE_DONE phase=%ld samples=%ld",g_v669Phase,n);
}

static void V669Restore(SOCKET s)
{
    g_v669Capture=0;

    if(!g_v669Installed){
        SendLine(s,"V669_RESTORE already=0");
        return;
    }

    if(g_v669Stub && V669Readable(g_v669Stub,16)){
        memcpy((void*)g_v669Stub,g_v669OrigStub,16);
        V645FlushCode(g_v669Stub,16);
        g_v669Installed=FALSE;
        SendLine(s,"V669_RESTORE ok=1");
    }else{
        SendLine(s,"V669_RESTORE ok=0");
    }
}


#define V670_STUB 0x81A731E4
#define V670_REAL 0x80102048
#define V670_PACKET_BYTES 0x280
#define V670_MAX_SLOTS 16
#define V670_MAX_CYCLES 192

typedef DWORD (*V670RealFn)(DWORD,DWORD,DWORD,DWORD,DWORD);

typedef struct {
    volatile LONG active;
    volatile LONG generation;
    DWORD pcm;
    DWORD desc;
    DWORD submitTick;
    DWORD baselineHash;
    volatile LONG firstChangeAge;
    volatile LONG lastChangeAge;
    volatile LONG changeCount;
    volatile DWORD lastHash;
} V670_SLOT;

typedef struct {
    DWORD seq;
    DWORD pcm;
    DWORD desc;
    DWORD reuseAge;
    DWORD firstChangeAge;
    DWORD lastChangeAge;
    DWORD changeCount;
    DWORD baselineHash;
    DWORD finalHash;
    DWORD changed;
} V670_CYCLE;

static BOOL g_v670Installed=FALSE;
static DWORD g_v670OrigStub[4]={0,0,0,0};
static volatile LONG g_v670Capture=0;
static volatile LONG g_v670SubmitCount=0;
static volatile LONG g_v670SlotCount=0;
static volatile LONG g_v670CycleCount=0;
static volatile LONG g_v670PollPasses=0;
static V670_SLOT g_v670Slots[V670_MAX_SLOTS];
static V670_CYCLE g_v670Cycles[V670_MAX_CYCLES];

static BOOL V670Readable(DWORD p,DWORD n)
{
    if(!p || !n) return FALSE;
    return MmIsAddressValid(p) && MmIsAddressValid(p+n-1);
}

static BOOL V670StubValid()
{
    if(!V670Readable(V670_STUB,16)) return FALSE;
    return
      *(volatile DWORD*)(V670_STUB+0x00)==0x3D608010 &&
      *(volatile DWORD*)(V670_STUB+0x04)==0x396B2048 &&
      *(volatile DWORD*)(V670_STUB+0x08)==0x7D6903A6 &&
      *(volatile DWORD*)(V670_STUB+0x0C)==0x4E800420;
}

static DWORD V670Hash(DWORD p)
{
    if(!V670Readable(p,V670_PACKET_BYTES)) return 0;

    const volatile BYTE* b=(const volatile BYTE*)p;
    DWORD h=2166136261u;

    for(DWORD i=0;i<V670_PACKET_BYTES;i++){
        h^=(DWORD)b[i];
        h*=16777619u;
    }

    return h;
}

static void V670Reset()
{
    g_v670Capture=0;
    g_v670SubmitCount=0;
    g_v670SlotCount=0;
    g_v670CycleCount=0;
    g_v670PollPasses=0;
    ZeroMemory(g_v670Slots,sizeof(g_v670Slots));
    ZeroMemory(g_v670Cycles,sizeof(g_v670Cycles));
}

static LONG V670FindSlot(DWORD pcm)
{
    LONG slots=g_v670SlotCount;
    if(slots>V670_MAX_SLOTS) slots=V670_MAX_SLOTS;

    for(LONG i=0;i<slots;i++){
        if(g_v670Slots[i].pcm==pcm) return i;
    }

    return -1;
}

static LONG V670AllocSlot(DWORD pcm)
{
    LONG ix=InterlockedIncrement(&g_v670SlotCount)-1;
    if(ix<0 || ix>=V670_MAX_SLOTS){
        InterlockedDecrement(&g_v670SlotCount);
        return -1;
    }

    g_v670Slots[ix].pcm=pcm;
    return ix;
}

static void V670FinalizeCycle(V670_SLOT* slot,DWORD now,DWORD finalHash)
{
    LONG ix=InterlockedIncrement(&g_v670CycleCount)-1;
    if(ix<0 || ix>=V670_MAX_CYCLES){
        InterlockedDecrement(&g_v670CycleCount);
        return;
    }

    V670_CYCLE* c=&g_v670Cycles[ix];
    ZeroMemory(c,sizeof(*c));

    c->seq=(DWORD)ix;
    c->pcm=slot->pcm;
    c->desc=slot->desc;
    c->reuseAge=now-slot->submitTick;
    c->firstChangeAge=(DWORD)slot->firstChangeAge;
    c->lastChangeAge=(DWORD)slot->lastChangeAge;
    c->changeCount=(DWORD)slot->changeCount;
    c->baselineHash=slot->baselineHash;
    c->finalHash=finalHash;
    c->changed=(finalHash!=slot->baselineHash || slot->changeCount>0)?1:0;
}

static void V670TrackSubmit(DWORD desc,DWORD pcm,DWORD hash,DWORD now)
{
    LONG ix=V670FindSlot(pcm);
    if(ix<0) ix=V670AllocSlot(pcm);
    if(ix<0) return;

    V670_SLOT* slot=&g_v670Slots[ix];

    // Invalidate the current generation before resetting it so the polling
    // thread cannot commit an observation into a newly recycled cycle.
    slot->active=0;
    InterlockedIncrement(&slot->generation);
    MemoryBarrier();

    if(slot->submitTick!=0){
        V670FinalizeCycle(slot,now,hash);
    }

    slot->desc=desc;
    slot->submitTick=now;
    slot->baselineHash=hash;
    slot->firstChangeAge=0;
    slot->lastChangeAge=0;
    slot->changeCount=0;
    slot->lastHash=hash;

    MemoryBarrier();
    slot->active=1;
}

extern "C" DWORD V670CallRealAndTrack(
    DWORD r3,DWORD r4,DWORD r5,DWORD r6,DWORD r7)
{
    DWORD pcm=0;
    DWORD preHash=0;
    BOOL track=FALSE;

    if(g_v670Capture &&
       r4==1 &&
       r5 &&
       V670Readable(r5,16)){
        DWORD bytes=*(volatile DWORD*)(r5+0x04);
        pcm=*(volatile DWORD*)(r5+0x08);
        DWORD f0c=*(volatile DWORD*)(r5+0x0C);

        if(bytes==V670_PACKET_BYTES &&
           f0c==0xA0 &&
           pcm &&
           V670Readable(pcm,V670_PACKET_BYTES)){
            preHash=V670Hash(pcm);
            track=TRUE;
        }
    }

    V670RealFn fn=(V670RealFn)V670_REAL;
    DWORD result=fn(r3,r4,r5,r6,r7);

    if(track && g_v670Capture){
        DWORD now=GetTickCount();
        V670TrackSubmit(r5,pcm,preHash,now);
        InterlockedIncrement(&g_v670SubmitCount);
    }

    return result;
}

extern "C" __declspec(naked) DWORD Hook_V670_SystemSubmit()
{
    __asm {
        mflr r0
        stwu r1,-0x80(r1)
        stw r0,0x60(r1)

        bl V670CallRealAndTrack

        lwz r0,0x60(r1)
        mtlr r0
        addi r1,r1,0x80
        blr
    }
}

static BOOL V670Install(SOCKET s)
{
    if(g_v670Installed){
        SendLine(s,"V670_INSTALL ok=1 already=1");
        return TRUE;
    }

    if(!V670StubValid()){
        SendLine(s,"V670_INSTALL ok=0 reason=system_stub_invalid");
        return FALSE;
    }

    for(DWORD i=0;i<4;i++){
        g_v670OrigStub[i]=*(volatile DWORD*)(V670_STUB+i*4);
    }

    DWORD hook=(DWORD)&Hook_V670_SystemSubmit;
    DWORD hi=(hook+0x8000)>>16;
    SHORT lo=(SHORT)(hook&0xFFFF);

    DWORD patch[4];
    patch[0]=0x3D600000|(hi&0xFFFF);
    patch[1]=0x396B0000|((WORD)lo);
    patch[2]=0x7D6903A6;
    patch[3]=0x4E800420;

    memcpy((void*)V670_STUB,patch,16);
    V645FlushCode(V670_STUB,16);

    V670Reset();
    g_v670Installed=TRUE;

    SendLine(s,
      "V670_INSTALL ok=1 OBSERVE_ONLY=1 stub=0x%08X real=0x%08X hook=0x%08X",
      V670_STUB,V670_REAL,hook);
    return TRUE;
}

static void V670Start(SOCKET s)
{
    if(!g_v670Installed){
        SendLine(s,"V670_CAPTURE_START ok=0 reason=not_installed");
        return;
    }

    V670Reset();
    g_v670Capture=1;

    SendLine(s,
      "V670_CAPTURE_START ok=1 maxSlots=%u maxCycles=%u packetBytes=0x280",
      V670_MAX_SLOTS,V670_MAX_CYCLES);
}

static void V670Poll()
{
    if(!g_v670Capture) return;

    InterlockedIncrement(&g_v670PollPasses);

    LONG slots=g_v670SlotCount;
    if(slots>V670_MAX_SLOTS) slots=V670_MAX_SLOTS;
    DWORD now=GetTickCount();

    for(LONG i=0;i<slots;i++){
        V670_SLOT* slot=&g_v670Slots[i];
        if(!slot->active || !slot->submitTick || !slot->pcm) continue;

        LONG gen=slot->generation;
        DWORD pcm=slot->pcm;
        DWORD age=now-slot->submitTick;

        // v6.68 already proved no changes through +15 ms. Begin after that and
        // follow the buffer for the full expected ring-recycle interval.
        if(age<16 || age>350) continue;

        DWORD h=V670Hash(pcm);

        // Abort if the voice thread recycled this slot while we were hashing it.
        if(!slot->active ||
           slot->generation!=gen ||
           slot->pcm!=pcm) continue;

        DWORD last=slot->lastHash;
        if(h!=last){
            if(slot->firstChangeAge==0){
                slot->firstChangeAge=(LONG)age;
            }
            slot->lastChangeAge=(LONG)age;
            InterlockedIncrement(&slot->changeCount);
            slot->lastHash=h;
        }
    }
}

static void V670StopDump(SOCKET s)
{
    g_v670Capture=0;
    Sleep(2);

    LONG slots=g_v670SlotCount;
    LONG cycles=g_v670CycleCount;
    if(slots>V670_MAX_SLOTS) slots=V670_MAX_SLOTS;
    if(cycles>V670_MAX_CYCLES) cycles=V670_MAX_CYCLES;

    SendLine(s,
      "V670_CAPTURE_STOP submits=%ld slots=%ld cycles=%ld pollPasses=%ld",
      g_v670SubmitCount,slots,cycles,g_v670PollPasses);

    for(LONG i=0;i<slots;i++){
        V670_SLOT* slot=&g_v670Slots[i];
        SendLine(s,
          "V670_SLOT index=%ld pcm=0x%08X desc=0x%08X",
          i,slot->pcm,slot->desc);
    }

    for(LONG i=0;i<cycles;i++){
        V670_CYCLE* c=&g_v670Cycles[i];
        SendLine(s,
          "V670_CYCLE seq=%u pcm=0x%08X desc=0x%08X reuseAge=%u changed=%u firstChange=%u lastChange=%u changes=%u baseline=0x%08X final=0x%08X",
          c->seq,c->pcm,c->desc,c->reuseAge,c->changed,
          c->firstChangeAge,c->lastChangeAge,c->changeCount,
          c->baselineHash,c->finalHash);
    }

    SendLine(s,"V670_DUMP_DONE cycles=%ld",cycles);
}

static void V670Restore(SOCKET s)
{
    g_v670Capture=0;

    if(!g_v670Installed){
        SendLine(s,"V670_RESTORE already=0");
        return;
    }

    if(V670Readable(V670_STUB,16)){
        memcpy((void*)V670_STUB,g_v670OrigStub,16);
        V645FlushCode(V670_STUB,16);
        g_v670Installed=FALSE;
        SendLine(s,"V670_RESTORE ok=1");
    }else{
        SendLine(s,"V670_RESTORE ok=0");
    }
}



// ============================================================================
// v7.00 UNIVERSAL SYSTEM MIC
//
// Goals versus v6.71:
//   * Resolve XVoicedSubmitPacket at runtime from xboxkrnl export ordinal 0x1E2.
//   * Resolve XamVoiceSubmitPacket at runtime (ordinal 0x30E) only to derive the
//     most likely XAM scan neighborhood; broad mapped fallback scan remains.
//   * Discover the XAM absolute thunk that targets the resolved XVoiced export.
//   * Discover the active 640-byte voice descriptor lane at runtime instead of
//     hard-coding r4==1 / +0x0C==0xA0 as mandatory requirements.
//   * Establish the post-submit baseline AFTER the real XVoiced call returns.
//   * Detect the first later external refill by hash change; no 150..230 ms
//     injection window is required.
//   * Stop touching stale slots automatically if submit reuse disappears.
//
// The PC transport intentionally remains 16 kHz mono PCM16-BE, 320 samples /
// 640 bytes / 20 ms because both proven titles converged on that system packet
// geometry. Unexpected geometry is observed and reported, never overwritten.
// ============================================================================

#define V700_KERNEL_XVOICED_SUBMIT_ORD 0x1E2
#define V700_XAM_VOICE_SUBMIT_ORD      0x30E
#define V702_KERNEL_XVOICED_HEADSET_ORD 0x1E1
#define V702_XAM_VOICE_HEADSET_ORD      0x30D
#define V702_AUDIO_FRESH_MS             120
#define V700_FRAME_BYTES               0x280
#define V700_MAX_SLOTS                 16
#define V700_AUDIO_RING                16
#define V700_MAX_CANDIDATES            12
#define V700_STALE_FALLBACK_MS         1000

typedef DWORD (*V700RealFn)(DWORD,DWORD,DWORD,DWORD,DWORD);

typedef struct {
    volatile LONG active;
    DWORD lane;
    DWORD bytes;
    DWORD f0c;
    volatile LONG hits;
    DWORD firstTick;
    DWORD lastTick;
} V700_CANDIDATE;

typedef struct {
    volatile LONG active;
    volatile LONG generation;
    DWORD pcm;
    DWORD desc;
    DWORD submitTick;
    DWORD baselineHash;
    volatile LONG firstChangeAge;
    volatile LONG lastObservedChangeAge;
    volatile LONG postRefillWrites;
    volatile DWORD lastObservedHash;
} V700_SLOT;

static BOOL g_v700Installed=FALSE;
static DWORD g_v700Stub=0;
static DWORD g_v700Real=0;
static DWORD g_v700XamSubmit=0;
static DWORD g_v700ScanStart=0;
static DWORD g_v700ScanEnd=0;
static DWORD g_v700OrigStub[4]={0,0,0,0};
static volatile LONG g_v700UsedLegacyRealFallback=0;

// v7.02 virtual-headset state.  We hook XAM's gateway to the kernel
// XVoicedHeadsetPresent export (0x1E1) dynamically, exactly like SubmitPacket.
// The real query is still executed so we know whether actual headset hardware
// is present; the caller sees TRUE while the universal injector is installed.
static DWORD g_v702HeadsetReal=0;
static DWORD g_v702XamHeadset=0;
static DWORD g_v702HeadsetStub=0;
static DWORD g_v702OrigHeadsetStub[4]={0,0,0,0};
static volatile LONG g_v702VirtualHeadset=0;
static volatile LONG g_v702PhysicalHeadset=0;
static volatile LONG g_v702HeadsetQueries=0;
static volatile LONG g_v702VirtualHeadsetHits=0;
static volatile LONG g_v702DirectVirtualWrites=0;
static volatile LONG g_v702ForcedSubmitSuccess=0;
static volatile LONG g_v702SilenceFallbackWrites=0;
static volatile DWORD g_v702LastAudioTick=0;
static volatile DWORD g_v702LastSubmitResult=0;

typedef DWORD (*V702HeadsetFn)(DWORD,DWORD,DWORD,DWORD,DWORD);

static volatile LONG g_v700Enabled=0; // 0 pass, 1 tone, 2 live
static volatile LONG g_v700SubmitCount=0;
static volatile LONG g_v700SlotCount=0;
static volatile LONG g_v700DetectedRefills=0;
static volatile LONG g_v700InjectedWrites=0;
static volatile LONG g_v700InjectedSlots=0;
static volatile LONG g_v700NoAudioWrites=0;
static volatile LONG g_v700UnexpectedGeometry=0;
static volatile LONG g_v700StaleDeactivations=0;

static volatile LONG g_v700LaneLocked=0;
static volatile DWORD g_v700Lane=0;
static volatile DWORD g_v700PacketBytes=0;
static volatile DWORD g_v700Field0C=0;
static volatile LONG g_v700CandidateCount=0;
static V700_CANDIDATE g_v700Candidates[V700_MAX_CANDIDATES];

static volatile LONG g_v700ReuseCount=0;
static volatile LONG g_v700ReuseAgeSum=0;
static volatile LONG g_v700ReuseAgeMin=0x7FFFFFFF;
static volatile LONG g_v700ReuseAgeMax=0;
static volatile LONG g_v700RefillAgeCount=0;
static volatile LONG g_v700RefillAgeSum=0;
static volatile LONG g_v700RefillAgeMin=0x7FFFFFFF;
static volatile LONG g_v700RefillAgeMax=0;

static V700_SLOT g_v700Slots[V700_MAX_SLOTS];

static BYTE g_v700Audio[V700_AUDIO_RING][V700_FRAME_BYTES];
static volatile LONG g_v700AudioWriteSeq=0;
static volatile LONG g_v700AudioReadSeq=0;
static volatile LONG g_v700AudioRx=0;
static volatile LONG g_v700AudioDrops=0;

static DWORD g_v700TonePhase=0;
static const SHORT g_v700Tone16[16]={
       0, 2296, 4243, 5543,
    6000, 5543, 4243, 2296,
       0,-2296,-4243,-5543,
   -6000,-5543,-4243,-2296
};

static BOOL V700Readable(DWORD p,DWORD n)
{
    if(!p || !n) return FALSE;
    return MmIsAddressValid(p) && MmIsAddressValid(p+n-1);
}

static DWORD V700ResolveExport(const char* moduleName,DWORD ordinal)
{
    HANDLE h=0;
    PVOID p=0;
    if(!moduleName) return 0;
    if(XexGetModuleHandle((PSZ)moduleName,&h)!=0 || !h) return 0;
    if(XexGetProcedureAddress(h,ordinal,&p)!=0 || !p) return 0;
    return (DWORD)p;
}

static DWORD V700Hash(DWORD p)
{
    if(!V700Readable(p,V700_FRAME_BYTES)) return 0;
    const volatile BYTE* b=(const volatile BYTE*)p;
    DWORD h=2166136261u;
    for(DWORD i=0;i<V700_FRAME_BYTES;i++){
        h^=(DWORD)b[i];
        h*=16777619u;
    }
    return h;
}

static void V700ResetAudio()
{
    g_v700AudioWriteSeq=0;
    g_v700AudioReadSeq=0;
    g_v700AudioRx=0;
    g_v700AudioDrops=0;
    g_v702LastAudioTick=0;
    ZeroMemory(g_v700Audio,sizeof(g_v700Audio));
}

static void V700PushAudio(const BYTE* data)
{
    LONG w=g_v700AudioWriteSeq;
    LONG r=g_v700AudioReadSeq;

    if(w-r>=V700_AUDIO_RING){
        InterlockedExchange(&g_v700AudioReadSeq,r+1);
        InterlockedIncrement(&g_v700AudioDrops);
        r++;
    }

    DWORD slot=(DWORD)w & (V700_AUDIO_RING-1);
    memcpy(g_v700Audio[slot],data,V700_FRAME_BYTES);
    MemoryBarrier();
    InterlockedExchange(&g_v700AudioWriteSeq,w+1);
    g_v702LastAudioTick=GetTickCount();
    InterlockedIncrement(&g_v700AudioRx);
}

static BOOL V700CopyLatestAudio(DWORD dst)
{
    if(!V700Readable(dst,V700_FRAME_BYTES)) return FALSE;

    LONG w=g_v700AudioWriteSeq;
    DWORD lastTick=g_v702LastAudioTick;
    DWORD now=GetTickCount();

    // v7.02: never repeat a stale speech frame forever.  If the PC stream has
    // not supplied a fresh 20 ms frame recently, explicitly overwrite the mic
    // packet with digital silence.  This also gives the game's normal VAD a
    // clean chance to drop its talking indicator.
    if(w<=0 || !lastTick || (DWORD)(now-lastTick)>V702_AUDIO_FRESH_MS){
        ZeroMemory((void*)dst,V700_FRAME_BYTES);
        InterlockedIncrement(&g_v702SilenceFallbackWrites);
        return FALSE;
    }

    DWORD slot=(DWORD)(w-1) & (V700_AUDIO_RING-1);
    memcpy((void*)dst,g_v700Audio[slot],V700_FRAME_BYTES);
    InterlockedExchange(&g_v700AudioReadSeq,w);
    return TRUE;
}

static void V700WriteTone(DWORD dst)
{
    volatile BYTE* out=(volatile BYTE*)dst;
    DWORD ph=g_v700TonePhase;
    for(DWORD i=0;i<320;i++){
        SHORT sample=g_v700Tone16[ph&15];
        WORD u=(WORD)sample;
        out[i*2+0]=(BYTE)(u>>8);
        out[i*2+1]=(BYTE)(u&0xFF);
        ph++;
    }
    g_v700TonePhase=ph&15;
}

static void V700UpdateMinMax(volatile LONG* mn,volatile LONG* mx,LONG value)
{
    LONG old=*mn;
    if(value<old) InterlockedExchange(mn,value);
    old=*mx;
    if(value>old) InterlockedExchange(mx,value);
}

static void V700ResetDiscovery()
{
    g_v700LaneLocked=0;
    g_v700Lane=0;
    g_v700PacketBytes=0;
    g_v700Field0C=0;
    g_v700CandidateCount=0;
    g_v700UnexpectedGeometry=0;
    ZeroMemory(g_v700Candidates,sizeof(g_v700Candidates));

    g_v700SubmitCount=0;
    g_v700SlotCount=0;
    g_v700DetectedRefills=0;
    g_v700InjectedWrites=0;
    g_v700InjectedSlots=0;
    g_v700NoAudioWrites=0;
    g_v700StaleDeactivations=0;
    g_v702DirectVirtualWrites=0;
    g_v702ForcedSubmitSuccess=0;
    g_v702SilenceFallbackWrites=0;
    g_v702LastSubmitResult=0;
    ZeroMemory(g_v700Slots,sizeof(g_v700Slots));

    g_v700ReuseCount=0;
    g_v700ReuseAgeSum=0;
    g_v700ReuseAgeMin=0x7FFFFFFF;
    g_v700ReuseAgeMax=0;
    g_v700RefillAgeCount=0;
    g_v700RefillAgeSum=0;
    g_v700RefillAgeMin=0x7FFFFFFF;
    g_v700RefillAgeMax=0;
}

static LONG V700FindCandidate(DWORD lane,DWORD bytes,DWORD f0c)
{
    LONG n=g_v700CandidateCount;
    if(n>V700_MAX_CANDIDATES) n=V700_MAX_CANDIDATES;
    for(LONG i=0;i<n;i++){
        V700_CANDIDATE* c=&g_v700Candidates[i];
        if(c->active && c->lane==lane && c->bytes==bytes && c->f0c==f0c)
            return i;
    }
    return -1;
}

static LONG V700AllocCandidate(DWORD lane,DWORD bytes,DWORD f0c,DWORD now)
{
    LONG ix=InterlockedIncrement(&g_v700CandidateCount)-1;
    if(ix<0 || ix>=V700_MAX_CANDIDATES){
        InterlockedDecrement(&g_v700CandidateCount);
        return -1;
    }

    V700_CANDIDATE* c=&g_v700Candidates[ix];
    c->active=0;
    c->lane=lane;
    c->bytes=bytes;
    c->f0c=f0c;
    c->hits=0;
    c->firstTick=now;
    c->lastTick=now;
    MemoryBarrier();
    c->active=1;
    return ix;
}

static void V700ObserveCandidate(DWORD lane,DWORD bytes,DWORD f0c,DWORD now)
{
    // v7.04 performance: once the lane/geometry is locked, candidate scoring is
    // finished.  Do not keep walking/updating the candidate table on every
    // XVoiced submit; only retain the unexpected-geometry diagnostic.
    if(g_v700LaneLocked){
        if(bytes!=V700_FRAME_BYTES)
            InterlockedIncrement(&g_v700UnexpectedGeometry);
        return;
    }

    // v7 transport can safely overwrite only the proven 640-byte packet shape.
    // Other valid descriptor shapes are counted but never selected for injection.
    if(bytes!=V700_FRAME_BYTES){
        InterlockedIncrement(&g_v700UnexpectedGeometry);
        return;
    }

    LONG ix=V700FindCandidate(lane,bytes,f0c);
    if(ix<0) ix=V700AllocCandidate(lane,bytes,f0c,now);
    if(ix<0) return;

    V700_CANDIDATE* c=&g_v700Candidates[ix];
    LONG hits=InterlockedIncrement(&c->hits);
    c->lastTick=now;

    if(!g_v700LaneLocked){
        LONG score=hits*4;

        // These are confidence bonuses learned from the two known-good titles,
        // not hard requirements. A different lane/field can still win by being
        // stable for enough packets.
        if(lane==1) score+=20;
        if(f0c==0xA0) score+=20;

        if(score>=60){
            g_v700Lane=lane;
            g_v700PacketBytes=bytes;
            g_v700Field0C=f0c;
            MemoryBarrier();
            InterlockedExchange(&g_v700LaneLocked,1);
        }
    }
}

static BOOL V700CandidateAccepted(DWORD lane,DWORD bytes,DWORD f0c)
{
    if(!g_v700LaneLocked) return FALSE;
    return lane==g_v700Lane && bytes==g_v700PacketBytes && f0c==g_v700Field0C;
}

static LONG V700FindSlot(DWORD pcm)
{
    LONG n=g_v700SlotCount;
    if(n>V700_MAX_SLOTS) n=V700_MAX_SLOTS;
    for(LONG i=0;i<n;i++){
        if(g_v700Slots[i].pcm==pcm) return i;
    }
    return -1;
}

static LONG V700AllocSlot(DWORD pcm)
{
    LONG ix=InterlockedIncrement(&g_v700SlotCount)-1;
    if(ix<0 || ix>=V700_MAX_SLOTS){
        InterlockedDecrement(&g_v700SlotCount);
        return -1;
    }
    g_v700Slots[ix].pcm=pcm;
    return ix;
}

static void V700TrackSubmit(DWORD desc,DWORD pcm,DWORD postHash,DWORD now)
{
    LONG ix=V700FindSlot(pcm);
    if(ix<0) ix=V700AllocSlot(pcm);
    if(ix<0) return;

    V700_SLOT* slot=&g_v700Slots[ix];

    if(slot->active && slot->submitTick){
        LONG reuse=(LONG)(now-slot->submitTick);
        InterlockedIncrement(&g_v700ReuseCount);
        InterlockedExchangeAdd(&g_v700ReuseAgeSum,reuse);
        V700UpdateMinMax(&g_v700ReuseAgeMin,&g_v700ReuseAgeMax,reuse);
    }

    slot->active=0;
    InterlockedIncrement(&slot->generation);
    MemoryBarrier();

    slot->desc=desc;
    slot->submitTick=now;
    slot->baselineHash=postHash;
    slot->firstChangeAge=0;
    slot->lastObservedChangeAge=0;
    slot->postRefillWrites=0;
    slot->lastObservedHash=postHash;

    MemoryBarrier();
    slot->active=1;
}

static DWORD V700AdaptiveStaleMs()
{
    LONG count=g_v700ReuseCount;
    if(count>=3){
        LONG avg=g_v700ReuseAgeSum/count;
        LONG ms=avg*3;
        if(ms<300) ms=300;
        if(ms>1500) ms=1500;
        return (DWORD)ms;
    }
    return V700_STALE_FALLBACK_MS;
}

extern "C" DWORD V700CallRealAndTrack(
    DWORD r3,DWORD r4,DWORD r5,DWORD r6,DWORD r7)
{
    DWORD pcm=0;
    DWORD bytes=0;
    DWORD f0c=0;
    BOOL descriptorValid=FALSE;
    DWORD now=GetTickCount();

    if(r5 && V700Readable(r5,16)){
        bytes=*(volatile DWORD*)(r5+0x04);
        pcm=*(volatile DWORD*)(r5+0x08);
        f0c=*(volatile DWORD*)(r5+0x0C);

        if(g_v700LaneLocked){
            // v7.04: after discovery is complete, reject unrelated voice
            // descriptors from their fields alone.  Do not touch/validate their
            // PCM buffers; only the locked 640-byte mic lane needs that work.
            if(bytes==g_v700PacketBytes && r4==g_v700Lane && f0c==g_v700Field0C){
                if(pcm && V700Readable(pcm,V700_FRAME_BYTES))
                    descriptorValid=TRUE;
            }else if(bytes!=V700_FRAME_BYTES){
                InterlockedIncrement(&g_v700UnexpectedGeometry);
            }
        }else if(pcm && bytes>=0x20 && bytes<=0x1000 && V700Readable(pcm,bytes)){
            descriptorValid=TRUE;
            V700ObserveCandidate(r4,bytes,f0c,now);
        }
    }

    V700RealFn fn=(V700RealFn)g_v700Real;
    DWORD result=fn ? fn(r3,r4,r5,r6,r7) : 0;
    g_v702LastSubmitResult=result;

    // Establish baseline after the synchronous submit path has completed. Any
    // later hash transition is therefore external/lifecycle activity, not a
    // mutation performed by XVoicedSubmitPacket itself.
    if(descriptorValid &&
       bytes==V700_FRAME_BYTES &&
       V700CandidateAccepted(r4,bytes,f0c)){

        // v7.04: virtual/no-wire injection is synchronous in this hook, so it
        // does not need the old wired-headset refill hash/slot machinery.
        // Skipping that bookkeeping removes two 640-byte hashes plus slot scans
        // from this very hot voice-submit path.  Wired hardware retains the
        // v7.01 refill-tracking path unchanged.
        BOOL virtualOnly=(g_v702VirtualHeadset && !g_v702PhysicalHeadset);
        BOOL directWrote=FALSE;
        LONG mode=g_v700Enabled;
        if(virtualOnly && mode==1){
            V700WriteTone(pcm);
            directWrote=TRUE;
        }else if(virtualOnly && mode==2){
            if(!V700CopyLatestAudio(pcm))
                InterlockedIncrement(&g_v700NoAudioWrites);
            directWrote=TRUE;
        }

        InterlockedIncrement(&g_v700SubmitCount);

        if(directWrote){
            InterlockedIncrement(&g_v702DirectVirtualWrites);
            InterlockedIncrement(&g_v700InjectedWrites);
        }else if(!virtualOnly){
            // Physical/wired headset path still needs post-submit baseline and
            // refill-edge tracking because controller DMA can refill the packet
            // asynchronously after this call returns.
            DWORD postHash=V700Hash(pcm);
            V700TrackSubmit(r5,pcm,postHash,GetTickCount());
        }
    }

    // The kernel voice submit API uses zero for success.  With no physical
    // headset, report success while virtual-headset mode is enabled so XAM/the
    // title does not tear the capture queue down solely because the accessory
    // detect bit is absent.  Preserve the untouched result for wired hardware.
    if(g_v702VirtualHeadset && !g_v702PhysicalHeadset && result!=0){
        InterlockedIncrement(&g_v702ForcedSubmitSuccess);
        result=0;
    }

    return result;
}

extern "C" __declspec(naked) DWORD Hook_V700_SystemSubmit()
{
    __asm {
        mflr r0
        stwu r1,-0x80(r1)
        stw r0,0x60(r1)

        bl V700CallRealAndTrack

        lwz r0,0x60(r1)
        mtlr r0
        addi r1,r1,0x80
        blr
    }
}

extern "C" DWORD V702CallRealHeadsetPresent(
    DWORD r3,DWORD r4,DWORD r5,DWORD r6,DWORD r7)
{
    V702HeadsetFn fn=(V702HeadsetFn)g_v702HeadsetReal;
    DWORD realPresent=fn ? fn(r3,r4,r5,r6,r7) : 0;
    InterlockedIncrement(&g_v702HeadsetQueries);
    InterlockedExchange(&g_v702PhysicalHeadset,realPresent?1:0);

    if(g_v702VirtualHeadset){
        InterlockedIncrement(&g_v702VirtualHeadsetHits);
        return 1;
    }
    return realPresent;
}

extern "C" __declspec(naked) DWORD Hook_V702_HeadsetPresent()
{
    __asm {
        mflr r0
        stwu r1,-0x80(r1)
        stw r0,0x60(r1)

        bl V702CallRealHeadsetPresent

        lwz r0,0x60(r1)
        mtlr r0
        addi r1,r1,0x80
        blr
    }
}

// v7.01: page-wise gateway scanner. v7.00 called MmIsAddressValid twice for
// every 4-byte candidate while scanning as much as 16 MB. On BO2 that made the
// synchronous O_install path exceed the PC-side timeout before a result could
// be reported. This scanner validates once per page, prefilters the first PPC
// instruction, and also accepts common r11/r12 + addi/ori thunk forms.
static BOOL V701RebuildGatewayTarget(DWORD a,DWORD* target)
{
    if(!target) return FALSE;
    DWORD w0=*(volatile DWORD*)(a+0);
    DWORD w1=*(volatile DWORD*)(a+4);
    DWORD w2=*(volatile DWORD*)(a+8);
    DWORD w3=*(volatile DWORD*)(a+12);

    // lis r11,HI ; addi/ori r11,r11,LO ; mtctr r11 ; bctr/bctrl
    if((w0&0xFFFF0000)==0x3D600000 &&
       ((w1&0xFFFF0000)==0x396B0000 || (w1&0xFFFF0000)==0x616B0000) &&
       w2==0x7D6903A6 && (w3==0x4E800420 || w3==0x4E800421)){
        DWORD hi=w0&0xFFFF;
        DWORD lo=w1&0xFFFF;
        if((w1&0xFFFF0000)==0x396B0000)
            *target=(hi<<16)+(LONG)(SHORT)lo;
        else
            *target=(hi<<16)|lo;
        return TRUE;
    }

    // Same canonical import thunk using r12.
    if((w0&0xFFFF0000)==0x3D800000 &&
       ((w1&0xFFFF0000)==0x398C0000 || (w1&0xFFFF0000)==0x618C0000) &&
       w2==0x7D8903A6 && (w3==0x4E800420 || w3==0x4E800421)){
        DWORD hi=w0&0xFFFF;
        DWORD lo=w1&0xFFFF;
        if((w1&0xFFFF0000)==0x398C0000)
            *target=(hi<<16)+(LONG)(SHORT)lo;
        else
            *target=(hi<<16)|lo;
        return TRUE;
    }

    return FALSE;
}

static DWORD V701FindThunkFast(SOCKET s,const char* tag,DWORD start,DWORD end,DWORD target)
{
    DWORD pages=0,mapped=0,candidates=0;
    SendLine(s,"V701_SCAN_BEGIN tag=%s range=0x%08X-0x%08X target=0x%08X",tag,start,end,target);

    for(DWORD page=start;page<end;page+=0x1000){
        pages++;
        if(!MmIsAddressValid(page) || !MmIsAddressValid(page+0xFFF)) continue;
        mapped++;

        // A 16-byte gateway cannot start after +0xFF0 and remain in-page.
        for(DWORD a=page;a<=page+0xFF0;a+=4){
            DWORD w0=*(volatile DWORD*)a;
            if((w0&0xFFFF0000)!=0x3D600000 &&
               (w0&0xFFFF0000)!=0x3D800000)
                continue;

            candidates++;
            DWORD rebuilt=0;
            if(V701RebuildGatewayTarget(a,&rebuilt) && rebuilt==target){
                SendLine(s,
                  "V701_SCAN_HIT tag=%s addr=0x%08X target=0x%08X pages=%u mapped=%u candidates=%u",
                  tag,a,target,pages,mapped,candidates);
                return a;
            }
        }

        if((pages&0x3FF)==0){
            SendLine(s,
              "V701_SCAN_PROGRESS tag=%s at=0x%08X pages=%u mapped=%u candidates=%u",
              tag,page,pages,mapped,candidates);
        }
    }

    SendLine(s,
      "V701_SCAN_DONE tag=%s hit=0 pages=%u mapped=%u candidates=%u",
      tag,pages,mapped,candidates);
    return 0;
}

static DWORD V700FindThunkDerived(SOCKET s,DWORD real,DWORD xamSubmit,DWORD* outStart,DWORD* outEnd)
{
    if(outStart) *outStart=0;
    if(outEnd) *outEnd=0;

    // XAM's resolved export sits in the 0x81xxxxxx system-code region on the
    // tested dashboards, but the XVoiced import gateway can be several MB away
    // from the export itself. v7.00 incorrectly searched only a 2 MB window
    // around the export first. Derive the full 16 MB region from the export's
    // top byte instead, then use the fast page scanner.
    DWORD start=0x81000000;
    DWORD end=0x82000000;
    if(xamSubmit){
        DWORD derived=xamSubmit & 0xFF000000;
        if(derived>=0x80000000 && derived<=0x8F000000){
            start=derived;
            end=derived+0x01000000;
        }
    }

    DWORD hit=V701FindThunkFast(s,"XAM_SYSTEM_REGION",start,end,real);
    if(hit){
        if(outStart) *outStart=start;
        if(outEnd) *outEnd=end;
        return hit;
    }

    // If an unusual loader places XAM outside the expected derived region,
    // make one bounded system-code fallback. This is still page-wise and emits
    // progress, so the PC can distinguish a real search from a deadlock.
    if(start!=0x81000000 || end!=0x82000000){
        start=0x81000000;
        end=0x82000000;
        hit=V701FindThunkFast(s,"SYSTEM_FALLBACK",start,end,real);
        if(hit){
            if(outStart) *outStart=start;
            if(outEnd) *outEnd=end;
            return hit;
        }
    }

    return 0;
}

static BOOL V700ResolveAndLocate(SOCKET s)
{
    g_v700Real=V700ResolveExport("xboxkrnl.exe",V700_KERNEL_XVOICED_SUBMIT_ORD);
    g_v700XamSubmit=V700ResolveExport("xam.xex",V700_XAM_VOICE_SUBMIT_ORD);
    g_v702HeadsetReal=V700ResolveExport("xboxkrnl.exe",V702_KERNEL_XVOICED_HEADSET_ORD);
    g_v702XamHeadset=V700ResolveExport("xam.xex",V702_XAM_VOICE_HEADSET_ORD);
    g_v700UsedLegacyRealFallback=0;

    // Compatibility fallback only. The normal v7 path is ordinal resolution.
    // Keeping this lets the proven v6.71 dashboard continue working if the SDK
    // export lookup unexpectedly fails on a particular loader environment.
    if(!g_v700Real && V700Readable(0x80102048,16)){
        g_v700Real=0x80102048;
        g_v700UsedLegacyRealFallback=1;
    }

    if(!g_v700Real){
        SendLine(s,
          "V700_RESOLVE ok=0 reason=xvoiced_export_missing kernelOrd=0x%X xamSubmit=0x%08X",
          V700_KERNEL_XVOICED_SUBMIT_ORD,g_v700XamSubmit);
        return FALSE;
    }

    if(!g_v702HeadsetReal){
        SendLine(s,
          "V702_RESOLVE ok=0 reason=headset_export_missing kernelOrd=0x%X xamHeadset=0x%08X",
          V702_KERNEL_XVOICED_HEADSET_ORD,g_v702XamHeadset);
        return FALSE;
    }

    DWORD start=0,end=0;
    DWORD stub=V700FindThunkDerived(s,g_v700Real,g_v700XamSubmit,&start,&end);
    if(!stub){
        SendLine(s,
          "V700_RESOLVE ok=0 reason=xam_thunk_not_found real=0x%08X xamSubmit=0x%08X",
          g_v700Real,g_v700XamSubmit);
        return FALSE;
    }

    DWORD hStart=(stub>0x00020000)?stub-0x00020000:start;
    DWORD hEnd=stub+0x00020000;
    if(hStart<start) hStart=start;
    if(hEnd>end || hEnd<stub) hEnd=end;
    DWORD headsetStub=V701FindThunkFast(s,"HEADSET_NEAR_SUBMIT",hStart,hEnd,g_v702HeadsetReal);
    if(!headsetStub)
        headsetStub=V701FindThunkFast(s,"HEADSET_SYSTEM_REGION",start,end,g_v702HeadsetReal);
    if(!headsetStub){
        SendLine(s,
          "V702_RESOLVE ok=0 reason=xam_headset_thunk_not_found real=0x%08X xamHeadset=0x%08X",
          g_v702HeadsetReal,g_v702XamHeadset);
        return FALSE;
    }

    DWORD rebuilt=0;
    if(!V701RebuildGatewayTarget(stub,&rebuilt) || rebuilt!=g_v700Real){
        SendLine(s,
          "V700_RESOLVE ok=0 reason=thunk_validation_failed stub=0x%08X rebuilt=0x%08X real=0x%08X",
          stub,rebuilt,g_v700Real);
        return FALSE;
    }

    DWORD hRebuilt=0;
    if(!V701RebuildGatewayTarget(headsetStub,&hRebuilt) || hRebuilt!=g_v702HeadsetReal){
        SendLine(s,
          "V702_RESOLVE ok=0 reason=headset_thunk_validation_failed stub=0x%08X rebuilt=0x%08X real=0x%08X",
          headsetStub,hRebuilt,g_v702HeadsetReal);
        return FALSE;
    }

    g_v700Stub=stub;
    g_v702HeadsetStub=headsetStub;
    g_v700ScanStart=start;
    g_v700ScanEnd=end;

    SendLine(s,
      "V700_RESOLVE ok=1 kernelOrd=0x%X real=0x%08X source=%s xamOrd=0x%X xamSubmit=0x%08X scan=0x%08X-0x%08X stub=0x%08X",
      V700_KERNEL_XVOICED_SUBMIT_ORD,
      g_v700Real,
      g_v700UsedLegacyRealFallback?"LEGACY_FALLBACK":"EXPORT",
      V700_XAM_VOICE_SUBMIT_ORD,
      g_v700XamSubmit,
      g_v700ScanStart,
      g_v700ScanEnd,
      g_v700Stub);
    SendLine(s,
      "V702_HEADSET_RESOLVE ok=1 kernelOrd=0x%X real=0x%08X xamOrd=0x%X xamHeadset=0x%08X stub=0x%08X virtual=READY",
      V702_KERNEL_XVOICED_HEADSET_ORD,g_v702HeadsetReal,
      V702_XAM_VOICE_HEADSET_ORD,g_v702XamHeadset,g_v702HeadsetStub);
    return TRUE;
}

static BOOL V700Install(SOCKET s)
{
    if(g_v700Installed){
        SendLine(s,"V700_INSTALL ok=1 already=1 stub=0x%08X real=0x%08X",g_v700Stub,g_v700Real);
        return TRUE;
    }

    if(!V700ResolveAndLocate(s)){
        SendLine(s,"V700_INSTALL ok=0 reason=resolver_failed");
        return FALSE;
    }

    if(!V700Readable(g_v700Stub,16)){
        SendLine(s,"V700_INSTALL ok=0 reason=stub_unreadable stub=0x%08X",g_v700Stub);
        return FALSE;
    }
    if(!V700Readable(g_v702HeadsetStub,16)){
        SendLine(s,"V702_INSTALL ok=0 reason=headset_stub_unreadable stub=0x%08X",g_v702HeadsetStub);
        return FALSE;
    }

    for(DWORD i=0;i<4;i++){
        g_v700OrigStub[i]=*(volatile DWORD*)(g_v700Stub+i*4);
        g_v702OrigHeadsetStub[i]=*(volatile DWORD*)(g_v702HeadsetStub+i*4);
    }

    DWORD hook=(DWORD)&Hook_V700_SystemSubmit;
    DWORD hi=(hook+0x8000)>>16;
    SHORT lo=(SHORT)(hook&0xFFFF);

    DWORD patch[4];
    patch[0]=0x3D600000|(hi&0xFFFF);
    patch[1]=0x396B0000|((WORD)lo);
    patch[2]=0x7D6903A6;
    patch[3]=0x4E800420;

    memcpy((void*)g_v700Stub,patch,16);
    V645FlushCode(g_v700Stub,16);

    DWORD hHook=(DWORD)&Hook_V702_HeadsetPresent;
    DWORD hHi=(hHook+0x8000)>>16;
    SHORT hLo=(SHORT)(hHook&0xFFFF);
    DWORD hPatch[4];
    hPatch[0]=0x3D600000|(hHi&0xFFFF);
    hPatch[1]=0x396B0000|((WORD)hLo);
    hPatch[2]=0x7D6903A6;
    hPatch[3]=0x4E800420;
    memcpy((void*)g_v702HeadsetStub,hPatch,16);
    V645FlushCode(g_v702HeadsetStub,16);

    g_v700Enabled=0;
    g_v700TonePhase=0;
    V700ResetDiscovery();
    V700ResetAudio();

    g_v702PhysicalHeadset=0;
    g_v702HeadsetQueries=0;
    g_v702VirtualHeadsetHits=0;
    g_v702VirtualHeadset=1;
    g_v700Installed=TRUE;

    SendLine(s,
      "V700_INSTALL ok=1 UNIVERSAL_SYSTEM_MIC=1 stub=0x%08X real=0x%08X hook=0x%08X lane=AUTO refill=AUTO",
      g_v700Stub,g_v700Real,hook);
    SendLine(s,
      "V702_VIRTUAL_HEADSET ok=1 enabled=1 headsetStub=0x%08X headsetReal=0x%08X directFallback=1 voiceGate=PC_RMS_PLUS_STALE_SILENCE",
      g_v702HeadsetStub,g_v702HeadsetReal);
    return TRUE;
}

static void V700StartMode(SOCKET s,LONG mode)
{
    if(!g_v700Installed){
        SendLine(s,"V700_START ok=0 reason=not_installed");
        return;
    }

    g_v700Enabled=0;
    g_v700DetectedRefills=0;
    g_v700InjectedWrites=0;
    g_v700InjectedSlots=0;
    g_v700NoAudioWrites=0;
    g_v702DirectVirtualWrites=0;
    g_v702ForcedSubmitSuccess=0;
    g_v702SilenceFallbackWrites=0;
    g_v700TonePhase=0;

    g_v700Enabled=mode;

    SendLine(s,
      "V700_START ok=1 mode=%ld strategy=DYNAMIC_LANE_POSTCALL_BASELINE_REFILL_EDGE waitingForLane=%u",
      mode,g_v700LaneLocked?0:1);
}

static void V700Stop(SOCKET s)
{
    LONG old=g_v700Enabled;
    g_v700Enabled=0;

    LONG refillAvg=g_v700RefillAgeCount ? g_v700RefillAgeSum/g_v700RefillAgeCount : 0;
    LONG reuseAvg=g_v700ReuseCount ? g_v700ReuseAgeSum/g_v700ReuseCount : 0;

    SendLine(s,
      "V700_STOP oldMode=%ld locked=%ld lane=%u bytes=0x%X f0c=0x%X submits=%ld slots=%ld refills=%ld refillAvg=%ld reuseAvg=%ld injectedWrites=%ld injectedSlots=%ld noAudioWrites=%ld rxFrames=%ld audioDrops=%ld stale=%ld unexpectedGeometry=%ld physicalHeadset=%ld headsetQueries=%ld virtualHits=%ld directVirtualWrites=%ld forcedSubmitSuccess=%ld silenceFallbackWrites=%ld lastSubmitRc=0x%08X",
      old,
      g_v700LaneLocked,
      g_v700Lane,
      g_v700PacketBytes,
      g_v700Field0C,
      g_v700SubmitCount,
      g_v700SlotCount,
      g_v700DetectedRefills,
      refillAvg,
      reuseAvg,
      g_v700InjectedWrites,
      g_v700InjectedSlots,
      g_v700NoAudioWrites,
      g_v700AudioRx,
      g_v700AudioDrops,
      g_v700StaleDeactivations,
      g_v700UnexpectedGeometry,
      g_v702PhysicalHeadset,
      g_v702HeadsetQueries,
      g_v702VirtualHeadsetHits,
      g_v702DirectVirtualWrites,
      g_v702ForcedSubmitSuccess,
      g_v702SilenceFallbackWrites,
      g_v702LastSubmitResult);
}

static void V700Poll()
{
    LONG mode=g_v700Enabled;
    if(mode==0 || !g_v700LaneLocked) return;

    // v7.04 performance: the virtual/no-wire path writes the newest frame
    // directly inside Hook_V700_SystemSubmit.  There is no controller DMA refill
    // edge to discover, so the old 1 ms scan/hash loop is pure CPU overhead.
    // Keep polling only for a real physical headset, where asynchronous refill
    // detection is still required.
    if(g_v702VirtualHeadset && !g_v702PhysicalHeadset) return;

    LONG n=g_v700SlotCount;
    if(n>V700_MAX_SLOTS) n=V700_MAX_SLOTS;

    DWORD now=GetTickCount();
    DWORD staleMs=V700AdaptiveStaleMs();

    for(LONG i=0;i<n;i++){
        V700_SLOT* slot=&g_v700Slots[i];
        if(!slot->active || !slot->submitTick || !slot->pcm) continue;

        LONG gen=slot->generation;
        DWORD age=now-slot->submitTick;

        // This is a stale-safety bound, not the old refill timing window. It is
        // learned from buffer reuse once enough samples exist.
        if(age>staleMs){
            slot->active=0;
            InterlockedIncrement(&g_v700StaleDeactivations);
            continue;
        }

        if(!V700Readable(slot->pcm,V700_FRAME_BYTES)){
            slot->active=0;
            InterlockedIncrement(&g_v700StaleDeactivations);
            continue;
        }

        DWORD h=V700Hash(slot->pcm);

        if(!slot->active || slot->generation!=gen) continue;

        if(h!=slot->lastObservedHash){
            if(slot->firstChangeAge==0){
                LONG first=(LONG)age;
                slot->firstChangeAge=first;
                InterlockedIncrement(&g_v700DetectedRefills);
                InterlockedIncrement(&g_v700RefillAgeCount);
                InterlockedExchangeAdd(&g_v700RefillAgeSum,first);
                V700UpdateMinMax(&g_v700RefillAgeMin,&g_v700RefillAgeMax,first);
            }
            slot->lastObservedChangeAge=(LONG)age;
            slot->lastObservedHash=h;
        }

        if(slot->firstChangeAge==0) continue;

        BOOL wrote=FALSE;
        if(mode==1){
            V700WriteTone(slot->pcm);
            wrote=TRUE;
        }else if(mode==2){
            if(!V700CopyLatestAudio(slot->pcm))
                InterlockedIncrement(&g_v700NoAudioWrites);
            // V700CopyLatestAudio writes explicit silence on underflow/stale,
            // so this still counts as an intentional overwrite.
            wrote=TRUE;
        }

        if(wrote){
            if(slot->postRefillWrites==0)
                InterlockedIncrement(&g_v700InjectedSlots);

            InterlockedIncrement(&slot->postRefillWrites);
            InterlockedIncrement(&g_v700InjectedWrites);

            // Prevent our own write from being counted as a new refill edge.
            slot->lastObservedHash=V700Hash(slot->pcm);
        }
    }
}

static void V700ReportDiscovery(SOCKET s)
{
    SendLine(s,
      "V700_DISCOVERY locked=%ld lane=%u bytes=0x%X f0c=0x%X candidates=%ld unexpectedGeometry=%ld",
      g_v700LaneLocked,g_v700Lane,g_v700PacketBytes,g_v700Field0C,
      g_v700CandidateCount,g_v700UnexpectedGeometry);

    LONG n=g_v700CandidateCount;
    if(n>V700_MAX_CANDIDATES) n=V700_MAX_CANDIDATES;
    for(LONG i=0;i<n;i++){
        V700_CANDIDATE* c=&g_v700Candidates[i];
        if(!c->active) continue;
        SendLine(s,
          "V700_CAND index=%ld lane=%u bytes=0x%X f0c=0x%X hits=%ld age=%u",
          i,c->lane,c->bytes,c->f0c,c->hits,(DWORD)(GetTickCount()-c->firstTick));
    }

    LONG refillAvg=g_v700RefillAgeCount ? g_v700RefillAgeSum/g_v700RefillAgeCount : 0;
    LONG reuseAvg=g_v700ReuseCount ? g_v700ReuseAgeSum/g_v700ReuseCount : 0;
    SendLine(s,
      "V700_TIMING refillCount=%ld refillMin=%ld refillAvg=%ld refillMax=%ld reuseCount=%ld reuseMin=%ld reuseAvg=%ld reuseMax=%ld staleNow=%u",
      g_v700RefillAgeCount,
      g_v700RefillAgeCount?g_v700RefillAgeMin:0,
      refillAvg,
      g_v700RefillAgeMax,
      g_v700ReuseCount,
      g_v700ReuseCount?g_v700ReuseAgeMin:0,
      reuseAvg,
      g_v700ReuseAgeMax,
      V700AdaptiveStaleMs());
}

static void V700Rediscover(SOCKET s)
{
    LONG mode=g_v700Enabled;
    g_v700Enabled=0;
    V700ResetDiscovery();
    g_v700Enabled=mode;
    SendLine(s,"V700_REDISCOVER ok=1 mode=%ld lane=AUTO refill=AUTO",mode);
}

static void V700Restore(SOCKET s)
{
    g_v700Enabled=0;
    g_v702VirtualHeadset=0;

    if(!g_v700Installed){
        SendLine(s,"V700_RESTORE already=0");
        return;
    }

    BOOL submitOk=(g_v700Stub && V700Readable(g_v700Stub,16));
    BOOL headsetOk=(g_v702HeadsetStub && V700Readable(g_v702HeadsetStub,16));
    if(submitOk)
        memcpy((void*)g_v700Stub,g_v700OrigStub,16);
    if(headsetOk)
        memcpy((void*)g_v702HeadsetStub,g_v702OrigHeadsetStub,16);
    if(submitOk) V645FlushCode(g_v700Stub,16);
    if(headsetOk) V645FlushCode(g_v702HeadsetStub,16);

    if(submitOk && headsetOk){
        g_v700Installed=FALSE;
        SendLine(s,"V700_RESTORE ok=1 stub=0x%08X real=0x%08X headsetStub=0x%08X",g_v700Stub,g_v700Real,g_v702HeadsetStub);
    }else{
        SendLine(s,"V700_RESTORE ok=0 submitOk=%u headsetOk=%u",submitOk?1:0,headsetOk?1:0);
    }
}

void SendXexScopeProbe(SOCKET s)
{
    g_diagSocket=s;
    ResolveSweeps();

    SendAll(s,"VOICE_DECODE_UNIVERSAL_SYSTEM_MIC_V7_04 HELLO\r\n");
    SendAll(s,"TRANSPORT EXACT_NOVA_V14_UNCHANGED\r\n");
    SendAll(s,"MODE UNIVERSAL_DYNAMIC_XVOICED_VIRTUAL_HEADSET_V7_04_XBOX_LISTENER_PERF\r\n");
    SendAll(s,"READY commands=O_install K_tone S_live T_stop D_rediscover P_report Q_restore_quit AUDIO=A570+640bytes_PCM16BE VIRTUAL_HEADSET=AUTO NETWORK=XBOX_LISTEN_36000\r\n");

    BOOL noDelay=TRUE;
    setsockopt(s,IPPROTO_TCP,TCP_NODELAY,(const char*)&noDelay,sizeof(noDelay));

    u_long nb=1;
    ioctlsocket(s,FIONBIO,&nb);

    BYTE buf[4096];
    BYTE audio[V700_FRAME_BYTES];
    DWORD audioState=0;
    DWORD audioPos=0;
    DWORD last=GetTickCount();

    for(;;){
        int n=recv(s,(char*)buf,sizeof(buf),0);
        if(n==0){
            g_diagSocket=INVALID_SOCKET;
            return;
        }
        if(n<0){
            int err=WSAGetLastError();
            if(err!=WSAEWOULDBLOCK){
                g_diagSocket=INVALID_SOCKET;
                return;
            }
        }
        if(n>0){
            for(int i=0;i<n;i++){
                BYTE b=buf[i];

                if(audioState==2){
                    audio[audioPos++]=b;
                    if(audioPos==V700_FRAME_BYTES){
                        V700PushAudio(audio);
                        audioPos=0;
                        audioState=0;
                    }
                    continue;
                }

                if(audioState==1){
                    if(b==0x70){
                        audioState=2;
                        audioPos=0;
                        continue;
                    }
                    audioState=0;
                }

                if(b==0xA5){
                    audioState=1;
                    continue;
                }

                char ch=(char)toupper((unsigned char)b);
                if(ch=='O'){
                    V700Install(s);
                }else if(ch=='K'){
                    V700StartMode(s,1);
                }else if(ch=='S'){
                    V700StartMode(s,2);
                }else if(ch=='T'){
                    V700Stop(s);
                }else if(ch=='D'){
                    V700Rediscover(s);
                }else if(ch=='P'){
                    V700ReportDiscovery(s);
                }else if(ch=='Q'){
                    V700Stop(s);
                    V700Restore(s);
                    SendAll(s,"SAFE_TO_UNLOAD\r\n");
                    g_diagSocket=INVALID_SOCKET;
                    return;
                }
            }
        }

        V700Poll();

        DWORD now=GetTickCount();
        if(now-last>=1000){
            last=now;
            LONG refillAvg=g_v700RefillAgeCount ? g_v700RefillAgeSum/g_v700RefillAgeCount : 0;
            LONG reuseAvg=g_v700ReuseCount ? g_v700ReuseAgeSum/g_v700ReuseCount : 0;
            SendLine(s,
              "V700_STATUS installed=%u mode=%ld locked=%ld lane=%u bytes=0x%X f0c=0x%X candidates=%ld submits=%ld slots=%ld refills=%ld refillAvg=%ld reuseAvg=%ld injectedWrites=%ld injectedSlots=%ld rxFrames=%ld drops=%ld unexpectedGeometry=%ld staleMs=%u physicalHeadset=%ld virtualHeadset=%ld headsetQueries=%ld directVirtualWrites=%ld forcedSubmitSuccess=%ld silenceFallbackWrites=%ld lastSubmitRc=0x%08X perf=%s",
              g_v700Installed?1:0,
              g_v700Enabled,
              g_v700LaneLocked,
              g_v700Lane,
              g_v700PacketBytes,
              g_v700Field0C,
              g_v700CandidateCount,
              g_v700SubmitCount,
              g_v700SlotCount,
              g_v700DetectedRefills,
              refillAvg,
              reuseAvg,
              g_v700InjectedWrites,
              g_v700InjectedSlots,
              g_v700AudioRx,
              g_v700AudioDrops,
              g_v700UnexpectedGeometry,
              V700AdaptiveStaleMs(),
              g_v702PhysicalHeadset,
              g_v702VirtualHeadset,
              g_v702HeadsetQueries,
              g_v702DirectVirtualWrites,
              g_v702ForcedSubmitSuccess,
              g_v702SilenceFallbackWrites,
              g_v702LastSubmitResult,
              (g_v702VirtualHeadset && !g_v702PhysicalHeadset)?"VIRTUAL_DIRECT_NO_POLL":"WIRED_REFILL_POLL");
        }

        // No-wire mode only needs to service a 20 ms network frame cadence.
        // Halve the wake-up rate there; preserve the 1 ms wired-headset poll.
        Sleep((g_v702VirtualHeadset && !g_v702PhysicalHeadset)?2:1);
    }
}
// BUILD_MARKER: V7_04_XBOX_LISTENER_VIRTUAL_DIRECT_PERF_NO_POLL
