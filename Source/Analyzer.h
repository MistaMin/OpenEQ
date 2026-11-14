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

        // Only the newest `size` samples can survive in the ring; older ones would be overwritten.
        if (numSamples > size)
        {
            writeIndex = (writeIndex + (numSamples - size) % size) % size;
            data += numSamples - size;
            numSamples = size;
        }

        // Block copies, at most two per pass: the ring wraps at most once.
        const int first = juce::jmin (numSamples, size - writeIndex);
        std::memcpy (privateRing.data() + writeIndex, data, sizeof (float) * (size_t) first);
        std::memcpy (privateRing.data(), data + first, sizeof (float) * (size_t) (numSamples - first));
        writeIndex = (writeIndex + numSamples) % size;

        if (! publishLock.tryEnter())
            return;

        // Oldest sample first: the tail of the ring from the write position, then its head.
        const int tail = size - writeIndex;
        std::memcpy (published.data(), privateRing.data() + writeIndex, sizeof (float) * (size_t) tail);
        std::memcpy (published.data() + tail, privateRing.data(), sizeof (float) * (size_t) writeIndex);
        generation.fetch_add (1, std::memory_order_release);

        publishLock.exit();
    }

    /** Counts publications. Read it before copyLatest: the copy is then at least as new as the count says,
        so a reader that sees the same count as last time already has this snapshot. */
    juce::uint32 getGeneration() const noexcept { return generation.load (std::memory_order_acquire); }

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
    std::atomic<juce::uint32> generation { 0 };
};
