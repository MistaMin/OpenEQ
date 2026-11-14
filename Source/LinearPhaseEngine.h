#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <complex>
#include <vector>
#include "ResponseModel.h"

/** Static linear-phase convolution.

    The worker designs an immutable kernel from a copied CurveRequest. The audio
    thread never calls into live filter objects and never calls loadImpulseResponse.
    Kernels are preallocated. A kernel swap is a published buffer index.

    What this mode is:
    - minimum-phase magnitude sampled on a real FFT, rotated to a symmetric IR,
      windowed with an un-normalised Blackman-Harris, then scaled so a flat
      curve stays unity
    - one shared delay for every path
    - stereo uses a 2×2 series matrix (left, right, mid, side)
    - mono uses one kernel that includes every included band

    What it deliberately does not do:
    - dynamic gain (a dynamic band is left out of the kernel)
    - all-pass phase (all-pass is left out; magnitude-only FIR cannot hold it)
    - a clickless quality change when the FFT size itself changes

    A new kernel of the same length replaces the previous one without clearing
    the audible overlap. The new convolution runs for one kernel of input while
    the previous kernel stays audible, then the outputs crossfade for 512 samples. */
class LinearPhaseEngine : private juce::Thread
{
public:
    struct CurveBand
    {
        FilterType type = FilterType::Bell;
        StereoMode placement = StereoMode::Stereo;
        float pan = 0.0f;
        float frequency = 1000.0f;
        float gainDb = 0.0f;
        float q = 1.0f;
        int slopeDb = 12;
        float phase = 0.0f; // free phase: -1 inverse, 0 linear, +1 minimum
        bool audible = false;
    };

    /** What the kernel is designed to do. Linear is the magnitude response, zero-phase and symmetric.
        Analog is the same magnitude taken from the analogue prototype (a 16x design, which removes the bilinear warp).
        Mixed is minimum-phase below the crossover and linear-phase above it. */
    enum class Target { Linear = 0, Analog, Mixed, AnalogZeroLatency };

    struct CurveRequest
    {
        double sampleRate = 44100.0;
        int fftOrder = 12;
        int count = 0;
        bool mono = false;
        Target target = Target::Linear;
        double crossoverHz = 1000.0;
        std::array<CurveBand, ResponseModel::graphBandCount> bands {};
    };

    LinearPhaseEngine();
    ~LinearPhaseEngine() override;

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset() noexcept;
    void process (juce::dsp::AudioBlock<float>& block);

    /** Copies the request if the worker is not reading it. Never waits, never allocates. */
    bool trySubmit (const CurveRequest& request);

    void setExpectedOrder (int fftOrder) noexcept;
    /** FIR window: shape (0 Blackman-Harris, 1 Kaiser, 2 Hann, 3 Hamming, 4 Blackman, 5 rectangular)
        and a tightening parameter 0..1 (Kaiser beta, or a skirt exponent for the other shapes). */
    void setWindowSettings (int shape, float parameter);
    /** Impulse padding: 0 keeps half the windowed impulse, 1 three quarters, 2 the full kernel. */
    void setImpulsePadding (int padding);

    int getLatencySamples() const noexcept;
    /** Target of the kernel now published, as a Target value. It changes when the worker finishes a design. */
    int getPublishedTarget() const noexcept { return publishedTarget.load(); }
    juce::uint64 getCompletedDesigns() const noexcept { return completedDesigns.load(); }

private:
    static constexpr int qualityCount = 4;
    static constexpr int maxKernelOrder = 14;
    static constexpr int maxKernel = 1 << maxKernelOrder;
    static constexpr int maxFft = maxKernel * 2;
    static constexpr int pathCount = 4;
    static constexpr int identitySlots = qualityCount;
    static constexpr int workerSlots = 2;
    static constexpr int slotCount = identitySlots + workerSlots;
    static constexpr int crossfadeSamples = 512;

    struct Kernel
    {
        int order = 12;
        int kernelLength = 2048;
        bool mono = false;
        bool valid = false;
        std::vector<juce::dsp::Complex<float>> spectrum;
    };

    void run() override;
    void buildIdentityKernels();
    void buildWindows();
    void designKernel (const CurveRequest& request, Kernel& dest);
    void renderRealSpectrum (const double* bins, int kernelLength, int path, Kernel& dest, bool causal);
    void renderComplexSpectrum (const std::complex<double>* bins, int kernelLength, int path, Kernel& dest, bool causal);
    void renderTimeFromSpectrum (int kernelLength, int path, Kernel& dest, bool causal);
    void convolvePath (const float* input, float* output, int numSamples, bool accumulate,
                       const juce::dsp::Complex<float>* spectrum, float* overlap, int kernelLength,
                       juce::dsp::FFT& fft);
    int identityIndexForOrder (int order) const noexcept;
    int selectKernel (int kernelLength) const noexcept;

    juce::WaitableEvent rebuildEvent;
    juce::CriticalSection requestLock;
    juce::CriticalSection designLock;
    CurveRequest pendingRequest;
    bool requestPending = false;

    std::atomic<bool> dirty { false };
    std::atomic<bool> prepared { false };
    std::atomic<int> expectedOrder { 12 };
    std::atomic<int> publishedSlot { 0 };
    std::atomic<int> audioReading { -1 };
    std::atomic<juce::uint64> completedDesigns { 0 };
    int workerWriteSlot = identitySlots;

    double sampleRate = 44100.0;
    std::array<Kernel, slotCount> kernels;
    std::array<std::vector<float>, qualityCount> windows;
    int windowShape = 0;
    float windowParameter = 0.0f;
    int impulsePadding = 2;
    std::atomic<int> publishedTarget { 0 };
    std::array<std::unique_ptr<juce::dsp::FFT>, qualityCount> audioFft;
    std::array<std::unique_ptr<juce::dsp::FFT>, qualityCount> workerKernelFft;
    std::array<std::unique_ptr<juce::dsp::FFT>, qualityCount> workerPadFft;

    std::vector<juce::dsp::Complex<float>> timeBuffer;
    std::vector<juce::dsp::Complex<float>> freqBuffer;
    std::vector<juce::dsp::Complex<float>> audioTime;
    std::vector<juce::dsp::Complex<float>> audioFreq;
    std::vector<double> binLL, binLR, binRL, binRR;
    std::vector<std::complex<double>> complexLL, complexLR, complexRL, complexRR;
    std::array<std::vector<float>, pathCount> overlap;
    std::array<std::vector<float>, pathCount> overlapRetire;
    std::array<std::vector<float>, 2> channelCopy;
    std::array<std::vector<float>, 2> channelAccum;
    std::array<std::vector<float>, 2> channelFade;
    Kernel fadeKernel;
    int lastKernelLength = 0;
    int lastSlot = -1;
    int engageSamplesLeft = 0;
    int fadeSamplesLeft = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LinearPhaseEngine)
};
