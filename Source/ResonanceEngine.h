#pragma once
#include <JuceHeader.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

/** Resonance suppressor.

    Works on a short-time spectrum. For every frequency it asks how far the level stands above its own
    surroundings (a smoothed copy of the spectrum, about an octave wide), and turns down only that excess.
    A tilted or broadly shaped spectrum has no excess and passes untouched; a ringing peak is pulled back
    toward its neighbours. Because the test is relative, the result does not depend on how loud the input is.

    Controls
      depth        how much of the excess is removed
      sharpness    how narrow the cuts are: low makes broad, gentle cuts, high makes narrow, deep ones
      selectivity  how far a peak has to stand out before it is touched
      attack       how fast a cut sets in, 1 ms to 500 ms
      release      how fast it lets go, 20 ms to 2 s
      floor        frequencies quieter than this are left alone
      centre/width the region that is processed, with half-octave soft edges
      link         0 treats left and right separately, 1 gives both the deeper of the two cuts
      mix          dry/wet
      hard         off: cuts level out at a ceiling set by depth. on: no ceiling
      delta        outputs only what is being removed
      mid/side     detects and cuts in mid and side instead of left and right
      focus bands  bell-shaped regions where the suppressor works harder or lighter
      sidechain    when given, the cuts follow what stands out in the sidechain instead of in the input

    Hann analysis and synthesis windows at 4x overlap; with no reduction the input is reconstructed after
    getLatencySamples(). process() does not allocate. */
class ResonanceEngine
{
public:
    struct Settings
    {
        float depth = 0.0f;        // 0..1
        float sharpness = 0.5f;    // 0..1
        float selectivity = 0.35f; // 0..1
        float attack = 0.2f;       // 0..1
        float release = 0.3f;      // 0..1
        float floorDb = -80.0f;    // dB relative to a full-scale sine
        float centreHz = 3000.0f;
        float widthOctaves = 10.0f;
        float mix = 1.0f;          // 0..1
        float link = 1.0f;         // 0..1
        bool hard = false;
        bool delta = false;
        bool midSide = false;      // detect and cut in mid and side instead of left and right

        /** Focus bands: each makes the suppressor work harder (amount above 0) or lighter (below 0) around a
            frequency. amount 1 quadruples the cut at the centre, -1 quarters it; bands multiply together. */
        struct Focus { float hz = 1000.0f; float amount = 0.0f; float q = 1.0f; };
        static constexpr int maxFocus = 24;
        std::array<Focus, maxFocus> focus {};
        int focusCount = 0;
    };

    /** Bands of the reduction readout, log-spaced from 20 Hz to 20 kHz. */
    static constexpr int displayBands = 120;
    static float displayBandHz (int band) noexcept
    {
        return 20.0f * std::pow (1000.0f, (float) band / (float) (displayBands - 1));
    }
    /** Current reduction in a readout band, in dB (0 or positive). Safe to call from the message thread. */
    float getDisplayReductionDb (int band) const noexcept
    {
        return display[(size_t) juce::jlimit (0, displayBands - 1, band)].load (std::memory_order_relaxed);
    }

    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        fftOrder = sampleRate <= 50000.0 ? 11 : (sampleRate <= 100000.0 ? 12 : 13);
        fftSize = 1 << fftOrder;
        hop = fftSize / 4;
        bins = fftSize / 2 + 1;
        transform = std::make_unique<juce::dsp::FFT> (fftOrder);

