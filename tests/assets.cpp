#include <lykoware/assets.hpp>

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

int main(int argc, char** argv)
{
    if (argc < 2 || argc > 3) { return EXIT_FAILURE; }
    lykoware::assets::Package package;
    package["sentinel"].bytes = {7};
    std::string error;
    const bool loaded = lykoware::assets::load(argv[1], 1024 * 1024, package, error);
    if (argc == 3 && std::string_view(argv[2]) == "--reject") {
        if (loaded || error.empty() || package.size() != 1 || package.at("sentinel").bytes != std::vector<std::uint8_t>{7}) {
            std::cerr << "invalid package accepted or output changed\n";
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }
    if (!loaded || package.size() != 2 || package.at("model").bytes != std::vector<std::uint8_t>{1, 2, 3}
        || package.at("texture").bytes != std::vector<std::uint8_t>{0, 255, 17}
        || package.at("model").dependencies != std::vector<std::string>{"texture"}) {
        std::cerr << "package mismatch: " << error << '\n';
        return EXIT_FAILURE;
    }
    const auto before = package.at("model").bytes;
    if (lykoware::assets::decode("{}", 1, package, error) || package.at("model").bytes != before) {
        return EXIT_FAILURE;
    }
    if (lykoware::assets::load(argv[1], 1, package, error) || package.at("model").bytes != before) {
        return EXIT_FAILURE;
    }
    std::cout << "source-free package passed\n";
    return EXIT_SUCCESS;
}
