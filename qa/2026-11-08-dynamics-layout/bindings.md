# Dynamics control inventory

All 25 existing controls remain. The dynamics enable switch adds a second view of the existing per-band enable parameter. Parameter IDs and processing code are unchanged.

| Control | Parameter expression |
|---|---|
| lookKnob | `"lookaheadMs"` |
| lookToggle | `"lookaheadOn"` |
| aboveToggle | `Params::bandDynAboveOnID (currentBand)` |
| aboveRangeSlider | `Params::bandDynAboveRangeID (currentBand)` |
| aboveRatioSlider | `Params::bandDynAboveRatioID (currentBand)` |
| aheadKnob | `Params::bandDynAheadID (currentBand)` |
| attackKnob | `Params::bandDynAttackID (currentBand)` |
| autoToggle | `Params::bandDynAutoID (currentBand)` |
| belowToggle | `Params::bandDynBelowOnID (currentBand)` |
| belowRangeSlider | `Params::bandDynBelowRangeID (currentBand)` |
| belowRatioSlider | `Params::bandDynBelowRatioID (currentBand)` |
| relativeKnob | `Params::bandDynBlendID (currentBand)` |
| detFreqKnob | `Params::bandDynDetFreqID (currentBand)` |
| linkToggle | `Params::bandDynDetLinkID (currentBand)` |
| detQKnob | `Params::bandDynDetQID (currentBand)` |
| kneeSlider | `Params::bandDynKneeID (currentBand)` |
| dynamicsToggle | `Params::bandDynOnID (currentBand)` |
| relFreeToggle | `Params::bandDynRelFreeID (currentBand)` |
| relFreqKnob | `Params::bandDynRelFreqID (currentBand)` |
| relQKnob | `Params::bandDynRelQID (currentBand)` |
| releaseKnob | `Params::bandDynReleaseID (currentBand)` |
| threshSlider | `Params::bandDynThreshID (currentBand)` |
| onsetsSlider | `Params::bandDynTransID (currentBand)` |
| detSourceCycle | `Params::bandDynSourceID (currentBand)` |
| relSourceCycle | `Params::bandDynRelSourceID (currentBand)` |
| listenToggle | Temporary detector audition, `setDetectorAudition` |
