#pragma once
#include <JuceHeader.h>
#include <array>
#include <cmath>
#include <complex>
#include <cstring>
#include "ButterworthDesign.h"
#include "FilterDesign.h"
#include "VintageFilters.h"

enum class FilterType
{
    Bell = 0,
    LowShelf,
    HighShelf,
    LowCut,
    HighCut,
    Notch,
    BandPass,
    TiltShelf,
    AllPass,
    SoftBell,
    FlatTilt,
    FlatTop,
    NarrowResonance,
    // Appended after the original set so saved type indices keep their meaning.
    BrickwallLowCut,   // Brickwall High Pass (reference "Brickwall HP")
    BrickwallHighCut,  // Brickwall Low Pass (reference "Brickwall LP")
    Sword,
    AllPassHigh,       // reference "Allpass H": the mirrored all-pass
    // Type E
    EHighPass,
    ELowPass,
    ELowShelf,
    EHighShelf,
    ELmf,
    EHmf,
    ELowBell,
    EHighBell,
    // Type G
    GLmf,
    // British N
    BritNHighPass,
    BritNLowShelf,
    BritNBell,
    BritNHighShelf,
    // EQ 250
    Eq250Bell,
    Eq250LowShelf,
    Eq250HighShelf,
    // Vintage Tube
    VtHighPass,
    VtLowPass,
    VtLowShelf,
    VtBoost,
    VtAtten,
    VtLmf,
    VtHmf,
    // Tone Stack
    ToneLow,
    ToneHigh,
    // Blue
    BlueLow,
    GLowShelf,  // reference "G LS" (appended, so saved type indices keep their meaning)
    GHighShelf, // reference "G HS"
    BlueHigh,   // reference "Blue High" (boost shelf)
    GHighPass,  // reference "G HP"
    GLowPass    // reference "G LP"
};

inline constexpr int filterTypeCount = 48;

enum class StereoMode
{
    Stereo = 0,
    Mid,
    Side,
    Left,
    Right,
    MidSide // reference "Mid Side": both mid and side pass through the band, weighted by the pan
};

/** Pure filter design and display snapshots.
    The audio thread owns the IIR objects. The graph and the linear-phase worker
    evaluate copies of these values and never read live filter state. */
namespace ResponseModel
{
    inline constexpr int maxStages = 16;
    inline constexpr int graphBandCount = 32;
    /** Brickwall cuts are a fixed 96 dB/oct Butterworth. They are sharp, not ideal. */
    // reference brickwall measures about 180 dB/oct (a 30-pole Butterworth), so the brickwall uses 30 poles.
    /** The slope reaches the designs through the integer slopeDb channel. Values from encodedSlopeBase upward carry
        reference normalised slope (0..1) with 1e-4 resolution; smaller values are plain dB per octave (legacy). */
    inline constexpr int encodedSlopeBase = 1000;
    inline constexpr int encodeSlopeNorm (float slopeNorm) noexcept
    {
        const float s = slopeNorm < 0.0f ? 0.0f : (slopeNorm > 1.0f ? 1.0f : slopeNorm);
        return encodedSlopeBase + (int) (s * 10000.0f + 0.5f);
    }
    inline constexpr bool isEncodedSlope (int slopeDb) noexcept { return slopeDb >= encodedSlopeBase; }
    inline constexpr double decodeSlopeNorm (int slopeDb) noexcept { return (double) (slopeDb - encodedSlopeBase) / 10000.0; }

    inline constexpr int brickwallPoles = 30;
    inline constexpr int brickwallSlopeDb = 180;

    inline bool isBrickwall (FilterType type) noexcept
    {
        return type == FilterType::BrickwallLowCut || type == FilterType::BrickwallHighCut;
    }

    /** How a type is built. Plain types build themselves. Families
        (Type E, Type G, British N, EQ 250, Vintage Tube, Tone Stack, Blue, Sword, Allpass H)
        build from a base shape plus a small set of traits. These are approximations of
        the selected response character, not circuit models. */
    struct TypeTraits
    {
        FilterType base = FilterType::Bell; // shape used for design and for control rules
        int gainLimit = 0;                  // 0 both ways, +1 raise only, -1 reduce only
        float qScale = 1.0f;                // multiplies the Q control
        float fixedQ = 0.0f;                // above zero replaces the Q control entirely
        bool mirror = false;                // all-pass with the phase transition moved to the top
        bool midSink = false;               // low shelf with a mid dip (Vintage Tube LS)
        bool highLoss = false;              // low shelf with some high-frequency loss (Tone Low)
    };

    inline TypeTraits typeTraits (FilterType type) noexcept
    {
        TypeTraits t;
        t.base = type;
        switch (type)
        {
            case FilterType::Sword:            t.base = FilterType::Bell; t.qScale = 2.0f; break;
            case FilterType::AllPassHigh:      t.base = FilterType::AllPass; t.mirror = true; break;
            case FilterType::EHighPass:        t.base = FilterType::LowCut; break;
            case FilterType::ELowPass:         t.base = FilterType::HighCut; break;
            case FilterType::ELowShelf:        t.base = FilterType::LowShelf; t.qScale = 0.8f; break;
            case FilterType::EHighShelf:       t.base = FilterType::HighShelf; t.qScale = 0.8f; break;
            case FilterType::ELmf:             t.base = FilterType::Bell; t.qScale = 0.9f; break;
            case FilterType::EHmf:             t.base = FilterType::Bell; t.qScale = 1.1f; break;
            case FilterType::ELowBell:         t.base = FilterType::Bell; t.fixedQ = 0.7f; break;
            case FilterType::EHighBell:        t.base = FilterType::Bell; t.fixedQ = 0.7f; break;
            case FilterType::GLmf:             t.base = FilterType::Bell; t.qScale = 1.2f; break;
            case FilterType::BritNHighPass:    t.base = FilterType::LowCut; break;
            case FilterType::BritNLowShelf:    t.base = FilterType::LowShelf; break;
            case FilterType::BritNBell:        t.base = FilterType::Bell; break;
            case FilterType::BritNHighShelf:   t.base = FilterType::HighShelf; break;
            case FilterType::Eq250Bell:        t.base = FilterType::Bell; t.qScale = 0.8f; break;
            case FilterType::Eq250LowShelf:    t.base = FilterType::LowShelf; t.fixedQ = 0.5f; break;
            case FilterType::Eq250HighShelf:   t.base = FilterType::HighShelf; t.fixedQ = 0.5f; break;
            case FilterType::VtHighPass:       t.base = FilterType::LowCut; break;
            case FilterType::VtLowPass:        t.base = FilterType::HighCut; break;
            case FilterType::VtLowShelf:       t.base = FilterType::LowShelf; t.midSink = true; break;
            case FilterType::VtBoost:          t.base = FilterType::Bell; t.gainLimit = 1; break;
            case FilterType::VtAtten:          t.base = FilterType::HighShelf; t.gainLimit = -1; break;
            case FilterType::VtLmf:            t.base = FilterType::Bell; t.qScale = 0.8f; break;
            case FilterType::VtHmf:            t.base = FilterType::Bell; t.qScale = 1.2f; break;
            case FilterType::ToneLow:          t.base = FilterType::LowShelf; t.highLoss = true; break;
            case FilterType::ToneHigh:         t.base = FilterType::HighShelf; break;
            case FilterType::BlueLow:          t.base = FilterType::LowShelf; t.gainLimit = 1; break;
            case FilterType::BlueHigh:         t.base = FilterType::HighShelf; t.gainLimit = 1; break;
            case FilterType::GHighPass:        t.base = FilterType::LowCut; break;
            case FilterType::GLowPass:         t.base = FilterType::HighCut; break;
            case FilterType::Bell:
            case FilterType::LowShelf:
            case FilterType::HighShelf:
            case FilterType::LowCut:
            case FilterType::HighCut:
            case FilterType::Notch:
            case FilterType::BandPass:
            case FilterType::TiltShelf:
            case FilterType::AllPass:
            case FilterType::SoftBell:
            case FilterType::FlatTop:
            case FilterType::NarrowResonance:
            case FilterType::BrickwallLowCut:
            case FilterType::BrickwallHighCut:
                break; // Plain shapes build themselves.
        }
        return t;
    }

