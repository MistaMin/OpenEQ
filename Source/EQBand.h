#pragma once
#include <JuceHeader.h>
#include "ResponseModel.h"

/** Audio-thread filter state for one band.

    The biquad cascade runs in double precision, which is 64-bit internal processing. Coefficients
    are designed in double, so low-frequency and high-Q poles keep their accuracy. Samples enter and leave as float.
    Coefficient objects are allocated in prepare and updated in place. Callers outside the audio thread must not read
    or write this object while it is processing. */
class EQBand
{
public:
    static constexpr int maxStages = ResponseModel::maxStages;
    static constexpr int numLanes = 4; // lanes 0/1 = left/mid, right/side; lanes 2/3 = the split second path for those
    static constexpr int maxChunk = 512; // processChannel works in pieces of this size

    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        // Every stage is promoted to a biquad here so later slope changes only clear
        // state. First-order cuts are stored as biquads with the extra terms at zero.
        const auto unity = juce::dsp::IIR::ArrayCoefficients<double>::makePeakFilter (
            sampleRate > 0.0 ? sampleRate : 44100.0, 1000.0, 1.0, 1.0);

        for (int stage = 0; stage < maxStages; ++stage)
        {
            stages[0][(size_t) stage].prepare (spec);
            *stages[0][(size_t) stage].coefficients = unity;
            stages[0][(size_t) stage].reset();
            // Every lane shares the same coefficient set; lanes 2/3 carry the split second-path state.
            for (int lane = 1; lane < numLanes; ++lane)
            {
                stages[(size_t) lane][(size_t) stage].coefficients = stages[0][(size_t) stage].coefficients;
                stages[(size_t) lane][(size_t) stage].prepare (spec);
                stages[(size_t) lane][(size_t) stage].reset();
            }
        }

        for (auto& lane : scratch)
            lane.fill (0.0);

