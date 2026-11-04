#include "bianlingo/local_models.hpp"
#include "bianlingo/bundle.hpp"
#include <fstream>
#include <iostream>
using namespace bl;
namespace {
namespace fs = std::filesystem;
void check(bool ok, const char *why) { if (!ok) throw std::runtime_error(why); }
template<class F> void rejects(F fn) { try { fn(); } catch (...) { return; } throw std::runtime_error("Invalid CT2 operation accepted"); }

}
int main(int argc, char **argv) {
    try {
        if (argc >= 5 && std::string_view(argv[1]) == "--prepare-real") {
            CTranslate2Models store(argv[2], argv[3]); std::vector<std::string> pairs;
            for (int i=4;i<argc;++i) pairs.emplace_back(argv[i]);
            store.prepare(pairs, {}, [](const ModelProgress &p) { if (p.done == p.total) std::cout << p.pair << " complete\n"; });
            for (const auto &pair : pairs) store.prepare_pair(pair);
            std::cout << "PASS real native CT2 runtime + model preparation\n"; return 0;
        }
        auto scratch = fs::temp_directory_path() / ("ct2-model-test-" + sha256(random_bytes(8)).substr(0,12)); fs::create_directories(scratch);
        struct Cleanup { fs::path p; ~Cleanup() { std::error_code e; fs::remove_all(p,e); } } cleanup{scratch};
        auto models = bundled_json("ctranslate2-models.json"), runtime = bundled_json("ctranslate2-runtime.json");
        std::map<std::string, Bytes> blobs;
        for (auto &m : models["models"]) for (auto f=m["files"].begin();f!=m["files"].end();++f) {
            auto text=m["pair"].get<std::string>()+":"+f.key(); Bytes data(text.begin(),text.end());
            f.value()=Json::array({uint64_t(data.size()),sha256(data)});
            auto url="https://huggingface.co/"+m["repository"].get<std::string>()+"/resolve/"+m["revision"].get<std::string>()+"/"+f.key();blobs[url]=data;
        }
        auto root=scratch/"old",cache=scratch/"UserData"/"ctranslate2";
        int probes = 0;
        auto probe = [&](const fs::path &p, std::stop_token stop) {
            check(!stop.stop_requested() && fs::is_regular_file(p/"BianLingoCT2.exe"), "Probe did not receive a complete native runtime");
            ++probes;
        };
        fs::create_directories(root);
        atomic_write(root/"BianLingoCT2.exe",Bytes{'x'});
        auto hash=sha256("x");atomic_write(root/"engine.sha256",Bytes(hash.begin(),hash.end()));
        write_json(root/"native-build.json",{{"binary_sha256",sha256("x")},{"source_lock_sha256",runtime["source_lock_sha256"]},{"protocol","BLCT3"}});
        atomic_write(root/"engine-LICENSES.txt",Bytes{'x'});
        CTranslate2Models store(root,cache,models,runtime,probe);
        check(store.route("zh-Hans","fr")==std::vector<std::string>{"zh-Hans-en","en-fr"}, "Pivot routing failed");
        rejects([&]{store.route("en","zh-Hant");}); rejects([&]{store.prepare_pair("en-fr");});
        int fetches=0; ModelProgress last;
        auto fetch=[&](std::string_view url,const fs::path &path,uint64_t size,const download::Progress &progress){
            ++fetches;auto &data=blobs.at(std::string(url));check(data.size()==size,"Unpinned fetch size");progress(0);atomic_write(path,data);progress(data.size());};
        store.prepare({"en-fr","fr-en","en-fr"},{},[&](auto p){last=p;},true,fetch);
        check(fetches==12 && last.done==last.total && !store.runtime_directory().empty(),"Runtime or selected pairs did not prepare exactly once");
        check(probes == 1, "Prepared runtime was not load-tested before commit");
        check(!fs::exists(cache/"runtime"),"Native runtime must not install Python/wheels");
        check(read_json(cache/"models"/"en-fr"/"ATTRIBUTION.json")["license"]=="Apache-2.0","Model attribution was not installed");
        fs::remove(cache/"models"/"en-fr"/"LICENSE-Upstream.txt");
        store.prepare_pair("en-fr");
        check(fs::is_regular_file(cache/"models"/"en-fr"/"LICENSE-Upstream.txt") && fetches==12,"Missing model license did not repair offline");
        store.prepare_pair("en-fr");store.prepare({"en-fr","fr-en"},{},{},true,fetch);check(fetches==12,"Ready models downloaded again");
        fs::create_directories(root/"models");fs::copy(cache/"models"/"en-fr",root/"models"/"en-fr",fs::copy_options::recursive);
        fs::remove(root/"models"/"en-fr"/"README.md");
        CTranslate2Models normalized(root,scratch/"normalized",models,runtime,probe);
        normalized.prepare_pair("en-fr");
        normalized.prepare({"en-fr"},{},{},true,fetch);
        check(fetches==13,"Fallback weights downloaded instead of copying verified files");
        auto damaged=cache/"models"/"en-fr"/"model.bin";auto bytes=read_bytes(damaged);bytes[0]^=1;atomic_write(damaged,bytes);
        store.prepare_pair("en-fr");store.prepare({"en-fr"},{},{},true,fetch);
        check(fetches==13,"Repair did not reuse a verified fallback model");
        atomic_write(root/"BianLingoCT2.exe",Bytes{'b'});
        rejects([&]{store.prepare({"en-fr"},{},{},true,fetch);});
        check(fetches==13,"Damaged native runtime caused a network runtime download");
        atomic_write(root/"BianLingoCT2.exe",Bytes{'x'});
        std::stop_source stop;
        rejects([&]{store.prepare({"en-de"},stop.get_token(),{},true,[&](auto,const auto &,auto,const auto &progress){stop.request_stop();progress(0);});});
        check(!fs::exists(cache/"models"/"en-de"/"model.bin.download"),"Cancel left an incomplete model");
        rejects([&]{store.prepare({"de-en"},{},{},true,[&](auto,const auto &path,auto,const auto &progress){atomic_write(path,Bytes{1});progress(1);});});
        CTranslate2Models missing(scratch/"no-fallback",scratch/"missing",models,runtime,probe);
        rejects([&]{missing.prepare({"en-fr"},{},{},true,fetch);});
        auto bad=models;bad["models"][0]["pair"]="../outside";rejects([&]{CTranslate2Models invalid(root,cache,bad,runtime);});
        bad=models;bad["models"][0]["revision"]="main";rejects([&]{CTranslate2Models invalid(root,cache,bad,runtime);});
        bad=models;bad["models"][0]["license"]="unknown";rejects([&]{CTranslate2Models invalid(root,cache,bad,runtime);});
        // An unrelated download holds the preparation lock; a ready pair still validates.
        std::promise<void> started,release;auto go=release.get_future();
        std::jthread worker([&]{try{store.prepare({"en-de"},{},{},true,[&](auto,const auto &,auto,const auto &){started.set_value();go.wait();throw std::runtime_error("end fixture");});}catch(...) {}});
        started.get_future().wait();store.prepare_pair("en-fr");release.set_value();worker.join();
        bool reject_probe = true;
        auto probe_cache = scratch/"probe-failure";
        CTranslate2Models failed_probe(root,probe_cache,models,runtime,[&](const auto &, auto) {
            if (reject_probe) throw std::runtime_error("synthetic missing native dependency");
        });
        rejects([&] { failed_probe.prepare({"en-fr"},{},{},true,fetch); });
        check(!fs::exists(probe_cache/"runtime") && !fs::exists(probe_cache/"models"),
              "A runtime failing its native import probe was committed as ready");
        reject_probe = false; failed_probe.prepare({"en-fr"},{},{},true,fetch);
        check(!failed_probe.runtime_directory().empty(), "Dependency repair could not retry preparation");
        std::cout<<"PASS CT2 catalogue, native runtime, selected downloads, offline/pivot, repair, cancellation, corruption and concurrent installed pairs\n";
        return 0;
    } catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
