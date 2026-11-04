#include "bianlingo/provider.hpp"
#include "bianlingo/bergamot.hpp"
#include "bianlingo/chat_style.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
using namespace bl;
namespace {
int checks = 0;
void require(bool ok, const char *message) {
    ++checks;
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
    ++checks;
    try {
        f();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Invalid provider configuration accepted");
}
Json profile() {
    auto p = new_connection("test");
    p["name"] = "自由命名";
    p["base_url"] = "https://api.example.invalid/v1/";
    p["model"] = "any-vendor/custom-model";
    p["key"] = "synthetic-test-key";
    return p;
}
} // namespace
int main(int argc, char **argv) {
    try {
#ifdef _WIN32
        if (argc == 2 && std::string_view(argv[1]) == "--bergamot-smoke") {
            auto incoming = bergamot_translate("Hello, friend. Looking for a healer.", true, "");
            auto outgoing = bergamot_translate("你好，朋友。我们需要一位治疗。", false, "English");
            require(!incoming.empty() && !outgoing.empty() && incoming != outgoing,
                    "Bergamot did not translate both directions");
            auto links = protect_chat_links("Please click |cff0070dd|Hitem:1|h[Blue Sword]|h|r to join.");
            auto linked = links.restore(bergamot_translate(links.text, true, ""));
            require(linked.find("|Hitem:1|h[Blue Sword]|h") != std::string::npos,
                    "Bergamot changed an item link placeholder");
            std::cout << "en->zh: " << incoming << "\nzh->en: " << outgoing << '\n';
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--local-smoke") {
            auto local = new_local_ollama_connection("local-smoke");
            auto settings = default_settings();
            for (auto [input, incoming] : {std::pair{"LFM 2 DPS for SOO 10m, need heals, pst", true},
                                           {"缺治疗，十分钟后开团，密我", false}}) {
                auto system = incoming ? settings.at("incoming_prompt").get<std::string>() :
                                         outbound_prompt(settings, "short", "English");
                auto request = provider_request(local, system, input);
                auto start = std::chrono::steady_clock::now();
                auto response = platform::http_json(request.url, request.body, {}, false, request.headers);
                auto result = provider_result(local, response);
                require(result.error.empty() && !result.text.empty(), "Local Ollama returned no translation");
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - start).count();
                std::cout << ms << " ms | " << input << " => " << result.text << '\n';
            }
            return 0;
        }
#endif
        auto p = profile();
        validate_connections(Json::array({p}), "test");
        auto r = provider_request(p, "translate only", "hello \"world\"\n|Hitem:x|h[测试]|h");
        require(r.url == "https://api.example.invalid/v1/chat/completions" &&
                    r.headers.at("Authorization") == "Bearer synthetic-test-key",
                "Chat URL or bearer authentication differs");
        require(!r.body.contains("thinking") && !r.body.contains("temperature") &&
                    !r.body.contains("max_tokens"),
                "Generic API request contains vendor-only or unsupported model parameters");
        require(r.body["messages"][1]["content"] == "hello \"world\"\n|Hitem:x|h[测试]|h",
                "Original message was interpolated instead of JSON encoded");
#ifdef _WIN32
        auto local = new_local_ollama_connection("local");
        require(local["model"] == "qwen3:0.6b", "New Ollama installs should default to the smallest model");
        for (auto option : ollama_model_options) {
            require(valid_ollama_download_model(option.name), "Bundled Ollama model cannot be downloaded");
            auto chosen = local;
            chosen["model"] = option.name;
            auto request = provider_request(chosen, "Translate", "hello");
            require(request.body["model"].get<std::string>() == option.name && request.body["think"] == false,
                    "Selected Ollama model was ignored or thinking enabled");
        }
        require(valid_ollama_download_model("library/custom-model:Q4_K_M") &&
                    !valid_ollama_download_model("") && !valid_ollama_download_model("-option") &&
                    !valid_ollama_download_model("qwen3;exit") &&
                    !valid_ollama_download_model("qwen3\" -Result injected") &&
                    !valid_ollama_download_model("qwen3\nexit") &&
                    !valid_ollama_download_model(std::string(129, 'a')),
                "Ollama download model validation accepts shell arguments or invalid names");
        validate_connections(Json::array({local}), "local");
        auto local_request = provider_request(local, "Translate to Chinese", "LFM 2 DPS");
        require(local_request.url == "http://127.0.0.1:11435/api/chat" && local_request.headers.empty() &&
                    local_request.body["think"] == false && local_request.body["stream"] == false &&
                    local_request.body["messages"][1]["content"] == "LFM 2 DPS",
                "Local Ollama request was not restricted to loopback or enabled thinking");
        auto local_result = provider_result(local, {{"message", {{"content", "招2个DPS"},
                                                                {"thinking", "不要显示"}}},
                                                    {"done_reason", "stop"},
                                                    {"prompt_eval_count", 15}, {"eval_count", 7}});
        require(local_result.error.empty() && local_result.text == "招2个DPS" &&
                    local_result.usage["total_tokens"] == 22,
                "Local Ollama response or token accounting differs");
        require(!provider_result(local, {{"message", {{"content", "半句"}}},
                                         {"done_reason", "length"}}).error.empty(),
                "Truncated local translation accepted");
        auto bergamot = new_local_bergamot_connection("bergamot");
        validate_connections(Json::array({bergamot}), "bergamot");
        require(bergamot["format"] == "bergamot_local" && bergamot["auth"] == "none" &&
                    bergamot["key"] == "" && bergamot["balance"]["type"] == "none",
                "Bergamot connection does not remain local and keyless");
        rejects([&] { provider_request(bergamot, "system", "hello"); });
        auto invalid_bergamot = bergamot;
        invalid_bergamot["base_url"] = "https://example.invalid";
        rejects([&] { validate_connections(Json::array({invalid_bergamot}), "bergamot"); });
        invalid_bergamot = bergamot;
        invalid_bergamot["model"] = "different model";
        rejects([&] { validate_connections(Json::array({invalid_bergamot}), "bergamot"); });
        auto ct2 = new_local_ctranslate2_connection("ct2");
        validate_connections(Json::array({ct2}), "ct2");
        rejects([&] { provider_request(ct2, "system", "hello"); });
        auto invalid_ct2 = ct2;
        invalid_ct2["base_url"] = "https://example.invalid";
        rejects([&] { validate_connections(Json::array({invalid_ct2}), "ct2"); });
        invalid_ct2 = ct2;
        invalid_ct2["model"] = "different model";
        rejects([&] { validate_connections(Json::array({invalid_ct2}), "ct2"); });
        for (auto mutate : std::vector<std::function<void(Json &)>>{
                 [](Json &x) { x["base_url"] = "http://192.168.1.1:11435/api/chat"; },
                 [](Json &x) { x["base_url"] = "http://localhost:11435/api/chat"; },
                 [](Json &x) { x["key"] = "unexpected"; },
                 [](Json &x) { x["auth"] = "bearer"; },
                 [](Json &x) { x["headers"] = {{"X-Test", "unexpected"}}; },
                 [](Json &x) { x["parameters"] = {{"think", true}}; },
                 [](Json &x) { x["full_url"] = true; }}) {
            auto invalid = local;
            mutate(invalid);
            rejects([&] { validate_connections(Json::array({invalid}), "local"); });
        }
        rejects([&] { platform::http_json("http://127.0.0.1:11435/api/tags", Json::object()); });
        rejects([&] { platform::http_json("http://127.0.0.1:11435/api/chat", Json::object(), "secret"); });
#else
        for (auto unsupported : {new_local_ollama_connection("local"),new_local_bergamot_connection("local"),new_local_ctranslate2_connection("local")})
            rejects([&] { validate_connections(Json::array({unsupported}), "local"); });
#endif
        p["format"] = "openai_responses";
        r = provider_request(p, "system", "user");
        require(r.url.ends_with("/responses") && r.body["instructions"] == "system" &&
                    r.body["input"] == "user",
                "Responses mapping differs");
        p["format"] = "anthropic";
        r = provider_request(p, "system", "user");
        require(r.url.ends_with("/messages") && r.headers.at("x-api-key") == "synthetic-test-key" &&
                    r.headers.at("anthropic-version") == "2023-06-01" && r.body["max_tokens"] == 1024 &&
                    r.body["system"] == "system",
                "Anthropic request mapping differs");
        p["parameters"] = {{"max_tokens", 2048}, {"temperature", nullptr}};
        p["headers"] = {{"Anthropic-Version", "custom-version"}, {"X-Custom", "prefix ${API_KEY}"}};
        r = provider_request(p, "system", "user");
        require(r.body["max_tokens"] == 2048 && !r.body.contains("temperature") &&
                    r.headers.at("Anthropic-Version") == "custom-version" &&
                    !r.headers.contains("anthropic-version") &&
                    r.headers.at("X-Custom") == "prefix synthetic-test-key",
                "Custom headers, key placeholder or merge patch ignored");
        p["format"] = "gemini";
        p["model"] = "models/model?unsafe/path";
        p["parameters"] = Json::object();
        p["headers"] = Json::object();
        r = provider_request(p, "system", "user");
        require(r.url == "https://api.example.invalid/v1/models/model%3Funsafe%2Fpath:generateContent" &&
                    r.headers.at("x-goog-api-key") == "synthetic-test-key" &&
                    r.body["systemInstruction"]["parts"][0]["text"] == "system" && !r.body.contains("model"),
                "Gemini path encoding or body differs");
        p["full_url"] = true;
        p["base_url"] = "https://relay.example.invalid/exact?route=one";
        require(provider_request(p, "s", "u").url == p["base_url"].get<std::string>(),
                "Full URL was modified or had an endpoint appended");
        p["auth"] = "none";
        p["key"] = "";
        require(provider_key_ready(p) && provider_request(p, "s", "u").headers.empty(),
                "Keyless/custom-header endpoints cannot be used");
        for (auto format : {"openai_chat", "openai_responses", "anthropic", "gemini"}) {
            auto valid = profile();
            valid["format"] = format;
            Json response;
            if (valid["format"] == "openai_chat")
                response = {
                    {"choices",
                     Json::array({{{"message", {{"content", "译文"}, {"reasoning_content", "不要显示思考"}}},
                                   {"finish_reason", "stop"}}})},
                    {"usage",
                     {{"prompt_tokens", 10},
                      {"completion_tokens", 3},
                      {"total_tokens", 13},
                      {"prompt_tokens_details", {{"cached_tokens", 4}}}}}};
            else if (valid["format"] == "openai_responses")
                response = {{"status", "completed"},
                            {"output", Json::array({{{"type", "reasoning"}},
                                                    {{"type", "message"},
                                                     {"content", Json::array({{{"type", "output_text"},
                                                                               {"text", "译文"}}})}}})},
                            {"usage",
                             {{"input_tokens", 10},
                              {"output_tokens", 3},
                              {"total_tokens", 13},
                              {"input_tokens_details", {{"cached_tokens", 4}}}}}};
            else if (valid["format"] == "anthropic")
                response = {{"stop_reason", "end_turn"},
                            {"content", Json::array({{{"type", "thinking"}, {"text", "不要显示思考"}},
                                                     {{"type", "text"}, {"text", "译文"}}})},
                            {"usage",
                             {{"input_tokens", 4},
                              {"cache_read_input_tokens", 4},
                              {"cache_creation_input_tokens", 2},
                              {"output_tokens", 3}}}};
            else
                response = {
                    {"candidates",
                     Json::array({{{"finishReason", "STOP"},
                                   {"content",
                                    {{"parts", Json::array({{{"text", "不要显示思考"}, {"thought", true}},
                                                            {{"text", "译文"}}})}}}}})},
                    {"usageMetadata",
                     {{"promptTokenCount", 10},
                      {"candidatesTokenCount", 2},
                      {"thoughtsTokenCount", 1},
                      {"totalTokenCount", 13},
                      {"cachedContentTokenCount", 4}}}};
            auto result = provider_result(valid, response);
            require(result.text == "译文" && result.error.empty(),
                    "Protocol reply parsing exposes reasoning or loses text");
            require(result.usage["prompt_tokens"] == 10 && result.usage["completion_tokens"] == 3 &&
                        result.usage["total_tokens"] == 13 && result.usage["cache_hit_tokens"] == 4,
                    "Protocol token accounting differs");
            if (valid["format"] == "openai_chat")
                response["choices"][0]["finish_reason"] = "length";
            else if (valid["format"] == "openai_responses")
                response["status"] = "incomplete";
            else if (valid["format"] == "anthropic")
                response["stop_reason"] = "max_tokens";
            else
                response["candidates"][0]["finishReason"] = "MAX_TOKENS";
            result = provider_result(valid, response);
            require(!result.error.empty() && result.usage["total_tokens"] == 13,
                    "Truncated output accepted or spent tokens discarded");
            require(!provider_result(valid, Json::object()).error.empty(), "Malformed response accepted");
        }
        auto original = profile();
        for (auto mutate : std::vector<std::function<void(Json &)>>{
                 [](Json &x) { x["base_url"] = "http://api.example.invalid"; },
                 [](Json &x) { x["base_url"] = "https://user:password@example.invalid/v1"; },
                 [](Json &x) { x["base_url"] = "https://example.invalid/v1?token=secret"; },
                 [](Json &x) { x["model"] = ""; }, [](Json &x) { x["format"] = "unknown"; },
                 [](Json &x) { x["headers"] = {{"X-Test", "bad\r\nInjected: value"}}; },
                 [](Json &x) { x["headers"] = {{"Content-Length", "1000"}}; },
                 [](Json &x) { x["parameters"] = {{"messages", Json::array()}}; },
                 [](Json &x) { x["parameters"] = Json::array(); }, [](Json &x) { x["full_url"] = "true"; },
                 [](Json &x) { x["auth"] = "unknown"; }}) {
            auto bad = original;
            mutate(bad);
            rejects([&] { validate_connections(Json::array({bad}), "test"); });
        }
        rejects([&] { validate_connections(Json::array({original, original}), "test"); });
        rejects([&] { validate_connections(Json::array({original}), "missing"); });
        auto visible = public_connections(Json::array({original}));
        require(!visible[0].contains("key") && visible[0]["has_key"] == true,
                "Saved key leaked into connection form data");
        require(merge_connections(visible, Json::array({original}))[0]["key"] == original["key"],
                "Blank key draft erases saved credentials");
        visible[0]["key"] = "replacement";
        require(merge_connections(visible, Json::array({original}))[0]["key"] == "replacement",
                "Updated key not applied");
        auto encrypted = seal_connections(Json::array({original}));
        require(encrypted.find("synthetic-test-key") == encrypted.npos &&
                    open_connections(encrypted) == Json::array({original}),
                "Protected connection persistence failed");
        encrypted[encrypted.size() / 2] = encrypted[encrypted.size() / 2] == 'A' ? 'B' : 'A';
        rejects([&] { open_connections(encrypted); });
        auto normalized = make_connection(default_settings(), "fallback-key");
        auto gemini = new_gemini_connection("preset-gemini");
        validate_connections(Json::array({gemini}), "preset-gemini");
        require(gemini["name"] == "Gemini" && !provider_key_ready(gemini) &&
                    gemini["parameters"].empty() && gemini["headers"].empty() &&
                    gemini["balance"]["type"] == "none",
                "Gemini starter must not inherit credentials or DeepSeek-only options");
        rejects([&] { provider_request(gemini, "translate", "hello"); });
        gemini["key"] = "synthetic-gemini-key";
        auto gemini_request = provider_request(gemini, "translate", "hello");
        require(gemini_request.url == "https://generativelanguage.googleapis.com/v1beta/openai/chat/completions" &&
                    gemini_request.headers.at("Authorization") == "Bearer synthetic-gemini-key" &&
                    gemini_request.body["model"] == "gemini-3.5-flash-lite" &&
                    gemini_request.body["messages"][0]["content"] == "translate" &&
                    !gemini_request.body.contains("thinking"),
                "Gemini starter request URL, authentication, model or prompt differs");
        auto starters = add_default_api_connections(Json::array({normalized}));
        require(starters.size() == 2 && starters[0] == normalized && starters[1]["key"] == "" &&
                    add_default_api_connections(starters) == starters,
                "Default API seeding duplicates profiles or copies an existing key");
        auto custom_google = gemini;
        custom_google["id"] = "my-google";
        custom_google["format"] = "gemini";
        custom_google["base_url"] = "https://generativelanguage.googleapis.com:443/v1beta";
        custom_google["model"] = "custom-google-model";
        require(add_default_api_connections(Json::array({custom_google})) == Json::array({custom_google}),
                "Default seeding changed or duplicated an existing native Gemini connection");
        custom_google["base_url"] = "https://generativelanguage.googleapis.com.evil.invalid/v1beta";
        require(add_default_api_connections(Json::array({custom_google})).size() == 2,
                "Gemini detection used an unsafe hostname prefix");
        auto full_list = Json::array();
        for (int i = 0; i < 32; ++i) full_list.push_back(new_connection("capacity-" + std::to_string(i)));
        require(add_default_api_connections(full_list) == full_list,
                "Default seeding exceeded the API connection limit");
        require(normalized["key"] == "fallback-key" && normalized["parameters"]["thinking"]["type"] == "disabled" &&
                    normalized["balance"]["type"] == "deepseek",
                "Fallback DeepSeek normalization lost options, key or balance");
        auto edited = public_connections(Json::array({normalized}));
        edited[0]["base_url"] = "https://another-vendor.invalid/v1";
        auto switched = merge_connections(edited, Json::array({normalized}));
        require(switched[0]["parameters"].empty() && switched[0]["balance"]["type"] == "none" &&
                    switched[0]["key"] == "fallback-key",
                "Simple endpoint change retains DeepSeek-only defaults or loses the saved key");
        auto other = default_settings();
        other["base_url"] = "https://api.deepseek.com.evil.invalid/v1";
        require(make_connection(other, "")["parameters"].empty(),
                "Normalization detects vendors by unsafe hostname prefix");
        p = profile();
        p["balance"] = {{"type", "custom"},
                        {"url", "https://billing.example.invalid/balance"},
                        {"amount_path", "data.credit"},
                        {"currency", "USD"}};
        auto balance = provider_balance(p, {{"data", {{"credit", "12.50"}}}}, 0);
        require(balance.amount == "$ 12.50" && provider_balance_request(p).get,
                "Custom balance field mapping failed");
        p["balance"] = {{"type", "none"}};
        rejects([&] { provider_balance_request(p); });
        Json stats = Json::object();
        add_provider_usage(
            stats,
            {{"prompt_tokens", 10}, {"completion_tokens", 3}, {"total_tokens", 13}, {"cache_hit_tokens", 4}});
        add_provider_usage(stats, Json::object());
        require(stats["requests"] == 2 && stats["total_tokens"] == 13 && stats["cache_hit_tokens"] == 4,
                "Usage statistics double-count caches or drop usage-less responses");
        // Exercise the real engine's fallback normalization and atomic configuration
        // commit in an isolated directory. No worker/network thread is started.
        auto temporary = std::filesystem::temp_directory_path();
        auto directory = temporary / ("bianlingo-providers-" + sha256(random_bytes(16)).substr(0, 16));
        std::filesystem::create_directories(directory);
        struct Cleanup {
            std::filesystem::path root, parent;
            ~Cleanup() {
                if (root.parent_path() == parent &&
                    root.filename().string().starts_with("bianlingo-providers-")) {
                    std::error_code ignored;
                    std::filesystem::remove_all(root, ignored);
                }
            }
        } cleanup{directory, temporary};
        auto fallback_settings = default_settings();
        fallback_settings.erase("connection_store");
        fallback_settings.erase("active_connection");
        fallback_settings.erase("skip_target_language_nicknames");
        fallback_settings["skip_chinese_nicknames"] = true;
        write_json(directory / "settings.json", fallback_settings);
        write_json(directory / "usage-stats.json",
                   {{"prompt_tokens", 10}, {"completion_tokens", 3}, {"total_tokens", 13}, {"requests", 1}});
        const std::string old_key = "isolated-fallback-secret";
        atomic_write(
            directory / "provider.dpapi",
            platform::protect(std::span(reinterpret_cast<const uint8_t *>(old_key.data()), old_key.size())));
        Engine engine(directory);
        require(engine.settings()["active_connection"] == "default" &&
                    engine.settings()["api_presets_version"] == 1 &&
                    active_connection(engine.connections(), "preset-gemini")["has_key"] == false,
                "Adding Gemini changed the active connection or exposed an inherited key");
        require(engine.settings()["skip_target_language_nicknames"] == true &&
                    !engine.settings().contains("skip_chinese_nicknames"),
                "Fallback nickname switch was lost when loading saved settings");
        require(engine.has_key() && engine.has_balance() && engine.usage()["total_tokens"] == 13,
                "Engine fallback credential/balance/usage normalization failed");
        engine.configure(engine.settings());
        auto saved = read_json(directory / "settings.json");
        require(saved["skip_target_language_nicknames"] == true && !saved.contains("skip_chinese_nicknames"),
                "Nickname normalization did not persist the new key");
        require(!saved.contains("base_url") && !saved.contains("model") &&
                    !saved["connection_store"].get<std::string>().empty(),
                "Private connection address or model leaked into unencrypted settings");
        auto opened = open_connections(saved["connection_store"].get<std::string>());
        require(active_connection(opened, "default")["key"] == old_key,
                "Normalization did not retain the decrypted fallback key");
        auto preset_directory = directory / "preset-persistence";
        Engine fresh(preset_directory);
        require(std::abs(background_opacity(fresh.settings()) - 0.30) < 0.0001,
                "Loading a fresh profile overwrote the 70 percent background transparency");
        require(active_connection(fresh.connections(), "preset-gemini")["model"] == "gemini-3.5-flash-lite" &&
                    !std::filesystem::exists(preset_directory / "settings.json"),
                "A fresh Gemini starter is missing or startup wrote settings without saving");
#ifdef _WIN32
        require(fresh.settings()["active_connection"] == "local-bergamot" &&
                    fresh.settings()["base_url"] == "local://bergamot" && !fresh.has_balance(),
                "Fresh Windows installs must use the bundled Bergamot connection");
#endif
        auto no_gemini = fresh.connections();
        no_gemini.erase(std::remove_if(no_gemini.begin(), no_gemini.end(), [](const auto &c) {
            return c.at("id") == "preset-gemini";
        }), no_gemini.end());
        fresh.configure(fresh.settings(), std::nullopt, no_gemini);
        require(Engine(preset_directory).connections() == fresh.connections(),
                "A deleted Gemini starter reappeared after saving and restarting");
        auto existing = read_json(preset_directory / "settings.json");
        existing.erase("api_presets_version");
        write_json(preset_directory / "settings.json", existing);
        Engine upgraded(preset_directory);
        require(active_connection(upgraded.connections(), "preset-gemini")["base_url"] ==
                    "https://generativelanguage.googleapis.com/v1beta/openai" &&
                    upgraded.settings()["active_connection"] == fresh.settings()["active_connection"],
                "An existing configuration did not receive the starter safely");
        upgraded.configure(upgraded.settings());
        require(Engine(preset_directory).connections() == upgraded.connections(),
                "Saving an added preset reused the old encrypted connection list");
        auto profiles = engine.connections();
        auto second = profile();
        second["full_url"] = true;
        second["base_url"] = "https://private.invalid/route?private_key=url-secret";
        second["headers"] = {{"X-Secret", "header-secret"}};
        second["parameters"] = {{"vendor_option", "option-secret"}};
        profiles.push_back(second);
        auto next_settings = engine.settings();
        next_settings["active_connection"] = "test";
        engine.configure(next_settings, std::nullopt, profiles);
        require(engine.settings()["model"] == "any-vendor/custom-model" && engine.usage().empty() &&
                    !engine.has_balance(),
                "Activating a custom profile did not isolate model, usage and optional balance");
        saved = read_json(directory / "settings.json");
        auto raw_saved = saved.dump();
        for (auto secret :
             {"synthetic-test-key", "header-secret", "option-secret", "url-secret", "isolated-fallback-secret"})
            require(raw_saved.find(secret) == raw_saved.npos, "Connection material was stored in plaintext");
        Engine reopened(directory);
        require(reopened.connections() == engine.connections() &&
                    reopened.settings()["active_connection"] == "test" && reopened.has_key(),
                "Encrypted profile list, selection or credentials were lost on restart");
        auto unchanged_store = reopened.settings()["connection_store"];
        auto appearance = reopened.settings();
        appearance["font_size"] = 16;
        reopened.configure(appearance);
        require(reopened.settings()["connection_store"] == unchanged_store &&
                    reopened.settings()["base_url"] == second["base_url"],
                "Unrelated settings re-encrypt or lose the full request URL");
        auto before_failure = read_json(directory / "settings.json");
        auto before_connections = reopened.connections();
        auto invalid_profiles = before_connections;
        for (auto &candidate : invalid_profiles)
            if (candidate.at("id") == "default") candidate["headers"] = {{"X-Test", "bad\r\nheader"}};
        rejects([&] { reopened.configure(reopened.settings(), std::nullopt, invalid_profiles); });
        require(read_json(directory / "settings.json") == before_failure &&
                    reopened.connections() == before_connections,
                "A rejected profile partially changed disk or in-memory settings");
#ifdef _WIN32
        auto with_bergamot = reopened.connections();
        with_bergamot.push_back(new_local_bergamot_connection("bergamot-saved"));
        auto bergamot_settings = reopened.settings();
        bergamot_settings["active_connection"] = "bergamot-saved";
        reopened.configure(bergamot_settings, std::nullopt, with_bergamot);
        require(reopened.settings()["active_connection"] == "local-bergamot" &&
                    reopened.settings()["base_url"] == "local://bergamot" && !reopened.has_balance() &&
                    reopened.connections().size() == with_bergamot.size() - 1,
                "Bergamot selection failed to collapse duplicate local entries");
        Engine bergamot_reopened(directory);
        require(bergamot_reopened.settings()["active_connection"] == "local-bergamot" &&
                    bergamot_reopened.settings()["base_url"] == "local://bergamot",
                "Bergamot selection was lost on restart");
        auto with_ct2 = reopened.connections();
        with_ct2.push_back(new_local_ctranslate2_connection("ct2-saved"));
        auto ct2_settings = reopened.settings();
        ct2_settings["active_connection"] = "ct2-saved";
        reopened.configure(ct2_settings, std::nullopt, with_ct2);
        require(reopened.settings()["active_connection"] == "local-ctranslate2" &&
                    reopened.settings()["base_url"] == "local://ctranslate2" &&
                    reopened.connections().size() == with_ct2.size() - 1,
                "CTranslate2 selection failed to collapse duplicate local entries");
        auto duplicate_ollama = reopened.connections();
        auto chosen_ollama = new_local_ollama_connection("old-ollama-choice");
        chosen_ollama["model"] = "installed-custom-model";
        duplicate_ollama.push_back(chosen_ollama);
        auto ollama_settings = reopened.settings();
        ollama_settings["active_connection"] = "old-ollama-choice";
        reopened.configure(ollama_settings, std::nullopt, duplicate_ollama);
        require(reopened.settings()["active_connection"] == "local-ollama" &&
                    reopened.settings()["model"] == "installed-custom-model" &&
                    reopened.connections().size() == duplicate_ollama.size() - 1,
                "Duplicate Ollama normalization lost the selected model");
        Engine ollama_reopened(directory);
        require(ollama_reopened.settings()["model"] == "installed-custom-model",
                "Existing custom Ollama model was reset on restart");
        auto fallback_profiles = Json::array({local});
        fallback_profiles[0]["model"] = "gemma4:12b-it-q4_K_M";
        std::string fallback_active = "local";
        auto preserved = fixed_local_connections(fallback_profiles, fallback_active);
        require(active_connection(preserved, fallback_active)["model"] == "gemma4:12b-it-q4_K_M",
                "Existing large Ollama model should not be silently replaced");
#else
        auto unsupported = reopened.connections();
        unsupported.push_back(new_local_bergamot_connection("unsupported"));
        auto unsupported_settings = reopened.settings();unsupported_settings["active_connection"]="unsupported";
        rejects([&] { reopened.configure(unsupported_settings,std::nullopt,unsupported); });
        require(reopened.connections()==before_connections,"Unsupported local engine changed saved API profiles");
#endif
        auto blocked = directory / "blocked";
        std::filesystem::create_directories(blocked / "settings.json");
        Engine cannot_write(blocked);
        auto original_memory = cannot_write.settings();
        auto change_memory = original_memory;
        change_memory["font_size"] = 20;
        rejects([&] { cannot_write.configure(change_memory); });
        require(cannot_write.settings() == original_memory,
                "Failed settings file replacement changed live configuration");
        // Queued work belongs to the selected configuration; switching must
        // release the outbound gate after delivering the cancellation notice.
        reopened.outbound("synthetic queued message");
        rejects([&] { reopened.outbound("second pending message"); });
        auto switched_settings = reopened.settings();
        switched_settings["active_connection"] = "default";
        reopened.configure(switched_settings);
        auto cancelled = reopened.drain();
        require(std::any_of(cancelled.begin(), cancelled.end(),
                            [](auto &n) { return n.kind == Notice::OutboundError; }),
                "Switching profiles does not report cancellation of a queued outbound translation");
        reopened.outbound("new profile message");
        require(reopened.settings()["active_connection"] == "default" &&
                    reopened.usage()["total_tokens"] == 13,
                "Switching back to the fallback profile loses its isolated usage");
        std::cout << "Passed " << checks << " provider checks\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