        coefficientsReady = false;
        appliedFrequency = -1.0f;
    }

    void resetState() noexcept
    {
        for (auto& lane : stages)
            for (auto& stage : lane)
                stage.reset();
        for (auto& lane : ddState)
            lane.fill (DDState {});
    }

    /** Snaps coefficients to this design. Used by tests and by the processor when values change.
        Returns false when the design matches the coefficients already installed. */
    bool applyIfChanged (FilterType newType, float newFrequency, float newGainDb, float newQ, int newSlopeDb)
    {
        if (sampleRate <= 0.0)
            return false;

        const float frequency = ResponseModel::clampFrequency (newFrequency, sampleRate);
        const float q = ResponseModel::clampQ (newQ);

        if (coefficientsReady
            && newType == appliedType
            && newSlopeDb == appliedSlope
            && ResponseModel::sameBits (frequency, appliedFrequency)
            && ResponseModel::sameBits (newGainDb, appliedGain)
            && ResponseModel::sameBits (q, appliedQ))
            return false;

        auto designed = ResponseModel::designBandT<double> (newType, frequency, newGainDb, q, newSlopeDb, sampleRate);
        int stagesToWrite = juce::jlimit (1, maxStages, designed.stageCount);

        // Digital+ : each step appends a phase-compensating first-order all-pass after
        // every section, fitted at design time. Higher steps widen the fit range, correcting further out.
        if (digitalPlusSteps > 0 && stagesToWrite < maxStages)
        {
            // Each step fits one more all-pass against the residual phase of the section plus the
            // all-pass stages fitted so far, so more steps monotonically improve the phase linearity.
            double existingD1[ResponseModel::maxStages] = {};
            double existingD2[ResponseModel::maxStages] = {};
            for (int i = 0; i < designed.stageCount && stagesToWrite < maxStages; ++i)
            {
                int existingCount = 0;
                for (int step = 0; step < digitalPlusSteps && stagesToWrite < maxStages; ++step)
                {
                    double d1 = 0.0, d2 = 0.0;
                    ResponseModel::digitalPlusAllPassCoefficients (
                        designed.raw[(size_t) i], designed.frequency, sampleRate, 0.5, 2.0,
                        existingD1, existingD2, existingCount, d1, d2);
                    existingD1[existingCount] = d1;
                    existingD2[existingCount] = d2;
                    ++existingCount;
                    designed.raw[(size_t) stagesToWrite] = { d2, d1, 1.0, 1.0, d1, d2 };
                    ++stagesToWrite;
                }
            }
        }

        const bool topologyChanged = ! coefficientsReady
            || newType != appliedType
            || stagesToWrite != activeStageCount
            || (ResponseModel::isCut (newType) && newSlopeDb != appliedSlope);

        for (int i = 0; i < stagesToWrite; ++i)
        {
            *stages[0][(size_t) i].coefficients = designed.raw[(size_t) i];
            // Normalised coefficients for the 117-bit path: b0, b1, b2, a1, a2 with a0 = 1.
            const auto& raw = designed.raw[(size_t) i];
            const double inv = std::abs (raw[3]) > 1.0e-300 ? 1.0 / raw[3] : 0.0;
            ddCoefficients[(size_t) i] = { raw[0] * inv, raw[1] * inv, raw[2] * inv, raw[4] * inv, raw[5] * inv };
        }

        activeStageCount = stagesToWrite;
        appliedType = newType;
        appliedSlope = newSlopeDb;
        appliedFrequency = designed.frequency;
        appliedGain = newGainDb;
        appliedQ = designed.q;
        coefficientsReady = true;

        // Biquad order is fixed in prepare, so this clears state and does not allocate.
        if (topologyChanged)
            resetState();

        return true;
    }

    void setParameters (FilterType newType, float newFrequency, float newGainDb, float newQ,
                        int newSlopeDbPerOct, StereoMode newStereoMode, bool newActive)
    {
        stereoMode = newStereoMode;
        active = newActive;
        applyIfChanged (newType, newFrequency, newGainDb, newQ, newSlopeDbPerOct);
    }

    void setStereoMode (StereoMode mode) noexcept { stereoMode = mode; }
    /** Pan position, -1..1. Used by Stereo (balance) and Mid Side (mid/side balance). */
    void setPan (float newPan) noexcept { pan = juce::jlimit (-1.0f, 1.0f, newPan); }
    float getPan() const noexcept { return pan; }

    /** 117-bit mode: the recursion keeps double-double state (ultra-high precision option).
        Switching it resets the filter state, which can click. */
    void setUltraPrecision (bool shouldUse) noexcept
    {
        if (shouldUse == ultra)
            return;
        ultra = shouldUse;
        resetState();
    }

    bool isUltraPrecision() const noexcept { return ultra; }
    /** Digital+ steps: how many phase-compensating all-pass stages follow each section (zero-latency path). */
    void setDigitalPlusSteps (int steps) noexcept { digitalPlusSteps = juce::jlimit (0, 4, steps); }
    int getDigitalPlusSteps() const noexcept { return digitalPlusSteps; }

    /** Processes one lane in place. Bypass mixing is the caller's responsibility. */
    void processChannel (float* data, int numSamples, int lane)
    {
        if (data == nullptr || numSamples <= 0 || ! coefficientsReady)
            return;

        const int laneIndex = juce::jlimit (0, numLanes - 1, lane);
        if (ultra)
        {
            processUltra (data, numSamples, laneIndex);
            return;
        }

        auto& work = scratch[(size_t) laneIndex];

        for (int offset = 0; offset < numSamples; offset += maxChunk)
        {
            const int count = juce::jmin (maxChunk, numSamples - offset);
            float* samples = data + offset;
            for (int i = 0; i < count; ++i)
                work[(size_t) i] = (double) samples[i];

            double* channel = work.data();
            juce::dsp::AudioBlock<double> block (&channel, 1, (size_t) count);
            juce::dsp::ProcessContextReplacing<double> context (block);

            for (int s = 0; s < activeStageCount; ++s)
                stages[(size_t) laneIndex][(size_t) s].process (context);

            for (int i = 0; i < count; ++i)
                samples[i] = (float) work[(size_t) i];
        }
    }

    float getMagnitudeForFrequency (double freq) const
    {
        if (! active || ! coefficientsReady || sampleRate <= 0.0)
            return 1.0f;

        double mag = 1.0;
        for (int i = 0; i < activeStageCount; ++i)
            mag *= stages[0][(size_t) i].coefficients->getMagnitudeForFrequency (freq, sampleRate);
        return (float) mag;
    }

    void exportStages (std::array<ResponseModel::NormalizedStage, maxStages>& dest, int& count) const noexcept
    {
        count = coefficientsReady ? activeStageCount : 0;
        for (int i = 0; i < count; ++i)
        {
            const auto& coeffs = *stages[0][(size_t) i].coefficients;
            const int n = juce::jmin (5, coeffs.coefficients.size());
            dest[(size_t) i] = {};
            dest[(size_t) i].order = (int) coeffs.getFilterOrder();
            const auto* raw = coeffs.getRawCoefficients();
            for (int k = 0; k < n; ++k)
                dest[(size_t) i].c[k] = (float) raw[k];
        }
    }

    bool isActive() const noexcept { return active; }
    FilterType getType() const noexcept { return appliedType; }
    float getFrequency() const noexcept { return appliedFrequency; }
    float getGain() const noexcept { return appliedGain; }
    float getQ() const noexcept { return appliedQ; }
    int getSlopeDb() const noexcept { return appliedSlope; }
    StereoMode getStereoMode() const noexcept { return stereoMode; }

