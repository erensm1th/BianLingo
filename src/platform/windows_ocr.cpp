#include "bianlingo/ocr.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <d3d11.h>
#include <DirectXPackedVector.h>
#include <dwmapi.h>
#include <dxgi1_6.h>
#include <tesseract/baseapi.h>
#include <tesseract/ocrclass.h>
#include <tesseract/resultiterator.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
namespace bl::platform {
using namespace winrt;
using namespace winrt::Windows::Graphics;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;
namespace {
bool hdr_monitor(HMONITOR monitor) {
    com_ptr<IDXGIFactory1> factory;
    check_hresult(CreateDXGIFactory1(__uuidof(IDXGIFactory1), factory.put_void()));
    for (UINT a = 0;; ++a) {
        com_ptr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(a, adapter.put()) == DXGI_ERROR_NOT_FOUND)
            break;
        for (UINT n = 0;; ++n) {
            com_ptr<IDXGIOutput> output;
            if (adapter->EnumOutputs(n, output.put()) == DXGI_ERROR_NOT_FOUND)
                break;
            auto modern = output.try_as<IDXGIOutput6>();
            DXGI_OUTPUT_DESC1 desc{};
            if (modern && SUCCEEDED(modern->GetDesc1(&desc)) && desc.Monitor == monitor)
                return desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 ||
                       desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
        }
    }
    return false;
}

uint8_t ocr_brightness(const uint16_t *rgba) {
    // WGC floating-point frames are linear scRGB. Preserve coloured chat by
    // taking its brightest channel, then compress HDR highlights for OCR.
    float light = std::max({::DirectX::PackedVector::XMConvertHalfToFloat(rgba[0]),
                            ::DirectX::PackedVector::XMConvertHalfToFloat(rgba[1]),
                            ::DirectX::PackedVector::XMConvertHalfToFloat(rgba[2])});
    if (!std::isfinite(light) || light <= 0)
        return 0;
    float mapped = std::min(1.0f, 1.5f * light / (1.0f + light));
    return static_cast<uint8_t>(std::lround(255.0f * std::sqrt(mapped)));
}

class WindowOcrCapture final : public OcrCapture {
    uintptr_t window_ = 0;
    HMONITOR monitor_ = nullptr;
    bool hdr_ = false;
    DirectXPixelFormat format_ = DirectXPixelFormat::B8G8R8A8UIntNormalized;
    std::chrono::steady_clock::time_point next_color_check_{};
    SizeInt32 size_{};
    com_ptr<ID3D11Device> device_;
    com_ptr<ID3D11DeviceContext> context_;
    IDirect3DDevice bridge_{nullptr};
    GraphicsCaptureItem item_{nullptr};
    Direct3D11CaptureFramePool pool_{nullptr};
    GraphicsCaptureSession session_{nullptr};
    com_ptr<ID3D11Texture2D> staging_;
    int w_ = 0, h_ = 0;

