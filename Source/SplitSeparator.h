#pragma once
#include <JuceHeader.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

/** Splits a signal into a transient stream and a tonal stream that add back to the input.

    The method is median-filter separation on a short-time spectrum (Fitzgerald, "Harmonic/Percussive
    Separation using Median Filtering", DAFx 2010, with a soft mask). A sustained partial holds its level
    from frame to frame, so a median over time keeps it and drops a short event. A short event is wide in
    frequency, so a median over frequency keeps it and drops a narrow partial. The two estimates give a
    per-bin mask between 0 (tonal) and 1 (transient).

    Only the transient stream is resynthesised. The tonal stream is the delayed input minus the transient
    stream, so the two streams always sum to the delayed input.

    Stereo channels share one mask, so the split does not move the image.

    Hann analysis and synthesis windows at 4x overlap. The frame length follows the sample rate and stays
    close to 21 ms. Every buffer is sized in prepare(); process() does not allocate. */
class SplitSeparator
{
public:
    /** Frames in the median over time, and how many of them come after the frame being decided. */
    static constexpr int timeFrames = 9;
    static constexpr int futureFrames = 3;
    /** Bins in the median over frequency. About 600 Hz at every sample rate. */
    static constexpr int frequencySpan = 13;

    struct Settings
    {
        float separation = 0.5f;   // 0..1. Higher asks for a stronger case before a bin counts as transient.
        float decay = 0.5f;        // 0..1. How long a bin stays in the transient stream after its onset.
        float smoothingMs = 5.0f;  // 0..50. Slows the mask, mostly on the way out of the transient stream.
        float sourceScale = 1.0f;  // Source preset: scales the separation bias.
    };

    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        const int order = juce::jlimit (10, 13, 10 + juce::roundToInt (std::log2 (sampleRate / 48000.0)));
        fftSize = 1 << order;
        hop = fftSize / 4;
        bins = fftSize / 2 + 1;
        latency = fftSize - 1 + futureFrames * hop;

