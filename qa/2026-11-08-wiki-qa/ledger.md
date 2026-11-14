# OpenEQ QA scope ledger — 2026-11-08

Baseline: 21ff8a7; 0.3.0-beta. Canonical process: plugin-qa and the engineering wiki. Result: **FAIL**. Scope: 17 groups, 8 passed, 7 failed, 2 unverified. The per-parameter ledger is retained with raw evidence outside this public repository.

| ID | Requirement | Status | Evidence / reason |
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

See report.md for individual findings, source locations, reproducible measurements, limitations, original failures and repaired test-runner evidence. No failed gate was excused to make the result pass. Ratio X.X:1 formatting remains an explicit user preference.
