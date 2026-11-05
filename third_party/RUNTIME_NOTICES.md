# Lykoware runtime notices

Keep this directory with the installed executables. Lykoleon owns its original
code; upstream authors retain their copyrights. Files below preserve upstream
notices, not a transfer of ownership or a public-release approval.

Paths are relative to this installed `share/lykoware/licenses` directory:

| Package | Installed attribution | Terms used |
| --- | --- | --- |
| Lykoware | `lykoware/LICENSE` | Studio proprietary notice, including the Jolt replacement functions |
| SDL 3.4.18 | `sdl3/LICENSE.txt`, `sdl3/PROVENANCE.md` | zlib |
| HIDAPI within SDL | `sdl3/hidapi/LICENSE-bsd.txt`, `LICENSE-orig.txt`, `AUTHORS.txt` | Recorded BSD option; original notice preserved; GPL alternative not selected |
| yuv2rgb within SDL | `sdl3/yuv2rgb/LICENSE` | BSD-3-Clause |
| stb_image within SDL | `sdl3/embedded/stb_image-MIT.txt` | MIT alternative, text extracted from the vendored header |
| miniz within SDL | `sdl3/embedded/miniz.txt` | Existing upstream public-domain/Unlicense statement, notice text preserved |
| libm within SDL | `sdl3/embedded/libm.txt` | Sun Microsystems permission and preservation notice |
| Khronos headers within SDL | `sdl3/khronos/khronos.txt`, `Apache-2.0.txt` | Header-specific notices (Apache-2.0 or MIT); independent of the row below |
| Vulkan-Headers | `vulkan-headers/LICENSE.md`, `LICENSES/MIT.txt`, `PROVENANCE.md` | Recorded MIT option |
| volk | `volk/LICENSE.md`, `volk/PROVENANCE.md` | MIT |
| Vulkan Memory Allocator 3.4.0 | `vma/LICENSE.txt`, `vma/PROVENANCE.md` | MIT |
| Jolt Physics 5.6.0 | `jolt/LICENSE`, `jolt/PROVENANCE.md` | MIT upstream; local replacement functions remain Lykoware code |
| nlohmann/json 3.11.3 | `json/LICENSE.MIT`, `json/PROVENANCE.md` | MIT |
| Wuffs (when built) | `wuffs/LICENSE-MIT`, `wuffs/PROVENANCE.md` | Recorded MIT option; its version is in its provenance |

SDL subcomponent notices are retained conservatively across platforms, including
code a particular build may not use. Generated excerpts stay in the build/install
trees; their source files remain unchanged. Generated excerpts normalize CRLF line endings to LF; copied licence/provenance files retain their original bytes. This index documents the attribution
set being installed. It does not approve additional licence exceptions or assert
that every future dependency/platform has undergone a complete legal audit.

Slang and SPIRV-Tools are build-time executables, not shipped in this runtime
package. System Vulkan drivers, operating-system libraries and local toolchains
are supplied separately. A public release still requires the studio's review.

A notices-only install is available without copying binaries:

```sh
cmake --install BUILD_DIRECTORY --prefix PREFIX --component RuntimeNotices
```

Configuration is required first; add `--config Release` for a multi-config build.
The normal full install includes these notices too. When adding/updating a runtime
dependency, update its provenance, installation rules and installed-tree tests.
