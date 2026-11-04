#include "bianlingo/package.hpp"
#include <iostream>

int main(int argc, char **argv) {
    try {
        if (argc != 4) return 2;
        const auto payload = bl::read_bytes(argv[1]);
        bl::unpack_package(payload, bl::read_json(argv[2]), std::filesystem::path(argv[3]));
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
