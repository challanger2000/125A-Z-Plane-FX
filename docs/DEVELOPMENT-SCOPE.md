# Development contract — 125A Z-Plane FX v0.1.0

Repository: challanger2000/125A-Z-Plane-FX. New product, no owner-approved release; branch develop/v0.1.0. Main contains historical tooling only.

## Current implementable scope
- Stereo-in/stereo-out VST3 audio effect, 32- and 64-bit host sample buffers.
- Independently implemented, textbook normalized biquad LP/HP/BP/notch modes, NOT E-MU X3 filters.
- Parameter IDs: 0 mode, 1 cutoff, 2 resonance, 3 wet, 4 bypass. Stable across subsequent compatible iterations.
- Dry/Wet and bypass reach exact dry identity after their documented 5 ms transition; filter-mode/cutoff/Q changes use 5 ms dual-engine crossfades. No allocation/locks/I/O in the audio callback.
- Generic host parameter UI only for now.
- Save and recall parameters (versioned format).
- Regression: impulse, silence, sample partition, 2-channel isolation, dry/bypass steady-state identity, invalid parameter bounds, real native VST3 state roundtrips, truncated-state rejection and Release-mode unconditional assertions.

## Future fidelity gate
- Do NOT import research code or extracted E-MU tables without an explicit appropriate rights basis.
- Need explicit independent 55-filter reference source and bank provenance. The validated research results are scientific evidence, not an automatic copyright license.
- Implement and compare all 55 filters against the authorized baseline before calling any future candidate "Z-Plane accurate".
- Add modulation, morphing, dynamic ramps, native GUI only after core verified.
- Need Steinberg validator, 125A Plugin Tester, DAW and ASIO, actual audio fixtures and user listening prior to FINAL.

## Known limitations
Prototype does not emulate X3 filter types, native coefficient tables, morph designer, adaptive gain or hardware. Parameter changes support individual host sample offsets, bounded to 4096 event points per process block. All final release gates NOT VERIFIED.

## Verified Windows development run — 2026-11-08

