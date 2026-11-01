#include <fstream>
#include <string>

#ifdef _WIN32
#include "../tools/cli/process.hpp"
#endif

namespace {
template<typename Argument>
int record(int argc, Argument argv[])
{
    std::ofstream output("tool-calls.txt", std::ios::app | std::ios::binary);
    if (!output) {
        return 91;
    }
    bool building = false;
    bool configure_failure = false;
    bool build_failure = false;
    for (int index = 1; index < argc; ++index) {
#ifdef _WIN32
        const std::string argument = lykoware::cli::to_utf8(argv[index]);
#else
        const std::string argument{argv[index]};
#endif
        output << argument << '\n';
        building = building || argument == "--build";
        configure_failure = configure_failure || argument == "fail-configure";
        build_failure = build_failure || argument == "fail-build";
    }
    output << "END\n";
    if (configure_failure) {
        return 17;
    }
    return building && build_failure ? 23 : 0;
}
}

#ifdef _WIN32
int wmain(int argc, wchar_t* argv[])
{
    return record(argc, argv);
}
#else
int main(int argc, char* argv[])
{
    return record(argc, argv);
}
#endif
