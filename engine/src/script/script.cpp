#include <lykoware/script.hpp>

#include <lykoware/log.hpp>

// Lua is compiled as C++ (lua_impl.cpp), so its headers are included without extern "C".
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#include <array>
#include <cstdlib>
#include <exception>
#include <string>
#include <utility>

namespace lykoware::script {
namespace {

// The instruction count hook runs every this many Lua instructions.
constexpr int hook_step = 1000;

constexpr std::array<std::string_view, 22> keywords{
    "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto", "if",
    "in", "local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while"};

bool identifier(std::string_view name)
{
    if (name.empty() || (name[0] >= '0' && name[0] <= '9')) {
        return false;
    }
    for (const char c : name) {
        const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
        if (!letter && !(c >= '0' && c <= '9')) {
            return false;
        }
    }
    for (const std::string_view keyword : keywords) {
        if (name == keyword) {
            return false;
        }
    }
    return true;
}

const char* type_name(int type)
{
    switch (type) {
    case LUA_TTABLE: return "a table";
    case LUA_TFUNCTION: return "a function";
    case LUA_TUSERDATA:
    case LUA_TLIGHTUSERDATA: return "a userdata";
    case LUA_TTHREAD: return "a coroutine";
    default: return "a value of another type";
    }
}

// Reads the values at stack indices first..last. False, with `error` naming the first value of
// another kind, for tables, functions, coroutines and userdata.
bool read_values(lua_State* L, int first, int last, std::vector<Value>& values, std::string_view what,
                 std::string& error)
{
    values.clear();
    for (int index = first; index <= last; ++index) {
        const int type = lua_type(L, index);
        switch (type) {
        case LUA_TNIL: values.emplace_back(std::monostate{}); break;
        case LUA_TBOOLEAN: values.emplace_back(lua_toboolean(L, index) != 0); break;
        case LUA_TNUMBER:
            if (lua_isinteger(L, index) != 0) {
                values.emplace_back(static_cast<std::int64_t>(lua_tointeger(L, index)));
            } else {
                values.emplace_back(static_cast<double>(lua_tonumber(L, index)));
            }
            break;
        case LUA_TSTRING: {
            std::size_t length = 0;
            const char* text = lua_tolstring(L, index, &length);
            values.emplace_back(std::string{text, length});
            break;
        }
        default:
            error = std::string{what} + " " + std::to_string(index - first + 1) + " is " + type_name(type)
                + "; only nil, booleans, numbers and strings cross between scripts and the host";
            return false;
        }
    }
    return true;
}

// Pushes values; raises a Lua error when the stack cannot grow.
void push_values(lua_State* L, std::span<const Value> values)
{
    luaL_checkstack(L, static_cast<int>(values.size()), "too many values");
    for (const Value& value : values) {
        if (std::holds_alternative<std::monostate>(value)) {
            lua_pushnil(L);
        } else if (const bool* flag = std::get_if<bool>(&value)) {
            lua_pushboolean(L, *flag ? 1 : 0);
        } else if (const std::int64_t* integer = std::get_if<std::int64_t>(&value)) {
            lua_pushinteger(L, static_cast<lua_Integer>(*integer));
        } else if (const double* number = std::get_if<double>(&value)) {
            lua_pushnumber(L, static_cast<lua_Number>(*number));
        } else {
            const std::string& text = std::get<std::string>(value);
            lua_pushlstring(L, text.data(), text.size());
        }
    }
}

}

struct Script::Impl {
    ~Impl()
    {
        if (L != nullptr) {
            lua_close(L);
        }
    }

    static Impl& from(lua_State* L) { return **static_cast<Impl**>(lua_getextraspace(L)); }

    // Lua's allocator, holding the heap to memory_limit.
    static void* allocate(void* user, void* block, std::size_t old_size, std::size_t new_size)
    {
        Impl& impl = *static_cast<Impl*>(user);
        // With no block, old_size is a type tag, not a size.
        const std::size_t old_bytes = block != nullptr ? old_size : 0;
        if (new_size == 0) {
            std::free(block); // NOLINT(cppcoreguidelines-no-malloc): Lua's allocator contract
            impl.memory -= old_bytes;
            return nullptr;
        }
        if (new_size > old_bytes && new_size - old_bytes > impl.memory_limit - impl.memory) {
            return nullptr;
        }
        void* resized = std::realloc(block, new_size); // NOLINT(cppcoreguidelines-no-malloc)
        if (resized != nullptr) {
            impl.memory = impl.memory - old_bytes + new_size;
        }
        return resized;
    }

