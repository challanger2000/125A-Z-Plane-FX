# Development contract — 125A Z-Plane FX v0.1.0

Repository: challanger2000/125A-Z-Plane-FX. New product, no owner-approved release; branch develop/v0.1.0. Main contains historical tooling only.

## Current implementable scope
- Stereo-in/stereo-out VST3 audio effect, 32- and 64-bit host sample buffers.
- Independently implemented, textbook normalized biquad LP/HP/BP/notch modes, NOT E-MU X3 filters.
- Parameter IDs: 0 mode, 1 cutoff, 2 resonance, 3 wet, 4 bypass. Stable across subsequent compatible iterations.
- Zero dry/wet = bit-identity; bypass = bit-identity; exact sample-rate-aware coefficients; no allocation/locks/I/O in audio callback.
- Generic host parameter UI only for now.
- Save and recall parameters (versioned format).
- Regression: impulse, silence, sample partition, 2-channel isolation, zero bypass identity, invalid parameter bounds, state roundtrips, MSVC Release active assertions.

## Future fidelity gate
- Do NOT import research code or extracted E-MU tables without an explicit appropriate rights basis.
- Need explicit independent 55-filter reference source and bank provenance. The validated research results are scientific evidence, not an automatic copyright license.
- Implement and compare all 55 filters against the authorized baseline before calling any future candidate "Z-Plane accurate".
- Add modulation, morphing, dynamic ramps, native GUI only after core verified.
- Need Steinberg validator, 125A Plugin Tester, DAW and ASIO, actual audio fixtures and user listening prior to FINAL.

## Known limitations
Prototype does not emulate X3 filter types, native coefficient tables, morph designer, adaptive gain or hardware. Parameter changes at host block boundary only; no sample-offset automation yet. All final release gates NOT VERIFIED.
