#pragma once
#include <JuceHeader.h>
#include <cmath>
#include "PluginProcessor.h"
#include "Parameters.h"
#include "PresetTables.h"
#include "PresetRules.h"

/** Imports normalized JSON preset data. Values are normalized to 0..1 and keys identify
    band or global parameters. PresetTables and PresetRules map these values to OpenEQ controls.
    Unsupported settings are reported in the import result. */
namespace PresetImport
{
    struct Result
    {
        int bandsImported = 0;
        juce::StringArray notes;
    };

    namespace detail
    {
        /** Linear interpolation over a table of 201 evenly spaced normalised positions. */
        inline double lookup (const float* table, int count, double normalised)
        {
            const double position = juce::jlimit (0.0, 1.0, normalised) * (count - 1);
            const int index = juce::jlimit (0, count - 2, (int) position);
            const double fraction = position - index;
            return table[index] + (table[index + 1] - table[index]) * fraction;
        }

        inline double frequencyFromNormalised (double v) { return 10.0 * std::pow (3000.0, juce::jlimit (0.0, 1.0, v)); }
        inline double qFromNormalised (double v) { return std::pow (40.0, 2.0 * juce::jlimit (0.0, 1.0, v) - 1.0); }

        inline juce::String slotLabel (int slot)
        {
            static const char* names[] = {
                "Low Pass", "High Pass", "Band Pass", "Bell", "Low Shelf", "High Shelf", "Notch", "Tilt Shelf",
                "Flat Tilt", "Flat Top", "Sword", "E HP", "E LS", "E Low Bell", "E LMF", "E HMF", "E High Bell",
                "E HS", "E LP", "G HP", "G LS", "G LMF", "G HMF", "G HS", "G LP", "BritN HP", "BritN LS",
                "BritN Bell", "BritN HS", "250 LS", "250 Bell", "250 HS", "VT HP", "VT LP", "VT LS", "VT Boost",
                "VT Atten", "VT LMF", "VT HMF", "Tone Low", "Tone High", "Wall LP", "Wall HP", "Allpass L",
                "Allpass H", "Blue Low", "Blue High"
            };
            return slot >= 0 && slot < (int) std::size (names) ? juce::String (names[slot]) : juce::String ("Reserved");
        }

        inline int indexOfType (const juce::String& name)
        {
            return Params::filterTypeNames.indexOf (name);
        }

        /** Imported stereo placement steps (0 to 5) to OpenEQ stereo modes. */
        inline int stereoForStep (int step)
        {
            static const int table[] = { 0 /*Stereo*/, 5 /*Mid Side*/, 3 /*Left*/, 4 /*Right*/, 1 /*Mid*/, 2 /*Side*/ };
            return table[juce::jlimit (0, 5, step)];
        }

        /** Imported slope in dB per octave to the nearest OpenEQ cut slope choice. */
        inline int slopeChoice (double dbPerOct)
        {
            static const double slopes[] = { 6.0, 12.0, 24.0, 36.0, 48.0, 72.0, 96.0 };
            int best = 0;
            for (int i = 1; i < 7; ++i)
                if (std::abs (slopes[i] - dbPerOct) < std::abs (slopes[best] - dbPerOct))
                    best = i;
            return best;
        }

        inline double parse (const juce::var& value)
        {
            if (value.isBool())
                return value ? 1.0 : 0.0;
            if (value.isInt() || value.isInt64() || value.isDouble())
                return (double) value;
            return juce::String (value.toString()).getDoubleValue();
        }
    }

