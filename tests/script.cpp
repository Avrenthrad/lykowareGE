// script.sandbox (ADR 0041): the sandboxed Lua scripting API. No files, no GPU.

#include <lykoware/script.hpp>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using lykoware::script::Script;
using lykoware::script::ScriptConfig;
using lykoware::script::Value;

int g_failures = 0;

void expect(bool condition, std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

bool contains(const std::string& text, std::string_view part)
{
    return text.find(part) != std::string::npos;
}

// A script whose printed lines are collected.
struct Fixture {
    std::unique_ptr<Script> script;
    std::vector<std::string> lines;
};

Fixture make(const ScriptConfig& config = {})
{
    Fixture fixture;
    std::string error;
    fixture.script = Script::create(config, error);
    expect(fixture.script != nullptr, "a script is created: " + error);
    if (fixture.script) {
        auto* lines = &fixture.lines;
        fixture.script->set_print([lines](std::string_view line) { lines->emplace_back(line); });
    }
    return fixture;
}

// Runs `source`; the error, or "" on success.
std::string run(Script& script, std::string_view source)
{
    std::string error;
    if (!script.run(source, "test.lua", error)) {
        expect(!error.empty(), "a failed run says why");
        return error.empty() ? std::string{"?"} : error;
    }
    return {};
}

void test_runs_and_prints()
{
    Fixture f = make();
    if (!f.script) {
        return;
    }
    expect(run(*f.script, "print('hello', 1, 2.5, true, nil)\nx = 40 + 2").empty(), "a chunk runs");
    expect(f.lines.size() == 1 && f.lines[0] == "hello\t1\t2.5\ttrue\tnil", "print goes to the sink, tab-separated");
    std::vector<Value> results;
    std::string error;
    expect(f.script->run("function answer() return x end", "test.lua", error), "a function is defined");
    expect(f.script->call("answer", {}, results, error) && results.size() == 1
               && std::get_if<std::int64_t>(&results[0]) != nullptr && std::get<std::int64_t>(results[0]) == 42,
           "globals persist between runs, and integers stay integers");
    expect(f.script->memory_used() > 0, "memory is accounted");
    expect(f.script->instructions_used() <= 1000, "instructions are counted in steps of 1,000");
}

void test_errors_name_their_place()
{
    Fixture f = make();
    if (!f.script) {
        return;
    }
    const std::string syntax = run(*f.script, "x = = 1");
    expect(contains(syntax, "test.lua:1:"), "a syntax error names the chunk and line: " + syntax);
    const std::string runtime = run(*f.script, "\n\nlocal t = nil\nreturn t.field");
    expect(contains(runtime, "test.lua:4:") && contains(runtime, "nil"), "a runtime error names its line: " + runtime);
    const std::string thrown = run(*f.script, "error({})");
    expect(contains(thrown, "error object is a table"), "a non-string error is described: " + thrown);
    expect(run(*f.script, "y = 1").empty(), "the state is usable after errors");
}

void test_sandbox_has_no_way_out()
{
    Fixture f = make();
    if (!f.script) {
        return;
    }
    for (const char* name : {"io", "os", "debug", "package", "require", "dofile", "loadfile", "load",
                             "collectgarbage", "string.dump", "math.randomseed", "module", "loadstring"}) {
        const std::string source = std::string{"assert("} + name + " == nil, '" + name + " is reachable')";
        const std::string error = run(*f.script, source);
        expect(error.empty() || contains(error, "attempt to index a nil value"), std::string{name} + " is absent: " + error);
    }
    for (const char* name : {"string.format", "table.sort", "math.floor", "utf8.char", "coroutine.wrap", "pcall",
                             "ipairs", "setmetatable", "tostring"}) {
        expect(run(*f.script, std::string{"assert(type("} + name + ") == 'function')").empty(),
               std::string{name} + " is available");
    }
    const std::string bytecode = run(*f.script, std::string{"\x1bLua"} + std::string(40, '\0'));
    expect(contains(bytecode, "attempt to load a binary chunk"), "bytecode is refused before it is read: " + bytecode);
    const std::string finalizer = run(*f.script, "setmetatable({}, {__gc = function() while true do end end})");
    expect(contains(finalizer, "__gc"), "finalizers are refused: " + finalizer);
    expect(run(*f.script, "local m = {} setmetatable({}, m) m.__gc = function() end").empty(),
           "a metatable without __gc is accepted");
}

void test_instruction_limit()
{
    ScriptConfig config;
    config.instruction_limit = 200'000;
    Fixture f = make(config);
    if (!f.script) {
        return;
    }
    // Each way of catching an error: the script stops within one step of the limit.
    const std::pair<std::string_view, std::string_view> cases[] = {
        {"an endless loop", "while true do end"},
        {"pcall", "while true do pcall(function() while true do end end) end"},
        {"xpcall", "while true do xpcall(function() while true do end end, function(e) return e end) end"},
        {"coroutine.resume",
         "while true do coroutine.resume(coroutine.create(function() while true do end end)) end"},
        {"a wrapped coroutine", "while true do pcall(coroutine.wrap(function() while true do end end)) end"},
        {"a __close handler",
         "do local x <close> = setmetatable({}, {__close = function() while true do end end}) while true do end end"},
    };
    for (const auto& [what, source] : cases) {
        const std::string error = run(*f.script, source);
        expect(contains(error, "instruction limit of 200000 reached"), std::string{what} + " is stopped: " + error);
        expect(f.script->instructions_used() > 200'000 && f.script->instructions_used() <= 202'000,
               std::string{what} + " is stopped at the limit, not later: " + std::to_string(f.script->instructions_used()));
    }
    expect(run(*f.script, "for i = 1, 1000 do end").empty(), "the next run starts with a fresh count");
    expect(run(*f.script,
               "local co = coroutine.create(function() local ok = pcall(coroutine.yield, 1) return ok end)\n"
               "local _, a = coroutine.resume(co)\nlocal _, b = coroutine.resume(co)\n"
               "assert(a == 1 and b == true)")
               .empty(),
           "a coroutine still yields across pcall");
}

// Loops inside Lua's C library run no Lua instructions, so the instruction limit cannot stop
// them; the sandbox bounds the ones a script could make endless without using memory.
void test_library_loops_are_bounded()
{
    Fixture f = make();
    if (!f.script) {
        return;
    }
    expect(run(*f.script, "assert(string.rep('', 1 << 40) == '' and ('').rep('', 1 << 62, '') == '')").empty(),
           "repeating an empty string many times is immediate");
    expect(run(*f.script, "assert(string.rep('ab', 3, ',') == 'ab,ab,ab' and ('x'):rep(0) == '')").empty(),
           "string.rep still repeats");
    const std::string move = run(*f.script, "table.move({}, 1, 1 << 40, 2)");
    expect(contains(move, "memory"), "moving more elements than memory could hold is refused: " + move);
    // Keys 2^40, 2^39, ..., 1 inserted in that order: Lua reports a length of 2^40 for 41 elements.
    const std::string sparse = run(*f.script, "local t = {} for k = 40, 0, -1 do t[1 << k] = k end\n"
                                              "assert(#t >= 1 << 40, 'no huge border')\ntable.insert(t, 1, 'x')");
    expect(contains(sparse, "memory"), "inserting into a sparse table with a huge border is refused: " + sparse);
    const std::string removed =
        run(*f.script, "local t = {} for k = 40, 0, -1 do t[1 << k] = k end\ntable.remove(t, 1)");
    expect(contains(removed, "memory"), "removing from it is refused too: " + removed);
    const std::string claimed = run(*f.script,
                                    "local t = setmetatable({}, {__len = function() return 1 << 40 end})\n"
                                    "table.insert(t, 1, 'x')");
    expect(contains(claimed, "memory"), "a __len that claims a huge length is refused: " + claimed);
    expect(run(*f.script, "local t = {1, 2, 3} table.insert(t, 1, 0) table.remove(t) table.insert(t, 9)\n"
                          "local m = table.move(t, 1, 3, 2, {}) assert(#t == 4 and t[1] == 0 and m[4] == 2)")
               .empty(),
           "insert, remove and move still work on real tables");
}

void test_memory_limit()
{
    ScriptConfig config;
    config.memory_limit = 1024 * 1024;
    Fixture f = make(config);
    if (!f.script) {
        return;
    }
    const std::string grown = run(*f.script, "local t = {} for i = 1, 1e7 do t[i] = ('x'):rep(64) .. i end");
    expect(contains(grown, "not enough memory"), "runaway memory is refused: " + grown);
    expect(f.script->memory_used() <= config.memory_limit, "the heap never passes the limit");
    const std::string huge = run(*f.script, "local s = ('x'):rep(1e9)");
    expect(contains(huge, "not enough memory"), "one huge string is refused: " + huge);
    expect(run(*f.script, "small = {1, 2, 3}").empty(), "the state is usable after running out");

    std::string error;
    ScriptConfig tiny;
    tiny.memory_limit = lykoware::script::min_memory_limit - 1;
    expect(Script::create(tiny, error) == nullptr && contains(error, "memory_limit"), "a tiny memory limit is refused");
    ScriptConfig short_count;
    short_count.instruction_limit = 999;
    expect(Script::create(short_count, error) == nullptr && contains(error, "instruction_limit"),
           "an instruction limit under one step is refused");
}

void test_host_functions()
{
    Fixture f = make();
    if (!f.script) {
        return;
    }
    std::string error;
    std::vector<Value> seen;
    expect(f.script->define("add",
                            [&seen](std::span<const Value> arguments, std::vector<Value>& results, std::string& failure) {
                                seen.assign(arguments.begin(), arguments.end());
                                if (arguments.size() != 2 || std::get_if<std::int64_t>(&arguments[0]) == nullptr
                                    || std::get_if<std::int64_t>(&arguments[1]) == nullptr) {
                                    failure = "add takes two integers";
                                    return false;
                                }
                                results = {std::get<std::int64_t>(arguments[0]) + std::get<std::int64_t>(arguments[1]),
                                           std::string{"sum"}};
                                return true;
                            },
                            error),
           "a host function is defined");
    expect(run(*f.script, "local n, s = add(40, 2) assert(n == 42 and s == 'sum')").empty(), "a script calls it");
    const std::string refused = run(*f.script, "\nadd(1.5, 2)");
    expect(contains(refused, "test.lua:2:") && contains(refused, "add takes two integers"),
           "its failure is a script error at the call: " + refused);
    expect(run(*f.script, "local ok, e = pcall(add, 'a') assert(not ok and e:find('two integers'))").empty(),
           "pcall catches a host function's failure");
    expect(run(*f.script, "add(nil, true, 'text', 3, 2.5)").size() > 0 && seen.size() == 5
               && std::holds_alternative<std::monostate>(seen[0]) && std::get<bool>(seen[1])
               && std::get<std::string>(seen[2]) == "text" && std::get<std::int64_t>(seen[3]) == 3
               && std::get<double>(seen[4]) == 2.5,
           "arguments arrive as nil, boolean, string, integer and number");
    const std::string table = run(*f.script, "add({}, 1)");
    expect(contains(table, "argument 1 is a table"), "a table argument is refused: " + table);

    expect(f.script->define("explode",
                            [](std::span<const Value>, std::vector<Value>&, std::string&) -> bool {
                                throw std::runtime_error("boom");
                            },
                            error),
           "a throwing host function is defined");
    const std::string thrown = run(*f.script, "explode()");
    expect(contains(thrown, "boom"), "a thrown std::exception becomes a script error: " + thrown);

    expect(f.script->define("reenter",
                            [&f](std::span<const Value>, std::vector<Value>&, std::string& failure) {
                                std::string inner;
                                const bool ran = f.script->run("x = 1", "inner", inner);
                                failure = inner;
                                return ran;
                            },
                            error),
           "a re-entering host function is defined");
    const std::string reentered = run(*f.script, "reenter()");
    expect(contains(reentered, "inside a host function"), "a host function cannot run the script: " + reentered);

    for (const char* bad : {"", "1st", "end", "a-b", "a b"}) {
        expect(!f.script->define(bad, [](std::span<const Value>, std::vector<Value>&, std::string&) { return true; },
                                 error),
               std::string{"'"} + bad + "' is refused as a name");
    }
    expect(!f.script->define("empty", {}, error), "an empty function is refused");
}

void test_calls_from_the_host()
{
    Fixture f = make();
    if (!f.script) {
        return;
    }
    expect(run(*f.script,
               "function describe(a, b, c, d) return type(a), b, c * 2, d .. '!' end\n"
               "function table_result() return {} end\n"
               "function fails() error('bad state') end")
               .empty(),
           "functions are defined");
    std::vector<Value> results;
    std::string error;
    const std::vector<Value> arguments{std::monostate{}, true, std::int64_t{21}, std::string{"hi"}};
    expect(f.script->call("describe", arguments, results, error) && results.size() == 4
               && std::get<std::string>(results[0]) == "nil" && std::get<bool>(results[1])
               && std::get<std::int64_t>(results[2]) == 42 && std::get<std::string>(results[3]) == "hi!",
           "call passes arguments and returns results: " + error);
    expect(!f.script->call("missing", {}, results, error) && contains(error, "no global function named 'missing'")
               && results.empty(),
           "a missing function is refused: " + error);
    expect(!f.script->call("table_result", {}, results, error) && contains(error, "result 1 is a table") && results.empty(),
           "a table result is refused: " + error);
    expect(!f.script->call("fails", {}, results, error) && contains(error, "bad state"), "an error is reported: " + error);
    expect(run(*f.script, "setmetatable(_G, {__index = function(_, k) error('lookup of ' .. k) end})").empty(),
           "a metatable on the globals is set");
    expect(!f.script->call("nothing", {}, results, error) && contains(error, "lookup of nothing"),
           "a lookup that runs script code is protected: " + error);
}

// The same script draws the same numbers and walks tables the same way in every state. The
// states are alive together, so they sit at different addresses, as they would in two runs.
std::string fingerprint(Fixture& f)
{
    if (!f.script) {
        return {};
    }
    expect(run(*f.script,
               "local t = {}\n"
               "for i = 1, 200 do t['key' .. i] = i end\n"
               "local order = {}\n"
               "for k in pairs(t) do order[#order + 1] = k end\n"
               "local draws = {}\n"
               "for i = 1, 20 do draws[i] = math.random(1, 1000000) end\n"
               "print(table.concat(order, ','))\n"
               "print(table.concat(draws, ','))")
               .empty(),
           "the fingerprint script runs");
    return f.lines.size() == 2 ? f.lines[0] + "|" + f.lines[1] : std::string{};
}

void test_determinism()
{
    ScriptConfig seven;
    seven.random_seed = 7;
    ScriptConfig eight;
    eight.random_seed = 8;
    Fixture a = make(seven);
    Fixture b = make(seven);
    Fixture c = make(eight);
    const std::string first = fingerprint(a);
    expect(!first.empty() && first == fingerprint(b), "two states with one seed iterate and draw identically");
    const std::string other = fingerprint(c);
    expect(first.substr(first.find('|')) != other.substr(other.find('|')), "another seed draws other numbers");
    expect(first.substr(0, first.find('|')) == other.substr(0, other.find('|')),
           "string-key iteration order does not depend on the seed");
}

}

int main()
{
    test_runs_and_prints();
    test_errors_name_their_place();
    test_sandbox_has_no_way_out();
    test_instruction_limit();
    test_library_loops_are_bounded();
    test_memory_limit();
    test_host_functions();
    test_calls_from_the_host();
    test_determinism();
    if (g_failures != 0) {
        std::cerr << g_failures << " script assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "script sandbox passed\n";
    return EXIT_SUCCESS;
}