        transform = std::make_unique<juce::dsp::FFT> (order);
        window.resize ((size_t) fftSize);
        for (int i = 0; i < fftSize; ++i)
            window[(size_t) i] = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) fftSize));

        hopScale.assign ((size_t) hop, 1.0f);
        for (int residue = 0; residue < hop; ++residue)
        {
            float sum = 0.0f;
            for (int k = 0; k < fftSize / hop; ++k)
                sum += window[(size_t) (residue + k * hop)] * window[(size_t) (residue + k * hop)];
            hopScale[(size_t) residue] = sum > 1.0e-8f ? 1.0f / sum : 0.0f;
        }

        delaySize = juce::nextPowerOfTwo (latency + 2);
        for (int channel = 0; channel < 2; ++channel)
        {
            input[(size_t) channel].assign ((size_t) fftSize, 0.0f);
            output[(size_t) channel].assign ((size_t) fftSize * 2, 0.0f);
            delay[(size_t) channel].assign ((size_t) delaySize, 0.0f);
            for (auto& spectrum : spectra[(size_t) channel])
                spectrum.assign ((size_t) bins, {});
        }
        for (auto& magnitude : magnitudes)
            magnitude.assign ((size_t) bins, 0.0f);
        frame.assign ((size_t) fftSize, {});
        scratch.assign ((size_t) fftSize, {});
        rawMask.assign ((size_t) bins, 0.0f);
        mirrored.assign ((size_t) (bins + 2 * (frequencySpan / 2)), 0.0f);
        held.assign ((size_t) bins, 0.0f);
        mask.assign ((size_t) bins, 0.0f);
        reset();
        prepared = true;
    }

    void reset() noexcept
    {
        for (int channel = 0; channel < 2; ++channel)
        {
            std::fill (input[(size_t) channel].begin(), input[(size_t) channel].end(), 0.0f);
            std::fill (output[(size_t) channel].begin(), output[(size_t) channel].end(), 0.0f);
            std::fill (delay[(size_t) channel].begin(), delay[(size_t) channel].end(), 0.0f);
            for (auto& spectrum : spectra[(size_t) channel])
                std::fill (spectrum.begin(), spectrum.end(), juce::dsp::Complex<float>());
        }
        for (auto& magnitude : magnitudes)
            std::fill (magnitude.begin(), magnitude.end(), 0.0f);
        std::fill (held.begin(), held.end(), 0.0f);
        std::fill (mask.begin(), mask.end(), 0.0f);
        newest = 0;
        seen = 0;
    }

    /** Samples between an input sample and the same sample in the two streams. */
    int getLatencySamples() const noexcept { return latency; }

    /** Writes the transient stream and the tonal stream of the input, both delayed by getLatencySamples().
        A null or repeated right input means mono, and the right outputs are then left alone.
        The outputs may not alias the inputs. */
    void process (const float* inLeft, const float* inRight,
                  float* transientLeft, float* transientRight,
                  float* tonalLeft, float* tonalRight,
                  int numSamples, const Settings& settings) noexcept
    {
        if (! prepared || inLeft == nullptr || numSamples <= 0)
            return;

        const bool stereo = inRight != nullptr && inRight != inLeft && transientRight != nullptr && tonalRight != nullptr;
        for (int i = 0; i < numSamples; ++i)
        {
            const int inputIndex = (int) (seen % fftSize);
            const int delayIndex = (int) (seen & (juce::int64) (delaySize - 1));
            input[0][(size_t) inputIndex] = inLeft[i];
            delay[0][(size_t) delayIndex] = inLeft[i];
            if (stereo)
            {
                input[1][(size_t) inputIndex] = inRight[i];
                delay[1][(size_t) delayIndex] = inRight[i];
            }
            ++seen;

            if (seen % hop == 0)
                synthesise (stereo, settings);

            const juce::int64 emitAt = seen - 1 - (juce::int64) latency;
            const float transientL = takeOutput (0, emitAt);
            const float dryL = emitAt >= 0 ? delay[0][(size_t) (emitAt & (juce::int64) (delaySize - 1))] : 0.0f;
            transientLeft[i] = transientL;
            tonalLeft[i] = dryL - transientL;
            if (stereo)
            {
                const float transientR = takeOutput (1, emitAt);
                const float dryR = emitAt >= 0 ? delay[1][(size_t) (emitAt & (juce::int64) (delaySize - 1))] : 0.0f;
                transientRight[i] = transientR;
                tonalRight[i] = dryR - transientR;
            }
        }
    }

