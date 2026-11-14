#pragma once
#include <JuceHeader.h>
#include <cmath>
#include <cstring>

/** Sample-rate dynamic gain.

    Detect: a band-pass (or the whole signal) measured by energy, left, right, mid or side.
    Relative: the reference that the detected level is compared against. It is either the broadband
    level, or its own band-pass ("Relative Free"), measured on its own channel choice.
    Gain: either the Below and Above panels (each with its own range and ratio, sharing one threshold),
    or the single-action path used by sessions saved before those panels existed.

    The envelope advances one sample at a time, so a split host block reaches the same state as one
    long block. Coefficient storage is allocated in prepare. */
class DynamicsEngine
{
public:
    enum class Mix
    {
        Energy = 0, // linked stereo energy, no L+R cancellation
        Left,
        Right,
        Mid,
        Side
    };

    /** Everything the gain computer needs for one block. Values are copied from the band state. */
    struct Settings
    {
        float thresholdDb = -24.0f;
        float kneeDb = 0.0f;
        float attackMs = 15.0f;
        float releaseMs = 150.0f;
        float transientPercent = 0.0f;
        bool automatic = false;
        float relativePercent = 0.0f;
        bool relativeFree = false;        // relative reference uses its own band-pass
        bool modern = false;              // use the Below and Above panels below
        bool belowOn = false;
        float belowRangeDb = 6.0f;
        float belowRatio = 1.0f;          // 1.0 is 100%
        bool aboveOn = false;
        float aboveRangeDb = -6.0f;
        float aboveRatio = 1.0f;
        int action = 0;                   // legacy path: 0 above, 1 below
        float rangeDb = 6.0f;             // legacy path
    };

    void prepare (double newSampleRate, int maximumBlockSize)
    {
        sampleRate = newSampleRate;
        const int capacity = juce::jmax (1, maximumBlockSize);
        scratch.setSize (2, capacity, false, true, false);
        relativeScratch.setSize (2, capacity, false, true, false);

        juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) capacity, 1 };
        for (auto& filter : filters)
        {
            *filter.coefficients = juce::dsp::IIR::ArrayCoefficients<float>::makeBandPass (sampleRate, 1000.0f, 0.7f);
            filter.prepare (spec);
            filter.reset();
        }
        for (auto& filter : relativeFilters)
        {
            *filter.coefficients = juce::dsp::IIR::ArrayCoefficients<float>::makeBandPass (sampleRate, 1000.0f, 0.7f);
            filter.prepare (spec);
            filter.reset();
        }

        lastFrequency = 1000.0f;
        lastQ = 0.7f;
        triggerReady = true;
        lastRelativeFrequency = 1000.0f;
        lastRelativeQ = 0.7f;
        relativeReady = true;
        envelope = 0.0f;
        fastEnvelope = 0.0f;
        gainLevel = 0.0f;
        reference = 0.0f;
    }

    void setTriggerFilter (double freq, double q)
    {
        setBandPass (filters, lastFrequency, lastQ, triggerReady, freq, q);
    }

    void setRelativeFilter (double freq, double q)
    {
        setBandPass (relativeFilters, lastRelativeFrequency, lastRelativeQ, relativeReady, freq, q);
    }

    /** Advances the envelope by `numSamples` and returns the gain offset at the end of that span.
        `left` and `right` may alias. Only `numSamples` are read. */
    float advance (const float* left, const float* right, int numSamples, Mix detectMix,
                   const float* relativeLeft, const float* relativeRight, Mix relativeMix,
                   const Settings& settings)
    {
        float attackMs = settings.attackMs;
        float releaseMs = settings.releaseMs;
        float thresholdDb = settings.thresholdDb;
        if (settings.automatic)
        {
            attackMs = 8.0f;
            releaseMs = 140.0f;
        }

        if (left == nullptr || numSamples <= 0 || sampleRate <= 0.0 || scratch.getNumSamples() <= 0)
        {
            gainLevel = envelope;
            if (settings.automatic)
                thresholdDb = juce::jlimit (-60.0f, 0.0f, referenceDb() + 8.0f);
            lastThresholdDb = thresholdDb;
            return gainFor (thresholdDb, settings);
        }

        const int n = juce::jmin (numSamples, scratch.getNumSamples());
        const float* detectorRight = right != nullptr ? right : left;

        float* filteredA = scratch.getWritePointer (0);
        float* filteredB = scratch.getWritePointer (1);
        runMix (filters, detectMix, left, detectorRight, filteredA, filteredB, n);

        // The relative path has its own inputs, so External can read the sidechain while Detect reads the band.
        const float* relLeft = relativeLeft != nullptr ? relativeLeft : left;
        const float* relRight = relativeRight != nullptr ? relativeRight : (relativeLeft != nullptr ? relLeft : detectorRight);

        const bool useRelativeBand = settings.relativeFree;
        float* relA = relativeScratch.getWritePointer (0);
        float* relB = relativeScratch.getWritePointer (1);
        if (useRelativeBand)
            runMix (relativeFilters, relativeMix, relLeft, relRight, relA, relB, n);

        const float attackCoeff = coeffForMs (attackMs);
        const float releaseCoeff = coeffForMs (releaseMs);
        const float fastAttackCoeff = coeffForMs (1.0f);
        const float refCoeff = coeffForMs (80.0f);

        for (int i = 0; i < n; ++i)
        {
            const float level = levelOf (detectMix, filteredA[i], filteredB[i]);

            const float follow = level > envelope ? attackCoeff : releaseCoeff;
            envelope = level + follow * (envelope - level);
            const float fastFollow = level > fastEnvelope ? fastAttackCoeff : releaseCoeff;
            fastEnvelope = level + fastFollow * (fastEnvelope - level);

            const float referenceLevel = useRelativeBand
                ? levelOf (relativeMix, relA[i], relB[i])
                : broadbandLevel (relativeMix, relLeft[i], relRight[i]);
            reference = referenceLevel + refCoeff * (reference - referenceLevel);
        }

        gainLevel = envelope;
        if (settings.transientPercent > 0.05f)
        {
            const float blend = juce::jlimit (0.0f, 1.0f, settings.transientPercent / 100.0f);
            gainLevel = juce::jmax (0.0f, envelope + blend * (fastEnvelope - envelope));
        }
        if (settings.automatic)
            thresholdDb = juce::jlimit (-60.0f, 0.0f, referenceDb() + 8.0f);

        lastThresholdDb = thresholdDb;
        return gainFor (thresholdDb, settings);
    }

    float getEnvelopeDb() const noexcept { return envelopeDb(); }
    float getLastThresholdDb() const noexcept { return lastThresholdDb; }

    float envelopeDb() const noexcept
    {
        return juce::Decibels::gainToDecibels (envelope, -100.0f);
    }

    float referenceDb() const noexcept
    {
        return juce::jmax (-80.0f, juce::Decibels::gainToDecibels (reference, -100.0f));
    }