    inline FilterType shapeOf (FilterType type) noexcept
    {
        return typeTraits (type).base;
    }

    inline bool isAllPass (FilterType type) noexcept
    {
        return shapeOf (type) == FilterType::AllPass;
    }

    inline bool usesGain (FilterType type) noexcept
    {
        const auto shape = shapeOf (type);
        return shape == FilterType::Bell || shape == FilterType::LowShelf
            || shape == FilterType::HighShelf || shape == FilterType::TiltShelf
            || shape == FilterType::SoftBell || shape == FilterType::FlatTilt
            || shape == FilterType::FlatTop || shape == FilterType::NarrowResonance;
    }

    inline bool isCut (FilterType type) noexcept
    {
        const auto shape = shapeOf (type);
        return shape == FilterType::LowCut || shape == FilterType::HighCut || isBrickwall (type);
    }

    /** Butterworth cuts take their Q from the cascade, the flat tilt is a
        fixed first-order slope, and some vintage types fix their Q. Those Q controls do not apply. */
    /** Generic types designed with Imported slope (filter order) control; see FilterDesign. */
    inline bool isGenericFilter (FilterType type) noexcept
    {
        return type == FilterType::Bell || type == FilterType::LowShelf || type == FilterType::HighShelf
            || type == FilterType::Notch || type == FilterType::BandPass
            || type == FilterType::LowCut || type == FilterType::HighCut;
    }

    /** Types whose slope parameter is reference normalised slope (0 to 1), carried encoded in slopeDb. For the
        generic types it is the filter order; for VT LS it is the P-type attenuation. */
    inline bool usesContinuousSlope (FilterType type) noexcept
    {
        return isGenericFilter (type) || type == FilterType::VtLowShelf;
    }

    /** VT LS uses the slope control as its attenuation. */
    inline bool slopeIsAttenuation (FilterType type) noexcept
    {
        return type == FilterType::VtLowShelf;
    }

    inline bool usesQ (FilterType type) noexcept
    {
        // The shelves ignore Q (slope sets their shape). Its plain cuts use Q at order 2.
        if (type == FilterType::LowShelf || type == FilterType::HighShelf)
            return false;
        if (type == FilterType::LowCut || type == FilterType::HighCut)
            return true;
        // Circuit models with fixed component values: Q does nothing in this model either.
        if (type == FilterType::VtLowShelf || type == FilterType::ToneLow || type == FilterType::ToneHigh
            || type == FilterType::BlueHigh)
            return false;
        return ! isCut (type) && shapeOf (type) != FilterType::FlatTilt && typeTraits (type).fixedQ <= 0.0f;
    }

    /** Brickwall slope is fixed, so the slope control does not apply to it. */
    inline bool usesSlope (FilterType type) noexcept
    {
        return usesContinuousSlope (type);
    }

    /** Dynamic gain moves the filter's gain parameter. Cuts, notches, band-pass,
        and all-pass have no gain to move. */
    inline bool supportsDynamics (FilterType type) noexcept
    {
        return usesGain (type);
    }

    /** Linear phase is a magnitude FIR. All-pass phase cannot be represented that way. */
    inline bool supportedInLinearPhase (FilterType type) noexcept
    {
        return ! isAllPass (type);
    }

    /** Upper bound is 20 kHz, and also stays below Nyquist when the session rate is lower.
        The stored parameter is left unchanged; only the designed frequency is clamped. */
    inline float clampFrequency (float frequency, double sampleRate) noexcept
    {
        const float upper = (float) juce::jmin (20000.0, sampleRate * 0.49);
        const float lower = 10.0f;
        if (! (upper > lower))
            return lower;
        return juce::jlimit (lower, upper, frequency);
    }

    inline float clampQ (float q) noexcept
    {
        return juce::jlimit (0.025f, 40.0f, q);
    }

    inline bool sameBits (float a, float b) noexcept
    {
        return std::memcmp (&a, &b, sizeof (float)) == 0;
    }

    struct NormalizedStage
    {
        int order = 0;
        float c[5] = {};
    };

    /** A designed band. T is the arithmetic type of the coefficients: float for the graph, double for the audio path. */
    /** Low-pass biquad from the analogue section by impulse invariance, with DC gain of one. These low passes keep
        the analogue slope up to Nyquist (historical response baseline, 12 dB/oct to 20 kHz at 1 kHz). The bilinear
        transform warps the top of the band and steepens it, so the low passes use this form. */
    template <typename T>
    inline std::array<T, 6> impulseInvariantLowPass (double sampleRate, float frequency, float q) noexcept
    {
        const double omega = juce::MathConstants<double>::twoPi * (double) frequency;
        const double zeta = 0.5 / (double) q;
        const double period = 1.0 / sampleRate;
        const double r = std::exp (-zeta * omega * period);
        const double theta = omega * std::sqrt (1.0 - zeta * zeta) * period;
        const double a1 = -2.0 * r * std::cos (theta);
        const double a2 = r * r;
        // The numerator is a single delay, scaled so the response at DC is one.
        const double b1 = 1.0 + a1 + a2;
        return { T (0), T (b1), T (0), T (1), T (a1), T (a2) };
    }

    /** Magnitude at the commanded frequency of an impulse-invariant low-pass cascade whose cutoffs are scaled by `scale`. */
    inline double impulseLowPassCascadeMagnitude (double sampleRate, double frequency, const double* qs, int sections,
                                                  double scale) noexcept
    {
        double magnitude = 1.0;
        for (int i = 0; i < sections; ++i)
        {
            const auto c = impulseInvariantLowPass<double> (sampleRate, (float) (frequency * scale), (float) qs[i]);
            const std::complex<double> zInv = std::polar (1.0, -juce::MathConstants<double>::twoPi * frequency / sampleRate);
            const auto response = c[1] * zInv / (1.0 + c[4] * zInv + c[5] * zInv * zInv);
            magnitude *= std::abs (response);
        }
        return magnitude;
    }

