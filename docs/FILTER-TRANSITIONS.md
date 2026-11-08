## v0.1.0 DSP changes

- Filter mode/cutoff/Q changes are crossfaded between two stateful filter banks over 5 ms, independently per stereo channel.
- Dense updates cannot indefinitely reset an in-progress transition: the latest requested filter is queued for the next transition.
- Wet and bypass retarget a 5 ms linear ramp from the current value.
- All transitions are bounded, allocation-free, and deterministic under block partitioning.
- NOT the original E-MU X3 Z-Plane algorithms. The 55-filter source/provenance gate remains unresolved.
- Known limitations: host automation timeline has a fixed 4096-event bound; exceeding it currently returns failure. The timeline still uses insertion sorting and therefore requires a proper CPU stress gate before production.
- Native VST3 host audio/automation/recall QA, paced ASIO and 125A Test Lab remain NOT VERIFIED.
