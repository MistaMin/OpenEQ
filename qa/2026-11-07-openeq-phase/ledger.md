# Scope ledger: OpenEQ phase modes, windowing, per-band phase, render DSP

Project: OpenEQ   Branch: master @ 3a0494e   Started: 2026-11-07   Agent: DeepSeek (harness)

Sources of scope:
- Request: "add those. That sounds good" (following the phase design phase/latency analysis: zero-latency analogue, window controls, per-band free phase, render DSP).
- phase design manual (/Users/marcosdeida/Downloads/external_phase design_Manual.pdf) sections 4.1 DSP, 4.1.2 Processing Mode, 5 Render DSP: FIR phase options (minimum, analogue, zero-latency analogue, linear, free with per-band phase handles 0deg/180deg/-180deg), window shapes (Kaiser default + classic set) with a window parameter that tightens the IR to reduce preringing, impulse padding, and Render DSP settings used only while rendering offline.
- Existing engine: Source/LinearPhaseEngine.h/.cpp (FFT kernel design: Blackman-Harris window, symmetric IR, 4 quality levels, latency = kernel half), processingModeNames {Zero Latency, Linear Phase, Analog, Mix}, suite checks latency reporting.

## Items

| ID | Item | Source | Code | GUI | Test | Evidence | Status |
|---|---|---|---|---|---|---|---|
| P1 | Zero-latency analogue mode: minimum-phase analogue kernel rendered causally, 0 reported latency | manual 4.1.2 | LinearPhaseEngine Target::AnalogZeroLatency + PluginProcessor latency | processing mode cycle ("Analog ZL") | latency 0, causal peak at sample 0, 300 Hz magnitude 5.78 dB vs 5.83 linear | dsp/evidence/test-run-phase.log | verified |
| P2 | Window shape choice (Blackman-Harris default, Kaiser, Hann, Hamming, Blackman, rectangular) | manual 4.1.2 | LinearPhaseEngine buildWindows | FFT menu Window submenu | Kaiser render keeps 300 Hz magnitude 5.80 dB | dsp/evidence/test-run-phase.log | verified |
| P3 | Window parameter (Kaiser beta / skirt exponent, 0..1) | manual 4.1.2 | LinearPhaseEngine buildWindows | FFT menu tightness submenu | full tightness (beta 14) magnitude within 0.8 dB | dsp/evidence/test-run-phase.log | verified |
| P4 | Per-band free phase: b<i>_phase parameter mapping linear (0) / minimum (1) / inverse (-1) blended into the kernel spectrum | manual 4.1.2 | LinearPhaseEngine complex rotation + BandParameterState | band panel PHASE knob | linear symmetric, minimum/inverse one-sided with mirrored side bias, energy preserved | dsp/evidence/test-run-phase.log | verified |
| P5 | Render DSP: offline-render quality and oversampling settings applied only during non-realtime rendering | manual section 5 | PluginProcessor setNonRealtime + effectiveLinearPhaseQuality | FFT menu render submenus | quality override on/off verified | dsp/evidence/test-run-phase.log | verified |
| P6 | Regression tests for all of the above; suite green | suite | Tests/RegressionTests.cpp checkPhaseModes | — | 41 sections pass | dsp/evidence/test-run-phase.log | verified |
| P7 | Docs updated (README + ledger) | docs | docs/ | — | read | — | in progress |

## Parameters (host list vs ledger)

New (append-only where possible): processing mode choice gains "Analog ZL" (appended); global windowShape (choice), windowParameter (float 0..1), per-band b<i>_phase (float -1..1), renderQuality (choice), renderOversample (choice/bool). Each needs a layout row, GUI control, audio effect, state round trip, and host-list presence.

## Exceptions (each needs a reason)

| Parameter / check | Exception | Reason | Decided by |
|---|---|---|---|
| Free-phase graph handles | v1 exposes per-band phase as a knob; dragging phase handles on the graph is deferred | the graph-handle UI is a separate visual feature; the DSP and parameter come first | this agent |
| Impulse padding control | not exposed (engine handles wrap-around internally) | manual luxury; revisit if needed | this agent |
| Digital+ IIR phase compensation | not in this batch | larger separate research item | this agent |

## Open questions

- Window parameter semantics: Kaiser beta scaling (0..1 -> beta 0..14) or a generalised tightening across shapes? (default: Kaiser beta = 14 * p, other shapes: interpolate toward rectangular as p -> 1)
- Per-band phase default: 0 (linear) keeps current sound; minimum-phase per band at 1 matches "analogue" style. Confirm defaults with user later.

## Out of scope

| Item | Decided by | Where |
|---|---|---|
| Free-phase graph handles / phase graph UI | deferred | ledger P4 note |
| Surround routing groups | earlier analysis | — |
| P8 | Impulse padding control (Short/Normal/Full truncation of the windowed impulse) | manual 4.1.2 | LinearPhaseEngine setImpulsePadding | FFT menu | magnitude kept within 0.9 dB at each setting | dsp/evidence/test-run-phase.log | verified |
| P9 | Digital+ IIR phase compensation: steps 0..4, sequential 2nd-order all-pass phase equalisers per section | manual 4.1.1 | ResponseModel digitalPlusAllPassCoefficients + EQBand | FFT menu | phase-line deviation 0.0606 -> 0.0209 at step 4 (Q=1 bell) | dsp/evidence/test-run-phase.log | verified |