    template <typename T>
    struct DesignedBandT
    {
        FilterType type = FilterType::Bell;
        float frequency = 1000.0f;
        float gainDb = 0.0f;
        float q = 1.0f;
        int slopeDb = 12;
        int stageCount = 0;
        std::array<std::array<T, 6>, maxStages> raw {};
    };

    using DesignedBand = DesignedBandT<float>;

    template <typename T>
    inline T dbToGain (float db) noexcept
    {
        return (T) juce::Decibels::decibelsToGain ((double) db);
    }

    template <typename T>
    inline std::array<T, 6> padFirstOrder (const std::array<T, 4>& coeffs) noexcept
    {
        return { coeffs[0], coeffs[1], (T) 0, coeffs[2], coeffs[3], (T) 0 };
    }

    /** One stage of a fitted reference shape: kind 'L' low shelf, 'B' bell, 'H' high shelf. The offset is a log2 ratio to
        the reference frequency, the gain is a fraction of the commanded gain, and Q is the stage Q. */
    struct FittedStage
    {
        char kind;
        double offset;
        double q;
        double fraction;
    };

    /** Builds a fitted cascade into a designed band. The fits come from the measured baseline (scratchpad/response_probe,
        KShape): they hold across the commanded frequency and gain, with the reference frequency scaling the shape. */
    template <typename T>
    inline void buildFittedCascade (DesignedBandT<T>& designed, double sampleRate, double reference,
                                    const FittedStage* stages, int count, float gainDb) noexcept
    {
        using Coeffs = juce::dsp::IIR::ArrayCoefficients<T>;
        for (int i = 0; i < count && i < maxStages; ++i)
        {
            const auto& stage = stages[i];
            const float frequency = clampFrequency ((float) (reference * std::pow (2.0, stage.offset)), sampleRate);
            const T linear = dbToGain<T> ((float) (stage.fraction * gainDb));
            switch (stage.kind)
            {
                case 'L': designed.raw[(size_t) i] = Coeffs::makeLowShelf (sampleRate, frequency, (float) stage.q, linear); break;
                case 'H': designed.raw[(size_t) i] = Coeffs::makeHighShelf (sampleRate, frequency, (float) stage.q, linear); break;
                case 'P': designed.raw[(size_t) i] = Coeffs::makeHighPass (sampleRate, frequency, (float) stage.q); break;
                case 'Q': designed.raw[(size_t) i] = Coeffs::makeLowPass (sampleRate, frequency, (float) stage.q); break;
                default:  designed.raw[(size_t) i] = Coeffs::makePeakFilter (sampleRate, frequency, (float) stage.q, linear); break;
            }
        }
        designed.stageCount = juce::jmin (count, maxStages);
    }

