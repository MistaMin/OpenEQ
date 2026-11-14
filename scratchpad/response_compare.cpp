/* response_compare: OpenEQ's side of a fidelity row against a response_probe measurement.

   For one reference type slot and setting, runs the same path the .json importer runs
   (PresetRules::resolve, then ResponseModel::designBand) and compares the designed
   magnitude with the measured reference curve in a response_probe .tsv file.

   Usage:
     response_compare <k.tsv> <slot> <freq> <gain> <q> <slope>
   Prints "maxerrDb rmsDb" on stdout and writes <k.tsv>.diff with per-frequency columns.
*/
#include <JuceHeader.h>
#include <cstdio>
#include <cmath>
#include "Parameters.h"
#include "ResponseModel.h"
#include "PresetRules.h"
#include "PresetTables.h"

static int openeqTypeIndex (const juce::String& name)
{
    return Params::filterTypeNames.indexOf (name);
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI initialise;
    if (argc < 7)
    {
        std::fprintf (stderr, "usage: response_compare <k.tsv> <slot> <freq> <gain> <q> <slope>\n");
        return 1;
    }

    const juce::File tsvFile (argv[1]);
    const int slot = std::atoi (argv[2]);
    const float freq = (float) std::atof (argv[3]);
    const float gainDb = (float) std::atof (argv[4]);
    const float sourceQ = (float) std::atof (argv[5]);
    const float slopeNorm = (float) std::atof (argv[6]);

    const auto resolved = PresetRules::resolve (slot, sourceQ, slopeNorm, gainDb);
    int typeIndex = openeqTypeIndex (resolved.openeqType);
    if (typeIndex < 0)
    {
        std::fprintf (stderr, "resolved type '%s' not in OpenEQ's list\n", resolved.openeqType.toRawUTF8());
        return 1;
    }

    // Experiment mode: slot 999 builds a two-section bilinear (RBJ) low pass, Q 0.7071 per section.
    const bool experimentLp = (slot == 999);
    if (experimentLp) typeIndex = openeqTypeIndex ("Low Pass");

    const float effectiveGain = std::isfinite (resolved.gainDb) ? (float) resolved.gainDb : gainDb;
    double slopeDb = resolved.cutSlopeDb > 0.0
        ? resolved.cutSlopeDb
        : (double) PresetTables::kPresetSlopeDbPerOct[(int) std::lround (slopeNorm * 200.0)];
    // The importer snaps the slope to the nearest OpenEQ choice.
    static const int choices[] = { 6, 12, 24, 36, 48, 72, 96 };
    int nearest = choices[0];
    for (int c : choices) if (std::abs ((double) c - slopeDb) < std::abs ((double) nearest - slopeDb)) nearest = c;

    // Types designed by FilterDesign consume the continuous slope through the encoded channel,
    // exactly like the processor (readAllBandParameters). Feed the encoded norm instead of the choice.
    if (ResponseModel::usesContinuousSlope ((FilterType) typeIndex))
        nearest = ResponseModel::encodeSlopeNorm (slopeNorm);

    auto designed = ResponseModel::designBand ((FilterType) typeIndex, freq, effectiveGain,
                                               (float) resolved.q, nearest, 48000.0);
    if (experimentLp)
    {
        using Coeffs = juce::dsp::IIR::ArrayCoefficients<float>;
        designed.stageCount = 2;
        designed.raw[0] = Coeffs::makeLowPass (48000.0, freq, 0.7071f);
        designed.raw[1] = Coeffs::makeLowPass (48000.0, freq, 0.7071f);
    }

    std::printf ("slot %d -> %s (index %d), q %.4f, gain %.2f dB, slope %.0f dB/oct, stages %d\n",
                 slot, resolved.openeqType.toRawUTF8(), typeIndex, resolved.q, effectiveGain, slopeDb, designed.stageCount);

    const auto lines = juce::StringArray::fromLines (tsvFile.loadFileAsString());
    double maxErr = 0.0, sumSq = 0.0;
    int count = 0;
    juce::String diff;
    for (const auto& line : lines)
    {
        if (line.isEmpty() || line.contains ("freqHz")) continue;
        const double f = line.upToFirstOccurrenceOf ("\t", false, false).getDoubleValue();
        const double measuredDb = line.fromFirstOccurrenceOf ("\t", false, false).getDoubleValue();
        if (f <= 0.0) continue;
        // Both curves are compared down to -150 dB, the floor the probe resolves (float precision).
        const double measuredClamped = juce::jmax (measuredDb, -150.0);
        const double openeqDb = juce::Decibels::gainToDecibels (
            ResponseModel::designedMagnitude (designed, (float) f, 48000.0), -150.0);
        const double err = openeqDb - measuredClamped;
        maxErr = juce::jmax (maxErr, std::abs (err));
        sumSq += err * err;
        ++count;
        diff += juce::String (f, 6) + "\t" + juce::String (measuredDb, 6) + "\t" + juce::String (openeqDb, 6)
              + "\t" + juce::String (err, 6) + "\n";
    }

    const double rms = count > 0 ? std::sqrt (sumSq / (double) count) : 0.0;
    std::printf ("maxerrDb %.4f rmsDb %.4f over %d points\n", maxErr, rms, count);
    tsvFile.withFileExtension ("diff").replaceWithText (diff);
    return 0;
}
