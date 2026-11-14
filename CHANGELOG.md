# Changelog

## 1.0 Beta

First numbered beta release (numeric version 1.0.0). Builds on the 0.3.0-beta source.

- Transient/tonal split is a two-stream engine; each band has its own gain per stream. About 37 ms of latency at 48 kHz while any band is split (Zero Latency mode only).
- Resonance suppressor on the RESO field: compares each frequency with its smoothed surroundings and removes only the excess, independent of input level. About 43 ms of latency at 48 kHz while on.
- New analyzer views: POST, PRE, SIDE, WATER, SPECTRO and SPEC 3D.
- Input Trim, shared input/output level meter with peak hold, and a grouped output section.
- Faster band processing chain, split medians, analyzer rings, meters, auto gain and convolution.
- Fixed a crash when a linked instance outlived the instance it was linked to.
- The release label now reads "1.0 Beta".

Known issues are listed in [RELEASE_NOTES.md](RELEASE_NOTES.md).

## 0.3.0-beta

Initial public source.
