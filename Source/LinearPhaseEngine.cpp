#include "LinearPhaseEngine.h"

namespace
{
    int qualityIndexForOrder (int order) noexcept
    {
        return juce::jlimit (0, 3, order - 11);
    }

    int kernelLengthForOrder (int order) noexcept
    {
        return 1 << juce::jlimit (11, 14, order);
    }
}

LinearPhaseEngine::LinearPhaseEngine() : juce::Thread ("OpenEQ IR Builder")
{
    startThread (juce::Thread::Priority::low);
}

LinearPhaseEngine::~LinearPhaseEngine()
{
    stopThread (4000);
}

void LinearPhaseEngine::prepare (const juce::dsp::ProcessSpec& spec)
{
    const juce::ScopedLock lock (designLock);
    sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;

    for (int q = 0; q < qualityCount; ++q)
    {
        const int kernelOrder = 11 + q;
        if (workerKernelFft[(size_t) q] == nullptr)
            workerKernelFft[(size_t) q] = std::make_unique<juce::dsp::FFT> (kernelOrder);
        if (workerPadFft[(size_t) q] == nullptr)
            workerPadFft[(size_t) q] = std::make_unique<juce::dsp::FFT> (kernelOrder + 1);
        if (audioFft[(size_t) q] == nullptr)
            audioFft[(size_t) q] = std::make_unique<juce::dsp::FFT> (kernelOrder + 1);

    }

    buildWindows();

    const auto ensureComplex = [] (std::vector<juce::dsp::Complex<float>>& buffer)
    {
        if ((int) buffer.size() != maxFft)
            buffer.assign ((size_t) maxFft, {});
    };
    ensureComplex (timeBuffer);
    ensureComplex (freqBuffer);
    ensureComplex (audioTime);
    ensureComplex (audioFreq);

    if ((int) binLL.size() != maxKernel / 2 + 1)
    {
        binLL.assign ((size_t) maxKernel / 2 + 1, 0.0);
        binLR.assign ((size_t) maxKernel / 2 + 1, 0.0);
        binRL.assign ((size_t) maxKernel / 2 + 1, 0.0);
        binRR.assign ((size_t) maxKernel / 2 + 1, 0.0);
        complexLL.assign ((size_t) maxKernel / 2 + 1, {});
        complexLR.assign ((size_t) maxKernel / 2 + 1, {});
        complexRL.assign ((size_t) maxKernel / 2 + 1, {});
        complexRR.assign ((size_t) maxKernel / 2 + 1, {});
    }

    for (auto& kernel : kernels)
        if ((int) kernel.spectrum.size() != pathCount * maxFft)
            kernel.spectrum.assign ((size_t) pathCount * maxFft, {});

    for (auto& buffer : overlap)
        if ((int) buffer.size() != maxKernel)
            buffer.assign ((size_t) maxKernel, 0.0f);
    for (auto& buffer : overlapRetire)
        if ((int) buffer.size() != maxKernel)
            buffer.assign ((size_t) maxKernel, 0.0f);

    for (auto& buffer : channelCopy)
        if ((int) buffer.size() != 4096)
            buffer.assign (4096, 0.0f);
    for (auto& buffer : channelAccum)
        if ((int) buffer.size() != 4096)
            buffer.assign (4096, 0.0f);
    for (auto& buffer : channelFade)
        if ((int) buffer.size() != 4096)
            buffer.assign (4096, 0.0f);
    if ((int) fadeKernel.spectrum.size() != pathCount * maxFft)
        fadeKernel.spectrum.assign ((size_t) pathCount * maxFft, {});

    buildIdentityKernels();
    reset();
    publishedSlot.store (identityIndexForOrder (expectedOrder.load()), std::memory_order_release);
    prepared.store (true);
}

