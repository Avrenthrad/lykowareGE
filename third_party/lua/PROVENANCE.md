# Lua

- **Upstream:** https://www.lua.org (PUC-Rio). Release archive https://www.lua.org/ftp/lua-5.4.9.tar.gz, dated 2026-08-10.
- **Version:** 5.4.9, the latest bug-fix release of the 5.4 line when vendored.
  - Lua 5.5 (5.5.0, 2025-12-15) also exists. 5.4 was chosen for its longer use in games and engines ([ADR 0041](../../docs/adr/0041-lua-scripting.md)). Moving to 5.5 is a later, separate decision.
- **Integrity:** archive SHA-256 `2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6`, matching the checksum lua.org publishes. Every vendored file is byte-for-byte the archive's.
- **Included:**
  - `src/`: every `.c` and `.h` file of the archive's `src` directory, unmodified;
  - left out: the standalone programs `lua.c` and `luac.c`, the `lua.hpp` header for C builds, the `Makefile` and `doc/`.
- **Licence:** MIT. Copyright (C) 1994-2026 Lua.org, PUC-Rio.
  - `LICENSE` is the notice at the end of `src/lua.h`, word for word, without the comment markers.
  - Keep `LICENSE` with any distribution. `cmake --install` copies it to `share/lykoware/licenses/lua`.
- **Local patches:** none. Two build settings are made in Lykoware's own file, not in the sources (below).
- **Transitive dependencies:** none. Lua uses only the C standard library.
- **Built:** one Lykoware translation unit, `engine/src/script/lua_impl.cpp`, as the static target `lykoware_lua` (`cmake/VendorLua.cmake`), without Lykoware's warning flags or clang-tidy. It includes the sources as upstream's `onelua.c` does, and:
  - **compiles them as C++,** so a Lua error unwinds C++ frames as an exception;
  - **defines `luai_makeseed`** as 0, a fixed string hash seed, so iteration over string keys repeats run to run;
  - **compiles only:**
    - the core;
    - the auxiliary library;
    - the base, coroutine, string, table, math and utf8 libraries.

    `liolib.c`, `loslib.c`, `loadlib.c`, `ldblib.c` and `linit.c` are vendored for completeness but not compiled.
- **Wrapper:** `lykoware/script.hpp` (`lykoware::script::Script`). No Lua type appears in a public header. The wrapper builds the sandbox, the memory and instruction limits, and the value conversion.
- **Updates:**
  1. Download the new release.
  2. Check its SHA-256 against lua.org.
  3. Replace `src/`.
  4. Check `LICENSE` against `lua.h`.
  5. Run `script.sandbox`, plus a fuzz run of `Script::run` under ASan/UBSan, as ADR 0041 records.
