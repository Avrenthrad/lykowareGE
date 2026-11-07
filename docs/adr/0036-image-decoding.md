# ADR 0036 — PNG and JPEG decoding through vendored Wuffs

Status: Proposed.
Related task/PR: Textures, PR 1 of 3 (branch `claude/image-decode`), stacked on model loading ([ADR 0035](0035-gltf-meshes.md)). PR 2 adds GPU textures to the RHI and renderer; PR 3 reads glTF materials and texture coordinates.
Supersedes / superseded by: none.

## Context

The owner asked for textures on models. glTF 2.0 textures are PNG or JPEG files. Lykoware has no image decoder, and textures may later come from outside the engine (exporters, downloads, mods), so the decoder must be safe on hostile files.

On 2026-11-05 the owner chose to vendor Wuffs, over stb_image and over writing our own. They also chose to do textures as three stacked PRs.

## Options

- **Wuffs** (Google; MIT or Apache-2.0):
  - written in a language whose compiler proves memory safety (no buffer overflows, no integer overflows in indexing), then shipped as generated C;
  - fast;
  - one large file.
- **stb_image** (public domain or MIT):
  - tiny and very common;
  - it has had several memory-safety bugs on malformed files over the years.
- **Our own PNG** (with zlib decompression) **and JPEG decoders:**
  - in keeping with the WAV and glTF readers;
  - JPEG (baseline and progressive, Huffman, IDCT, chroma subsampling) is a large and error-prone format to write and fuzz.

The owner chose Wuffs.

## Decision

Proposed:

- **The vendored version:** Wuffs tag `v0.4.0-alpha.12` (commit `7f70dda`), the single file `wuffs-v0.4.c`, unmodified, under its MIT option ([PROVENANCE](../../third_party/wuffs/PROVENANCE.md)).
  - **Why an alpha:** 0.3 is upstream's latest stable line, but it has no JPEG decoder.
  - **Pinned:** the exact file is pinned (SHA-256 recorded), so upstream API changes reach Lykoware only through a deliberate update.
- **Build:**
  - one translation unit (`engine/src/image/wuffs_impl.cpp`) builds the static target `lykoware_wuffs`, without Lykoware's warning flags or clang-tidy;
  - only the modules Lykoware uses are compiled: PNG with zlib, Deflate, Adler-32 and CRC-32; JPEG; and the C++ image helpers. GIF, BMP, WebP and the rest are preprocessed out.
- **`lykoware/image_decode.hpp`:** `decode_image(bytes, error)` returns an `RgbaImage`.
  - **Formats:** only PNG and JPEG are accepted, told apart by their first bytes.
  - **Output:** 8-bit RGBA, not premultiplied, rows from top to bottom, tightly packed.
    - Greyscale, palette (with `tRNS` transparency), 16-bit and alpha images all become 8-bit RGBA. 16-bit channels keep their high byte.
    - An image without alpha is opaque. An animated PNG gives its first frame.
  - **No colour conversion:** colour values are as stored, with no gamma or ICC profile applied. glTF base colour textures are sRGB, and the GPU texture in PR 2 will be an sRGB format.
  - **Checksums:** PNG chunk CRC-32 and zlib Adler-32 checksums are checked. Wuffs' helper skips them by default for speed, but a damaged asset should fail clearly instead of loading wrong pixels.
  - **Size limits:** at most 16,384 pixels on a side and 67,108,864 pixels (8192 × 8192, 256 MiB as RGBA). Both are refused from the header, before pixel memory is allocated.
  - **Partial decodes are refused:** a truncated file is an error, not a half image.
- **Licence notice:** Wuffs' MIT notice is installed with Lykoware (`share/lykoware/licenses/wuffs`), like nlohmann/json's.

## Contracts and compatibility

- New public header `lykoware/image_decode.hpp`. New files: `engine/src/image/image_decode.cpp`, `engine/src/image/wuffs_impl.cpp`, `cmake/VendorWuffs.cmake` and `third_party/wuffs/`. No Wuffs type appears in a public header.
- **Threading:** `decode_image` shares no state and can run on any thread, for example asset loading threads.
- `RgbaImage` is separate from `lykoware::Image` (the RGB8 test harness image, ADR 0007), whose contract stays as it is.
- **Not yet:**
  - other formats (KTX2/Basis, WebP and DDS come with texture compression, if needed);
  - colour-profile handling;
  - mipmap generation (PR 2, on the GPU);
  - streaming large images.

## Consequences

- PNG and JPEG textures can be read safely enough to accept files Lykoware did not make.
- Corrupted JPEG entropy data usually still decodes to wrong-looking pixels without an error, as in every JPEG decoder. Structure errors and truncation are refused.
- The vendored file is 3.7 MB of generated C. Only the modules used are compiled, which takes about 15 s once per build directory.
- **Updating means re-pinning a whole file:** a new hash, the decoding tests and a fuzz run.

## Verification

- **New test `assets.image`.** Every image is encoded by the test itself: a minimal PNG writer using uncompressed zlib blocks, and a minimal baseline JPEG writer using flat 8×8 blocks with quantisation 1, which decode back exactly. The expected pixels are therefore known, and no image files are committed.
  - **PNG:**
    - 2×2 RGBA exact, top-to-bottom rows, alpha not premultiplied;
    - RGB becomes opaque;
    - greyscale, and greyscale with alpha;
    - palette with transparency;
    - 16-bit keeps the high byte;
    - the Sub and Up row filters are undone.
  - **JPEG:**
    - greyscale blocks decode to their exact levels;
    - a 5×3 image is cropped from its block;
    - neutral YCbCr gives exact grey;
    - a coloured block converts within ±2 of the standard YCbCr formula.
  - **Refusals:**
    - empty input, GIF and BMP;
    - a truncated PNG, a bad CRC and a bad Adler-32;
    - zero width and an invalid bit depth;
    - more than 16,384 pixels wide;
    - more than 2^26 pixels;
    - a truncated JPEG.
- **Deliberate breaks:** 7 of 8 caught:
  - BGRA output;
  - premultiplied alpha;
  - checksums ignored;
  - half-decoded images accepted;
  - the pixel limit unchecked (Wuffs then allocated 268 MB for a tiny file before failing, which shows why the early check matters);
  - the dimension limit not passed;
  - rows copied short.

  Not caught, as expected: removing the zero-size guard, because Wuffs refuses zero-size images itself.
- **Wuffs' default is not checksum-checked:** the CRC and Adler-32 tests first failed, which is how the skipped checks were found and turned on.
- **Cross-check with PIL** (local; PIL is not a build dependency): real files made by PIL decode identically to PIL's own decoder (libjpeg and libpng), with a largest difference of 0:
  - compressed RGBA, palette and greyscale PNGs;
  - 4:2:0 baseline, progressive, 4:4:4 and greyscale JPEGs.
- **Fuzzing** (local, not committed): 200,000 mutated PNG and JPEG files were run under ASan/UBSan with no crash. 20,947 still decoded, mostly JPEGs with damaged entropy data.
- **Local results:** 48/48 pass in the Debug, ASan/UBSan and clang-tidy presets. A GCC `-Wshadow -Wconversion` sweep of the new Lykoware files is clean. `cmake --install` installs the Wuffs notice.
- **CI:** blocked for the whole account since 2026-11-05 (see ADR 0033); to be recorded. MSVC builds Wuffs' C as C++; upstream supports MSVC, which is unverified here until CI runs.