void LinearPhaseEngine::buildWindows()
{
    for (int q = 0; q < qualityCount; ++q)
    {
        const int kernelLength = 1 << (11 + q);
        windows[(size_t) q].assign ((size_t) kernelLength, 1.0f);

        switch (windowShape)
        {
            case 1: // Kaiser, beta from the parameter (1 .. 14)
            {
                const double beta = 1.0 + 13.0 * juce::jlimit (0.0f, 1.0f, windowParameter);
                const int half = kernelLength / 2;
                auto besselI0 = [] (double x)
                {
                    double sum = 1.0, term = 1.0;
                    for (int k = 1; k < 30; ++k)
                    {
                        term *= (x * x) / (4.0 * (double) k * (double) k);
                        sum += term;
                        if (term < 1.0e-15 * sum) break;
                    }
                    return sum;
                };
                const double denom = besselI0 (beta);
                for (int i = 0; i < kernelLength; ++i)
                {
                    const double x = 2.0 * (double) (i - half) / (double) kernelLength;
                    windows[(size_t) q][(size_t) i] = (float) (besselI0 (beta * std::sqrt (juce::jmax (0.0, 1.0 - x * x))) / denom);
                }
                break;
            }
            default:
            {
                auto kind = juce::dsp::WindowingFunction<float>::blackmanHarris;
                if (windowShape == 2) kind = juce::dsp::WindowingFunction<float>::hann;
                else if (windowShape == 3) kind = juce::dsp::WindowingFunction<float>::hamming;
                else if (windowShape == 4) kind = juce::dsp::WindowingFunction<float>::blackman;
                else if (windowShape == 5) kind = juce::dsp::WindowingFunction<float>::rectangular;
                juce::dsp::WindowingFunction<float> window ((size_t) kernelLength, kind, false);
                window.multiplyWithWindowingTable (windows[(size_t) q].data(), (size_t) kernelLength);
                // The parameter raises the skirt (tightens the impulse response, less preringing).
                const double exponent = 1.0 + 3.0 * juce::jlimit (0.0f, 1.0f, windowParameter);
                for (int i = 0; i < kernelLength; ++i)
                    windows[(size_t) q][(size_t) i] = std::pow (juce::jmax (windows[(size_t) q][(size_t) i], 1.0e-12f), (float) exponent);
                break;
            }
        }
    }
}

void LinearPhaseEngine::setWindowSettings (int shape, float parameter)
{
    const juce::ScopedLock lock (designLock);
    windowShape = juce::jlimit (0, 5, shape);
    windowParameter = juce::jlimit (0.0f, 1.0f, parameter);
    buildWindows();
}

void LinearPhaseEngine::setImpulsePadding (int padding)
{
    const juce::ScopedLock lock (designLock);
    impulsePadding = juce::jlimit (0, 2, padding);
}

void LinearPhaseEngine::reset() noexcept
{
    for (auto& buffer : overlap)
        std::fill (buffer.begin(), buffer.end(), 0.0f);
    for (auto& buffer : overlapRetire)
        std::fill (buffer.begin(), buffer.end(), 0.0f);
    lastKernelLength = 0;
    lastSlot = -1;
    engageSamplesLeft = 0;
    fadeSamplesLeft = 0;
    fadeKernel.valid = false;
}

void LinearPhaseEngine::setExpectedOrder (int fftOrder) noexcept
{
    expectedOrder.store (juce::jlimit (11, 14, fftOrder));
}

int LinearPhaseEngine::getLatencySamples() const noexcept
{
    // Zero-latency analogue kernels are causal (the impulse starts at sample 0): no delay is reported.
    if (publishedTarget.load() == (int) Target::AnalogZeroLatency)
        return 0;
    return kernelLengthForOrder (expectedOrder.load()) / 2;
}

int LinearPhaseEngine::identityIndexForOrder (int order) const noexcept
{
    return qualityIndexForOrder (order);
}

int LinearPhaseEngine::selectKernel (int kernelLength) const noexcept
{
    const int published = publishedSlot.load (std::memory_order_acquire);
    if (published >= 0 && published < slotCount
        && kernels[(size_t) published].valid
        && kernels[(size_t) published].kernelLength == kernelLength)
        return published;

    return identityIndexForOrder (expectedOrder.load());
}

bool LinearPhaseEngine::trySubmit (const CurveRequest& request)
{
    if (! prepared.load() || ! requestLock.tryEnter())
        return false;

    pendingRequest = request;
    requestPending = true;
    requestLock.exit();

    expectedOrder.store (juce::jlimit (11, 14, request.fftOrder));
    publishedTarget.store ((int) request.target);
    dirty.store (true);
    rebuildEvent.signal();
    return true;
}