    /** Reads a .json file and writes its values into the processor as one undoable edit. */
    inline Result importPreset (OpenEQAudioProcessor& processor, const juce::File& file)
    {
        Result result;
        const auto parsed = juce::JSON::parse (file);
        auto* object = parsed.getDynamicObject();
        if (object == nullptr)
        {
            result.notes.add ("The file is not a valid .json preset JSON.");
            return result;
        }

        auto value = [object] (const juce::String& key, double fallback) -> double
        {
            const auto property = object->getProperty (juce::Identifier (key));
            return property.isVoid() ? fallback : detail::parse (property);
        };
        auto has = [object] (const juce::String& key) { return ! object->getProperty (juce::Identifier (key)).isVoid(); };

        processor.beginEdit ("Import .json Preset");

        // Global settings.
        processor.setRawFromUI ("bypass", value ("Bypass", 0.0) > 0.5 ? 1.0f : 0.0f);
        processor.setRawFromUI ("polarity", value ("Invert Phase", 0.0) > 0.5 ? 1.0f : 0.0f);
        const double masterDb = (double) PresetTables::kPresetMasterDb[(int) std::lround (juce::jlimit (0.0, 1.0, value ("Master Vol.", 0.5)) * 200.0)];
        processor.setRawFromUI ("outputGain", (float) juce::jlimit (-24.0, 24.0, masterDb));
        if (masterDb > 24.0)
            result.notes.add ("Master volume above +24 dB is limited to +24 dB.");
        processor.setRawFromUI ("gainScale", (float) (-200.0 + 400.0 * juce::jlimit (0.0, 1.0, value ("Gain Scale", 0.75))));
        processor.setRawFromUI ("autoGain", value ("Enable Auto Gain", 0.0) > 0.5 ? 1.0f : 0.0f);
        processor.setRawFromUI ("outputPan", (float) (200.0 * juce::jlimit (0.0, 1.0, value ("Pan", 0.5)) - 100.0));
        processor.setRawFromUI ("stereoWidth", (float) (200.0 * juce::jlimit (0.0, 1.0, value ("Width", 0.5))));
        processor.setRawFromUI ("sideGain", (float) juce::jlimit (-100.0, 12.0,
            (double) PresetTables::kPresetSideDb[(int) std::lround (juce::jlimit (0.0, 1.0, value ("Side Vol.", 1.0)) * 200.0)]));
        processor.setRawFromUI ("lookaheadOn", value ("Enable Lookahead", 0.0) > 0.5 ? 1.0f : 0.0f);
        processor.setRawFromUI ("lookaheadMs", (float) (20.0 * juce::jlimit (0.0, 1.0, value ("Lookahead", 0.0))));
        processor.setRawFromUI ("ultraPrecision", value ("Enable DoubleDouble", 0.0) > 0.5 ? 1.0f : 0.0f);

        if (has ("Phase Mode"))
        {
            if (value ("Phase Mode", 0.0) < 0.5)
                processor.setRawFromUI ("processingMode", 0.0f);
            else
                result.notes.add ("Phase mode other than Minimum Phase is not imported, because its value is not known.");
        }
        for (const auto* key : { "Mix Phase Crossover Freq.", "Linear Phase Precision", "Upsample Factor" })
            if (has (key))
                result.notes.add (juce::String (key) + " is not imported: the value does not map to an OpenEQ setting yet.");

        // Bands. A preset replaces the whole state, so bands not in the file are cleared.
        for (int band = 0; band < OpenEQAudioProcessor::numBands; ++band)
        {
            const auto prefix = "Band" + juce::String (band + 1) + " ";
            const bool used = value (prefix + "Used", 0.0) > 0.5;
            if (! used)
            {
                processor.setRawFromUI (Params::bandShownID (band), 0.0f);
                processor.setRawFromUI (Params::bandActiveID (band), 0.0f);
                continue;
            }

            const int slot = (int) std::lround (juce::jlimit (0.0, 1.0, value (prefix + "Type", 0.0)) * 71.0);
            const double slopeNorm = juce::jlimit (0.0, 1.0, value (prefix + "Slope", 0.5));
            const double sourceQ = juce::jlimit (0.025, 40.0, detail::qFromNormalised (value (prefix + "Q", 0.5)));
            const double gainDb = -30.0 + 60.0 * juce::jlimit (0.0, 1.0, value (prefix + "Gain", 0.5));
            const auto resolved = PresetRules::resolve (slot, sourceQ, slopeNorm, gainDb);
            const int typeIndex = detail::indexOfType (resolved.openeqType);
            if (! resolved.exact)
                result.notes.add ("Band " + juce::String (band + 1) + ": " + detail::slotLabel (slot) + " is approximated by "
                                  + resolved.openeqType + " (measured error up to " + juce::String (resolved.residualDb, 1) + " dB).");
            if (typeIndex < 0)
                continue;

            const double frequencyNorm = value (prefix + "Freq.", 0.5);
            const double frequency = detail::frequencyFromNormalised (frequencyNorm);
            if (frequency > 20000.0)
                result.notes.add ("Band " + juce::String (band + 1) + ": frequency above 20 kHz is limited to 20 kHz.");

            processor.setRawFromUI (Params::bandTypeID (band), (float) typeIndex);
            processor.setRawFromUI (Params::bandShownID (band), 1.0f);
            processor.setRawFromUI (Params::bandActiveID (band), value (prefix + "Enabled", 1.0) > 0.5 ? 1.0f : 0.0f);
            processor.setRawFromUI (Params::bandFreqID (band), (float) juce::jlimit (20.0, 20000.0, frequency));
            processor.setRawFromUI (Params::bandGainID (band), (float) (std::isfinite (resolved.gainDb) ? resolved.gainDb : gainDb));
            processor.setRawFromUI (Params::bandQID (band), (float) resolved.q);
            const double slopeDb = resolved.cutSlopeDb > 0.0
                ? resolved.cutSlopeDb
                : (double) PresetTables::kPresetSlopeDbPerOct[(int) std::lround (slopeNorm * 200.0)];
            processor.setRawFromUI (Params::bandSlopeID (band), (float) detail::slopeChoice (slopeDb));
            // The continuous slope is a normalized control, so it imports as is.
            processor.setRawFromUI (Params::bandSlopeNormID (band), (float) slopeNorm);
            processor.setRawFromUI (Params::bandStereoID (band), (float) detail::stereoForStep (
                (int) std::lround (juce::jlimit (0.0, 1.0, value (prefix + "Stereo Placement", 0.0)) * 5.0)));
            processor.setRawFromUI (Params::bandPanID (band), (float) (200.0 * juce::jlimit (0.0, 1.0, value (prefix + "Pan/Width", 0.5)) - 100.0));

            // Dynamics. The import schema has one dynamic switch for both Below and Above; OpenEQ's panels are switched separately.
            const bool dynamic = value (prefix + "Dyn. On", 0.0) > 0.5;
            processor.setRawFromUI (Params::bandDynOnID (band), dynamic ? 1.0f : 0.0f);
            processor.setRawFromUI (Params::bandDynBelowOnID (band), dynamic ? 1.0f : 0.0f);
            processor.setRawFromUI (Params::bandDynAboveOnID (band), dynamic ? 1.0f : 0.0f);
            processor.setRawFromUI (Params::bandDynThreshID (band), (float) (-48.0 + 48.0 * juce::jlimit (0.0, 1.0, value (prefix + "Dyn. Threshold", 0.5))));
            processor.setRawFromUI (Params::bandDynBelowRatioID (band), (float) (200.0 * juce::jlimit (0.0, 1.0, value (prefix + "Dyn. Below Ratio", 0.5))));
            processor.setRawFromUI (Params::bandDynBelowRangeID (band), (float) (-30.0 + 60.0 * juce::jlimit (0.0, 1.0, value (prefix + "Dyn. Below Range", 0.5))));
            processor.setRawFromUI (Params::bandDynAboveRatioID (band), (float) (200.0 * juce::jlimit (0.0, 1.0, value (prefix + "Dyn. Above Ratio", 0.5))));
            processor.setRawFromUI (Params::bandDynAboveRangeID (band), (float) (-30.0 + 60.0 * juce::jlimit (0.0, 1.0, value (prefix + "Dyn. Above Range", 0.5))));
            processor.setRawFromUI (Params::bandDynAttackID (band), (float) juce::jlimit (0.1, 500.0,
                (double) PresetTables::kPresetAttackMs[(int) std::lround (juce::jlimit (0.0, 1.0, value (prefix + "Dyn. Attack", 0.4)) * 200.0)]));
            processor.setRawFromUI (Params::bandDynReleaseID (band), (float) juce::jlimit (1.0, 2000.0,
                (double) PresetTables::kPresetReleaseMs[(int) std::lround (juce::jlimit (0.0, 1.0, value (prefix + "Dyn. Release", 0.4)) * 200.0)]));
            processor.setRawFromUI (Params::bandDynTransID (band), (float) (100.0 * juce::jlimit (0.0, 1.0, value (prefix + "Dyn. Onsets Mix", 0.0))));
            processor.setRawFromUI (Params::bandDynBlendID (band), (float) (100.0 * juce::jlimit (0.0, 1.0, value (prefix + "Dyn. Rela. Mix", 0.0))));
            processor.setRawFromUI (Params::bandDynSourceID (band), value (prefix + "Dyn. Det. Use SideChain", 0.0) > 0.5 ? 5.0f : 0.0f);
            processor.setRawFromUI (Params::bandDynRelSourceID (band), value (prefix + "Dyn. Rela. Use SideChain", 0.0) > 0.5 ? 5.0f : 0.0f);
            // The import schema's "free mode" detector is OpenEQ's unlinked detector.
            processor.setRawFromUI (Params::bandDynDetLinkID (band), value (prefix + "Det. Free Mode On", 0.0) > 0.5 ? 0.0f : 1.0f);
            processor.setRawFromUI (Params::bandDynRelFreeID (band), value (prefix + "Rela. Free Mode On", 0.0) > 0.5 ? 1.0f : 0.0f);
            processor.setRawFromUI (Params::bandDynDetFreqID (band), (float) juce::jlimit (20.0, 20000.0,
                detail::frequencyFromNormalised (value (prefix + "Dyn. Det. Freq.", 0.5))));
            processor.setRawFromUI (Params::bandDynDetQID (band), (float) juce::jlimit (0.025, 40.0,
                detail::qFromNormalised (value (prefix + "Dyn. Det. Q", 0.5))));
            processor.setRawFromUI (Params::bandDynRelFreqID (band), (float) juce::jlimit (20.0, 20000.0,
                detail::frequencyFromNormalised (value (prefix + "Dyn. Rela. Freq.", 0.5))));
            processor.setRawFromUI (Params::bandDynRelQID (band), (float) juce::jlimit (0.025, 40.0,
                detail::qFromNormalised (value (prefix + "Dyn. Rela. Q", 0.5))));

            ++result.bandsImported;
        }

        processor.endEdit();
        return result;
    }
}
