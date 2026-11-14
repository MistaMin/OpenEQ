#pragma once
#include <tuple>
#include <JuceHeader.h>

namespace Params
{
    constexpr int numBands = 32;

    inline juce::String bandActiveID (int i)    { return "b" + juce::String (i) + "_active"; }
    inline juce::String bandTypeID (int i)      { return "b" + juce::String (i) + "_type"; }
    inline juce::String bandFreqID (int i)      { return "b" + juce::String (i) + "_freq"; }
    inline juce::String bandGainID (int i)      { return "b" + juce::String (i) + "_gain"; }
    inline juce::String bandQID (int i)         { return "b" + juce::String (i) + "_q"; }
    inline juce::String bandSlopeID (int i)     { return "b" + juce::String (i) + "_slope"; }
    inline juce::String bandStereoID (int i)    { return "b" + juce::String (i) + "_stereo"; }
    inline juce::String bandSoloID (int i)      { return "b" + juce::String (i) + "_solo"; }
    inline juce::String bandDynOnID (int i)     { return "b" + juce::String (i) + "_dynOn"; }
    inline juce::String bandDynThreshID (int i) { return "b" + juce::String (i) + "_dynThresh"; }
    inline juce::String bandDynRangeID (int i)  { return "b" + juce::String (i) + "_dynRange"; }
    inline juce::String bandDynAttackID (int i) { return "b" + juce::String (i) + "_dynAtk"; }
    inline juce::String bandDynReleaseID (int i){ return "b" + juce::String (i) + "_dynRel"; }
    inline juce::String bandShownID (int i)     { return "b" + juce::String (i) + "_shown"; }
    inline juce::String bandDynActionID (int i) { return "b" + juce::String (i) + "_dynAct"; }
    inline juce::String bandDynKneeID (int i)   { return "b" + juce::String (i) + "_dynKnee"; }
    inline juce::String bandDynSourceID (int i) { return "b" + juce::String (i) + "_dynSrc"; }
    inline juce::String bandDynBlendID (int i)  { return "b" + juce::String (i) + "_dynBlend"; }
    inline juce::String bandDynAheadID (int i)  { return "b" + juce::String (i) + "_dynAhead"; }
    inline juce::String bandDynTransID (int i)  { return "b" + juce::String (i) + "_dynTrans"; }
    inline juce::String bandDynAutoID (int i)   { return "b" + juce::String (i) + "_dynAuto"; }
    inline juce::String bandDynDetLinkID (int i){ return "b" + juce::String (i) + "_dynDetLink"; }
    inline juce::String bandDynDetFreqID (int i){ return "b" + juce::String (i) + "_dynDetFreq"; }
    inline juce::String bandDynDetQID (int i)   { return "b" + juce::String (i) + "_dynDetQ"; }
    // Below and Above panels and the relative detector. Appended after the original set.
    inline juce::String bandDynBelowOnID (int i)    { return "b" + juce::String (i) + "_dynBelowOn"; }
    inline juce::String bandDynBelowRangeID (int i) { return "b" + juce::String (i) + "_dynBelowRange"; }
    inline juce::String bandDynBelowRatioID (int i) { return "b" + juce::String (i) + "_dynBelowRatio"; }
    inline juce::String bandDynAboveOnID (int i)    { return "b" + juce::String (i) + "_dynAboveOn"; }
    inline juce::String bandDynAboveRangeID (int i) { return "b" + juce::String (i) + "_dynAboveRange"; }
    inline juce::String bandDynAboveRatioID (int i) { return "b" + juce::String (i) + "_dynAboveRatio"; }
    inline juce::String bandDynRelFreeID (int i)    { return "b" + juce::String (i) + "_dynRelFree"; }
    inline juce::String bandDynRelFreqID (int i)    { return "b" + juce::String (i) + "_dynRelFreq"; }
    inline juce::String bandDynRelQID (int i)       { return "b" + juce::String (i) + "_dynRelQ"; }
    inline juce::String bandDynRelSourceID (int i)  { return "b" + juce::String (i) + "_dynRelSrc"; }
    inline juce::String bandPanID (int i)           { return "b" + juce::String (i) + "_pan"; }
    /** Continuous slope, reference normalised 0..1 control. It sets the filter order for bells, shelves, notch, band
        pass and cuts (see FilterDesign). The older b<i>_slope choice is kept only to migrate old sessions. */
    inline juce::String bandSlopeNormID (int i)     { return "b" + juce::String (i) + "_slopeN"; }
    // Split mode (transient/tonal): the band's curve applies to the transient and the tail with independent gains.
    inline juce::String bandSplitOnID (int i)     { return "b" + juce::String (i) + "_splitOn"; }
    // Free phase : -1 inverse, 0 linear, +1 minimum phase for this band's kernel contribution.
    inline juce::String bandPhaseID (int i)       { return "b" + juce::String (i) + "_phase"; }
    inline juce::String bandSuppressID (int i)    { return "b" + juce::String (i) + "_suppress"; }
    inline juce::String bandSplitTransID (int i)  { return "b" + juce::String (i) + "_splitTrans"; }
    inline juce::String bandSplitTailID (int i)   { return "b" + juce::String (i) + "_splitTail"; }