void LinearPhaseEngine::run()
{
    while (! threadShouldExit())
    {
        rebuildEvent.wait (200);
        if (threadShouldExit())
            return;
        if (! dirty.load() || ! prepared.load())
            continue;

        CurveRequest request;
        {
            const juce::ScopedLock lock (requestLock);
            if (! requestPending)
            {
                dirty.store (false);
                continue;
            }
            request = pendingRequest;
            requestPending = false;
        }

        const juce::ScopedLock lock (designLock);
        if (threadShouldExit() || ! prepared.load())
            continue;

        const int published = publishedSlot.load();
        const int reading = audioReading.load();
        int slot = -1;
        for (int candidate = identitySlots; candidate < slotCount; ++candidate)
            if (candidate != published && candidate != reading)
                slot = candidate;

        if (slot < 0)
        {
            dirty.store (true);
            continue;
        }

        designKernel (request, kernels[(size_t) slot]);
        publishedSlot.store (slot, std::memory_order_release);
        completedDesigns.fetch_add (1, std::memory_order_release);
        const juce::ScopedLock relock (requestLock);
        if (! requestPending)
            dirty.store (false);
    }
}

void LinearPhaseEngine::buildIdentityKernels()
{
    for (int q = 0; q < qualityCount; ++q)
    {
        const int kernelLength = 1 << (11 + q);
        const int bins = kernelLength / 2 + 1;
        std::fill (binLL.begin(), binLL.begin() + bins, 1.0);
        std::fill (binLR.begin(), binLR.begin() + bins, 0.0);
        std::fill (binRL.begin(), binRL.begin() + bins, 0.0);
        std::fill (binRR.begin(), binRR.begin() + bins, 1.0);

        auto& kernel = kernels[(size_t) q];
        kernel.order = 11 + q;
        kernel.kernelLength = kernelLength;
        kernel.mono = false;
        renderRealSpectrum (binLL.data(), kernelLength, 0, kernel, false);
        renderRealSpectrum (binLR.data(), kernelLength, 1, kernel, false);
        renderRealSpectrum (binRL.data(), kernelLength, 2, kernel, false);
        renderRealSpectrum (binRR.data(), kernelLength, 3, kernel, false);
        kernel.valid = true;
    }
}

namespace
{
    /** Crossover weight for the mixed target: 1 well below fc, 0 well above it, one octave of cosine transition. */
    double mixedCrossoverWeight (double frequency, double crossoverHz) noexcept
    {
        const double octaves = std::log2 (juce::jmax (frequency, 1.0) / juce::jmax (crossoverHz, 1.0));
        const double t = juce::jlimit (-0.5, 0.5, octaves);
        return 0.5 * (1.0 + std::cos (juce::MathConstants<double>::pi * (t + 0.5)));
    }

    /** Complex response of one normalised biquad stage at a frequency. The denominator uses a1, a2 as stored. */
    std::complex<double> stageResponse (const ResponseModel::NormalizedStage& stage, double frequency, double sampleRate) noexcept
    {
        const double w = juce::MathConstants<double>::twoPi * frequency / sampleRate;
        const std::complex<double> z1 = std::polar (1.0, -w);
        const std::complex<double> z2 = z1 * z1;
        const std::complex<double> numerator = (double) stage.c[0] + (double) stage.c[1] * z1 + (double) stage.c[2] * z2;
        const std::complex<double> denominator = 1.0 + (double) stage.c[3] * z1 + (double) stage.c[4] * z2;
        return numerator / denominator;
    }
}

