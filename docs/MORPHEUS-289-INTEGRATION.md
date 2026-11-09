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
