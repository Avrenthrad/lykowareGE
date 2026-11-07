"""Validate installed artifacts, including verbatim upstream attribution (ADR 0045)."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile

cmake, build_dir, source_dir, configuration, has_wuffs = sys.argv[1:]
build = Path(build_dir).resolve()
source = Path(source_dir).resolve()


def run(*arguments, expected=0, cwd=None):
    result = subprocess.run(
        list(map(str, arguments)), cwd=cwd, capture_output=True, text=True,
        encoding="utf-8", errors="replace", timeout=60, check=False,
    )
    assert result.returncode == expected, (
        f"{arguments}: expected {expected}, got {result.returncode}\n"
        f"{result.stdout}\n{result.stderr}"
    )
    return result.stdout + result.stderr


# Independent expected package mapping; do not read the production install manifest.
# Keys are installed paths, values are unchanged files in the source checkout.
files = {
    "README.md": "third_party/RUNTIME_NOTICES.md",
    "lykoware/LICENSE": "LICENSE",
    "sdl3/LICENSE.txt": "third_party/sdl3/LICENSE.txt",
    "sdl3/hidapi/LICENSE-bsd.txt": "third_party/sdl3/src/hidapi/LICENSE-bsd.txt",
    "sdl3/hidapi/LICENSE-orig.txt": "third_party/sdl3/src/hidapi/LICENSE-orig.txt",
    "sdl3/hidapi/AUTHORS.txt": "third_party/sdl3/src/hidapi/AUTHORS.txt",
    "sdl3/yuv2rgb/LICENSE": "third_party/sdl3/src/video/yuv2rgb/LICENSE",
    "sdl3/khronos/Apache-2.0.txt": "third_party/vulkan-headers/LICENSES/Apache-2.0.txt",
    "jolt/LICENSE": "third_party/jolt/LICENSE",
    "volk/LICENSE.md": "third_party/volk/LICENSE.md",
    "vma/LICENSE.txt": "third_party/vma/LICENSE.txt",
    "vulkan-headers/LICENSE.md": "third_party/vulkan-headers/LICENSE.md",
    "vulkan-headers/LICENSES/MIT.txt": "third_party/vulkan-headers/LICENSES/MIT.txt",
    "json/LICENSE.MIT": "third_party/json/LICENSE.MIT",
}
for dependency in ("sdl3", "jolt", "volk", "vma", "vulkan-headers", "json"):
    files[f"{dependency}/PROVENANCE.md"] = f"third_party/{dependency}/PROVENANCE.md"
if has_wuffs == "1":
    files["wuffs/LICENSE-MIT"] = "third_party/wuffs/LICENSE-MIT"
    files["wuffs/PROVENANCE.md"] = "third_party/wuffs/PROVENANCE.md"

if (source / "third_party/lua/LICENSE").exists():
    files["lua/LICENSE"] = "third_party/lua/LICENSE"
    files["lua/PROVENANCE.md"] = "third_party/lua/PROVENANCE.md"


def excerpt(relative, start, end):
    text = (source / relative).read_bytes().decode("utf-8").replace("\r\n", "\n")
    assert start in text, f"missing reference start marker: {relative}"
    return start + text.split(start, 1)[1].split(end, 1)[0]


def check_notices(prefix):
    root = prefix / "share/lykoware/licenses"
    for installed, original in files.items():
        assert (root / installed).read_bytes() == (source / original).read_bytes(), installed
    expected = {
        "stb_image-MIT.txt": excerpt(
            "third_party/sdl3/src/video/stb_image.h", "ALTERNATIVE A - MIT License", "ALTERNATIVE B - Public Domain"),
        "miniz.txt": excerpt(
            "third_party/sdl3/src/video/miniz.h",
            "  This is free and unencumbered software released into the public domain.", "*/"),
        "libm.txt": excerpt(
            "third_party/sdl3/src/libm/e_sqrt.c", " * Copyright (C) 1993 by Sun Microsystems", " */"),
    }
    for name, text in expected.items():
        assert (root / "sdl3/embedded" / name).read_bytes().replace(b"\r\n", b"\n") == text.encode("utf-8"), name
    khronos = (root / "sdl3/khronos/khronos.txt").read_text(encoding="utf-8")
    for header in sorted((source / "third_party/sdl3/src/video/khronos").rglob("*.h")):
        relative = header.relative_to(source).as_posix()
        block = excerpt(relative, "Copyright", "*/")
        assert f"Source: {relative}\n{block}\n\n" in khronos, relative
    assert not list(root.rglob("LICENSE-gpl*")), "unselected HIDAPI GPL alternative was installed"
    assert "ALTERNATIVE B" not in (root / "sdl3/embedded/stb_image-MIT.txt").read_text(encoding="utf-8")
    for excluded in ("slang", "spirv-tools", "spirv-headers"):
        assert not (root / excluded).exists(), f"build-only tool presented as a runtime dependency: {excluded}"


with tempfile.TemporaryDirectory(prefix="package notices Ω ", dir=build) as temporary:
    work = Path(temporary)
    arguments = [cmake, "--install", build]
    if configuration:
        arguments += ["--config", configuration]
    full = work / "full install"
    run(*arguments, "--prefix", full)
    check_notices(full)
    suffix = ".exe" if os.name == "nt" else ""
    for program in ("lyko", "lykoware_sandbox"):
        assert (full / "bin" / (program + suffix)).is_file(), program
    for shader in ("cli_lit.vert.spv", "cli_lit.frag.spv", "sandbox_lit.vert.spv", "sandbox_lit.frag.spv", "sandbox_skin.vert.spv"):
        assert (full / "bin/shaders" / shader).stat().st_size > 0, shader

    notices_only = work / "notices only"
    run(*arguments, "--prefix", notices_only, "--component", "RuntimeNotices")
    check_notices(notices_only)
    assert not (notices_only / "bin").exists(), "notice-only install unexpectedly copied executables"
    full_notices = full / "share/lykoware/licenses"
    component_notices = notices_only / "share/lykoware/licenses"
    for artifact in component_notices.rglob("*"):
        if artifact.is_file():
            assert artifact.read_bytes() == (full_notices / artifact.relative_to(component_notices)).read_bytes()

    relocated = work / "relocated install"
    shutil.move(full, relocated)
    check_notices(relocated)
    assert "lyko " in run(relocated / "bin" / ("lyko" + suffix), "--version", cwd=work)

    # Exercise the real extractor's refusal paths without modifying vendored sources.
    for name, start, end in (("start", "absent", "end"), ("end", "start", "absent")):
        input_file = work / "changed notice.txt"
        input_file.write_text("start\noriginal notice\nend\n", encoding="utf-8")
        script = work / f"invalid-{name}.cmake"
        script.write_text(
            f'include([[{(source / "cmake/RuntimeNotices.cmake").as_posix()}]])\n'
            f'_lykoware_notice_excerpt([[{input_file.as_posix()}]] "{start}" "{end}" '
            f'[[{(work / "should-not-exist.txt").as_posix()}]])\n', encoding="utf-8")
        assert "marker changed" in run(cmake, "-P", script, expected=1)
        assert not (work / "should-not-exist.txt").exists()

    input_file = work / "CRLF notice.txt"
    input_file.write_bytes(b"ignored\r\nstart\r\nCopyright test holder\r\nend\r\n")
    output_file = work / "CRLF excerpt.txt"
    script = work / "preserve-newlines.cmake"
    script.write_text(
        f'include([[{(source / "cmake/RuntimeNotices.cmake").as_posix()}]])\n'
        f'_lykoware_notice_excerpt([[{input_file.as_posix()}]] "start" "end" '
        f'[[{output_file.as_posix()}]])\n', encoding="utf-8")
    run(cmake, "-P", script)
    assert output_file.read_bytes().replace(b"\r\n", b"\n") == b"start\nCopyright test holder\n"

print("runtime package notices passed: full/component install, source fidelity, relocation and refusal checks")
