// The single compiled copy of Wuffs (third_party/wuffs, ADR 0036). Built as the static target
// lykoware_wuffs without Lykoware's warning flags, because the code below the include is
// upstream code. The modules compiled are listed in cmake/VendorWuffs.cmake.

#define WUFFS_IMPLEMENTATION
#include <wuffs-v0.4.c>
