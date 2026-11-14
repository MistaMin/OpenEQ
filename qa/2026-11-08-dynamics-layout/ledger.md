# Dynamics layout scope — 2026-11-08

Source: user approved the dynamics design assessment. Preserve processing, parameter IDs and all existing controls; retain 0.3.0-beta. Local commit is authorized; push is outside this request.

| ID | Item | Code / GUI | Verification | Status |
|---|---|---|---|---|
| S1 | Short Level / Timing / Detector navigation | DynamicsPanel | All views, screenshots | Verified: see report.md |
| S2 | Below/Above switches in headers; aligned threshold/range/ratio sliders | Level | Visual and binding check | Verified: see report.md |
| S3 | Attack/Release aligned; Auto and Global lookahead switches in headers | Timing | Visual and binding check | Verified: see report.md |
| S4 | Source row; Link/Listen/Free in headers; frequency/Q/relative knobs in grid | Detector | Linked/unlinked and free states | Verified: see report.md |
| S5 | One dynamics readout in inspector header | PluginEditor / DynamicsPanel | Screenshot | Verified: see report.md |
| S6 | Dynamics enable directly in inspector | All dynamics views | Toggle and parameter readback | Verified: see report.md |
| S7 | Keep every existing dynamics parameter and audition control | Parameter attachments | Binding inventory, interaction | Verified: see report.md |
| S8 | Readable at GUI scaling and window limits; EQ/Split tabs retained | Floating inspector | Default/minimum live review; 150% renders | Partially verified; exhaustive theme/scale sweep not repeated |
| S9 | Release builds and regressions | AU/VST3/Standalone | Build, regression and AU validator | Verified: see report.md |

Full DSP redesign, toolbar redesign, output-column redesign, and whole-plugin skin redesign are out of scope.
