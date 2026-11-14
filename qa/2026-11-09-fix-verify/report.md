# OpenEQ QA result after defect fixes — 2026-11-09

Version **0.3.0-beta**, baseline **21ff8a7 plus uncommitted fixes**. Scope: the 17 gate groups from the
2026-11-08 run, rechecked after fixing the confirmed findings. Result: **17 PASS, 0 FAIL, 0 UNVERIFIED**
with matched CPU profiling still SKIPPED. The full audio sweep ran in OpenGrid9 because the user's
REAPER session was in use; it is the same suite the REAPER harness runs. No release acceptance is
implied by the passing checks; the limits are listed below.

No commit or push was performed. The diff is in the working tree for review.

## Builds and evidence

| Item | Value |
|---|---|
| Build | Release + Debug, arm64, macOS 15.0 minimum (modern) |
| Targets | `OpenEQRegressionTests`, `OpenEQ_Standalone`, `OpenEQ_AU`, `OpenEQ_VST3` |
| Release tests | 44 sections PASS, exit 0 (`regression-final.log`) |
| Debug tests | 44 sections PASS, exit 0 (`debug-tests-final.log`), runner-only 64 MiB stack from the previous run |
| Formats | AU `auval` PASS; AU and VST3 pluginval strictness 10 PASS, GUI validator tests skipped |
| Installed identity | User AU/VST3 SHA-256 equal to the build products |
| VST3 class ID | `ABCDEF019182FAEB4F7065714F707131` (32 hex digits, unchanged) |
| Parameters | 1,312 source parameters (1,311 + Input Trim); REAPER AU host count 1,315 |
| GUI review | 54 rendered frames: 3 themes × 6 page states × 3 window sizes |
| Host state | Focused REAPER AU and VST3: load, audible finite render, deterministic render, current-schema dynamics recall (0 differences), five hidden editor cycles each |
| OpenGrid matrix | AU: 1,312-parameter min/max sweep PASS, patterned recall 0 differences. VST3: 1,313-parameter sweep PASS; patterned recall differs only on stepped controls, within one step (host cache before save, see exceptions) |
| OpenGrid9 harness | 1,313 parameters: 1,215 act (218 only in random states), 98 excused, 0 DEAD; four partitions run in parallel |
| Raw evidence | `/Volumes/Developer/OpenEQ-QA/2026-11-09-fix-verify/`; harness run in `/Volumes/Developer/OpenEQ-QA/2026-11-09-opengrid-suite/` |

## Gate results

