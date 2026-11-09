# 125A Z-Plane FX — Windows VST3 development v0.1.0

**Research prototype only. Not a finished or authorized commercial E-MU emulation.**

The plugin now has two DSP paths:

- **Legacy filters:** four independent biquad filters (low-pass, high-pass, band-pass, notch) with stereo input/output, mix and bypass.
- **Experimental X3 path:** filter selector with 33 factory, 17 standard and 5 programmable Emulator X3 filter types, linked against the [public research implementation](https://github.com/noneofyourbisness/EMU-Emulator-X3-ZPlaneFilter-Research) only in the opt-in development build. The reference C++ sources are obtained from their original archive at a pinned checksum during CMake configuration, not committed to this repository.

**The 55 X3 filters are now fully self-contained in the Windows VST3.** The three reconstructed and source-verified coefficient tables (`factory-bank.bin`, `standard-bank.bin`, `morph-support.bin`) are compiled into the plugin PE as native RCDATA resources. During initialization, the bank parser reads temporary copies on the **control thread**; they are removed immediately afterward. Users do **not** need an external `Banks` folder, an environment variable, or any manual installation of filter files. If embedded resource loading ever fails, initialization fails explicitly rather than silently pretending to filter dry audio. Separate external research files are needed **only** to run an independent scientific reference oracle locally, not for listening to or using the 55 filters.

This branch exposes the actual research filter routines. Windows CI now builds **seven** Release-mode CTests, including 55 filter modes with real reconstructed banks across 44.1/48/96/192 kHz and the original independent X3 output reference vectors. The packaged VST3 is also loaded and tested in a native Windows host using the original-bank data. Those tests demonstrate bit-accurate results for the covered X3 reference cases, not exhaustive full-product or hardware emulation.

## Windows build

Build Windows x64 with C++23 and pinned Steinberg VST3 SDK `v3.8.1_build_84` (commit `3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96`).

Enable `-DZPLANE_WITH_X3_RESEARCH=ON` to compile the experimental external research backend. A build without this option retains the legacy DSP and the new state format.

[Fully embedded 55-mode Windows prototype + owner BAT kit, #42555445494](https://github.com/challanger2000/125A-Z-Plane-FX/actions/runs/42555445494): **7/7** CTests, **47/47** Steinberg Validator, **2,640/2,640** original X3 model PCM cases, GUI pointer/menu/control tests, and an additional **440** real staged VST3 renders (55 modes × 4 rates × 32/64-bit) using a *clean Windows profile without bank files*. The owner-local BAT and test kit were built and executed in a separate Windows job in the same run. The kit verifies SHA256, six stereo audio/state cases, native GUI/snapshot and the **embedded 55-mode smoke**, and reports an optional missing external **research-reference** oracle as `PARTIAL_X3_REFERENCE_NOT_RUN`; actual embedded X3 functionality is a mandatory PASS. End users do not need Python, a bank directory or admin rights.

## Compatibility and limitations

The original five parameter IDs remain unchanged. Parameter ID 5 adds X3 selection, and component state v2 reads both v1 (five parameters) and v2 (six parameters). A first dark VSTGUI control layout is included (55-type X3 selector, Morph, Resonance/Q, Wet, Bypass and legacy filter selector). Its Windows editor view can be created by the actual VST3 host and reports the intended 900×470 size. Actual Win32 pointer/control/menu gestures are covered by native regression tests; owner visual/design acceptance, HiDPI variants and DAW session interaction remain unverified. The X3 Morph Designer programming controls are not exposed.

**Distribution provenance:** The project owner states that the required commercial distribution rights are held. The repository itself does not contain a publicly verifiable grant; any commercial release decision is separate from the technical build and tests.

The independent 125A Plugin Test Lab **technical baseline** and 55-filter original-research PCM comparisons passed in CI, but genuine Fender Studio Pro/ASIO playback, independent E-MU hardware fidelity and the entire required product audit remain open. **No FULL AUDIT and no release approval.** See [Development scope](docs/DEVELOPMENT-SCOPE.md).