    static int raise_stop(lua_State* L)
    {
        const Impl& impl = from(L);
        lua_pushlstring(L, impl.stop_message.data(), impl.stop_message.size());
        return lua_error(L);
    }

    // Counts instructions. Past the limit, and every step after that, it raises an error.
    static void count(lua_State* L, lua_Debug* /*debug*/)
    {
        Impl& impl = from(L);
        impl.instructions += hook_step;
        if (!impl.stopped && impl.instructions > impl.instruction_limit) {
            impl.stopped = true;
            impl.stop_message = "instruction limit of " + std::to_string(impl.instruction_limit) + " reached";
        }
        if (impl.stopped) {
            raise_stop(L);
        }
    }

    // pcall, xpcall and coroutine.resume call the original (upvalue 1) and then pass a stop
    // on, so a script past its limit cannot catch the error and carry on.
    static int finish_guarded(lua_State* L, int /*status*/, lua_KContext /*context*/)
    {
        if (from(L).stopped) {
            return raise_stop(L);
        }
        return lua_gettop(L);
    }

    static int guarded(lua_State* L)
    {
        lua_pushvalue(L, lua_upvalueindex(1));
        lua_insert(L, 1);
        lua_callk(L, lua_gettop(L) - 1, LUA_MULTRET, 0, finish_guarded);
        return finish_guarded(L, LUA_OK, 0);
    }

    // setmetatable refusing finalizers: Lua runs __gc with hooks off, beyond the limits.
    static int guarded_setmetatable(lua_State* L)
    {
        if (lua_type(L, 2) == LUA_TTABLE) {
            lua_pushliteral(L, "__gc");
            const bool finalizer = lua_rawget(L, 2) != LUA_TNIL;
            lua_pop(L, 1);
            if (finalizer) {
                return luaL_error(L, "finalizers (__gc) are not allowed in scripts");
            }
        }
        lua_pushvalue(L, lua_upvalueindex(1));
        lua_insert(L, 1);
        lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
        return lua_gettop(L);
    }

    static int print(lua_State* L)
    {
        const int count = lua_gettop(L);
        luaL_Buffer buffer;
        luaL_buffinit(L, &buffer);
        for (int index = 1; index <= count; ++index) {
            if (index > 1) {
                luaL_addchar(&buffer, '\t');
            }
            luaL_tolstring(L, index, nullptr);
            luaL_addvalue(&buffer);
        }
        luaL_pushresult(&buffer);
        std::size_t length = 0;
        const char* text = lua_tolstring(L, -1, &length);
        std::string failure;
        try {
            from(L).sink(std::string_view{text, length});
        } catch (const std::exception& error) {
            failure = std::string{"print failed: "} + error.what();
        }
        if (!failure.empty()) {
            lua_pushlstring(L, failure.data(), failure.size());
            return lua_error(L);
        }
        return 0;
    }

    // A host function (upvalue 1) called from a script.
    static int host(lua_State* L)
    {
        auto* function = static_cast<HostFunction*>(lua_touserdata(L, lua_upvalueindex(1)));
        std::string error;
        int count = -1;
        try {
            std::vector<Value> arguments;
            std::vector<Value> results;
            if (read_values(L, 1, lua_gettop(L), arguments, "argument", error)) {
                if ((*function)(arguments, results, error)) {
                    push_values(L, results);
                    count = static_cast<int>(results.size());
                } else if (error.empty()) {
                    error = "host function failed";
                }
            }
        } catch (const std::exception& failure) { // never Lua's own errors, which are not std::exceptions
            error = std::string{"host function failed: "} + failure.what();
        }
        if (count >= 0) {
            return count;
        }
        luaL_where(L, 1);
        lua_pushlstring(L, error.data(), error.size());
        lua_concat(L, 2);
        return lua_error(L);
    }

