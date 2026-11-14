#pragma once
#include <array>

/** Shared analogue-domain types for the designs.

    A design produces an AnalogCascade: second-order (or first-order, with b2 = a2 = 0) s-domain sections plus an overall
    gain. A separate digitiser (FilterDesign::matchSection, a Nyquist-matched transform) turns each section into a
    digital biquad. Keeping the analogue design and the digitiser apart lets each be checked against the plug-in on its own.

    Section transfer function, with s in rad/s:  H(s) = (b2 s^2 + b1 s + b0) / (a2 s^2 + a1 s + a0). */
namespace AnalogModel
{
    struct AnalogSection
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0;
        double a0 = 1.0, a1 = 0.0, a2 = 0.0;
    };

    struct AnalogCascade
    {
        static constexpr int maxSections = 16;
        std::array<AnalogSection, maxSections> sections {};
        int count = 0;
        double gain = 1.0; // overall linear gain applied to the cascade
    };
}
