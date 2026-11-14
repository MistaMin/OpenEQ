#pragma once
#include <JuceHeader.h>
#include <cmath>
#include "PresetTables.h"

/** Maps normalized preset slots to OpenEQ filter types, Q, gain, and slope.
    residualDb records the historical fit residual over 20 Hz to 20 kHz; values above 0.5
    are reported as approximate by the importer. */
namespace PresetRules
{
    enum class QMode
    {
        Direct,  // OpenEQ Q equals source Q
        PowerLaw,   // OpenEQ Q = a * sourceQ^p
        Fixed,      // OpenEQ Q = a, whatever the source Q
        ShelfSlope, // The shelf design ignores Q for shelves; slope sets the shape, mapped with shelfQ()
        TiltSlope,  // tilt shelf: shelfQ() scaled by the commanded gain
        Cut,        // cut types: slope is mapped with the measured cut curve
        NotchSlope  // notch: a true notch at slope 0; above that, a bell whose gain and Q follow the measured table
    };

    struct Rule
    {
        const char* openeqType;
        QMode mode;
        double a;
        double p;
        double residualDb;
        const char* godeqType() const noexcept { return openeqType; }
    };

    /** Rules for preset type slots 0 to 46 (47 to 71 are reserved). */
    inline const Rule& ruleForSlot (int slot)
    {
        static const Rule rules[] = {
            { "Low Pass", QMode::Direct, 0, 0, 0.05 },        // 0 Low Pass (FilterDesign, slope maps directly)
            { "High Pass", QMode::Direct, 0, 0, 0.02 },       // 1 High Pass (FilterDesign)
            { "Band Pass", QMode::Direct, 0, 0, 0.3 },        // 2 Band Pass (FilterDesign; fractional orders approximate)
            { "Bell", QMode::Direct, 1.0, 1.0, 0.01 },      // 3 Bell (FilterDesign)
            { "Low Shelf", QMode::Direct, 0, 0, 0.01 },       // 4 Low Shelf (FilterDesign)
            { "High Shelf", QMode::Direct, 0, 0, 0.05 },      // 5 High Shelf (FilterDesign)
            { "Notch", QMode::Direct, 0, 0, 0.1 },            // 6 Notch (FilterDesign; fractional orders approximate)
            { "Tilt Shelf", QMode::TiltSlope, 0, 0, 0.21 },    // 7 Tilt Shelf
            { "Flat Tilt", QMode::Fixed, 0.15, 0, 0.10 },         // 8 Flat Tilt (absolute shelf cascade plus offset, fitted across 120 Hz to 5 kHz)
            { "Flat Top", QMode::Fixed, 1.1, 0, 0.3 },          // 9 Flat Top (fitted two-bell cascade)
            { "Sword", QMode::PowerLaw, 3.63, 0.51, 0.18 },    // 10 Sword
            { "E HP", QMode::Fixed, 0.74, 0, 0.02 },                // 11 E HP (fitted two-section high pass)
            { "E LS", QMode::Fixed, 0.621, 0, 0.30 },          // 12 E LS
            { "E Low Bell", QMode::Fixed, 0.3, 0, 0.12 },        // 13 E Low Bell (fitted bell)
            { "E LMF", QMode::PowerLaw, 0.396, 0.18, 0.14 },   // 14 E LMF
            { "E HMF", QMode::PowerLaw, 0.339, 0.17, 0.05 },   // 15 E HMF
            { "E High Bell", QMode::Fixed, 0.2, 0, 0.12 },        // 16 E High Bell (fitted)
            { "E HS", QMode::Fixed, 0.2, 0, 0.12 },                 // 17 E HS (fitted)
            { "E LP", QMode::Fixed, 0.0, 0, 0.04 },             // 18 E LP (VintageFilters)
            { "G HP", QMode::Fixed, 0.0, 0, 0.01 },             // 19 G HP (VintageFilters)
            { "G LS", QMode::Fixed, 0.0, 0, 0.09 },             // 20 G LS (fitted cascade)
            { "G LMF", QMode::Fixed, 0.35, 0, 0.15 },              // 21 G LMF (fitted bell)
            { "G LMF", QMode::Fixed, 0.35, 0, 0.2 },                // 22 G HMF (approximated by the fitted G LMF bell)
            { "G HS", QMode::Fixed, 0.0, 0, 0.09 },             // 23 G HS (fitted cascade)
            { "G LP", QMode::Fixed, 0.0, 0, 0.04 },             // 24 G LP (VintageFilters)
            { "BritN HP", QMode::Fixed, 0.0, 0, 0.01 },         // 25 BritN HP (VintageFilters)
            { "BritN LS", QMode::Fixed, 0.6, 0, 0.24 },             // 26 BritN LS (fitted)
            { "BritN Bell", QMode::PowerLaw, 0.5225, 0.165, 0.12 }, // 27 BritN Bell
            { "BritN HS", QMode::Fixed, 0.6, 0, 0.27 },             // 28 BritN HS (fitted)
            { "250 LS", QMode::Fixed, 0.2, 0, 0.12 },            // 29 250 LS (fitted cascade; Q fixed)
            { "250 Bell", QMode::PowerLaw, 0.863, 0.196, 0.02 }, // 30 250 Bell
            { "250 HS", QMode::Fixed, 0.2, 0, 0.04 },            // 31 250 HS (fitted cascade; Q fixed)
            { "VT HP", QMode::Fixed, 0.7, 0, 0.02 },                // 32 VT HP (fitted two-section high pass)
            { "VT LP", QMode::Fixed, 0.0, 0, 0.01 },            // 33 VT LP (VintageFilters)
            { "VT LS", QMode::Direct, 0, 0, 0.31 },         // 34 VT LS (fitted gain laws, fits/character-shelves.md)
            { "VT Boost", QMode::Fixed, 0.666, 0, 0.47 },      // 35 VT Boost (source Q moves it by 0.5 dB)
            { "VT Atten", QMode::Direct, 0, 0, 0.21 },      // 36 VT Atten (fitted gain laws, fits/character-shelves.md)
            { "VT LMF", QMode::Fixed, 0.7916, 0, 0.02 },       // 37 VT LMF
            { "VT HMF", QMode::Fixed, 0.9412, 0, 0.01 },       // 38 VT HMF
            { "Tone Low", QMode::Direct, 0, 0, 0.21 },      // 39 Tone Low (fitted gain laws, fits/character-shelves.md)
            { "Tone High", QMode::Direct, 0, 0, 0.14 },     // 40 Tone High (fitted gain laws, fits/character-shelves.md)
            { "Brickwall LP", QMode::Direct, 0, 0, 0.3 },   // 41 Wall LP (fitted 15-section cascade, fits/wall_joint_fit.json)
            { "Brickwall HP", QMode::Direct, 0, 0, 0.3 },   // 42 Wall HP (mirror of Wall LP)
            { "Allpass L", QMode::Direct, 0, 0, 0.0 },      // 43 Allpass L
            { "Allpass H", QMode::Direct, 0, 0, 0.0 },      // 44 Allpass H
            { "Blue Low", QMode::Direct, 0, 0, 0.05 },       // 45 Blue Low (VintageFilters, boost shelf)
            { "Blue High", QMode::Fixed, 0.0, 0, 0.05 },        // 46 Blue High (VintageFilters, boost shelf)
        };
        return rules[juce::jlimit (0, (int) std::size (rules) - 1, slot)];
    }