    template <typename T>
    inline DesignedBandT<T> designBandT (FilterType type, float frequency, float gainDb, float q,
                                     int slopeDb, double sampleRate) noexcept
    {
        DesignedBandT<T> designed;
        designed.type = type;
        designed.frequency = clampFrequency (frequency, sampleRate);
        designed.gainDb = gainDb;
        designed.q = clampQ (q);
        designed.slopeDb = slopeDb;
        designed.stageCount = 1;

        if (sampleRate <= 0.0)
            return designed;

        using Coeffs = juce::dsp::IIR::ArrayCoefficients<T>;

        // Generic types with Imported slope: analogue Butterworth-family prototypes of the slope's order, digitised
        // with a Nyquist-matched transform (FilterDesign). A band that does nothing (order 0, or 0 dB) is unity.
        if (isEncodedSlope (slopeDb) && isGenericFilter (type))
        {
            FilterDesign::Shape shape = FilterDesign::Shape::Bell;
            switch (type)
            {
                case FilterType::LowShelf:  shape = FilterDesign::Shape::LowShelf; break;
                case FilterType::HighShelf: shape = FilterDesign::Shape::HighShelf; break;
                case FilterType::Notch:     shape = FilterDesign::Shape::Notch; break;
                case FilterType::BandPass:  shape = FilterDesign::Shape::BandPass; break;
                case FilterType::LowCut:    shape = FilterDesign::Shape::HighPass; break;
                case FilterType::HighCut:   shape = FilterDesign::Shape::LowPass; break;
                default:                    shape = FilterDesign::Shape::Bell; break;
            }
            FilterDesign::Biquad sections[maxStages];
            const int count = FilterDesign::design (shape, sampleRate, designed.frequency, gainDb, designed.q,
                                                       decodeSlopeNorm (slopeDb), sections, maxStages);
            if (count == 0)
            {
                designed.raw[0] = { T (1), T (0), T (0), T (1), T (0), T (0) };
                designed.stageCount = 1;
                return designed;
            }
            for (int i = 0; i < count; ++i)
                designed.raw[(size_t) i] = { T (sections[i].b0), T (sections[i].b1), T (sections[i].b2),
                                             T (1), T (sections[i].a1), T (sections[i].a2) };
            designed.stageCount = count;
            return designed;
        }

        // Vintage types with closed-form analogue models (VintageFilters), digitised like the generic types.
        {
            bool vintage = true;
            auto vintageType = VintageFilters::Type::BlueLow;
            switch (type)
            {
                case FilterType::BlueLow:       vintageType = VintageFilters::Type::BlueLow; break;
                case FilterType::BlueHigh:      vintageType = VintageFilters::Type::BlueHigh; break;
                case FilterType::GHighPass:     vintageType = VintageFilters::Type::GHighPass; break;
                case FilterType::ELowPass:      vintageType = VintageFilters::Type::ELowPass; break;
                case FilterType::GLowPass:      vintageType = VintageFilters::Type::GLowPass; break;
                case FilterType::VtLowPass:     vintageType = VintageFilters::Type::VtLowPass; break;
                case FilterType::BritNHighPass: vintageType = VintageFilters::Type::BritNHighPass; break;
                case FilterType::VtLowShelf:    vintageType = VintageFilters::Type::VtLowShelf; break;
                case FilterType::ToneLow:       vintageType = VintageFilters::Type::ToneLow; break;
                case FilterType::ToneHigh:      vintageType = VintageFilters::Type::ToneHigh; break;
                default:                        vintage = false; break;
            }
            AnalogModel::AnalogCascade cascade;
            const double vintageSlope = isEncodedSlope (slopeDb) ? decodeSlopeNorm (slopeDb) : 0.0;
            if (vintage && ! VintageFilters::designAnalog (vintageType, designed.frequency, gainDb, designed.q, cascade, vintageSlope))
            {
                // The circuit is flat at this setting.
                designed.raw[0] = { T (1), T (0), T (0), T (1), T (0), T (0) };
                designed.stageCount = 1;
                return designed;
            }
            if (vintage)
            {
                FilterDesign::Biquad sections[maxStages];
                const int count = FilterDesign::digitise (cascade, sampleRate, sections, maxStages);
                for (int i = 0; i < count; ++i)
                    designed.raw[(size_t) i] = { T (sections[i].b0), T (sections[i].b1), T (sections[i].b2),
                                                 T (1), T (sections[i].a1), T (sections[i].a2) };
                designed.stageCount = juce::jmax (1, count);
                return designed;
            }
        }

        // VT Atten (scratchpad/response_probe, fits/character-shelves.md): a reduce-only high shelf whose knee, Q and gain follow
        // the cut depth. It ignores the Q and slope controls, like the plug-in.
        if (type == FilterType::VtAtten)
        {
            const float g = gainDb;
            int written = 0;
            auto shelf = [&] (char kind, float stageFreq, float stageQ, float stageGainDb)
            {
                if (written >= maxStages || std::abs (stageGainDb) < 1.0e-3f)
                    return;
                const float clamped = clampFrequency (stageFreq, sampleRate);
                const T linear = dbToGain<T> (stageGainDb);
                designed.raw[(size_t) written] = kind == 'L' ? Coeffs::makeLowShelf (sampleRate, clamped, stageQ, linear)
                                            : kind == 'H' ? Coeffs::makeHighShelf (sampleRate, clamped, stageQ, linear)
                                                          : Coeffs::makePeakFilter (sampleRate, clamped, stageQ, linear);
                ++written;
            };

            {
                // Reduce-only. One high shelf whose knee, Q and effective gain all follow the cut depth; the
                // effective gain saturates hard at -18.246 dB. Gains >= 0 do nothing.
                const float u = juce::jmax (0.0f, -g);
                float s = 0.0f;
                if (u > 0.0f && u <= 12.0f)
                    s = -1.048316765f * u + 0.006819794f * u * u - 0.001110705f * u * u * u;
                else if (u > 12.0f)
                {
                    const float s12 = -13.517f;
                    s = s12 + (u - 12.0f) * (-18.246f - s12) / 3.0f;
                }
                s = juce::jlimit (-18.246f, 0.0f, s);
                const float qs = juce::jlimit (0.02f, 12.0f, 0.490742950f + 0.005047484f * u - 0.000944086f * u * u + 0.000026823f * u * u * u);
                const float ck = 934.552396f - 102.560885f * u + 12.617993f * u * u - 0.330062f * u * u * u;
                const float knee = ck * juce::jmax (1.0f, designed.frequency / 1033.0f);
                shelf ('H', knee, qs, s);
                if (written == 0)
                {
                    // A band that does nothing is unity, like the generic path.
                    designed.raw[0] = { T (1), T (0), T (0), T (1), T (0), T (0) };
                    written = 1;
                }
                designed.stageCount = written;
                designed.q = clampQ (0.5f);
                return designed;
            }
        }

        // Shapes fitted to the measured baseline (scratchpad/response_probe, KShape). See buildFittedCascade.
        {
            static constexpr FittedStage eq250Low[] = { { 'L', 2.9218, 1.0351, -0.0440 }, { 'B', -0.0170, 0.2005, 1.0068 }, { 'H', 1.7977, 1.0938, 0.0284 } };
            static constexpr FittedStage eq250High[] = { { 'L', 3.4258, 1.1647, 0.0110 }, { 'B', -0.0809, 0.2030, 0.8171 }, { 'H', 1.1638, 0.7193, 0.0455 } };
            static constexpr FittedStage eHighBell[] = { { 'B', 1.9916, 0.0520, 0.1404 }, { 'B', -0.0223, 0.2982, 0.8721 }, { 'H', 2.3232, 0.7071, 0.0748 } };
            static constexpr FittedStage eHighShelf[] = { { 'L', -0.1149, 1.1364, -0.0081 }, { 'B', 2.2222, 0.2845, 0.1236 }, { 'H', -0.6572, 0.4902, 0.9835 } };
            static constexpr FittedStage eLowBell[] = { { 'B', -0.0690, 0.2956, 1.0020 } };
            static constexpr FittedStage britNLow[] = { { 'L', 1.4000, 0.6381, 0.8131 }, { 'B', -3.7966, 0.1135, 0.1921 }, { 'H', 0.0925, 1.6072, -0.0482 } };
            static constexpr FittedStage britNHigh[] = { { 'L', -0.0379, 1.1411, -0.1100 }, { 'B', 0.2354, 0.6795, -0.1600 }, { 'H', -1.0403, 0.6086, 1.0216 } };
            static constexpr FittedStage flatTop[] = { { 'B', 0.2183, 1.1289, 0.2475 }, { 'B', -0.0693, 1.0216, 0.7841 } };
            static constexpr FittedStage gLmf[] = { { 'B', 0.0053, 0.3474, 1.0004 } };
            // High passes: two bilinear second-order sections, fitted with their Q free. The second sits near 8 Hz.
            static constexpr FittedStage eHighPass[] = { { 'P', 0.1872, 0.7538, 0.0 }, { 'P', -6.9283, 0.7372, 0.0 } };
            static constexpr FittedStage vtHighPass[] = { { 'P', -0.0523, 0.6828, 0.0 }, { 'P', -6.6508, 0.7073, 0.0 } };
            static constexpr FittedStage gLowShelf[] = { { 'L', 0.7375, 0.4724, 0.7671 }, { 'B', -3.1104, 0.0778, 0.2419 }, { 'H', -3.7309, 1.1020, -0.0036 } };
            static constexpr FittedStage gHighShelf[] = { { 'L', -0.3110, 1.1597, -0.0508 }, { 'B', -0.4015, 0.5501, -0.1713 }, { 'H', -1.2205, 0.5862, 0.9925 } };
            switch (type)
            {
                case FilterType::Eq250LowShelf:  buildFittedCascade (designed, sampleRate, juce::jmin (1000.0, (double) designed.frequency), eq250Low, 3, gainDb); designed.q = clampQ (0.2f); return designed;
                case FilterType::Eq250HighShelf: buildFittedCascade (designed, sampleRate, 1000.0, eq250High, 3, gainDb); designed.q = clampQ (0.2f); return designed;
                case FilterType::EHighBell:      buildFittedCascade (designed, sampleRate, designed.frequency, eHighBell, 3, gainDb); designed.q = clampQ (0.2f); return designed;
                case FilterType::EHighShelf:     buildFittedCascade (designed, sampleRate, designed.frequency, eHighShelf, 3, gainDb); designed.q = clampQ (0.2f); return designed;
                case FilterType::ELowBell:       buildFittedCascade (designed, sampleRate, designed.frequency, eLowBell, 1, gainDb); designed.q = clampQ (0.3f); return designed;
                case FilterType::BritNLowShelf:  buildFittedCascade (designed, sampleRate, designed.frequency, britNLow, 3, gainDb); designed.q = clampQ (0.6f); return designed;
                case FilterType::BritNHighShelf: buildFittedCascade (designed, sampleRate, designed.frequency, britNHigh, 3, gainDb); designed.q = clampQ (0.6f); return designed;
                case FilterType::FlatTop:        buildFittedCascade (designed, sampleRate, designed.frequency, flatTop, 2, gainDb); designed.q = clampQ (1.1f); return designed;
                case FilterType::GLmf:           buildFittedCascade (designed, sampleRate, designed.frequency, gLmf, 1, gainDb); designed.q = clampQ (0.35f); return designed;
                case FilterType::GLowShelf:      buildFittedCascade (designed, sampleRate, designed.frequency, gLowShelf, 3, gainDb); designed.q = clampQ (0.5f); return designed;
                case FilterType::EHighPass:      buildFittedCascade (designed, sampleRate, designed.frequency, eHighPass, 2, gainDb); designed.q = clampQ (0.74f); return designed;
                case FilterType::VtHighPass:     buildFittedCascade (designed, sampleRate, designed.frequency, vtHighPass, 2, gainDb); designed.q = clampQ (0.7f); return designed;
                case FilterType::GHighShelf:     buildFittedCascade (designed, sampleRate, designed.frequency, gHighShelf, 3, gainDb); designed.q = clampQ (0.6f); return designed;
                default: break;
            }
        }

        // Families built on a base shape: design the shape, then apply the family's traits.
        const auto traits = typeTraits (type);
        if (traits.base != type)
        {
            float gain = gainDb;
            if (traits.gainLimit > 0)
                gain = juce::jmax (0.0f, gain);
            else if (traits.gainLimit < 0)
                gain = juce::jmin (0.0f, gain);
            const float qUsed = traits.fixedQ > 0.0f ? traits.fixedQ : q * traits.qScale;

            DesignedBandT<T> shape = designBandT<T> (traits.base, frequency, gain, qUsed, slopeDb, sampleRate);
            shape.type = type;
            shape.gainDb = gain;
            shape.q = clampQ (qUsed);

            if (traits.mirror)
            {
                // z -> -z moves the phase transition from fc to Nyquist - fc.
                for (int i = 0; i < shape.stageCount; ++i)
                {
                    shape.raw[(size_t) i][1] = -shape.raw[(size_t) i][1];
                    shape.raw[(size_t) i][4] = -shape.raw[(size_t) i][4];
                }
            }
            if (traits.midSink && shape.stageCount < maxStages)
            {
                // A mid dip at three times the shelf frequency, opposite to the shelf gain.
                shape.raw[(size_t) shape.stageCount] = Coeffs::makePeakFilter (sampleRate,
                    clampFrequency (shape.frequency * 3.0f, sampleRate), 0.7f,
                    dbToGain<T> (-0.25f * gain));
                ++shape.stageCount;
            }
            if (traits.highLoss && shape.stageCount < maxStages)
            {
                shape.raw[(size_t) shape.stageCount] = Coeffs::makeHighShelf (sampleRate,
                    clampFrequency (4000.0f, sampleRate), 0.7f,
                    dbToGain<T> (-0.2f * juce::jmax (0.0f, gain)));
                ++shape.stageCount;
            }
            return shape;
        }

        const T linearGain = dbToGain<T> (designed.gainDb);

        switch (type)
        {
            case FilterType::Bell:
                designed.raw[0] = Coeffs::makePeakFilter (sampleRate, designed.frequency, designed.q, linearGain);
                break;
            case FilterType::LowShelf:
                designed.raw[0] = Coeffs::makeLowShelf (sampleRate, designed.frequency, designed.q, linearGain);
                break;
            case FilterType::HighShelf:
                designed.raw[0] = Coeffs::makeHighShelf (sampleRate, designed.frequency, designed.q, linearGain);
                break;
            case FilterType::TiltShelf:
            {
                // reference tilt (historical response baseline): a low shelf at minus the commanded gain
                // cascaded with a high shelf at plus the commanded gain, sharing the pivot. The lows go down by the
                // full gain and the highs go up by it. The shelf Q is set by the Q parameter.
                const float full = dbToGain<T> (designed.gainDb);
                const float inv = dbToGain<T> (-designed.gainDb);
                designed.stageCount = 2;
                designed.raw[0] = Coeffs::makeLowShelf (sampleRate, designed.frequency, designed.q, inv);
                designed.raw[1] = Coeffs::makeHighShelf (sampleRate, designed.frequency, designed.q, full);
                break;
            }
            case FilterType::Notch:
                designed.raw[0] = Coeffs::makeNotch (sampleRate, designed.frequency, designed.q);
                break;
            case FilterType::BandPass:
                designed.raw[0] = Coeffs::makeBandPass (sampleRate, designed.frequency, designed.q);
                break;
            case FilterType::AllPass:
                designed.raw[0] = Coeffs::makeAllPass (sampleRate, designed.frequency, designed.q);
                break;
            case FilterType::SoftBell:
            {
                // Original soft bell: the same peak-gain definition as Bell, with Q
                // tightening slightly as |gain| grows. qEff = q * (1 + 0.15 * |gain| / 12).
                const float qEff = clampQ (designed.q * (1.0f + 0.15f * std::abs (designed.gainDb) / 12.0f));
                designed.q = qEff;
                designed.raw[0] = Coeffs::makePeakFilter (sampleRate, designed.frequency, qEff, linearGain);
                break;
            }
            case FilterType::FlatTilt:
            {
                // reference flat tilt, fitted to the measured baseline (scratchpad/response_probe, KFlat9):
                // a straight line in dB against log frequency, about gainDb / 6 dB per octave, pivoting at the
                // commanded frequency. The fit is 15 shelves at fixed absolute pivots (1 kHz times 2^(k - 7)) with
                // fixed gains, scaled by gainDb / 6, plus one flat gain stage. Moving the pivot away from 1 kHz adds
                // a flat offset of (gainDb / 6) * log2(1 kHz / pivot). The fit matches the plug-in to 0.04 to 0.08 dB
                // RMS at 120 Hz, 1 kHz and 5 kHz, and at +-6 and +-12 dB. The shelf Q is fixed, so the Q control
                // does not apply to this type.
                static constexpr float gainsAt6[15] = { -1.69327f, -1.83087f, -5.98842f, -5.32816f, 2.00866f, 6.91748f, 0.42315f, 8.65194f, 7.76851f, 3.25589f, -5.18791f, -0.01847f, -3.70616f, -3.70616f, -3.70616f };
                constexpr float shelfQ = 0.15f;
                const float scale = designed.gainDb / 6.0f;
                int count = 0;
                for (int k = 0; k < 15; ++k)
                {
                    const float pivot = clampFrequency (1000.0f * std::pow (2.0f, (float) (k - 7)), sampleRate);
                    designed.raw[(size_t) count] = Coeffs::makeHighShelf (sampleRate, pivot, shelfQ,
                                                                          dbToGain<T> (gainsAt6[k] * scale));
                    ++count;
                }
                const float offsetDb = scale * std::log2 (1000.0f / designed.frequency);
                designed.raw[(size_t) count] = { dbToGain<T> (offsetDb), T (0), T (0), T (1), T (0), T (0) };
                ++count;
                designed.stageCount = count;
                designed.q = shelfQ;
                break;
            }
            case FilterType::FlatTop:
            {
                // Two peaks placed symmetrically around the centre. Each carries
                // 0.88 of the commanded gain. Their overlap lands near that gain,
                // and the top stays wider than one bell at the same Q.
                const float octaves = 0.45f / juce::jmax (0.1f, designed.q);
                const float ratio = std::pow (2.0f, octaves);
                const float stageQ = clampQ (designed.q * 1.35f);
                const float stageGain = dbToGain<T> (designed.gainDb * 0.88f);
                designed.stageCount = 2;
                designed.raw[0] = Coeffs::makePeakFilter (sampleRate,
                    clampFrequency (designed.frequency / ratio, sampleRate), stageQ, stageGain);
                designed.raw[1] = Coeffs::makePeakFilter (sampleRate,
                    clampFrequency (designed.frequency * ratio, sampleRate), stageQ, stageGain);
                break;
            }
            case FilterType::NarrowResonance:
            {
                // Same centre-gain definition as Bell. The Q knob is scaled by 4,
                // so the same position is a tighter linear resonance. Drive is separate.
                const float qEff = clampQ (designed.q * 4.0f);
                designed.q = qEff;
                designed.raw[0] = Coeffs::makePeakFilter (sampleRate, designed.frequency, qEff, linearGain);
                break;
            }
            case FilterType::LowCut:
            case FilterType::HighCut:
            case FilterType::BrickwallLowCut:
            case FilterType::BrickwallHighCut:
            {
                const bool highPass = (type == FilterType::LowCut || type == FilterType::BrickwallLowCut);
                if (isBrickwall (type))
                {
                    // reference brickwall, fitted to the measured baseline (scratchpad/response_probe,
                    // fits/wall_joint_fit.json): 15 bilinear (RBJ) sections whose ratios to the commanded
                    // frequency and Qs reproduce the measured knee and roll-off (0.25 dB RMS, 1.6 dB max
                    // over the four measured wall rows). LP and HP mirror each other: the HP uses fc / ratio.
                    static constexpr double wallRatios[15] = { 1.0287, 0.9848, 0.5000, 1.7615, 0.8422, 1.8274, 3.2683,
                                                               1.5256, 2.4631, 2.3359, 0.6815, 1.7172, 2.3688, 1.7555, 2.3451 };
                    static constexpr double wallQs[15] = { 21.3706, 13.7389, 1.0297, 0.3525, 6.0471, 0.3053, 5.1587,
                                                           0.3295, 0.5592, 0.3681, 2.2832, 0.3298, 0.3889, 0.3759, 0.2564 };
                    designed.stageCount = 15;
                    for (int i = 0; i < 15; ++i)
                    {
                        const float stageFreq = clampFrequency (
                            (float) (designed.frequency * (highPass ? 1.0 / wallRatios[(size_t) i] : wallRatios[(size_t) i])),
                            sampleRate);
                        designed.raw[(size_t) i] = highPass
                            ? Coeffs::makeHighPass (sampleRate, stageFreq, (float) wallQs[(size_t) i])
                            : Coeffs::makeLowPass (sampleRate, stageFreq, (float) wallQs[(size_t) i]);
                    }
                    break;
                }

                const int poles = juce::jlimit (1, maxStages * 2, juce::roundToInt (slopeDb / 6.0f));
                if (poles <= 1)
                {
                    const auto first = highPass
                        ? Coeffs::makeFirstOrderHighPass (sampleRate, designed.frequency)
                        : Coeffs::makeFirstOrderLowPass (sampleRate, designed.frequency);
                    designed.raw[0] = padFirstOrder<T> (first);
                    break;
                }

                const int sections = juce::jmin (ButterworthDesign::sectionCount (poles), maxStages);
                double qs[(size_t) maxStages] = {};
                ButterworthDesign::cascadeQs (poles, qs, sections);
                designed.stageCount = sections;

                // Low passes are impulse-invariant, which moves the cutoff slightly. The cascade's cutoffs are scaled so that
                // the magnitude at the commanded frequency is exactly -3 dB, as the Butterworth design says.
                double scale = 1.0;
                if (! highPass)
                {
                    const double target = 1.0 / std::sqrt (2.0);
                    double low = 0.9, high = 1.1;
                    for (int iteration = 0; iteration < 40; ++iteration)
                    {
                        const double mid = 0.5 * (low + high);
                        // The magnitude at the commanded frequency rises with the cutoff scale.
                        const double value = impulseLowPassCascadeMagnitude (sampleRate, designed.frequency, qs, sections, mid);
                        if (value > target) high = mid; else low = mid;
                    }
                    scale = 0.5 * (low + high);
                }
                for (int i = 0; i < sections; ++i)
                {
                    const float stageQ = (float) qs[(size_t) i];
                    designed.raw[(size_t) i] = highPass
                        ? Coeffs::makeHighPass (sampleRate, designed.frequency, stageQ)
                        : impulseInvariantLowPass<T> (sampleRate, designed.frequency * (float) scale, stageQ);
                }
                break;
            }
            // Family types are returned by the traits branch above and never reach this switch.
            case FilterType::Sword:
            case FilterType::AllPassHigh:
            case FilterType::EHighPass:
            case FilterType::ELowPass:
            case FilterType::ELowShelf:
            case FilterType::EHighShelf:
            case FilterType::ELmf:
            case FilterType::EHmf:
            case FilterType::ELowBell:
            case FilterType::EHighBell:
            case FilterType::GLmf:
            case FilterType::BritNHighPass:
            case FilterType::BritNLowShelf:
            case FilterType::BritNBell:
            case FilterType::BritNHighShelf:
            case FilterType::Eq250Bell:
            case FilterType::Eq250LowShelf:
            case FilterType::Eq250HighShelf:
            case FilterType::VtHighPass:
            case FilterType::VtLowPass:
            case FilterType::VtLowShelf:
            case FilterType::VtBoost:
            case FilterType::VtAtten:
            case FilterType::VtLmf:
            case FilterType::VtHmf:
            case FilterType::ToneLow:
            case FilterType::ToneHigh:
            case FilterType::BlueLow:
            case FilterType::GLowShelf:
            case FilterType::GHighShelf:
            case FilterType::BlueHigh:
            case FilterType::GHighPass:
            case FilterType::GLowPass:
                break;
        }

        return designed;
    }

