# 125A Z-Plane FX — Windows VST3 development v0.1.0

**Research prototype only. Not a finished or authorized commercial E-MU emulation.**

The plugin now has two DSP paths:

- **Legacy filters:** four independent biquad filters (low-pass, high-pass, band-pass, notch) with stereo input/output, mix and bypass.
- **Experimental X3 path:** filter selector with 33 factory, 17 standard and 5 programmable Emulator X3 filter types, linked against the [public research implementation](https://github.com/noneofyourbisness/EMU-Emulator-X3-ZPlaneFilter-Research) only in the opt-in development build. The reference C++ sources are obtained from their original archive at a pinned checksum during CMake configuration, not committed to this repository.

**The 55-filter path requires original coefficient banks, not committed to this repository.** All three have now been reconstructed from the original-reference recordings and test vectors and validated against both native C++ reference tests. The user receives the banks separately for local testing. Without installed banks, selecting an X3 filter returns dry audio, not a surrogate filter. The three files are `factory-bank.bin`, `standard-bank.bin` and `morph-support.bin`, placed in `%LOCALAPPDATA%\125A\ZPlaneFX\Banks` or the directory given by `ZPLANE_X3_BANK_DIR`.

This branch exposes the actual research filter routines. Windows CI now builds **six** Release-mode CTests, including 55 filter modes with real reconstructed banks across 44.1/48/96/192 kHz and the original independent X3 output reference vectors. The packaged VST3 is also loaded and tested in a native Windows host using the original-bank data. Those tests demonstrate bit-accurate results for the covered X3 reference cases, not exhaustive full-product or hardware emulation.

## Windows build

Build Windows x64 with C++23 and pinned Steinberg VST3 SDK `v3.8.1_build_84` (commit `3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96`).

Enable `-DZPLANE_WITH_X3_RESEARCH=ON` to compile the experimental external research backend. A build without this option retains the legacy DSP and the new state format.

[Verified Windows GUI + 55-filter development build, run 42523015491](https://github.com/challanger2000/125A-Z-Plane-FX/actions/runs/42523015491): **6/6 CTests; 47/47 Steinberg Validator; 440 native VST3 original-bank host PCM cases + editor create/getSize PASS**. The downloaded VST3 does NOT contain the bank binaries: install the three locally provided `.bin` files in `%LOCALAPPDATA%\\125A\\ZPlaneFX\\Banks` and restart the DAW.

## Compatibility and limitations

The original five parameter IDs remain unchanged. Parameter ID 5 adds X3 selection, and component state v2 reads both v1 (five parameters) and v2 (six parameters). A first dark VSTGUI control layout is included (55-type X3 selector, Morph, Resonance/Q, Wet, Bypass and legacy filter selector). Its Windows editor view can be created by the actual VST3 host and reports the intended 900×470 size. Visual rendering, user-interaction and production artwork remain unverified. The X3 Morph Designer programming controls are not exposed.

**External source licensing is unresolved:** the research repository contains no explicit reuse license, and the original bank data originates from commercial software. Development experiments are not clearance to distribute or sell the borrowed code or data.

No FULL AUDIT, no completed 125A Plugin Test Lab evaluation, no real Fender Studio Pro/ASIO acceptance and no original-bank 55-filter audio comparison have been performed for this development version. See [Development scope](docs/DEVELOPMENT-SCOPE.md).
