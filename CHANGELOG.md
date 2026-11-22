# Changelog

## 1.0.1 Beta

- The DRIVE readout showed 1% at full drive; it now reads 0 to 100%, and typing 100 or 100% sets full drive.
- The top bar, buttons and graph labels follow the theme and read clearly in Graphite, Light and High contrast.
- Adds an AAX build for Pro Tools, installed with the new installer. No audio or parameter changes.
- Drive is gentle by design: at full drive a -20 dBFS sine comes out about 2 dB lower with mild saturation.

## 1.0 Beta

First numbered beta release (numeric version 1.0.0). Builds on the 0.3.0-beta source.

- Instance menu: the INST button lists every OpenEQ instance in this host process by its track name (with the track colour when the host gives one; "Instance N" when it gives none, and " (2)" for duplicates). Choosing one makes this window edit that instance; choosing this plug-in's own row switches back. If the edited instance is closed, the window returns to its own instance. The header badge shows the name of the instance being edited. Hosts that run each plug-in in its own process cannot show other instances.
- "Rename ..." in the instance menu gives the edited instance a name of your own. It wins over the track name, is saved with the session (not in presets or A/B slots), and helps in hosts that do not report track names. Unnamed instances read "Instance <id>", and every listed name is unique.
- The two-way parameter mirror moved to a submenu of the instance menu. Changes from host automation are now written to the other instance by the message thread; the audio thread no longer touches the other instance or tears the link down.
- Loading a damaged preset file now reports failure and changes nothing; a damaged saved default is ignored.
- The settings file is guarded by an inter-process lock, so two host processes do not overwrite each other's settings.
- Build: the measurement probes (`response_probe`, `response_compare`, `plugin_probe`) now live in `Tools/` and are built only with `-DOPENEQ_BUILD_PROBES=ON`.
- Tests: the regression suite keeps its settings and presets in a temporary folder and no longer needs an enlarged stack.
- Transient/tonal split is a two-stream engine; each band has its own gain per stream. About 37 ms of latency at 48 kHz while any band is split (Zero Latency mode only).
- Resonance suppressor on the RESO field: compares each frequency with its smoothed surroundings and removes only the excess, independent of input level. About 43 ms of latency at 48 kHz while on.
- New analyzer views: POST, PRE, SIDE, WATER, SPECTRO and SPEC 3D.
- Input Trim, shared input/output level meter with peak hold, and a grouped output section.
- Faster band processing chain, split medians, analyzer rings, meters, auto gain and convolution.
- Fixed a crash when a linked instance outlived the instance it was linked to, and a link that could be taken over by a new instance created at the same memory address.
- Fixed a crash when clicking the DYNAMICS key with the editor open.
- Selecting another band no longer switches off the previous band's dynamics settings.
- The release label now reads "1.0.1 Beta".

Known issues are listed in [RELEASE_NOTES.md](RELEASE_NOTES.md).

## 0.3.0-beta

Initial public source.