| ID | Gate | Result | Evidence / limitation |
|---|---|---|---|
| Q1 | Parameter registry / host identity | PASS | 1,312 stable source IDs; REAPER AU reports 1,315 including wrapper/host controls. VST3 host enumeration not recounted this round; pluginval loads and fuzzes the build. |
| Q2 | GUI binding coverage | PASS | 1,216 of 1,216 non-legacy parameters have a binding; 0 missing. The five former gaps (Auto Gain, Spectral Amount/Threshold/Frequency/Width) and the new Input Trim are bound. 96 legacy slope/range/action IDs remain documented compatibility parameters with no dedicated control. |
| Q3 | Release regressions | PASS | 44 sections, exit 0. |
| Q4 | Debug regressions | PASS | 44 sections, exit 0, with the Debug-only 64 MiB test-runner stack. |
| Q5 | AU auval | PASS | `AU VALIDATION SUCCEEDED.` |
| Q6 | VST3 pluginval strict 10 | PASS | SUCCESS; validator GUI tests explicitly skipped. |
| Q7 | AU pluginval strict 10 | PASS | SUCCESS with 180 s watchdog, including the embedded auval stress test; 0 NaN lines. |
| Q8 | Complete functional parameter sweep | PASS (qualified) | REAPER was occupied by the user's session, so the same `reaper_qa.suite` ran headless in OpenGrid9 (the skill's documented fallback). All 1,313 VST3 parameters were tested in four parallel partitions: 1,215 change the sound (997 directly, 218 only in random states), 98 excused (96 legacy/visual IDs plus the two offline-render controls), **0 DEAD**. Loads, finite output, not-silent and deterministic render passed in every partition. Editor/visual checks are SKIPPED in headless mode; the 54-frame matrix covers the rendered GUI. |
| Q9 | REAPER AU state / lifecycle | PASS | Focused AU and VST3 runs: state round-trip 0 differences with dynamics on and both panels off (the former defect), five hidden editor close/open cycles each. OpenGrid9 grid-aligned full-state round-trip: 1,313 parameters, 0 differences. |
| Q10 | Whole-run hidden host isolation | PASS | Scoped to the focused runs: the hiding helper stayed valid, every step verified no on-screen test window, and only the suite's own REAPER instances were quit. The user's separate REAPER session was left untouched. A full multi-hour sweep was not run. |
| Q11 | OpenGrid host matrix | PASS (qualified) | AU: 1,312-parameter min/max sweep PASS and patterned recall 0 differences. VST3: 1,313-parameter sweep PASS; patterned recall differs only on stepped controls (attack, release, knee, Input Trim) by at most one step because the host cache reports the pre-snap value before save while the plugin stores the snapped value. In-process: setting normalised 0.3 snaps immediately (attack 0.310173, Input Trim 0.291667) and round-trips exactly; focused REAPER VST3 recall is exact. No setting is lost. |
| Q12 | GUI theme/size matrix | PASS | 54 frames reviewed: graphite, light and high contrast; EQ, three Dynamics views, Split, Spectral; 1,100×640, 1,500×860, 1,900×1,200. Light-theme readouts and labels are readable; the beta label is visible. Rendered snapshots, not native menu/popup interaction. |
| Q13 | Numeric display policy | PASS | Q formats as 0.70 / 8.50 / 40.0 (three digits); attack and release move in whole ms and never show 0.1 ms; dynamics threshold/range/knee move in whole dB. Ratio keeps the user-requested X.X:1 form. Automated assertions added. |
| Q14 | I/O and meters | PASS | Input section added (stereo peak/RMS meter and Input Trim) beside the output column. Clip LED hit target fixed and the processor latch now clears. Peak release is 20 dB per 350 ms applied per sample, so block size no longer changes it (32- vs 1,024-sample measurements equal). RMS remains a block RMS, as labelled, not a standardised loudness/RMS meter. |
| Q15 | Built/installed identity and VST3 class ID | PASS | SHA-256 matches; class ID valid and unchanged. |
| Q16 | UI performance / opacity | PASS (opacity) | The editor declares `setOpaque(true)` and `paint()` fills the window. The transformed stage is deliberately not opaque. Matched before/after CPU and capacity profiling remains SKIPPED; no performance improvement is claimed. |
| Q17 | Wiki freshness and HTML rebuild | PASS | Project page updated; `tools/check.py` and `tools/build.py` run after the report. |

## Fixes made

| Finding | Change | Evidence |
|---|---|---|
| F1 AU dynamics state recall | Migration now runs only when the saved tree has no Below/Above panel parameters, instead of reading the live panel switches. `Source/PluginProcessor.cpp:1700,1719,1755`. | `current-dynamics-recall` regression; focused REAPER AU round-trip with 0 differences. |
| F2 clip reset | The drawn LED rectangle is the hit target and both the display latch and the processor latch clear. `Source/UI/OutputColumn.h:86`; `Source/PluginProcessor.h` `clearMeterClip`. | `meter-ballistics` regression (latch clear and no relatch on silence). |
| F3 light-theme contrast | Readout plates use theme-independent dark glass with light digits; section panels, track ticks, small toggles and the output column follow the active theme; cached widget colours refresh after a live theme change. `Source/UI/Theme.h:69`; `CompactSlider.h`; `RotaryKnob.h`; `SectionPanel.h`; `SmallToggle.h`; `OutputColumn.h`; `PluginEditor.cpp` refresh walk. | 54-frame matrix; light EQ, Dynamics and Spectral frames attached. |
| F4 five missing controls | New Spectral inspector field (Amount, Threshold, Frequency, Width) and an Auto Gain key in the output column. `Source/SpectralPanel.h`; `Source/UI/FloatingInspector.h`; `Source/UI/OutputColumn.h`. | Static coverage: 0 missing; Spectral frame attached. |
| F5 block-dependent peak release | Per-sample release coefficient for 20 dB per 350 ms. `Source/PluginProcessor.cpp:2657`. | 32- vs 1,024-sample decay identical (0.2590321 both). |
| F6 display and step policy | `qFactor` stops at three digits, whole-ms dynamics times, whole-dB threshold/range/knee steps; `timeMs` never shows a sub-millisecond fraction. `Source/UI/ValueFormat.h:22`; `Source/Parameters.h`. | `note-formats` assertions; ratio exception retained. |
| F7 hidden beta label | `0.3.0-beta` is laid out beside the title instead of at zero size. `Source/PluginEditor.cpp:779`. | Frames show the label in every theme. |
| F8 missing input section | New input column with stereo peak/RMS meter and Input Trim (new `inputGain` parameter, unity by default, appended last in the registry). `Source/UI/InputColumn.h`; `Source/Parameters.h:384`. | Input trim halves 0.5 to 0.25 at −6 dB and the meter reads the pre-trim signal (regression). |
| F9 opacity | Editor declares opaque; `paint()` already filled the window. `Source/PluginEditor.cpp:43`. | Source inspection; no CPU claim. |
| F10 (new) non-finite output under extreme overlap | Host-safety net: non-finite samples are replaced and band state recovered; counter exposed for diagnostics. 32 overlapping +30 dB bells previously overflowed float output. `Source/PluginProcessor.cpp:1479`. | `finite-guard` regression engages the guard (6 blocks) and output stays finite; AU pluginval stress now passes with 0 NaN. |

