#include <lykoware/log.hpp>
#include <lykoware/version.hpp>

#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
int run(int argc, char* argv[])
{
    if (argc > 2) {
        throw std::invalid_argument("Expected at most one option. Use --help.");
    }
    if (argc == 2) {
        const std::string_view option{argv[1]};
        if (option == "--version") {
            std::cout << "lykowareGE " << lykoware::version << '\n';
            return EXIT_SUCCESS;
        }
        if (option == "--help") {
            std::cout << "Usage: lykoware_sandbox [--version|--help]\n"
                         "Without options, runs the console foundation check.\n";
            return EXIT_SUCCESS;
        }
        throw std::invalid_argument("Unknown option: " + std::string{option}
                                    + ". Use --help.");
    }

    lykoware::log(lykoware::LogLevel::info,
                  "Starting lykowareGE " + std::string{lykoware::version});
    lykoware::log(lykoware::LogLevel::info,
                  "Engine library linked; sandbox bootstrap ready.");
    lykoware::log(lykoware::LogLevel::info,
                  "Foundation check complete. Window and input arrive in Phase 1.");
    return EXIT_SUCCESS;
}
}

int main(int argc, char* argv[])
{
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        lykoware::log(lykoware::LogLevel::error, error.what());
    } catch (...) {
        lykoware::log(lykoware::LogLevel::error, "Unknown startup failure.");
    }
    return EXIT_FAILURE;
}
