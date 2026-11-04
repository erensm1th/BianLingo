#include <winsock2.h>
#include <windows.h>
#include "bianlingo/core.hpp"
#include "bianlingo/download.hpp"
#include <iostream>
using namespace bl;
namespace {
void check(bool ok, const char *why) { if (!ok) throw std::runtime_error(why); }
struct Server {
    SOCKET listener = INVALID_SOCKET, client = INVALID_SOCKET;
    std::thread thread;
    std::atomic<bool> finished = false;
    std::promise<void> ready;
    explicit Server(int mode) {
        listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        check(listener != INVALID_SOCKET, "Test socket failed");
        BOOL exclusive = TRUE; setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<char *>(&exclusive), sizeof(exclusive));
        sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); address.sin_port = htons(11435);
        if (bind(listener,reinterpret_cast<sockaddr *>(&address),sizeof(address)) || listen(listener,1)) {
            closesocket(listener); listener = INVALID_SOCKET;
            throw std::runtime_error("Test requires free private port 11435; no existing process is stopped");
        }
        thread = std::thread([this,mode] {
            client = accept(listener,nullptr,nullptr);
            if (client == INVALID_SOCKET) { ready.set_value(); return; }
            char input[8192]; recv(client,input,sizeof(input),0);
            if (mode == 1) {
                const std::string partial = "HTTP/1.1 200 OK\r\nContent-Length: 100\r\nConnection: close\r\n\r\n{";
                send(client,partial.data(),int(partial.size()),0);
            } else if (mode == 2) {
                const std::string complete = "HTTP/1.1 200 OK\r\nContent-Length: 11\r\nConnection: close\r\n\r\n{\"ok\":true}";
                send(client,complete.data(),int(complete.size()),0);
            }
            ready.set_value();
            while (!finished.load()) Sleep(10);
        });
    }
    ~Server() {
        finished = true;
        shutdown(listener,SD_BOTH); closesocket(listener);
        if (thread.joinable()) thread.join();
        if (client != INVALID_SOCKET) { shutdown(client,SD_BOTH); closesocket(client); }
    }
};
}
int main() {
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2,2),&wsa)) return 1;
    try {
        const auto url = "http://127.0.0.1:11435/api/chat";
        for (int mode : {0,1}) {
            Server server(mode); auto ready = server.ready.get_future();
            auto state = std::make_shared<StopState>();
            auto request = std::async(std::launch::async,[&] {
                try { platform::http_json(url,Json::object(),{},false,{},StopToken(state)); return false; }
                catch(const std::exception &e) { return lower_ascii(e.what()).find("cancel") != std::string::npos; }
            });
            check(ready.wait_for(std::chrono::seconds(5)) == std::future_status::ready, "Local HTTP fixture did not receive request");
            Sleep(100); const auto start = Clock::now(); state->stopped = true;
            check(request.wait_for(std::chrono::seconds(2)) == std::future_status::ready && request.get(),
                  "HTTP did not cancel during stalled headers/body");
            check(Clock::now()-start < std::chrono::seconds(2), "Cancellation exceeded shutdown budget");
        }
        { Server server(2); check(platform::http_json(url,Json::object())["ok"] == true, "Async HTTP lost a normal response"); }
        {
            // Stop during a real TLS handshake before any certificate arrives.
            // HTTPS verification remains enabled; the server never sends a cert.
            Server server(0); auto ready = server.ready.get_future();
            std::atomic<bool> cancel = false;
            const auto file = std::filesystem::temp_directory_path() / ("cancel-download-" + sha256(random_bytes(8)).substr(0,12));
            auto request = std::async(std::launch::async,[&] {
                try {
                    download::download("https://127.0.0.1:11435/stall", file, 1024, [&](uint64_t) {
                        if (cancel.load()) throw std::runtime_error("download cancelled");
                    });
                    return false;
                } catch (const std::exception &e) { return std::string(e.what()) == "download cancelled"; }
            });
            check(ready.wait_for(std::chrono::seconds(5)) == std::future_status::ready, "TLS download fixture did not receive a request");
            cancel = true;
            check(request.wait_for(std::chrono::seconds(2)) == std::future_status::ready && request.get(),
                  "Model download could not cancel a stalled TLS handshake");
            check(!std::filesystem::exists(file), "Cancelled handshake created a model file");
        }
        WSACleanup();
        std::cout << "PASS actual loopback HTTP: stalled headers, stalled body, cancellation and normal response\n";
        return 0;
    } catch (const std::exception &e) { WSACleanup(); std::cerr << e.what() << '\n'; return 1; }
}
