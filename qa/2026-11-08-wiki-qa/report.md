# OpenEQ QA result: FAIL

Version **0.3.0-beta**, baseline **21ff8a7**. Scope: 17 gate groups — 8 PASS, 7 FAIL, 2 UNVERIFIED. No release acceptance is implied by the passing tests.

The only source change made during this run is a Debug-only regression-runner stack correction in CMakeLists.txt. Product defects below remain unfixed. No commit or push was performed.

## Builds and evidence

Release AU, VST3 and Standalone were built/tested; the binaries are arm64 with macOS 15.0 minimum (modern). Legacy builds and release manifests are outside this QA run. User-installed AU/VST3 SHA-256 values match the build products. Hosts: REAPER 7.80 and OpenGrid 9.8.294 on macOS 15.7.5.

Raw logs, WAVs, per-parameter records, diagnostics and the 45-image gallery are retained locally at `/Volumes/Developer/OpenEQ-QA/2026-11-08-21ff8a7/`. The raw evidence remains outside the public source tree; selected small summaries are alongside this report. `parameter-ledger.json` in the raw folder has one row for every host parameter, resolved to the stable source ID where applicable.

## Gate results

| ID | Gate | Result | Evidence / limitation |
|---|---|---|---|
| Q1 | Parameter registry / host identity | PASS | 1,311 stable source IDs match VST3 hashes; 1,315 VST3 and 1,314 AU REAPER parameters include wrapper/host controls. |
| Q2 | GUI binding coverage | FAIL | 1,210 bindings found; five confirmed missing controls; 96 legacy compatibility IDs lack dedicated controls. |
| Q3 | Release regressions | PASS | 41 sections, 72.97 s. |
| Q4 | Debug regressions | PASS | Initial 32 MiB stack overflow; 93.25 s PASS with Debug-only 64 MiB test stack. |
| Q5 | AU auval | PASS | AU VALIDATION SUCCEEDED. |
| Q6 | VST3 pluginval strict 10 | PASS | SUCCESS; validator GUI tests explicitly skipped. |
| Q7 | AU pluginval strict 10 | PASS | SUCCESS with 180 s watchdog; initial 30 s embedded-auval timeout retained. |
| Q8 | REAPER VST3 complete functional sweep | UNVERIFIED | All 1,315 primary rows ran; conditional retries interrupted by hidden-window guard. Generic audio/visual references have confirmed false positives. |
| Q9 | REAPER AU state / lifecycle | FAIL | All 1,314 primary rows and conditional retries ran. State recall changed dynamics values; focused AU reproducer confirms three changed values. Focused five-cycle editor lifecycle passed hidden. |
| Q10 | Whole-run hidden host isolation | UNVERIFIED | Initial/editor checks passed; one-hour hiding helper expired during long sweeps. Only test instances were stopped. Focused VST3 lifecycle also stopped by hidden-window guard. |
| Q11 | OpenGrid host matrix | FAIL | VST3: 1,312 min/max rows and patterned recall PASS. AU: 1,311 min/max rows PASS; patterned recall has 12 differences. |
| Q12 | GUI theme/size matrix | FAIL | 45 rendered frames inspected: 3 themes × 5 page/view states × 3 sizes. Light theme values/controls lose contrast; some theme transitions retain stale label colours. Native interaction/scale/overlay matrix remains incomplete. |
| Q13 | Numeric display policy | FAIL | Q=40.00 has four digits; dynamics allow fractional dB steps; attack can display 0.1 ms. Existing user-requested ratio X.X:1 formatting is retained. |
| Q14 | I/O and meters | FAIL | Output meters calibrate as block peak/RMS; input section absent. Clip reset fails. Peak release depends on block size. |
| Q15 | Built/installed identity and VST3 class ID | PASS | SHA-256 matches for user-installed AU/VST3. Factory/scanner CID ABCDEF019182FAEB4F7065714F707131 is 32 hex digits. |
| Q16 | UI performance / opacity | FAIL | The editor fills its entire background without declaring opaque. The transformed stage also requires an opacity/bounds audit. Matched native before/after CPU and capacity tests are unverified; no performance improvement is claimed. |
| Q17 | Wiki freshness and HTML rebuild | PASS | Project facts updated; run tools/check.py and tools/build.py after final report generation. |

