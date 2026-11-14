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
        for (auto& lane : biquadState)
            lane.fill ({ 0.0, 0.0 });
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

            for (int s = 0; s < activeStageCount; ++s)
                runStage (stages[0][(size_t) s].coefficients->getRawCoefficients(),
                          biquadState[(size_t) laneIndex][(size_t) s], work.data(), count);

            for (int i = 0; i < count; ++i)
                samples[i] = (float) work[(size_t) i];
        }
    }

    /** Processes two different lanes in place in one pass (left/right, or mid/side). Each lane gets exactly
        the arithmetic processChannel would give it; running them side by side only lets the two
        recursions overlap. */
    void processChannelPair (float* dataA, float* dataB, int numSamples, int laneA, int laneB)
    {
        if (dataA == nullptr || dataB == nullptr || numSamples <= 0 || ! coefficientsReady)
            return;

        const int indexA = juce::jlimit (0, numLanes - 1, laneA);
        const int indexB = juce::jlimit (0, numLanes - 1, laneB);
        if (ultra || indexA == indexB)
        {
            processChannel (dataA, numSamples, indexA);
            processChannel (dataB, numSamples, indexB);
            return;
        }

        auto& workA = scratch[(size_t) indexA];
        auto& workB = scratch[(size_t) indexB];

        for (int offset = 0; offset < numSamples; offset += maxChunk)
        {
            const int count = juce::jmin (maxChunk, numSamples - offset);
            float* samplesA = dataA + offset;
            float* samplesB = dataB + offset;
            for (int i = 0; i < count; ++i)
            {
                workA[(size_t) i] = (double) samplesA[i];
                workB[(size_t) i] = (double) samplesB[i];
            }

            for (int s = 0; s < activeStageCount; ++s)
                runStagePair (stages[0][(size_t) s].coefficients->getRawCoefficients(),
                              biquadState[(size_t) indexA][(size_t) s], biquadState[(size_t) indexB][(size_t) s],
                              workA.data(), workB.data(), count);

            for (int i = 0; i < count; ++i)
            {
                samplesA[i] = (float) workA[(size_t) i];
                samplesB[i] = (float) workB[(size_t) i];
            }
        }
    }

    /** True when processChannelPair on lanes 0 and 1 would run the plain double-precision cascade, which is
        what processCascadePair reproduces. */
    bool canCascade() const noexcept { return coefficientsReady && ! ultra; }

    /** Runs several bands in series on lanes 0 and 1, with exactly the result of calling
        processChannelPair (dataA, dataB, numSamples, 0, 1) on each band in turn: every stage does the same
        arithmetic on the same inputs, a band's output is rounded to float before the next band reads it,
        and the denormal snap falls at the same block ends. The work is laid out as a wavefront (stage e
        works on sample t - e at step t), so the stages' recursions overlap instead of running one
        after another. Every band must satisfy canCascade(). */
    static void processCascadePair (EQBand* const* chain, int bandCount, float* dataA, float* dataB, int numSamples) noexcept
    {
        constexpr int capacity = 32; // stages per wavefront; any one band has at most maxStages (16)
        static_assert (capacity >= maxStages, "a single band must fit one wavefront");

        int first = 0;
        while (first < bandCount)
        {
            // Take as many whole bands as fit.
            int last = first, stageTotal = 0;
            while (last < bandCount && stageTotal + chain[last]->activeStageCount <= capacity)
                stageTotal += chain[last++]->activeStageCount;
            if (stageTotal > 0)
                for (int offset = 0; offset < numSamples; offset += maxChunk)
                    runWavefront (chain + first, last - first, stageTotal, dataA + offset, dataB + offset,
                                  juce::jmin (maxChunk, numSamples - offset));
            first = last;
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
    using BiquadState = std::array<double, 2>;

    /** One transposed direct form II biquad over a block, in place. The arithmetic, its order, and the
        end-of-block denormal snap are those of juce::dsp::IIR::Filter for a second-order section, so
        the output is the same bit for bit. c holds b0, b1, b2, a1, a2 with a0 = 1. */
    static void runStage (const double* c, BiquadState& z, double* x, int count) noexcept
    {
        const double b0 = c[0], b1 = c[1], b2 = c[2], a1 = c[3], a2 = c[4];
        double lv1 = z[0], lv2 = z[1];
        for (int i = 0; i < count; ++i)
        {
            const double input = x[i];
            const double output = (input * b0) + lv1;
            x[i] = output;
            lv1 = (input * b1) - (output * a1) + lv2;
            lv2 = (input * b2) - (output * a2);
        }
        juce::dsp::util::snapToZero (lv1); z[0] = lv1;
        juce::dsp::util::snapToZero (lv2); z[1] = lv2;
    }

    /** runStage on two lanes that share coefficients, interleaved so the two recursions run in parallel. */
    static void runStagePair (const double* c, BiquadState& zA, BiquadState& zB, double* xA, double* xB, int count) noexcept
    {
        const double b0 = c[0], b1 = c[1], b2 = c[2], a1 = c[3], a2 = c[4];
        double lv1A = zA[0], lv2A = zA[1];
        double lv1B = zB[0], lv2B = zB[1];
        for (int i = 0; i < count; ++i)
        {
            const double inputA = xA[i];
            const double inputB = xB[i];
            const double outputA = (inputA * b0) + lv1A;
            const double outputB = (inputB * b0) + lv1B;
            xA[i] = outputA;
            xB[i] = outputB;
            lv1A = (inputA * b1) - (outputA * a1) + lv2A;
            lv1B = (inputB * b1) - (outputB * a1) + lv2B;
            lv2A = (inputA * b2) - (outputA * a2);
            lv2B = (inputB * b2) - (outputB * a2);
        }
        juce::dsp::util::snapToZero (lv1A); zA[0] = lv1A;
        juce::dsp::util::snapToZero (lv2A); zA[1] = lv2A;
        juce::dsp::util::snapToZero (lv1B); zB[0] = lv1B;
        juce::dsp::util::snapToZero (lv2B); zB[1] = lv2B;
    }

    /** processCascadePair for at most 32 stages and at most maxChunk samples. Stage e handles sample t - e
        at step t; going through the stages from the last to the first, carry[e - 1] still holds what stage
        e - 1 produced one step earlier, which is the same sample. */
    static void runWavefront (EQBand* const* chain, int bandCount, int stageCount, float* dataA, float* dataB, int numSamples) noexcept
    {
        constexpr int capacity = 32;
        double b0[capacity], b1[capacity], b2[capacity], a1[capacity], a2[capacity];
        double lv1A[capacity], lv2A[capacity], lv1B[capacity], lv2B[capacity];
        double carryA[capacity], carryB[capacity];
        bool roundAfter[capacity];

        int e = 0;
        for (int b = 0; b < bandCount; ++b)
        {
            auto& band = *chain[b];
            for (int s = 0; s < band.activeStageCount; ++s, ++e)
            {
                const double* c = band.stages[0][(size_t) s].coefficients->getRawCoefficients();
                b0[e] = c[0]; b1[e] = c[1]; b2[e] = c[2]; a1[e] = c[3]; a2[e] = c[4];
                lv1A[e] = band.biquadState[0][(size_t) s][0]; lv2A[e] = band.biquadState[0][(size_t) s][1];
                lv1B[e] = band.biquadState[1][(size_t) s][0]; lv2B[e] = band.biquadState[1][(size_t) s][1];
                carryA[e] = carryB[e] = 0.0;
                // A band hands the next one a float sample, as processChannelPair stores it.
                roundAfter[e] = s == band.activeStageCount - 1;
            }
        }
        jassert (e == stageCount);
        const int lastStage = stageCount - 1;

        for (int t = 0; t < numSamples + lastStage; ++t)
        {
            const int high = juce::jmin (lastStage, t);
            const int low = juce::jmax (0, t - (numSamples - 1));
            for (int k = high; k >= low; --k)
            {
                const int n = t - k;
                const double inputA = k == 0 ? (double) dataA[n] : carryA[k - 1];
                const double inputB = k == 0 ? (double) dataB[n] : carryB[k - 1];
                const double outputA = (inputA * b0[k]) + lv1A[k];
                const double outputB = (inputB * b0[k]) + lv1B[k];
                lv1A[k] = (inputA * b1[k]) - (outputA * a1[k]) + lv2A[k];
                lv1B[k] = (inputB * b1[k]) - (outputB * a1[k]) + lv2B[k];
                lv2A[k] = (inputA * b2[k]) - (outputA * a2[k]);
                lv2B[k] = (inputB * b2[k]) - (outputB * a2[k]);
                if (roundAfter[k])
                {
                    const float roundedA = (float) outputA, roundedB = (float) outputB;
                    if (k == lastStage)
                    {
                        dataA[n] = roundedA;
                        dataB[n] = roundedB;
                    }
                    carryA[k] = (double) roundedA;
                    carryB[k] = (double) roundedB;
                }
                else
                {
                    carryA[k] = outputA;
                    carryB[k] = outputB;
                }
            }
        }

        e = 0;
        for (int b = 0; b < bandCount; ++b)
        {
            auto& band = *chain[b];
            for (int s = 0; s < band.activeStageCount; ++s, ++e)
            {
                juce::dsp::util::snapToZero (lv1A[e]); juce::dsp::util::snapToZero (lv2A[e]);
                juce::dsp::util::snapToZero (lv1B[e]); juce::dsp::util::snapToZero (lv2B[e]);
                band.biquadState[0][(size_t) s] = { lv1A[e], lv2A[e] };
                band.biquadState[1][(size_t) s] = { lv1B[e], lv2B[e] };
            }
        }
    }

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
    // The filters hold the installed coefficients (lane 0 owns them, the other lanes share the pointer).
    // The recursion state lives in biquadState, so a pair of lanes can be processed in one pass.
    std::array<std::array<juce::dsp::IIR::Filter<double>, maxStages>, numLanes> stages;
    std::array<std::array<BiquadState, maxStages>, numLanes> biquadState {};
    std::array<std::array<double, maxChunk>, numLanes> scratch {};
};
