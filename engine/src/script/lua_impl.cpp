// Lykoware's build of the vendored Lua 5.4.9 (third_party/lua, ADR 0041): the core, the
// auxiliary library and the libraries scripts may use, in one translation unit, the way
// upstream's onelua.c builds Lua.
//
// - Compiled as C++, so a Lua error is a C++ exception and unwinds the C++ frames of host
//   functions instead of skipping their destructors (ldo.c, LUAI_THROW).
// - The string hash seed is fixed, so table iteration over string keys has the same order in
//   every run. Upstream mixes in the time and addresses (lstate.c, luai_makeseed).
// - The io, os, package and debug libraries, and linit.c, are not compiled: scripts get no files,
//   processes, clocks, module loading or introspection.

#define luai_makeseed(L) (0U)

// Every standard header the sources use comes first, as in onelua.c: Lua's internal macros
// (llex.c defines `next`, for one) must not reach a C++ library header included later.
#include "lprefix.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Core.
#include "lzio.c"
#include "lctype.c"
#include "lopcodes.c"
#include "lmem.c"
#include "lundump.c"
#include "ldump.c"
#include "lstate.c"
#include "lgc.c"
#include "llex.c"
#include "lcode.c"
#include "lparser.c"
#include "ldebug.c"
#include "lfunc.c"
#include "lobject.c"
#include "ltm.c"
#include "lstring.c"
#include "ltable.c"
#include "ldo.c"
#include "lvm.c"
#include "lapi.c"

// Auxiliary library.
#include "lauxlib.c"

// Standard libraries scripts may use.
#include "lbaselib.c"
#include "lcorolib.c"
#include "lmathlib.c"
#include "lstrlib.c"
#include "ltablib.c"
#include "lutf8lib.c"
