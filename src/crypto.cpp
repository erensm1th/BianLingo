#include "bianlingo/core.hpp"
#include <algorithm>
#include <fstream>
#include <limits>
#include <openssl/evp.h>
#include <openssl/rand.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace bl {
std::string canonical(const Json &value) {
    return value.dump(-1, ' ', true, Json::error_handler_t::strict);
}
Bytes random_bytes(size_t size) {
    if (size > INT_MAX)
        throw std::runtime_error("Random request too large");
    Bytes out(size);
    if (RAND_bytes(out.data(), int(size)) != 1)
        throw std::runtime_error("Secure RNG failed");
    return out;
}
std::string base64(std::span<const uint8_t> raw) {
    std::string out((raw.size() + 2) / 3 * 4, '\0');
    if (!raw.empty())
        EVP_EncodeBlock(reinterpret_cast<unsigned char *>(out.data()), raw.data(), int(raw.size()));
    while (!out.empty() && out.back() == '=')
        out.pop_back();
    for (auto &c : out) {
        if (c == '+')
            c = '-';
        else if (c == '/')
            c = '_';
    }
    return out;
}
Bytes unbase64(std::string_view s) {
    if (s.size() > 16384 || s.size() % 4 == 1)
        throw std::runtime_error("Invalid base64 size");
    std::string padded(s);
    for (auto &c : padded) {
        if (c == '-')
            c = '+';
        else if (c == '_')
            c = '/';
        else if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')))
            throw std::runtime_error("Invalid base64");
    }
    while (padded.size() % 4)
        padded += '=';
    Bytes out(padded.size() / 4 * 3);
    if (padded.empty())
        return out;
    int n = EVP_DecodeBlock(out.data(), reinterpret_cast<const unsigned char *>(padded.data()),
                            int(padded.size()));
    if (n < 0)
        throw std::runtime_error("Invalid base64");
    if (padded.back() == '=')
        n--;
    if (padded.size() > 1 && padded[padded.size() - 2] == '=')
        n--;
    out.resize(n);
    return out;
}
std::string sha256(std::span<const uint8_t> data) {
    std::array<uint8_t, 32> digest{};
    unsigned n = 0;
    if (EVP_Digest(data.data(), data.size(), digest.data(), &n, EVP_sha256(), nullptr) != 1 || n != 32)
        throw std::runtime_error("SHA256 failed");
    const char *hex = "0123456789abcdef";
    std::string out;
    for (auto c : digest) {
        out += hex[c >> 4];
        out += hex[c & 15];
    }
    return out;
}
std::string sha256(std::string_view s) {
    return sha256(std::span(reinterpret_cast<const uint8_t *>(s.data()), s.size()));
}
Bytes read_bytes(const std::filesystem::path &path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f)
        throw std::runtime_error("Could not read local file");
    auto n = f.tellg();
    if (n < 0 || n > 16 * 1024 * 1024)
        throw std::runtime_error("File is too large");
    Bytes out(static_cast<size_t>(n));
    f.seekg(0);
    if (!out.empty() && !f.read(reinterpret_cast<char *>(out.data()), std::streamsize(out.size())))
        throw std::runtime_error("File read was incomplete");
    return out;
}
void atomic_write(const std::filesystem::path &path, std::span<const uint8_t> data) {
    std::filesystem::create_directories(path.parent_path());
    auto tmp = path;
    tmp += "." + base64(random_bytes(12)) + ".tmp";
    try {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f)
            throw std::runtime_error("Could not save local file");
        f.write(reinterpret_cast<const char *>(data.data()), std::streamsize(data.size()));
        f.flush();
        if (!f)
            throw std::runtime_error("File write failed");
        f.close();
#ifdef _WIN32
        if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("File replacement failed");
#else
        if (chmod(tmp.c_str(), 0600) != 0)
            throw std::runtime_error("Could not set file permissions");
        std::filesystem::rename(tmp, path);
#endif
    } catch (...) {
        std::error_code ec;
        std::filesystem::remove(tmp, ec);
        throw;
    }
}
Json read_json(const std::filesystem::path &p, const Json &fallback) {
    try {
        auto raw = read_bytes(p);
        auto j = Json::parse(raw);
        if (j.is_object())
            return j;
    } catch (const std::exception &) {
    }
    return fallback;
}
void write_json(const std::filesystem::path &p, const Json &value) {
    auto s = value.dump(2);
    atomic_write(p, std::span(reinterpret_cast<const uint8_t *>(s.data()), s.size()));
}
} // namespace bl
