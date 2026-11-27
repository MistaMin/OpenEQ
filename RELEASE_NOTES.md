# OpenEQ 1.0.3 Beta

A parametric and dynamic equalizer: 32 bands, per-band routing, above/below dynamics, a transient/tonal split, a resonance suppressor, and zero-latency, linear-phase, analogue and mixed-phase processing modes.

This is a beta. Save your sessions and report reproducible problems with the host name and version, sample rate, buffer size and the steps that reproduce it.

## Formats and requirements

- macOS 15 or later, universal arm64 + x86_64: Audio Unit, VST3, CLAP, LV2 and AAX (Pro Tools) in a signed and notarized installer package. The AAX is signed for Pro Tools.
- Windows 10 or later, x64: VST3, CLAP and LV2 in an installer. It is not code-signed, so Windows may warn before running it.
- Linux x86_64, built on Ubuntu 22.04: VST3, CLAP and LV2 in a `.tar.gz` with `usr/lib` folders.
- The Standalone application is built from source but not shipped in this release (it has no application icon yet).

## What is in this release

- 1.0.3 fix: VST3 and LV2 no longer rebuild the linear-phase filter on almost every audio block.
- 1.0.3 fixes: Digital+ Steps and 2x Oversampling now take effect while playing and after reopening a session. Switching 2x leaves a short gap in the audio.
- 1.0.3 fixes to preset import, preset saving, undo after loading, and the offline render oversampling setting.
- 1.0.3: CLAP and LV2 formats, and Windows and Linux builds.
- 1.0.3: the LV2 sends restored values to the host in small groups after loading, so the host shows them.
- 1.0.3 fix: with Output Gain away from 0 dB, the gain was also applied to the sidechain input buffer.
- 1.0.2: the editor uses much less CPU while nothing is changing (Standalone, idle, no signal: about 35% of one core down to about 9%; with audio playing the saving is much smaller). The selected-band glow is steady.
- 1.0.1: DRIVE now reads 0 to 100%, the top bar follows the theme and reads clearly in Graphite, Light and High contrast, and an AAX build is added.

- Transient/tonal split with per-band stream gains (about 37 ms latency at 48 kHz while a band is split; Zero Latency mode only).
- Resonance suppressor (about 43 ms latency at 48 kHz while on).
- Six analyzer views, Input Trim, shared input/output meter with peak hold.
- Performance work on the band chain, analyzer, meters, auto gain and convolution.
- The INST button lists the instances in this host process by track name and lets this window edit any of them. Hosts that run each plug-in in its own process cannot show other instances.
- "Rename ..." in that menu names an instance yourself (saved with the session), for hosts that do not report track names.
- A damaged preset file now reports failure and changes nothing.

## Checks this release passed

- macOS: regression suite 59 of 59 (Release); `auval`; VST3 and Audio Unit validators at strictness 10; CLAP validator (two rounding tests excluded, see Known issues); AAX validator 5 of 6.
- macOS host: the CLAP loads, renders, keeps its values over save and reopen and survives editor open/close cycles. The LV2 loads and renders, and after reopening a project that set every parameter, the audio and the saved values match and the host shows the restored values.
- Windows and Linux: VST3 validator at strictness 10 and the CLAP validator, on automated build machines. No host testing.
- Not repeated for 1.0.3: full parameter sweeps, the on-screen visual sweep and the instance menu tests in a host.

See the changelog for the full list. Nobody has yet listened to the newer audio paths in a long session.

## Known issues

- Windows and Linux: new builds, checked with automated validators only; not tried in a host, and the regression suite does not run there.
- CLAP: a restored session or typed value can differ from the saved one in the last digits on some frequency controls.
- LV2: renders of an unchanged project can differ from each other, by up to about -44 dB in testing; the cause is not yet known. VST3 renders were identical.
- Settings and theme changes can take up to a second to show on the graph.
- Sessions saved by builds from before the public 0.3.0-beta source can be misread by hosts that identify parameters by position (the band parameter block grew during development). Sessions from 0.3.0-beta load unchanged.
- A suppressor threshold saved by an older version is read as a floor and can leave the suppressor silent until the Floor control is reset.
- Impulse Padding and Dyn Knee (with the Above/Below panels) have no effect while playing.
- A host that switches to offline rendering on its audio thread gets the render oversampling setting at its next restart of playback.
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

Run `OpenEQ-1.0.3-macos.pkg` and choose the formats you want. They install to `/Library/Audio/Plug-Ins/Components`, `/Library/Audio/Plug-Ins/VST3` and `/Library/Application Support/Avid/Audio/Plug-Ins`.