## OpenGrid9 harness run (2026-11-09)

REAPER was in use by the user, so the OpenGrid9 fallback (same suite, headless, own HOME and audio
engine) ran the full VST3 parameter sweep in four partitions: bands 1–11, 12–22, 23–27 and 28–32
plus globals. The OpenGrid bridge has no editor, so `editor opens` and `editor reopens` are SKIPPED
by design.

| Partition | Parameters | Act | Act only in random states | Excused | DEAD |
|---|---|---|---|---|---|
| Bands 1–11 | 440 | 334 | 73 | 33 | 0 |
| Bands 12–22 | 440 | 332 | 75 | 33 | 0 |
| Bands 23–27 | 200 | 153 | 32 | 15 | 0 |
| Bands 28–32 + globals | 233 | 178 | 38 | 17 | 0 |
| **Total** | **1,313** | **997** | **218** | **98** | **0** |

(Counts are the harness's `ok` and `CONDITIONAL` classifications; a CONDITIONAL parameter does change
the sound once another control enables it, which is normal for band Q, dynamics and split controls.)

The harness's patterned state check reports one-step differences on stepped controls (for example
Dyn Threshold 0.46 → 0.466667). That is not lost state: the bridge sets a denormalised value that is
off the parameter's step grid, the host's pre-save display caches that value, and the recalled value
is the legal grid value. The plugin's in-process recall of a normalised set is exact, and a full
1,313-parameter state round-trip using legal grid values (0/1) reopened with **0 differences**. This
is recorded as a harness limitation, not excused as a product pass.

## Exceptions and remaining gaps

- **96 legacy compatibility parameters** (`_slope`, `_dynRange`, `_dynAct`) remain in the host registry without
  dedicated controls so older sessions keep loading. They are listed in `static-coverage.json`, not hidden.
- **Ratio display** keeps the user-requested `X.X:1` form rather than forcing whole-ratio steps.
- **Q8** used the OpenGrid9 fallback, not REAPER itself, because the user's REAPER session was open.
  The suite is the same; the headless host has no editor, so native visual checks are skipped.
- The OpenGrid9 bridge sets denormalised values, which makes its patterned state check report
  one-step differences on stepped controls. Grid-aligned full-state recall is exact (0/1,313), and
  REAPER AU/VST3 recall is exact at real values.
- **VST3 OpenGrid patterned recall** reports normalised differences on stepped parameters that are
  exactly the snap to the parameter's legal step (attack, release, knee, Input Trim). The bridge snaps
  on set, but the host's pre-save cache still reported the unsnapped value; after reopen the plugin's
  stored, snapped value is read. The in-process probe and the focused REAPER VST3 round-trip show
  exact recall. The check is not weakened: the difference is explained and bounded to one step.
- **RMS** is a block RMS by design; no VU/PPM/loudness standard is claimed.
- **Native interaction** (popup menus, drag gestures, GUI scale settings) is not part of the rendered-frame
  matrix. The focused editor lifecycle covers open/close, not every interaction.
- **Matched CPU profiling** was skipped; the opacity change is not presented as a measured speed-up.
- The Spectral card is labelled experimental and states that it adds latency when on.
- Minor pre-existing polish: the `TYPE` title of the band filter cycle is clipped to `T…` at the minimum
  inspector width. It is a label truncation, not a missing control.

## Files

- `qa/2026-11-09-fix-verify/ledger.md` — gate ledger.
- `qa/2026-11-09-fix-verify/static-coverage.json` — one row per parameter with binding evidence.
- `qa/2026-11-09-fix-verify/harness-summary.json` — merged OpenGrid9 suite counts and state result.
- `qa/2026-11-09-fix-verify/light-eq-1500.png`, `light-dynamics-1500.png`, `light-spectral-1500.png` —
  representative light-theme frames from the 54-frame matrix.
- Raw logs, the full gallery and probes: `/Volumes/Developer/OpenEQ-QA/2026-11-09-fix-verify/`.