    // Filter names are grouped by family. Index order is the stored choice value, so new entries only go at the end.
    /** Every parameter that belongs to one band slot, in a fixed order. Band copy, paste, split, swap and sort use it. */
    inline juce::StringArray bandParameterIDs (int i)
    {
        return { bandActiveID (i), bandTypeID (i), bandFreqID (i), bandGainID (i), bandQID (i),
                 bandSlopeID (i), bandStereoID (i), bandSoloID (i), bandDynOnID (i), bandDynThreshID (i),
                 bandDynRangeID (i), bandDynAttackID (i), bandDynReleaseID (i), bandShownID (i),
                 bandDynActionID (i), bandDynKneeID (i), bandDynSourceID (i), bandDynBlendID (i),
                 bandDynAheadID (i), bandDynTransID (i), bandDynAutoID (i), bandDynDetLinkID (i),
                 bandDynDetFreqID (i), bandDynDetQID (i),
                 bandDynBelowOnID (i), bandDynBelowRangeID (i), bandDynBelowRatioID (i),
                 bandDynAboveOnID (i), bandDynAboveRangeID (i), bandDynAboveRatioID (i),
                 bandDynRelFreeID (i), bandDynRelFreqID (i), bandDynRelQID (i), bandDynRelSourceID (i),
                 bandPanID (i), bandSlopeNormID (i), bandSplitOnID (i), bandSplitTransID (i), bandSplitTailID (i),
                 bandPhaseID (i), bandSuppressID (i) };
    }

    const juce::StringArray filterTypeNames {
        "Bell", "Low Shelf", "High Shelf", "High Pass", "Low Pass", "Notch", "Band Pass", "Tilt Shelf",
        "Allpass L", "Soft Bell", "Flat Tilt", "Flat Top", "Narrow Resonance",
        "Brickwall HP", "Brickwall LP", "Sword", "Allpass H",
        "E HP", "E LP", "E LS", "E HS", "E LMF", "E HMF", "E Low Bell", "E High Bell",
        "G LMF",
        "BritN HP", "BritN LS", "BritN Bell", "BritN HS",
        "250 Bell", "250 LS", "250 HS",
        "VT HP", "VT LP", "VT LS", "VT Boost", "VT Atten", "VT LMF", "VT HMF",
        "Tone Low", "Tone High",
        "Blue Low", "G LS", "G HS", "Blue High", "G HP", "G LP" };

    /** Submenu for each type. An empty string keeps the type at the top of the menu, for the common shapes. */
    inline juce::String filterTypeGroup (int typeIndex)
    {
        switch (typeIndex)
        {
            case 0:  case 1:  case 2:  case 3:  case 4:  case 5:  case 6:  case 7:
            case 10: case 11: case 15:
                return {};
            case 17: case 18: case 19: case 20: case 21: case 22: case 23: case 24: return "Type E";
            case 25: case 43: case 44: case 46: case 47:                             return "Type G";
            case 26: case 27: case 28: case 29:                                      return "British N";
            case 30: case 31: case 32:                                               return "EQ 250";
            case 33: case 34: case 35: case 36: case 37: case 38: case 39:           return "Vintage Tube";
            case 40: case 41:                                                        return "Tone Stack";
            case 42: case 45:                                                        return "Blue";
            case 8:  case 13: case 14: case 16:                                      return "Misc";
            case 9:  case 12:                                                        return "OpenEQ Extras";
            default:                                                                 return {};
        }
    }

