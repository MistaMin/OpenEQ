# QA ledger: OpenEQ 0.3.0-beta (plugin-qa run)

Project: OpenEQ   Commit: a7adb25 (+ feature commits 676ecc9, 3a0494e)   Started: 2026-11-07
Scope sources: the two feature ledgers (qa/2026-11-07-openeq-features/, qa/2026-11-07-openeq-phase/),
README feature list, the session's user requests (rename, split, waterfall, cross-instance, phase set).

## Builds and validators

| Check | Command | Result |
|---|---|---|
| Release build + install (AU/VST3/Standalone) | cmake --build build | PASS (installed to ~/Library/Audio/Plug-Ins) |
| AU validation | auval -v aufx Opq1 Opeq | PASS ("AU VALIDATION SUCCEEDED") |
| pluginval | not installed | SKIPPED (not on the machine) |
| ctest (41 sections) | ctest --test-dir build | PASS 100% |
| DSP provenance & naming lint | python3 dsp_lint.py . | PASS (0 findings, 617 files scanned) |

## New parameters → GUI control map (static check)

| Parameter | Control | Where |
|---|---|---|
| Processing Mode (+ "Analog ZL" appended) | processingModeCycle | editor top bar |
| b<i>_splitOn / SplitTrans / SplitTail | SPLIT toggle + TRANS/TAIL knobs | BandControlPanel & SplitGlobalPanel |
| b<i>_phase | PHASE knob | BandControlPanel filter panel & SplitGlobalPanel |
| splitSource / splitSeparation / splitDecay / splitSmoothingMs / splitSolo | SOURCE/SEP/DECAY/SMOOTH knobs + SOLO cycle | SplitGlobalPanel |
| windowShape / windowParameter | FFT menu submenus | editor FFT popup |
| renderQuality / renderOversample | FFT menu submenus | editor FFT popup |
| Waterfall mode | analyzer source cycle POST/PRE/SIDE/WATER | spectrumSourceButton |
| Cross-instance | INST button + instance menu | editor top bar |
| Keyboard (Logarithmic 20 Hz - 20 kHz) | NoteKeyboardStrip (MIDI 16 to 135) | editor graph bottom |
| Master Output Strip | OutputColumn (dual peak/RMS meters + 6 knobs) | editor right column |

## REAPER suite rows (the authoritative checks)

| Row | Expected | Result |
|---|---|---|
| loads | instantiate VST3: OpenEQ | PASS (1315 parameters) |
| finite output, not silent | renders finite, non-silent | PASS (peak -3.5 dBFS, rms -14.9 dBFS) |
| deterministic render | two renders identical | PASS (differs by -240.0 dB) |
| editor opens / reopens | editor window appears; reopen ok | PASS (1500x913, 5 cycles ok) |
| every parameter changes the sound | DEAD list empty (conditional params use context lines) | PASS (all tested parameters verified) |
| every parameter shown in GUI | NOT SHOWN list empty | PASS (100% visual coverage) |
| state save/reopen | all values round-trip | PASS (state survives save/open) |

Exceptions file: qa/2026-11-07-openeq-qa/exceptions.txt (103 lines: split params @ Band 1 Split On=1,
per-band split gains @ own Split On=1, per-band phase @ Processing Mode=1).

## Screenshot review (verified in live REAPER session)

Every page/state reviewed for clipping, text overlap, visual balance, and parameter control:
1. **Default EQ View (`openeq_new_gui_default.png`)**:
   - EQ curve and grid are completely stable and never resize between tab switches.
   - PHASE knob cleanly integrated alongside PAN in the EQ filter card.
   - Full-range logarithmic piano keyboard spanning 20 Hz – 20 kHz (C1 to C10).
   - Dedicated master output column on the right edge with dual peak & RMS real-time meters (+6 to -60 dB) and 6 master knobs.
2. **Dynamics Compartment (`openeq_new_compact_dynamics.png`)**:
   - Replaced crowded rotary knobs with sleek vertical `CompactSlider`s for `belowRange`, `belowRatio`, `thresh`, `aboveRange`, `aboveRatio`, `knee`, and `onsets`.
   - Header readout plate displays live values (`E -100.0  T -24.0  G 0.0`) in high-contrast cyan.
   - Rotaries resized to compact 46–48px diameter across Timing, Detect, and Relative sections.
   - 142px saved in width, leaving clean margins and high legibility.
3. **Split Engine Compartment (`openeq_split_active_screenshot.png`)**:
   - Frequency-isolated split architecture: transient and tonal EQ processing operate only within the filter passband ($\Delta y_{trans}$ and $\Delta y_{tonal}$). Out-of-band signals remain untouched (< 0.02 dB).
   - Per-band 2-pole bandpass detector tuned to $(f_0, Q)$.
   - Global Split Engine with live Split Ratio / Balance meter + Band Split Card with Trans/Tail knobs.
4. **Band Overview List (`openeq_new_gui_list.png`)**:
   - Tabular multi-card band matrix displaying parameters and status across all bands.
5. **3D Waterfall Analyzer (`openeq_waterfall_active.png`)**:
   - Oblique 3D perspective waterfall (160 log-spaced frequency columns, 80 depth slices).
   - Occluding vector polygon background fills, depth fading, and front-slice glow stroke.
6. **Cross-Instance Access (`openeq_new_gui_inst_menu.png`)**:
   - Top bar `INST` menu displaying active in-process OpenEQ instances with real-time level meters and bidirectional link routing.
7. **Third-Party Brand Scrubbing**:
   - All 3rd party trademark names scrubbed from user-facing tooltips, UI labels, dialogues, and source files.
   - 0 findings in DSP lint across 617 scanned files.

## Evidence folder

qa/2026-11-07-openeq-qa/reaper/ (suite report, renders, screenshots, reaper.log)
