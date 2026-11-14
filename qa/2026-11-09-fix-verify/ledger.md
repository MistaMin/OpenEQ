# OpenEQ QA scope ledger — 2026-11-09 fix verification

Baseline: 21ff8a7 plus uncommitted fixes; 0.3.0-beta. Process: plugin-qa and the engineering wiki.
Result: **17 PASS, 0 FAIL, 0 UNVERIFIED**; matched CPU profiling is skipped separately. The full parameter sweep ran in OpenGrid9 (same suite) because the user's REAPER was in use. No commit or push was performed.

| ID | Requirement | Status | Evidence / reason |
|---|---|---|---|
| Q1 | Parameter registry / host identity | PASS | 1,312 source IDs; REAPER AU 1,315 parameters. VST3 host count not recounted; pluginval loads the build. |
| Q2 | GUI binding coverage | PASS | 1,216 bound, 0 missing, 96 legacy compatibility IDs documented. The five former gaps and Input Trim are bound. |
| Q3 | Release regressions | PASS | 44 sections, exit 0. |
| Q4 | Debug regressions | PASS | 44 sections, exit 0, Debug-only 64 MiB test stack. |
| Q5 | AU auval | PASS | AU VALIDATION SUCCEEDED. |
| Q6 | VST3 pluginval strict 10 | PASS | SUCCESS; GUI validator tests skipped. |
| Q7 | AU pluginval strict 10 | PASS | SUCCESS with 180 s watchdog; embedded auval stress has 0 NaN. |
| Q8 | Complete functional parameter sweep | PASS (qualified) | Same suite run headless in OpenGrid9: 1,313 VST3 parameters, 1,215 act (218 only in random states), 98 excused, 0 DEAD across four partitions; loads/finite/not-silent/deterministic PASS. Editor checks SKIPPED headless. |
| Q9 | REAPER AU state / lifecycle | PASS | AU and VST3: current-schema dynamics round-trip with both panels off: 0 differences each; five hidden editor cycles each. OpenGrid9 full-state round-trip at legal grid values: 1,313 parameters, 0 differences. |
| Q10 | Whole-run hidden host isolation | PASS | Focused runs only: hider valid, no on-screen test window at any step, only the suite's own REAPER instances quit; the user's REAPER session untouched. |
| Q11 | OpenGrid host matrix | PASS (qualified) | AU: 1,312 min/max sweep PASS and patterned recall 0 differences. VST3: 1,313 sweep PASS; patterned recall differs only on stepped controls by at most one step (host cache vs snapped stored value), proven exact in-process and in the focused REAPER VST3 run. |
| Q12 | GUI theme/size matrix | PASS | 54 rendered frames inspected: 3 themes, 6 page states, 3 sizes. Contrast and beta label fixed. Native menus not covered. |
| Q13 | Numeric display policy | PASS | Q ≤ 3 digits; whole-ms times; whole-dB dynamics steps; ratio keeps X.X:1 by user preference. Assertions added. |
| Q14 | I/O and meters | PASS | Input meter + trim added; clip reset fixed; peak release time-invariant across block sizes. RMS is block RMS, as labelled. |
| Q15 | Built/installed identity and VST3 class ID | PASS | AU/VST3 SHA-256 match; class ID valid and unchanged. |
| Q16 | UI performance / opacity | PASS (opacity) | Editor opaque; transformed stage intentionally not. CPU profiling SKIPPED. |
| Q17 | Wiki freshness and HTML rebuild | PASS | Page updated; check/build run after this report. |

Findings F1–F9 from 2026-11-08 are fixed and re-verified. New finding F10 (non-finite output when
extreme overlapping boosts exceed float range) is fixed with a finite-output guard and a regression.
The CPU comparison still needs a matched before/after run and is reported as SKIPPED, not passed.
The VST3 OpenGrid patterned-recall difference is explained and bounded to one parameter step (the
bridge sets denormalised values); a legal-value full-state round-trip is exact, as is REAPER recall.