    /** Shelf Q for a Imported slope setting (0 to 1), fitted at 1 kHz, +6 dB. */
    inline double shelfQ (double slopeNorm)
    {
        static const double qs[9] = { 0.4875, 0.6431, 0.7780, 0.8338, 0.8782, 0.9576, 0.9914, 1.0263, 1.0263 };
        const double position = juce::jlimit (0.0, 1.0, slopeNorm) * 8.0;
        const int index = juce::jmin (7, (int) position);
        const double fraction = position - index;
        return qs[index] + (qs[index + 1] - qs[index]) * fraction;
    }

    struct Resolved
    {
        juce::String openeqType;
        juce::String godeqType; // alias for backwards compatibility
        double q = 1.0;
        double cutSlopeDb = 0.0; // only for cut types
        double gainDb = std::nan (""); // set when the rule replaces the gain (notch above slope 0); NaN keeps the band gain
        bool exact = true;
        double residualDb = 0.0;
    };

    /** The OpenEQ type and Q for one reference band. `sourceQ` is the source Q value and `slopeNorm` the normalised slope. */
    inline Resolved resolve (int slot, double sourceQ, double slopeNorm, double gainDb)
    {
        const auto& rule = ruleForSlot (slot);
        Resolved out;
        out.openeqType = rule.openeqType;
        out.godeqType = rule.openeqType;
        out.residualDb = rule.residualDb;
        out.exact = rule.residualDb <= 0.5;

        double q = sourceQ;
        switch (rule.mode)
        {
            case QMode::Direct:   q = sourceQ; break;
            case QMode::PowerLaw:    q = rule.a * std::pow (sourceQ, rule.p); break;
            case QMode::Fixed:       q = rule.a; break;
            case QMode::ShelfSlope:  q = shelfQ (slopeNorm); break;
            case QMode::TiltSlope:   q = shelfQ (slopeNorm) * (0.974 - 0.029 * (std::abs (gainDb) - 6.0)); break;
            case QMode::Cut:         break;
            case QMode::NotchSlope:  q = sourceQ; break;
        }
        out.q = juce::jlimit (0.025, 40.0, q);

        if (rule.mode == QMode::NotchSlope && slopeNorm > 0.06)
        {
            // Historical response baseline (KNotch): reference notch above slope 0 is approximated by a bell.
            // Gain and Q are interpolated from the fit at slopes 0 to 1 in steps of 0.125.
            static const double slopes[9] = { 0.0, 0.125, 0.25, 0.375, 0.5, 0.625, 0.75, 0.875, 1.0 };
            static const double gains[9] = { -47.5, -67.5, -67.5, -70.0, -70.0, -80.0, -87.5, -92.5, -95.0 };
            static const double qs[9] = { 15.36, 40.75, 33.53, 33.53, 27.59, 30.41, 33.53, 27.59, 22.70 };
            const double position = juce::jlimit (0.0, 1.0, slopeNorm) * 8.0;
            const int index = juce::jmin (7, (int) position);
            const double fraction = position - index;
            out.openeqType = "Bell";
            out.godeqType = "Bell";
            out.gainDb = gains[index] + (gains[index + 1] - gains[index]) * fraction;
            q = qs[index] + (qs[index + 1] - qs[index]) * fraction;
            out.residualDb = 0.5 + 20.0 * slopeNorm * slopeNorm;
            out.exact = false;
            (void) slopes;
            out.q = juce::jlimit (0.025, 40.0, q);
            return out;
        }

        if (rule.mode == QMode::Cut)
        {
            // Cuts use the measured cut-slope curve. Above 0.6 the plug-in is below float precision, so the steepest slope is used.
            out.cutSlopeDb = slopeNorm > 0.6 ? 96.0
                : PresetTables::kPresetCutSlopeDbPerOct[(int) std::lround (juce::jlimit (0.0, 0.6, slopeNorm) * 20.0)];
        }
        return out;
    }
}
