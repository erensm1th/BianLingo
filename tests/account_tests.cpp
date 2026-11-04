#include "bianlingo/core.hpp"
#include <iostream>
using namespace bl;
namespace {
int checks = 0;
void require(bool value, const char *message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F test) {
    try {
        test();
    } catch (const std::exception &) {
        ++checks;
        return;
    }
    throw std::runtime_error("Invalid balance response accepted");
}
} // namespace
int main() {
    try {
        Json data = {{"is_available", true},
                     {"balance_infos", Json::array({{{"currency", "CNY"},
                                                     {"total_balance", "3.2500"},
                                                     {"topped_up_balance", "2.25"},
                                                     {"granted_balance", "1.00"}},
                                                    {{"currency", "USD"},
                                                     {"total_balance", "0.10"},
                                                     {"topped_up_balance", "0.10"},
                                                     {"granted_balance", "0"}}})}};
        auto view = format_balance(data, 1700000000);
        require(view.amount == "¥ 3.25  ·  $ 0.10", "Total balance formatting changed amounts");
        require(view.detail == "Paid ¥ 2.25 · Bonus ¥ 1.00; Paid $ 0.10 · Bonus $ 0.00",
                "Balance breakdown unclear");
        require(view.available && view.state == "Account available" && view.updated.starts_with("Updated at "),
                "Account state or timestamp missing");
        data["is_available"] = false;
        require(format_balance(data, 0).state == "Account unavailable", "Unavailable account shown as available");
        data["balance_infos"] = Json::array({{{"currency", "CNY"}, {"total_balance", "0.00012340"}}});
        view = format_balance(data, 0);
        require(view.amount == "¥ 0.0001234" && view.detail.empty(),
                "Small balance rounded away or missing breakdown invented");
        data["balance_infos"][0]["total_balance"] = "-0.500";
        require(format_balance(data, 0).amount == "¥ -0.50", "Negative balance lost");
        for (auto invalid : {"NaN", "1e20", "1.2.3", "", "<html>", "-", "1."}) {
            data["balance_infos"][0]["total_balance"] = invalid;
            rejects([&] { format_balance(data, 0); });
        }
        rejects([&] { format_balance(Json{{"error", "bad key"}}, 0); });
        data["balance_infos"][0] = Json{{"currency", "CNY"}};
        rejects([&] { format_balance(data, 0); });
        data["balance_infos"] = Json::array();
        require(format_balance(data, 0).amount == "No balance details", "Empty balance shown as zero");
        auto usage = format_usage({{"total_tokens", 12345},
                                   {"prompt_tokens", 12000},
                                   {"completion_tokens", 345},
                                   {"cache_hit_tokens", 4000},
                                   {"requests", 20}}, "zh");
        for (const auto language : {"ja", "ko", "pt-BR", "ru"}) {
            const auto localized = format_usage({{"total_tokens", 12345}, {"requests", 20},
                                                {"prompt_tokens", 12000}, {"cache_hit_tokens", 4000},
                                                {"completion_tokens", 345}}, language);
            require(localized.total.find("12,345") != std::string::npos &&
                    localized.detail.find("12,000") != std::string::npos &&
                    localized.detail.find("4,000") != std::string::npos &&
                    localized.detail.find("345") != std::string::npos && localized.total != usage.total,
                    "New interface locale changed usage values or fell back to Chinese");
        }
        require(usage.total == "累计 12,345 Tokens  ·  20 次请求", "Unreadable token total");
        require(usage.detail == "输入 12,000（含缓存 4,000）  ·  输出 345",
                "Cache counted twice or usage missing");
        require(format_usage(Json::object(), "zh").total == "累计 0 Tokens  ·  0 次请求",
                "Empty usage display failed");
        std::cout << "Passed " << checks << " account presentation assertions\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