void LinearPhaseEngine::designKernel (const CurveRequest& request, Kernel& dest)
{
    const int order = juce::jlimit (11, 14, request.fftOrder);
    const int kernelLength = 1 << order;
    const int bins = kernelLength / 2 + 1;
    dest.order = order;
    dest.kernelLength = kernelLength;
    dest.mono = request.mono;
    dest.valid = false;

    if (request.sampleRate <= 0.0)
        return;

    // The analog target designs every band at 16 times the rate. At that rate the bilinear warp is negligible,
    // so the magnitude approximates the analogue prototype (reference analogue prototype mode).
    const bool causal = request.target == Target::AnalogZeroLatency;
    const double designRate = (request.target == Target::Analog || causal) ? request.sampleRate * 16.0 : request.sampleRate;

    (void) causal;

    struct PreparedBand
    {
        bool include = false;
        StereoMode placement = StereoMode::Stereo;
        float pan = 0.0f;
        int stageCount = 0;
        std::array<ResponseModel::NormalizedStage, ResponseModel::maxStages> stages {};
    };

    std::array<PreparedBand, ResponseModel::graphBandCount> preparedBands {};
    const int bandCount = juce::jmin (request.count, ResponseModel::graphBandCount);

    // The mixed target and any free-phase band need complex responses. Each band's routing matrix is complex,
    // so L, R, mid, side and panned bands are mixed too. Mono has one channel and is handled by the real path.
    bool anyFreePhase = false;
    for (const auto& band : request.bands)
        if (band.audible && (std::abs (band.phase) > 1.0e-6f || causal))
            anyFreePhase = true;
    // The zero-latency target is the minimum-phase analogue response, rendered causally.
    bool complexAllowed = anyFreePhase || (request.target == Target::Mixed && ! request.mono);

    for (int i = 0; i < bandCount; ++i)
    {
        const auto& band = request.bands[(size_t) i];
        if (! band.audible || ! ResponseModel::supportedInLinearPhase (band.type))
            continue;

        const auto designed = ResponseModel::designBand (band.type, band.frequency, band.gainDb,
                                                          band.q, band.slopeDb, designRate);
        auto& destBand = preparedBands[(size_t) i];
        destBand.include = designed.stageCount > 0;
        destBand.placement = band.placement;
        destBand.pan = band.pan;
        destBand.stageCount = designed.stageCount;
        for (int stage = 0; stage < designed.stageCount; ++stage)
            destBand.stages[(size_t) stage] = ResponseModel::normalizeBiquad (designed.raw[(size_t) stage]);

    }

    for (int bin = 0; bin < bins; ++bin)
    {
        const double frequency = juce::jmax (1.0, bin * request.sampleRate / (double) kernelLength);
        if (request.mono && ! complexAllowed)
        {
            double gain = 1.0;
            for (const auto& band : preparedBands)
            {
                if (! band.include)
                    continue;
                for (int stage = 0; stage < band.stageCount; ++stage)
                    gain *= ResponseModel::stageMagnitude (band.stages[(size_t) stage], frequency, designRate);
            }
            binLL[(size_t) bin] = gain;
            binLR[(size_t) bin] = 0.0;
            binRL[(size_t) bin] = 0.0;
            binRR[(size_t) bin] = gain;
        }
        else if (request.mono)
        {
            // Mono with free phase or the zero-latency target: one complex response shared by all paths.
            using Complex = std::complex<double>;
            Complex ll (1.0, 0.0);
            for (int i = 0; i < bandCount; ++i)
            {
                const auto& prepared = preparedBands[(size_t) i];
                if (! prepared.include)
                    continue;
                Complex response (1.0, 0.0);
                for (int stage = 0; stage < prepared.stageCount; ++stage)
                    response *= stageResponse (prepared.stages[(size_t) stage], frequency, designRate);
                if (causal || std::abs (request.bands[(size_t) i].phase) > 1.0e-6f)
                {
                    const double magnitude = std::abs (response);
                    const double rotation = causal ? 1.0 : juce::jlimit (-1.0, 1.0, (double) request.bands[(size_t) i].phase);
                    response = std::pow (response, rotation) * std::pow (juce::jmax (magnitude, 1.0e-12), 1.0 - rotation);
                }
                ll *= response;
            }
            complexLL[(size_t) bin] = ll;
            // The mono complex path renders the first complex path into every channel below.
        }
        else
        {
            ResponseModel::RealMatrix matrix;
            for (const auto& band : preparedBands)
            {
                if (! band.include)
                    continue;
                double gain = 1.0;
                for (int stage = 0; stage < band.stageCount; ++stage)
                    gain *= ResponseModel::stageMagnitude (band.stages[(size_t) stage], frequency, designRate);
                matrix = ResponseModel::multiply (ResponseModel::bandMatrix (gain, band.placement, band.pan), matrix);
            }
            binLL[(size_t) bin] = matrix.ll;
            binLR[(size_t) bin] = matrix.lr;
            binRL[(size_t) bin] = matrix.rl;
            binRR[(size_t) bin] = matrix.rr;
        }

        if (complexAllowed)
        {
            // Mixed: each band's minimum-phase response times its routing matrix, multiplied in the same order as the
            // real matrix. The result is blended with the linear magnitude matrix by the crossover.
            using Complex = std::complex<double>;
            Complex ll (1.0, 0.0), lr (0.0, 0.0), rl (0.0, 0.0), rr (1.0, 0.0);
            for (int i = 0; i < bandCount; ++i)
            {
                const auto& prepared = preparedBands[(size_t) i];
                if (! prepared.include)
                    continue;
                const float bandPhase = causal ? 1.0f : request.bands[(size_t) i].phase;
                Complex response (1.0, 0.0);
                for (int stage = 0; stage < prepared.stageCount; ++stage)
                    response *= stageResponse (prepared.stages[(size_t) stage], frequency, designRate);

                // Free phase: rotate the band's minimum-phase response. p = 0 stays linear (real),
                // p = +1 keeps the minimum-phase response, p = -1 conjugates it (inverse phase).
                // The zero-latency target forces p = +1 (a causal minimum-phase kernel).
                if (std::abs (bandPhase) > 1.0e-6f)
                {
                    const double magnitude = std::abs (response);
                    const double rotation = juce::jlimit (-1.0, 1.0, (double) bandPhase);
                    response = std::pow (response, rotation) * std::pow (juce::jmax (magnitude, 1.0e-12), 1.0 - rotation);
                }

                const auto routing = ResponseModel::bandMatrix (1.0, prepared.placement, prepared.pan);
                const Complex bll = response * routing.ll;
                const Complex blr = response * routing.lr;
                const Complex brl = response * routing.rl;
                const Complex brr = response * routing.rr;

                // New matrix = band x previous matrix, as in the real path.
                const Complex nll = bll * ll + blr * rl;
                const Complex nlr = bll * lr + blr * rr;
                const Complex nrl = brl * ll + brr * rl;
                const Complex nrr = brl * lr + brr * rr;
                ll = nll;
                lr = nlr;
                rl = nrl;
                rr = nrr;
            }

            // Pure free-phase modes take the complex response everywhere; only the mixed target crossfades it.
            const double weight = request.target == Target::Mixed
                ? mixedCrossoverWeight (frequency, request.crossoverHz) : 1.0;
            complexLL[(size_t) bin] = weight * ll + (1.0 - weight) * binLL[(size_t) bin];
            complexLR[(size_t) bin] = weight * lr + (1.0 - weight) * binLR[(size_t) bin];
            complexRL[(size_t) bin] = weight * rl + (1.0 - weight) * binRL[(size_t) bin];
            complexRR[(size_t) bin] = weight * rr + (1.0 - weight) * binRR[(size_t) bin];
        }
    }

    if (complexAllowed && request.mono)
    {
        // Pack the mono complex response into the first path; the other paths stay identity-real.
        complexLR.assign (complexLR.size(), {});
        complexRL.assign (complexRL.size(), {});
        renderComplexSpectrum (complexLL.data(), kernelLength, 0, dest, causal);
        renderComplexSpectrum (complexLL.data(), kernelLength, 1, dest, causal);
        renderComplexSpectrum (complexLL.data(), kernelLength, 2, dest, causal);
        renderComplexSpectrum (complexLL.data(), kernelLength, 3, dest, causal);
    }
    else if (complexAllowed)
    {
        renderComplexSpectrum (complexLL.data(), kernelLength, 0, dest, causal);
        renderComplexSpectrum (complexLR.data(), kernelLength, 1, dest, causal);
        renderComplexSpectrum (complexRL.data(), kernelLength, 2, dest, causal);
        renderComplexSpectrum (complexRR.data(), kernelLength, 3, dest, causal);
    }
    else
    {
        renderRealSpectrum (binLL.data(), kernelLength, 0, dest, causal);
        renderRealSpectrum (binLR.data(), kernelLength, 1, dest, causal);
        renderRealSpectrum (binRL.data(), kernelLength, 2, dest, causal);
        renderRealSpectrum (binRR.data(), kernelLength, 3, dest, causal);
    }
    dest.valid = true;
}