  public:
    WindowOcrCapture() {
        init_apartment(apartment_type::multi_threaded);
    }
    ~WindowOcrCapture() {
        if (session_)
            session_.Close();
        if (pool_)
            pool_.Close();
        session_ = nullptr;
        pool_ = nullptr;
        item_ = nullptr;
        bridge_ = nullptr;
        staging_ = nullptr;
        context_ = nullptr;
        device_ = nullptr;
        uninit_apartment();
    }
    std::optional<Image> grab(uintptr_t window, Rect region, int width, int height, int dpi) override {
        auto hwnd = reinterpret_cast<HWND>(window);
        if (!IsWindow(hwnd) || !IsWindowVisible(hwnd) || IsIconic(hwnd))
            return {};
        RECT client{};
        GetClientRect(hwnd, &client);
        if (client.right != width || client.bottom != height || int(GetDpiForWindow(hwnd)) != dpi)
            throw std::runtime_error("The game size or DPI changed; select the OCR region again");
        if (!GraphicsCaptureSession::IsSupported())
            throw std::runtime_error("Game-window OCR capture is not supported by this system");
        auto monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        auto now = std::chrono::steady_clock::now();
        bool new_hdr = hdr_;
        if (window_ != window || monitor_ != monitor || now >= next_color_check_) {
            new_hdr = hdr_monitor(monitor);
            next_color_check_ = now + std::chrono::seconds(5);
        }
        if (window_ != window || monitor_ != monitor || new_hdr != hdr_) {
            if (session_)
                session_.Close();
            if (pool_)
                pool_.Close();
            session_ = nullptr;
            pool_ = nullptr;
            item_ = nullptr;
            bridge_ = nullptr;
            staging_ = nullptr;
            context_ = nullptr;
            device_ = nullptr;
            w_ = h_ = 0;
            window_ = 0;
            monitor_ = monitor;
            hdr_ = new_hdr;
            format_ = hdr_ ? DirectXPixelFormat::R16G16B16A16Float
                           : DirectXPixelFormat::B8G8R8A8UIntNormalized;
            check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                            D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
                                            device_.put(), nullptr, context_.put()));
            auto dxgi = device_.as<IDXGIDevice>();
            com_ptr<IInspectable> inspectable;
            check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.get(), inspectable.put()));
            bridge_ = inspectable.as<IDirect3DDevice>();
            auto capture_factory = get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
            check_hresult(
                capture_factory->CreateForWindow(hwnd, guid_of<GraphicsCaptureItem>(), put_abi(item_)));
            size_ = item_.Size();
            pool_ = Direct3D11CaptureFramePool::CreateFreeThreaded(bridge_, format_, 2, size_);
            session_ = pool_.CreateCaptureSession(item_);
            try {
                session_.IsCursorCaptureEnabled(false);
            } catch (const winrt::hresult_error &) {
            }
            session_.StartCapture();
            window_ = window;
        }
        auto frame = pool_.TryGetNextFrame();
        if (!frame)
            return {};
        // Keep the latest frame only; never build a CPU screenshot queue.
        for (int i = 0; i < 2; ++i) {
            auto latest = pool_.TryGetNextFrame();
            if (!latest)
                break;
            frame.Close();
            frame = std::move(latest);
        }
        auto current = frame.ContentSize();
        if (current.Width != size_.Width || current.Height != size_.Height) {
            frame.Close();
            size_ = current;
            pool_.Recreate(bridge_, format_, 2, size_);
            return {};
        }
        RECT bounds{};
        if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &bounds, sizeof(bounds))))
            GetWindowRect(hwnd, &bounds);
        POINT origin{};
        ClientToScreen(hwnd, &origin);
        int x = origin.x - bounds.left + region.x, y = origin.y - bounds.top + region.y;
        if (x < 0 || y < 0 || x + region.w > size_.Width || y + region.h > size_.Height)
            throw std::runtime_error("The OCR region does not match the window image; select it again");
        auto access =
            frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        com_ptr<ID3D11Texture2D> texture;
        check_hresult(access->GetInterface(__uuidof(ID3D11Texture2D), texture.put_void()));
        if (!staging_ || w_ != region.w || h_ != region.h) {
            staging_ = nullptr;
            w_ = region.w;
            h_ = region.h;
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = w_;
            desc.Height = h_;
            desc.MipLevels = desc.ArraySize = 1;
            desc.Format = hdr_ ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_STAGING;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            check_hresult(device_->CreateTexture2D(&desc, nullptr, staging_.put()));
        }
        D3D11_BOX box{UINT(x), UINT(y), 0, UINT(x + region.w), UINT(y + region.h), 1};
        context_->CopySubresourceRegion(staging_.get(), 0, 0, 0, 0, texture.get(), 0, &box);
        D3D11_MAPPED_SUBRESOURCE data{};
        check_hresult(context_->Map(staging_.get(), 0, D3D11_MAP_READ, 0, &data));
        Image out;
        out.width = w_;
        out.height = h_;
        out.stride = w_ * 4;
        out.pixels.resize(size_t(out.stride) * h_);
        for (int row = 0; row < h_; ++row) {
            auto *dst = out.pixels.data() + size_t(row) * out.stride;
            auto *src = static_cast<const uint8_t *>(data.pData) + size_t(row) * data.RowPitch;
            if (!hdr_) {
                std::memcpy(dst, src, out.stride);
            } else {
                for (int col = 0; col < w_; ++col) {
                    uint16_t rgba[4];
                    std::memcpy(rgba, src + size_t(col) * 8, sizeof(rgba));
                    auto value = ocr_brightness(rgba);
                    dst[col * 4] = dst[col * 4 + 1] = dst[col * 4 + 2] = value;
                    dst[col * 4 + 3] = 255;
                }
            }
        }
        context_->Unmap(staging_.get(), 0);
        frame.Close();
        return out;
    }
};
class TessRecognizer final : public OcrRecognizer {
    tesseract::TessBaseAPI api_;

