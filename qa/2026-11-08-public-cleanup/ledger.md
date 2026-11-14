# OpenEQ public beta cleanup — 2026-11-08

Scope: public-source naming, documentation, UI text, historical research publication, and the beta label. No DSP algorithm or parameter ID changes are intended.

| Item | Code / GUI | Evidence | Status |
|---|---|---|---|
| Remove external EQ product/vendor names from the published tree | Source, tests, documentation, filenames, preset/theme UI | Full tracked-file path/text scan: zero audited name matches; VST3 string scan: zero audited name matches | Verified |
| Describe OpenEQ on its own | README.md, docs/FEATURE_COVERAGE.md, docs/FILTER_VALIDATION.md | Documentation review | Verified |
| Exclude historical comparison research from publication | Historical reports archived outside the repository; old repository retained as a local bundle | Public tree inventory | Verified |
| Preserve the beta label | Source/Version.h, editor subtitle, README.md; numeric CMake version remains 0.3.0 | Visible version macro remains 0.3.0-beta; repository description marks beta software | Verified |
| Preserve processing and preset behavior | Renamed filter/preset implementation; JSON import and theme loading remain content-based; file choosers allow all files for compatibility | Numeric constants unchanged across 13 engine/parameter/import files; all 40 per-band parameter ID suffixes unchanged; full regression target passes | Verified |
| Preserve import test coverage without an external installation | Tests/fixtures/import-bass.json, Tests/RegressionTests.cpp | Synthetic bass fixture now runs unconditionally; normalized import checks pass | Verified |
| Preserve UI catalogue naming protection | third_party/GoodLookinUI/tests/core.cpp | Exact approved-label catalogue replaces a vendor-name blacklist; standalone catalogue/roundtrip test passes | Verified |

Checks:

- Release AU, VST3, and Standalone builds: PASS.
- Full regression target (41 sections): PASS, 89.69 seconds. See regression.log.
- AU format validation: PASS. See auval.log.
- UI catalogue / invalid-input / roundtrip test: PASS.
- Whitespace checks and audited-name scans: PASS.
- Live GUI interaction and full DAW audio sweeps: not rerun for this publication cleanup. Preset/theme controls were inspected in source and exercised through regression tests.

Publication uses a clean root snapshot, retaining the beta version and current implementation. Previous Git history and original engineering records are backed up locally outside this repository.