        window.resize ((size_t) fftSize);
        double windowSum = 0.0;
        for (int i = 0; i < fftSize; ++i)
        {
            window[(size_t) i] = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) fftSize));
            windowSum += window[(size_t) i];
        }
        // Scales a bin magnitude so a full-scale sine reads 0 dB.
        magnitudeScale = (float) (2.0 / windowSum);

        hopScale.assign ((size_t) hop, 1.0f);
        for (int residue = 0; residue < hop; ++residue)
        {
            float sum = 0.0f;
            for (int k = 0; k < fftSize / hop; ++k)
                sum += window[(size_t) (residue + k * hop)] * window[(size_t) (residue + k * hop)];
            hopScale[(size_t) residue] = sum > 1.0e-8f ? 1.0f / sum : 0.0f;
        }

        for (auto& ring : input)  ring.assign ((size_t) fftSize, 0.0f);
        for (auto& ring : output) ring.assign ((size_t) fftSize * 2, 0.0f);
        frame.assign ((size_t) fftSize, {});
        for (auto& s : spectrum) s.assign ((size_t) fftSize, {});
        focusWeight.assign ((size_t) bins, 1.0f);
        focusCached = {};
        focusCachedCount = -1;
        for (auto& v : detector)  v.assign ((size_t) bins, 0.0f);
        for (auto& v : levelDb)   v.assign ((size_t) bins, -200.0f);
        for (auto& v : wanted)    v.assign ((size_t) bins, 0.0f);
        for (auto& v : reduction) v.assign ((size_t) bins, 0.0f);
        prefix.assign ((size_t) bins + 1, 0.0);
        scratch.assign ((size_t) bins, 0.0f);

        // The surroundings a bin is compared against: 1.2 octaves centred on it, never fewer than 9 bins.
        envelopeLow.resize ((size_t) bins);
        envelopeHigh.resize ((size_t) bins);
        const double down = std::pow (2.0, -0.6), up = std::pow (2.0, 0.6);
        for (int k = 0; k < bins; ++k)
        {
            int lo = (int) std::floor (k * down), hi = (int) std::ceil (k * up);
            if (hi - lo < 8) { lo = k - 4; hi = k + 4; }
            envelopeLow[(size_t) k] = juce::jlimit (1, bins - 1, lo);
            envelopeHigh[(size_t) k] = juce::jlimit (1, bins - 1, hi);
        }

        binBand.resize ((size_t) bins);
        for (int k = 0; k < bins; ++k)
        {
            const double hz = juce::jmax (1.0, k * sampleRate / (double) fftSize);
            const double position = std::log (hz / 20.0) / std::log (1000.0) * (displayBands - 1);
            binBand[(size_t) k] = (hz < 20.0 || hz > 20000.0) ? -1 : juce::jlimit (0, displayBands - 1, (int) std::lround (position));
        }

        hopSeconds = (float) (hop / sampleRate);
        detectorCoef = std::exp (-hopSeconds / 0.02f);
        reset();
        prepared = true;
    }

    void reset() noexcept
    {
        for (auto& ring : input)  std::fill (ring.begin(), ring.end(), 0.0f);
        for (auto& ring : output) std::fill (ring.begin(), ring.end(), 0.0f);
        for (auto& v : detector)  std::fill (v.begin(), v.end(), 0.0f);
        for (auto& v : reduction) std::fill (v.begin(), v.end(), 0.0f);
        for (auto& value : display) value.store (0.0f, std::memory_order_relaxed);
        seen = 0;
        detectorPrimed = false;
    }

    /** Samples between an input impulse and the reconstructed output impulse. */
    int getLatencySamples() const noexcept { return fftSize - 1; }

    /** sideLeft/sideRight: an optional sidechain of the same length. When given, detection listens to it. */
    void process (float* left, float* right, int numSamples, const Settings& settings,
                  const float* sideLeft = nullptr, const float* sideRight = nullptr)
    {
        if (! prepared || left == nullptr || numSamples <= 0)
            return;

        const bool stereo = right != nullptr && right != left;
        const bool keyed = sideLeft != nullptr;
        for (int i = 0; i < numSamples; ++i)
        {
            const size_t slot = (size_t) (seen % fftSize);
            input[0][slot] = left[i];
            if (stereo)
                input[1][slot] = right[i];
            // The sidechain rings are kept running (with silence when there is none) so switching is clean.
            input[2][slot] = keyed ? sideLeft[i] : 0.0f;
            input[3][slot] = keyed ? (sideRight != nullptr ? sideRight[i] : sideLeft[i]) : 0.0f;
            ++seen;

            // Every hop, including the zero-padded frames before the buffer has filled, so the very first
            // samples are reconstructed too.
            if (seen % hop == 0)
                processFrame (stereo, keyed, settings);

            const juce::int64 emitAt = seen - 1 - (juce::int64) getLatencySamples();
            left[i] = takeOutput (0, emitAt);
            if (stereo)
                right[i] = takeOutput (1, emitAt);
        }
    }

