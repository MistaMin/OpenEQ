#pragma once
#include <JuceHeader.h>
#include <cmath>

namespace ButterworthDesign
{
    /** Number of second-order sections in a Butterworth cascade of `numPoles` poles.
        Odd pole counts are not used; 6 dB/oct cuts are first-order and do not call this. */
    inline int sectionCount (int numPoles) noexcept
    {
        return juce::jmax (0, numPoles / 2);
    }

    /** Writes Q values for a cascade of biquads forming a Butterworth filter.
        `qs` must hold at least `sectionCount(numPoles)` entries. No heap use. */
    inline void cascadeQs (int numPoles, double* qs, int count) noexcept
    {
        if (qs == nullptr || numPoles < 2 || count <= 0)
            return;

        for (int k = 1; k <= count; ++k)
        {
            const double theta = juce::MathConstants<double>::pi * (2.0 * k - 1.0) / (2.0 * numPoles);
            qs[k - 1] = 1.0 / (2.0 * std::cos (theta));
        }
    }
}