    /** Float design for the graph, linear-phase kernels, and the tests. */
    inline DesignedBand designBand (FilterType type, float frequency, float gainDb, float q,
                                     int slopeDb, double sampleRate) noexcept
    {
        return designBandT<float> (type, frequency, gainDb, q, slopeDb, sampleRate);
    }

    /** Matches juce::dsp::IIR::Coefficients::assignImpl for a six-coefficient biquad:
        divide by a0 and drop a0, leaving b0 b1 b2 a1 a2. */
    inline NormalizedStage normalizeBiquad (const std::array<float, 6>& raw) noexcept
    {
        NormalizedStage stage;
        stage.order = 2;
        const float a0 = raw[3];
        const float inv = std::abs (a0) > 1.0e-12f ? 1.0f / a0 : 0.0f;
        stage.c[0] = raw[0] * inv;
        stage.c[1] = raw[1] * inv;
        stage.c[2] = raw[2] * inv;
        stage.c[3] = raw[4] * inv;
        stage.c[4] = raw[5] * inv;
        return stage;
    }

    inline double stageMagnitude (const NormalizedStage& stage, double frequency, double sampleRate) noexcept
    {
        if (stage.order <= 0 || sampleRate <= 0.0 || ! std::isfinite (frequency))
            return 1.0;

        const double nyquist = sampleRate * 0.5;
        const double f = juce::jlimit (0.0, nyquist * 0.999, frequency);
        const auto jw = std::exp (std::complex<double> (0.0, -juce::MathConstants<double>::twoPi * f / sampleRate));

        std::complex<double> numerator = 0.0;
        std::complex<double> factor = 1.0;
        for (int n = 0; n <= stage.order; ++n)
        {
            numerator += (double) stage.c[n] * factor;
            factor *= jw;
        }

        std::complex<double> denominator = 1.0;
        factor = jw;
        for (int n = 0; n < stage.order; ++n)
        {
            denominator += (double) stage.c[stage.order + 1 + n] * factor;
            factor *= jw;
        }

        const double mag = std::abs (numerator / denominator);
        return std::isfinite (mag) ? mag : 1.0;
    }