void LinearPhaseEngine::renderRealSpectrum (const double* bins, int kernelLength, int path, Kernel& dest, bool causal)
{
    for (int i = 0; i < kernelLength; ++i)
        timeBuffer[(size_t) i] = {};

    for (int bin = 0; bin <= kernelLength / 2; ++bin)
    {
        const float gain = std::isfinite (bins[bin]) ? (float) bins[bin] : 0.0f;
        timeBuffer[(size_t) bin] = { gain, 0.0f };
        if (bin > 0 && bin < kernelLength / 2)
            timeBuffer[(size_t) (kernelLength - bin)] = { gain, 0.0f };
    }

    renderTimeFromSpectrum (kernelLength, path, dest, causal);
}

void LinearPhaseEngine::renderComplexSpectrum (const std::complex<double>* bins, int kernelLength, int path, Kernel& dest, bool causal)
{
    // Hermitian spectrum: bin k carries the response and bin N-k its conjugate, so the impulse response is real.
    for (int i = 0; i < kernelLength; ++i)
        timeBuffer[(size_t) i] = {};

    for (int bin = 0; bin <= kernelLength / 2; ++bin)
    {
        const auto value = bins[bin];
        const float re = std::isfinite (value.real()) ? (float) value.real() : 0.0f;
        const float im = std::isfinite (value.imag()) ? (float) value.imag() : 0.0f;
        timeBuffer[(size_t) bin] = { re, im };
        if (bin > 0 && bin < kernelLength / 2)
            timeBuffer[(size_t) (kernelLength - bin)] = { re, -im };
    }

    renderTimeFromSpectrum (kernelLength, path, dest, causal);
}