## Confirmed findings

### F1 — Current-schema dynamics state is rewritten on AU recall (high)

migrateLegacyDynamics uses panel enable values rather than checking whether modern panel parameters were present in the incoming state. A saved current state with dynamics on and both panels off is treated as legacy. In the focused AU test Below On changes 0→1, Below Range 6→−12 dB and Below Ratio 50→100. VST3 host round-trips passed in this run, so the actual host failure is qualified as AU; the format-independent processor harness reproduces the same migration error.

Source: `Source/PluginProcessor.cpp:1677; Source/PluginProcessor.cpp:1715`. Evidence in the raw folder: `og9-au/parameter-matrix.json; reaper-focused-au/focused-report.json; meter-measurements.json`.

### F2 — Clip reset hit target and latch are broken (medium)

The drawn LED begins below y=34, but mouseDown only resets when y<34. Clicking the LED leaves it red. Clicking the header clears only the local flag; the processor latch remains true and the next timer sets red again. Synthetic component interaction and source inspection agree.

Source: `Source/UI/OutputColumn.h:73; Source/PluginProcessor.cpp:2558`. Evidence in the raw folder: `meter-measurements.json; clip-before.png; clip-led-click.png; clip-header-click.png; clip-next-timer.png`.

### F3 — Light theme makes numeric readouts and controls unreadable (medium)

Several readout plates and surfaces stay dark while text changes to the light theme's dark foreground. Some label colours are captured at construction and remain stale across a theme change. Screenshots simulate the same Theme::applyTheme/repaint path; they establish rendered contrast, not native menu operation.

Source: `Source/UI/Theme.h; Source/UI/CompactSlider.h; Source/UI/RotaryKnob.h; Source/UI/SectionPanel.h`. Evidence in the raw folder: `gui/theme1-tab1-view0-width1100.png; gui/theme1-tab1-view2-width1500.png; gui-matrix.json`.

### F4 — Five processing parameters have no GUI controls (medium)

autoGain, spectralAmount, spectralThresh, spectralFreq, spectralOct are in the host registry and processor but have no UI bindings. Separately, 96 legacy slope/range/action parameters are retained without dedicated controls; they are listed rather than silently excused.

Source: `Source/Parameters.h; Source/PluginEditor.cpp; Source/UI/OutputColumn.h`. Evidence in the raw folder: `static-coverage.json; parameter-ledger.json; static_coverage.py`.

### F5 — Output peak release changes with host block size (medium)

At 48 kHz, a 20 dB decay takes 25.33 ms at 32-sample blocks versus 810.67 ms at 1,024 samples. The backend multiplies by 0.94 once per block. GUI smoothing may impose a slower display floor, but it does not make the backend ballistics time-invariant. RMS matches the mathematical current-block RMS, with no fixed integration window.

Source: `Source/PluginProcessor.cpp:2556`. Evidence in the raw folder: `meter-measurements.json; meter_probe.cpp`.

### F6 — Value and step policies are not consistently met (low)

Examples include Q 40.00, attack 0.1 ms, and a −17.5 dB threshold. These conflict with default wiki/QA policies. Fractional ratio formatting X.X:1 is an existing user request and must not be changed to satisfy a generic policy without considering that preference.

Source: `Source/UI/ValueFormat.h; Source/Parameters.h`. Evidence in the raw folder: `meter-measurements.json displayExamples`.

### F7 — The beta label is hidden in the plugin UI (medium)

The version macro and README retain 0.3.0-beta, but the editor puts the version label at zero size. Repository beta marking is present; visible in-plugin beta marking is absent.

Source: `Source/PluginEditor.cpp: subtitleLabel.setBounds(0, 0, 0, 0)`. Evidence in the raw folder: `Source/Version.h; all gui/ frames`.

### F8 — Input section missing under the canonical QA rule (low)

The GUI has a complete output column but no dedicated input level/trim section. This fails the skill's input/output-section requirement; it is not a claim that the audio input bus is broken.