private:
    /** Medians by fixed comparator networks: each gives the same value std::nth_element picks (the element
        of rank count / 2), without branches. The networks were derived from an odd-even merge sort, pruned to
        the median output, and checked against every 0/1 input, which by the 0-1 principle covers every
        input. They run on rows of `tile` bins at once, so the compiler can vectorise them. */
    struct Comparator { int low, high; };
    static constexpr Comparator medianOf9[] = {
        { 0, 1 }, { 2, 3 }, { 0, 2 }, { 1, 3 }, { 1, 2 }, { 4, 5 }, { 6, 7 }, { 4, 6 }, { 5, 7 }, { 5, 6 },
        { 0, 4 }, { 2, 6 }, { 2, 4 }, { 1, 5 }, { 3, 7 }, { 3, 5 }, { 3, 4 }, { 4, 8 }, { 3, 4 } };
    static constexpr int medianOf9Slot = 4;
    static constexpr Comparator medianOf13[] = {
        { 0, 1 }, { 2, 3 }, { 0, 2 }, { 1, 3 }, { 1, 2 }, { 4, 5 }, { 6, 7 }, { 4, 6 }, { 5, 7 }, { 5, 6 },
        { 0, 4 }, { 2, 6 }, { 2, 4 }, { 1, 5 }, { 3, 7 }, { 3, 5 }, { 1, 2 }, { 3, 4 }, { 5, 6 }, { 8, 9 },
        { 10, 11 }, { 8, 10 }, { 9, 11 }, { 9, 10 }, { 8, 12 }, { 10, 12 }, { 9, 10 }, { 11, 12 }, { 4, 12 },
        { 4, 8 }, { 2, 10 }, { 6, 10 }, { 6, 8 }, { 1, 9 }, { 5, 9 }, { 3, 11 }, { 3, 5 }, { 5, 6 } };
    static constexpr int medianOf13Slot = 6;
    static_assert (timeFrames == 9 && frequencySpan == 13, "the median networks are sized for these spans");
    static constexpr int tile = 16;

    template <size_t rowCount, size_t comparators>
    static void sortRows (float (&rows)[rowCount][tile], const Comparator (&network)[comparators]) noexcept
    {
        for (const auto& c : network)
        {
            float* low = rows[c.low];
            float* high = rows[c.high];
            for (int j = 0; j < tile; ++j)
            {
                const float a = low[j], b = high[j];
                low[j] = std::min (a, b);
                high[j] = std::max (a, b);
            }
        }
    }

    void synthesise (bool stereo, const Settings& settings) noexcept
    {
        newest = (newest + 1) % timeFrames;
        analyse (0, spectra[0][(size_t) newest]);
        if (stereo)
            analyse (1, spectra[1][(size_t) newest]);

        auto& newestMagnitude = magnitudes[(size_t) newest];
        for (int bin = 0; bin < bins; ++bin)
        {
            float power = std::norm (spectra[0][(size_t) newest][(size_t) bin]);
            if (stereo)
                power += std::norm (spectra[1][(size_t) newest][(size_t) bin]);
            newestMagnitude[(size_t) bin] = std::sqrt (power);
        }

        // The frame being decided has futureFrames newer frames after it.
        const int centre = (newest - futureFrames + timeFrames) % timeFrames;
        const auto& centreMagnitude = magnitudes[(size_t) centre];

        // Separation 0.5 is an even split. Each 1/6 of the range doubles the bias one way or the other.
        const float bias = std::pow (2.0f, (juce::jlimit (0.0f, 1.0f, settings.separation) - 0.5f) * 6.0f)
                         * juce::jmax (0.1f, settings.sourceScale);
        const float biasSquared = bias * bias;
        const int half = frequencySpan / 2;

        // The centre frame with its mirror images at both ends of the spectrum, so every window over
        // frequency is a contiguous run: window `bin` is mirrored[bin .. bin + frequencySpan - 1].
        for (int i = -half; i < bins + half; ++i)
        {
            int index = i;
            if (index < 0) index = -index;
            if (index >= bins) index = 2 * (bins - 1) - index;
            mirrored[(size_t) (i + half)] = centreMagnitude[(size_t) index];
        }

        for (int start = 0; start < bins; start += tile)
        {
            const int count = juce::jmin (tile, bins - start);
            float overTime[timeFrames][tile] = {};
            float overFrequency[frequencySpan][tile] = {};
            for (int f = 0; f < timeFrames; ++f)
                std::copy_n (magnitudes[(size_t) f].data() + start, count, overTime[f]);
            for (int k = 0; k < frequencySpan; ++k)
                std::copy_n (mirrored.data() + start + k, count, overFrequency[k]);
            sortRows (overTime, medianOf9);
            sortRows (overFrequency, medianOf13);

            for (int j = 0; j < count; ++j)
            {
                const float tonal = overTime[medianOf9Slot][j];
                const float transient = overFrequency[medianOf13Slot][j];
                const float transientPower = transient * transient;
                const float tonalPower = tonal * tonal * biasSquared;
                rawMask[(size_t) (start + j)] = transientPower / (transientPower + tonalPower + 1.0e-20f);
            }
        }

        // Decay holds a bin in the transient stream after its onset. Smoothing slows every change.
        const double hopSeconds = (double) hop / sampleRate;
        const double decaySeconds = 0.150 * (double) settings.decay * (double) settings.decay;
        const float holdCoefficient = decaySeconds > 1.0e-4 ? (float) std::exp (-hopSeconds / decaySeconds) : 0.0f;
        // A bin enters the transient stream four times faster than it leaves, so smoothing does not smear an
        // onset into the tonal stream.
        const double smoothSeconds = (double) settings.smoothingMs * 0.001;
        const float fallCoefficient = smoothSeconds > 1.0e-4 ? (float) std::exp (-hopSeconds / smoothSeconds) : 0.0f;
        const float riseCoefficient = smoothSeconds > 1.0e-4 ? (float) std::exp (-4.0 * hopSeconds / smoothSeconds) : 0.0f;

        for (int bin = 0; bin < bins; ++bin)
        {
            // A three-bin blur over frequency keeps isolated bins from switching on their own.
            const float below = rawMask[(size_t) juce::jmax (0, bin - 1)];
            const float above = rawMask[(size_t) juce::jmin (bins - 1, bin + 1)];
            const float blurred = 0.25f * below + 0.5f * rawMask[(size_t) bin] + 0.25f * above;
            held[(size_t) bin] = juce::jmax (blurred, held[(size_t) bin] * holdCoefficient);
            const float coefficient = held[(size_t) bin] > mask[(size_t) bin] ? riseCoefficient : fallCoefficient;
            mask[(size_t) bin] = held[(size_t) bin] + coefficient * (mask[(size_t) bin] - held[(size_t) bin]);
        }

        const juce::int64 frameStart = seen - fftSize - (juce::int64) futureFrames * hop;
        addFrame (0, spectra[0][(size_t) centre], frameStart);
        if (stereo)
            addFrame (1, spectra[1][(size_t) centre], frameStart);
    }

    void analyse (int channel, std::vector<juce::dsp::Complex<float>>& dest) noexcept
    {
        const juce::int64 start = seen - fftSize;
        for (int i = 0; i < fftSize; ++i)
        {
            const juce::int64 time = start + i;
            const float sample = time >= 0 ? input[(size_t) channel][(size_t) (time % fftSize)] : 0.0f;
            frame[(size_t) i] = { sample * window[(size_t) i], 0.0f };
        }
        transform->perform (frame.data(), scratch.data(), false);
        std::copy (scratch.begin(), scratch.begin() + bins, dest.begin());
    }

    /** Resynthesises the transient part of one stored frame and overlap-adds it at its own time. */
    void addFrame (int channel, const std::vector<juce::dsp::Complex<float>>& spectrum, juce::int64 frameStart) noexcept
    {
        for (int bin = 0; bin < bins; ++bin)
        {
            scratch[(size_t) bin] = spectrum[(size_t) bin] * mask[(size_t) bin];
            if (bin > 0 && bin < fftSize / 2)
                scratch[(size_t) (fftSize - bin)] = std::conj (scratch[(size_t) bin]);
        }
        transform->perform (scratch.data(), frame.data(), true);

        auto& ring = output[(size_t) channel];
        const juce::int64 ringSize = (juce::int64) ring.size();
        for (int i = 0; i < fftSize; ++i)
        {
            const juce::int64 time = frameStart + i;
            if (time < 0)
                continue;
            const float scale = hopScale[(size_t) (time % hop)];
            ring[(size_t) (time % ringSize)] += frame[(size_t) i].real() * window[(size_t) i] * scale;
        }
    }

    float takeOutput (int channel, juce::int64 time) noexcept
    {
        if (time < 0)
            return 0.0f;
        auto& ring = output[(size_t) channel];
        const auto index = (size_t) (time % (juce::int64) ring.size());
        const float sample = ring[index];
        ring[index] = 0.0f;
        return sample;
    }

    double sampleRate = 44100.0;
    bool prepared = false;
    int fftSize = 1024;
    int hop = 256;
    int bins = 513;
    int latency = 1023 + futureFrames * 256;
    int delaySize = 2048;
    int newest = 0;
    juce::int64 seen = 0;

    std::unique_ptr<juce::dsp::FFT> transform;
    std::vector<float> window, hopScale;
    std::array<std::vector<float>, 2> input, output, delay;
    /** The last timeFrames spectra per channel, and their channel-linked magnitudes. A ring indexed by `newest`. */
    std::array<std::array<std::vector<juce::dsp::Complex<float>>, timeFrames>, 2> spectra;
    std::array<std::vector<float>, timeFrames> magnitudes;
    std::vector<juce::dsp::Complex<float>> frame, scratch;
    std::vector<float> rawMask, held, mask, mirrored;
};
