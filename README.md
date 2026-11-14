# OpenEQ

Version **0.3.0-beta** — beta software under active development.

OpenEQ is a parametric and dynamic audio equalizer built with C++20, JUCE, and GoodLookinUI. It builds as VST3, Audio Unit, and a standalone application.

## Features

- 32 bands with bell, shelf, cut, notch, band-pass, tilt, all-pass, and character filter shapes.
- Per-band stereo, left, right, mid, side, and mid/side placement, with solo and pan controls.
- Above/below dynamic EQ with configurable detection, sidechain input, relative detection, timing, and lookahead.
- Transient/tonal splitting: the audio is separated into a transient stream and a tonal stream that add back to the input, and each band has its own gain per stream. Adds about 37 ms of latency while any band is split; Zero Latency mode only.
- Resonance suppressor: turns down only what stands out from its surroundings in the spectrum, so ringing peaks are pulled back while tilt and broad tone are left alone. Independent of input level. Depth, sharpness, selectivity, attack, release, region, stereo link, mix, hard mode and a delta listen. Adds about 43 ms of latency at 48 kHz while on.
- Zero-latency, linear-phase, analogue, mixed-phase, and analogue zero-latency processing modes, with configurable FIR windows and offline render quality.
- Interactive response graph, pre/post/sidechain spectrum analysis, waterfall display, and note keyboard.
- Movable, collapsible inspector with EQ, dynamics, split and suppressor tabs.
- Band management, presets, JSON preset import, theme files, undo/redo, A/B comparison, and access to other OpenEQ instances in the same host process.
- Output gain, width, balance, side trim, oversampling, and host automation/state restoration.

## Beta status

This is a development beta. Validation is ongoing across hosts, sample rates, routing configurations, and processing modes. Save your sessions and report reproducible problems with the host version, sample rate, buffer size, and reproduction steps.

Linear-phase processing omits dynamic and all-pass band contributions. Changing FIR quality resets the overlap and may produce a discontinuity. Hosts that isolate plugins into separate processes cannot share the cross-instance registry. See [feature coverage](docs/FEATURE_COVERAGE.md) and [filter validation notes](docs/FILTER_VALIDATION.md) for engineering details and remaining checks.

## Building

Requires CMake 3.22+ and a C++20 compiler. CMake fetches JUCE 8.0.6 automatically.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
ctest --test-dir build --output-on-failure
```

Build outputs are in `build/OpenEQ_artefacts/Release/{VST3,AU,Standalone}/`. Plugin copying after build is enabled; the configured JUCE destinations receive the AU and VST3 bundles.

Optional measurement tools are `response_probe` and `response_compare`. The probe requires an explicit `--plugin <VST3 path>` argument.

## Licensing

OpenEQ's own source code is available under the [MIT License](LICENSE). JUCE 8.0.6 is separately licensed under AGPLv3 or a JUCE licence; users building or distributing OpenEQ must comply with the JUCE licence applicable to them. OpenGrid uses a commercial JUCE licence. GoodLookinUI retains its MIT licence and copyright notices.