void LinearPhaseEngine::renderTimeFromSpectrum (int kernelLength, int path, Kernel& dest, bool causal)
{
    const int q = qualityIndexForOrder (dest.order);
    const int fftLength = kernelLength * 2;
    auto& kernelFft = *workerKernelFft[(size_t) q];
    auto& padFft = *workerPadFft[(size_t) q];
    const float* window = windows[(size_t) q].data();
    const float centre = window[kernelLength / 2];
    const float windowScale = std::abs (centre) > 1.0e-8f ? 1.0f / centre : 1.0f;

    kernelFft.perform (timeBuffer.data(), freqBuffer.data(), true);

    for (int i = 0; i < kernelLength; ++i)
    {
        // Causal kernels skip the centre rotation: the impulse starts at sample 0 and the pre-ring half is cut.
        const int source = causal ? i : (i + kernelLength / 2) % kernelLength;
        const bool preRing = causal && i >= kernelLength / 2;
        // The causal kernel keeps the symmetric window's right half, so the impulse peak at sample 0 survives.
        const int windowIndex = causal ? juce::jmin (kernelLength - 1, kernelLength / 2 + i) : i;
        const float sample = preRing ? 0.0f : freqBuffer[(size_t) source].real() * window[windowIndex] * windowScale;
        timeBuffer[(size_t) i] = { sample, 0.0f };
    }
    // The causal kernel is a minimum-phase impulse whose head carries the energy: no fade-in is applied,
    // and no truncation occurs (the minimum-phase response is causal by construction).

    // Impulse padding truncates the windowed impulse to its central fraction (Short/Normal/Full).
    // Less padding shortens the effective impulse and its wrap-around margin at the cost of ripple.
    if (! causal && impulsePadding < 2)
    {
        const int keep = kernelLength / 2 + (impulsePadding == 0 ? 0 : kernelLength / 4);
        for (int i = 0; i < kernelLength; ++i)
        {
            const int distance = std::abs (i - kernelLength / 2);
            if (distance > keep)
                timeBuffer[(size_t) i] = { 0.0f, 0.0f };
        }
    }
    else if (causal && impulsePadding < 2)
    {
        const int keep = kernelLength / 2 + (impulsePadding == 0 ? 0 : kernelLength / 4);
        for (int i = keep; i < kernelLength / 2; ++i)
            timeBuffer[(size_t) i] = { 0.0f, 0.0f };
    }
    for (int i = kernelLength; i < fftLength; ++i)
        timeBuffer[(size_t) i] = {};

    padFft.perform (timeBuffer.data(), freqBuffer.data(), false);

    auto* destBin = dest.spectrum.data() + (size_t) path * maxFft;
    for (int i = 0; i < fftLength; ++i)
        destBin[i] = freqBuffer[(size_t) i];
}

