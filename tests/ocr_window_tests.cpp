#include "bianlingo/ocr.hpp"
#include <dxgi1_6.h>
#include <iostream>
#include <string>
#include <windows.h>
#include <wrl/client.h>
using namespace bl;
LRESULT CALLBACK fixture(HWND h, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT paint;
        auto dc = BeginPaint(h, &paint);
        RECT r{};
        GetClientRect(h, &r);
        FillRect(dc, &r, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        auto font = CreateFontW(-28, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                                ANTIALIASED_QUALITY, 0, L"Arial");
        auto old = SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(170, 170, 255));
        const wchar_t *text = L"[Party] [Player]: need a healer for Deadmines";
        TextOutW(dc, 10, 15, text, int(wcslen(text)));
        SelectObject(dc, old);
        DeleteObject(font);
        EndPaint(h, &paint);
        return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}
void pump() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}
int game_probe(int argc, char **argv) {
    if (argc != 8)
        return 2;
    struct Target {
        DWORD pid;
        HWND window = nullptr;
        long long area = 0;
    } target{static_cast<DWORD>(std::stoul(argv[3]))};
    EnumWindows([](HWND window, LPARAM data) -> BOOL {
        auto &target = *reinterpret_cast<Target *>(data);
        DWORD pid = 0;
        GetWindowThreadProcessId(window, &pid);
        RECT bounds{};
        if (pid == target.pid && IsWindowVisible(window) && GetClientRect(window, &bounds)) {
            auto area = static_cast<long long>(bounds.right) * bounds.bottom;
            if (area > target.area) {
                target.window = window;
                target.area = area;
            }
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&target));
    if (!target.window)
        throw std::runtime_error("Game window not found");
    RECT client{};
    GetClientRect(target.window, &client);
    Rect region{std::stoi(argv[4]), std::stoi(argv[5]),
                std::stoi(argv[6]), std::stoi(argv[7])};
    auto capture = platform::make_ocr_capture();
    std::optional<Image> image;
    for (int i = 0; i < 50 && !image; ++i) {
        image = capture->grab(reinterpret_cast<uintptr_t>(target.window), region,
                              client.right, client.bottom, GetDpiForWindow(target.window));
        if (!image)
            Sleep(100);
    }
    if (!image)
        throw std::runtime_error("No game WGC frame available");
    auto recognizer = platform::make_ocr_recognizer(std::filesystem::u8path(argv[2]), "eng");
    auto lines = recognizer->recognize(*image);
    for (auto &line : lines)
        std::cout << line.confidence << " " << line.text << '\n';
    if (lines.empty())
        throw std::runtime_error("No game chat text recognized");
    std::cout << "GAME WGC OCR lines=" << lines.size()
              << " messages=" << ocr_messages(lines).size() << '\n';
    return 0;
}
int main(int argc, char **argv) {
    if (argc != 2 && !(argc == 8 && std::string(argv[1]) == "--game"))
        return 2;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (argc == 8) {
        try { return game_probe(argc, argv); }
        catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
    }
    HWND window = nullptr, occluder = nullptr;
    try {
        WNDCLASSW wc{};
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpfnWndProc = fixture;
        wc.lpszClassName = L"BianLingo.Ocr.CaptureFixture";
        RegisterClassW(&wc);
        int location_x = 80, location_y = 80;
        bool found_hdr = false;
        Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
        CreateDXGIFactory1(IID_PPV_ARGS(&factory));
        if (factory)
            for (UINT a = 0;; ++a) {
                Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
                if (factory->EnumAdapters1(a, &adapter) == DXGI_ERROR_NOT_FOUND)
                    break;
                for (UINT n = 0;; ++n) {
                    Microsoft::WRL::ComPtr<IDXGIOutput> output;
                    if (adapter->EnumOutputs(n, &output) == DXGI_ERROR_NOT_FOUND)
                        break;
                    Microsoft::WRL::ComPtr<IDXGIOutput6> modern;
                    output.As(&modern);
                    DXGI_OUTPUT_DESC1 desc{};
                    if (modern && SUCCEEDED(modern->GetDesc1(&desc)) && desc.AttachedToDesktop) {
                        bool hdr = desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 ||
                                   desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
                        if (!hdr && found_hdr)
                            continue;
                        location_x = desc.DesktopCoordinates.left + 80;
                        location_y = desc.DesktopCoordinates.top + 80;
                        found_hdr = hdr;
                    }
                }
            }
        window =
            CreateWindowExW(0, wc.lpszClassName, L"OCR capture fixture", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                            location_x, location_y, 950, 280, nullptr, nullptr, wc.hInstance, nullptr);
        UpdateWindow(window);
        RECT r{};
        GetClientRect(window, &r);
        int dpi = int(GetDpiForWindow(window));
        auto capture = platform::make_ocr_capture();
        auto begin = Clock::now();
        std::optional<Image> image;
        // A different application's/top-level window must never enter the crop.
        occluder =
            CreateWindowExW(WS_EX_TOPMOST, L"STATIC", L"Not game chat", WS_POPUP | WS_VISIBLE | SS_BLACKRECT,
                            location_x, location_y, 950, 280, nullptr, nullptr, wc.hInstance, nullptr);
        for (int i = 0; i < 50 && !image; ++i) {
            pump();
            image = capture->grab(reinterpret_cast<uintptr_t>(window), {0, 0, 900, 90},
                                  r.right, r.bottom, dpi);
            Sleep(100);
        }
        if (!image)
            throw std::runtime_error("No WGC frame available on this desktop");
        if (image->width != 900 || image->height != 90)
            throw std::runtime_error("WGC returned the wrong crop size");
        auto recognizer = platform::make_ocr_recognizer(std::filesystem::u8path(argv[1]), "eng");
        auto lines = recognizer->recognize(*image);
        auto messages = ocr_messages(lines);
        for (auto &line : lines)
            std::cout << line.confidence << " " << line.text << '\n';
        if (messages.empty() || messages[0].find("healer") == std::string::npos)
            throw std::runtime_error("WGC crop lost target text or included the overlay");
        SetWindowPos(window, nullptr, location_x, location_y, 1050, 320,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        pump();
        bool rejected = false;
        try {
            capture->grab(reinterpret_cast<uintptr_t>(window), {0, 0, 100, 100}, r.right, r.bottom, dpi);
        } catch (const std::exception &) {
            rejected = true;
        }
        if (!rejected)
            throw std::runtime_error("Resized client accepted stale crop");
        GetClientRect(window, &r);
        ShowWindow(window, SW_MINIMIZE);
        pump();
        if (capture->grab(reinterpret_cast<uintptr_t>(window), {0, 0, 100, 100}, r.right, r.bottom, dpi))
            throw std::runtime_error("Minimized capture was not paused");
        DestroyWindow(window);
        window = nullptr;
        DestroyWindow(occluder);
        occluder = nullptr;
        std::cout
            << "WGC " << (found_hdr ? "HDR" : "SDR")
            << " window-only crop / external-overlay exclusion / real resize / minimized passed, total ms="
            << std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - begin).count() << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
    } catch (...) {
        std::cerr << "Windows capture unavailable on current desktop\n";
    }
    if (window)
        DestroyWindow(window);
    if (occluder)
        DestroyWindow(occluder);
    return 1;
}
