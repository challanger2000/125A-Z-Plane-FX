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
