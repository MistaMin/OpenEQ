# OpenEQ 1.0 Beta

A parametric and dynamic equalizer: 32 bands, per-band routing, above/below dynamics, a transient/tonal split, a resonance suppressor, and zero-latency, linear-phase, analogue and mixed-phase processing modes.

This is a beta. Save your sessions and report reproducible problems with the host name and version, sample rate, buffer size and the steps that reproduce it.

## Formats and requirements

- Audio Unit and VST3 for macOS, universal arm64 + x86_64, in a signed and notarized installer package.
- The Standalone application is built from source but not shipped in this release (it has no application icon yet).
- No AAX, CLAP or LV2 build, and no Windows or Linux build, in this release.

## What is in this release

- Transient/tonal split with per-band stream gains (about 37 ms latency at 48 kHz while a band is split; Zero Latency mode only).
- Resonance suppressor (about 43 ms latency at 48 kHz while on).
- Six analyzer views, Input Trim, shared input/output meter with peak hold.
- Performance work on the band chain, analyzer, meters, auto gain and convolution.

## Checks this release passed

- Regression suite: 46 of 46 sections, exit 0, 20 consecutive runs after the fix below.
- Audio Unit validation (`auval`).
- VST3 validator at strictness 10, including its editor tests.
- Audio Unit validator at strictness 10 with its editor tests skipped.

See the changelog for the full list. Nobody has yet listened to the newer audio paths in a long session, and several host and interface checks have not been run (see below).

## Known issues

- The Audio Unit validator's editor stress test crashes the plug-in (same family as the next item); the VST3 passes it.
- Clicking the DYNAMICS key on the DYN field can crash the host process.
- Selecting another band switches off the previous band's Dynamic On, Below On and Auto settings.
- Parameters added since 0.3.0-beta were inserted into the middle of the parameter list instead of at the end. Hosts that identify parameters by position (not by name) can misread automation in projects saved with the older version.
- A suppressor threshold saved by an older version is read as a floor and can leave the suppressor silent until the Floor control is reset.
- Impulse Padding, Dyn Knee (with the Above/Below panels) and the 2x Oversampling control have no effect while playing; oversampling is read only when playback starts.
- With host double precision, the external sidechain is not used.
- Auto Gain can step by several dB within a few samples when a band gain changes.
- The WATER and SPEC 3D views can take longer to draw than the display update interval at large window sizes.
- With split or lookahead on, the suppressor's sidechain key runs slightly early.
- Light theme: some value readouts, the notice line and graph labels are hard to read.
- Some tooltips do not match the control ranges; default Output Gain can read "-0.0".
- The RMS and input level bars depend on the host buffer size.
- Native menus, real-display mouse interaction, the Standalone launch and old projects in a real host were not covered by testing.

## Installing

Run `OpenEQ-1.0.0-macos.pkg` and choose the formats you want. They install to `/Library/Audio/Plug-Ins/Components` and `/Library/Audio/Plug-Ins/VST3`.
