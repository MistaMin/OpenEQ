# OpenEQ beta feature coverage

Version **1.0 Beta**. This inventory describes implemented areas; beta validation remains ongoing.

| Area | Implementation | Validation |
|---|---|---|
| Bands and graph | 32 bands, filter families, per-band response, routing, selection and editing | Regression sections for controls, filters, graph painting, band operations, routing, and sliders |
| Dynamics | Above/below processing, relative detection, sidechain, timing, lookahead | Dynamics, detector, lookahead, and state regression sections |
| Phase and precision | Direct filters, FIR modes, windowing, free phase, oversampling, offline quality | Phase, linear-phase crossfade, oversampling, and precision regression sections |
| Transient/tonal split | Global separation and independent per-band gains | Split-mode reconstruction, isolation, and state regression section |
| Presets and settings | Native presets, normalized JSON import, themes, undo/redo, A/B | Preset, import, theme, settings, and state regression sections |
| Cross-instance control | Shared process registry and parameter relay | Registry and relay regression section; process-isolated instances cannot share the registry |
| Inspector | Movable/collapsible EQ, dynamics, and split controls | Existing UI review and slider regression; full host interaction coverage remains ongoing |

Host discovery, editor lifecycle, automation, session recall, sidechain routing, delay compensation, and offline rendering require continued verification in supported hosts. See [filter validation](FILTER_VALIDATION.md) for the scope of the numerical baselines.
