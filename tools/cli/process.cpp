#include "process.hpp"

#include <iostream>
#include <stdexcept>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <limits>
#include <string_view>

namespace lykoware::cli {
namespace {
std::wstring to_wide(const std::string& text)
{
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(), -1,
                                       nullptr, 0);
    if (size == 0) {
        throw std::runtime_error("Invalid UTF-8 command argument.");
    }
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(), -1,
                            result.data(), size) == 0) {
        throw std::runtime_error("Unable to convert command argument.");
    }
    result.pop_back();
    return result;
}

// Encode exactly one argument using the Windows CRT quoting rules. In
// particular, backslashes before quotes and the closing quote must be doubled.
std::wstring quote(std::wstring_view text)
{
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (const wchar_t character : text) {
        if (character == L'\\') {
            ++slashes;
            continue;
        }
        if (character == L'"') {
            result.append(slashes * 2 + 1, L'\\');
        } else {
            result.append(slashes, L'\\');
        }
        slashes = 0;
        result += character;
    }
    result.append(slashes * 2, L'\\');
    result += L'"';
    return result;
}

struct ProcessHandles {
    PROCESS_INFORMATION value{};
    ~ProcessHandles()
    {
        if (value.hThread != nullptr) {
            CloseHandle(value.hThread);
        }
        if (value.hProcess != nullptr) {
            CloseHandle(value.hProcess);
        }
    }
    ProcessHandles() = default;
    ProcessHandles(const ProcessHandles&) = delete;
    ProcessHandles& operator=(const ProcessHandles&) = delete;
};
}

std::string to_utf8(const wchar_t* text)
{
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1,
                                       nullptr, 0, nullptr, nullptr);
    if (size == 0) {
        throw std::runtime_error("Invalid Unicode command argument.");
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, result.data(),
                            size, nullptr, nullptr) == 0) {
        throw std::runtime_error("Unable to convert command argument.");
    }
    result.pop_back();
    return result;
}

int run_process(const std::string& executable, const std::vector<std::string>& arguments,
                const std::filesystem::path& directory)
{
    std::wstring command = quote(to_wide(executable));
    for (const auto& argument : arguments) {
        command += L' ';
        command += quote(to_wide(argument));
    }
    STARTUPINFOW startup{};
    startup.cb = static_cast<DWORD>(sizeof(startup));
    ProcessHandles process;
    if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, 0, nullptr,
                       directory.c_str(), &startup, &process.value) == 0) {
        std::cerr << "lyko: cannot launch " << executable << " (Windows error "
                  << GetLastError() << ").\n";
        return 127;
    }
    if (WaitForSingleObject(process.value.hProcess, INFINITE) != WAIT_OBJECT_0) {
        throw std::runtime_error("Unable to wait for child process.");
    }
    DWORD exit_code = 0;
    if (GetExitCodeProcess(process.value.hProcess, &exit_code) == 0) {
        throw std::runtime_error("Unable to read child exit code.");
    }
    return exit_code <= static_cast<DWORD>(std::numeric_limits<int>::max())
        ? static_cast<int>(exit_code) : 1;
}
}
#else
#include <cerrno>
#include <cstdio>
#include <sys/wait.h>
#include <unistd.h>

namespace lykoware::cli {
int run_process(const std::string& executable, const std::vector<std::string>& arguments,
                const std::filesystem::path& directory)
{
    std::vector<char*> argv;
    argv.reserve(arguments.size() + 2);
    argv.push_back(const_cast<char*>(executable.c_str()));
    for (const auto& argument : arguments) {
        argv.push_back(const_cast<char*>(argument.c_str()));
    }
    argv.push_back(nullptr);
    const pid_t child = fork();
    if (child < 0) {
        std::perror("lyko: fork");
        return 127;
    }
    if (child == 0) {
        if (chdir(directory.c_str()) != 0) {
            std::perror("lyko: child working directory");
            _exit(127);
        }
        execvp(executable.c_str(), argv.data());
        std::perror("lyko: cannot launch tool");
        _exit(127);
    }
    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) {
            throw std::runtime_error("Unable to wait for child process.");
        }
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return WIFSIGNALED(status) ? 128 + WTERMSIG(status) : 1;
}
}
#endif