  public:
    TessRecognizer(const std::filesystem::path &directory, const std::string &languages) {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        api_.SetVariable("tessedit_parallelize", "0");
        // Memory loading supports Unicode application-data paths on Windows.
        auto loader = [](const char *filename, std::vector<char> *data) {
            try {
                auto b = read_bytes(std::filesystem::u8path(filename));
                data->assign(b.begin(), b.end());
                return true;
            } catch (...) {
                return false;
            }
        };
        auto path = directory.u8string();
        std::string utf8(path.begin(), path.end());
        if (api_.Init(utf8.c_str(), 0, languages.c_str(), tesseract::OEM_LSTM_ONLY, nullptr, 0, nullptr,
                      nullptr, false, loader) != 0)
            throw std::runtime_error("Could not load the OCR model; install the selected language data");
        api_.SetPageSegMode(tesseract::PSM_SINGLE_BLOCK);
        api_.SetVariable("user_defined_dpi", "144");
        api_.SetVariable("debug_file", "NUL");
    }
    std::vector<OcrLine> recognize(const Image &image) override {
        // 2x text scale, white-on-black normalization; retain coordinates in
        // original pixels. Bright coloured native chat survives max(R,G,B).
        int scale = image.width < 1400 ? 2 : 1, w = image.width * scale, h = image.height * scale;
        std::vector<uint8_t> gray(size_t(w) * h);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                auto p = image.pixels.data() + size_t(y / scale) * image.stride + (x / scale) * 4;
                gray[size_t(y) * w + x] = uint8_t(255 - std::max({p[0], p[1], p[2]}));
            }
        api_.SetImage(gray.data(), w, h, 1, w);
        tesseract::ETEXT_DESC monitor;
        monitor.set_deadline_msecs(2500);
        if (api_.Recognize(&monitor) != 0)
            throw std::runtime_error("OCR recognition failed or timed out");
        std::vector<OcrLine> lines;
        auto iterator = std::unique_ptr<tesseract::ResultIterator>(api_.GetIterator());
        if (iterator)
            do {
                std::unique_ptr<char[]> text(iterator->GetUTF8Text(tesseract::RIL_TEXTLINE));
                int x = 0, y = 0, r = 0, b = 0;
                if (text && iterator->BoundingBox(tesseract::RIL_TEXTLINE, &x, &y, &r, &b))
                    lines.push_back({trim(text.get()), x / scale, y / scale, (r - x) / scale, (b - y) / scale,
                                     iterator->Confidence(tesseract::RIL_TEXTLINE)});
            } while (iterator->Next(tesseract::RIL_TEXTLINE));
        api_.Clear();
        return lines;
    }
};
} // namespace
std::unique_ptr<OcrCapture> make_ocr_capture() {
    return std::make_unique<WindowOcrCapture>();
}
std::unique_ptr<OcrRecognizer> make_ocr_recognizer(const std::filesystem::path &root,
                                                   const std::string &langs) {
    return std::make_unique<TessRecognizer>(root, langs);
}
} // namespace bl::platform