void LinearPhaseEngine::convolvePath (const float* input, float* output, int numSamples, bool accumulate,
                                      const juce::dsp::Complex<float>* spectrum, float* overlapBuffer,
                                      int kernelLength, juce::dsp::FFT& fft)
{
    const int fftLength = kernelLength * 2;
    for (int i = 0; i < fftLength; ++i)
        audioTime[(size_t) i] = { i < numSamples ? input[i] : 0.0f, 0.0f };

    fft.perform (audioTime.data(), audioFreq.data(), false);
    {
        // The complex product as std::complex computes it: four products, then one subtraction and one addition.
        // Written out it vectorises. std::complex only does something different when both parts come out NaN,
        // so in that case the whole product is redone with the operator itself.
        auto* bins = reinterpret_cast<float*> (audioFreq.data());
        const auto* kernelBins = reinterpret_cast<const float*> (spectrum);
        bool bothNaN = false;
        for (int i = 0; i < fftLength; ++i)
        {
            const float a = bins[2 * i], b = bins[2 * i + 1];
            const float c = kernelBins[2 * i], d = kernelBins[2 * i + 1];
            const float ac = a * c;
            const float bd = b * d;
            const float ad = a * d;
            const float bc = b * c;
            const float x = ac - bd;
            const float y = ad + bc;
            bothNaN |= (x != x) && (y != y);
            bins[2 * i] = x;
            bins[2 * i + 1] = y;
        }
        if (bothNaN)
        {
            // Rare: start again from the transform and let std::complex handle the infinities.
            fft.perform (audioTime.data(), audioFreq.data(), false);
            for (int i = 0; i < fftLength; ++i)
                audioFreq[(size_t) i] *= spectrum[i];
        }
    }
    fft.perform (audioFreq.data(), audioTime.data(), true);

    for (int i = 0; i < numSamples; ++i)
    {
        const float sample = audioTime[(size_t) i].real() + overlapBuffer[i];
        output[i] = accumulate ? output[i] + sample : sample;
    }

    // Keep kernelLength-1 future samples. Walk upward: this is an in-place left
    // shift, so overlap[i + numSamples] must still hold the previous hop.
    // The two range tests of each sample are hoisted into the loop bounds; every sample still gets older + newer,
    // with 0.0f standing in for a missing term exactly as before.
    const int tail = kernelLength - 1;
    const int olderEnd = juce::jlimit (0, juce::jmax (0, tail), tail - numSamples);
    const int newerEnd = juce::jlimit (0, juce::jmax (0, tail), fftLength - numSamples);
    const int bothEnd = juce::jmin (olderEnd, newerEnd);
    int i = 0;
    for (; i < bothEnd; ++i)
        overlapBuffer[i] = overlapBuffer[i + numSamples] + audioTime[(size_t) (i + numSamples)].real();
    for (; i < olderEnd; ++i)
        overlapBuffer[i] = overlapBuffer[i + numSamples] + 0.0f;
    for (; i < newerEnd; ++i)
        overlapBuffer[i] = 0.0f + audioTime[(size_t) (i + numSamples)].real();
    for (; i < tail; ++i)
        overlapBuffer[i] = 0.0f + 0.0f;
}

