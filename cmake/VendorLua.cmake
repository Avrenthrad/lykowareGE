# Vendored Lua 5.4.9 (scripting). See third_party/lua/PROVENANCE.md and ADR 0041.
#
# One Lykoware translation unit, engine/src/script/lua_impl.cpp, includes the Lua sources it
# uses and compiles them as C++, without Lykoware's warning flags or clang-tidy, like Wuffs.
# Only lykoware::script includes Lua's headers; no Lua type appears in a public header.

function(lykoware_add_lua)
    add_library(lykoware_lua STATIC "${CMAKE_SOURCE_DIR}/engine/src/script/lua_impl.cpp")
    target_include_directories(lykoware_lua SYSTEM PUBLIC "${CMAKE_SOURCE_DIR}/third_party/lua/src")
    target_compile_features(lykoware_lua PRIVATE cxx_std_20)
    set_target_properties(lykoware_lua PROPERTIES POSITION_INDEPENDENT_CODE ON)
    if(LYKOWARE_SANITIZERS)
        target_compile_options(lykoware_lua PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(lykoware_lua PRIVATE -fsanitize=address,undefined)
    endif()
endfunction()
