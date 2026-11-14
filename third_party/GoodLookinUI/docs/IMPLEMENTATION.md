# Implementation plan

## Product requirements

Audience: developers of audio utilities and effects, plus non-programming interface testers. The first adopter was a plugin that lives in its own repository. A rack/console aesthetic combines generated hardware surfaces with modern analysis displays. Instruments and synths are outside current scope.

Controls and panels must be generated in code without requiring designer-produced image assets. The finished toolkit needs realistic knob, switch, illumination, menu, panel and meter animations. Parameter changes must reach DSP immediately even when their graphical representation is animated.

Development builds contain an in-plugin editor. Testers can eventually add, swap, place and colour controls, edit labels and menu presentation, and select bindings from existing parameters. Developers implement new DSP behaviours and menu values. Host automation IDs and stored DSP enum values must remain stable through presentation changes.

Release users can scale the entire interface. They cannot edit typography or layouts. The editor must be removed at compile time. Finished designs are readable, itemised source files using ordinary property names and hexadecimal colours, embedded in the plugin during its build.

The core library must not depend on JUCE. The initial rendering/input and parameter adapters use unmodified JUCE. Future frameworks implement adapter interfaces. Clang, GCC and MSVC are build targets, not GUI backends.

Target systems: Apple Silicon/macOS, Windows 11, Ubuntu Studio and Arch Linux. Plugin wrappers: AU, VST3, LV2 and AAX as applicable to platform. These wrappers remain the plugin project's responsibility rather than the UI toolkit's. Future signal-chain rearrangement needs explicit DSP routing support and is separate from cosmetic layout changes.

## 0.1 — current foundation

Core design model, CSV validation, motion, procedural knob renderer, knob inspector and a host-plugin adapter. Existing EQ display and other controls retained. Compile-time editor removal and baked design support.

Verified on Apple Silicon/macOS: linked Debug/Release standalone, VST3 and AU builds; standalone launch and initial visual inspection; live label/colour edits, undo, CSV save/load; editor absent from the Release UI. Acceptance remaining: visual resize checks at 860×620, 980×760 and 1600×1100, audio/automation checks in a DAW, and Windows/Linux builds.

## 0.2 — editor and generated control library

Split the adapter into rendering, control and editing modules. Add a reusable parameter registry, safe attachment lifetime management, parameter units/defaults and compatible control choices. Implement control palette, drag/drop, selection overlays, snapping, alignment, grouping, redo and atomic saving. Add generated faders, toggle/rocker switches, pushbuttons, dropdowns, panel materials and shared lighting. Expand the design schema for panels, component types and stable menu choice IDs.

## 0.3 — analysis and animation

Audio-to-UI snapshot interfaces with bounded updates; FFT/response displays, peak/RMS meters, calibrated VU motion and compressor curve/knee display. Cache static materials at scale and redraw only dirty regions. Define animation time from elapsed time, idle suspension, and deterministic meter calibration. Add keyboard interaction and accessibility metadata.

## 0.4 — portability and release qualification

Platform-specific plugin format selection, optional AAX SDK path, LV2 build integration, CI builds on macOS/Windows/Linux, standalone control gallery, render comparisons, multiple-instance performance profiling and DAW smoke tests. The source and documentation are published under the MIT License.

## Later

A second plugin validates reuse. Add a second GUI backend when needed. Signal-chain editing requires a routing model, valid connection rules, parameter/state migration and DSP integration before it can affect audio.