    // Builds the sandbox; run protected, since every step can fail for memory.
    static int open(lua_State* L)
    {
        const Impl& impl = from(L);
        const std::array<std::pair<const char*, lua_CFunction>, 6> libraries{{
            {LUA_GNAME, luaopen_base},
            {LUA_COLIBNAME, luaopen_coroutine},
            {LUA_STRLIBNAME, luaopen_string},
            {LUA_TABLIBNAME, luaopen_table},
            {LUA_MATHLIBNAME, luaopen_math},
            {LUA_UTF8LIBNAME, luaopen_utf8},
        }};
        for (const auto& [name, opener] : libraries) {
            luaL_requiref(L, name, opener, 1);
            lua_pop(L, 1);
        }

        // math.random seeded by the host; scripts cannot reseed it.
        lua_getglobal(L, LUA_MATHLIBNAME);
        lua_getfield(L, -1, "randomseed");
        lua_pushinteger(L, static_cast<lua_Integer>(impl.random_seed));
        lua_pushinteger(L, 0);
        lua_call(L, 2, 0);
        lua_pushnil(L);
        lua_setfield(L, -2, "randomseed");
        lua_pop(L, 1);

        lua_getglobal(L, LUA_STRLIBNAME);
        lua_pushnil(L);
        lua_setfield(L, -2, "dump");
        lua_pop(L, 1);

        for (const char* removed : {"dofile", "loadfile", "load", "collectgarbage"}) {
            lua_pushnil(L);
            lua_setglobal(L, removed);
        }
        lua_pushcfunction(L, print);
        lua_setglobal(L, "print");

        for (const char* name : {"pcall", "xpcall"}) {
            lua_getglobal(L, name);
            lua_pushcclosure(L, guarded, 1);
            lua_setglobal(L, name);
        }
        lua_getglobal(L, "setmetatable");
        lua_pushcclosure(L, guarded_setmetatable, 1);
        lua_setglobal(L, "setmetatable");
        lua_getglobal(L, LUA_COLIBNAME);
        lua_getfield(L, -1, "resume");
        lua_pushcclosure(L, guarded, 1);
        lua_setfield(L, -2, "resume");
        lua_pop(L, 1);
        return 0;
    }

    struct CallContext {
        const std::string* name = nullptr;
        std::span<const Value> arguments;
        std::vector<Value>* results = nullptr;
    };

    // Looks up and calls a global function, protected (the lookup can run script code).
    static int call_protected(lua_State* L)
    {
        auto* context = static_cast<CallContext*>(lua_touserdata(L, 1));
        lua_settop(L, 0);
        if (lua_getglobal(L, context->name->c_str()) != LUA_TFUNCTION) {
            return luaL_error(L, "no global function named '%s'", context->name->c_str());
        }
        push_values(L, context->arguments);
        lua_call(L, static_cast<int>(context->arguments.size()), LUA_MULTRET);
        std::string error;
        bool ok = false;
        try {
            ok = read_values(L, 1, lua_gettop(L), *context->results, "result", error);
        } catch (const std::exception& failure) {
            error = failure.what();
        }
        if (!ok) {
            context->results->clear();
            lua_pushlstring(L, error.data(), error.size());
            return lua_error(L);
        }
        return 0;
    }

    struct DefineContext {
        const std::string* name = nullptr;
        HostFunction* function = nullptr;
    };

    static int define_protected(lua_State* L)
    {
        auto* context = static_cast<DefineContext*>(lua_touserdata(L, 1));
        lua_pushlightuserdata(L, context->function);
        lua_pushcclosure(L, host, 1);
        lua_setglobal(L, context->name->c_str());
        return 0;
    }

    // The error object on top of the stack as text, popped. Reads without allocating in Lua.
    std::string pop_error()
    {
        std::string message;
        if (lua_type(L, -1) == LUA_TSTRING) {
            std::size_t length = 0;
            const char* text = lua_tolstring(L, -1, &length);
            message.assign(text, length);
        } else {
            message = std::string{"error object is "} + type_name(lua_type(L, -1));
        }
        lua_pop(L, 1);
        return message;
    }

    // Starts a run() or call(); false when one is already running.
    bool begin(std::string& error)
    {
        if (running) {
            error = "a script cannot be run from inside a host function";
            return false;
        }
        running = true;
        instructions = 0;
        stopped = false;
        stop_message.clear();
        return true;
    }

