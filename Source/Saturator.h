#pragma once
#include <JuceHeader.h>
#include <cmath>

/** Optional output colour. Drive 0 is a bypass and adds no latency.
    The waveshaper is an odd tanh with unity small-signal gain, run at 2×. */
class Saturator
{
public:
    void prepare (double sampleRate, int maximumBlockSize, int channels)
    {
        oversampling.initProcessing ((size_t) juce::jmax (1, maximumBlockSize));
        scratch.setSize (juce::jmax (1, channels), juce::jmax (1, maximumBlockSize), false, false, true);
        juce::ignoreUnused (sampleRate);
        prepared = true;
    }

    int getLatencySamples() const noexcept
    {
        return prepared ? (int) oversampling.getLatencyInSamples() : 0;
    }

    void process (juce::AudioBuffer<float>& buffer, int offset, int numSamples, int channels, float drive)
    {
        if (! prepared || numSamples <= 0 || drive <= 1.0e-4f)
            return;

        const float amount = juce::jlimit (0.0f, 1.0f, drive);
        const float k = 1.0f + amount * 8.0f;
        const int used = juce::jmin (channels, buffer.getNumChannels(), scratch.getNumChannels());
        if (scratch.getNumSamples() < numSamples || used <= 0)
            return;

        for (int channel = 0; channel < used; ++channel)
            scratch.copyFrom (channel, 0, buffer, channel, offset, numSamples);

        juce::dsp::AudioBlock<float> block (scratch);
        auto sub = block.getSubBlock (0, (size_t) numSamples).getSubsetChannelBlock (0, (size_t) used);
        auto up = oversampling.processSamplesUp (sub);

        const int upChannels = (int) up.getNumChannels();
        const int upSamples = (int) up.getNumSamples();
        for (int channel = 0; channel < upChannels; ++channel)
        {
            auto* data = up.getChannelPointer ((size_t) channel);
            for (int i = 0; i < upSamples; ++i)
                data[i] = std::tanh (k * data[i]) / k;
        }

        oversampling.processSamplesDown (sub);

        for (int channel = 0; channel < used; ++channel)
            buffer.copyFrom (channel, offset, scratch, channel, 0, numSamples);
    }

private:
    juce::dsp::Oversampling<float> oversampling { 2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true };
    juce::AudioBuffer<float> scratch;
    bool prepared = false;
};