private:
    /** Redesigns one band-pass bank when its frequency or Q changes. Called from the audio thread. */
    void setBandPass (std::array<juce::dsp::IIR::Filter<float>, 2>& bank, float& storedFrequency, float& storedQ,
                      bool& isReady, double freq, double q) noexcept
    {
        if (sampleRate <= 0.0 || bank[0].coefficients == nullptr)
            return;

        const float frequency = (float) freq;
        const float qValue = (float) q;
        const auto sameBits = [] (float a, float b) noexcept
        {
            return std::memcmp (&a, &b, sizeof (float)) == 0;
        };
        if (isReady && sameBits (frequency, storedFrequency) && sameBits (qValue, storedQ))
            return;

        const auto coeffs = juce::dsp::IIR::ArrayCoefficients<float>::makeBandPass (sampleRate, frequency, qValue);
        for (auto& filter : bank)
        {
            *filter.coefficients = coeffs;
            filter.reset();
        }
        storedFrequency = frequency;
        storedQ = qValue;
        isReady = true;
    }

    void runMix (std::array<juce::dsp::IIR::Filter<float>, 2>& bank, Mix mix, const float* left, const float* right,
                 float* a, float* b, int n)
    {
        if (mix == Mix::Mid || mix == Mix::Side)
        {
            for (int i = 0; i < n; ++i)
            {
                const float mid = 0.5f * (left[i] + right[i]);
                const float side = 0.5f * (left[i] - right[i]);
                a[i] = mix == Mix::Mid ? mid : side;
            }
            runFilter (bank, 0, a, n);
        }
        else if (mix == Mix::Left)
        {
            std::memcpy (a, left, sizeof (float) * (size_t) n);
            runFilter (bank, 0, a, n);
        }
        else if (mix == Mix::Right)
        {
            std::memcpy (a, right, sizeof (float) * (size_t) n);
            runFilter (bank, 0, a, n);
        }
        else
        {
            std::memcpy (a, left, sizeof (float) * (size_t) n);
            std::memcpy (b, right, sizeof (float) * (size_t) n);
            runFilter (bank, 0, a, n);
            runFilter (bank, 1, b, n);
        }
    }

    static float levelOf (Mix mix, float a, float b) noexcept
    {
        if (mix == Mix::Energy)
            return std::sqrt (0.5f * (a * a + b * b));
        return std::abs (a);
    }

    /** Unfiltered level of a channel choice, for a relative reference without its own band-pass. */
    static float broadbandLevel (Mix mix, float l, float r) noexcept
    {
        switch (mix)
        {
            case Mix::Left:  return std::abs (l);
            case Mix::Right: return std::abs (r);
            case Mix::Mid:   return std::abs (0.5f * (l + r));
            case Mix::Side:  return std::abs (0.5f * (l - r));
            case Mix::Energy:
            default:         return std::sqrt (0.5f * (l * l + r * r));
        }
    }

    static void runFilter (std::array<juce::dsp::IIR::Filter<float>, 2>& bank, int index, float* data, int numSamples)
    {
        juce::dsp::AudioBlock<float> block (&data, 1, (size_t) numSamples);
        juce::dsp::ProcessContextReplacing<float> context (block);
        bank[(size_t) index].process (context);
    }

    float coeffForMs (float ms) const noexcept
    {
        const float samples = juce::jmax (1.0f, (float) sampleRate * juce::jmax (0.05f, ms) / 1000.0f);
        return std::exp (-1.0f / samples);
    }

    float gainFor (float thresholdDb, const Settings& settings) const noexcept
    {
        const float bandDb = juce::Decibels::gainToDecibels (gainLevel, -100.0f);
        const float rawReference = juce::Decibels::gainToDecibels (reference, -100.0f);
        // Silence floor: relative detection must not expand a quiet passage.
        if (rawReference < -70.0f && bandDb < -70.0f)
            return 0.0f;

        const float blend = juce::jlimit (0.0f, 1.0f, settings.relativePercent / 100.0f);
        const float relativeDb = bandDb - referenceDb();
        const float used = bandDb + blend * (relativeDb - bandDb);

        if (settings.modern)
        {
            // One signed compression curve around the threshold. Above it, Above applies; below it, Below applies.
            const float signedDepth = softKnee (used - thresholdDb, settings.kneeDb)
                                    - softKnee (thresholdDb - used, settings.kneeDb);
            if (signedDepth > 0.0f)
                return settings.aboveOn ? limitedOffset (signedDepth * settings.aboveRatio, settings.aboveRangeDb) : 0.0f;
            if (signedDepth < 0.0f)
                return settings.belowOn ? limitedOffset (-signedDepth * settings.belowRatio, settings.belowRangeDb) : 0.0f;
            return 0.0f;
        }

        const float region = settings.action == 0 ? (used - thresholdDb) : (thresholdDb - used);
        const float depth = softKnee (region, settings.kneeDb);
        if (depth <= 0.0f)
            return 0.0f;
        if (settings.rangeDb >= 0.0f)
            return juce::jmin (depth, settings.rangeDb);
        return juce::jmax (-depth, settings.rangeDb);
    }

    /** Dynamics rule: the offset is the depth scaled by the ratio, limited by the range, with the range's sign. */
    static float limitedOffset (float depthDb, float rangeDb) noexcept
    {
        const float magnitude = juce::jmin (juce::jmax (0.0f, depthDb), std::abs (rangeDb));
        return rangeDb < 0.0f ? -magnitude : magnitude;
    }

    static float softKnee (float region, float kneeDb) noexcept
    {
        const float knee = juce::jmax (0.0f, kneeDb);
        if (knee <= 0.05f)
            return juce::jmax (0.0f, region);
        const float half = 0.5f * knee;
        if (region <= -half)
            return 0.0f;
        if (region >= half)
            return region;
        const float t = (region + half) / knee;
        return t * t * half;
    }

    double sampleRate = 44100.0;
    float envelope = 0.0f;
    float fastEnvelope = 0.0f;
    float gainLevel = 0.0f;
    float lastThresholdDb = -24.0f;
    float reference = 0.0f;
    float lastFrequency = -1.0f;
    float lastQ = -1.0f;
    bool triggerReady = false;
    float lastRelativeFrequency = -1.0f;
    float lastRelativeQ = -1.0f;
    bool relativeReady = false;
    std::array<juce::dsp::IIR::Filter<float>, 2> filters;
    std::array<juce::dsp::IIR::Filter<float>, 2> relativeFilters;
    juce::AudioBuffer<float> scratch;
    juce::AudioBuffer<float> relativeScratch;
};