    /** Submenu order, in display order. */
    inline juce::StringArray filterTypeGroupOrder()
    {
        return { "Type E", "Type G", "British N", "EQ 250", "Vintage Tube", "Tone Stack", "Blue", "Misc", "OpenEQ Extras" };
    }
    const juce::StringArray slopeNames      { "6 dB/oct", "12 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct", "72 dB/oct", "96 dB/oct" };
    const juce::StringArray stereoModeNames { "Stereo", "Mid", "Side", "Left", "Right", "Mid Side" };
    // reference Minimum, Linear, Analog, and Mix. Zero Latency is the minimum-phase biquad path.
    const juce::StringArray processingModeNames { "Zero Latency", "Linear Phase", "Analog", "Mix", "Analog ZL" };
    const juce::StringArray windowShapeNames { "Blackman-Harris", "Kaiser", "Hann", "Hamming", "Blackman", "Rectangular" };
    const juce::StringArray renderQualityNames { "Realtime", "Low", "Medium", "High", "Maximum" };
    const juce::StringArray renderOversampleNames { "Realtime", "Off", "2x" };
    const juce::StringArray impulsePaddingNames { "Short", "Normal", "Full" };
    const juce::StringArray digitalPlusNames { "Off", "1", "2", "3", "4" };
    const juce::StringArray mixCrossoverNames { "500 Hz", "1 kHz", "2 kHz", "4 kHz", "8 kHz" };
    const juce::StringArray linearPhaseQualityNames { "Low", "Medium", "High", "Maximum" };
    const juce::StringArray dynActionNames { "Above", "Below" };
    const juce::StringArray dynSourceNames { "Follow", "Left", "Right", "Mid", "Side", "External" };
    const juce::StringArray dynRelSourceNames { "Energy", "Left", "Right", "Mid", "Side", "External" };
    // Split section (transient/tonal transient/tonal split). Source presets scale the separation decision.
    const juce::StringArray splitSourceNames { "Full Mix", "Drums", "Percussive", "Instrument", "Vocal" };
    const juce::StringArray splitSoloNames { "Off", "Transient", "Tonal" };

    /** Normalised slope that gives a cut of the given dB per octave (cut order N = dB / 6). Inverse of the cut order map. */
    inline float slopeNormForCutDb (float dbPerOct) noexcept
    {
        const float order = juce::jlimit (0.0f, 16.0f, dbPerOct / 6.0f);
        if (order <= 2.0f) return order / 4.0f;
        if (order <= 8.0f) return 0.5f + (order - 2.0f) / 24.0f;
        return 0.75f + (order - 8.0f) / 32.0f;
    }

    inline int slopeIndexToDb (int index) noexcept
    {
        static const int table[] = { 6, 12, 24, 36, 48, 72, 96 };
        return table[juce::jlimit (0, 6, index)];
    }

    inline int linearPhaseQualityToFftOrder (int index) noexcept
    {
        static const int table[] = { 11, 12, 13, 14 }; // 2048 .. 16384
        return table[juce::jlimit (0, 3, index)];
    }

    inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

        juce::NormalisableRange<float> freqRange (20.0f, 20000.0f);
        freqRange.setSkewForCentre (1000.0f);

        juce::NormalisableRange<float> qRange (0.025f, 40.0f);
        qRange.setSkewForCentre (1.0f);

        juce::NormalisableRange<float> attackRange (1.0f, 500.0f, 1.0f);
        attackRange.setSkewForCentre (20.0f);

        juce::NormalisableRange<float> releaseRange (1.0f, 2000.0f, 1.0f);
        releaseRange.setSkewForCentre (150.0f);

