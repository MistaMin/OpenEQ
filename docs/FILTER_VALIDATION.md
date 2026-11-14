# OpenEQ filter validation

These archived numerical baselines cover the JSON preset mapping and filter response at 48 kHz. They are historical measurements, rather than a claim of validation in every host or at every sample rate. The regression suite checks selected response points and continuous-slope behavior with unchanged tolerances.

`PresetRules::resolve` maps normalized preset values to filter type, Q, gain, and slope. `ResponseModel::designBand` produces the corresponding response. The retained table records maximum and RMS residuals down to the measurement floor of −150 dB. Setting A uses 1 kHz, +6 dB, Q 1.0; setting B uses 120 Hz, −9 dB, Q 0.7.

The generic filters use Butterworth-family prototypes with continuous order control and Nyquist-matched digitization (`Source/FilterDesign.h`). Character filters use analogue sections and fitted cascades (`Source/VintageFilters.h`, `Source/ResponseModel.h`).

| Slot | OpenEQ type | Setting | Max error (dB) | RMS error (dB) | Exact by rule |
|---|---|---|---|---|---|
| 0 | Low Pass | A | 0.01 | 0.004 | yes |
| 0 | Low Pass | B | 0.00 | 0.000 | yes |
| 1 | High Pass | A | 0.00 | 0.000 | yes |
| 1 | High Pass | B | 0.00 | 0.000 | yes |
| 2 | Band Pass | A | 1.90 | 0.393 | yes |
| 2 | Band Pass | B | 1.90 | 0.394 | yes |
| 3 | Bell | A | 0.00 | 0.001 | yes |
| 3 | Bell | B | 0.00 | 0.002 | yes |
| 4 | Low Shelf | A | 0.00 | 0.002 | yes |
| 4 | Low Shelf | B | 0.60 | 0.208 | yes |
| 5 | High Shelf | A | 0.00 | 0.001 | yes |
| 5 | High Shelf | B | 0.60 | 0.208 | yes |
| 6 | Notch | A | 0.01 | 0.001 | yes |
| 6 | Notch | B | 0.13 | 0.013 | yes |
| 7 | Tilt Shelf | A | 0.59 | 0.211 | yes |
| 7 | Tilt Shelf | B | 0.42 | 0.180 | yes |
| 8 | Flat Tilt | A | 0.24 | 0.043 | yes |
| 8 | Flat Tilt | B | 0.38 | 0.060 | yes |
| 9 | Flat Top | A | 0.53 | 0.170 | yes |
| 9 | Flat Top | B | 1.15 | 0.512 | no |
| 10 | Sword | A | 1.44 | 0.178 | yes |
| 10 | Sword | B | 1.93 | 0.306 | yes |
| 11 | E HP | A | 0.06 | 0.018 | yes |
| 11 | E HP | B | 0.08 | 0.029 | yes |
| 12 | E LS | A | 0.63 | 0.304 | yes |
| 12 | E LS | B | 1.28 | 0.598 | no |
| 13 | E Low Bell | A | 0.17 | 0.121 | yes |
| 13 | E Low Bell | B | 0.15 | 0.068 | yes |
| 14 | E LMF | A | 0.27 | 0.139 | yes |
| 14 | E LMF | B | 0.50 | 0.357 | yes |
| 15 | E HMF | A | 0.08 | 0.048 | yes |
| 15 | E HMF | B | 0.43 | 0.229 | yes |
| 16 | E High Bell | A | 0.21 | 0.118 | yes |
| 16 | E High Bell | B | 0.31 | 0.106 | yes |
| 17 | E HS | A | 0.26 | 0.113 | yes |
| 17 | E HS | B | 0.53 | 0.120 | yes |
| 18 | E LP | A | 0.12 | 0.036 | yes |
| 18 | E LP | B | 0.02 | 0.007 | yes |
| 19 | G HP | A | 0.00 | 0.000 | yes |
| 19 | G HP | B | 0.00 | 0.001 | yes |
| 20 | G LS | A | 0.22 | 0.097 | yes |
| 20 | G LS | B | 0.16 | 0.068 | yes |
| 21 | G LMF | A | 0.34 | 0.182 | yes |
| 21 | G LMF | B | 0.78 | 0.512 | no |
| 22 | G HMF | A | 0.21 | 0.114 | yes |
| 22 | G HMF | B | 0.38 | 0.198 | yes |
| 23 | G HS | A | 0.24 | 0.089 | yes |
| 23 | G HS | B | 0.14 | 0.056 | yes |
| 24 | G LP | A | 0.12 | 0.035 | yes |
| 24 | G LP | B | 0.02 | 0.007 | yes |
| 25 | BritN HP | A | 0.00 | 0.001 | yes |
| 25 | BritN HP | B | 0.00 | 0.000 | yes |
| 26 | BritN LS | A | 0.29 | 0.125 | yes |
| 26 | BritN LS | B | 0.43 | 0.241 | yes |
| 27 | BritN Bell | A | 0.25 | 0.115 | yes |
| 27 | BritN Bell | B | 2.27 | 0.572 | no |
| 28 | BritN HS | A | 0.53 | 0.267 | yes |
| 28 | BritN HS | B | 0.34 | 0.118 | yes |
| 29 | 250 LS | A | 0.27 | 0.130 | yes |
| 29 | 250 LS | B | 0.43 | 0.164 | yes |
| 30 | 250 Bell | A | 0.05 | 0.020 | yes |
| 30 | 250 Bell | B | 0.25 | 0.124 | yes |
| 31 | 250 HS | A | 0.08 | 0.043 | yes |
| 31 | 250 HS | B | 0.19 | 0.094 | yes |
| 32 | VT HP | A | 0.30 | 0.045 | yes |
| 32 | VT HP | B | 0.28 | 0.040 | yes |
| 33 | VT LP | A | 0.02 | 0.007 | yes |
| 33 | VT LP | B | 0.00 | 0.001 | yes |
| 34 | VT LS | A | 0.17 | 0.053 | yes |
| 34 | VT LS | B | 0.01 | 0.002 | yes |
| 35 | VT Boost | A | 0.03 | 0.012 | yes |
| 35 | VT Boost | B | 0.00 | 0.000 | yes |
| 36 | VT Atten | A | 0.00 | 0.000 | yes |
| 36 | VT Atten | B | 0.08 | 0.036 | yes |
| 37 | VT LMF | A | 0.03 | 0.016 | yes |
| 37 | VT LMF | B | 1.45 | 0.703 | no |
| 38 | VT HMF | A | 0.01 | 0.006 | yes |
| 38 | VT HMF | B | 0.93 | 0.412 | yes |
| 39 | Tone Low | A | 1.20 | 0.427 | yes |
| 39 | Tone Low | B | 0.04 | 0.020 | yes |
| 40 | Tone High | A | 0.01 | 0.007 | yes |
| 40 | Tone High | B | 0.03 | 0.017 | yes |
| 41 | Wall LP | A | 1.41 | 0.259 | yes |
| 41 | Wall LP | B | 1.54 | 0.225 | yes |
| 42 | Wall HP | A | 1.63 | 0.271 | yes |
| 42 | Wall HP | B | 1.52 | 0.231 | yes |
| 43 | Allpass L | A | 0.00 | 0.000 | yes |
| 43 | Allpass L | B | 0.00 | 0.000 | yes |
| 44 | Allpass H | A | 0.00 | 0.000 | yes |
| 44 | Allpass H | B | 0.00 | 0.000 | yes |
| 45 | Blue Low | A | 0.05 | 0.023 | yes |
| 45 | Blue Low | B | 0.00 | 0.001 | yes |
| 46 | Blue High | A | 0.01 | 0.001 | yes |
| 46 | Blue High | B | 0.00 | 0.000 | yes |

`Exact by rule` records the fit status at its fitting condition; it does not establish accuracy at every frequency, gain, Q, or slope. These historical rows are retained as numerical engineering baselines. Current regression results are recorded separately.
