# Scope ledger: OpenEQ rename + Split/Waterfall/Cross-instance features

Project: OpenEQ (renamed from GodEQ)   Branch: master @ a64aa21 (0.3.0-beta)   Started: 2026-11-07   Agent: DeepSeek (harness)

Sources of scope:
- Request (this session): "lets change the name to OpenEQ" + "1) access any instance of the eq from the same window in another channel and vice versa, 2) a 3d waterfall mode from the fft, 3) every band can have a split mode that enhances/reduces the transient or enhances/reduces the tail. Like transient/tonal from external. feel free to test transient/tonal from external"
- transient/tonal User Guide (~/Music/external/transient/tonal/transient/tonal User Guide.pdf): transient/tonal split is complementary (transient + tonal = dry), level-independent, with Source/Transient Separation/Transient Decay/Smoothing controls; two parallel EQs with per-path band gains (green transient / blue tonal), linked handles.
- Existing machinery: Source/InstanceRegistry.h (in-process instance list, ids, spectrum summaries), DynamicsEngine fastEnvelope + transientPercent onset weight, SpectrumAnalyzer FFT snapshots.

## Items

| ID | Item | Source | Code | GUI | Test | Evidence | Status |
|---|---|---|---|---|---|---|---|
| R1 | Product renamed OpenEQ: plugin name, codes Opq1/Opeq, bundle id, window title, menus | request | CMakeLists, PluginEditor.cpp | window title | build + suite | OpenEQ.vst3/.component/.app | verified |
| R2 | Presets folder moved to Application Support/OpenEQ with one-time migration from GodEQ | request | PluginProcessor.cpp userPresetDirectory | preset menu | preset-import/preset tests | suite green | verified |
| R3 | Docs/README renamed | request | README, docs/ | — | read | — | verified |
| R4 | plugin-qa profile updated to OpenEQ | profile | skills/projects/openeq.md | — | — | — | verified |
| S1 | Per-band split mode: transient gain and tail gain, complementary split (sum = dry when gains 0) | request + guide p.8 | EQBand (dual-state lanes 2/3), PluginProcessor processLane/computeSplitMeasure | band panel + graph handles | null test + burst/tone separation (suite "split-mode") | dsp/evidence/test-run-split.log | verified (DSP) |
| S2 | Global split controls: separation (0-100), decay (0-100), smoothing (0-50 ms), source presets | guide p.8-9 | PluginProcessor computeSplitMeasure | split section (GUI pending) | separation test passes via params | dsp/evidence/test-run-split.log | verified (DSP) |
| S3 | Split parameters in the host list, saved in sessions, schema migration | request | Parameters.h (append-only) | knobs (GUI pending) | state round trip passes | dsp/evidence/test-run-split.log | verified |
| S4 | Transient/tonal solo for tuning | guide p.8 | processLane soloMode 1/2, splitSolo param | solo buttons (GUI pending) | render check (pending) | — | in progress |
| W1 | Waterfall display mode: history of analyzer FFT frames | request | SpectrumAnalyzer waterfallHistory, WATER source mode | FFT source cycle (POST/PRE/SIDE/WATER) | suite green; visual sweep pending | dsp/evidence/test-run-ci.log | verified (code) |
| W2 | 3D tilt/perspective rendering of the waterfall, colour mapping | request | SpectrumAnalyzer paintWaterfall + Theme::waterfallColour | waterfall view | screenshot (pending) | — | verified (code) |
| C1 | Instance registry exposes processors (pointer + id + name) for in-process access | request | InstanceRegistry.h | — | checkCrossInstance | dsp/evidence/test-run-ci.log | verified |
| C2 | Editor window can target any other instance: view its graph/meters and edit its parameters (both directions) | request | PluginProcessor setRemoteTarget + PluginEditor INST button | INST menu | relay test (A->B and B->A) | dsp/evidence/test-run-ci.log | verified |
| C3 | Instance lifetime safety: viewed instance removed -> fall back to own instance | request | relayParameter registry re-check | — | destruction test | dsp/evidence/test-run-ci.log | verified |
| T1 | Regression tests for all of the above; suite stays green | suite | Tests/RegressionTests.cpp | — | 40 sections, ctest 100% | dsp/evidence/test-run-ci.log | verified |
| T2 | Docs updated (README, parity-style feature docs) | docs | README.md | — | read | — | verified |

## Parameters (host list vs ledger)

New parameters to add (append-only): per band b<i>_splitOn, b<i>_splitTransientDb, b<i>_splitTailDb (32 x 3 = 96); global: splitSeparation, splitDecay, splitSmoothingMs, splitSource, splitSolo (5). Every new parameter needs: layout row, GUI control, audio effect, session save (schema 4 migration), host list check.

## Exceptions (each needs a reason)

| Parameter / check | Exception | Reason | Decided by |
|---|---|---|---|
| transient/tonal reference | Only the AAX build is installed; no VST3/AU to probe | measuring AAX requires Pro Tools; the User Guide + patent-free own detector design are used instead | this agent |
| Reference separation algorithm | Not copied; own complementary transient/tonal split | A reference product's separation method is patented; OpenEQ uses its own design | this agent |
| .godeq preset extension | New saves use .opeq; old .godeq files still load | migration keeps old sessions working | this agent |
| Cross-instance access | In-process only | hosts that isolate plugins per process cannot share memory; documented | this agent |

## Open questions

- Waterfall: fixed 3D tilt or user-adjustable? (default: fixed tilt, adjustable in settings — confirm with user if needed)
- Split: per-band split uses the band's shared EQ curve with separate transient/tail gains (v1); full independent per-path curves (transient/tonal dual handles) later. OK?
- Cross-instance: should parameter edits from window A to instance B be live (direct) or require an explicit "link" toggle? (default: a target selector; direct editing when another instance is selected)

## Out of scope

| Item | Decided by | Where |
|---|---|---|
| Full per-path EQ curves and pan (transient/tonal dual-handle UI) | this agent — v1 is per-band transient/tail gains on the shared curve | ledger S1 |
| Pro Tools AAX build of OpenEQ | no AAX SDK/format in the project | — |
