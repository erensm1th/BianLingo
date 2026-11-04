#include "bianlingo/local_models.hpp"
#include <iostream>
#include <zstd.h>
using namespace bl;
namespace {
void check(bool condition, const char *reason) { if (!condition) throw std::runtime_error(reason); }
template<class F> void rejects(F work) { try { work(); } catch (...) { return; } throw std::runtime_error("Invalid model operation accepted"); }
void save(const std::filesystem::path &p, std::string_view content) {
    atomic_write(p, std::span(reinterpret_cast<const uint8_t *>(content.data()), content.size()));
}
}
int main(int argc, char **argv) {
    auto parent = std::filesystem::weakly_canonical(std::filesystem::temp_directory_path());
    auto scratch = parent / ("bianlingo-models-" + sha256(random_bytes(16)).substr(0, 16));
    struct Cleanup {
        std::filesystem::path root, parent;
        ~Cleanup() { std::error_code e; auto actual = std::filesystem::weakly_canonical(root, e);
            if (!e && actual.parent_path() == parent && actual.filename().string().starts_with("bianlingo-models-"))
                std::filesystem::remove_all(actual, e); }
    } cleanup{scratch, parent};
    try {
        if (argc == 5 && std::string_view(argv[1]) == "--prepare-direct") {
            BergamotModels real(argv[2], argv[3]); real.prepare({argv[4]});
            real.prepare_pair(argv[4]); std::cout << "PASS prepared direct model " << argv[4] << '\n'; return 0;
        }
        if (argc == 4 && std::string_view(argv[1]) == "--verify-bundled") {
            BergamotModels real(argv[2], argv[3]);
            real.prepare({"en-zh-Hans", "zh-Hans-en"}, {}, {}, false);
            real.prepare_pair("en-zh-Hans"); real.prepare_pair("zh-Hans-en");
            auto available = real.inventory();
            check(std::any_of(available.begin(), available.end(), [](const auto &p) { return p.id == "en-zh-Hans" && p.installed; }), "Real bundled models did not prepare both directions");
            std::cout << "PASS real bundled model decompression, pinned hashes and offline pair preparation\n";
            return 0;
        }
        auto root = scratch / "runtime", cache = scratch / "cache";
        auto manifest = read_json(TEST_MODEL_MANIFEST);
        std::map<std::string, std::string> payloads;
        uint64_t fr_size = 0;
        for (auto &r : manifest["models"]) {
            auto value = "fixture:" + r["pair"].get<std::string>() + ":" + r["name"].get<std::string>();
            r["size"] = uint64_t(value.size()); r["sha256"] = sha256(value);
            if (r["pair"] == "en-fr" || r["pair"] == "fr-en") fr_size += value.size();
            payloads[r["url"].get<std::string>()] = value;
            if (r.contains("compressed_sha256")) {
                Bytes packed(ZSTD_compressBound(value.size()));
                packed.resize(ZSTD_compress(packed.data(), packed.size(), value.data(), value.size(), 1));
                r["compressed_sha256"] = sha256(packed);
                atomic_write(root / "compressed" / r["pair"].get<std::string>() / (r["name"].get<std::string>() + ".zst"), packed);
            }
        }
        write_json(root / "models.json", manifest);
        int fetches = 0;
        auto fetch = [&](std::string_view url, const std::filesystem::path &out, uint64_t limit, const download::Progress &progress) {
            ++fetches;
            const auto &value = payloads.at(std::string(url)); check(value.size() == limit, "Download limit is not pinned");
            progress(0); save(out, value); progress(value.size());
        };
        BergamotModels store(root, cache);
        auto inventory = store.inventory();
        auto installed = [&](std::string_view id) { auto all = store.inventory(); return std::find_if(all.begin(), all.end(), [&](const auto &p) { return p.id == id; })->installed; };
        check(inventory.size() > 8 && !installed("en-fr") && !installed("en-zh-Hans"),
              "Initial inventory confuses bundled and installed models");
        check(local_pack_for_target("en-GB") == "en" && local_pack_for_target("fr") == "fr" &&
              local_pack_for_target("ja") == "ja", "Current-target model selection is incorrect");
        check(store.route("ja", "zh-Hant") == std::vector<std::string>{"ja-en", "en-zh-Hant"} &&
              store.route("pt-BR", "en") == std::vector<std::string>{"pt-en"}, "Catalogue did not plan pivot or regional aliases");
        rejects([&] { store.route("en", "xx"); });
        ModelProgress last;
        store.prepare({"en-fr", "fr-en", "en-fr"}, {}, [&](const auto &p) { last = p; }, true, fetch);
        check(fetches == 6 && last.total == fr_size && last.done == last.total && (installed("en-fr") && installed("fr-en")) &&
              !installed("en-de"), "Selection downloaded extra languages or missed one direction");
        store.prepare({"en-fr", "fr-en"}, {}, {}, true, fetch);
        check(fetches == 6, "Verified files downloaded again");
        store.prepare({"en-zh-Hans", "zh-Hans-en"}, {}, {}, false, fetch);
        check(fetches == 6 && installed("en-zh-Hans"), "Bundled Chinese models require a network");
        // Translation can use one existing direction; it must never start a download.
        auto first = manifest["models"][7];
        auto broken = cache / "models" / first["pair"].get<std::string>() / first["name"].get<std::string>();
        save(broken, std::string(size_t(first["size"].get<uint64_t>()), 'x'));
        rejects([&] { store.prepare_pair("en-fr"); });
        store.prepare_pair("fr-en");
        store.prepare({"en-fr", "fr-en"}, {}, {}, true, fetch);
        check(fetches == 7, "A single corrupt file did not trigger a single repair");
        rejects([&] { store.prepare({"en-de"}, {}, {}, false, fetch); });
        check(fetches == 7, "Offline preparation attempted a download");
        rejects([&] { store.prepare({"../../outside"}, {}, {}, true, fetch); });
        rejects([&] { store.prepare({}, {}, {}, true, fetch); });
        std::stop_source stop; stop.request_stop();
        rejects([&] { store.prepare({"en-es"}, stop.get_token(), {}, true, fetch); });
        check(fetches == 7, "Cancelled job started a download");
        std::stop_source mid;
        rejects([&] { store.prepare({"en-de"}, mid.get_token(), {}, true,
            [&](auto, const auto &out, auto, const auto &progress) { save(out, "partial"); mid.request_stop(); progress(1); }); });
        rejects([&] { store.prepare({"en-de"}, {}, {}, true,
            [&](auto, const auto &out, auto, const auto &) { save(out, "wrong hash"); }); });
        for (const auto &entry : std::filesystem::recursive_directory_iterator(cache))
            check(entry.path().extension() != ".download", "Failed/cancelled download leaked its staging file");
        // An existing direction remains usable while another pack is downloading.
        std::promise<void> entered, release;
        auto released = release.get_future().share();
        std::jthread background([&](std::stop_token stop) {
            try { store.prepare({"en-es"}, stop, {}, true, [&](auto, const auto &, auto, const auto &) {
                entered.set_value(); released.wait_for(std::chrono::seconds(3)); throw std::runtime_error("synthetic network stop");
            }); } catch (...) {}
        });
        check(entered.get_future().wait_for(std::chrono::seconds(2)) == std::future_status::ready, "Background model job did not start");
        auto started = Clock::now();
        store.prepare_pair("fr-en");
        rejects([&] { store.prepare_pair("en-de"); });
        check(Clock::now() - started < std::chrono::milliseconds(200), "Translation was blocked by an unrelated download");
        release.set_value(); background.join();
        auto bad = manifest; bad["models"][0]["name"] = "../escape";
        write_json(root / "models.json", bad);
        rejects([&] { BergamotModels invalid(root, cache); });
        std::cout << "PASS selective bilateral packs, inventory, bundled unpack, verification, repair, cancellation and offline translation\n";
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
