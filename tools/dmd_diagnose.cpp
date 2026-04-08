#include <iostream>
#include <cstdlib>
#include <cstring>
#include <windows.h>
#include <setupapi.h>
#include <initguid.h>
#include "alp.h"

#pragma comment(lib, "setupapi.lib")

using namespace std;

// ===== DeviceIoControl hook to intercept ALP SDK's USB communication =====

static int g_ioctlCount = 0;
static BOOL (WINAPI *g_realDeviceIoControl)(
    HANDLE, DWORD, LPVOID, DWORD, LPVOID, DWORD, LPDWORD, LPOVERLAPPED) = NULL;

BOOL WINAPI HookedDeviceIoControl(
    HANDLE hDevice, DWORD dwIoControlCode,
    LPVOID lpInBuffer, DWORD nInBufferSize,
    LPVOID lpOutBuffer, DWORD nOutBufferSize,
    LPDWORD lpBytesReturned, LPOVERLAPPED lpOverlapped)
{
    g_ioctlCount++;

    // Call the real function
    BOOL result = g_realDeviceIoControl(
        hDevice, dwIoControlCode, lpInBuffer, nInBufferSize,
        lpOutBuffer, nOutBufferSize, lpBytesReturned, lpOverlapped);
    DWORD err = GetLastError();

    // Log it
    printf("  IOCTL #%d: code=0x%08X inSize=%4lu outSize=%4lu -> %s",
           g_ioctlCount, dwIoControlCode, nInBufferSize, nOutBufferSize,
           result ? "OK" : "FAIL");
    if (lpBytesReturned)
        printf(" returned=%lu", *lpBytesReturned);
    if (!result)
        printf(" err=%lu", err);
    printf("\n");

    // Print first bytes of input/output for interesting calls
    if (nInBufferSize > 0 && nInBufferSize <= 64 && lpInBuffer) {
        printf("         IN:  ");
        for (DWORD i = 0; i < min(nInBufferSize, (DWORD)24); i++)
            printf("%02X ", ((BYTE*)lpInBuffer)[i]);
        printf("\n");
    }
    if (result && lpBytesReturned && *lpBytesReturned > 0 &&
        *lpBytesReturned <= 256 && lpOutBuffer) {
        printf("         OUT: ");
        for (DWORD i = 0; i < min(*lpBytesReturned, (DWORD)24); i++)
            printf("%02X ", ((BYTE*)lpOutBuffer)[i]);
        printf("\n");
    }

    SetLastError(err); // Restore error code
    return result;
}

// Patch IAT of a loaded DLL to redirect DeviceIoControl calls
bool hookIAT(HMODULE hModule) {
    // Get the real DeviceIoControl address
    g_realDeviceIoControl = (decltype(g_realDeviceIoControl))
        GetProcAddress(GetModuleHandleA("kernel32.dll"), "DeviceIoControl");
    if (!g_realDeviceIoControl) {
        cout << "  ERROR: Cannot find DeviceIoControl" << endl;
        return false;
    }

    // Parse PE headers to find IAT
    BYTE* base = (BYTE*)hModule;
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    IMAGE_IMPORT_DESCRIPTOR* imports = (IMAGE_IMPORT_DESCRIPTOR*)(base +
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);

    for (; imports->Name; imports++) {
        const char* dllName = (const char*)(base + imports->Name);
        // Look for KERNEL32.dll imports
        if (_stricmp(dllName, "KERNEL32.dll") != 0 &&
            _stricmp(dllName, "KERNEL32.DLL") != 0 &&
            _stricmp(dllName, "kernel32.dll") != 0)
            continue;

        IMAGE_THUNK_DATA* thunk = (IMAGE_THUNK_DATA*)(base + imports->FirstThunk);
        IMAGE_THUNK_DATA* origThunk = (IMAGE_THUNK_DATA*)(base + imports->OriginalFirstThunk);

        for (; thunk->u1.Function; thunk++, origThunk++) {
            // Check if this is DeviceIoControl
            FARPROC* funcPtr = (FARPROC*)&thunk->u1.Function;
            if (*funcPtr == (FARPROC)g_realDeviceIoControl) {
                // Patch it
                DWORD oldProtect;
                VirtualProtect(funcPtr, sizeof(FARPROC), PAGE_READWRITE, &oldProtect);
                *funcPtr = (FARPROC)HookedDeviceIoControl;
                VirtualProtect(funcPtr, sizeof(FARPROC), oldProtect, &oldProtect);
                cout << "  Hooked DeviceIoControl in " << dllName << " IAT" << endl;
                return true;
            }
        }
    }

    cout << "  WARNING: DeviceIoControl not found in IAT" << endl;
    return false;
}

const char* alpErrorName(long code) {
    switch (code) {
        case 0:    return "ALP_OK";
        case 1001: return "ALP_NOT_ONLINE";
        case 1010: return "ALP_ERROR_INIT";
        case 1011: return "ALP_ERROR_COMM";
        case 1013: return "ALP_NOT_CONFIGURED";
        case 1014: return "ALP_LOADER_VERSION";
        default:   return "OTHER";
    }
}