    inline double designedMagnitude (const DesignedBand& designed, double frequency, double sampleRate) noexcept
    {
        double mag = 1.0;
        for (int i = 0; i < designed.stageCount; ++i)
            mag *= stageMagnitude (normalizeBiquad (designed.raw[(size_t) i]), frequency, sampleRate);
        return mag;
    }

    struct GraphBand
    {
        bool commandedActive = false;
        bool audible = false;
        FilterType type = FilterType::Bell;
        StereoMode stereoMode = StereoMode::Stereo;
        float targetFrequency = 1000.0f;
        float targetGainDb = 0.0f;
        float appliedFrequency = 1000.0f;
        float appliedGainDb = 0.0f;
        float appliedQ = 1.0f;
        int slopeDb = 12;
        int stageCount = 0;
        float pan = 0.0f; // -1 .. 1, used by Stereo (left/right balance) and MidSide (mid/side balance)
        bool splitOn = false;
        float splitTransDb = 0.0f;
        float splitTailDb = 0.0f;
        /** The band as designed. For a split band this is the transient path (gain + transient gain). */
        std::array<NormalizedStage, maxStages> stages {};
        /** Split bands only: the tonal path (gain + tonal gain). It has the same stage count as `stages`. */
        std::array<NormalizedStage, maxStages> tailStages {};
    };

