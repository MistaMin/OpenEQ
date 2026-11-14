/* Filter-response regression checks for fitted designs.
   The numerical expectations are retained historical measurement baselines.
   Do not loosen tolerances to accommodate a regression. */
#include <JuceHeader.h>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include "ResponseModel.h"
#include "PresetRules.h"

namespace
{
    [[noreturn]] void failTest (const char* message) { throw std::runtime_error (message); }
    void require (bool condition, const char* message) { if (! condition) failTest (message); }

    bool nearDb (double a, double b, double tolerance) { return std::abs (a - b) <= tolerance; }

    double magDb (const ResponseModel::DesignedBand& band, float frequency)
    {
        return juce::Decibels::gainToDecibels (ResponseModel::designedMagnitude (band, frequency, 48000.0), -150.0);
    }
}

void runFilterResponseTests()
{
    // ---- Low-pass family (FilterDesign, slope 0.5 = 12 dB/oct). Measured k_slot0_A.tsv. ----
    {
        const int slope = ResponseModel::encodeSlopeNorm (0.5f);
        const auto lp = ResponseModel::designBand (FilterType::HighCut, 1000.0f, 0.0f, 1.0f, slope, 48000.0);
        require (nearDb (magDb (lp, 100.0f), 0.0, 0.02), "LP should be flat at 100 Hz");
        require (nearDb (magDb (lp, 1000.0f), -3.0, 0.1), "LP should be -3 dB at the cutoff");
        require (nearDb (magDb (lp, 20000.0f), -53.1, 1.5), "LP top octave should follow the analogue slope (measured -53.1 dB at 20 kHz)");
    }

    // ---- Band pass (slope 0.5): unity at the centre, ~12 dB/oct skirts. Measured k_slot2_A.tsv. ----
    {
        const int slope = ResponseModel::encodeSlopeNorm (0.5f);
        const auto bp = ResponseModel::designBand (FilterType::BandPass, 1000.0f, 6.0f, 1.0f, slope, 48000.0);
        require (nearDb (magDb (bp, 1000.0f), 0.0, 0.1), "BP should be unity at its centre");
        require (nearDb (magDb (bp, 200.0f), -13.6, 1.5), "BP low skirt should fall ~12 dB/oct (measured -13.6 dB at 200 Hz)");
        require (nearDb (magDb (bp, 5000.0f), -13.6, 1.5), "BP high skirt should mirror the low skirt");
    }

    // ---- Notch (slope 0.5): broad cut around the centre, recovers far out. Measured k_slot6_A.tsv
    //      (the probe's grid has no point exactly at 1 kHz; -67.2 dB is the measured value at 1010.5 Hz). ----
    {
        const int slope = ResponseModel::encodeSlopeNorm (0.5f);
        const auto notch = ResponseModel::designBand (FilterType::Notch, 1000.0f, 0.0f, 1.0f, slope, 48000.0);
        require (nearDb (magDb (notch, 1010.5f), -67.2, 2.0), "notch depth at slope 0.5 should match the plug-in (measured -67.2 dB at 1010.5 Hz)");
        require (nearDb (magDb (notch, 976.0f), -52.6, 2.0), "notch skirt should match the plug-in (measured -52.6 dB at 976 Hz)");
        require (nearDb (magDb (notch, 100.0f), 0.0, 0.2), "notch should be flat far below its centre");
        require (nearDb (magDb (notch, 10000.0f), 0.0, 0.2), "notch should be flat far above its centre");
    }

    // ---- Character shelves (fits/character-shelves.md). ----
    {
        // VT Atten (slot 36): reduce-only high shelf with gain-dependent knee/Q/gain. Unity at gain >= 0.
        const auto flat = ResponseModel::designBand (FilterType::VtAtten, 1000.0f, 0.0f, 1.0f, 12, 48000.0);
        require (flat.stageCount == 1 && nearDb (magDb (flat, 1000.0f), 0.0, 0.01), "VT Atten at 0 dB should be unity");
        const auto cut = ResponseModel::designBand (FilterType::VtAtten, 1000.0f, -9.0f, 1.0f, 12, 48000.0);
        require (nearDb (magDb (cut, 20.0f), 0.0, 0.2), "VT Atten should be flat at low frequencies");
        require (nearDb (magDb (cut, 20000.0f), -9.69, 1.0), "VT Atten cut depth should follow the fitted law (measured -9.68 dB at -9 dB)");

        // Tone Low (slot 39): commanded low-shelf gain plus a weak HF loss. Measured k_slot39_A.tsv.
        const auto toneLow = ResponseModel::designBand (FilterType::ToneLow, 1000.0f, 6.0f, 1.0f, 12, 48000.0);
        require (nearDb (magDb (toneLow, 20.0f), 6.0, 0.3), "Tone Low should shelve by the commanded gain at low frequencies");
        require (magDb (toneLow, 12000.0f) < 5.0, "Tone Low should keep its HF loss at 12 kHz (measured +4.0 dB)");

        // Tone High (slot 40): commanded high-shelf gain plus a resonant bell at ~0.65 fc. Measured k_slot40_A.tsv:
        // the bell hump sits around 0.5 fc (+0.94 dB at 487 Hz), the top shelves by the commanded gain.
        const auto toneHigh = ResponseModel::designBand (FilterType::ToneHigh, 1000.0f, 6.0f, 1.0f, 12, 48000.0);
        require (nearDb (magDb (toneHigh, 20000.0f), 6.0, 0.5), "Tone High should shelve by the commanded gain at the top");
        require (nearDb (magDb (toneHigh, 487.0f), 0.94, 0.2), "Tone High should keep its bell hump around 0.5 fc (measured +0.94 dB at 487 Hz)");
        require (nearDb (magDb (toneHigh, 20.0f), 0.0, 0.05), "Tone High should be flat at 20 Hz");
    }

    // ---- Continuous slope plumbing (F17). ----
    {
        constexpr int encoded = ResponseModel::encodeSlopeNorm (0.5f);
        require (ResponseModel::isEncodedSlope (encoded), "encoded slope must be recognised");
        require (std::abs (ResponseModel::decodeSlopeNorm (encoded) - 0.5) < 1.0e-4, "slope norm should round-trip");
        require (! ResponseModel::isEncodedSlope (12), "legacy dB/oct values must not read as encoded");
        // The legacy dB/oct channel still designs the old way for sessions that predate the continuous slope.
        const auto legacy = ResponseModel::designBand (FilterType::HighCut, 1000.0f, 0.0f, 1.0f, 12, 48000.0);
        require (nearDb (magDb (legacy, 1000.0f), -3.0, 0.3), "legacy 12 dB/oct slope should still design a working low pass");
    }

    std::cout << "PASS: reference fidelity designs\n";
}
