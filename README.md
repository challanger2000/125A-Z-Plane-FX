# 125A Z-Plane FX — development 0.1.0

**Status: EARLY PROTOTYPE — NOT an E-MU filter emulation or finished release.**

Windows x64 VST3 audio effect. Currently the independently written **reference biquad filter** supports low-pass, high-pass, band-pass and notch, with stereo processing, wet/dry and bypass. This is a host and DSP integration foundation, **not** the recovered 55-filter implementation.

## Rights / provenance
The external [Emulator X3 filter research](https://github.com/noneofyourbisness/EMU-Emulator-X3-ZPlaneFilter-Research) contains a useful independently verified technical study. Its public repository does **not** explicitly grant a reuse license, and its own README warns that some data derives from a commercial installation. No external source, coefficient bank, extracted table, ROM, installer or proprietary code is bundled here. A documented permission/provenance decision is mandatory before integrating any external filter implementation.

## Source
C++23, Steinberg VST3 SDK pinned to tag `v3.8.1_build_84`, commit `3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96`. Windows x64 only. No custom GUI yet. No release approval.

## Build
Configure with CMake, `-DVST3_SDK_ROOT=<recursive pinned SDK checkout>`; build `ZPlaneFX` and `zplane_core_tests`, then run `ctest`. CI checks Windows compilation, deterministic DSP and VST3 Validator. Fender Studio Pro / 125A Plugin Tester are separately NOT RUN.

## Engineering contract
See `docs/DEVELOPMENT-SCOPE.md`. Product is an audio insert, no MIDI instrument, no sampler, no connection to private copyrighted installers. Full 55-filter fidelity is **NOT IMPLEMENTED**.