private:
    /** Double-double value: hi + lo, with |lo| at most half an ulp of hi. */
    struct DD
    {
        double hi = 0.0;
        double lo = 0.0;
    };

    static DD quickTwoSum (double a, double b) noexcept
    {
        const double s = a + b;
        return { s, b - (s - a) };
    }

    static DD twoSum (double a, double b) noexcept
    {
        const double s = a + b;
        const double bb = s - a;
        return { s, (a - (s - bb)) + (b - bb) };
    }

    static DD add (DD a, DD b) noexcept
    {
        DD s = twoSum (a.hi, b.hi);
        s.lo += a.lo + b.lo;
        return quickTwoSum (s.hi, s.lo);
    }

    /** Double-double times a double. The product error comes from an exact fused multiply-add. */
    static DD scale (DD a, double b) noexcept
    {
        const double p = a.hi * b;
        const double e = std::fma (a.hi, b, -p);
        return quickTwoSum (p, e + a.lo * b);
    }

    void processUltra (float* data, int numSamples, int lane)
    {
        auto& z = ddState[(size_t) lane];
        for (int i = 0; i < numSamples; ++i)
        {
            DD x { (double) data[i], 0.0 };
            for (int s = 0; s < activeStageCount; ++s)
            {
                const auto& c = ddCoefficients[(size_t) s];
                auto& state = z[(size_t) s];
                // Transposed direct form II: y = b0 x + z1, z1 = b1 x - a1 y + z2, z2 = b2 x - a2 y.
                const DD y = add (scale (x, c[0]), state.z1);
                state.z1 = add (add (scale (x, c[1]), scale (y, -c[3])), state.z2);
                state.z2 = add (scale (x, c[2]), scale (y, -c[4]));
                x = y;
            }
            data[i] = (float) (x.hi + x.lo);
        }
    }

    struct DDState
    {
        DD z1;
        DD z2;
    };

    bool ultra = false;
    std::array<std::array<double, 5>, maxStages> ddCoefficients {};
    std::array<std::array<DDState, maxStages>, numLanes> ddState {};

    FilterType appliedType = FilterType::Bell;
    float appliedFrequency = -1.0f;
    float appliedGain = 0.0f;
    float appliedQ = 1.0f;
    int appliedSlope = 12;
    StereoMode stereoMode = StereoMode::Stereo;
    float pan = 0.0f;
    int activeStageCount = 1;
    bool active = true;
    bool coefficientsReady = false;
    double sampleRate = 44100.0;

    int digitalPlusSteps = 0;
    std::array<std::array<juce::dsp::IIR::Filter<double>, maxStages>, numLanes> stages;
    std::array<std::array<double, maxChunk>, numLanes> scratch {};
};