Source: `Source/UI/OutputColumn.h; Source/PluginEditor.cpp`. Evidence in the raw folder: `gui/ frames; parameter registry`.

### F9 — Full background painters do not declare opacity (low)

The editor fills its full background but omits setOpaque(true). The stage paints logical bounds while its component bounds are transformed, so its opacity flag needs a separate bounds check rather than being set blindly. No measured CPU penalty is claimed. Rounded cards and translucent overlays must remain non-opaque.

Source: `Source/PluginEditor.h: EditorStage; Source/PluginEditor.cpp: paint / paintStage`. Evidence in the raw folder: `Source inspection`.

## Tool failures and evidence limits

- The original Debug run crashed in `___chkstk_darwin` at `checkRoadmap`. Disassembly shows a frame of roughly 34 MB against a 32 MiB executable stack. CMake now gives only the Debug regression executable 64 MiB. Release stays at 32 MiB; no DSP, assertion or tolerance was changed. Debug then passed in 93.25 seconds; Release passed in 72.97 seconds.
- AU pluginval's original 30-second watchdog expired inside its embedded auval subprocess. Standalone auval passed, and repeating the same strictness-10 workload with a 180-second watchdog passed. Both logs are retained. Validator GUI tests were explicitly skipped; no blanket GUI-validator pass is claimed.
- REAPER ran all 1,315 VST3 primary parameter rows and all 1,314 AU primary rows. Initial flat/default states made most controls appear DEAD. AU retried all 1,285 suspects; VST3 retried 31 before interruption. These counts do not establish actual control coverage.
- **Confirmed audio-sweep false positives:** the AU sweep labels three `Shown` controls CONDITIONAL, but their min and max outputs null at −240 dB while both differ from the random-state reference by −8.77 dB. Thus that reference is stale or otherwise not comparable. A correct sweep needs a stable repeated reference in each context before classifying a parameter. The generic “every parameter changes the sound” PASS is not accepted as evidence.
- **Confirmed visual-sweep false positives:** its one starting EQ frame is compared against later Dynamics pages. Off-page controls can inherit a PASS because the page or meters already changed. Static bindings and actual page screenshots are required; 1,315 automatic visual passes do not imply 1,315 visible controls.
- The one-hour hiding helpers expired during the long host sweeps. Test windows were detected onscreen; only those isolated instances were stopped and cleaned up. This interrupted VST3 conditional retries and the original editor-lifecycle stage. Focused runs used a three-hour helper: AU repeated the state failure and passed five hidden editor reopen cycles; VST3 state recall passed but its editor check was stopped by the visibility guard. No user DAW session was terminated.
- OpenGrid originally lacked cache entries for OpenEQ. Targeted scans of its user-installed AU/VST3 bundles resolved discovery. The VST3 factory scanner reports the same 32-hex class ID as moduleinfo.
- Gallery review covers EQ, three Dynamics views and Split, three themes, and 1,100×640 / 1,500×860 / 1,900×1,200. These are rendered component snapshots. Layout remains within the inspected frame sizes, but the light-theme contrast failure prevents a GUI pass. Native popup operation, all scale settings and every band/control interaction are not fully accepted.
- No VU/PPM standard is claimed by these meters. Peak/RMS mathematical calibration, block dependence and synthetic clip-reset interaction were measured; this is not a certification of standardized ballistics.
- Matched old/new native CPU profiling, full DAW capacity, blind listening and exhaustive target-host interaction remain unverified. AAX/CLAP/LV2 are not built here; signing, packaging and distribution were outside scope.

## Repair priorities

1. Preserve current-schema panel enable/range/ratio values on AU state restore; migrate only missing legacy fields. Add the both-panels-off current-state regression.
2. Correct the clip LED hit rectangle and clear the processor latch along with the display latch.
3. Make light-theme text/readout surfaces coherent and refresh cached widget colours on theme changes.
4. Add deliberate GUI access for the five exposed processing parameters, and document compatibility-only host parameters.
5. Make meter decay depend on elapsed samples/time, expose the beta label visibly, and address the remaining policy/opacity gaps.
6. Correct the host-sweep reference validation and page-aware GUI test before treating their blanket PASS results as acceptance.
