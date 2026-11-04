#include "bianlingo/provider.hpp"
#include <algorithm>
#include <cctype>
#include <limits>
#include <set>

namespace bl {
namespace {
constexpr std::string_view local_ollama_url = "http://127.0.0.1:11435/api/chat";
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
bool clean_string(const Json &j, size_t maximum, bool empty = true) {
    if (!j.is_string())
        return false;
    const auto &s = j.get_ref<const std::string &>();
    return s.size() <= maximum && (empty || !trim(s).empty()) &&
           std::none_of(s.begin(), s.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
void url(const Json &v) {
    check(clean_string(v, 8192, false), "Invalid API URL");
    const auto &s = v.get_ref<const std::string &>();
    check(s.starts_with("https://"), "API URLs must use HTTPS");
    auto end = s.find_first_of("/?#", 8);
    auto authority = s.substr(8, end == s.npos ? end : end - 8);
    check(s.starts_with("https://") && !authority.empty() && authority.find('@') == authority.npos &&
              s.find('#') == s.npos && s.find(' ') == s.npos,
          "API URLs must use HTTPS without embedded credentials");
}
void headers(const Json &j) {
    check(j.is_object() && j.size() <= 32 && j.dump().size() <= 16384, "Headers must be a JSON object with at most 32 entries");
    static const std::set<std::string> managed = {"host", "content-length", "transfer-encoding",
                                                  "connection"};
    for (auto i = j.begin(); i != j.end(); ++i) {
        check(!i.key().empty() && i.key().size() <= 128 &&
                  std::all_of(i.key().begin(), i.key().end(),
                              [](unsigned char c) {
                                  return std::isalnum(c) || std::string_view("!#$%&'*+-.^_`|~").find(c) !=
                                                                std::string_view::npos;
                              }) &&
                  clean_string(i.value(), 4096),
              "Invalid custom header format");
        check(!managed.contains(lower_ascii(i.key())), "Transport-managed HTTP headers cannot be overridden");
    }
}
uint64_t count(const Json &j, const char *name) {
    auto i = j.find(name);
    if (i == j.end() || !i->is_number_integer() || (!i->is_number_unsigned() && i->get<int64_t>() < 0))
        return 0;
    return std::min(i->get<uint64_t>(), uint64_t(1000000000000ULL));
}
Json usage(uint64_t input, uint64_t output, uint64_t total, uint64_t cached) {
    return {{"prompt_tokens", input},
            {"completion_tokens", output},
            {"total_tokens", total ? total : input + output},
            {"cache_hit_tokens", cached}};
}
void set_header(std::map<std::string, std::string> &values, std::string name, std::string value) {
    auto existing = std::find_if(values.begin(), values.end(),
                                 [&](auto &v) { return lower_ascii(v.first) == lower_ascii(name); });
    if (existing != values.end())
        values.erase(existing);
    values[std::move(name)] = std::move(value);
}
ProviderRequest authenticated(const Json &p, std::string address) {
    ProviderRequest out;
    out.url = std::move(address);
    auto auth = p.at("auth").get<std::string>();
    if (auth == "auto") {
        auto format = p.at("format").get<std::string>();
        auth = format == "anthropic" ? "x-api-key" : format == "gemini" ? "x-goog-api-key" : "bearer";
    }
    const auto &key = p.at("key").get_ref<const std::string &>();
    if (auth != "none" && key.empty())
        throw std::runtime_error("Save an API key for this connection or select no authentication in advanced settings");
    if (auth != "none")
        set_header(out.headers, auth == "bearer" ? "Authorization" : auth,
                   auth == "bearer" ? "Bearer " + key : key);
    if (p.at("format") == "anthropic")
        out.headers["anthropic-version"] = "2023-06-01";
    for (auto i = p.at("headers").begin(); i != p.at("headers").end(); ++i) {
        auto value = i.value().get<std::string>();
        size_t pos = 0;
        while ((pos = value.find("${API_KEY}", pos)) != value.npos) {
            value.replace(pos, 10, key);
            pos += key.size();
        }
        set_header(out.headers, i.key(), std::move(value));
    }
    return out;
}
std::string base(std::string s) {
    while (!s.empty() && s.back() == '/')
        s.pop_back();
    return s;
}
std::string model_path(std::string value) {
    if (value.starts_with("models/"))
        value.erase(0, 7);
    std::string out;
    constexpr char digits[] = "0123456789ABCDEF";
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.')
            out += c;
        else {
            out += '%';
            out += digits[c >> 4];
            out += digits[c & 15];
        }
    }
    return out;
}
void text_blocks(const Json &parts, std::string &result, bool gemini = false) {
    if (!parts.is_array())
        return;
    for (const auto &part : parts) {
        if (!part.is_object() || !part.contains("text") || !part["text"].is_string())
            continue;
        if (gemini ? part.value("thought", false)
                   : (part.value("type", std::string()) != "text" &&
                      part.value("type", std::string()) != "output_text"))
            continue;
        result += part["text"].get<std::string>();
    }
}
} // namespace
void validate_http_headers(const std::map<std::string, std::string> &values) {
    headers(Json(values));
}
Json new_connection(std::string id) {
    if (id.empty())
        id = "conn-" + sha256(random_bytes(16)).substr(0, 16);
    return {{"id", id},
            {"name", "New connection"},
            {"base_url", ""},
            {"model", ""},
            {"format", "openai_chat"},
            {"auth", "auto"},
            {"full_url", false},
            {"headers", Json::object()},
            {"parameters", Json::object()},
            {"balance", {{"type", "none"}}},
            {"key", ""}};
}
Json new_local_ollama_connection(std::string id) {
    auto p = new_connection(std::move(id));
    p["name"] = "Local Ollama";
    p["base_url"] = local_ollama_url;
    p["model"] = default_ollama_model;
    p["format"] = "ollama_local";
    p["auth"] = "none";
    return p;
}
Json new_gemini_connection(std::string id) {
    auto p = new_connection(std::move(id));
    p["name"] = "Gemini";
    p["base_url"] = "https://generativelanguage.googleapis.com/v1beta/openai";
    p["model"] = "gemini-3.5-flash-lite";
    return p;
}
Json add_default_api_connections(Json profiles) {
    size_t api_count = 0;
    for (const auto &p : profiles) {
        if (!p.at("format").get<std::string>().ends_with("_local")) ++api_count;
        if (p.at("id") == "preset-gemini") return profiles;
        const auto address = lower_ascii(p.at("base_url").get<std::string>());
        constexpr std::string_view prefix = "https://";
        if (!address.starts_with(prefix)) continue;
        const auto end = address.find_first_of("/?#", prefix.size());
        const auto host = address.substr(prefix.size(), end == address.npos ? end : end - prefix.size());
        if (host == "generativelanguage.googleapis.com" || host == "generativelanguage.googleapis.com:443")
            return profiles;
    }
    if (api_count < 32 && profiles.size() < 35)
        profiles.push_back(new_gemini_connection("preset-gemini"));
    return profiles;
}
bool valid_ollama_download_model(std::string_view model) {
    auto alnum = [](unsigned char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    };
    return !model.empty() && model.size() <= 128 && alnum(model.front()) &&
           std::all_of(model.begin(), model.end(), [&](unsigned char c) {
               return alnum(c) || c == '.' || c == '_' || c == ':' || c == '/' || c == '-';
           });
}
Json new_local_bergamot_connection(std::string id) {
    auto p = new_connection(std::move(id));
    p["name"] = "Local Bergamot";
    p["base_url"] = "local://bergamot";
    p["model"] = "Firefox en↔zh-Hans";
    p["format"] = "bergamot_local";
    p["auth"] = "none";
    return p;
}
Json new_local_ctranslate2_connection(std::string id) {
    auto p = new_connection(std::move(id));
    p["name"] = "Local CTranslate2";
    p["base_url"] = "local://ctranslate2";
    p["model"] = "OPUS-MT en↔zh";
    p["format"] = "ctranslate2_local";
    p["auth"] = "none";
    return p;
}
Json fixed_local_connections(Json profiles, std::string &active) {
#ifdef _WIN32
    Json api = Json::array();
    std::string ollama_model(default_ollama_model);
    bool found_ollama = false;
    for (const auto &p : profiles) {
        const auto format = p.at("format").get<std::string>();
        if (format == "ollama_local") {
            if (!found_ollama || p.at("id") == active) {
                ollama_model = p.at("model").get<std::string>();
                found_ollama = true;
            }
            if (p.at("id") == active)
                active = "local-ollama";
        } else if (format == "bergamot_local") {
            if (p.at("id") == active)
                active = "local-bergamot";
        } else if (format == "ctranslate2_local") {
            if (p.at("id") == active)
                active = "local-ctranslate2";
        } else {
            api.push_back(p);
        }
    }
    // Older builds created a fresh local profile on every button click.
    // Keep API profiles and one stable entry per bundled local engine.
    Json result = Json::array();
    auto ollama = new_local_ollama_connection("local-ollama");
    ollama["model"] = ollama_model;
    result.push_back(std::move(ollama));
    result.push_back(new_local_bergamot_connection("local-bergamot"));
    result.push_back(new_local_ctranslate2_connection("local-ctranslate2"));
    for (auto &p : api)
        result.push_back(std::move(p));
    return result;
#else
    return profiles;
#endif
}
Json make_connection(const Json &s, std::string key) {
    auto p = new_connection("default");
    p["name"] = "Default connection";
    p["base_url"] = s.at("base_url");
    p["model"] = s.at("model");
    p["key"] = std::move(key);
    auto address = base(p["base_url"].get<std::string>());
    auto host = lower_ascii(
        address.substr(8, address.find('/', 8) == address.npos ? address.npos : address.find('/', 8) - 8));
    if (host == "api.deepseek.com" || host == "api.deepseek.com:443") {
        p["name"] = "DeepSeek";
        p["fallback_deepseek_defaults"] = true;
        p["parameters"] = {{"thinking", {{"type", "disabled"}}}, {"temperature", 0.2}, {"max_tokens", 1024}};
        p["balance"] = {{"type", "deepseek"}, {"url", address + "/user/balance"}};
    }
    return p;
}
const Json &active_connection(const Json &profiles, std::string_view id) {
    for (const auto &p : profiles)
        if (p.at("id").get_ref<const std::string &>() == id)
            return p;
    throw std::runtime_error("The selected connection does not exist");
}
void validate_connections(const Json &profiles, std::string_view active) {
    check(profiles.is_array() && !profiles.empty() && profiles.size() <= 35, "Between 1 and 35 connections are required");
    std::set<std::string> ids;
    for (const auto &p : profiles) {
        check(p.is_object(), "Invalid connection format");
        check(clean_string(p.at("id"), 64, false) && clean_string(p.at("name"), 192, false),
              "Invalid connection name or ID");
        auto id = p.at("id").get<std::string>();
        check(std::all_of(id.begin(), id.end(),
                          [](unsigned char c) { return std::isalnum(c) || c == '-' || c == '_'; }) &&
                  ids.insert(id).second,
              "Invalid or duplicate connection ID");
        bool bergamot = p.at("format") == "bergamot_local";
        bool ct2 = p.at("format") == "ctranslate2_local";
        bool local = p.at("format") == "ollama_local" || bergamot || ct2;
        if (local) {
#ifndef _WIN32
            throw std::runtime_error("Local translation connections are currently available on Windows only");
#endif
            check(p.at("base_url").get<std::string>() ==
                      (bergamot ? "local://bergamot" : ct2 ? "local://ctranslate2" : local_ollama_url),
                  "Local translation connection URLs cannot be changed");
        } else
            url(p.at("base_url"));
        check(p.at("full_url").is_boolean(), "Invalid full-URL option");
        check(p.at("full_url").get<bool>() ||
                  p.at("base_url").get_ref<const std::string &>().find('?') == std::string::npos,
              "Base URLs cannot include a query; enable Full request URL");
        check(clean_string(p.at("model"), 256, false), "The model name cannot be empty or contain control characters");
        check(clean_string(p.at("key"), 4096), "Invalid API key format");
        check(p.at("full_url").is_boolean(), "Invalid full-URL option");
        check(
            std::set<Json>{"openai_chat", "openai_responses", "anthropic", "gemini", "ollama_local", "bergamot_local", "ctranslate2_local"}.contains(p.at("format")),
            "Unknown request protocol");
        check(std::set<Json>{"auto", "bearer", "x-api-key", "x-goog-api-key", "none"}.contains(p.at("auth")),
              "Unknown authentication method");
        headers(p.at("headers"));
        if (local)
            check(p.at("auth") == "none" && p.at("key") == "" && p.at("headers").empty() &&
                      p.at("parameters").empty() && p.at("balance").at("type") == "none" &&
                      !p.at("full_url").get<bool>(),
                  "Local translation connections cannot contain keys, headers or cloud options");
        if (bergamot)
            check(p.at("model") == "Firefox en↔zh-Hans", "Bergamot models are managed by the local component");
        if (ct2)
            check(p.at("model") == "OPUS-MT en↔zh", "CTranslate2 models are managed by the optional component");
        const auto &parameters = p.at("parameters");
        check(parameters.is_object() && parameters.size() <= 64 && parameters.dump().size() <= 65536,
              "Extra parameters must be a JSON object of at most 64 KiB");
        for (auto key : {"model", "messages", "system", "instructions", "input", "contents",
                         "systemInstruction", "stream"})
            check(!parameters.contains(key), "Extra parameters cannot replace the model, input or streaming mode");
        auto balance = p.at("balance");
        check(balance.is_object() && balance.dump().size() <= 8192, "Balance settings must be a JSON object");
        check(std::set<Json>{"none", "deepseek", "custom"}.contains(balance.at("type")), "Unknown balance API type");
        if (balance["type"] != "none")
            url(balance.at("url"));
        if (balance["type"] == "custom") {
            check(clean_string(balance.at("amount_path"), 512, false) &&
                      clean_string(balance.at("currency"), 3, false),
                  "Invalid balance amount path or currency");
            auto currency = balance["currency"].get<std::string>();
            check(currency.size() == 3 && std::all_of(currency.begin(), currency.end(),
                                                      [](char c) { return c >= 'A' && c <= 'Z'; }),
                  "Currency must be three uppercase letters, such as USD or CNY");
        }
    }
    active_connection(profiles, active);
}
Json public_connections(Json profiles) {
    for (auto &p : profiles) {
        p["has_key"] = !p.value("key", std::string()).empty();
        p.erase("key");
    }
    return profiles;
}
Json merge_connections(Json submitted, const Json &previous) {
    check(submitted.is_array(), "Invalid connection format");
    for (auto &p : submitted) {
        if (!p.contains("key")) {
            p["key"] = "";
            for (const auto &old : previous)
                if (old.at("id") == p.at("id")) {
                    p["key"] = old.at("key");
                    break;
                }
        }
        p["key"] = trim(p.at("key").get<std::string>());
        p.erase("has_key");
        for (const auto &old : previous) {
            if (old.at("id") != p.at("id") || !old.value("fallback_deepseek_defaults", false))
                continue;
            auto address = p.at("base_url").get<std::string>();
            auto authority_end = address.find_first_of("/?#", 8);
            auto authority =
                lower_ascii(address.substr(std::min(size_t(8), address.size()),
                                           authority_end == address.npos ? address.npos : authority_end - 8));
            bool still_deepseek = p.at("format") == "openai_chat" &&
                                  (authority == "api.deepseek.com" || authority == "api.deepseek.com:443");
            if (!still_deepseek) {
                if (p.at("parameters") == old.at("parameters"))
                    p["parameters"] = Json::object();
                if (p.at("balance") == old.at("balance"))
                    p["balance"] = {{"type", "none"}};
                p["fallback_deepseek_defaults"] = false;
            }
        }
    }
    return submitted;
}
bool provider_uses_prompts(std::string_view format) {
    return format != "bergamot_local" && format != "ctranslate2_local";
}
bool provider_key_ready(const Json &p) {
    return p.at("auth") == "none" || !p.at("key").get_ref<const std::string &>().empty();
}
std::string seal_connections(const Json &profiles) {
    auto raw = canonical({{"schema", 1}, {"connections", profiles}});
    return base64(platform::protect(std::span(reinterpret_cast<const uint8_t *>(raw.data()), raw.size())));
}
Json open_connections(std::string_view encoded) {
    check(encoded.size() <= 4 * 1024 * 1024, "Encrypted connection data is too large");
    auto raw = platform::protect(unbase64(encoded), true);
    auto j = Json::parse(raw);
    check(j.at("schema") == 1, "Invalid connection data version");
    return j.at("connections");
}
ProviderRequest provider_request(const Json &p, std::string_view system, std::string_view text) {
    auto format = p.at("format").get<std::string>();
    if (format == "bergamot_local" || format == "ctranslate2_local")
        throw std::runtime_error("This connection uses a local engine and sends no HTTP requests");
    if (format == "ollama_local") {
        validate_connections(Json::array({p}), p.at("id").get<std::string>());
        ProviderRequest out;
        out.url = local_ollama_url;
        out.body = {{"model", p.at("model")},
                    {"messages", Json::array({{{"role", "system"}, {"content", system}},
                                              {{"role", "user"}, {"content", text}}})},
                    {"stream", false}, {"think", false}, {"keep_alive", "5m"},
                    {"options", {{"temperature", 0.1}, {"num_predict", 1024}}}};
        return out;
    }
    auto address = base(p.at("base_url").get<std::string>());
    if (!p.at("full_url").get<bool>())
        address += format == "openai_chat"        ? "/chat/completions"
                   : format == "openai_responses" ? "/responses"
                   : format == "anthropic"        ? "/messages"
                                           : "/models/" + model_path(p.at("model")) + ":generateContent";
    else
        address = p.at("base_url").get<std::string>();
    auto out = authenticated(p, address);
    if (format == "openai_chat")
        out.body = {{"model", p.at("model")},
                    {"messages", Json::array({{{"role", "system"}, {"content", system}},
                                              {{"role", "user"}, {"content", text}}})},
                    {"stream", false}};
    else if (format == "openai_responses")
        out.body = {{"model", p.at("model")}, {"instructions", system}, {"input", text}, {"stream", false}};
    else if (format == "anthropic")
        out.body = {{"model", p.at("model")},
                    {"system", system},
                    {"messages", Json::array({{{"role", "user"}, {"content", text}}})},
                    {"max_tokens", 1024},
                    {"stream", false}};
    else
        out.body = {
            {"systemInstruction", {{"parts", Json::array({{{"text", system}}})}}},
            {"contents", Json::array({{{"role", "user"}, {"parts", Json::array({{{"text", text}}})}}})}};
    out.body.merge_patch(p.at("parameters"));
    return out;
}
ProviderRequest provider_balance_request(const Json &p) {
    check(p.at("balance").at("type") != "none", "No balance API is configured; add one in advanced settings");
    auto out = authenticated(p, p.at("balance").at("url"));
    out.get = true;
    return out;
}
ProviderResult provider_result(const Json &p, const Json &j) {
    ProviderResult out;
    try {
        auto format = p.at("format").get<std::string>();
        auto u = j.value("usage", Json::object());
        if (format == "ollama_local") {
            out.text = j.at("message").at("content").get<std::string>();
            out.usage = usage(count(j, "prompt_eval_count"), count(j, "eval_count"), 0, 0);
            if (j.value("done_reason", std::string("stop")) != "stop")
                out.error = "The local model returned an incomplete translation; try shorter text or a larger model";
        } else if (format == "openai_chat" || format == "openai_responses") {
            bool chat = format == "openai_chat";
            auto details = u.value(chat ? "prompt_tokens_details" : "input_tokens_details", Json::object());
            out.usage =
                usage(count(u, chat ? "prompt_tokens" : "input_tokens"),
                      count(u, chat ? "completion_tokens" : "output_tokens"), count(u, "total_tokens"),
                      std::max(count(details, "cached_tokens"), count(u, "prompt_cache_hit_tokens")));
            if (chat) {
                auto c = j.at("choices").at(0);
                auto content = c.at("message").at("content");
                if (content.is_string())
                    out.text = content.get<std::string>();
                else
                    text_blocks(content, out.text);
                if (c.value("finish_reason", std::string("stop")) != "stop")
                    out.error = "The model returned an incomplete translation; check the output limit or parameters";
            } else {
                for (const auto &item : j.at("output"))
                    if (item.value("type", std::string()) == "message")
                        text_blocks(item.at("content"), out.text);
                if (j.value("status", std::string("completed")) != "completed")
                    out.error = "The model returned an incomplete translation; check the output limit or parameters";
            }
        } else if (format == "anthropic") {
            out.usage = usage(count(u, "input_tokens") + count(u, "cache_read_input_tokens") +
                                  count(u, "cache_creation_input_tokens"),
                              count(u, "output_tokens"), 0, count(u, "cache_read_input_tokens"));
            text_blocks(j.at("content"), out.text);
            auto stop = j.value("stop_reason", std::string("end_turn"));
            if (stop != "end_turn" && stop != "stop_sequence")
                out.error = "The model returned an incomplete translation; check the output limit or parameters";
        } else {
            u = j.value("usageMetadata", Json::object());
            out.usage = usage(count(u, "promptTokenCount"),
                              count(u, "candidatesTokenCount") + count(u, "thoughtsTokenCount"),
                              count(u, "totalTokenCount"), count(u, "cachedContentTokenCount"));
            auto c = j.at("candidates").at(0);
            text_blocks(c.at("content").at("parts"), out.text, true);
            if (c.value("finishReason", std::string("STOP")) != "STOP")
                out.error = "The model returned incomplete text or the provider blocked it";
        }
        out.text = trim(out.text);
        if (out.text.empty() && out.error.empty())
            out.error = "The model returned no translation; check the protocol, model or thinking options";
    } catch (const std::exception &) {
        out.error = "The response does not match the selected protocol";
    }
    return out;
}
BalanceView provider_balance(const Json &p, const Json &j, int64_t time) {
    if (p.at("balance").at("type") == "deepseek")
        return format_balance(j, time);
    try {
        auto path = p.at("balance").at("amount_path").get<std::string>();
        if (!path.starts_with('/')) {
            std::replace(path.begin(), path.end(), '.', '/');
            path = "/" + path;
        }
        auto amount = j.at(Json::json_pointer(path));
        auto view = format_balance(
            {{"is_available", true},
             {"balance_infos",
              Json::array({{{"currency", p.at("balance").at("currency")}, {"total_balance", amount}}})}},
            time);
        view.state = "Balance retrieved";
        view.available = false;
        return view;
    } catch (const std::exception &) {
        throw std::runtime_error("The balance response contains no valid amount; check the field path in advanced settings");
    }
}
void add_provider_usage(Json &stats, const Json &u) {
    constexpr uint64_t maximum = uint64_t(std::numeric_limits<int64_t>::max());
    for (auto key : {"prompt_tokens", "completion_tokens", "total_tokens", "cache_hit_tokens", "requests"}) {
        uint64_t current = 0;
        if (stats.contains(key) && stats[key].is_number_integer() &&
            (stats[key].is_number_unsigned() || stats[key].get<int64_t>() >= 0))
            current = std::min(stats[key].get<uint64_t>(), maximum);
        auto increment = std::string_view(key) == "requests" ? 1 : count(u, key);
        stats[key] = current + std::min(increment, maximum - current);
    }
}
} // namespace bl
