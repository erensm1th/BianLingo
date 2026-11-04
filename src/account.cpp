#include "bianlingo/core.hpp"
#include "bianlingo/bundle.hpp"
#include "bianlingo/localization.hpp"
#include <algorithm>
#include <ctime>

namespace bl {
namespace {
std::string money(const Json &value) {
    if (!value.is_string() && !value.is_number())
        throw std::runtime_error("Invalid balance amount");
    auto text = value.is_string() ? trim(value.get<std::string>()) : value.dump();
    if (text.empty() || text.size() > 24)
        throw std::runtime_error("Invalid balance amount");
    size_t start = text.front() == '-' ? 1 : 0, point = text.find('.');
    bool digit = false;
    for (size_t i = start; i < text.size(); ++i) {
        if (text[i] == '.' && i == point && i > start && i + 1 < text.size())
            continue;
        if (text[i] < '0' || text[i] > '9')
            throw std::runtime_error("Invalid balance amount");
        digit = true;
    }
    if (!digit)
        throw std::runtime_error("Invalid balance amount");
    if (point == text.npos)
        text += ".00";
    else {
        while (text.size() > point + 3 && text.back() == '0')
            text.pop_back();
        while (text.size() < point + 3)
            text += '0';
    }
    return text;
}
std::string amount(const Json &item, const char *key, const std::string &currency) {
    auto value = money(item.at(key));
    return (currency == "CNY" ? "¥ " : currency == "USD" ? "$ " : currency + " ") + value;
}
std::string grouped(uint64_t value) {
    auto out = std::to_string(value);
    for (int i = int(out.size()) - 3; i > 0; i -= 3)
        out.insert(size_t(i), ",");
    return out;
}
uint64_t count(const Json &j, const char *key) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_number_integer() || (!it->is_number_unsigned() && it->get<int64_t>() < 0))
        return 0;
    return it->get<uint64_t>();
}
} // namespace
BalanceView format_balance(const Json &data, int64_t queried_at) {
    if (!data.is_object() || !data.contains("is_available") || !data["is_available"].is_boolean() ||
        !data.contains("balance_infos") || !data["balance_infos"].is_array() ||
        data["balance_infos"].size() > 8)
        throw std::runtime_error("The service returned no valid account balance");
    BalanceView view;
    view.amount.clear();
    view.available = data["is_available"].get<bool>();
    view.state = view.available ? "Account available" : "Account unavailable";
    for (const auto &item : data["balance_infos"]) {
        if (!item.is_object() || !item.contains("currency") || !item["currency"].is_string() ||
            !item.contains("total_balance"))
            throw std::runtime_error("The service returned no valid account balance");
        auto currency = item.at("currency").get<std::string>();
        if (currency.size() != 3 ||
            !std::all_of(currency.begin(), currency.end(), [](char c) { return c >= 'A' && c <= 'Z'; }))
            throw std::runtime_error("Invalid balance currency");
        if (!view.amount.empty())
            view.amount += "  ·  ";
        view.amount += amount(item, "total_balance", currency);
        std::string detail;
        if (item.contains("topped_up_balance"))
            detail = "Paid " + amount(item, "topped_up_balance", currency);
        if (item.contains("granted_balance")) {
            if (!detail.empty())
                detail += " · ";
            detail += "Bonus " + amount(item, "granted_balance", currency);
        }
        if (!detail.empty()) {
            if (!view.detail.empty())
                view.detail += "; ";
            view.detail += detail;
        }
    }
    if (view.amount.empty())
        view.amount = "No balance details";
    std::time_t timestamp = std::time_t(queried_at);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &timestamp);
#else
    localtime_r(&timestamp, &local);
#endif
    char time[16]{};
    std::strftime(time, sizeof(time), "%H:%M:%S", &local);
    view.updated = "Updated at " + std::string(time);
    return view;
}
UsageView format_usage(const Json &usage, std::string_view language) {
    static const Json formats = bundled_json("usage-formats.json");
    const auto &selected = formats.at(normalized_ui_language(language));
    const std::pair<std::string_view, std::string> values[] = {
        {"{total}", grouped(count(usage, "total_tokens"))},
        {"{requests}", grouped(count(usage, "requests"))},
        {"{input}", grouped(count(usage, "prompt_tokens"))},
        {"{cached}", grouped(count(usage, "cache_hit_tokens"))},
        {"{output}", grouped(count(usage, "completion_tokens"))}};
    auto render = [&](std::string text) {
        for (const auto &[key, value] : values) {
            size_t offset = 0;
            while ((offset = text.find(key, offset)) != text.npos) {
                text.replace(offset, key.size(), value); offset += value.size();
            }
        }
        return text;
    };
    return {render(selected.at(0)), render(selected.at(1))};
}
} // namespace bl
