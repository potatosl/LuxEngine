// Loads LuxForge.dll, starts The Forge on a hidden window and prints what it created.
// Exit code 0 = Direct3D 12 renderer came up, 1 = failed.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <psapi.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "LuxForgeApi.h"

static double WorkingSetMb() {
    PROCESS_MEMORY_COUNTERS pmc = {};
    GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
    return static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
}

int main(int argc, char** argv) {
    // Stress mode: LuxForgeProbe.exe --stress <seconds> creates and destroys the renderer in a
    // loop and reports failures and memory growth. Exit code 0 only if every cycle succeeded.
    const bool stress = argc >= 3 && std::strcmp(argv[1], "--stress") == 0;
    const ULONGLONG stressMs = stress ? static_cast<ULONGLONG>(std::atoll(argv[2])) * 1000ull : 0;
    HMODULE dll = LoadLibraryW(L"LuxForge.dll");
    if (!dll) {
        std::printf("FAIL: LuxForge.dll could not be loaded (error %lu)\n", GetLastError());
        return 1;
    }
    auto getApi = reinterpret_cast<LuxForgeGetApiFn>(GetProcAddress(dll, "LuxForgeGetApi"));
    const LuxForgeApi* api = getApi ? getApi() : nullptr;
    if (!api || api->version != LUX_FORGE_API_VERSION) {
        std::printf("FAIL: LuxForgeGetApi missing or wrong version\n");
        return 1;
    }

    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"LuxForgeProbe";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"probe", WS_OVERLAPPEDWINDOW, 0, 0, 640, 360,
                                nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) {
        std::printf("FAIL: could not create probe window\n");
        return 1;
    }

    if (stress) {
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        const ULONGLONG start = GetTickCount64();
        unsigned long cycles = 0, failures = 0;
        const double startMb = WorkingSetMb();
        double peakMb = startMb;
        ULONGLONG nextReport = start + 60000;
        LuxForgeInitDesc sd = {};
        sd.nativeWindow = hwnd;
        sd.width = 640;
        sd.height = 360;
        sd.framesInFlight = 2;
        while (GetTickCount64() - start < stressMs) {
            char err[512] = {};
            LuxForgeDevice* d = api->create(&sd, err, sizeof(err));
            if (!d) {
                ++failures;
                std::printf("STRESS FAIL cycle %lu: %s\n", cycles, err);
            } else {
                // Present a few frames per device so the loop exercises the swap chain too.
                for (int f = 0; f < 10; ++f) {
                    MSG msg;
                    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
                        DispatchMessageW(&msg);
                    const int start = api->beginFrame(d);
                    if (start == LUX_FORGE_FRAME_ERROR ||
                        (start == LUX_FORGE_FRAME_READY &&
                         (!api->clear(d, (f % 3) / 2.f, .5f, 1.f - (f % 3) / 2.f, 1) ||
                          !api->endFrame(d)))) {
                        ++failures;
                        std::printf("STRESS FRAME FAIL cycle %lu: %s\n", cycles, api->lastError(d));
                        break;
                    }
                }
                api->destroy(d);
            }
            ++cycles;
            const double mb = WorkingSetMb();
            if (mb > peakMb)
                peakMb = mb;
            if (GetTickCount64() >= nextReport) {
                nextReport += 60000;
                std::printf("STRESS %llus: cycles=%lu failures=%lu memory=%.1fMB (start %.1fMB)\n",
                            (GetTickCount64() - start) / 1000ull, cycles, failures, mb, startMb);
                std::fflush(stdout);
            }
        }
        std::printf("STRESS DONE: cycles=%lu failures=%lu memory %.1f -> %.1f MB (peak %.1f)\n",
                    cycles, failures, startMb, WorkingSetMb(), peakMb);
        return failures == 0 ? 0 : 1;
    }

    LuxForgeInitDesc desc = {};
    desc.nativeWindow = hwnd;
    desc.width = 640;
    desc.height = 360;
    desc.framesInFlight = 2;
    char error[512] = {};
    LuxForgeDevice* device = api->create(&desc, error, sizeof(error));
    if (!device) {
        std::printf("FAIL: create: %s\n", error);
        return 1;
    }
    LuxForgeInfo info = {};
    api->getInfo(device, &info);
    std::printf("OK: api=%s gpu=%s frames=%u\n", info.apiName, info.deviceName,
                info.framesInFlight);

    // Clear the window to a known colour, capture the presented frame and check the pixels.
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    int captured = 0;
    for (int frame = 0; frame < 30 && !captured; ++frame) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            DispatchMessageW(&msg);
        const bool want = frame >= 3;
        if (want)
            api->requestCapture(device);
        const int start = api->beginFrame(device);
        if (start == LUX_FORGE_FRAME_ERROR) {
            std::printf("FAIL: beginFrame: %s\n", api->lastError(device));
            return 1;
        }
        if (start == LUX_FORGE_FRAME_SKIPPED)
            continue;
        if (!api->clear(device, .2f, .4f, .6f, 1) || !api->endFrame(device)) {
            std::printf("FAIL: frame: %s\n", api->lastError(device));
            return 1;
        }
        if (want) {
            uint32_t w = 0, h = 0;
            size_t size = 0;
            api->readCapture(device, &w, &h, nullptr, 0, &size);
            std::vector<uint8_t> pixels(size);
            if (!api->readCapture(device, &w, &h, pixels.data(), pixels.size(), &size)) {
                std::printf("FAIL: readCapture: %s\n", api->lastError(device));
                return 1;
            }
            const uint8_t* p = &pixels[(static_cast<size_t>(h / 2) * w + w / 2) * 4];
            std::printf("CAPTURE %ux%u centre=(%u,%u,%u,%u) expected=(51,102,153,255)\n", w, h,
                        p[0], p[1], p[2], p[3]);
            auto within1 = [](int a, int b) { return a - b <= 1 && b - a <= 1; };
            if (!within1(p[0], 51) || !within1(p[1], 102) || !within1(p[2], 153) || p[3] != 255) {
                std::printf("FAIL: clear colour mismatch\n");
                return 1;
            }
            captured = 1;
        }
    }
    if (!captured) {
        std::printf("FAIL: no frame captured\n");
        return 1;
    }
    std::printf("OK: clear + present + capture, frames=%llu\n",
                static_cast<unsigned long long>(api->framesPresented(device)));
    api->destroy(device);
    std::printf("OK: destroyed cleanly\n");
    DestroyWindow(hwnd);
    FreeLibrary(dll);
    return 0;
}