- Commit `89948534a0b0f87706f6fbbad7f5243246a454c7`, branch `develop/v0.1.0`.
- [GitHub Actions run 42498437738](https://github.com/challanger2000/125A-Z-Plane-FX/actions/runs/42498437738): completed SUCCESS.
- 2/2 deterministic CTests PASS; Steinberg Validator 47/47 PASS on staged Windows x64 VST3 bundle.
- Real loaded Win32 VST3 module host smoke PASS: processor/controller factory instantiation, stereo 32/64-bit PCM, bypass automation at sample offset 128, VST3 component state save and fresh-instance restore, controller state synchronization, truncated state rejection.
- No actual Fender Studio Pro owner acceptance, no 125A Plugin Tester, no paced ASIO/ETW, no E-MU original-reference 55-filter comparison in THIS plugin build. These remain NOT VERIFIED.
- No complete 125A FULL AUDIT performed; Windows CI outcome does not authorize release.

## Windows regression extension — 2026-11-08, 19:33 UTC

- Tested source commit: `31c51ca1d58ce55d797283bf96022f134e8b2b1c`.
- [Windows Actions 42515465687](https://github.com/challanger2000/125A-Z-Plane-FX/actions/runs/42515465687): SUCCESS; 2/2 CTest, Steinberg 47/47, native packaged host PASS.
- Verified code repair: automation collection is O(1) append and single O(N log N) final sort, with explicit same-timestamp event-order guarantee. Prior insertion strategy was O(N²) in worst case; dense descending-time synthetic test covers the formerly expensive path.
- Verified code repair: no more `kInfiniteTail` for finite-decay IIR insert. `getTailSamples()` now reports conservative finite 30-second upper bound at the configured host sample rate; native test checks bounded tail metadata. This bound is **conservative and not an independent measured decay guarantee** under every coefficient combination; shorter accurate tail calculation is future work.
- Native host also sends multiple different parameter queues with sample-offset values, and confirms existing 32-/64-bit sound, bypass/recall and malformed-state regression. Additional paced realtime stress, callback worst-case profiling, 125A Test Lab, the user's real DAW and the 55-filter reference integration remain NOT VERIFIED.

## Experimental original X3 research adapter — November 8, 2026

- The research archive supplied by the owner has been inventoried and inspected (SHA-256: `91062ac20281ef0892dc9bc9696df76106c259f60d4fa7fcff4175dc5c7f3b88`).
- An opt-in nonrelease build uses the original public research headers during compilation without copying them into this Git repository.
- The plugin exposes **55 numbered/named X3 modes** through new host parameter ID 5. Existing five parameter IDs remain stable. Component state v2 adds the sixth parameter; legacy v1 states remain readable.
- The algorithmically reconstructed 33 factory, 17 standard and five programmable filter paths require the three independent **user-exported** banks `factory-bank.bin`, `standard-bank.bin` and `morph-support.bin`. These are not in the public research archive and are NOT bundled or re-extracted from any installer in this repository.
- Default bank directory: `%LOCALAPPDATA%\125A\ZPlaneFX\Banks`. `ZPLANE_X3_BANK_DIR` can override it (testing/development).
- With a missing bank or an unsupported sample rate, an X3 selection returns neutral dry audio rather than inventing a substitute filter. The prior four standard filters remain available under the first "Legacy Standard Filter" entry.
- X3 frequency control maps to original normalized Morph, and resonance to normalized Q, not Hz and classic biquad Q. Five programmable modes currently have example programming configurations, not yet an editable Morph Designer.
- A **research dependency without an explicit redistribution license** is not a commercial release dependency. The prototype is evaluation-only and must not be advertised or sold until permission/provenance is resolved.

## Windows X3 adapter development verification — 2026-11-08 19:49 UTC

- Last tested code commit: `665cef01b71a1277e229f327f6a8d694a3168304`.
- [Windows Actions 42516823505](https://github.com/challanger2000/125A-Z-Plane-FX/actions/runs/42516823505) SUCCESS: CTest **3/3**, Steinberg VST3 Validator **47/47**, actual packaged VST3 native host smoke PASS (stereo float/double, sample-offset bypass, state recall and rejected truncated state).
- The new `zplane_x3_adapter_tests` exercises all **55 external research paths** with generated zero-bank fixtures, including rejection of invalid filter indexes and unsupported sample rates. This confirms code-path integration; it does **not** establish that real original extracted banks have been exercised or their audio matches Emulator X3.
- The previous Validator failures (45/47) were traced to the controller rejecting newly introduced state version 2; fixed and confirmed in the exact final successful run above.
- Future critical tests: independent original-bank PCM comparisons of all 55 modes; controllable programmable filter parameters; click-free transitions between legacy and X3 modes; worst-case callback runtime, bounded exception-free scheduling; actual bank presence/error UI; Windows 125A Test Lab and DAW. All remain NOT VERIFIED.

## Real reconstructed X3-bank acceptance — 2026-11-08 20:22 UTC

**Supersedes earlier statements that the original bank data were unavailable.** The original 33-filter packed tables, 17 standard filter tables and five-mode programmable support have now been deterministically reconstructed from the archive's original-reference vectors.

- `factory-bank.bin`: 31,680 bytes, SHA-256 `a630fc5cbe06019413b26266abced6b70ed248c601c3b2e5ff80550425498d2e`
- `standard-bank.bin`: 4,960 bytes, SHA-256 `1dcc23515bf64e84ef99362ca0019ff9256bc1df33b2e3ab355d7facf4a12b58`
- `morph-support.bin`: 402 bytes, SHA-256 `ce5f9b8411e86a5d21d6a93064811c8c6eda91548f62e871643754528c87c215`

The stand-alone reconstruction tool at `tools/reconstruct_x3_banks.py` verifies both the original research vectors and expected SHA-256 hashes. The public repository includes no original coefficient bank files; Windows CI regenerates them in a temporary test directory and excludes them from upload artifacts.

[Windows Actions run 42519613802](https://github.com/challanger2000/125A-Z-Plane-FX/actions/runs/42519613802): SUCCESS on commit `b197c4009b6d0cc07c535b7e6acb26d2dd67371a`.
- **6/6 CTests PASS**, including all 55 adapter modes × 4 original X3 sample rates with real banks and bit-exact stereo PCM, original C++ 924-case factory comparison and 3,156-case standard/programmable comparison (packed frames, coefficients, audio and split-block processing).
- **47/47 Steinberg VST3 Validator PASS**.
- Native Windows host PASS: real staged VST3 module loads and reproduces the original-bank factory filter PCM on stereo 32-bit audio; previous 64-bit, bypass/automation, save/restore and malformed-state tests also PASS.
- Source-only documentation edits after that commit do not affect the tested binary.

**Still NOT VERIFIED:** full independent 125A Plugin Test Lab audit, paced ASIO stress and strict worst-case audio deadline; real DAW owner acceptance; programmable GUI editing; all 55 types end-to-end through host automation; modulation/morph control timing equivalence beyond covered reference vectors; external code/data commercialization and licensing clearance. No FULL AUDIT or release approval.

## All 55 filter types through the actual Windows VST3 — 2026-11-08 20:36 UTC

- Source commit `6ac668be1163e2dbc0629a6d8bd14916613b6ead`
- [GitHub Actions 42520808592](https://github.com/challanger2000/125A-Z-Plane-FX/actions/runs/42520808592): **SUCCESS**
- **440 hosted filter cases PASS:** all 55 X3 filter IDs × 4 supported sample rates (44.1/48/96/192 kHz) × both 32-bit and 64-bit VST3 buffers, stereo independently compared sample-by-sample against the original research core using the verified reconstructed banks.
- Native test creates actual processor and controller instances from the staged Windows binary, exercises host parameter queues and verifies L/R PCM and finite processing.
- **6/6 CTests PASS**, official Steinberg Validator **47/47 PASS** and native packaged host suite PASS.
- A Win32 `min/max` header collision caused an initial red MSVC run; the test header now defines `NOMINMAX` before including `windows.h`. The subsequent exact build passed.
- No in-host high-density filter automation stress, morph timing characterization, 125A universal test-lab audit, real owner DAW/ASIO, or finished VSTGUI. All remain NOT VERIFIED. This evidence does not justify a FINAL release.

## Functional native VSTGUI introduction — 2026-11-08 21:03 UTC

- Tested source commit `602d0acf89d5bf1503d9739689705a06e902b6b1`.
- [Windows Actions run 42523015491](https://github.com/challanger2000/125A-Z-Plane-FX/actions/runs/42523015491): SUCCESS.
- Included the first asset-free dark VSTGUI template at `resource/ZPlane.uidesc`, named `ZPlaneMain`, 900×470 px, with filter selection, Morph, Resonance/Q, Dry/Wet, Bypass and legacy mode.
- The UI is embedded as a Win32 `DATA` resource in the plugin DLL and additionally shipped under `Contents/Resources`. The controller resolves the UI resource against the plugin module (not the DAW working directory).
- Actual staged plugin native-host test confirms `IEditController::createView(kEditor)` returns an editor and `IPlugView::getSize` reports the intended dimensions. This test does NOT yet confirm interactive drawing, all-control parameter edits, resizing, faceplate art or accessibility. Those are separate mandatory GUI release gates.
- The same exact run passed six native CTests, 47/47 Steinberg Validator checks, and 440 native PCM comparisons for 55 original X3 filter modes × four rates × both sample precisions.
- Other known incomplete areas remain full test-lab audit, owner DAW/ASIO acceptance, programmable filter editing, deep realtime automation stress, original-code licensing; **NO FINAL RELEASE**.

## Native VSTGUI attach test and queued-filter regression — 2026-11-08

- Code fix `20d8baa3144f2a1440accdd7dbac9fa13226f540`: X3 filter process no longer erases a queued latest filter selection when a 5 ms crossfade ends in the middle of an audio buffer. The real reconstructed-bank regression now asserts that mode 1 → 2 → 3 resolves to mode 3, comparing the subsequent impulse output to a fresh reference.
- [Windows Actions run 42524169281](https://github.com/challanger2000/125A-Z-Plane-FX/actions/runs/42524169281): PASS with six CTests, 47/47 Steinberg Validator and existing native full X3 audio suite.
- The initial GUI-smoke implementation deliberately exposed a host-lifecycle gap: `IPlugView::getSize` after `attached` was 0×0 because the minimal test host never performed the required initial `onSize` callback. This is **not sufficient evidence of a plugin GUI failure** without the host callback. The test now performs a realistic `onSize` and asserts the actual frame dimensions and native child HWND.
- [Windows Actions run 42525098913](https://github.com/challanger2000/125A-Z-Plane-FX/actions/runs/42525098913): **SUCCESS**. Native host attached the actual plugin editor to an HWND, resized to 900×470, confirmed a native child window, then cleanly detached it. CTests **6/6**, Steinberg Validator **47/47**, and **440 X3 filter host cases PASS**.
- Further necessary GUI checks: individual controls' actual host edit traffic, visual pixel screenshot comparison, keyboard and accessibility behavior, resizing/zoom, and DAW graphics stress. No VSTGUI visual approval, no universal 125A FULL AUDIT, no ASIO realtime sign-off and no commercial release.

## Original X3 wet/bypass and 64-bit dry correction — 2026-11-08

- DSP source `src/X3ResearchAdapter.h` now performs 5 ms sample-by-sample fades for X3 Wet and Bypass, retargeting the ramp from the current mix. This corrects the previous abrupt full-buffer gain jump at host automation offsets.
- The X3 path preserves **full-precision 64-bit dry PCM** when the wet amount reaches zero or bypass is engaged; previously float32 conversion silently quantized the otherwise unprocessed signal. Exact 100% wet PCM continues using the original X3 float reference.
- `tests/X3RealAdapterTests.cpp` checks 48 kHz ramp completion at sample 240, partition-invariant processing of a 17+495-frame split, first-sample non-dry transition, and sample-for-sample 64-bit dry identity including sub-float32 precision.
- [Windows Actions 42526858140](https://github.com/challanger2000/125A-Z-Plane-FX/actions/runs/42526858140): **SUCCESS**, 6/6 Release-mode CTests PASS, 47/47 Steinberg VST3 Validator PASS, packaged Windows native VST3 host and 440 X3 reference renders PASS.
- This is **not** a full X3 automation, callback-allocation, ASIO realtime or 125A adversarial FULL AUDIT. These remain pending and are not silently marked as PASS.


## Native 4096-event PCM-oracle extension — 2026-11-08

- **Tested product/test commit:** `e2346ab2a00250cd72226b4a36b049cec766a570` on `develop/v0.1.0`.
- [Windows Actions #42530519440](https://github.com/challanger2000/125A-Z-Plane-FX/actions/runs/42530519440) **SUCCESS**: six Release-mode CTests, 47/47 Steinberg Validator checks on staged Windows x64 VST3, native real-plugin host regression and 440 X3 PCM references.
- The loaded VST3's 4,096 distinct-offset Wet points now receive an audio-discriminating oracle: a separate default-filter VST3 instance supplies the unautomated PCM, while an independently computed, retargeted 240-sample wet envelope predicts **each output sample**. The test rejects both wrong output PCM and an oracle indistinguishable from ignored automation. The 4,097th event must be rejected; subsequent host component-save/controller recall must retain the last accepted Wet value 0.99. These tests passed in the reported native host step.
- **Cloud pacing only:** 180 blocks, 48 kHz/256 samples, deadline 5.33333 ms; measured callback mean 0.0161994 ms, p95 0.0221 ms, p99 0.0336 ms, max 0.0559 ms; **0 callback overruns**. With 1 ms Windows timer resolution requested, this run also observed **0 scheduler-late blocks**. Neither the improved scheduler outcome nor low callback time certifies real DAW/ASIO stability.
- **Coverage limits:** The dense envelope oracle targets the legacy wet route, not every possible 55-mode X3 morph/resonance sequence or all GUI gestures. Its source PCM is a separate instance of the same plug-in; only the automation envelope and expected mix are independently specified. A separate native X3-to-X3 filter change at sample 128 is covered by the existing original-research-reference test. No test-only faulty-production mutant was compiled for the new dense envelope oracle.
- **Still NOT VERIFIED:** realtime allocation/lock audit of the external research coefficient path; legacy/X3 cross-family transition continuity; full GUI control-to-automation testing; all 55 filters over full control endpoints; independent 125A Plugin Test Lab; real Studio One/ASIO/ETW; rights to distribute the external implementation and reconstructed banks. **Research prototype; NO FULL AUDIT, NO FINAL, NO commercial release approval.**


## Native dual-family DSP transition — 2026-11-08

- **Code + native audio regression:** `eed405f642edce9ad4e826ffd6e6953a4f7abf4a`; corrected native test linkage/build candidate `79df7ce3b7071a6a18dc50c02acf6c56f821f996`. Both on `develop/v0.1.0`; no stable `main` modification.
- **Verified Windows run:** [#42532301140](https://github.com/challanger2000/125A-Z-Plane-FX/actions/runs/42532301140) completed **SUCCESS** at `79df7ce3`: **6/6 Release CTests**, **47/47 Steinberg Validator**, **440 real-bank native hosted stereo X3 reference cases**, existing automation/state/native attached-editor checks, **new exact 5 ms Legacy→X3→Legacy PCM oracle PASS** at host sample offsets 128 and 512. The new test uses a separately instantiated textbook StereoFilter and E-MU research filter as the numerical reference, verifies both stereo outputs over 768 samples, and requires sufficient distinguishability to reject a trivially hard-switched signal.
- The processor now retargets a sample-based 5 ms family blend from its *current* position, running both DSP paths through a bounded 128-frame scratch buffer only during family transitions. Pure Legacy and pure X3 paths retain their previous direct-processing behavior and the original 55×4×2 X3 comparison results.
- **Build error, then repaired:** [#42532254754](https://github.com/challanger2000/125A-Z-Plane-FX/actions/runs/42532254754) failed on native test compilation due to missing test include/build target dependency, before the new assertions could run. The corrected `79df7ce3` candidate includes `FilterCore.h` and links `zplane_core` in `zplane_native_host_tests`; the subsequent run above passed.
- **Cloud pacing (not ASIO):** 180 callbacks at 48 kHz/256 samples; deadline 5.33333 ms; mean 0.0111806 ms, p95 0.0149 ms, p99 0.0198 ms, max 0.0202 ms, **0 callback deadline misses and 0 scheduler-late blocks in this run** (Windows 1 ms timer period requested). No causal before/after CPU claim because the runner/load are not controlled.
- **Still NOT VERIFIED:** multiple reversals inside one unfinished family fade under a real host; cross-family automation with wet/bypass changes, GUI mouse/automation/state and owner-host images, true ASIO/ETW timing, release rights to external research core/tables, 125A Plugin Test Lab/full adversarial audit, and finished commercial GUI. No owner-approved FINAL/release.
