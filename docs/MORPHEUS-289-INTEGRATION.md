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