        for (int i = 0; i < numBands; ++i)
        {
            const bool defaultActive = (i < 3);
            const float defaultFreq = juce::jlimit (20.0f, 20000.0f, 60.0f * std::pow (1.75f, (float) i));

            params.push_back (std::make_unique<juce::AudioParameterBool> (
                bandActiveID (i), "Band " + juce::String (i + 1) + " Active", defaultActive));

            params.push_back (std::make_unique<juce::AudioParameterChoice> (
                bandTypeID (i), "Band " + juce::String (i + 1) + " Type", filterTypeNames, 0));

            params.push_back (std::make_unique<juce::AudioParameterFloat> (
                bandFreqID (i), "Band " + juce::String (i + 1) + " Frequency", freqRange, defaultFreq));

            params.push_back (std::make_unique<juce::AudioParameterFloat> (
                bandGainID (i), "Band " + juce::String (i + 1) + " Gain",
                juce::NormalisableRange<float> (-30.0f, 30.0f, 0.01f), 0.0f));

            params.push_back (std::make_unique<juce::AudioParameterFloat> (
                bandQID (i), "Band " + juce::String (i + 1) + " Q", qRange, 1.0f));

            params.push_back (std::make_unique<juce::AudioParameterChoice> (
                bandSlopeID (i), "Band " + juce::String (i + 1) + " Slope", slopeNames, 1));

            params.push_back (std::make_unique<juce::AudioParameterChoice> (
                bandStereoID (i), "Band " + juce::String (i + 1) + " Stereo Mode", stereoModeNames, 0));

            params.push_back (std::make_unique<juce::AudioParameterBool> (
                bandSoloID (i), "Band " + juce::String (i + 1) + " Solo", false));

            params.push_back (std::make_unique<juce::AudioParameterBool> (
                bandDynOnID (i), "Band " + juce::String (i + 1) + " Dynamic On", false));

            params.push_back (std::make_unique<juce::AudioParameterFloat> (
                bandDynThreshID (i), "Band " + juce::String (i + 1) + " Dyn Threshold",
                juce::NormalisableRange<float> (-60.0f, 0.0f, 1.0f), -24.0f));

            params.push_back (std::make_unique<juce::AudioParameterFloat> (
                bandDynRangeID (i), "Band " + juce::String (i + 1) + " Dyn Range",
                juce::NormalisableRange<float> (-30.0f, 30.0f, 1.0f), 6.0f));

            params.push_back (std::make_unique<juce::AudioParameterFloat> (
                bandDynAttackID (i), "Band " + juce::String (i + 1) + " Dyn Attack", attackRange, 15.0f));

            params.push_back (std::make_unique<juce::AudioParameterFloat> (
                bandDynReleaseID (i), "Band " + juce::String (i + 1) + " Dyn Release", releaseRange, 150.0f));

            params.push_back (std::make_unique<juce::AudioParameterBool> (
                bandShownID (i), "Band " + juce::String (i + 1) + " Shown", defaultActive));

            params.push_back (std::make_unique<juce::AudioParameterChoice> (
                bandDynActionID (i), "Band " + juce::String (i + 1) + " Dyn Action", dynActionNames, 0));

            params.push_back (std::make_unique<juce::AudioParameterFloat> (
                bandDynKneeID (i), "Band " + juce::String (i + 1) + " Dyn Knee",
                juce::NormalisableRange<float> (0.0f, 24.0f, 1.0f), 0.0f));

            params.push_back (std::make_unique<juce::AudioParameterChoice> (
                bandDynSourceID (i), "Band " + juce::String (i + 1) + " Dyn Source", dynSourceNames, 0));

            params.push_back (std::make_unique<juce::AudioParameterFloat> (
                bandDynBlendID (i), "Band " + juce::String (i + 1) + " Dyn Relative",
                juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f));

            params.push_back (std::make_unique<juce::AudioParameterFloat> (
                bandDynAheadID (i), "Band " + juce::String (i + 1) + " Dyn Lookahead",
                juce::NormalisableRange<float> (0.0f, 20.0f, 0.1f), 0.0f));

            params.push_back (std::make_unique<juce::AudioParameterFloat> (
                bandDynTransID (i), "Band " + juce::String (i + 1) + " Dyn Transient",
                juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f));

            params.push_back (std::make_unique<juce::AudioParameterBool> (
                bandDynAutoID (i), "Band " + juce::String (i + 1) + " Dyn Auto", false));

            params.push_back (std::make_unique<juce::AudioParameterBool> (
                bandDynDetLinkID (i), "Band " + juce::String (i + 1) + " Dyn Detector Link", true));

            params.push_back (std::make_unique<juce::AudioParameterFloat> (
                bandDynDetFreqID (i), "Band " + juce::String (i + 1) + " Dyn Detector Frequency",
                freqRange, defaultFreq));

            params.push_back (std::make_unique<juce::AudioParameterFloat> (
                bandDynDetQID (i), "Band " + juce::String (i + 1) + " Dyn Detector Q", qRange, 1.0f));

            params.push_back (std::make_unique<juce::AudioParameterBool> (
                bandDynBelowOnID (i), "Band " + juce::String (i + 1) + " Dyn Below On", false));
            params.push_back (std::make_unique<juce::AudioParameterFloat> (
                bandDynBelowRangeID (i), "Band " + juce::String (i + 1) + " Dyn Below Range",
                juce::NormalisableRange<float> (-30.0f, 30.0f, 1.0f), 6.0f));
            params.push_back (std::make_unique<juce::AudioParameterFloat> (
                bandDynBelowRatioID (i), "Band " + juce::String (i + 1) + " Dyn Below Ratio",
                juce::NormalisableRange<float> (0.0f, 200.0f, 0.1f), 100.0f));

            params.push_back (std::make_unique<juce::AudioParameterBool> (
                bandDynAboveOnID (i), "Band " + juce::String (i + 1) + " Dyn Above On", false));
            params.push_back (std::make_unique<juce::AudioParameterFloat> (
                bandDynAboveRangeID (i), "Band " + juce::String (i + 1) + " Dyn Above Range",
                juce::NormalisableRange<float> (-30.0f, 30.0f, 1.0f), -6.0f));
            params.push_back (std::make_unique<juce::AudioParameterFloat> (
                bandDynAboveRatioID (i), "Band " + juce::String (i + 1) + " Dyn Above Ratio",
                juce::NormalisableRange<float> (0.0f, 200.0f, 0.1f), 100.0f));

            params.push_back (std::make_unique<juce::AudioParameterBool> (
                bandDynRelFreeID (i), "Band " + juce::String (i + 1) + " Dyn Relative Free", false));
            params.push_back (std::make_unique<juce::AudioParameterFloat> (
                bandDynRelFreqID (i), "Band " + juce::String (i + 1) + " Dyn Relative Frequency",
                freqRange, defaultFreq));
            params.push_back (std::make_unique<juce::AudioParameterFloat> (
                bandDynRelQID (i), "Band " + juce::String (i + 1) + " Dyn Relative Q", qRange, 1.0f));
            params.push_back (std::make_unique<juce::AudioParameterChoice> (
                bandDynRelSourceID (i), "Band " + juce::String (i + 1) + " Dyn Relative Source", dynRelSourceNames, 0));

            params.push_back (std::make_unique<juce::AudioParameterFloat> (
                bandPanID (i), "Band " + juce::String (i + 1) + " Pan",
                juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f));

            params.push_back (std::make_unique<juce::AudioParameterFloat> (
                bandSlopeNormID (i), "Band " + juce::String (i + 1) + " Slope",
                juce::NormalisableRange<float> (0.0f, 1.0f, 0.0001f), 0.0f));

            params.push_back (std::make_unique<juce::AudioParameterBool> (
                bandSplitOnID (i), "Band " + juce::String (i + 1) + " Split On", false));
            params.push_back (std::make_unique<juce::AudioParameterFloat> (
                bandSplitTransID (i), "Band " + juce::String (i + 1) + " Split Transient Gain",
                juce::NormalisableRange<float> (-30.0f, 30.0f, 0.01f), 0.0f));
            params.push_back (std::make_unique<juce::AudioParameterFloat> (
                bandSplitTailID (i), "Band " + juce::String (i + 1) + " Split Tail Gain",
                juce::NormalisableRange<float> (-30.0f, 30.0f, 0.01f), 0.0f));
            params.push_back (std::make_unique<juce::AudioParameterFloat> (
                bandPhaseID (i), "Band " + juce::String (i + 1) + " Phase",
                juce::NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f));
            params.push_back (std::make_unique<juce::AudioParameterFloat> (
                bandSuppressID (i), "Band " + juce::String (i + 1) + " Suppressor Focus",
                juce::NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f));
        }

        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            "processingMode", "Processing Mode", processingModeNames, 0));

        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            "linearPhaseQuality", "Linear Phase Quality", linearPhaseQualityNames, 1));

        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            "outputGain", "Output Gain", juce::NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f));

        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            "stereoWidth", "Stereo Width", juce::NormalisableRange<float> (0.0f, 200.0f, 1.0f), 100.0f));

        params.push_back (std::make_unique<juce::AudioParameterBool> ("bypass", "Bypass", false));

        params.push_back (std::make_unique<juce::AudioParameterBool> ("polarity", "Polarity Invert", false));
        params.push_back (std::make_unique<juce::AudioParameterBool> ("autoGain", "Auto Gain", false));

        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            "drive", "Drive", juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));

        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            "spectralAmount", "Spectral Amount", juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            "spectralThresh", "Spectral Floor",
            juce::NormalisableRange<float> (-100.0f, 0.0f, 0.1f), -80.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            "spectralFreq", "Spectral Frequency", freqRange, 3000.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            "spectralOct", "Spectral Width",
            juce::NormalisableRange<float> (0.1f, 10.0f, 0.01f), 10.0f));
        // Resonance suppressor controls. New IDs, so older states load unchanged.
        for (const auto& [id, name, def] : { std::tuple<const char*, const char*, float> { "spectralSharp", "Spectral Sharpness", 0.5f },
                                             { "spectralSelect", "Spectral Selectivity", 0.35f },
                                             { "spectralAttack", "Spectral Attack", 0.2f },
                                             { "spectralRelease", "Spectral Release", 0.3f },
                                             { "spectralMix", "Spectral Mix", 1.0f },
                                             { "spectralLink", "Spectral Stereo Link", 1.0f } })
            params.push_back (std::make_unique<juce::AudioParameterFloat> (id, name, juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), def));
        params.push_back (std::make_unique<juce::AudioParameterBool> ("spectralHard", "Spectral Hard Mode", false));
        params.push_back (std::make_unique<juce::AudioParameterBool> ("spectralDelta", "Spectral Delta", false));
        params.push_back (std::make_unique<juce::AudioParameterBool> ("spectralMS", "Spectral Mid/Side", false));
        params.push_back (std::make_unique<juce::AudioParameterBool> ("spectralSidechain", "Spectral Sidechain", false));

        // Global controls added after the original layout. Their IDs are new, so older states load unchanged.
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            "gainScale", "Gain Scale", juce::NormalisableRange<float> (-200.0f, 200.0f, 0.1f), 100.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            "sideGain", "Side Level", juce::NormalisableRange<float> (-100.0f, 12.0f, 0.1f), 0.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            "outputPan", "Output Pan", juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f));
        // reference 117-bit ultra-high precision option. Saved, because it changes the audio.
        params.push_back (std::make_unique<juce::AudioParameterBool> ("ultraPrecision", "117-bit Precision", false));
        // reference mixed-phase crossover. Choices are 500 Hz to 8 kHz.
        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            "mixCrossover", "Mix Crossover", mixCrossoverNames, 1));
        // reference 2x oversampling. Saved. Changing it re-prepares the plugin.
        params.push_back (std::make_unique<juce::AudioParameterBool> ("oversample2x", "2x Oversampling", false));
        // reference global lookahead: when on, the audio runs 20 ms late and every dynamic band looks ahead by this amount.
        params.push_back (std::make_unique<juce::AudioParameterBool> ("lookaheadOn", "Lookahead", false));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            "lookaheadMs", "Lookahead Time", juce::NormalisableRange<float> (0.0f, 20.0f, 0.1f), 0.0f));

        // Split section (transient/tonal). The split itself is global; each band weights the two paths.
        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            "splitSource", "Split Source", splitSourceNames, 0));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            "splitSeparation", "Transient Separation",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            "splitDecay", "Transient Decay", juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            "splitSmoothingMs", "Split Smoothing",
            juce::NormalisableRange<float> (0.0f, 50.0f, 0.1f), 5.0f));
        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            "splitSolo", "Split Solo", splitSoloNames, 0));

        // FIR windowing : shape and a tightening parameter (Kaiser beta / skirt exponent).
        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            "windowShape", "Window Shape", windowShapeNames, 0));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            "windowParameter", "Window Parameter", juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));

        // Render DSP (phase design section 5): quality and oversampling applied only while rendering offline.
        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            "renderQuality", "Render Quality", renderQualityNames, 0));
        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            "renderOversample", "Render Oversampling", renderOversampleNames, 0));

        // Impulse padding: how much of the windowed impulse is kept (phase design wrap-around truncation).
        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            "impulsePadding", "Impulse Padding", impulsePaddingNames, 1));
        // Digital+ : phase-compensating all-pass steps for the zero-latency biquads.
        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            "digitalPlus", "Digital+ Steps", digitalPlusNames, 0));

        // Input section trim, added at the end of the registry so older states and host
        // automation indices are unchanged.
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            "inputGain", "Input Trim", juce::NormalisableRange<float> (-24.0f, 24.0f, 1.0f), 0.0f));

        return { params.begin(), params.end() };
    }
}
