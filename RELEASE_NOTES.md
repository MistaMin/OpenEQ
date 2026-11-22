# OpenEQ 1.0.1 Beta

A parametric and dynamic equalizer: 32 bands, per-band routing, above/below dynamics, a transient/tonal split, a resonance suppressor, and zero-latency, linear-phase, analogue and mixed-phase processing modes.

This is a beta. Save your sessions and report reproducible problems with the host name and version, sample rate, buffer size and the steps that reproduce it.

## Formats and requirements

- Audio Unit, VST3 and AAX (Pro Tools) for macOS, universal arm64 + x86_64, in a signed and notarized installer package. The AAX is signed for Pro Tools.
- The Standalone application is built from source but not shipped in this release (it has no application icon yet).
- No CLAP or LV2 build, and no Windows or Linux build, in this release.

## What is in this release

- 1.0.1: DRIVE now reads 0 to 100%, the top bar follows the theme and reads clearly in Graphite, Light and High contrast, and an AAX build is added.

- Transient/tonal split with per-band stream gains (about 37 ms latency at 48 kHz while a band is split; Zero Latency mode only).
- Resonance suppressor (about 43 ms latency at 48 kHz while on).
- Six analyzer views, Input Trim, shared input/output meter with peak hold.
- Performance work on the band chain, analyzer, meters, auto gain and convolution.
- The INST button lists the instances in this host process by track name and lets this window edit any of them. Hosts that run each plug-in in its own process cannot show other instances.
- "Rename ..." in that menu names an instance yourself (saved with the session), for hosts that do not report track names.
- A damaged preset file now reports failure and changes nothing.

## Checks this release passed

- Regression suite: 58 of 58, exit 0, repeated runs in Release and one in Debug.
- Audio Unit validation (`auval`).
- VST3 and Audio Unit validators at strictness 10, including their editor tests.

- Host checks on this build: load, render, state save and reopen, 10 editor open/close cycles, and every parameter set with the editor open, in two hosts, with the Audio Unit and the VST3. In one host the instance menu, switching to another track's instance and the Rename dialog were driven and worked, and a rename survived saving the project.

See the changelog for the full list. Nobody has yet listened to the newer audio paths in a long session.

## Known issues

- Sessions saved by builds from before the public 0.3.0-beta source can be misread by hosts that identify parameters by position (the band parameter block grew during development). Sessions from 0.3.0-beta load unchanged.
- A suppressor threshold saved by an older version is read as a floor and can leave the suppressor silent until the Floor control is reset.
- Impulse Padding, Dyn Knee (with the Above/Below panels) and the 2x Oversampling control have no effect while playing; oversampling is read only when playback starts.
- With host double precision, the external sidechain is not used.
- Auto Gain can step by several dB within a few samples when a band gain changes.
- The WATER and SPEC 3D views can take longer to draw than the display update interval at large window sizes.
- With split or lookahead on, the suppressor's sidechain key runs slightly early.
- DRIVE is gentle: at 100% a -20 dBFS sine is about 2 dB lower with mild saturation.
- Pro Tools: tested with one mono track only (insert, editor, output, bypass, save and reopen, reset). Automation, bounce, stereo tracks, other sample rates and GUI scales were not tested. One reopen restored OUTPUT at -24 dB and was not reproduced.
- The AAX has no page tables (the AAX validator reports that test as failed); control surfaces map parameters by default order.
- Some tooltips do not match the control ranges; default Output Gain can read "-0.0".
- The RMS and input level bars depend on the host buffer size.
- Automated validators can time out under heavy machine load; raise their time limit.
- The mirror (link) part of the instance menu, other hosts' track-name behaviour, native popups with a real mouse, the Standalone launch and old projects in a real host were not covered by testing. Where a host sends no track names, use Rename.

## Installing

Run `OpenEQ-1.0.1-macos.pkg` and choose the formats you want. They install to `/Library/Audio/Plug-Ins/Components`, `/Library/Audio/Plug-Ins/VST3` and `/Library/Application Support/Avid/Audio/Plug-Ins`.
