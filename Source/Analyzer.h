#pragma once
#include <JuceHeader.h>
#include <cstring>

/** Spectrum handoff. The audio thread writes a private ring and never waits.
    A published copy is swapped for the UI. If the UI is copying that snapshot,
    the audio thread drops the display update and keeps the samples. */
class AnalyzerRing
{
public:
    static constexpr int order = 12;
    static constexpr int size = 1 << order;

    void pushSamples (const float* data, int numSamples)
    {
        if (data == nullptr || numSamples <= 0)
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            privateRing[(size_t) writeIndex] = data[i];
            writeIndex = (writeIndex + 1) % size;
        }

        if (! publishLock.tryEnter())
            return;

        for (int i = 0; i < size; ++i)
            published[(size_t) i] = privateRing[(size_t) ((writeIndex + i) % size)];

        publishLock.exit();
    }

    void copyLatest (std::array<float, size>& dest) const
    {
        const juce::ScopedLock sl (publishLock);
        std::memcpy (dest.data(), published.data(), sizeof (float) * (size_t) size);
    }

private:
    mutable juce::CriticalSection publishLock;
    std::array<float, size> privateRing {};
    std::array<float, size> published {};
    int writeIndex = 0;
};
