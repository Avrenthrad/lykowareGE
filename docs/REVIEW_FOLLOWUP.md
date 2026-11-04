# Core review follow-ups

Owner: Codex. Runtime fixes: PR #35, `codex/core-contract-hardening`. Scene/replay integration: PR #36, `codex/scene-command-replay`, built on #35 and Claude's #30/#32–#34. Joshua merges. Checkboxes reflect completed verification, not just authored code.

- [ ] World isolation: every ECS/body/character handle lookup/mutation rejects foreign owners; moved-from ECS reuse gets a new token; runtime tokens excluded from physics hashes.
- [ ] Link ownership: duplicate/locked links fail; cleanup hooks cannot be replaced; body/character ownership excludes the other; linked worlds cannot move; helper destruction retains cleanup.
- [ ] Failure safety: staged nonthrowing-movable ECS storage, allocation-free destroy, attachment rollback, failed deferred creation rollback and remaining-command preservation; injected failures pass Debug/Release/sanitizers.
- [ ] Persistent identity and replay: explicit versioned component/command names and stable authored IDs; canonical round-trip/invalid-input tests; all 180 tick hashes match at 30/60/144 Hz and irregular frames.
- [ ] Presentation separation: const ECS renderer, read-only interpolated snapshots, camera-relative double subtraction before GPU float values; GPU draw leaves simulation unchanged and far-origin capture matches.
- [ ] Integrated fixture/tooling: text scene + commands, lit falling crates, headless CLI replay, real PPM capture, repeatable hashes/pixels, offscreen measurement report, installed shader deployment, Windows measurement runner.
- [ ] Actual target-PC verification: Windows 11 Home / i7-14700F / 16 GB context supplied; GPU/VRAM, driver, measured Release results and frame pacing remain unverified. Run `tools/validation/measure-scene.ps1`; offscreen fixture results alone do not close flagship/streaming budgets.

Current local/CI evidence and remaining failures: [STATUS](STATUS.md). This list does not imply completion of all Phase 1 or later roadmap modules.
