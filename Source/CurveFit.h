#pragma once
#include <JuceHeader.h>
#include <vector>
#include "ResponseModel.h"

/** Offline curve fitting. These functions allocate and must not run on the audio thread. */
namespace CurveFit
{
    struct Band
    {
        float frequency = 1000.0f;
        float gainDb = 0.0f;
        float q = 1.0f;
    };

    inline double bellDb (float frequency, float gainDb, float q, double atHz, double sampleRate)
    {
        const auto designed = ResponseModel::designBand (FilterType::Bell, frequency, gainDb, q, 12, sampleRate);
        const double mag = ResponseModel::designedMagnitude (designed, atHz, sampleRate);
        return juce::Decibels::gainToDecibels (mag, -100.0);
    }

    /** Greedy bell fit. Each new bell is placed on the largest remaining error. */
    inline void fitBells (const float* frequency, const float* targetDb, int count, int maxBands,
                          double sampleRate, std::vector<Band>& result)
    {
        result.clear();
        if (frequency == nullptr || targetDb == nullptr || count <= 0 || maxBands <= 0)
            return;

        std::vector<float> residual (targetDb, targetDb + count);
        const int bands = juce::jmin (maxBands, 8);

        for (int bandIndex = 0; bandIndex < bands; ++bandIndex)
        {
            int peak = 0;
            float peakError = 0.0f;
            for (int i = 0; i < count; ++i)
            {
                if (std::abs (residual[(size_t) i]) > std::abs (peakError))
                {
                    peakError = residual[(size_t) i];
                    peak = i;
                }
            }

            if (std::abs (peakError) < 0.4f)
                break;

            Band band;
            band.frequency = juce::jlimit (20.0f, 20000.0f, frequency[peak]);
            band.gainDb = juce::jlimit (-12.0f, 12.0f, peakError);
            band.q = 1.2f;
            result.push_back (band);

            for (int i = 0; i < count; ++i)
                residual[(size_t) i] = (float) ((double) residual[(size_t) i]
                    - bellDb (band.frequency, band.gainDb, band.q, frequency[i], sampleRate));
        }
    }

    /** Difference of two averaged spectra, in dB. Quiet bins are ignored.
        The result is smoothed with a short moving average and clamped. */
    inline void matchDifference (const float* sourceDb, const float* referenceDb, int count,
                                 float* differenceDb, float maxBoostDb = 12.0f)
    {
        if (sourceDb == nullptr || referenceDb == nullptr || differenceDb == nullptr || count <= 0)
            return;

        for (int i = 0; i < count; ++i)
        {
            if (sourceDb[i] < -70.0f && referenceDb[i] < -70.0f)
                differenceDb[i] = 0.0f;
            else
                differenceDb[i] = referenceDb[i] - sourceDb[i];
        }

        std::vector<float> smooth ((size_t) count, 0.0f);
        constexpr int radius = 2;
        for (int i = 0; i < count; ++i)
        {
            float sum = 0.0f;
            int used = 0;
            for (int k = -radius; k <= radius; ++k)
            {
                const int index = juce::jlimit (0, count - 1, i + k);
                sum += differenceDb[index];
                ++used;
            }
            smooth[(size_t) i] = sum / (float) used;
        }

        for (int i = 0; i < count; ++i)
            differenceDb[i] = juce::jlimit (-maxBoostDb, maxBoostDb, smooth[(size_t) i]);
    }
}