    struct GraphSnapshot
    {
        double sampleRate = 44100.0;
        std::array<GraphBand, graphBandCount> bands {};
    };

    inline bool hasSplitBands (const GraphSnapshot& snapshot) noexcept
    {
        for (const auto& band : snapshot.bands)
            if (band.audible && band.splitOn)
                return true;
        return false;
    }

    /** One band's magnitude on the tonal path. A band without Split has one path, so both streams share it. */
    inline double bandTonalMagnitude (const GraphBand& band, double frequency, double sampleRate) noexcept
    {
        const auto& source = band.splitOn ? band.tailStages : band.stages;
        double mag = 1.0;
        for (int i = 0; i < band.stageCount; ++i)
            mag *= stageMagnitude (source[(size_t) i], frequency, sampleRate);
        return std::isfinite (mag) ? mag : 1.0;
    }

    /** One band's magnitude on the transient path. */
    inline double bandTransientMagnitude (const GraphBand& band, double frequency, double sampleRate) noexcept
    {
        double mag = 1.0;
        for (int i = 0; i < band.stageCount; ++i)
            mag *= stageMagnitude (band.stages[(size_t) i], frequency, sampleRate);
        return std::isfinite (mag) ? mag : 1.0;
    }

    /** The EQ curve the transient stream passes through: every audible band at its transient gain. */
    inline float splitTransientMagnitude (const GraphSnapshot& snapshot, double frequency) noexcept
    {
        double mag = 1.0;
        for (const auto& band : snapshot.bands)
            if (band.audible)
                mag *= bandTransientMagnitude (band, frequency, snapshot.sampleRate);
        return std::isfinite (mag) ? (float) mag : 1.0f;
    }

    /** The EQ curve the tonal stream passes through: every audible band at its tonal gain. */
    inline float splitTonalMagnitude (const GraphSnapshot& snapshot, double frequency) noexcept
    {
        double mag = 1.0;
        for (const auto& band : snapshot.bands)
            if (band.audible)
                mag *= bandTonalMagnitude (band, frequency, snapshot.sampleRate);
        return std::isfinite (mag) ? (float) mag : 1.0f;
    }

    inline float totalMagnitude (const GraphSnapshot& snapshot, double frequency) noexcept
    {
        double mag = 1.0;
        for (const auto& band : snapshot.bands)
        {
            if (! band.audible)
                continue;
            for (int i = 0; i < band.stageCount; ++i)
                mag *= stageMagnitude (band.stages[(size_t) i], frequency, snapshot.sampleRate);
        }
        return std::isfinite (mag) ? (float) mag : 1.0f;
    }

    inline double bandMagnitude (const GraphBand& band, double frequency, double sampleRate) noexcept
    {
        double mag = 1.0;
        for (int i = 0; i < band.stageCount; ++i)
            mag *= stageMagnitude (band.stages[(size_t) i], frequency, sampleRate);
        return std::isfinite (mag) ? mag : 1.0;
    }

    /** Real 2×2 gain at one frequency. Bands are in series.
        Mid/side entries are signed gains built from the band magnitude, which is the
        same magnitude model the linear-phase kernel uses. */
    struct RealMatrix
    {
        double ll = 1.0;
        double lr = 0.0;
        double rl = 0.0;
        double rr = 1.0;
    };

    /** Wet weights for a pan position. Left and right (or mid and side) each keep a share of the band:
        a full pan to one side removes the band from the other. Centre is 1 and 1. */
    inline void panWeights (float pan, double& left, double& right) noexcept
    {
        const double p = juce::jlimit (-1.0, 1.0, (double) pan);
        left = juce::jmin (1.0, 1.0 - p);
        right = juce::jmin (1.0, 1.0 + p);
    }