private:
    void processFrame (bool stereo, bool keyed, const Settings& s)
    {
        const int channels = stereo ? 2 : 1;
        const bool midSide = stereo && s.midSide;
        for (int ch = 0; ch < channels; ++ch)
            analyse (ch);
        if (keyed)
            for (int ch = 0; ch < channels; ++ch)
                analyse (2 + ch);
        if (midSide)
        {
            toMidSide (spectrum[0], spectrum[1]);
            if (keyed)
                toMidSide (spectrum[2], spectrum[3]);
        }
        updateFocus (s);

        const float depth = juce::jlimit (0.0f, 1.0f, s.depth);
        const float sharpness = juce::jlimit (0.0f, 1.0f, s.sharpness);
        const float knee = 1.0f + 8.0f * juce::jlimit (0.0f, 1.0f, s.selectivity);
        const float ceiling = 4.0f + 14.0f * depth;
        const float ratio = (s.hard ? 3.0f : 1.5f) * depth;
        const float halfWidth = juce::jmax (0.05f, s.widthOctaves) * 0.5f;
        const float centre = juce::jlimit (20.0f, 20000.0f, s.centreHz);
        // The cut is spread over this many octaves either side: broad at low sharpness, narrow at high.
        const double spread = std::pow (2.0, 0.5 * (0.4 + (0.03 - 0.4) * (double) sharpness)) - 1.0;

        for (int ch = 0; ch < channels; ++ch)
        {
            auto& level = levelDb[(size_t) ch];
            auto& power = detector[(size_t) ch];
            // Detection listens to the sidechain when there is one, otherwise to the signal itself.
            const auto& spec = spectrum[(size_t) (keyed ? 2 + ch : ch)];

            // Level per bin, steadied over about 20 ms so noise does not flicker the detection.
            prefix[0] = 0.0;
            for (int k = 0; k < bins; ++k)
            {
                const float magnitude = std::abs (spec[(size_t) k]) * magnitudeScale;
                const float instant = magnitude * magnitude;
                power[(size_t) k] = detectorPrimed ? instant + (power[(size_t) k] - instant) * detectorCoef : instant;
                level[(size_t) k] = 10.0f * std::log10 (power[(size_t) k] + 1.0e-20f);
                prefix[(size_t) k + 1] = prefix[(size_t) k] + (double) level[(size_t) k];
            }

            // The surroundings, in two passes. The first is a plain average. The second averages again with
            // anything standing more than 2 dB above the first held down to it, so a peak does not raise the
            // very reference it is measured against.
            for (int k = 1; k < bins; ++k)
            {
                const int lo = envelopeLow[(size_t) k], hi = envelopeHigh[(size_t) k];
                scratch[(size_t) k] = (float) ((prefix[(size_t) hi + 1] - prefix[(size_t) lo]) / (double) (hi - lo + 1));
            }
            scratch[0] = scratch[1];
            prefix[0] = 0.0;
            for (int k = 0; k < bins; ++k)
                prefix[(size_t) k + 1] = prefix[(size_t) k] + (double) juce::jmin (level[(size_t) k], scratch[(size_t) k] + 2.0f);

            // How far each bin stands above its surroundings, turned into the cut it should get.
            auto& want = wanted[(size_t) ch];
            want[0] = 0.0f;
            for (int k = 1; k < bins; ++k)
            {
                const int lo = envelopeLow[(size_t) k], hi = envelopeHigh[(size_t) k];
                const float surroundings = (float) ((prefix[(size_t) hi + 1] - prefix[(size_t) lo]) / (double) (hi - lo + 1));
                const float excess = level[(size_t) k] - surroundings - knee;
                float cut = 0.0f;
                if (excess > 0.0f && depth > 0.0f)
                {
                    cut = s.hard ? juce::jmin (40.0f, ratio * excess)
                                 : ceiling * std::tanh (ratio * excess / ceiling);

                    const float hz = (float) (k * sampleRate / (double) fftSize);
                    const float distance = std::abs (std::log2 (juce::jmax (1.0f, hz) / centre));
                    const float region = distance <= halfWidth ? 1.0f : juce::jmax (0.0f, 1.0f - (distance - halfWidth) * 2.0f);
                    const float audible = juce::jlimit (0.0f, 1.0f, (level[(size_t) k] - s.floorDb) * 0.1f);
                    cut = juce::jmin (40.0f, cut * region * audible * focusWeight[(size_t) k]);
                }
                want[(size_t) k] = cut;
            }

            // Spread each cut over its neighbours. This sets how narrow the cuts are and keeps them smooth.
            prefix[0] = 0.0;
            for (int k = 0; k < bins; ++k)
                prefix[(size_t) k + 1] = prefix[(size_t) k] + (double) want[(size_t) k];
            for (int k = 1; k < bins; ++k)
            {
                const int reach = juce::jmax (1, (int) std::lround (k * spread));
                const int lo = juce::jmax (1, k - reach), hi = juce::jmin (bins - 1, k + reach);
                scratch[(size_t) k] = (float) ((prefix[(size_t) hi + 1] - prefix[(size_t) lo]) / (double) (hi - lo + 1));
            }
            std::copy (scratch.begin() + 1, scratch.end(), want.begin() + 1);
        }
        detectorPrimed = true;

        // Stereo link: toward the deeper of the two cuts, so a linked cut never shifts the image.
        if (stereo && s.link > 0.0f)
        {
            const float link = juce::jmin (1.0f, s.link);
            for (int k = 1; k < bins; ++k)
            {
                const float deeper = juce::jmax (wanted[0][(size_t) k], wanted[1][(size_t) k]);
                wanted[0][(size_t) k] += (deeper - wanted[0][(size_t) k]) * link;
                wanted[1][(size_t) k] += (deeper - wanted[1][(size_t) k]) * link;
            }
        }

        const float attackSeconds = 0.001f * std::pow (500.0f, juce::jlimit (0.0f, 1.0f, s.attack));
        const float releaseSeconds = 0.02f * std::pow (100.0f, juce::jlimit (0.0f, 1.0f, s.release));
        const float attackCoef = std::exp (-hopSeconds / attackSeconds);
        const float releaseCoef = std::exp (-hopSeconds / releaseSeconds);
        const float mix = juce::jlimit (0.0f, 1.0f, s.mix);

        std::array<float, displayBands> shown {};
        for (int ch = 0; ch < channels; ++ch)
        {
            auto& current = reduction[(size_t) ch];
            auto& spec = spectrum[(size_t) ch];
            for (int k = 0; k < bins; ++k)
            {
                const float target = wanted[(size_t) ch][(size_t) k];
                const float coef = target > current[(size_t) k] ? attackCoef : releaseCoef;
                current[(size_t) k] = target + (current[(size_t) k] - target) * coef;

                const float cutGain = std::exp (-0.115129255f * current[(size_t) k]);
                const float gain = s.delta ? 1.0f - cutGain : 1.0f + (cutGain - 1.0f) * mix;
                spec[(size_t) k] *= gain;
                if (k > 0 && k < fftSize / 2)
                    spec[(size_t) (fftSize - k)] = std::conj (spec[(size_t) k]);

                const int band = binBand[(size_t) k];
                if (band >= 0)
                    shown[(size_t) band] = juce::jmax (shown[(size_t) band], current[(size_t) k]);
            }
        }
        if (midSide)
            toLeftRight (spectrum[0], spectrum[1]);
        for (int ch = 0; ch < channels; ++ch)
            addFrame (ch);

        // Readout bands narrower than a bin (at the low end) take their neighbour's value.
        float last = 0.0f;
        int lastBand = -1;
        for (int k = 1; k < bins; ++k)
        {
            const int band = binBand[(size_t) k];
            if (band < 0)
                continue;
            for (int fill = lastBand + 1; fill < band; ++fill)
                shown[(size_t) fill] = juce::jmax (last, shown[(size_t) band]);
            last = shown[(size_t) band];
            lastBand = band;
        }
        for (int band = 0; band < displayBands; ++band)
            display[(size_t) band].store (shown[(size_t) band], std::memory_order_relaxed);
    }

    using Spectrum = std::vector<juce::dsp::Complex<float>>;
    void toMidSide (Spectrum& a, Spectrum& b) const noexcept
    {
        for (int k = 0; k < bins; ++k)
        {
            const auto l = a[(size_t) k], r = b[(size_t) k];
            a[(size_t) k] = (l + r) * 0.5f;
            b[(size_t) k] = (l - r) * 0.5f;
        }
    }
    void toLeftRight (Spectrum& a, Spectrum& b) const noexcept
    {
        for (int k = 0; k < bins; ++k)
        {
            const auto m = a[(size_t) k], side = b[(size_t) k];
            a[(size_t) k] = m + side;
            b[(size_t) k] = m - side;
            if (k > 0 && k < fftSize / 2)
            {
                a[(size_t) (fftSize - k)] = std::conj (a[(size_t) k]);
                b[(size_t) (fftSize - k)] = std::conj (b[(size_t) k]);
            }
        }
    }

    /** Rebuilds the per-bin focus multiplier when the focus bands have changed. */
    void updateFocus (const Settings& s)
    {
        const int count = juce::jlimit (0, Settings::maxFocus, s.focusCount);
        bool same = count == focusCachedCount;
        for (int i = 0; same && i < count; ++i)
            same = s.focus[(size_t) i].hz == focusCached[(size_t) i].hz && s.focus[(size_t) i].amount == focusCached[(size_t) i].amount
                   && s.focus[(size_t) i].q == focusCached[(size_t) i].q;
        if (same)
            return;
        focusCachedCount = count;
        focusCached = s.focus;
        for (int k = 0; k < bins; ++k)
        {
            const float hz = juce::jmax (1.0f, (float) (k * sampleRate / (double) fftSize));
            float octavesOfGain = 0.0f;
            for (int i = 0; i < count; ++i)
            {
                const auto& band = s.focus[(size_t) i];
                const float sigma = juce::jlimit (0.06f, 2.5f, 0.7f / juce::jmax (0.05f, band.q));
                const float distance = std::log2 (hz / juce::jlimit (20.0f, 20000.0f, band.hz)) / sigma;
                octavesOfGain += 2.0f * juce::jlimit (-1.0f, 1.0f, band.amount) * std::exp (-0.5f * distance * distance);
            }
            focusWeight[(size_t) k] = std::pow (2.0f, juce::jlimit (-4.0f, 4.0f, octavesOfGain));
        }
    }

    void analyse (int channel)
    {
        const juce::int64 start = seen - fftSize;
        for (int i = 0; i < fftSize; ++i)
        {
            const juce::int64 time = start + i;
            const float sample = time >= 0 ? input[(size_t) channel][(size_t) (time % fftSize)] : 0.0f;
            frame[(size_t) i] = { sample * window[(size_t) i], 0.0f };
        }
        transform->perform (frame.data(), spectrum[(size_t) channel].data(), false);
    }

    void addFrame (int channel)
    {
        transform->perform (spectrum[(size_t) channel].data(), frame.data(), true);
        const juce::int64 frameStart = seen - fftSize;
        auto& ring = output[(size_t) channel];
        const int ringSize = (int) ring.size();
        for (int i = 0; i < fftSize; ++i)
        {
            const juce::int64 time = frameStart + i;
            if (time < 0)
                continue;
            ring[(size_t) (time % ringSize)] += frame[(size_t) i].real() * window[(size_t) i] * hopScale[(size_t) (time % hop)];
        }
    }

    float takeOutput (int channel, juce::int64 time) noexcept
    {
        if (time < 0)
            return 0.0f;
        auto& ring = output[(size_t) channel];
        const size_t index = (size_t) (time % (juce::int64) ring.size());
        const float sample = ring[index];
        ring[index] = 0.0f;
        return sample;
    }

    double sampleRate = 44100.0;
    int fftOrder = 11, fftSize = 2048, hop = 512, bins = 1025;
    bool prepared = false, detectorPrimed = false;
    float magnitudeScale = 1.0f, hopSeconds = 0.01f, detectorCoef = 0.5f;
    juce::int64 seen = 0;
    std::unique_ptr<juce::dsp::FFT> transform;
    std::vector<float> window, hopScale, scratch;
    std::vector<double> prefix;
    std::vector<int> envelopeLow, envelopeHigh, binBand;
    std::array<std::vector<float>, 4> input;   // left, right, sidechain left, sidechain right
    std::array<std::vector<float>, 2> output, detector, levelDb, wanted, reduction;
    std::vector<juce::dsp::Complex<float>> frame;
    std::array<std::vector<juce::dsp::Complex<float>>, 4> spectrum;
    std::vector<float> focusWeight;
    std::array<Settings::Focus, Settings::maxFocus> focusCached {};
    int focusCachedCount = -1;
    std::array<std::atomic<float>, displayBands> display {};
};
