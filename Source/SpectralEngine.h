#pragma once
#include <JuceHeader.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <vector>

/** Experimental short-time suppressor.

    Hann windows, 4× overlap, analysis and synthesis window both applied.
    A flat gain of 1 reconstructs the input after a fixed delay. Amount above
    zero turns down bins inside the chosen region once they pass the threshold.
    Stereo bins share one gain so a loud side does not leave the other untouched.

    The processor does not call this unless spectral amount is above zero. */
class SpectralEngine
{
public:
    static constexpr int fftOrder = 10;
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr int hop = fftSize / 4;

    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        transform = std::make_unique<juce::dsp::FFT> (fftOrder);
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

        for (auto& ring : input)
            ring.assign ((size_t) fftSize, 0.0f);
        for (auto& ring : output)
            ring.assign ((size_t) fftSize * 2, 0.0f);
        frame.assign ((size_t) fftSize, {});
        spectrumL.assign ((size_t) fftSize, {});
        spectrumR.assign ((size_t) fftSize, {});
        seen = 0;
        prepared = true;
    }

    void reset() noexcept
    {
        for (auto& ring : input)
            std::fill (ring.begin(), ring.end(), 0.0f);
        for (auto& ring : output)
            std::fill (ring.begin(), ring.end(), 0.0f);
        seen = 0;
    }

    /** Samples between an input impulse and the reconstructed output impulse. */
    int getLatencySamples() const noexcept { return fftSize - 1; }

    void process (float* left, float* right, int numSamples, float amount, float thresholdDb,
                  float centreHz, float widthOctaves)
    {
        if (! prepared || left == nullptr || numSamples <= 0 || transform == nullptr)
            return;

        const bool stereo = right != nullptr && right != left;
        for (int i = 0; i < numSamples; ++i)
        {
            input[0][(size_t) (seen % fftSize)] = left[i];
            if (stereo)
                input[1][(size_t) (seen % fftSize)] = right[i];
            ++seen;

            // A hop-aligned frame, including the zero-padded frames before the
            // buffer is full. Without those, an impulse at sample 0 sits only on
            // the Hann zero and never comes back.
            if (seen % hop == 0)
                synthesise (stereo, amount, thresholdDb, centreHz, widthOctaves);

            const juce::int64 emitAt = seen - 1 - (juce::int64) getLatencySamples();
            left[i] = takeOutput (0, emitAt);
            if (stereo)
                right[i] = takeOutput (1, emitAt);
        }
    }

private:
    void synthesise (bool stereo, float amount, float thresholdDb, float centreHz, float widthOctaves)
    {
        analyse (0, spectrumL);
        if (stereo)
            analyse (1, spectrumR);
        else
            std::copy (spectrumL.begin(), spectrumL.end(), spectrumR.begin());

        const float octaves = juce::jmax (0.05f, widthOctaves);
        const float low = centreHz / std::pow (2.0f, octaves * 0.5f);
        const float high = centreHz * std::pow (2.0f, octaves * 0.5f);
        const float depth = juce::jlimit (0.0f, 1.0f, amount) * 24.0f;

        for (int bin = 0; bin <= fftSize / 2; ++bin)
        {
            const double frequency = bin * sampleRate / (double) fftSize;
            const float magnitude = std::max (std::abs (spectrumL[(size_t) bin]), std::abs (spectrumR[(size_t) bin]));
            const float decibels = juce::Decibels::gainToDecibels (magnitude, -120.0f);
            float gain = 1.0f;
            if (depth > 0.0f && frequency >= (double) low && frequency <= (double) high && decibels > thresholdDb)
                gain = juce::Decibels::decibelsToGain (-juce::jmin (depth, decibels - thresholdDb));

            spectrumL[(size_t) bin] *= gain;
            spectrumR[(size_t) bin] *= gain;
            if (bin > 0 && bin < fftSize / 2)
            {
                spectrumL[(size_t) (fftSize - bin)] = std::conj (spectrumL[(size_t) bin]);
                spectrumR[(size_t) (fftSize - bin)] = std::conj (spectrumR[(size_t) bin]);
            }
        }

        addFrame (0, spectrumL);
        if (stereo)
            addFrame (1, spectrumR);
    }

    void analyse (int channel, std::vector<juce::dsp::Complex<float>>& dest)
    {
        const int start = (int) seen - fftSize;
        for (int i = 0; i < fftSize; ++i)
        {
            const int time = start + i;
            const float sample = time >= 0 ? input[(size_t) channel][(size_t) (time % fftSize)] : 0.0f;
            frame[(size_t) i] = { sample * window[(size_t) i], 0.0f };
        }
        transform->perform (frame.data(), dest.data(), false);
    }

    void addFrame (int channel, const std::vector<juce::dsp::Complex<float>>& spectrum)
    {
        transform->perform (spectrum.data(), frame.data(), true);
        const juce::int64 frameStart = seen - fftSize;
        auto& ring = output[(size_t) channel];
        const int ringSize = (int) ring.size();
        for (int i = 0; i < fftSize; ++i)
        {
            const juce::int64 time = frameStart + i;
            if (time < 0)
                continue;
            const int index = (int) (time % ringSize);
            const float scale = hopScale[(size_t) ((int) (time % hop))];
            ring[(size_t) index] += frame[(size_t) i].real() * window[(size_t) i] * scale;
        }
    }

    float takeOutput (int channel, juce::int64 time) noexcept
    {
        if (time < 0)
            return 0.0f;
        auto& ring = output[(size_t) channel];
        const int index = (int) (time % (juce::int64) ring.size());
        const float sample = ring[(size_t) index];
        ring[(size_t) index] = 0.0f;
        return sample;
    }

    double sampleRate = 44100.0;
    bool prepared = false;
    std::vector<float> hopScale;
    juce::int64 seen = 0;
    std::unique_ptr<juce::dsp::FFT> transform;
    std::vector<float> window;
    std::array<std::vector<float>, 2> input;
    std::array<std::vector<float>, 2> output;
    std::vector<juce::dsp::Complex<float>> frame;
    std::vector<juce::dsp::Complex<float>> spectrumL;
    std::vector<juce::dsp::Complex<float>> spectrumR;
};
