#pragma once
#include "bianlingo/core.hpp"
#include <array>

namespace bl {
struct OllamaModelOption {
    std::string_view name, download_size;
};
inline constexpr std::array<OllamaModelOption, 3> ollama_model_options{{
    {"qwen3:0.6b", "523 MB"}, {"qwen3:1.7b", "1.4 GB"}, {"qwen3:4b", "2.5 GB"}}};
inline constexpr std::string_view default_ollama_model = "qwen3:0.6b";
bool valid_ollama_download_model(std::string_view model);
struct ProviderRequest {
    std::string url;
    Json body;
    std::map<std::string, std::string> headers;
    bool get = false;
};
struct ProviderResult {
    std::string text, error;
    Json usage = Json::object();
};
void validate_http_headers(const std::map<std::string, std::string> &headers);
Json new_connection(std::string id = {});
Json new_gemini_connection(std::string id = {});
Json add_default_api_connections(Json profiles);
Json new_local_ollama_connection(std::string id = {});
Json fixed_local_connections(Json profiles, std::string &active);
Json new_local_bergamot_connection(std::string id = {});
Json new_local_ctranslate2_connection(std::string id = {});
Json make_connection(const Json &settings, std::string key);
Json public_connections(Json profiles);
Json merge_connections(Json submitted, const Json &previous);
void validate_connections(const Json &profiles, std::string_view active);
const Json &active_connection(const Json &profiles, std::string_view id);
bool provider_key_ready(const Json &profile);
bool provider_uses_prompts(std::string_view format);
std::string seal_connections(const Json &profiles);
Json open_connections(std::string_view protected_text);
ProviderRequest provider_request(const Json &profile, std::string_view system, std::string_view text);
ProviderRequest provider_balance_request(const Json &profile);
ProviderResult provider_result(const Json &profile, const Json &response);
BalanceView provider_balance(const Json &profile, const Json &response, int64_t time);
void add_provider_usage(Json &stats, const Json &usage);
} // namespace bl
