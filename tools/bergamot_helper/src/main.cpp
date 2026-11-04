// Copyright (c) 2026 k3l1. MIT license, see the repository LICENSE.
// Private length-prefixed pipe protocol between the desktop app and its local engine.
#include "translator/parser.h"
#include "translator/response_options.h"
#include "translator/service.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace {
namespace fs = std::filesystem;
using marian::bergamot::BlockingService;
using marian::bergamot::ResponseOptions;
using marian::bergamot::TranslationModel;

bool read_bytes(void *out, size_t size) {
    return std::fread(out, 1, size, stdin) == size;
}

uint32_t read_u32() {
    std::array<unsigned char, 4> bytes{};
    if (!read_bytes(bytes.data(), bytes.size()))
        throw std::runtime_error("Pipe request ended early");
    return uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) |
           (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24);
}

void write_u32(uint32_t value) {
    std::array<unsigned char, 4> bytes{
        static_cast<unsigned char>(value), static_cast<unsigned char>(value >> 8),
        static_cast<unsigned char>(value >> 16), static_cast<unsigned char>(value >> 24)};
    if (std::fwrite(bytes.data(), 1, bytes.size(), stdout) != bytes.size())
        throw std::runtime_error("Pipe response write failed");
}

std::string quote(const fs::path &path) {
    const auto raw = path.generic_u8string();
    std::string result = "\"";
    for (char ch : raw) {
        if (ch == '\\' || ch == '"') result += '\\';
        result += ch;
    }
    return result + '"';
}

std::shared_ptr<TranslationModel> load_model(const fs::path &directory, unsigned char /*direction*/) {
    std::array<fs::path, 4> selected;
    const auto metadata = directory / "files.list";
    if (fs::is_regular_file(metadata)) {
        std::ifstream input(metadata);
        for (auto &file : selected) {
            std::string name;
            if (!std::getline(input, name) || name.empty() || name.size() > 100 || name == "." || name == ".." ||
                !std::all_of(name.begin(), name.end(), [](unsigned char c) {
                    return std::isalnum(c) || c == '.' || c == '-' || c == '_';
                })) throw std::runtime_error("Invalid Bergamot file metadata");
            file = directory / name;
        }
    } else {
    const auto pair = directory.filename().string();
    const auto separator = pair.find('-');
    if (separator == std::string::npos) throw std::runtime_error("Invalid Bergamot language pair");
    auto stem = pair;
    for (size_t at = 0; (at = stem.find("-Hans", at)) != std::string::npos;)
        stem.erase(at, 5);
    stem.erase(std::remove(stem.begin(), stem.end(), '-'), stem.end());
    if (stem.size() != 4) throw std::runtime_error("Unsupported Bergamot language pair");
    const fs::path model = directory / ("model." + stem + ".intgemm.alphas.bin");
    const auto shared_vocab = directory / ("vocab." + stem + ".spm");
    const fs::path vocab = fs::is_regular_file(shared_vocab) ? shared_vocab
                                                              : directory / ("srcvocab." + stem + ".spm");
    const fs::path target_vocab = fs::is_regular_file(shared_vocab) ? shared_vocab
                                                                     : directory / ("trgvocab." + stem + ".spm");
    const fs::path lexicon = directory / ("lex.50.50." + stem + ".s2t.bin");
    selected = {model, vocab, target_vocab, lexicon};
    }
    const auto &[model, vocab, target_vocab, lexicon] = selected;
    for (const auto &file : {model, vocab, target_vocab, lexicon}) {
        if (!fs::is_regular_file(file)) throw std::runtime_error("Bergamot model file missing");
    }
    const std::string options =
        "models: [" + quote(model) + "]\n" +
        "vocabs: [" + quote(vocab) + ", " + quote(target_vocab) + "]\n" +
        "shortlist: [" + quote(lexicon) + ", false]\n" +
        "gemm-precision: int8shiftAlphaAll\nbeam-size: 1\nnormalize: 1\n"
        "word-penalty: 0\nworkspace: 128\nmini-batch-words: 1024\n"
        "max-length-break: 128\nmax-length-factor: 2.0\n"
        "skip-cost: true\nalignment: soft\nssplit-mode: paragraph\nquiet: true\n";
    return std::make_shared<TranslationModel>(marian::bergamot::parseOptionsFromString(options));
}

void respond(bool ok, const std::string &body) {
    if (body.size() > 120000) throw std::runtime_error("Bergamot response too long");
    const unsigned char status = ok ? 0 : 1;
    if (std::fwrite(&status, 1, 1, stdout) != 1) throw std::runtime_error("Pipe response write failed");
    write_u32(static_cast<uint32_t>(body.size()));
    if (!body.empty() && std::fwrite(body.data(), 1, body.size(), stdout) != body.size())
        throw std::runtime_error("Pipe response write failed");
    std::fflush(stdout);
}
} // namespace

int main() {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    std::fwrite("BLMT1\n", 1, 6, stdout);
    std::fflush(stdout);
    BlockingService service({});
    std::shared_ptr<TranslationModel> loaded_model;
    fs::path loaded_directory;
    int loaded_direction = -1;
    for (;;) {
        unsigned char direction = 0;
        if (!read_bytes(&direction, 1)) return 0;
        try {
            unsigned char html = 0;
            if (!read_bytes(&html, 1)) return 1;
            const auto path_size = read_u32();
            const auto text_size = read_u32();
            if (direction > 1 || html > 1 || path_size == 0 || path_size > 4096 ||
                text_size == 0 || text_size > 12000)
                throw std::runtime_error("Invalid Bergamot request size");
            std::string path(path_size, '\0'), text(text_size, '\0');
            if (!read_bytes(path.data(), path.size()) || !read_bytes(text.data(), text.size()))
                return 1;
            const auto directory = fs::u8path(path);
            if (!loaded_model || direction != loaded_direction || directory != loaded_directory) {
                loaded_model = load_model(directory, direction);
                loaded_directory = directory;
                loaded_direction = direction;
            }
            ResponseOptions options;
            options.HTML = html != 0;
            auto translated = service.translateMultiple(loaded_model, {std::move(text)}, {options});
            if (translated.size() != 1 || translated.front().target.text.empty())
                throw std::runtime_error("Bergamot returned no translation");
            respond(true, translated.front().target.text);
        } catch (const std::exception &error) {
            loaded_model.reset();
            loaded_direction = -1;
            respond(false, error.what());
        }
    }
}