void LinearPhaseEngine::process (juce::dsp::AudioBlock<float>& block)
{
    if (! prepared.load())
        return;

    const int channels = (int) block.getNumChannels();
    const int total = (int) block.getNumSamples();
    if (channels <= 0 || total <= 0)
        return;

    int offset = 0;
    while (offset < total)
    {
        const int count = juce::jmin (512, total - offset);
        const int kernelLength = kernelLengthForOrder (expectedOrder.load());
        const int slot = selectKernel (kernelLength);
        if (slot != lastSlot || kernelLength != lastKernelLength)
        {
            const auto& incoming = kernels[(size_t) slot];
            const bool sameLength = lastSlot >= 0
                                 && kernelLength == lastKernelLength
                                 && kernels[(size_t) lastSlot].valid
                                 && kernels[(size_t) lastSlot].kernelLength == kernelLength
                                 && incoming.valid
                                 && incoming.kernelLength == kernelLength
                                 && fadeKernel.spectrum.size() == incoming.spectrum.size();
            if (sameLength)
            {
                if (engageSamplesLeft <= 0 && fadeSamplesLeft <= 0)
                {
                    const auto& previous = kernels[(size_t) lastSlot];
                    fadeKernel.order = previous.order;
                    fadeKernel.kernelLength = previous.kernelLength;
                    fadeKernel.mono = previous.mono;
                    fadeKernel.valid = true;
                    std::copy (previous.spectrum.begin(), previous.spectrum.end(), fadeKernel.spectrum.begin());
                    for (size_t path = 0; path < overlap.size(); ++path)
                        std::copy (overlap[path].begin(), overlap[path].end(), overlapRetire[path].begin());
                }
                for (auto& buffer : overlap)
                    std::fill (buffer.begin(), buffer.end(), 0.0f);
                engageSamplesLeft = kernelLength;
                fadeSamplesLeft = 0;
            }
            else
            {
                for (auto& buffer : overlap)
                    std::fill (buffer.begin(), buffer.end(), 0.0f);
                engageSamplesLeft = 0;
                fadeSamplesLeft = 0;
                fadeKernel.valid = false;
            }
            lastSlot = slot;
            lastKernelLength = kernelLength;
        }

        const auto& kernel = kernels[(size_t) slot];
        audioReading.store (slot, std::memory_order_release);
        const int usedChannels = juce::jmin (2, channels);

        for (int channel = 0; channel < usedChannels; ++channel)
            std::memcpy (channelCopy[(size_t) channel].data(),
                         block.getChannelPointer ((size_t) channel) + offset,
                         sizeof (float) * (size_t) count);

        auto renderInto = [&] (const Kernel& source, std::array<std::vector<float>, pathCount>& overlapSet,
                               std::array<std::vector<float>, 2>& dest)
        {
            const int q = qualityIndexForOrder (source.order);
            auto& fft = *audioFft[(size_t) q];
            const bool monoKernel = source.mono || usedChannels < 2;
            if (monoKernel)
            {
                convolvePath (channelCopy[0].data(), dest[0].data(), count, false,
                              source.spectrum.data(), overlapSet[0].data(), source.kernelLength, fft);
                if (usedChannels > 1)
                    convolvePath (channelCopy[1].data(), dest[1].data(), count, false,
                                  source.spectrum.data(), overlapSet[1].data(), source.kernelLength, fft);
            }
            else
            {
                convolvePath (channelCopy[0].data(), dest[0].data(), count, false,
                              source.spectrum.data() + 0 * maxFft, overlapSet[0].data(), source.kernelLength, fft);
                convolvePath (channelCopy[1].data(), dest[0].data(), count, true,
                              source.spectrum.data() + 1 * maxFft, overlapSet[1].data(), source.kernelLength, fft);
                convolvePath (channelCopy[0].data(), dest[1].data(), count, false,
                              source.spectrum.data() + 2 * maxFft, overlapSet[2].data(), source.kernelLength, fft);
                convolvePath (channelCopy[1].data(), dest[1].data(), count, true,
                              source.spectrum.data() + 3 * maxFft, overlapSet[3].data(), source.kernelLength, fft);
            }
        };

        renderInto (kernel, overlap, channelAccum);
        const bool handoff = fadeKernel.valid && (engageSamplesLeft > 0 || fadeSamplesLeft > 0);
        if (handoff)
            renderInto (fadeKernel, overlapRetire, channelFade);

        for (int i = 0; i < count; ++i)
        {
            float newer = 1.0f;
            float older = 0.0f;
            if (handoff && engageSamplesLeft > 0)
            {
                newer = 0.0f;
                older = 1.0f;
                if (--engageSamplesLeft == 0)
                    fadeSamplesLeft = crossfadeSamples;
            }
            else if (handoff && fadeSamplesLeft > 0)
            {
                newer = 1.0f - (float) fadeSamplesLeft / (float) crossfadeSamples;
                older = 1.0f - newer;
                --fadeSamplesLeft;
                if (fadeSamplesLeft <= 0)
                    fadeKernel.valid = false;
            }

            for (int channel = 0; channel < usedChannels; ++channel)
            {
                const float sample = newer * channelAccum[(size_t) channel][(size_t) i]
                                   + older * channelFade[(size_t) channel][(size_t) i];
                block.getChannelPointer ((size_t) channel)[(size_t) offset + i] = sample;
            }
        }

        audioReading.store (-1, std::memory_order_release);
        offset += count;
    }
}
