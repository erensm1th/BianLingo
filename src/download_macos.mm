#include "bianlingo/download.hpp"
#import <Foundation/Foundation.h>
#include <fcntl.h>
#include <fstream>
#include <libproc.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
namespace {
struct DownloadState {
    std::mutex mutex;
    std::condition_variable changed;
    bool done = false;
    std::string error;
    uint64_t limit = 0, total = 0;
    std::ofstream out;
    bl::download::Progress progress;
};
} // namespace
@interface ATModelDownload : NSObject <NSURLSessionDataDelegate> {
  @public
    std::shared_ptr<DownloadState> state;
}
@end
@implementation ATModelDownload
- (void)URLSession:(NSURLSession *)session
                          task:(NSURLSessionTask *)task
    willPerformHTTPRedirection:(NSHTTPURLResponse *)response
                    newRequest:(NSURLRequest *)request
             completionHandler:(void (^)(NSURLRequest *))completion {
    (void)session;
    (void)task;
    (void)response;
    NSURL *url = request.URL;
    if (![url.scheme.lowercaseString isEqualToString:@"https"] || url.user.length || url.password.length) {
        state->error = "Unsafe model redirect blocked";
        completion(nil);
    } else
        completion(request);
}
- (void)URLSession:(NSURLSession *)session
              dataTask:(NSURLSessionDataTask *)task
    didReceiveResponse:(NSURLResponse *)response
     completionHandler:(void (^)(NSURLSessionResponseDisposition))completion {
    (void)session;
    (void)task;
    if (((NSHTTPURLResponse *)response).statusCode != 200 ||
        response.expectedContentLength > int64_t(state->limit)) {
        state->error = "Model HTTP error or response too large";
        completion(NSURLSessionResponseCancel);
    } else
        completion(NSURLSessionResponseAllow);
}
- (void)URLSession:(NSURLSession *)session
          dataTask:(NSURLSessionDataTask *)task
    didReceiveData:(NSData *)data {
    (void)session;
    std::lock_guard lock(state->mutex);
    try {
        state->total += data.length;
        if (state->total > state->limit)
            throw std::runtime_error("Model download exceeds the size limit");
        state->out.write(static_cast<const char *>(data.bytes), std::streamsize(data.length));
        if (!state->out)
            throw std::runtime_error("Could not save the model file");
        if (state->progress)
            state->progress(state->total);
    } catch (const std::exception &e) {
        state->error = e.what();
        [task cancel];
    }
}
- (void)URLSession:(NSURLSession *)session
                    task:(NSURLSessionTask *)task
    didCompleteWithError:(NSError *)error {
    (void)session;
    (void)task;
    std::lock_guard lock(state->mutex);
    state->out.close();
    if (state->error.empty() && (error || !state->out || !state->total))
        state->error = "The model connection failed, the certificate is invalid or the download was interrupted";
    state->done = true;
    state->changed.notify_all();
}
@end
namespace bl::download {
void download(std::string_view address, const std::filesystem::path &destination, uint64_t limit,
              const Progress &progress) {
    @autoreleasepool {
        NSString *text = [[NSString alloc] initWithBytes:address.data()
                                                  length:address.size()
                                                encoding:NSUTF8StringEncoding];
        NSURL *url = [NSURL URLWithString:text];
        if (![url.scheme.lowercaseString isEqualToString:@"https"] || !url.host.length || url.user.length ||
            url.password.length || url.fragment.length || !limit || limit > max_download)
            throw std::runtime_error("Model URLs must use HTTPS without embedded credentials");
        auto state = std::make_shared<DownloadState>();
        state->limit = limit;
        state->progress = progress;
        state->out.open(destination, std::ios::binary | std::ios::trunc);
        if (!state->out)
            throw std::runtime_error("Could not save the model file");
        ATModelDownload *delegate = [ATModelDownload new];
        delegate->state = state;
        auto config = [NSURLSessionConfiguration ephemeralSessionConfiguration];
        config.timeoutIntervalForRequest = 15;
        config.timeoutIntervalForResource = 150;
        config.HTTPCookieStorage = nil;
        config.URLCredentialStorage = nil;
        config.URLCache = nil;
        NSOperationQueue *queue = [NSOperationQueue new];
        queue.maxConcurrentOperationCount = 1;
        NSURLSession *session = [NSURLSession sessionWithConfiguration:config
                                                              delegate:delegate
                                                         delegateQueue:queue];
        NSMutableURLRequest *request = [NSMutableURLRequest requestWithURL:url];
        [request setValue:@"no-cache" forHTTPHeaderField:@"Cache-Control"];
        NSURLSessionDataTask *task = [session dataTaskWithRequest:request];
        [task resume];
        std::unique_lock lock(state->mutex);
        auto deadline = Clock::now() + std::chrono::seconds(155);
        while (!state->done) {
            state->changed.wait_for(lock, std::chrono::milliseconds(200), [&] { return state->done; });
            try {
                if (progress)
                    progress(state->total);
                if (Clock::now() > deadline)
                    throw std::runtime_error("Model download timed out");
            } catch (...) {
                state->progress = {};
                [task cancel];
                [session invalidateAndCancel];
                state->changed.wait_for(lock, std::chrono::seconds(2), [&] { return state->done; });
                throw;
            }
        }
        [session finishTasksAndInvalidate];
        if (!state->error.empty())
            throw std::runtime_error(state->error);
    }
}
} // namespace bl::download
