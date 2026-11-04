// Maintainer-only connection benchmark. No capture/UI, settings writes or key output.
#include "bianlingo/bergamot.hpp"
#include "bianlingo/core.hpp"
#include "bianlingo/ctranslate2.hpp"
#include "bianlingo/provider.hpp"
#include <windows.h>
#include <chrono>
#include <iostream>

namespace {
using namespace bl;
Json saved_profiles() {
    const auto directory = platform::app_data();
    auto settings = read_json(directory / "settings.json", default_settings());
    const auto store = settings.value("connection_store", std::string());
    if (!store.empty())
        return open_connections(store);
    std::string key;
    if (std::filesystem::exists(directory / "provider.dpapi")) {
        const auto bytes = platform::protect(read_bytes(directory / "provider.dpapi"), true);
        key.assign(bytes.begin(), bytes.end());
    }
    return Json::array({make_connection(settings, key)});
}
Json saved_api() {
    auto profiles = saved_profiles();
    Json fallback;
    for (const auto &profile : profiles) {
        if (profile.at("format").get<std::string>().ends_with("_local") || !provider_key_ready(profile))
            continue;
        if (lower_ascii(profile.at("base_url").get<std::string>()).find("deepseek.com") != std::string::npos)
            return profile;
        if (fallback.is_null())
            fallback = profile;
    }
    return fallback;
}
}

int main(int argc, char **argv) {
    using namespace bl;
    try {
        if (argc != 2)
            return 2;
        const std::string backend = argv[1];
        Json profile;
        if (backend == "inventory") {
            auto api = saved_api();
            std::cout << Json{{"api_available", !api.is_null()},
                              {"api_model", api.is_null() ? "" : api.value("model", "")},
                              {"api_is_deepseek", !api.is_null() && api["base_url"].get<std::string>().find("deepseek.com") != std::string::npos}}
                             .dump() << std::endl;
            return 0;
        }
        if (backend == "api") {
            profile = saved_api();
            if (profile.is_null())
                throw std::runtime_error("No saved API connection");
        } else if (backend == "ollama") {
            profile = new_local_ollama_connection("benchmark-local");
        } else if (backend != "bergamot" && backend != "ctranslate2") {
            return 2;
        }
        const auto defaults = default_settings();
        std::cout << Json{{"ready", true}, {"pid", GetCurrentProcessId()},
                          {"model", profile.is_null() ? backend : profile.value("model", "")}}
                         .dump() << std::endl;
        std::string line;
        for (int requests = 0; requests < 128 && std::getline(std::cin, line); ++requests) {
            const auto request = Json::parse(line);
            const auto text = request.at("text").get<std::string>();
            const bool incoming = request.value("direction", "en-zh") == "en-zh";
            if (text.empty() || text.size() > 12000)
                return 2;
            const auto start = std::chrono::steady_clock::now();
            try {
                std::string result;
                Json usage = Json::object();
                if (backend == "bergamot")
                    result = bergamot_translate(text, incoming, incoming ? "" : "English", "zh-Hans");
                else if (backend == "ctranslate2")
                    result = ctranslate2_translate(text, incoming, incoming ? "" : "English", "zh-Hans");
                else {
                    const auto system = incoming ? defaults.at("incoming_prompt").get<std::string>()
                                                 : outbound_prompt(defaults, "short", "English");
                    const auto outgoing = provider_request(profile, system, text);
                    const auto response = platform::http_json(outgoing.url, outgoing.body, {}, false, outgoing.headers);
                    const auto parsed = provider_result(profile, response);
                    if (!parsed.error.empty() || parsed.text.empty())
                        throw std::runtime_error("Provider returned no valid translation");
                    result = parsed.text;
                    usage = parsed.usage;
                }
                auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                std::cout << Json{{"ok", true}, {"ms", ms}, {"text", result}, {"usage", usage}}.dump() << std::endl;
            } catch (const std::exception &error) {
                // A remote error body can echo authentication data. Never emit it.
                auto message = backend == "api" || backend == "ollama" ? "Provider request failed" : error.what();
                std::cout << Json{{"ok", false}, {"error", message}}.dump() << std::endl;
            }
        }
        return 0;
    } catch (...) {
        std::cout << Json{{"ok", false}, {"error", "Benchmark configuration could not be read"}}.dump() << std::endl;
        return 1;
    }
}
