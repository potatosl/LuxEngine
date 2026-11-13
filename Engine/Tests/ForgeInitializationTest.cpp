#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <unknwn.h>
#include "Forge/LuxForgeApi.h"
#include <cstdio>
#include <cstring>
#include <unordered_map>

namespace {
using GetProcAddressFn = FARPROC(WINAPI*)(HMODULE, LPCSTR);
GetProcAddressFn g_GetProcAddress = nullptr;
unsigned g_FailedDeviceCalls = 0;
unsigned g_FailCreationStage = 0;
std::unordered_map<void*, unsigned> g_CreationStages;

HRESULT WINAPI FailCreateDevice(void* adapter, unsigned level, const GUID& iid, void** device) {
    bool fail = g_FailCreationStage == 0;
    if (g_FailCreationStage && device) {
        IUnknown* identity = nullptr;
        if (adapter && SUCCEEDED(reinterpret_cast<IUnknown*>(adapter)->QueryInterface(
                           __uuidof(IUnknown), reinterpret_cast<void**>(&identity)))) {
            fail = ++g_CreationStages[identity] == g_FailCreationStage;
            identity->Release();
        }
    }
    if (!fail) {
        using CreateDeviceFn = HRESULT(WINAPI*)(void*, unsigned, const GUID&, void**);
        const auto createDevice = reinterpret_cast<CreateDeviceFn>(
            g_GetProcAddress(GetModuleHandleW(L"d3d12.dll"), "D3D12CreateDevice"));
        return createDevice(adapter, level, iid, device);
    }
    ++g_FailedDeviceCalls;
    if (device)
        *device = nullptr;
    return static_cast<HRESULT>(0x887A0004L);
}

FARPROC WINAPI ResolveProcAddress(HMODULE module, LPCSTR name) {
    if (reinterpret_cast<ULONG_PTR>(name) > 0xffff && std::strcmp(name, "D3D12CreateDevice") == 0)
        return reinterpret_cast<FARPROC>(&FailCreateDevice);
    return g_GetProcAddress(module, name);
}

class DeviceCreationFailure {
public:
    explicit DeviceCreationFailure(HMODULE module) {
        auto* base = reinterpret_cast<unsigned char*>(module);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        const auto& imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (!imports.VirtualAddress)
            return;
        auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + imports.VirtualAddress);
        for (; descriptor->Name; ++descriptor) {
            if (!descriptor->OriginalFirstThunk)
                continue;
            auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->OriginalFirstThunk);
            auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->FirstThunk);
            for (; names->u1.AddressOfData; ++names, ++slots) {
                if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
                    continue;
                const auto* entry = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
                if (std::strcmp(reinterpret_cast<const char*>(entry->Name), "GetProcAddress") != 0)
                    continue;
                DWORD protection = 0;
                if (!VirtualProtect(&slots->u1.Function, sizeof(slots->u1.Function), PAGE_READWRITE, &protection))
                    return;
                slot_ = &slots->u1.Function;
                original_ = *slot_;
                g_GetProcAddress = reinterpret_cast<GetProcAddressFn>(original_);
                *slot_ = reinterpret_cast<ULONG_PTR>(&ResolveProcAddress);
                VirtualProtect(slot_, sizeof(*slot_), protection, &protection);
                return;
            }
        }
    }

    ~DeviceCreationFailure() {
        if (slot_) {
            DWORD protection = 0;
            if (VirtualProtect(slot_, sizeof(*slot_), PAGE_READWRITE, &protection)) {
                *slot_ = original_;
                VirtualProtect(slot_, sizeof(*slot_), protection, &protection);
            }
        }
    }

    explicit operator bool() const { return slot_ != nullptr; }

private:
    ULONG_PTR* slot_ = nullptr;
    ULONG_PTR original_ = 0;
};
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    HMODULE module = LoadLibraryW(L"LuxForge.dll");
    if (!module)
        return 1;
    const auto getApi = reinterpret_cast<LuxForgeGetApiFn>(GetProcAddress(module, "LuxForgeGetApi"));
    const auto* api = getApi ? getApi() : nullptr;
    if (!api || api->version != LUX_FORGE_API_VERSION)
        return 1;
    HWND window = CreateWindowExW(0, L"STATIC", L"Forge initialization regression", WS_OVERLAPPEDWINDOW,
                                  0, 0, 320, 240, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!window)
        return 1;
    LuxForgeInitDesc desc = {};
    desc.nativeWindow = window;
    desc.windowType = LUX_FORGE_WINDOW_WIN32;
    desc.width = 320;
    desc.height = 240;
    bool passed = true;
    {
        DeviceCreationFailure failure(module);
        if (!failure)
            return 1;
        for (int attempt = 0; attempt < 2; ++attempt) {
            char error[512] = {};
            auto* device = api->create(&desc, error, sizeof(error));
            passed = passed && !device && std::strstr(error, "HRESULT 0x887A0004") &&
                     std::strstr(error, "D3D12 device creation failed");
            std::printf("Injected failure %d: %s\n", attempt + 1, error);
            if (device)
                api->destroy(device);
        }
        for (unsigned stage = 1; stage <= 3; ++stage) {
            g_FailCreationStage = stage;
            g_CreationStages.clear();
            const auto previousFailures = g_FailedDeviceCalls;
            char error[512] = {};
            auto* device = api->create(&desc, error, sizeof(error));
            passed = passed && !device && g_FailedDeviceCalls > previousFailures &&
                     std::strstr(error, "HRESULT 0x887A0004") &&
                     std::strstr(error, "D3D12 device creation failed");
            std::printf("Injected creation stage %u: %s\n", stage, error);
            if (device)
                api->destroy(device);
        }
    }
    passed = passed && g_FailedDeviceCalls >= 8;
    char error[512] = {};
    auto* device = api->create(&desc, error, sizeof(error));
    if (!device) {
        std::printf("Recovery failed: %s\n", error);
        passed = false;
    } else {
        LuxForgeInfo info = {};
        api->getInfo(device, &info);
        std::printf("Recovered GPU: %s (%s)\n", info.deviceName, info.apiName);
        api->destroy(device);
    }
    DestroyWindow(window);
    FreeLibrary(module);
    std::printf("%s: D3D12 initialization failure and recovery\n", passed ? "PASS" : "FAIL");
    return passed ? 0 : 1;
}