    lua_State* L = nullptr;
    std::size_t memory_limit = 0;
    std::size_t memory = 0;
    std::uint64_t instruction_limit = 0;
    std::uint64_t instructions = 0;
    std::uint64_t random_seed = 0;
    bool stopped = false;
    bool running = false;
    std::string stop_message;
    PrintSink sink;
    // Host functions, at stable addresses for their closures.
    std::vector<std::unique_ptr<HostFunction>> functions;
};

Script::Script(std::unique_ptr<Impl> impl)
    : impl_{std::move(impl)}
{
}

Script::~Script() = default;

std::unique_ptr<Script> Script::create(const ScriptConfig& config, std::string& error)
{
    error.clear();
    if (config.memory_limit < min_memory_limit) {
        error = "memory_limit must be at least " + std::to_string(min_memory_limit) + " bytes";
        return nullptr;
    }
    if (config.instruction_limit < static_cast<std::uint64_t>(hook_step)) {
        error = "instruction_limit must be at least " + std::to_string(hook_step);
        return nullptr;
    }
    auto impl = std::make_unique<Impl>();
    impl->memory_limit = config.memory_limit;
    impl->instruction_limit = config.instruction_limit;
    impl->random_seed = config.random_seed;
    impl->sink = [](std::string_view line) { log(LogLevel::info, "script: " + std::string{line}); };
    impl->L = lua_newstate(Impl::allocate, impl.get());
    if (impl->L == nullptr) {
        error = "Lua could not start: not enough memory";
        return nullptr;
    }
    *static_cast<Impl**>(lua_getextraspace(impl->L)) = impl.get();
    lua_pushcfunction(impl->L, Impl::open);
    if (lua_pcall(impl->L, 0, 0, 0) != LUA_OK) {
        error = "Lua could not open its libraries: " + impl->pop_error();
        return nullptr;
    }
    lua_sethook(impl->L, Impl::count, LUA_MASKCOUNT, hook_step);
    return std::unique_ptr<Script>(new Script(std::move(impl)));
}

bool Script::run(std::string_view source, std::string_view chunk_name, std::string& error)
{
    error.clear();
    Impl& impl = *impl_;
    if (!impl.begin(error)) {
        return false;
    }
    const std::string name = "=" + std::string{chunk_name};
    int status = luaL_loadbufferx(impl.L, source.data(), source.size(), name.c_str(), "t");
    if (status == LUA_OK) {
        status = lua_pcall(impl.L, 0, 0, 0);
    }
    impl.running = false;
    if (status != LUA_OK) {
        error = impl.pop_error();
        return false;
    }
    return true;
}

bool Script::call(std::string_view name, std::span<const Value> arguments, std::vector<Value>& results,
                  std::string& error)
{
    error.clear();
    results.clear();
    Impl& impl = *impl_;
    if (!impl.begin(error)) {
        return false;
    }
    const std::string global{name};
    Impl::CallContext context{&global, arguments, &results};
    lua_pushcfunction(impl.L, Impl::call_protected);
    lua_pushlightuserdata(impl.L, &context);
    const int status = lua_pcall(impl.L, 1, 0, 0);
    impl.running = false;
    if (status != LUA_OK) {
        results.clear();
        error = impl.pop_error();
        return false;
    }
    return true;
}

bool Script::define(std::string_view name, HostFunction function, std::string& error)
{
    error.clear();
    Impl& impl = *impl_;
    if (impl.running) {
        error = "functions cannot be defined from inside a host function";
        return false;
    }
    if (!identifier(name)) {
        error = "'" + std::string{name} + "' is not a Lua identifier";
        return false;
    }
    if (!function) {
        error = "the host function is empty";
        return false;
    }
    impl.functions.push_back(std::make_unique<HostFunction>(std::move(function)));
    const std::string global{name};
    Impl::DefineContext context{&global, impl.functions.back().get()};
    lua_pushcfunction(impl.L, Impl::define_protected);
    lua_pushlightuserdata(impl.L, &context);
    if (lua_pcall(impl.L, 1, 0, 0) != LUA_OK) {
        impl.functions.pop_back();
        error = impl.pop_error();
        return false;
    }
    return true;
}

void Script::set_print(PrintSink sink)
{
    impl_->sink = sink ? std::move(sink) : PrintSink{[](std::string_view) {}};
}

std::size_t Script::memory_used() const
{
    return impl_->memory;
}

std::uint64_t Script::instructions_used() const
{
    return impl_->instructions;
}

}
