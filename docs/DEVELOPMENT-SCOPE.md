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
