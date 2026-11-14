# Dynamics layout review — 2026-11-08

Build: Release, AU/VST3/Standalone; working changes based on 9e640de. Version remains 0.3.0-beta.

The dynamics inspector uses explicit control rows and header switches. Level, Timing and Detector replace the longer navigation labels. A dynamics enable switch is available in every view, and the floating header owns the single live readout. The summary uses the applied dynamic threshold, including automatic threshold behavior. The inspector grows to 252 logical pixels high on Dynamics and retains the previous size on EQ/Split. Active-status dots now use explicit UTF-8 conversion.

All 25 original controls and 22 attachment mappings remain; two source selectors and temporary detector audition are retained. A new attachment exposes the existing per-band dynamics enable parameter. See bindings.md. Parameter definitions and DSP processing are unchanged.

## Evidence

- Release AU, VST3 and Standalone builds: PASS, with existing compiler warnings.
- Full regression target: PASS, 82.60 seconds; regression.log.
- Added card containment/non-overlap assertions cover all three views at the inspector's actual logical size. Existing threshold and applied-gain readout assertions are retained after moving the readout into the floating header.
- Final detector/layout regression and rendered previews: detector-check.log; level.png, timing.png, detector.png. Previews are offscreen renders at 150%, with synthetic example ratios of 2:1 and 4:1. They are distinct from the live interaction review.
- AU validator: PASS; auval.log contains the result excerpt.
- Live standalone review: all three views inspected at the default window size; Detector inspected at the minimum window size. Link and Free exposed the corresponding frequency/Q controls. Detector frequency changed independently to 500 Hz and relative frequency to 1 kHz, confirmed by accessibility values. Dynamics enabled through the EQ control was reflected in the new panel switch.
- No audio-thread processing, parameters, IDs, or timer rates were added or changed. The redundant per-panel readout update was removed.

## Limits

The full DAW audio/automation suite, exhaustive theme/scale combinations, and before/after CPU sampling were not repeated for this layout change. No CPU improvement is claimed. Live source-popup selection could not be verified reliably through the UI automation surface; its existing binding is unchanged. The live review window was left under the user's control when automation reported user interaction.
