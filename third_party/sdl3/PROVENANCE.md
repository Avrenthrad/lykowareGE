# SDL3 provenance

This directory is the vendored SDL 3.4.18 source used by the window and input wrapper. It is not a Lykoware-owned library. Upstream copyright stays with the SDL authors.

| Field | Value |
| --- | --- |
| Name | SDL |
| Upstream | https://github.com/libsdl-org/SDL |
| Release tag | `release-3.4.18` |
| Release commit | `829a65d769d935c4852f8159e964312c0957260a` (also recorded in upstream `.git-hash` and `REVISION.txt`) |
| Release archive | https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz |
| Archive sha256 | `9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3` |
| SPDX licence | Zlib |
| Licence file | [LICENSE.txt](LICENSE.txt) |
| Wrapper | `lykoware::Window` and `lykoware::InputState` in the engine library. Public engine headers do not include SDL. |
| Local source patches | None. Upstream files that are present are unmodified. |

## What was omitted

This tree is a plainly marked subset of that official archive. The CMake build does not use the omitted directories, and no file under `include/`, `src/`, `cmake/`, `build-scripts/`, or `wayland-protocols/` was edited.

Omitted:

- `test/`
- `examples/`
- `docs/`
- `VisualC/`
- `VisualC-GDK/`
- `Xcode/`
- `android-project/`

`PROVENANCE.md` is a Lykoware record. It is not an upstream file.

## Terms we actually use

SDL itself is used under the zlib licence in `LICENSE.txt`.

HIDAPI is shipped inside SDL under `src/hidapi/` and offers a choice of GPL-3.0, a BSD-style licence, or the original HIDAPI licence. Lykoware elects the BSD-style licence in `src/hidapi/LICENSE-bsd.txt`. The GPL text is not elected and is not the terms for this copy. The original HIDAPI notice remains in `src/hidapi/LICENSE-orig.txt`.

Other notices stay next to the files they cover, including Khronos headers under `src/video/khronos/`, `src/video/yuv2rgb/LICENSE`, and the stb/miniz headers in `src/`. Those permissive terms are unchanged.

## Build

The parent build compiles the static library only (`SDL_STATIC=ON`, `SDL_SHARED=OFF`). SDL's test library, tests, examples, and install rules are off. Audio, camera, GPU, render, haptic, and OpenGL are off. Video, the dummy and offscreen drivers, joystick/gamepad, and Vulkan surface support stay on. Vulkan is loaded dynamically by SDL when a caller asks for a surface. This repository does not fetch SDL, and it does not add a Vulkan dependency.

Replace this tree only from a pinned release archive, then update this record and the wrapper together.