    inline RealMatrix bandMatrix (double linearGain, StereoMode mode, float pan = 0.0f) noexcept
    {
        const double H = std::isfinite (linearGain) ? linearGain : 1.0;
        double weightA = 1.0, weightB = 1.0;
        panWeights (pan, weightA, weightB);
        switch (mode)
        {
            case StereoMode::MidSide:
            {
                // Mid gain and side gain, each mixed between the filtered and the dry signal by its weight.
                const double mid = weightA * H + (1.0 - weightA);
                const double side = weightB * H + (1.0 - weightB);
                return { 0.5 * (mid + side), 0.5 * (mid - side), 0.5 * (mid - side), 0.5 * (mid + side) };
            }
            case StereoMode::Left:  return { H, 0.0, 0.0, 1.0 };
            case StereoMode::Right: return { 1.0, 0.0, 0.0, H };
            case StereoMode::Mid:
            {
                const double a = 0.5 * H + 0.5;
                const double b = 0.5 * H - 0.5;
                return { a, b, b, a };
            }
            case StereoMode::Side:
            {
                const double a = 0.5 + 0.5 * H;
                const double b = 0.5 - 0.5 * H;
                return { a, b, b, a };
            }
            case StereoMode::Stereo:
            default:
            {
                // Left and right keep their own dry share. Centre pan is the plain band on both channels.
                const double left = weightA * H + (1.0 - weightA);
                const double right = weightB * H + (1.0 - weightB);
                return { left, 0.0, 0.0, right };
            }
        }
    }

    inline RealMatrix multiply (RealMatrix newer, RealMatrix older) noexcept
    {
        return {
            newer.ll * older.ll + newer.lr * older.rl,
            newer.ll * older.lr + newer.lr * older.rr,
            newer.rl * older.ll + newer.rr * older.rl,
            newer.rl * older.lr + newer.rr * older.rr
        };
    }

    inline RealMatrix responseMatrix (const GraphSnapshot& snapshot, double frequency) noexcept
    {
        RealMatrix matrix;
        for (const auto& band : snapshot.bands)
        {
            if (! band.audible || band.stageCount <= 0)
                continue;
            matrix = multiply (bandMatrix (bandMagnitude (band, frequency, snapshot.sampleRate), band.stereoMode, band.pan), matrix);
        }
        return matrix;
    }

    inline bool hasMixedRouting (const GraphSnapshot& snapshot) noexcept
    {
        for (const auto& band : snapshot.bands)
            if (band.audible && (band.stereoMode != StereoMode::Stereo || std::abs (band.pan) > 1.0e-6f))
                return true;
        return false;
    }

    inline double stagePhase (const NormalizedStage& stage, double frequency, double sampleRate) noexcept
    {
        if (stage.order <= 0 || sampleRate <= 0.0 || ! std::isfinite (frequency))
            return 0.0;

        const double nyquist = sampleRate * 0.5;
        const double f = juce::jlimit (0.0, nyquist * 0.999, frequency);
        const auto jw = std::exp (std::complex<double> (0.0, -juce::MathConstants<double>::twoPi * f / sampleRate));

        std::complex<double> numerator = 0.0;
        std::complex<double> factor = 1.0;
        for (int n = 0; n <= stage.order; ++n)
        {
            numerator += (double) stage.c[n] * factor;
            factor *= jw;
        }

        std::complex<double> denominator = 1.0;
        factor = jw;
        for (int n = 0; n < stage.order; ++n)
        {
            denominator += (double) stage.c[stage.order + 1 + n] * factor;
            factor *= jw;
        }

        return std::arg (numerator / denominator);
    }
    /** Second-order all-pass coefficients for Digital+ phase compensation . Fits a resonant
        all-pass (radius r, resonance angle theta) so the section cascaded with the already-fitted all-pass
        stages deviates least from a straight phase line around the section frequency. Grid-fit at design time;
        allocation-free. */
    inline void digitalPlusAllPassCoefficients (const std::array<double, 6>& rawSection, float frequency,
                                                double sampleRate, double lowRatio, double highRatio,
                                                const double* existingD1, const double* existingD2, int existingCount,
                                                double& outD1, double& outD2) noexcept
    {
        const std::array<float, 6> rawFloat { (float) rawSection[0], (float) rawSection[1], (float) rawSection[2],
                                             (float) rawSection[3], (float) rawSection[4], (float) rawSection[5] };
        const NormalizedStage section = normalizeBiquad (rawFloat);
        outD1 = 0.0;
        outD2 = 0.0;
        double bestError = 1.0e300;

        auto apPhase = [] (double d1, double d2, double w) -> double
        {
            const std::complex<double> z = std::polar (1.0, -w);
            return std::arg ((d2 + d1 * z + z * z) / (1.0 + d1 * z + d2 * z * z));
        };

        for (int radiusIndex = 0; radiusIndex < 15; ++radiusIndex)
        {
            const double r = 0.1 + 0.06 * (double) radiusIndex; // 0.1 .. 0.94
            for (int thetaIndex = 0; thetaIndex < 9; ++thetaIndex)
            {
                const double theta = 0.15 + 0.0875 * (double) thetaIndex; // resonance angle, 0.15..0.85 pi
                const double d1 = -2.0 * r * std::cos (theta * juce::MathConstants<double>::pi);
                const double d2 = r * r;

                double phases[5];
                double freqs[5];
                for (int k = 0; k < 5; ++k)
                {
                    const double ratio = lowRatio + (highRatio - lowRatio) * (double) k / 4.0;
                    const double f = juce::jlimit (1.0, sampleRate * 0.49, (double) frequency * ratio);
                    const double w = juce::MathConstants<double>::twoPi * f / sampleRate;
                    double phase = stagePhase (section, f, sampleRate) + apPhase (d1, d2, w);
                    for (int existing = 0; existing < existingCount; ++existing)
                        phase += apPhase (existingD1[existing], existingD2[existing], w);
                    phases[k] = phase;
                    freqs[k] = f;
                }
                for (int k = 1; k < 5; ++k)
                {
                    while (phases[k] - phases[k - 1] > juce::MathConstants<double>::pi)
                        phases[k] -= juce::MathConstants<double>::twoPi;
                    while (phases[k] - phases[k - 1] < -juce::MathConstants<double>::pi)
                        phases[k] += juce::MathConstants<double>::twoPi;
                }
                double sumF = 0.0, sumP = 0.0, sumFF = 0.0, sumFP = 0.0;
                for (int k = 0; k < 5; ++k)
                {
                    sumF += freqs[k];
                    sumP += phases[k];
                    sumFF += freqs[k] * freqs[k];
                    sumFP += freqs[k] * phases[k];
                }
                const double denominator = 5.0 * sumFF - sumF * sumF;
                const double slope = denominator > 1.0e-12 ? (5.0 * sumFP - sumF * sumP) / denominator : 0.0;
                const double intercept = (sumP - slope * sumF) / 5.0;
                double error = 0.0;
                for (int k = 0; k < 5; ++k)
                {
                    const double d = phases[k] - (slope * freqs[k] + intercept);
                    error += d * d;
                }
                if (error < bestError)
                {
                    bestError = error;
                    outD1 = d1;
                    outD2 = d2;
                }
            }
        }
    }

}