void queryDeviceInfo(ALP_ID nDevId) {
    long val = 0, ret = 0;
    ret = AlpDevInquire(nDevId, ALP_VERSION, &val);
    cout << "  ALP_VERSION:    " << val << " (ret=" << ret << ")" << endl;
    ret = AlpDevInquire(nDevId, ALP_DEV_DMDTYPE, &val);
    cout << "  ALP_DEV_DMDTYPE:" << val << " (ret=" << ret << ")" << endl;
}

void usage() {
    cout << "Usage: dmd_diagnose.exe [mode]" << endl;
    cout << "  probe    - USB probe only (safe, no ALP calls)" << endl;
    cout << "  trace    - Hook DeviceIoControl + run AlpDevAlloc (shows USB traffic)" << endl;
    cout << "  alloc D I - AlpDevAlloc(DeviceNum=D, InitFlag=I)" << endl;
}

void doProbe() {
    cout << "=== USB DEVICE PROBE ===" << endl;
    GUID usbGuid = {0xA5DCBF10, 0x6530, 0x11D2,
                    {0x90, 0x1F, 0x00, 0xC0, 0x4F, 0xB9, 0x51, 0xED}};
    HDEVINFO devInfo = SetupDiGetClassDevs(&usbGuid, NULL, NULL,
                                           DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devInfo == INVALID_HANDLE_VALUE) { cout << "  FAILED" << endl; return; }

    SP_DEVINFO_DATA devData;
    devData.cbSize = sizeof(SP_DEVINFO_DATA);
    for (DWORD i = 0; SetupDiEnumDeviceInfo(devInfo, i, &devData); i++) {
        char hwId[512] = {0};
        if (!SetupDiGetDeviceRegistryPropertyA(devInfo, &devData, SPDRP_HARDWAREID,
                                               NULL, (BYTE*)hwId, sizeof(hwId), NULL))
            continue;
        if (!strstr(hwId, "VID_132F")) continue;
        char desc[256] = {0};
        SetupDiGetDeviceRegistryPropertyA(devInfo, &devData, SPDRP_DEVICEDESC,
                                          NULL, (BYTE*)desc, sizeof(desc), NULL);
        cout << "  Found: " << desc << endl;
        cout << "  HwID:  " << hwId << endl;
    }
    SetupDiDestroyDeviceInfoList(devInfo);
}

void doTrace() {
    cout << "=== TRACED AlpDevAlloc ===" << endl;
    cout << "  Hooking DeviceIoControl to intercept USB traffic..." << endl;

    // The DLL is already loaded (we're linked against it).
    // Find it and hook its IAT.
    HMODULE hAlpDll = GetModuleHandleA("alpD41.dll");
    if (!hAlpDll) {
        cout << "  ERROR: alpD41.dll not loaded!" << endl;
        return;
    }
    cout << "  alpD41.dll loaded at: 0x" << hex << (uintptr_t)hAlpDll << dec << endl;

    if (!hookIAT(hAlpDll)) {
        cout << "  Proceeding without hook (will still show ALP return code)" << endl;
    }

    cout << endl << "  Calling AlpDevAlloc(ALP_DEFAULT, ALP_DEFAULT)..." << endl;
    cout << "  --- DeviceIoControl trace begins ---" << endl;

    ALP_ID nDevId = 0;
    g_ioctlCount = 0;
    long ret = AlpDevAlloc(ALP_DEFAULT, ALP_DEFAULT, &nDevId);

    cout << "  --- DeviceIoControl trace ends ---" << endl;
    cout << "  Total IOCTLs: " << g_ioctlCount << endl;
    cout << "  AlpDevAlloc returned: " << ret << " = " << alpErrorName(ret) << endl;

    if (ret == ALP_OK) {
        cout << "  >>> SUCCESS! Device ID: " << nDevId << " <<<" << endl;
        queryDeviceInfo(nDevId);
        AlpDevHalt(nDevId);
        AlpDevFree(nDevId);
    }
}

int main(int argc, char* argv[]) {
    cout << "DMD DIAGNOSTIC TOOL v4 (with IOCTL tracing)" << endl;
    cout << "=============================================" << endl;

    if (argc < 2) { usage(); return 0; }

    string mode = argv[1];
    if (mode == "probe") {
        doProbe();
    } else if (mode == "trace") {
        doTrace();
    } else if (mode == "alloc" && argc >= 4) {
        ALP_ID nDevId = 0;
        long devNum = atol(argv[2]);
        long initFlag = atol(argv[3]);
        cout << "AlpDevAlloc(" << devNum << ", " << initFlag << ")..." << endl;
        long ret = AlpDevAlloc(devNum, initFlag, &nDevId);
        cout << "Return: " << ret << " = " << alpErrorName(ret) << endl;
        if (ret == ALP_OK) {
            queryDeviceInfo(nDevId);
            AlpDevHalt(nDevId);
            AlpDevFree(nDevId);
        }
    } else {
        usage();
    }
    return 0;
}
