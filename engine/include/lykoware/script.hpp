#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// Scripting (ADR 0041): sandboxed Lua 5.4 behind a Lykoware API. No Lua type appears here.
namespace lykoware::script {

// A value passed between the host and a script: nil, a boolean, an integer, a number or a
// string. Lua keeps integers and floats apart, and so does this. Tables, functions and
// coroutines stay inside the script.
using Value = std::variant<std::monostate, bool, std::int64_t, double, std::string>;

// The smallest memory_limit create() accepts: enough for the libraries and a small script.
inline constexpr std::size_t min_memory_limit = 256 * 1024;

struct ScriptConfig {
    // Most bytes the script's Lua heap may hold. An allocation past it fails, which the script
    // sees as a "not enough memory" error. At least min_memory_limit.
    std::size_t memory_limit = 16 * 1024 * 1024;
    // Lua instructions one run() or call() may execute, counted in steps of 1,000, before it is
    // stopped with an error that the script cannot catch. At least 1,000.
    std::uint64_t instruction_limit = 100'000'000;
    // Seeds math.random, so a script draws the same numbers every run.
    std::uint64_t random_seed = 0;
};

// Receives each line a script prints.
using PrintSink = std::function<void(std::string_view line)>;

// A host function that scripts call by name. It receives the arguments; on success it fills
// `results` and returns true. On failure it fills `error` and returns false, which raises a Lua
// error at the call; the script may catch it with pcall. It must not call back into the same
// Script. A std::exception it throws is turned into such an error; it must not throw anything
// else.
using HostFunction =
    std::function<bool(std::span<const Value> arguments, std::vector<Value>& results, std::string& error)>;

// One sandboxed Lua state.
//
// The sandbox: scripts get Lua's base functions, coroutines, and the string, table, math and
// utf8 libraries. They get no files, processes, clocks, module loading or debug library, and
// none of load, loadfile, dofile, collectgarbage or string.dump. Precompiled bytecode is
// refused. print() goes to the print sink. math.randomseed is removed: the host seeds
// math.random. Finalizers (__gc) are refused, because Lua runs them with the instruction limit
// switched off.
//
// Limits: memory and instructions, as in ScriptConfig. A script that reaches the instruction
// limit is stopped even inside pcall, xpcall or a coroutine. A call into Lua's C library counts
// as one instruction, however long it runs. The library calls that could loop without end
// while using no memory are bounded: string.rep of an empty string returns at once, and
// table.insert, table.remove and table.move refuse lengths and ranges larger than the memory
// limit could hold. A pattern match (string.find, match, gmatch, gsub) can still take very
// long on a pathological pattern. The sandbox protects the engine from mistakes in scripts it runs, such as endless
// loops, runaway memory and reaching for files. It is not yet a defence against hostile code.
//
// Determinism: the string hash seed is fixed and math.random is seeded by the host, so the same
// script given the same calls gives the same results on the same build. Iteration order over
// keys that are tables or functions depends on memory addresses, so it is not repeatable, and
// math functions such as math.sin come from the platform's C library.
//
// Ownership: create() returns the sole owner, which owns the Lua state and the host functions.
// Copy and move are not provided.
//
// Threading: one thread at a time. Nothing runs in the background.
//
// Failure: create() returns nullptr and fills error for limits below their minimums or when Lua
// cannot start. run(), call() and define() return false and fill error; the state stays usable.
// They do not throw for script errors; std::bad_alloc can propagate from the host side.
class Script {
public:
    [[nodiscard]] static std::unique_ptr<Script> create(const ScriptConfig& config, std::string& error);

    ~Script();
    Script(const Script&) = delete;
    Script& operator=(const Script&) = delete;
    Script(Script&&) = delete;
    Script& operator=(Script&&) = delete;

    // Runs Lua source text as one chunk. `chunk_name` names it in error messages, for example
    // "level.lua:3: attempt to call a nil value". False for a syntax error, a runtime error,
    // bytecode, a limit, or when called from inside a host function.
    [[nodiscard]] bool run(std::string_view source, std::string_view chunk_name, std::string& error);

    // Calls the global function `name` with `arguments` and stores what it returns in
    // `results`. False, with `results` empty, when there is no such function, it fails, a limit
    // is reached, it returns a table, function or coroutine, or when called from inside a host
    // function.
    [[nodiscard]] bool call(std::string_view name, std::span<const Value> arguments, std::vector<Value>& results,
                            std::string& error);

    // Makes `function` callable from scripts as the global `name`, replacing any global of that
    // name. False for a name that is not a Lua identifier (letters, digits and underscores, not
    // starting with a digit, not a keyword), for an empty function, or when called from inside
    // a host function.
    [[nodiscard]] bool define(std::string_view name, HostFunction function, std::string& error);

    // Where print() goes. By default each line is logged at info level with a "script: " prefix;
    // an empty sink discards it. A std::exception the sink throws becomes a script error.
    void set_print(PrintSink sink);

    // Bytes the Lua heap holds now.
    [[nodiscard]] std::size_t memory_used() const;
    // Instructions the last run() or call() executed, in steps of 1,000.
    [[nodiscard]] std::uint64_t instructions_used() const;

private:
    struct Impl;
    explicit Script(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

}
