#include "bianlingo/core.hpp"
#include "bianlingo/provider.hpp"
#import <AppKit/AppKit.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <IOKit/IOKitLib.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <Security/Security.h>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <openssl/evp.h>

namespace bl::platform {
void initialize_storage() {}
std::string user_locale() {
    if (const char *override = getenv("BIANLINGO_UI_LANGUAGE"); override && *override)
        return override;
    NSString *tag = [[NSLocale preferredLanguages] firstObject];
    return tag ? std::string([tag UTF8String]) : "en";
}
bool is_letter(char32_t c) {
    return CFCharacterSetIsLongCharacterMember(CFCharacterSetGetPredefined(kCFCharacterSetLetter),
                                               UTF32Char(c));
}
struct FrameState {
    std::mutex mutex;
    std::condition_variable changed;
    Image latest;
    uint64_t sequence = 0;
    std::string error;
};
struct HttpState {
    std::mutex mutex;
    std::condition_variable changed;
    Bytes data;
    bool done = false;
    int status = 0;
    std::string error;
};
} // namespace bl::platform
@interface ATFrameOutput : NSObject <SCStreamOutput, SCStreamDelegate> {
  @public
    std::shared_ptr<bl::platform::FrameState> state;
}
@end
@implementation ATFrameOutput
- (void)stream:(SCStream *)stream
    didOutputSampleBuffer:(CMSampleBufferRef)sample
                   ofType:(SCStreamOutputType)type {
    (void)stream;
    if (type != SCStreamOutputTypeScreen || !CMSampleBufferIsValid(sample))
        return;
    auto image = CMSampleBufferGetImageBuffer(sample);
    if (!image)
        return;
    if (CVPixelBufferLockBaseAddress(image, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess)
        return;
    size_t width = CVPixelBufferGetWidth(image), height = CVPixelBufferGetHeight(image),
           stride = CVPixelBufferGetBytesPerRow(image);
    if (width > 0 && height > 0 && width * height <= 100'000'000 && stride >= width * 4) {
        std::lock_guard lock(state->mutex);
        auto &out = state->latest;
        out.width = int(width);
        out.height = int(height);
        out.stride = int(width) * 4;
        out.pixels.resize(width * height * 4);
        auto *source = static_cast<const uint8_t *>(CVPixelBufferGetBaseAddress(image));
        for (size_t y = 0; y < height; y++)
            std::memcpy(out.pixels.data() + y * width * 4, source + y * stride, width * 4);
        state->sequence++;
        state->changed.notify_all();
    }
    CVPixelBufferUnlockBaseAddress(image, kCVPixelBufferLock_ReadOnly);
}
- (void)stream:(SCStream *)stream didStopWithError:(NSError *)error {
    (void)stream;
    std::lock_guard lock(state->mutex);
    state->error = error.localizedDescription.UTF8String;
    state->changed.notify_all();
}
@end
@interface ATHTTPDelegate : NSObject <NSURLSessionDataDelegate> {
  @public
    std::shared_ptr<bl::platform::HttpState> state;
}
@end
@implementation ATHTTPDelegate
- (void)URLSession:(NSURLSession *)session
                          task:(NSURLSessionTask *)task
    willPerformHTTPRedirection:(NSHTTPURLResponse *)response
                    newRequest:(NSURLRequest *)request
             completionHandler:(void (^)(NSURLRequest *))completionHandler {
    (void)session;
    (void)task;
    (void)response;
    (void)request;
    completionHandler(nil);
}
- (void)URLSession:(NSURLSession *)session
              dataTask:(NSURLSessionDataTask *)task
    didReceiveResponse:(NSURLResponse *)response
     completionHandler:(void (^)(NSURLSessionResponseDisposition))completionHandler {
    (void)session;
    (void)task;
    std::lock_guard lock(state->mutex);
    state->status = int(((NSHTTPURLResponse *)response).statusCode);
    if (response.expectedContentLength > 2 * 1024 * 1024) {
        state->error = "The API response is too large";
        completionHandler(NSURLSessionResponseCancel);
    } else
        completionHandler(NSURLSessionResponseAllow);
}
- (void)URLSession:(NSURLSession *)session
          dataTask:(NSURLSessionDataTask *)task
    didReceiveData:(NSData *)data {
    (void)session;
    std::lock_guard lock(state->mutex);
    if (state->data.size() + data.length > 2 * 1024 * 1024) {
        state->error = "The API response is too large";
        [task cancel];
        return;
    }
    const auto *begin = static_cast<const uint8_t *>(data.bytes);
    state->data.insert(state->data.end(), begin, begin + data.length);
}
- (void)URLSession:(NSURLSession *)session
                    task:(NSURLSessionTask *)task
    didCompleteWithError:(NSError *)error {
    (void)session;
    (void)task;
    std::lock_guard lock(state->mutex);
    if (error && state->error.empty())
        state->error = "The API connection failed or the request timed out";
    state->done = true;
    state->changed.notify_all();
}
@end

namespace bl::platform {
namespace {
NSString *ns(std::string_view s) {
    return [[NSString alloc] initWithBytes:s.data() length:s.size() encoding:NSUTF8StringEncoding];
}
Bytes storage_key() {
    NSDictionary *query = @{
        (__bridge id)kSecClass : (__bridge id)kSecClassGenericPassword,
        (__bridge id)kSecAttrService : @"io.github.erensm1th.BianLingo",
        (__bridge id)kSecAttrAccount : @"local-storage-v1",
        (__bridge id)kSecReturnData : @YES
    };
    CFTypeRef found = nullptr;
    OSStatus status = SecItemCopyMatching((__bridge CFDictionaryRef)query, &found);
    if (status == errSecSuccess) {
        NSData *data = CFBridgingRelease(found);
        if (data.length != 32)
            throw std::runtime_error("Invalid Keychain key");
        auto *p = static_cast<const uint8_t *>(data.bytes);
        return Bytes(p, p + data.length);
    }
    if (status != errSecItemNotFound)
        throw std::runtime_error("Could not access the system Keychain");
    auto key = random_bytes(32);
    NSDictionary *item = @{
        (__bridge id)kSecClass : (__bridge id)kSecClassGenericPassword,
        (__bridge id)kSecAttrService : @"io.github.erensm1th.BianLingo",
        (__bridge id)kSecAttrAccount : @"local-storage-v1",
        (__bridge id)kSecAttrAccessible : (__bridge id)kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly,
        (__bridge id)kSecValueData : [NSData dataWithBytes:key.data() length:key.size()]
    };
    status = SecItemAdd((__bridge CFDictionaryRef)item, nullptr);
    if (status == errSecDuplicateItem)
        return storage_key();
    if (status != errSecSuccess)
        throw std::runtime_error("Could not save the system Keychain key");
    return key;
}
} // namespace
std::filesystem::path app_data() {
    @autoreleasepool {
        return std::filesystem::path(NSHomeDirectory().UTF8String) / "Library" / "Application Support" /
               "BianLingo";
    }
}
std::filesystem::path executable_dir() {
    @autoreleasepool {
        return std::filesystem::path(NSBundle.mainBundle.executablePath.UTF8String).parent_path();
    }
}
Bytes protect(std::span<const uint8_t> raw, bool decrypt) {
    @autoreleasepool {
        auto key = storage_key();
        std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> ctx(EVP_CIPHER_CTX_new(),
                                                                            EVP_CIPHER_CTX_free);
        if (!ctx)
            throw std::runtime_error("Credential encryption failed");
        if (raw.size() > 16 * 1024 * 1024 || (decrypt && raw.size() < 29))
            throw std::runtime_error("Credential size invalid");
        Bytes nonce = decrypt ? Bytes(raw.begin() + 1, raw.begin() + 13) : random_bytes(12),
              output(raw.size() + 32);
        int count = 0, total = 0;
        if (decrypt) {
            if (raw[0] != 1 ||
                EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, key.data(), nonce.data()) != 1 ||
                EVP_DecryptUpdate(ctx.get(), output.data(), &count, raw.data() + 13, int(raw.size() - 29)) !=
                    1)
                throw std::runtime_error("Could not decrypt local credentials");
            total = count;
            if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, 16,
                                    const_cast<uint8_t *>(raw.data() + raw.size() - 16)) != 1 ||
                EVP_DecryptFinal_ex(ctx.get(), output.data() + total, &count) != 1)
                throw std::runtime_error("Local credential verification failed");
            output.resize(total + count);
        } else {
            if (EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, key.data(), nonce.data()) != 1 ||
                EVP_EncryptUpdate(ctx.get(), output.data(), &count, raw.data(), int(raw.size())) != 1)
                throw std::runtime_error("Could not encrypt local credentials");
            total = count;
            if (EVP_EncryptFinal_ex(ctx.get(), output.data() + total, &count) != 1)
                throw std::runtime_error("Could not encrypt local credentials");
            output.resize(total + count);
            Bytes packed{1};
            packed.insert(packed.end(), nonce.begin(), nonce.end());
            packed.insert(packed.end(), output.begin(), output.end());
            std::array<uint8_t, 16> tag{};
            if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, 16, tag.data()) != 1)
                throw std::runtime_error("Credential tag failed");
            packed.insert(packed.end(), tag.begin(), tag.end());
            output = std::move(packed);
        }
        OPENSSL_cleanse(key.data(), key.size());
        return output;
    }
}
std::string hardware_id() {
    io_service_t service =
        IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("IOPlatformExpertDevice"));
    if (!service)
        throw std::runtime_error("Could not identify this Mac");
    auto value = IORegistryEntryCreateCFProperty(service, CFSTR("IOPlatformUUID"), kCFAllocatorDefault, 0);
    IOObjectRelease(service);
    if (!value || CFGetTypeID(value) != CFStringGetTypeID()) {
        if (value)
            CFRelease(value);
        throw std::runtime_error("Could not read the Mac device identifier");
    }
    std::array<char, 256> buffer{};
    bool ok = CFStringGetCString(static_cast<CFStringRef>(value), buffer.data(), buffer.size(),
                                 kCFStringEncodingUTF8);
    CFRelease(value);
    if (!ok)
        throw std::runtime_error("Invalid Mac device identifier");
    std::string domain = "bianlingo/device/v1";
    domain += '\0';
    return sha256(domain + canonical({{"mac_system_uuid", lower_ascii(trim(buffer.data()))}}));
}
Json http_json(std::string_view address, const Json &body, std::string_view bearer, bool get,
               const std::map<std::string, std::string> &extra_headers, StopToken stop) {
    if (stop.stop_requested()) throw std::runtime_error("Request cancelled.");
    validate_http_headers(extra_headers);
    @autoreleasepool {
        NSURL *url = [NSURL URLWithString:ns(address)];
        if (!url || ![url.scheme isEqualToString:@"https"] || url.user.length || url.password.length ||
            !url.host.length)
            throw std::runtime_error("API URLs must use HTTPS");
        if (bearer.find_first_of("\r\n") != std::string_view::npos)
            throw std::runtime_error("Invalid API key format");
        auto payload = get ? std::string() : body.dump();
        if (payload.size() > 128 * 1024)
            throw std::runtime_error("The API request is too large");
        NSMutableURLRequest *request = [NSMutableURLRequest requestWithURL:url];
        request.HTTPMethod = get ? @"GET" : @"POST";
        request.timeoutInterval = 20;
        [request setValue:@"application/json" forHTTPHeaderField:@"Content-Type"];
        for (const auto &[name, value] : extra_headers)
            [request setValue:ns(value) forHTTPHeaderField:ns(name)];
        if (!bearer.empty() && std::none_of(extra_headers.begin(), extra_headers.end(), [](auto &entry) {
                return lower_ascii(entry.first) == "authorization";
            }))
            [request setValue:[@"Bearer " stringByAppendingString:ns(bearer)]
                forHTTPHeaderField:@"Authorization"];
        if (!get)
            request.HTTPBody = [NSData dataWithBytes:payload.data() length:payload.size()];
        ATHTTPDelegate *delegate = [ATHTTPDelegate new];
        delegate->state = std::make_shared<HttpState>();
        auto state = delegate->state;
        NSURLSessionConfiguration *cfg = [NSURLSessionConfiguration ephemeralSessionConfiguration];
        cfg.timeoutIntervalForResource = 45;
        NSURLSession *session = [NSURLSession sessionWithConfiguration:cfg
                                                              delegate:delegate
                                                         delegateQueue:nil];
        auto task = [session dataTaskWithRequest:request];
        [task resume];
        {
            std::unique_lock lock(state->mutex);
            const auto deadline = Clock::now() + std::chrono::seconds(45);
            while (!state->done && !stop.stop_requested() && Clock::now() < deadline)
                state->changed.wait_until(lock, std::min(deadline, Clock::now() + std::chrono::milliseconds(50)));
            if (stop.stop_requested() || !state->done) {
                lock.unlock();
                [task cancel];
                [session invalidateAndCancel];
                throw std::runtime_error(stop.stop_requested() ? "Request cancelled." : "The API request timed out");
            }
        }
        [session finishTasksAndInvalidate];
        if (!state->error.empty())
            throw std::runtime_error(state->error);
        if (state->status < 200 || state->status >= 300)
            throw std::runtime_error("API HTTP " + std::to_string(state->status));
        try {
            return Json::parse(state->data);
        } catch (const std::exception &) {
            throw std::runtime_error("The API returned invalid JSON");
        }
    }
}
class MacCapture final : public Capture {
    struct Display {
        __strong SCDisplay *display;
        Rect rect;
        double scale;
    };
    std::vector<Display> displays_;
    std::shared_ptr<FrameState> state_ = std::make_shared<FrameState>();
    __strong SCStream *stream_ = nil;
    __strong ATFrameOutput *output_ = nil;
    __strong SCContentFilter *filter_ = nil;
    dispatch_queue_t queue_;
    std::optional<Rect> current_;
    Image image_;
    uint64_t last_sequence_ = 0;
    CGDirectDisplayID current_display_ = 0;
    template <class Start> void completed(Start start) {
        dispatch_semaphore_t sem = dispatch_semaphore_create(0);
        __block NSString *error = nil;
        start(^(NSError *e) {
          if (e)
              error = e.localizedDescription;
          dispatch_semaphore_signal(sem);
        });
        if (dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 8 * NSEC_PER_SEC)) != 0)
            throw std::runtime_error("Screen capture setup timed out");
        if (error)
            throw std::runtime_error(error.UTF8String);
    }
    void load_displays() {
        dispatch_semaphore_t sem = dispatch_semaphore_create(0);
        __block SCShareableContent *content = nil;
        __block NSError *error = nil;
        [SCShareableContent getShareableContentExcludingDesktopWindows:YES
                                                   onScreenWindowsOnly:YES
                                                     completionHandler:^(SCShareableContent *c, NSError *e) {
                                                       content = c;
                                                       error = e;
                                                       dispatch_semaphore_signal(sem);
                                                     }];
        if (dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 8 * NSEC_PER_SEC)) != 0 || error ||
            !content)
            throw std::runtime_error("Allow screen recording in system settings, then restart BianLingo");
        int origin = 0;
        for (SCDisplay *display in content.displays) {
            auto bounds = CGDisplayBounds(display.displayID);
            int width = int(CGDisplayPixelsWide(display.displayID)),
                height = int(CGDisplayPixelsHigh(display.displayID));
            if (!width || !height)
                continue;
            displays_.push_back({display, {origin, 0, width, height}, double(width) / bounds.size.width});
            origin += width;
        }
    }

  public:
    MacCapture() {
        queue_ = dispatch_queue_create("art.k9playboy.bianlingo.frames", DISPATCH_QUEUE_SERIAL);
        load_displays();
        output_ = [ATFrameOutput new];
        output_->state = state_;
    }
    ~MacCapture() override {
        if (stream_) {
            [stream_ stopCaptureWithCompletionHandler:^(NSError *){
            }];
            NSError *error = nil;
            [stream_ removeStreamOutput:output_ type:SCStreamOutputTypeScreen error:&error];
        }
    }
    std::vector<Rect> monitors() override {
        std::vector<Rect> out;
        for (auto &d : displays_)
            out.push_back(d.rect);
        return out;
    }
    const Image &grab(Rect r) override {
        @autoreleasepool {
            auto it = std::find_if(displays_.begin(), displays_.end(), [&](const Display &d) {
                return r.x >= d.rect.x && r.y >= d.rect.y && r.x + r.w <= d.rect.x + d.rect.w &&
                       r.y + r.h <= d.rect.y + d.rect.h;
            });
            if (it == displays_.end())
                throw std::runtime_error("The display layout changed; restart BianLingo");
            if (!current_ || *current_ != r) {
                SCStreamConfiguration *cfg = [SCStreamConfiguration new];
                cfg.width = r.w;
                cfg.height = r.h;
                cfg.pixelFormat = kCVPixelFormatType_32BGRA;
                cfg.sourceRect = CGRectMake((r.x - it->rect.x) / it->scale, (r.y - it->rect.y) / it->scale,
                                            r.w / it->scale, r.h / it->scale);
                cfg.minimumFrameInterval = CMTimeMake(1, 10);
                cfg.queueDepth = 3;
                cfg.showsCursor = NO;
                cfg.capturesAudio = NO;
                cfg.colorSpaceName = kCGColorSpaceSRGB;
                if (!stream_ || current_display_ != it->display.displayID) {
                    if (stream_)
                        completed([&](auto done) { [stream_ stopCaptureWithCompletionHandler:done]; });
                    filter_ = [[SCContentFilter alloc] initWithDisplay:it->display excludingWindows:@[]];
                    stream_ = [[SCStream alloc] initWithFilter:filter_ configuration:cfg delegate:output_];
                    NSError *error = nil;
                    if (![stream_ addStreamOutput:output_
                                             type:SCStreamOutputTypeScreen
                               sampleHandlerQueue:queue_
                                            error:&error])
                        throw std::runtime_error("Could not create the screen capture stream");
                    completed([&](auto done) { [stream_ startCaptureWithCompletionHandler:done]; });
                    current_display_ = it->display.displayID;
                } else
                    completed([&](auto done) { [stream_ updateConfiguration:cfg completionHandler:done]; });
                current_ = r;
                image_.width = 0;
                image_.height = 0;
                image_.pixels.clear();
                {
                    std::lock_guard lock(state_->mutex);
                    last_sequence_ = state_->sequence;
                    state_->error.clear();
                }
            }
            std::unique_lock lock(state_->mutex);
            if (!state_->changed.wait_for(lock, std::chrono::seconds(3), [&] {
                    return !state_->error.empty() ||
                           (state_->sequence > last_sequence_ && state_->latest.width == r.w &&
                            state_->latest.height == r.h);
                })) {
                // ScreenCaptureKit may omit unchanged frames. Reuse a previously validated region.
                if (image_.width == r.w && image_.height == r.h && !image_.pixels.empty())
                    return image_;
                throw std::runtime_error("No valid screen capture frame is available");
            }
            if (!state_->error.empty())
                throw std::runtime_error(state_->error);
            image_ = state_->latest;
            last_sequence_ = state_->sequence;
            return image_;
        }
    }
};
std::unique_ptr<Capture> make_capture() {
    return std::make_unique<MacCapture>();
}
} // namespace bl::platform
