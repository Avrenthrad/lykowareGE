# Manual ownership probes

These sources preserve the exact negative probes used in the [health report](../../HEALTH_CHECK_2026-11-06.md). They require the pending audio/mesh/texture modules in integration commit `48b0da9d52bef2b183f92ecc911a5f0620214d21`; merged main does not provide those APIs. Copy the probes outside the checkout when using that frozen snapshot. They are not passing CTest tests.

For the audio probe, from the combined checkout on Linux:

```sh
c++ -std=c++20 -ffp-contract=off -Iengine/include /path/to/audio_owner.cpp engine/src/audio.cpp -o /tmp/lyko-audio-owner
/tmp/lyko-audio-owner
```

For the renderer probe, build `linux-debug` first, then compile `render_owner.cpp` as C++20 with `engine/include` and link against the same engine/dependency libraries as `lykoware_render_model_test`. The Ninja build exposes the complete link line with `ninja -C build/linux-debug -t commands lykoware_render_model_test`. Substitute the probe source for that test's object, select an external output path, and omit the test dependency-file linker flag. Run the resulting binary with the built `shaders/lit.vert.spv` and `shaders/lit.frag.spv` paths as its two arguments, using an available Vulkan driver. It performs an actual offscreen draw and readback.

Exit 1 means the reproduced bad behavior occurred; exit 2 means setup failed. Exit 0 only means this particular probe did not reproduce the defect. The renderer probe intentionally keeps IDs across renderer destruction; the mixer probe passes IDs between independent mixers. Full fixes should add normal regression coverage on the owning branches.
