# Morpheus / UltraProteus 289-cube integration candidate

Development branch: \`feature/morpheus-ultraproteus-289-cubes\`. Separate from 55 existing X3 filters.

- 289 recovered Rossum Morpheus records, 116 32-bit words each, including index 0 Null Cube, indexed consistently with historical first 197 Morpheus + 91 UltraProteus-only types.
- New independent seven-stage DSP, native encoded cube interpolation along Transform/Frequency/Morph; 44.1/48/96/192 kHz host-rate adaptation; distinct stereo state, nonfinite guards.
- New VST3 controls 6..10: Morpheus On, Cube, Morph, Frequency, Transform. Existing 0..5 parameter IDs unchanged.
- State v3 serializes 11 parameters; v1/v2 upgrade keeps older saved settings and disables new engine. Engine changes use bounded crossfade; no audio-thread heap or file I/O.
- Minimal native VSTGUI access for new selection/controls. Artistic redesign and accurate frequency-response display intentionally deferred.
- Native host PCM matrix and 289-cube standalone regression added. Real original hardware bit accuracy is NOT claimed.
- C++23 Windows x64 VST3 only. Owner reports source-data and implementation distribution rights cleared.
- Release gates: real Windows CI/CTest, native VST3, Steinberg validator, Test Lab, real Studio One/ASIO, full listening/hardware comparison NOT VERIFIED until run and reviewed.

## Independent reference regression (2026-11-09)

- Added frozen upstream Morphosis linear-filter cases for cube 46 `MdQ 2PoleLP` at 48 kHz, cube 197 `Dave'sRave` at 96 kHz, and cube 288 `AllPoleDst2` at 48 kHz. Seven-stage coefficient, exact gain, spectral magnitude and 32-sample stereo response are checked against independent, immutable values.
- Restored native binary32 radius rounding before host-rate conversion, plus the firmware cosine approximation at the native sample rate. Tests include a changed-coefficient negative control so a flat/neutral response cannot pass.
- Source: `limitsurface/morphosis_vst/tests/fixtures/reference.json`, schema `morphosis-linear-reference/v1`, source cube-body hash `07ec5c87a17fbcc6c199ced1caf6756c9e2ad8813daf05eb9a9cdff2e610ff29`.
- Mathematical/PCM agreement with these reference cases is not proof of historical hardware accuracy, all 288 filter sounds, ASIO scheduling or Studio One acceptance. No DSP UI filter display yet.

## Full 288-filter independent oracle (development QA)
- A TEST-ONLY executable compiles the **pinned upstream MorphosisDSP.cpp** directly at commit `d271a8a8c6cf8bb5524e588c3497f4b5544a22cd`. This reference code is **not linked into the shipped VST3**.
- Compares every non-null cube (1..288) against upstream coefficients and analytical filter response at **five 3D settings × five sample rates** (including native firmware rate). Any missing/invalid case, silent fallback or mismatched coefficient is a regression failure.
- The seven-pole-pair signal transfer uses both recovered Cartesian components a/b; evaluating the native cosine/sine firmware approximations must preserve a²+b² rather than substituting radius².
- Physical hardware equivalence and nonlinear behavior still require independent hardware/audio references and host acceptance.

## Functional VSTGUI filter display
- Custom view `125AFilterResponse`, 256 log-spaced bins from 20 Hz to 20 kHz, visible -48..+24 dB. The editor remains a deliberately provisional functional UI (900×835).
- **Morpheus/UltraProteus**: complex frequency response of the same seven-stage coefficients as the audio engine; Dry/Wet and Bypass mixed in the complex domain; the Null Cube is exactly flat. Changes in preset, Morph, Frequency and Transform redraw on the UI thread at up to 10 Hz.
- **Legacy**: precise RBJ biquad transfer from active Mode/Cutoff/Q, including Dry/Wet, Bypass.
- **X3**: an independent GUI-thread X3ResearchAdapter instance loads the embedded bank through the same resource loader and measures 8192-sample 48 kHz impulse response; radix-2 FFT and complex-bin interpolation provide the frequency curve. Failed bank load visibly says response unavailable: **no fake curve**.
- Chart explicitly labels 48 kHz as a fixed reference sample rate, not the host's actual rate. The finite X3 impulse introduces low-frequency/tail approximation and cannot be called hardware-exact. Audio callbacks never compute the graph, FFT or redraw.
- Automated tests cover analytic transfer, wet/bypass/null identity, Morpheus-axis response, X3 no-fallback, and native plugin GUI attachment, plus the preexisting VST3 host regression matrix.

### VSTGUI redraw QA
Windows Actions uses Direct2D for the plugin GUI, so Win32 `BitBlt` of the frame returned constant white/black pixels despite correct VSTGUI redraws. The native-host test therefore verifies actual custom `CView` construction, repeated `draw()` callbacks, and changed numeric curve values delivered to the line renderer. A development-only test ABI (`ZPlaneResponseQaGet`) exists only when `BUILD_TESTING=ON`. This is **not** a pixel-perfect image test or proof of visual appearance; further visual Studio One acceptance remains open.

## LFO 1 + 2 — development implementation
- Each LFO: ON, RATE/DIVISION, DEPTH, five shapes (sine, triangle, saw, square, deterministic sample-and-hold), TARGET, DAW SYNC.
- Targets: Morph (Legacy/X3/Morpheus), Frequency/Transform (Morpheus), Resonance/Q (Legacy/X3). Both LFOs sum bipolar offsets into clamped normalized values.
- Free 0.05–12 Hz, beat sync 1/64 to 4 bars. VST3 ProcessContext requests PPQ position and tempo; fallback uses a continuous sample/beat accumulator if a host omits position. Modulation changes are at ~5 ms DSP control rate, aligned independently of host blocks.
- Old v1/v2/v3 state loads into new v4 (23 params) with both LFOs disabled. Existing X3/Morpheus filter values remain base/automated; audio-only effective values are not written back into project state.
- The provisional response display currently visualizes the **base filter curve**. A true DAW-time telemetry feed to redraw moving LFO output is **not implemented** and must not be claimed.
- 0 depth and OFF routes preserve legacy PCM. This is a functional development slice, not an audit, finished GUI design, performance acceptance or release.
