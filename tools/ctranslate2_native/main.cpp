// Project-owned, CPU-only framed translation worker. No Python or GPU runtime.
#include <ctranslate2/translator.h>
#include <ctranslate2/types.h>
#include <nlohmann/json.hpp>
#include <sentencepiece_processor.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#endif
namespace {
namespace fs=std::filesystem;
using Json=nlohmann::json;
bool safe_pair(const std::string &s) {
    return s.size()>=3 && s.size()<=48 && std::all_of(s.begin(),s.end(),[](unsigned char c) {
        return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-';
    });
}
bool exact(char *out,size_t count) {
    if(!count)return true;
    std::cin.read(out,std::streamsize(count));return size_t(std::cin.gcount())==count;
}
uint32_t u32() {
    std::array<char,4> b{};if(!exact(b.data(),b.size()))throw std::runtime_error("Incomplete frame");
    return uint8_t(b[0])|(uint32_t(uint8_t(b[1]))<<8)|(uint32_t(uint8_t(b[2]))<<16)|(uint32_t(uint8_t(b[3]))<<24);
}
void reply(bool ok,const std::string &message) {
    if(message.size()>120000)throw std::runtime_error("Response too large");
    std::array<char,5> header{char(ok?0:1)};
    for(int i=0;i<4;++i)header[i+1]=char(uint32_t(message.size())>>(8*i));
    std::cout.write(header.data(),header.size());std::cout.write(message.data(),std::streamsize(message.size()));std::cout.flush();
    if(!std::cout)throw std::runtime_error("Closed output pipe");
}
struct Character { uint32_t value;size_t first,last; };
std::vector<Character> characters(const std::string &s) {
    std::vector<Character> out;
    for(size_t i=0;i<s.size();) {
        const auto first=i;unsigned char lead=uint8_t(s[i++]);uint32_t cp=lead;int remaining=0;
        if(lead>=0xc2&&lead<=0xdf){cp=lead&31;remaining=1;}
        else if(lead>=0xe0&&lead<=0xef){cp=lead&15;remaining=2;}
        else if(lead>=0xf0&&lead<=0xf4){cp=lead&7;remaining=3;}
        else if(lead>=0x80)throw std::runtime_error("Invalid UTF-8");
        for(int n=0;n<remaining;++n){if(i>=s.size()||(uint8_t(s[i])&0xc0)!=0x80)throw std::runtime_error("Invalid UTF-8");cp=(cp<<6)|(uint8_t(s[i++])&63);}
        if((remaining==1&&cp<0x80)||(remaining==2&&cp<0x800)||(remaining==3&&cp<0x10000)||
           cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))throw std::runtime_error("Invalid UTF-8");
        out.push_back({cp,first,i});
    }
    return out;
}
bool space(uint32_t c) {
    return (c>=9&&c<=13)||c==32||c==0x85||c==0xa0||c==0x1680||(c>=0x2000&&c<=0x200a)||
           c==0x2028||c==0x2029||c==0x202f||c==0x205f||c==0x3000;
}
bool word(uint32_t c) {
    if((c>='0'&&c<='9')||(c>='a'&&c<='z')||(c>='A'&&c<='Z'))return true;
#ifdef _WIN32
    if(c<=0xffff) {wchar_t value=wchar_t(c);WORD type=0;return GetStringTypeW(CT_CTYPE1,&value,1,&type)&&(type&(C1_ALPHA|C1_DIGIT));}
    return (c>=0x20000&&c<=0x323af); // Supplementary CJK, not emoji/punctuation.
#else
    return c>=0x80&&!space(c)&&!(c>=0x2000&&c<=0x206f)&&!(c>=0x3000&&c<=0x303f);
#endif
}
std::string path_text(const fs::path &path) {auto u=path.generic_u8string();return std::string(u.begin(),u.end());}
struct Pair {
    ctranslate2::Translator translator;
    sentencepiece::SentencePieceProcessor source,target;
    explicit Pair(const fs::path &root):translator(path_text(root),ctranslate2::Device::CPU,
        ctranslate2::ComputeType::INT8,{0},false,[] {ctranslate2::ReplicaPoolConfig c;c.num_threads_per_replica=4;return c;}()) {
        if(!source.Load(path_text(root/"source.spm")).ok()||!target.Load(path_text(root/"target.spm")).ok())
            throw std::runtime_error("Tokenizer could not load");
    }
    std::string chunk(const std::string &text) {
        auto chars=characters(text);
        if(std::none_of(chars.begin(),chars.end(),[](const auto &c){return word(c.value);}))return text;
        size_t first=0,last=chars.size();while(first<last&&space(chars[first].value))++first;
        while(last>first&&space(chars[last-1].value))--last;
        auto begin=chars[first].first,end=chars[last-1].last;
        std::vector<std::string> input;
        if(!source.Encode(text.substr(begin,end-begin),&input).ok())throw std::runtime_error("Tokenization failed");
        input.push_back("</s>");
        ctranslate2::TranslationOptions options;options.beam_size=1;options.return_end_token=true;
        options.max_decoding_length=std::min(size_t(512),std::max(size_t(64),input.size()*4+20));
        auto results=translator.translate_batch({input},options);
        if(results.empty()||results[0].hypotheses.empty()||results[0].hypotheses[0].empty())throw std::runtime_error("Empty translation");
        auto &tokens=results[0].hypotheses[0];
        if(tokens.back()!="</s>")throw std::runtime_error("Translation was truncated");
        tokens.pop_back();std::string output;
        if(!target.Decode(tokens,&output).ok()||output.empty())throw std::runtime_error("Decode failed");
        auto translated=characters(output);size_t a=0,b=translated.size();
        while(a<b&&space(translated[a].value))++a;
        while(b>a&&space(translated[b-1].value))--b;
        if(a==b)throw std::runtime_error("Empty translation");
        return text.substr(0,begin)+output.substr(translated[a].first,translated[b-1].last-translated[a].first)+text.substr(end);
    }
    std::string translate(const std::string &text) {
        characters(text); // Validate the whole frame, including protected spans.
        std::string output;size_t from=0,search=0;
        while((search=text.find("[[BLG_LINK_",search))!=std::string::npos) {
            auto digits=search+11,end=text.find("]]",digits);
            if(end==std::string::npos||end==digits||end-digits>3||
               !std::all_of(text.begin()+digits,text.begin()+end,[](char c){return c>='0'&&c<='9';})){++search;continue;}
            output+=chunk(text.substr(from,search-from));output+=text.substr(search,end+2-search);from=search=end+2;
        }
        return output+chunk(text.substr(from));
    }
};
bool complete(const fs::path &root) {
    for(auto file:{"model.bin","config.json","shared_vocabulary.json","source.spm","target.spm"})
        if(!fs::is_regular_file(root/file))return false;
    return true;
}
}
int run(int argc,char **argv) {
    try {
        if(argc==2&&std::string(argv[1])=="--self-test") {
            if(!ctranslate2::mayiuse_int8(ctranslate2::Device::CPU))return 2;
            std::cout<<"BLCT2_NATIVE_READY\n";return 0;
        }
#ifdef _WIN32
        _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
#endif
        fs::path root,bundled,catalog;
        for(int i=1;i+1<argc;i+=2) {
            std::string key=argv[i];auto path=fs::u8path(argv[i+1]);
            if(key=="--models-root")root=path;else if(key=="--bundled-root")bundled=path;
            else if(key=="--catalog")catalog=path;else return 2;
        }
        if(root.empty()||catalog.empty())return 2;
        std::ifstream file(catalog,std::ios::binary);if(!file)return 2;
        auto models=Json::parse(file).at("models");std::map<std::string,fs::path> directories;
        for(const auto &model:models) {
            auto id=model.at("source").get<std::string>()+"-"+model.at("target").get<std::string>();
            auto old=model.at("pair").get<std::string>();if(!safe_pair(id)||!safe_pair(old))return 2;
            auto selected=root/"models"/id;
            directories[id]=complete(selected)?selected:bundled/"models"/old;
        }
        std::list<std::pair<std::string,std::unique_ptr<Pair>>> loaded;
        std::cout<<"BLCT3\n";std::cout.flush();
        for(;;) {
            char mode=0;if(!exact(&mode,1))return 0;
            std::string name;uint32_t length=0;
            if(mode==2) {char n=0;if(!exact(&n,1))return 1;length=u32();if(uint8_t(n)<3||uint8_t(n)>48||!length||length>12000)return 1;
                name.resize(uint8_t(n));if(!exact(name.data(),name.size()))return 1;
            }else if(mode==0||mode==1){length=u32();name=mode==0?"en-zh-Hans":"zh-Hans-en";if(!length||length>12000)return 1;}
            else return 1;
            std::string text(length,'\0');if(!exact(text.data(),text.size()))return 1;
            try {
                if(!safe_pair(name)||!directories.count(name))throw std::runtime_error("Unsupported direction");
                auto it=std::find_if(loaded.begin(),loaded.end(),[&](const auto &p){return p.first==name;});
                if(it==loaded.end()) {
                    if(!complete(directories.at(name)))throw std::runtime_error("Language pack is not prepared");
                    if(loaded.size()>=2)loaded.pop_back();
                    loaded.emplace_front(name,std::make_unique<Pair>(directories.at(name)));it=loaded.begin();
                } else loaded.splice(loaded.begin(),loaded,it);
                reply(true,loaded.front().second->translate(text));
            }catch(const std::exception &e){reply(false,e.what());}
        }
    }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
#ifdef _WIN32
int wmain(int argc,wchar_t **argv) {
    std::vector<std::string> values;std::vector<char*> pointers;
    for(int i=0;i<argc;++i) {
        int count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,argv[i],-1,nullptr,0,nullptr,nullptr);
        if(count<=0)return 2;
        std::string value(size_t(count),'\0');
        WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,argv[i],-1,value.data(),count,nullptr,nullptr);
        value.pop_back();values.push_back(std::move(value));
    }
    for(auto &value:values)pointers.push_back(value.data());
    return run(argc,pointers.data());
}
#else
int main(int argc,char **argv){return run(argc,argv);}
#endif
