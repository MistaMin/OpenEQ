#include "EQBand.h"
#include "UI/ThemeFile.h"
#include "PresetImport.h"
#include "BandControlPanel.h"
#include "DynamicsPanel.h"
#include "UI/AppLookAndFeel.h"
#include "PluginProcessor.h"
#include "EQCurveComponent.h"
#include "PluginEditor.h"
#include "Analyzer.h"
#include "LinearPhaseEngine.h"
#include "ResponseModel.h"
#include "CurveFit.h"
#include "SpectralEngine.h"
#include "InstanceRegistry.h"
#include "UI/ValueFormat.h"
#include "AllocInterpose.h"

void runFilterResponseTests();

#include <algorithm>
#include <cmath>
#include <dlfcn.h>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

static void require (bool condition, const char* message)
{
    if (! condition)
        throw std::runtime_error (message);
}

static void requireMsg (bool condition, const std::string& message)
{
    if (! condition)
        throw std::runtime_error (message);
}

static void setRaw (juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float value)
{
    auto* parameter = apvts.getParameter (id);
    requireMsg (parameter != nullptr, ("missing parameter " + id).toStdString());
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

static float rawValue (OpenEQAudioProcessor& processor, const juce::String& id)
{
    auto* value = processor.apvts.getRawParameterValue (id);
    requireMsg (value != nullptr, ("missing raw parameter " + id).toStdString());
    return value->load();
}

static void silenceBands (OpenEQAudioProcessor& processor)
{
    for (int i = 0; i < OpenEQAudioProcessor::numBands; ++i)
    {
        setRaw (processor.apvts, Params::bandActiveID (i), 0.0f);
        setRaw (processor.apvts, Params::bandSoloID (i), 0.0f);
        setRaw (processor.apvts, Params::bandDynOnID (i), 0.0f);
    }
}

static void configureBand (OpenEQAudioProcessor& processor, int index, FilterType type,
                           float frequency, float gainDb, float q, int slopeIndex, int stereoIndex, bool active)
{
    setRaw (processor.apvts, Params::bandTypeID (index), (float) type);
    setRaw (processor.apvts, Params::bandFreqID (index), frequency);
    setRaw (processor.apvts, Params::bandGainID (index), gainDb);
    setRaw (processor.apvts, Params::bandQID (index), q);
    setRaw (processor.apvts, Params::bandSlopeID (index), (float) slopeIndex);
    // The continuous slope drives the generic types: a cut keeps the dB/oct of its old slope index, everything else
    // uses slope 0 (a second-order bell, a first-order shelf).
    const bool cut = type == FilterType::LowCut || type == FilterType::HighCut;
    setRaw (processor.apvts, Params::bandSlopeNormID (index),
            cut ? Params::slopeNormForCutDb ((float) Params::slopeIndexToDb (slopeIndex)) : 0.0f);
    setRaw (processor.apvts, Params::bandStereoID (index), (float) stereoIndex);
    setRaw (processor.apvts, Params::bandActiveID (index), active ? 1.0f : 0.0f);
}

static void prepareStereo (OpenEQAudioProcessor& processor, double sampleRate, int block)
{
    processor.setRateAndBufferSizeDetails (sampleRate, block);
    processor.prepareToPlay (sampleRate, block);
}

static void process (OpenEQAudioProcessor& processor, juce::AudioBuffer<float>& buffer)
{
    juce::MidiBuffer midi;
    processor.processBlock (buffer, midi);
}

enum class Tone { Centered, Side };

static void fillTone (juce::AudioBuffer<float>& buffer, double sampleRate, double frequency,
                      float amplitude, int phaseSample, Tone tone)
{
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const float sample = amplitude * (float) std::sin (juce::MathConstants<double>::twoPi
                                                            * frequency * (double) (phaseSample + i) / sampleRate);
        buffer.setSample (0, i, sample);
        if (buffer.getNumChannels() > 1)
            buffer.setSample (1, i, tone == Tone::Side ? -sample : sample);
    }
}

static juce::AudioBuffer<float> copyBuffer (const juce::AudioBuffer<float>& source)
{
    juce::AudioBuffer<float> dest (source.getNumChannels(), source.getNumSamples());
    for (int channel = 0; channel < source.getNumChannels(); ++channel)
        dest.copyFrom (channel, 0, source, channel, 0, source.getNumSamples());
    return dest;
}

static bool allFinite (const juce::AudioBuffer<float>& buffer)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        const auto* data = buffer.getReadPointer (channel);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (! std::isfinite (data[i]))
                return false;
    }
    return true;
}

static float maxAbsDiff (const juce::AudioBuffer<float>& left, const juce::AudioBuffer<float>& right,
                         int channel, int start, int count)
{
    const auto* a = left.getReadPointer (channel);
    const auto* b = right.getReadPointer (channel);
    float maxDiff = 0.0f;
    for (int i = start; i < start + count; ++i)
        maxDiff = std::max (maxDiff, std::abs (a[i] - b[i]));
    return maxDiff;
}

static double rmsRange (const float* data, int start, int count)
{
    double energy = 0.0;
    for (int i = start; i < start + count; ++i)
        energy += (double) data[i] * (double) data[i];
    return std::sqrt (energy / (double) count);
}

static double dbRatio (double ratio)
{
    return 20.0 * std::log10 (std::max (ratio, 1.0e-12));
}

static double impulseMagnitude (const float* impulse, int length, double frequency, double sampleRate)
{
    double real = 0.0;
    double imaginary = 0.0;
    const double omega = juce::MathConstants<double>::twoPi * frequency / sampleRate;
    for (int n = 0; n < length; ++n)
    {
        real += (double) impulse[n] * std::cos (omega * (double) n);
        imaginary -= (double) impulse[n] * std::sin (omega * (double) n);
    }
    return std::hypot (real, imaginary);
}

static float bellMagnitude (float gainDb)
{
    EQBand band;
    band.prepare ({ 48000.0, 512, 1 });
    band.setParameters (FilterType::Bell, 1000.0f, gainDb, 1.0f, 12, StereoMode::Stereo, true);
    return band.getMagnitudeForFrequency (1000.0);
}

static void symbolize (const char* label, void* address)
{
    Dl_info info {};
    if (address != nullptr && dladdr (address, &info) != 0 && info.dli_sname != nullptr)
        std::cout << "  " << label << "_caller " << info.dli_sname << "\n";
    else
        std::cout << "  " << label << "_caller " << address << "\n";
}

static void checkCuts()
{
    constexpr double sampleRate = 48000.0;
    double maxModelError = 0.0;

    for (auto type : { FilterType::LowCut, FilterType::HighCut })
        for (int slope : { 6, 12, 24, 36, 48, 72, 96 })
        {
            EQBand band;
            band.prepare ({ sampleRate, 512, 1 });
            band.setParameters (type, 1000.0f, 0.0f, 1.0f, slope, StereoMode::Stereo, true);
            const bool lowCut = type == FilterType::LowCut;
            require (band.getMagnitudeForFrequency (lowCut ? 100.0 : 10000.0) < 0.11f,
                     "Cut must attenuate the named end of the spectrum");
            require (band.getMagnitudeForFrequency (lowCut ? 10000.0 : 100.0) > 0.98f,
                     "Cut must preserve the passband");
            require (std::abs (band.getMagnitudeForFrequency (1000.0) - std::sqrt (0.5f)) < 0.002f,
                     "Every Butterworth slope must be -3 dB at its cutoff");

            const auto designed = ResponseModel::designBand (type, 1000.0f, 0.0f, 1.0f, slope, sampleRate);

            std::vector<float> impulse (32768, 0.0f);
            impulse[0] = 1.0f;
            band.processChannel (impulse.data(), (int) impulse.size(), 0);
            for (double frequency : { 700.0, 1000.0, 1500.0 })
            {
                // High passes keep the bilinear-warped Butterworth. Low passes use the analogue curve, which the
                // impulse-invariant design follows and which reference measures (see FILTER_VALIDATION.md).
                const double ratio = std::tan (juce::MathConstants<double>::pi * frequency / sampleRate)
                                   / std::tan (juce::MathConstants<double>::pi * 1000.0 / sampleRate);
                const double analogueRatio = frequency / 1000.0;
                const double expected = lowCut
                    ? 1.0 / std::sqrt (1.0 + std::pow (1.0 / ratio, 2.0 * slope / 6.0))
                    : 1.0 / std::sqrt (1.0 + std::pow (analogueRatio, 2.0 * slope / 6.0));
                const double tolerance = lowCut ? 0.002 : 0.03;
                require (std::abs (band.getMagnitudeForFrequency (frequency) - expected) < tolerance,
                         "Cut response must match its advertised Butterworth slope");
                const double model = ResponseModel::designedMagnitude (designed, frequency, sampleRate);
                maxModelError = std::max (maxModelError, std::abs (model - (double) band.getMagnitudeForFrequency (frequency)));
                require (std::abs (model - (double) band.getMagnitudeForFrequency (frequency)) < 0.002,
                         "Response model must match the installed filter");
                double real = 0.0, imaginary = 0.0;
                const double omega = juce::MathConstants<double>::twoPi * frequency / sampleRate;
                for (size_t n = 0; n < impulse.size(); ++n)
                {
                    real += impulse[n] * std::cos (omega * (double) n);
                    imaginary -= impulse[n] * std::sin (omega * (double) n);
                }
                require (std::abs (std::hypot (real, imaginary) - band.getMagnitudeForFrequency (frequency)) < 0.002,
                         "Graph magnitude must match the processed impulse");
            }
        }

    std::cout << "  model_vs_filter_max_abs " << maxModelError << "\n";
    std::cout << "PASS: all cut slopes, cutoff gain, and graph/audio agreement\n";
}

static void checkLayout (juce::Component& component)
{
    for (auto* child : component.getChildren())
    {
        require (component.getLocalBounds().contains (child->getBounds()), "Control extends outside its panel");
        checkLayout (*child);
    }
}

static void checkBandControls()
{
    OpenEQAudioProcessor processor;
    BandControlPanel panel (processor);
    for (int width : { 800, 1130, 1630 })
    {
        panel.setBounds (0, 0, width, 184);
        checkLayout (panel);
    }
    panel.setSelectedBand (1);
    juce::Button* active = nullptr;
    juce::Button* dynamic = nullptr;
    std::function<void (juce::Component&)> find = [&] (juce::Component& component)
    {
        for (auto* child : component.getChildren())
        {
            if (auto* button = dynamic_cast<juce::Button*> (child))
            {
                if (button->getButtonText() == "On") active = button;
                if (button->getButtonText() == "D") dynamic = button;
            }
            find (*child);
        }
    };
    find (panel);
    require (active != nullptr && dynamic != nullptr, "Selected-band toggles missing");
    active->setToggleState (false, juce::sendNotificationSync);
    require (processor.apvts.getRawParameterValue (Params::bandActiveID (0))->load() > 0.5f,
             "Changing band 2 must not change band 1");
    require (processor.apvts.getRawParameterValue (Params::bandActiveID (1))->load() < 0.5f,
             "Toggle must update selected band");
    active->setToggleState (true, juce::sendNotificationSync);
    dynamic->setToggleState (true, juce::sendNotificationSync);
    require (processor.apvts.getRawParameterValue (Params::bandActiveID (1))->load() > 0.5f,
             "Selected band must re-enable");
    require (processor.apvts.getRawParameterValue (Params::bandDynOnID (1))->load() > 0.5f,
             "Dynamic toggle must update selected band");
    require (processor.apvts.getRawParameterValue (Params::bandDynOnID (0))->load() < 0.5f,
             "Dynamic toggle must not enable dynamics on band 1");
    std::cout << "PASS: panel bounds and selected-band active/dynamic toggle isolation\n";
}

static double renderChannelRatio (OpenEQAudioProcessor& processor, Tone tone, int channel)
{
    constexpr int total = 4800;
    constexpr int skip = 1440;
    juce::AudioBuffer<float> buffer (2, total);
    fillTone (buffer, 48000.0, 1000.0, 0.1f, 0, tone);
    const auto dry = copyBuffer (buffer);
    process (processor, buffer);
    require (allFinite (buffer), "Routing render produced a non-finite sample");
    const double in = rmsRange (dry.getReadPointer (channel), skip, total - skip);
    const double out = rmsRange (buffer.getReadPointer (channel), skip, total - skip);
    return out / in;
}

static void checkRouting()
{
    const float plus12 = bellMagnitude (12.0f);
    const float plus18 = bellMagnitude (18.0f);
    std::cout << "  bell_12_db_magnitude " << plus12 << "\n";
    std::cout << "  bell_18_db_magnitude " << plus18 << "\n";

    {
        OpenEQAudioProcessor processor;
        silenceBands (processor);
        configureBand (processor, 0, FilterType::Bell, 1000.0f, 12.0f, 1.0f, 1, (int) StereoMode::Stereo, true);
        prepareStereo (processor, 48000.0, 512);
        const double left = renderChannelRatio (processor, Tone::Centered, 0);
        const double right = renderChannelRatio (processor, Tone::Centered, 1);
        std::cout << "  stereo_bell_left_db " << dbRatio (left / plus12) << " ratio " << left << "\n";
        std::cout << "  stereo_bell_right_db " << dbRatio (right / plus12) << " ratio " << right << "\n";
        require (std::abs (dbRatio (left / plus12)) < 0.4, "Stereo bell left channel missed +12 dB");
        require (std::abs (dbRatio (right / plus12)) < 0.4, "Stereo bell right channel missed +12 dB");
    }

    {
        OpenEQAudioProcessor processor;
        silenceBands (processor);
        configureBand (processor, 0, FilterType::Bell, 1000.0f, 12.0f, 1.0f, 1, (int) StereoMode::Left, true);
        prepareStereo (processor, 48000.0, 512);
        juce::AudioBuffer<float> buffer (2, 4800);
        fillTone (buffer, 48000.0, 1000.0, 0.1f, 0, Tone::Centered);
        const auto dry = copyBuffer (buffer);
        process (processor, buffer);
        const double left = rmsRange (buffer.getReadPointer (0), 1440, 3360) / rmsRange (dry.getReadPointer (0), 1440, 3360);
        const float rightError = maxAbsDiff (buffer, dry, 1, 0, buffer.getNumSamples());
        std::cout << "  left_mode_left_db " << dbRatio (left / plus12) << " right_max_abs " << rightError << "\n";
        require (std::abs (dbRatio (left / plus12)) < 0.4, "Left placement missed the left channel");
        require (rightError == 0.0f, "Left placement changed the right channel");
    }

    {
        OpenEQAudioProcessor processor;
        silenceBands (processor);
        configureBand (processor, 0, FilterType::Bell, 1000.0f, 12.0f, 1.0f, 1, (int) StereoMode::Right, true);
        prepareStereo (processor, 48000.0, 512);
        juce::AudioBuffer<float> buffer (2, 4800);
        fillTone (buffer, 48000.0, 1000.0, 0.1f, 0, Tone::Centered);
        const auto dry = copyBuffer (buffer);
        process (processor, buffer);
        const double right = rmsRange (buffer.getReadPointer (1), 1440, 3360) / rmsRange (dry.getReadPointer (1), 1440, 3360);
        const float leftError = maxAbsDiff (buffer, dry, 0, 0, buffer.getNumSamples());
        std::cout << "  right_mode_right_db " << dbRatio (right / plus12) << " left_max_abs " << leftError << "\n";
        require (std::abs (dbRatio (right / plus12)) < 0.4, "Right placement missed the right channel");
        require (leftError == 0.0f, "Right placement changed the left channel");
    }

    {
        OpenEQAudioProcessor processor;
        silenceBands (processor);
        configureBand (processor, 0, FilterType::Bell, 1000.0f, 12.0f, 1.0f, 1, (int) StereoMode::Mid, true);
        prepareStereo (processor, 48000.0, 512);
        const double center = renderChannelRatio (processor, Tone::Centered, 0);
        std::cout << "  mid_center_db " << dbRatio (center / plus12) << "\n";
        require (std::abs (dbRatio (center / plus12)) < 0.4, "Mid placement missed a centered signal");
    }

    {
        OpenEQAudioProcessor processor;
        silenceBands (processor);
        configureBand (processor, 0, FilterType::Bell, 1000.0f, 12.0f, 1.0f, 1, (int) StereoMode::Mid, true);
        prepareStereo (processor, 48000.0, 512);
        juce::AudioBuffer<float> side (2, 4800);
        fillTone (side, 48000.0, 1000.0, 0.1f, 0, Tone::Side);
        const auto drySide = copyBuffer (side);
        process (processor, side);
        const float sideError = std::max (maxAbsDiff (side, drySide, 0, 0, side.getNumSamples()),
                                           maxAbsDiff (side, drySide, 1, 0, side.getNumSamples()));
        std::cout << "  mid_side_max_abs " << sideError << "\n";
        require (sideError < 1.0e-5f, "Mid placement changed a pure side signal");
    }

    {
        OpenEQAudioProcessor processor;
        silenceBands (processor);
        configureBand (processor, 0, FilterType::Bell, 1000.0f, 12.0f, 1.0f, 1, (int) StereoMode::Side, true);
        prepareStereo (processor, 48000.0, 512);
        const double side = renderChannelRatio (processor, Tone::Side, 0);
        std::cout << "  side_signal_db " << dbRatio (side / plus12) << "\n";
        require (std::abs (dbRatio (side / plus12)) < 0.4, "Side placement missed a side signal");
    }

    {
        OpenEQAudioProcessor processor;
        silenceBands (processor);
        configureBand (processor, 0, FilterType::Bell, 1000.0f, 12.0f, 1.0f, 1, (int) StereoMode::Side, true);
        prepareStereo (processor, 48000.0, 512);
        juce::AudioBuffer<float> center (2, 2400);
        fillTone (center, 48000.0, 1000.0, 0.1f, 0, Tone::Centered);
        const auto dryCenter = copyBuffer (center);
        process (processor, center);
        const float centerError = std::max (maxAbsDiff (center, dryCenter, 0, 0, center.getNumSamples()),
                                             maxAbsDiff (center, dryCenter, 1, 0, center.getNumSamples()));
        std::cout << "  side_center_max_abs " << centerError << "\n";
        require (centerError < 1.0e-5f, "Side placement changed a centered signal");
    }

    {
        OpenEQAudioProcessor processor;
        silenceBands (processor);
        configureBand (processor, 0, FilterType::Bell, 1000.0f, 0.0f, 1.0f, 1, (int) StereoMode::Stereo, true);
        configureBand (processor, 1, FilterType::Bell, 1000.0f, 18.0f, 1.0f, 1, (int) StereoMode::Stereo, true);
        setRaw (processor.apvts, Params::bandSoloID (0), 1.0f);
        prepareStereo (processor, 48000.0, 512);
        const double ratio = renderChannelRatio (processor, Tone::Centered, 0);
        std::cout << "  solo_excludes_other_ratio " << ratio << " db " << dbRatio (ratio) << "\n";
        require (ratio < 1.25, "Solo still let the unsoloed +18 dB band through");
        require (ratio > 0.8, "Solo removed the soloed band as well");
    }

    {
        OpenEQAudioProcessor processor;
        silenceBands (processor);
        configureBand (processor, 0, FilterType::Bell, 1000.0f, 18.0f, 1.0f, 1, (int) StereoMode::Stereo, false);
        prepareStereo (processor, 48000.0, 512);
        juce::AudioBuffer<float> buffer (2, 2048);
        fillTone (buffer, 48000.0, 1000.0, 0.2f, 0, Tone::Centered);
        buffer.setSample (0, 3, 0.8f);
        buffer.setSample (1, 9, -0.65f);
        const auto dry = copyBuffer (buffer);
        process (processor, buffer);
        const float error = std::max (maxAbsDiff (buffer, dry, 0, 0, buffer.getNumSamples()),
                                       maxAbsDiff (buffer, dry, 1, 0, buffer.getNumSamples()));
        std::cout << "  inactive_max_abs " << error << "\n";
        require (error == 0.0f, "Inactive bands changed the audio");
    }

    {
        OpenEQAudioProcessor processor;
        silenceBands (processor);
        configureBand (processor, 0, FilterType::Bell, 1000.0f, 0.0f, 1.0f, 1, (int) StereoMode::Stereo, true);
        prepareStereo (processor, 48000.0, 512);
        juce::AudioBuffer<float> buffer (2, 512);
        buffer.clear();
        buffer.setSample (0, 0, 1.0f);
        buffer.setSample (1, 0, 1.0f);
        const auto dry = copyBuffer (buffer);
        process (processor, buffer);
        const float error = std::max (maxAbsDiff (buffer, dry, 0, 0, buffer.getNumSamples()),
                                       maxAbsDiff (buffer, dry, 1, 0, buffer.getNumSamples()));
        std::cout << "  zero_db_bell_max_abs " << error << "\n";
        require (error < 2.0e-4f, "A 0 dB bell diverged from unity");
    }

    {
        OpenEQAudioProcessor processor;
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add (juce::AudioChannelSet::mono());
        layout.inputBuses.add (juce::AudioChannelSet::disabled());
        layout.outputBuses.add (juce::AudioChannelSet::mono());
        const bool accepted = processor.setBusesLayout (layout);
        std::cout << "  mono_layout " << (accepted ? "accepted" : "rejected") << "\n";
        require (accepted, "Processor rejected a mono layout it claims to support");
        silenceBands (processor);
        configureBand (processor, 0, FilterType::Bell, 1000.0f, 12.0f, 1.0f, 1, (int) StereoMode::Right, true);
        prepareStereo (processor, 48000.0, 512);
        juce::AudioBuffer<float> buffer (1, 4800);
        fillTone (buffer, 48000.0, 1000.0, 0.1f, 0, Tone::Centered);
        const auto dry = copyBuffer (buffer);
        process (processor, buffer);
        require (allFinite (buffer), "Mono render produced a non-finite sample");
        const double ratio = rmsRange (buffer.getReadPointer (0), 1440, 3360) / rmsRange (dry.getReadPointer (0), 1440, 3360);
        std::cout << "  mono_right_placed_db " << dbRatio (ratio / plus12) << " ratio " << ratio << "\n";
        require (std::abs (dbRatio (ratio / plus12)) < 0.4, "Mono input skipped a right-placed band");
    }

    std::cout << "PASS: routing, solo exclusion, inactive path, and mono\n";
}

static juce::AudioBuffer<float> renderChunked (OpenEQAudioProcessor& processor, const juce::AudioBuffer<float>& input, int chunkSize)
{
    juce::AudioBuffer<float> rendered (input.getNumChannels(), input.getNumSamples());
    int offset = 0;
    while (offset < input.getNumSamples())
    {
        const int count = std::min (chunkSize, input.getNumSamples() - offset);
        juce::AudioBuffer<float> chunk (input.getNumChannels(), count);
        for (int channel = 0; channel < input.getNumChannels(); ++channel)
            chunk.copyFrom (channel, 0, input, channel, offset, count);
        process (processor, chunk);
        for (int channel = 0; channel < input.getNumChannels(); ++channel)
            rendered.copyFrom (channel, offset, chunk, channel, 0, count);
        offset += count;
    }
    return rendered;
}

static void checkBlockHandling()
{
    auto configure = [] (OpenEQAudioProcessor& processor)
    {
        silenceBands (processor);
        configureBand (processor, 0, FilterType::Bell, 1000.0f, 6.0f, 1.2f, 1, (int) StereoMode::Stereo, true);
    };

    juce::AudioBuffer<float> input (2, 4000);
    fillTone (input, 48000.0, 1000.0, 0.15f, 0, Tone::Centered);
    input.setSample (0, 10, 0.55f);
    input.setSample (1, 400, -0.4f);

    OpenEQAudioProcessor whole;
    OpenEQAudioProcessor chunked;
    configure (whole);
    configure (chunked);
    prepareStereo (whole, 48000.0, 512);
    prepareStereo (chunked, 48000.0, 512);

    auto wholeBuffer = copyBuffer (input);
    process (whole, wholeBuffer);
    const auto chunkBuffer = renderChunked (chunked, input, 64);
    const float splitError = std::max (maxAbsDiff (wholeBuffer, chunkBuffer, 0, 0, input.getNumSamples()),
                                        maxAbsDiff (wholeBuffer, chunkBuffer, 1, 0, input.getNumSamples()));
    std::cout << "  block_4000_vs_64_max_abs " << splitError << "\n";
    require (allFinite (wholeBuffer) && allFinite (chunkBuffer), "Block split produced a non-finite sample");
    require (splitError < 1.0e-5f, "A 4000-sample block disagreed with 64-sample blocks");

    juce::AudioBuffer<float> longInput (2, 5000);
    fillTone (longInput, 48000.0, 440.0, 0.12f, 20, Tone::Centered);
    OpenEQAudioProcessor preparedSmall;
    OpenEQAudioProcessor preparedLarge;
    configure (preparedSmall);
    configure (preparedLarge);
    prepareStereo (preparedSmall, 48000.0, 128);
    prepareStereo (preparedLarge, 48000.0, 512);
    auto longBuffer = copyBuffer (longInput);
    process (preparedSmall, longBuffer);
    const auto longChunked = renderChunked (preparedLarge, longInput, 64);
    const float longError = std::max (maxAbsDiff (longBuffer, longChunked, 0, 0, longInput.getNumSamples()),
                                       maxAbsDiff (longBuffer, longChunked, 1, 0, longInput.getNumSamples()));
    std::cout << "  block_5000_prepare128_vs_64_max_abs " << longError << "\n";
    require (allFinite (longBuffer), "Oversized host block produced a non-finite sample");
    require (longError < 1.0e-5f, "Chunked oversized block disagreed with small blocks");

    for (int size : { 1, 7, 32, 511, 512, 513 })
    {
        OpenEQAudioProcessor processor;
        configure (processor);
        prepareStereo (processor, 48000.0, 128);
        juce::AudioBuffer<float> buffer (2, size);
        fillTone (buffer, 48000.0, 1000.0, 0.2f, 0, Tone::Centered);
        process (processor, buffer);
        requireMsg (allFinite (buffer), "Non-finite output at block size " + std::to_string (size));
    }

    {
        OpenEQAudioProcessor processor;
        silenceBands (processor);
        configureBand (processor, 0, FilterType::Bell, 1000.0f, 0.0f, 1.0f, 1, (int) StereoMode::Stereo, true);
        prepareStereo (processor, 48000.0, 512);
        juce::AudioBuffer<float> empty (2, 0);
        process (processor, empty);
        setRaw (processor.apvts, Params::bandFreqID (0), 2345.0f);
        processor.serviceIdleGraph();
        const auto snapshot = processor.getGraphSnapshot();
        std::cout << "  zero_block_idle_frequency " << snapshot.bands[0].appliedFrequency << "\n";
        require (std::abs (snapshot.bands[0].targetFrequency - 2345.0f) < 0.1f,
                 "A zero-length block prevented the idle graph from tracking parameters");
        require (std::abs (snapshot.bands[0].appliedFrequency - 2345.0f) < 0.1f,
                 "A zero-length block left the idle design at the old frequency");
    }

    std::cout << "PASS: block splitting, oversized blocks, and zero-length blocks\n";
}

static void checkSmoothing()
{
    OpenEQAudioProcessor processor;
    silenceBands (processor);
    configureBand (processor, 0, FilterType::Bell, 1000.0f, 0.0f, 1.0f, 1, (int) StereoMode::Stereo, true);
    prepareStereo (processor, 48000.0, 512);

    juce::AudioBuffer<float> silence (2, 256);
    silence.clear();
    process (processor, silence);

    setRaw (processor.apvts, Params::bandGainID (0), 12.0f);
    juce::AudioBuffer<float> first (2, 64);
    fillTone (first, 48000.0, 1000.0, 0.1f, 0, Tone::Centered);
    process (processor, first);
    const float earlyGain = processor.getGraphSnapshot().bands[0].appliedGainDb;
    std::cout << "  gain_after_64_samples_db " << earlyGain << "\n";
    require (allFinite (first), "Gain ramp produced a non-finite sample");
    require (earlyGain > 0.4f && earlyGain < 1.0f, "64-sample gain ramp was not near 0.64 dB");

    juce::AudioBuffer<float> rest (2, 4992);
    fillTone (rest, 48000.0, 1000.0, 0.1f, 64, Tone::Centered);
    process (processor, rest);
    const float settledGain = processor.getGraphSnapshot().bands[0].appliedGainDb;
    const auto settledUpdates = processor.getDesignUpdateCount();
    std::cout << "  gain_after_100_ms_db " << settledGain << "\n";
    require (allFinite (rest), "Settling ramp produced a non-finite sample");
    require (std::abs (settledGain - 12.0f) < 0.02f, "Gain ramp did not reach +12 dB");

    for (int i = 0; i < 8; ++i)
    {
        juce::AudioBuffer<float> steady (2, 256);
        fillTone (steady, 48000.0, 1000.0, 0.1f, 5056 + i * 256, Tone::Centered);
        process (processor, steady);
        require (allFinite (steady), "Settled gain path produced a non-finite sample");
    }
    const auto steadyDelta = processor.getDesignUpdateCount() - settledUpdates;
    std::cout << "  settled_gain_design_delta " << steadyDelta << "\n";
    require (steadyDelta == 0, "Settled gain kept rebuilding coefficients");

    OpenEQAudioProcessor bypassed;
    silenceBands (bypassed);
    configureBand (bypassed, 0, FilterType::Bell, 1000.0f, 12.0f, 1.0f, 1, (int) StereoMode::Stereo, true);
    prepareStereo (bypassed, 48000.0, 512);
    juce::AudioBuffer<float> warmup (2, 2048);
    fillTone (warmup, 48000.0, 1000.0, 0.1f, 0, Tone::Centered);
    process (bypassed, warmup);

    setRaw (bypassed.apvts, Params::bandActiveID (0), 0.0f);
    juce::AudioBuffer<float> early (2, 64);
    fillTone (early, 48000.0, 1000.0, 0.1f, 2048, Tone::Centered);
    const auto earlyDry = copyBuffer (early);
    process (bypassed, early);
    const double earlyRatio = rmsRange (early.getReadPointer (0), 0, 64) / rmsRange (earlyDry.getReadPointer (0), 0, 64);
    std::cout << "  band_bypass_after_64_ratio " << earlyRatio << "\n";
    require (earlyRatio > 2.5, "Band bypass reached dry audio inside 64 samples");

    juce::AudioBuffer<float> tail (2, 4096);
    fillTone (tail, 48000.0, 1000.0, 0.1f, 2112, Tone::Centered);
    const auto tailDry = copyBuffer (tail);
    process (bypassed, tail);
    const float tailError = std::max (maxAbsDiff (tail, tailDry, 0, 2048, 2048),
                                       maxAbsDiff (tail, tailDry, 1, 2048, 2048));
    std::cout << "  band_bypass_tail_max_abs " << tailError << "\n";
    require (allFinite (tail), "Band bypass fade produced a non-finite sample");
    require (tailError < 1.0e-5f, "Band bypass tail did not return to the dry signal");

    OpenEQAudioProcessor globalBypass;
    silenceBands (globalBypass);
    configureBand (globalBypass, 0, FilterType::Bell, 1000.0f, 12.0f, 1.0f, 1, (int) StereoMode::Stereo, true);
    setRaw (globalBypass.apvts, "bypass", 1.0f);
    prepareStereo (globalBypass, 48000.0, 512);
    juce::AudioBuffer<float> dryBlock (2, 512);
    fillTone (dryBlock, 48000.0, 1000.0, 0.15f, 0, Tone::Centered);
    const auto dryCopy = copyBuffer (dryBlock);
    process (globalBypass, dryBlock);
    const float bypassError = std::max (maxAbsDiff (dryBlock, dryCopy, 0, 0, dryBlock.getNumSamples()),
                                         maxAbsDiff (dryBlock, dryCopy, 1, 0, dryBlock.getNumSamples()));
    setRaw (globalBypass.apvts, Params::bandFreqID (0), 880.0f);
    juce::AudioBuffer<float> edited (2, 256);
    fillTone (edited, 48000.0, 1000.0, 0.15f, 512, Tone::Centered);
    const auto editedDry = copyBuffer (edited);
    process (globalBypass, edited);
    const float editedError = std::max (maxAbsDiff (edited, editedDry, 0, 0, edited.getNumSamples()),
                                         maxAbsDiff (edited, editedDry, 1, 0, edited.getNumSamples()));
    const float editedFrequency = globalBypass.getGraphSnapshot().bands[0].appliedFrequency;
    std::cout << "  settled_bypass_max_abs " << bypassError
              << " edited_max_abs " << editedError
              << " edited_frequency " << editedFrequency << "\n";
    require (bypassError == 0.0f, "Settled global bypass changed the audio");
    require (editedError == 0.0f, "An edit during global bypass changed the audio");
    require (std::abs (editedFrequency - 880.0f) < 0.1f, "Global bypass did not install the edited frequency");

    std::cout << "PASS: gain smoothing, band bypass, and settled global bypass\n";
}

static double analyticalCut (bool lowCut, int slopeDb, double frequency, double sampleRate)
{
    const double ratio = std::tan (juce::MathConstants<double>::pi * frequency / sampleRate)
                       / std::tan (juce::MathConstants<double>::pi * 1000.0 / sampleRate);
    return 1.0 / std::sqrt (1.0 + std::pow (lowCut ? 1.0 / ratio : ratio, 2.0 * slopeDb / 6.0));
}

static void checkSnapshotsAndResponse()
{
    {
        OpenEQAudioProcessor processor;
        setRaw (processor.apvts, Params::bandFreqID (0), 3333.0f);
        processor.serviceIdleGraph();
        const auto idle = processor.getGraphSnapshot();
        std::cout << "  idle_before_audio_frequency " << idle.bands[0].appliedFrequency << "\n";
        require (std::abs (idle.bands[0].targetFrequency - 3333.0f) < 0.1f,
                 "Idle graph ignored a parameter edit before audio started");
    }

    {
        OpenEQAudioProcessor processor;
        silenceBands (processor);
        configureBand (processor, 0, FilterType::Bell, 1000.0f, 3.0f, 1.0f, 1, (int) StereoMode::Stereo, true);
        prepareStereo (processor, 48000.0, 512);
        juce::AudioBuffer<float> buffer (2, 128);
        fillTone (buffer, 48000.0, 1000.0, 0.1f, 0, Tone::Centered);
        process (processor, buffer);
        setRaw (processor.apvts, Params::bandFreqID (0), 4321.0f);
        const auto held = processor.getGraphSnapshot();
        processor.serviceIdleGraph();
        const auto after = processor.getGraphSnapshot();
        std::cout << "  live_snapshot_frequency " << held.bands[0].appliedFrequency
                  << " after_idle " << after.bands[0].appliedFrequency << "\n";
        require (std::abs (held.bands[0].targetFrequency - 1000.0f) < 0.1f,
                 "Graph snapshot picked up a parameter the audio thread has not processed");
        require (std::abs (held.bands[0].appliedFrequency - 1000.0f) < 0.1f,
                 "Applied snapshot frequency changed without an audio block");
        require (std::abs (after.bands[0].appliedFrequency - held.bands[0].appliedFrequency) < 1.0e-4f,
                 "Idle service overwrote a fresh audio snapshot");
    }

    {
        OpenEQAudioProcessor processor;
        silenceBands (processor);
        configureBand (processor, 0, FilterType::LowCut, 1000.0f, 0.0f, 1.0f, 2, (int) StereoMode::Stereo, true);
        prepareStereo (processor, 48000.0, 512);
        constexpr int length = 32768;
        juce::AudioBuffer<float> impulse (2, length);
        impulse.clear();
        impulse.setSample (0, 0, 1.0f);
        impulse.setSample (1, 0, 1.0f);
        process (processor, impulse);
        const auto snapshot = processor.getGraphSnapshot();
        double maxCurve = 0.0;
        double maxAudio = 0.0;
        double maxAnalytical = 0.0;
        for (double frequency : { 700.0, 1000.0, 1500.0 })
        {
            const double analytical = analyticalCut (true, 24, frequency, 48000.0);
            const double curve = ResponseModel::totalMagnitude (snapshot, frequency);
            const double audio = impulseMagnitude (impulse.getReadPointer (0), length, frequency, 48000.0);
            const auto designed = ResponseModel::designBand (FilterType::LowCut, 1000.0f, 0.0f, 1.0f,
                                                             ResponseModel::encodeSlopeNorm (Params::slopeNormForCutDb (24.0f)), 48000.0);
            const double model = ResponseModel::designedMagnitude (designed, frequency, 48000.0);
            maxCurve = std::max (maxCurve, std::abs (curve - model));
            maxAudio = std::max (maxAudio, std::abs (audio - curve));
            maxAnalytical = std::max (maxAnalytical, std::abs (audio - analytical));
        }
        std::cout << "  snapshot_vs_model_max_abs " << maxCurve
                  << " audio_vs_snapshot_max_abs " << maxAudio
                  << " audio_vs_analytical_max_abs " << maxAnalytical << "\n";
        require (maxCurve < 0.002, "Published curve disagreed with the response model");
        require (maxAudio < 0.01, "Processed impulse disagreed with the published curve");
        require (maxAnalytical < 0.01, "Processed low cut disagreed with the Butterworth curve");
    }

    std::cout << "PASS: snapshot isolation and processed-response agreement\n";
}

static void checkDynamicsShortBlock()
{
    OpenEQAudioProcessor processor;
    silenceBands (processor);
    configureBand (processor, 0, FilterType::Bell, 1000.0f, 0.0f, 1.0f, 1, (int) StereoMode::Stereo, true);
    setRaw (processor.apvts, Params::bandDynOnID (0), 1.0f);
    setRaw (processor.apvts, Params::bandDynThreshID (0), -24.0f);
    setRaw (processor.apvts, Params::bandDynRangeID (0), 12.0f);
    setRaw (processor.apvts, Params::bandDynAttackID (0), 0.1f);
    setRaw (processor.apvts, Params::bandDynReleaseID (0), 8.0f);
    prepareStereo (processor, 48000.0, 512);

    juce::AudioBuffer<float> loud (2, 512);
    fillTone (loud, 48000.0, 1000.0, 0.5f, 0, Tone::Centered);
    process (processor, loud);
    const float hot = processor.getDynamicEnvelopeDb (0);
    std::cout << "  detector_hot_db " << hot << "\n";
    require (allFinite (loud), "Dynamic loud block produced a non-finite sample");
    require (hot > -30.0f, "1 kHz sine did not drive the detector");

    for (int i = 0; i < 50; ++i)
    {
        juce::AudioBuffer<float> gap (2, 16);
        gap.clear();
        process (processor, gap);
        require (allFinite (gap), "Short dynamic block produced a non-finite sample");
    }

    const float cold = processor.getDynamicEnvelopeDb (0);
    std::cout << "  detector_after_50x16_silence_db " << cold << "\n";
    require (cold < hot - 12.0f, "Short blocks kept the detector hot from stale scratch");

    std::cout << "PASS: short-block detector reads only the current chunk\n";
}

static void checkNyquistAndTypes()
{
    OpenEQAudioProcessor processor;
    silenceBands (processor);
    configureBand (processor, 0, FilterType::Bell, 20000.0f, 18.0f, 40.0f, 1, (int) StereoMode::Stereo, true);
    prepareStereo (processor, 22050.0, 256);
    juce::AudioBuffer<float> buffer (2, 1024);
    fillTone (buffer, 22050.0, 1000.0, 0.2f, 0, Tone::Centered);
    process (processor, buffer);
    const float applied = processor.getGraphSnapshot().bands[0].appliedFrequency;
    std::cout << "  nyquist_22050_applied_frequency " << applied << "\n";
    require (allFinite (buffer), "Near-Nyquist bell produced a non-finite sample");
    require (applied < 22050.0 * 0.49 + 0.05, "Design frequency was not kept below Nyquist");
    require (applied < 15000.0f, "20 kHz at 22050 Hz was not clamped");

    setRaw (processor.apvts, Params::bandTypeID (0), (float) FilterType::HighCut);
    setRaw (processor.apvts, Params::bandSlopeID (0), 6.0f);
    juce::AudioBuffer<float> cut (2, 1024);
    fillTone (cut, 22050.0, 800.0, 0.2f, 0, Tone::Centered);
    process (processor, cut);
    require (allFinite (cut), "Near-Nyquist high cut produced a non-finite sample");

    for (double sampleRate : { 44100.0, 96000.0 })
    {
        for (int type = 0; type < filterTypeCount; ++type)
        {
            OpenEQAudioProcessor smoke;
            silenceBands (smoke);
            configureBand (smoke, 0, (FilterType) type, 1000.0f, 6.0f, 2.0f, 2, (int) StereoMode::Stereo, true);
            prepareStereo (smoke, sampleRate, 256);
            juce::AudioBuffer<float> block (2, 256);
            fillTone (block, sampleRate, 1000.0, 0.2f, 0, Tone::Centered);
            process (smoke, block);
            requireMsg (allFinite (block), "Non-finite output for type " + std::to_string (type)
                                            + " at " + std::to_string ((int) sampleRate));
        }
    }

    std::cout << "PASS: Nyquist clamp and finite output across types\n";
}

static bool stateHasParameter (const juce::ValueTree& state, const char* id)
{
    for (int i = 0; i < state.getNumChildren(); ++i)
        if (state.getChild (i).getProperty ("id").toString() == id)
            return true;
    return false;
}

static void checkStateRoundTrip()
{
    OpenEQAudioProcessor original;
    configureBand (original, 7, FilterType::HighCut, 3456.0f, -6.5f, 2.5f, 4, (int) StereoMode::Right, true);
    require (original.apvts.getParameter ("b0_freq") != nullptr, "b0_freq is missing");
    require (original.apvts.getParameter ("b7_freq") != nullptr, "b7_freq is missing");
    require (stateHasParameter (original.apvts.state, "b0_freq"), "State tree is missing b0_freq");
    require (stateHasParameter (original.apvts.state, "b7_freq"), "State tree is missing b7_freq");

    juce::MemoryBlock state;
    original.getStateInformation (state);
    require (state.getSize() > 0, "State block was empty");

    OpenEQAudioProcessor restored;
    restored.setStateInformation (state.getData(), (int) state.getSize());

    const float frequencyError = std::abs (rawValue (restored, Params::bandFreqID (7)) - rawValue (original, Params::bandFreqID (7)));
    const float qError = std::abs (rawValue (restored, Params::bandQID (7)) - rawValue (original, Params::bandQID (7)));
    const float gainError = std::abs (rawValue (restored, Params::bandGainID (7)) - rawValue (original, Params::bandGainID (7)));
    std::cout << "  state_freq " << rawValue (restored, Params::bandFreqID (7))
              << " gain " << rawValue (restored, Params::bandGainID (7))
              << " q " << rawValue (restored, Params::bandQID (7))
              << " type " << rawValue (restored, Params::bandTypeID (7))
              << " stereo " << rawValue (restored, Params::bandStereoID (7))
              << " slope " << rawValue (restored, Params::bandSlopeID (7))
              << " active " << rawValue (restored, Params::bandActiveID (7)) << "\n";
    std::cout << "  state_roundtrip_freq_abs " << frequencyError << " q_abs " << qError << " gain_abs " << gainError << "\n";

    require (std::abs (rawValue (original, Params::bandFreqID (7)) - 3456.0f) < 0.1f, "Frequency did not store 3456 Hz");
    require (frequencyError < 1.0e-3f, "Frequency did not round-trip");
    require (qError < 1.0e-4f, "Q did not round-trip");
    require (gainError < 1.0e-3f, "Gain did not round-trip");
    require (rawValue (restored, Params::bandTypeID (7)) == (float) FilterType::HighCut, "High Cut type did not round-trip");
    require (rawValue (restored, Params::bandStereoID (7)) == (float) StereoMode::Right, "Right placement did not round-trip");
    require (rawValue (restored, Params::bandSlopeID (7)) == 4.0f, "48 dB/oct slope did not round-trip");
    require (rawValue (restored, Params::bandActiveID (7)) > 0.5f, "Band 7 active state did not round-trip");
    require (restored.apvts.getParameter ("b7_dynAtk") != nullptr, "b7_dynAtk is missing");
    require (restored.apvts.getParameter ("processingMode") != nullptr, "processingMode is missing");

    std::cout << "PASS: parameter IDs and state round-trip\n";
}

static void checkAnalyzerRing()
{
    AnalyzerRing ring;
    std::vector<float> samples (5000);
    for (int i = 0; i < (int) samples.size(); ++i)
        samples[(size_t) i] = (float) i;
    ring.pushSamples (samples.data(), (int) samples.size());

    std::array<float, AnalyzerRing::size> latest {};
    ring.copyLatest (latest);
    std::cout << "  analyzer_oldest " << latest[0] << " newest " << latest[AnalyzerRing::size - 1] << "\n";
    require (latest[0] == 904.0f, "Analyzer window did not start at the oldest sample");
    require (latest[AnalyzerRing::size - 1] == 4999.0f, "Analyzer window did not end at the newest sample");
    std::cout << "PASS: analyzer ring keeps oldest-at-front order\n";
}

static void checkCurveRequestIsolation()
{
    LinearPhaseEngine::CurveRequest request;
    request.sampleRate = 48000.0;
    request.fftOrder = 12;
    request.count = 1;
    request.bands[0].type = FilterType::Bell;
    request.bands[0].frequency = 1000.0f;
    request.bands[0].gainDb = 12.0f;
    request.bands[0].q = 1.0f;
    request.bands[0].slopeDb = 12;
    request.bands[0].audible = true;

    const auto copied = request;
    request.bands[0].gainDb = -18.0f;
    request.bands[0].frequency = 200.0f;
    request.count = 0;

    const auto designed = ResponseModel::designBand (copied.bands[0].type, copied.bands[0].frequency,
                                                      copied.bands[0].gainDb, copied.bands[0].q,
                                                      copied.bands[0].slopeDb, copied.sampleRate);
    std::cout << "  curve_request_copy_gain_db " << designed.gainDb
              << " frequency " << designed.frequency << "\n";
    require (designed.gainDb == 12.0f, "Curve request copy followed a later parameter edit");
    require (std::abs (designed.frequency - 1000.0f) < 0.1f, "Curve request copy followed a later frequency edit");
    std::cout << "PASS: linear-phase request values are independent of later edits\n";
    std::cout << "  linear_phase_request_isolated (impulse check is in roadmap)\n";
}

static void checkGraphPaint()
{
    OpenEQAudioProcessor processor;
    EQCurveComponent curve (processor);
    curve.setBounds (0, 0, 900, 420);
    juce::Image image (juce::Image::ARGB, 900, 420, true);
    {
        juce::Graphics graphics (image);
        curve.paint (graphics);
    }

    int ink = 0;
    for (int y = 0; y < image.getHeight(); y += 4)
        for (int x = 0; x < image.getWidth(); x += 4)
            if (image.getPixelAt (x, y).getAlpha() > 0)
                ++ink;

    std::cout << "  graph_ink_samples " << ink << "\n";
    require (ink > 20, "Graph paint did not cover the image");

    const auto requireInside = [] (juce::Component& parent, auto&& self) -> void
    {
        if (parent.getWidth() <= 0 || parent.getHeight() <= 0)
            return;
        for (int i = 0; i < parent.getNumChildComponents(); ++i)
        {
            auto* child = parent.getChildComponent (i);
            if (! child->isVisible())
                continue;
            const auto bounds = child->getBounds();
            const bool inside = bounds.getX() >= -2 && bounds.getY() >= -2
                             && bounds.getRight() <= parent.getWidth() + 2
                             && bounds.getBottom() <= parent.getHeight() + 2;
            if (! inside)
                std::cout << "  outside " << typeid (*child).name()
                          << " bounds " << bounds.getX() << "," << bounds.getY()
                          << " " << bounds.getWidth() << "x" << bounds.getHeight()
                          << " parent " << parent.getWidth() << "x" << parent.getHeight()
                          << " " << typeid (parent).name() << "\n";
            require (inside, "Editor child drew outside its parent");
            self (*child, self);
        }
    };

    for (int width : { 1100, 1400, 1900 })
    {
        const int height = width == 1100 ? 640 : (width == 1900 ? 1100 : 860);
        OpenEQAudioProcessorEditor editor (processor);
        editor.setSize (width, height);
        requireInside (editor, requireInside);
        std::cout << "  editor_containment " << width << "x" << height
                  << " children " << editor.getNumChildComponents() << "\n";
    }

    std::cout << "PASS: graph paints from the parameter and snapshot path\n";
}

static void readAlloc (long long& mallocs, long long& callocs, long long& reallocs)
{
    openeqAllocRead (&mallocs, &callocs, &reallocs);
}

static void checkSteadyStateAllocations()
{
    openeqAllocReset();
    openeqAllocSetArmed (1);
    void* probe = std::malloc (64);
    void* probeItems = std::calloc (1, 32);
    require (probe != nullptr && probeItems != nullptr, "Allocation self-test could not allocate");
    // Observable stores stop LTO from deleting the malloc/free pair and invalidating the counter.
    *static_cast<volatile unsigned char*> (probe) = 1;
    *static_cast<volatile unsigned char*> (probeItems) = 1;
    openeqAllocSetArmed (0);
    long long mallocSeen = 0;
    long long callocSeen = 0;
    long long reallocSeen = 0;
    readAlloc (mallocSeen, callocSeen, reallocSeen);
    std::free (probe);
    std::free (probeItems);
    std::cout << "  interpose_selftest_malloc " << mallocSeen << " calloc " << callocSeen << "\n";
    require (mallocSeen >= 1, "Allocation measurement invalid: malloc interpose did not observe std::malloc");
    require (callocSeen >= 1, "Allocation measurement invalid: calloc interpose did not observe std::calloc");

    auto runSteady = [] (OpenEQAudioProcessor& processor, int blocks, int blockSize, const char* label)
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        for (int i = 0; i < 8; ++i)
        {
            fillTone (buffer, 48000.0, 1000.0, 0.2f, i * blockSize, Tone::Centered);
            process (processor, buffer);
        }

        openeqAllocReset();
        const auto before = processor.getDesignUpdateCount();
        openeqAllocSetArmed (1);
        for (int i = 0; i < blocks; ++i)
        {
            fillTone (buffer, 48000.0, 1000.0, 0.2f, (8 + i) * blockSize, Tone::Centered);
            process (processor, buffer);
            require (allFinite (buffer), "Steady block produced a non-finite sample");
        }
        openeqAllocSetArmed (0);
        long long mallocs = 0;
        long long callocs = 0;
        long long reallocs = 0;
        readAlloc (mallocs, callocs, reallocs);
        const auto delta = processor.getDesignUpdateCount() - before;
        std::cout << "  " << label << "_malloc " << mallocs
                  << " calloc " << callocs
                  << " realloc " << reallocs
                  << " design_delta " << delta << "\n";
        if (mallocs > 0)
            symbolize ("malloc", openeqAllocFirstCaller (0));
        if (callocs > 0)
            symbolize ("calloc", openeqAllocFirstCaller (1));
        if (reallocs > 0)
            symbolize ("realloc", openeqAllocFirstCaller (2));
        require (mallocs == 0 && callocs == 0 && reallocs == 0,
                 "Steady-state audio path allocated");
        require (delta == 0, "Steady-state audio path rebuilt coefficients");
    };

    {
        OpenEQAudioProcessor processor;
        silenceBands (processor);
        configureBand (processor, 0, FilterType::Bell, 1000.0f, 6.0f, 0.7f, 1, (int) StereoMode::Stereo, true);
        prepareStereo (processor, 48000.0, 512);
        runSteady (processor, 40, 32, "static32");
        runSteady (processor, 40, 256, "static256");
        runSteady (processor, 30, 2048, "static2048");
    }

    {
        // Split: the separator and the second band chain run on the audio thread.
        OpenEQAudioProcessor processor;
        silenceBands (processor);
        configureBand (processor, 0, FilterType::Bell, 1000.0f, 3.0f, 0.7f, 1, (int) StereoMode::Stereo, true);
        setRaw (processor.apvts, Params::bandSplitOnID (0), 1.0f);
        setRaw (processor.apvts, Params::bandSplitTransID (0), 6.0f);
        setRaw (processor.apvts, Params::bandSplitTailID (0), -3.0f);
        prepareStereo (processor, 48000.0, 512);
        runSteady (processor, 40, 32, "split32");
        runSteady (processor, 30, 2048, "split2048");
    }

    {
        OpenEQAudioProcessor processor;
        silenceBands (processor);
        configureBand (processor, 0, FilterType::Bell, 1000.0f, 0.0f, 1.0f, 1, (int) StereoMode::Stereo, true);
        setRaw (processor.apvts, Params::bandDynOnID (0), 1.0f);
        setRaw (processor.apvts, Params::bandDynThreshID (0), -24.0f);
        setRaw (processor.apvts, Params::bandDynRangeID (0), 12.0f);
        setRaw (processor.apvts, Params::bandDynAttackID (0), 0.1f);
        setRaw (processor.apvts, Params::bandDynReleaseID (0), 80.0f);
        prepareStereo (processor, 48000.0, 512);

        juce::AudioBuffer<float> warmup (2, 480);
        for (int i = 0; i < 30; ++i)
        {
            fillTone (warmup, 48000.0, 1000.0, 0.5f, i * 480, Tone::Centered);
            process (processor, warmup);
        }
        std::cout << "  dynamic_warmup_envelope_db " << processor.getDynamicEnvelopeDb (0) << "\n";

        openeqAllocReset();
        const auto before = processor.getDesignUpdateCount();
        openeqAllocSetArmed (1);
        for (int i = 0; i < 100; ++i)
        {
            fillTone (warmup, 48000.0, 1000.0, 0.5f, (30 + i) * 480, Tone::Centered);
            process (processor, warmup);
            require (allFinite (warmup), "Dynamic steady block produced a non-finite sample");
        }
        openeqAllocSetArmed (0);
        long long mallocs = 0;
        long long callocs = 0;
        long long reallocs = 0;
        readAlloc (mallocs, callocs, reallocs);
        const auto delta = processor.getDesignUpdateCount() - before;
        std::cout << "  dynamic_malloc " << mallocs
                  << " calloc " << callocs
                  << " realloc " << reallocs
                  << " design_delta " << delta
                  << " envelope_db " << processor.getDynamicEnvelopeDb (0) << "\n";
        if (mallocs > 0)
            symbolize ("dynamic_malloc", openeqAllocFirstCaller (0));
        if (callocs > 0)
            symbolize ("dynamic_calloc", openeqAllocFirstCaller (1));
        if (reallocs > 0)
            symbolize ("dynamic_realloc", openeqAllocFirstCaller (2));
        require (mallocs == 0 && callocs == 0 && reallocs == 0,
                 "Settled dynamic path allocated");
        require (delta == 0, "Settled dynamic path rebuilt coefficients");
    }

    std::cout << "PASS: steady-state static and dynamic paths did not allocate or rebuild\n";
}

static void flushLinearPhase (OpenEQAudioProcessor& processor)
{
    const int samples = std::max (4096, processor.getReportedLatencySamples() * 2 + 1024);
    juce::AudioBuffer<float> silence (2, std::max (1, samples));
    silence.clear();
    process (processor, silence);
}

static int peakIndex (const float* data, int length)
{
    int index = 0;
    float peak = 0.0f;
    for (int i = 0; i < length; ++i)
        if (std::abs (data[i]) > peak)
        {
            peak = std::abs (data[i]);
            index = i;
        }
    return index;
}

static void checkRoadmap()
{
    {
        const auto tilt = ResponseModel::designBand (FilterType::TiltShelf, 1000.0f, 6.0f, 0.7f, 12, 48000.0);
        const double low = ResponseModel::designedMagnitude (tilt, 100.0, 48000.0);
        const double high = ResponseModel::designedMagnitude (tilt, 8000.0, 48000.0);
        std::cout << "  tilt_low_db " << juce::Decibels::gainToDecibels (low)
                  << " tilt_high_db " << juce::Decibels::gainToDecibels (high) << "\n";
        require (juce::Decibels::gainToDecibels (high) > juce::Decibels::gainToDecibels (low) + 2.0,
                 "Tilt shelf did not lift the highs above the lows");

        OpenEQAudioProcessor processor;
        silenceBands (processor);
        configureBand (processor, 0, FilterType::TiltShelf, 1000.0f, 6.0f, 0.7f, 1, (int) StereoMode::Stereo, true);
        prepareStereo (processor, 48000.0, 512);
        juce::AudioBuffer<float> lowTone (2, 4800);
        juce::AudioBuffer<float> highTone (2, 4800);
        fillTone (lowTone, 48000.0, 100.0, 0.1f, 0, Tone::Centered);
        fillTone (highTone, 48000.0, 8000.0, 0.1f, 0, Tone::Centered);
        const auto dryLow = copyBuffer (lowTone);
        const auto dryHigh = copyBuffer (highTone);
        process (processor, lowTone);
        OpenEQAudioProcessor highProcessor;
        silenceBands (highProcessor);
        configureBand (highProcessor, 0, FilterType::TiltShelf, 1000.0f, 6.0f, 0.7f, 1, (int) StereoMode::Stereo, true);
        prepareStereo (highProcessor, 48000.0, 512);
        process (highProcessor, highTone);
        const double lowRatio = rmsRange (lowTone.getReadPointer (0), 1440, 3360) / rmsRange (dryLow.getReadPointer (0), 1440, 3360);
        const double highRatio = rmsRange (highTone.getReadPointer (0), 1440, 3360) / rmsRange (dryHigh.getReadPointer (0), 1440, 3360);
        std::cout << "  tilt_audio_low_db " << dbRatio (lowRatio) << " tilt_audio_high_db " << dbRatio (highRatio) << "\n";
        require (allFinite (lowTone) && allFinite (highTone), "Tilt render produced a non-finite sample");
        require (dbRatio (highRatio) > dbRatio (lowRatio) + 1.5, "Tilt audio did not separate low and high");
    }

    {
        const auto allpass = ResponseModel::designBand (FilterType::AllPass, 1000.0f, 0.0f, 0.7f, 12, 48000.0);
        double maxMagError = 0.0;
        for (double frequency : { 100.0, 1000.0, 5000.0 })
            maxMagError = std::max (maxMagError, std::abs (ResponseModel::designedMagnitude (allpass, frequency, 48000.0) - 1.0));
        std::cout << "  allpass_mag_error " << maxMagError << "\n";
        require (maxMagError < 0.02, "All-pass magnitude left unity");

        EQBand band;
        juce::dsp::ProcessSpec spec { 48000.0, 512, 1 };
        band.prepare (spec);
        band.applyIfChanged (FilterType::AllPass, 1000.0f, 0.0f, 0.7f, 12);
        std::vector<float> impulse (4096, 0.0f);
        impulse[0] = 1.0f;
        band.processChannel (impulse.data(), (int) impulse.size(), 0);
        double tail = 0.0;
        for (size_t i = 1; i < impulse.size(); ++i)
            tail += std::abs (impulse[i]);
        std::cout << "  allpass_impulse_tail " << tail << "\n";
        require (std::isfinite (tail) && tail > 0.05, "All-pass did not move energy off the first sample");
    }

    {
        const auto soft = ResponseModel::designBand (FilterType::SoftBell, 1000.0f, 12.0f, 1.0f, 12, 48000.0);
        const auto plain = ResponseModel::designBand (FilterType::Bell, 1000.0f, 12.0f, 1.0f, 12, 48000.0);
        std::cout << "  soft_bell_q " << soft.q << " bell_q " << plain.q << "\n";
        require (std::abs (soft.q - 1.15f) < 0.02f, "Soft bell did not use q * (1 + 0.15 * |gain| / 12)");
        require (std::abs (plain.q - 1.0f) < 0.001f, "Plain bell Q changed");
        const double softMag = ResponseModel::designedMagnitude (soft, 1000.0, 48000.0);
        require (std::abs (juce::Decibels::gainToDecibels (softMag) - 12.0) < 0.2, "Soft bell missed its centre gain");
    }

    {
        OpenEQAudioProcessor processor;
        silenceBands (processor);
        configureBand (processor, 0, FilterType::Bell, 1000.0f, 0.0f, 1.0f, 1, (int) StereoMode::Stereo, true);
        setRaw (processor.apvts, Params::bandDynOnID (0), 1.0f);
        setRaw (processor.apvts, Params::bandDynThreshID (0), -24.0f);
        setRaw (processor.apvts, Params::bandDynRangeID (0), 12.0f);
        setRaw (processor.apvts, Params::bandDynAttackID (0), 0.1f);
        prepareStereo (processor, 48000.0, 512);
        juce::AudioBuffer<float> anti (2, 2048);
        fillTone (anti, 48000.0, 1000.0, 0.5f, 0, Tone::Centered);
        juce::FloatVectorOperations::negate (anti.getWritePointer (1), anti.getReadPointer (1), anti.getNumSamples());
        process (processor, anti);
        const float hot = processor.getDynamicEnvelopeDb (0);
        std::cout << "  antiphase_detector_db " << hot << "\n";
        require (allFinite (anti), "Anti-phase dynamic render produced a non-finite sample");
        require (hot > -30.0f, "Anti-phase stereo cancelled the energy detector");
    }

    {
        OpenEQAudioProcessor wide;
        OpenEQAudioProcessor narrow;
        auto arm = [] (OpenEQAudioProcessor& processor)
        {
            silenceBands (processor);
            configureBand (processor, 0, FilterType::Bell, 1000.0f, 0.0f, 1.0f, 1, (int) StereoMode::Stereo, true);
            setRaw (processor.apvts, Params::bandDynOnID (0), 1.0f);
            setRaw (processor.apvts, Params::bandDynThreshID (0), -24.0f);
            setRaw (processor.apvts, Params::bandDynRangeID (0), 12.0f);
            setRaw (processor.apvts, Params::bandDynAttackID (0), 0.1f);
            setRaw (processor.apvts, Params::bandDynReleaseID (0), 8.0f);
            prepareStereo (processor, 48000.0, 512);
            juce::AudioBuffer<float> loud (2, 512);
            fillTone (loud, 48000.0, 1000.0, 0.5f, 0, Tone::Centered);
            process (processor, loud);
        };
        arm (wide);
        arm (narrow);
        juce::AudioBuffer<float> gap (2, 800);
        gap.clear();
        process (wide, gap);
        for (int i = 0; i < 50; ++i)
        {
            juce::AudioBuffer<float> slice (2, 16);
            slice.clear();
            process (narrow, slice);
        }
        std::cout << "  envelope_800 " << wide.getDynamicEnvelopeDb (0)
                  << " envelope_50x16 " << narrow.getDynamicEnvelopeDb (0) << "\n";
        require (std::abs (wide.getDynamicEnvelopeDb (0) - narrow.getDynamicEnvelopeDb (0)) < 1.5f,
                 "Dynamic envelope depended on the host block size");
    }

    {
        OpenEQAudioProcessor processor;
        silenceBands (processor);
        configureBand (processor, 0, FilterType::Bell, 1000.0f, 0.0f, 1.0f, 1, (int) StereoMode::Stereo, true);
        setRaw (processor.apvts, Params::bandDynOnID (0), 1.0f);
        setRaw (processor.apvts, Params::bandDynActionID (0), 1.0f);
        setRaw (processor.apvts, Params::bandDynKneeID (0), 6.0f);
        setRaw (processor.apvts, Params::bandDynBlendID (0), 50.0f);
        setRaw (processor.apvts, Params::bandDynAheadID (0), 5.0f);
        prepareStereo (processor, 48000.0, 512);
        juce::AudioBuffer<float> tone (2, 2048);
        fillTone (tone, 48000.0, 1000.0, 0.2f, 0, Tone::Centered);
        process (processor, tone);
        const int latency = processor.getReportedLatencySamples();
        std::cout << "  lookahead_latency " << latency << "\n";
        require (allFinite (tone), "Lookahead render produced a non-finite sample");
        require (latency == (int) std::llround (0.020 * 48000.0), "Lookahead did not take the fixed 20 ms delay");
    }

    {
        OpenEQAudioProcessor processor;
        const float original = rawValue (processor, Params::bandFreqID (0));
        processor.beginEdit ("Frequency");
        processor.setRawFromUI (Params::bandFreqID (0), 1000.0f);
        processor.setRawFromUI (Params::bandFreqID (0), 1800.0f);
        processor.endEdit();
        require (processor.undo(), "One gesture did not undo");
        require (std::abs (rawValue (processor, Params::bandFreqID (0)) - original) < 1.0f, "Undo did not restore the gesture start");
        require (! processor.undo(), "One gesture produced more than one undo step");
        setRaw (processor.apvts, Params::bandFreqID (0), 4000.0f);
        require (! processor.undo(), "Direct parameter write entered the undo stack");
        require (std::abs (rawValue (processor, Params::bandFreqID (0)) - 4000.0f) < 1.0f, "Undo moved a host-style write");
        std::cout << "  undo_gesture_steps 1\n";
    }

    {
        OpenEQAudioProcessor processor;
        setRaw (processor.apvts, Params::bandGainID (0), 5.0f);
        processor.switchAB();
        const float other = rawValue (processor, Params::bandGainID (0));
        processor.switchAB();
        const float back = rawValue (processor, Params::bandGainID (0));
        std::cout << "  ab_other_gain " << other << " ab_restored_gain " << back << "\n";
        require (std::abs (other) < 0.05f, "A/B switch did not load the other slot");
        require (std::abs (back - 5.0f) < 0.05f, "A/B switch did not restore the edited slot");
    }

    {
        OpenEQAudioProcessor processor;
        setRaw (processor.apvts, Params::bandFreqID (3), 4321.0f);
        auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("openeq-roadmap-preset.xml");
        require (processor.savePresetFile (file), "Preset file was not written");
        setRaw (processor.apvts, Params::bandFreqID (3), 1000.0f);
        require (processor.loadPresetFile (file), "Preset file was not read");
        const float restored = rawValue (processor, Params::bandFreqID (3));
        file.deleteFile();
        std::cout << "  preset_restored_freq " << restored << "\n";
        require (std::abs (restored - 4321.0f) < 1.0f, "Preset file did not restore frequency");
    }

    {
        OpenEQAudioProcessor processor;
        processor.setMonitorMode (3);
        processor.setAudition (true, 2);
        juce::MemoryBlock state;
        processor.getStateInformation (state);
        OpenEQAudioProcessor restored;
        restored.setStateInformation (state.getData(), (int) state.getSize());
        std::cout << "  restored_monitor " << restored.getMonitorMode()
                  << " restored_audition " << (restored.isAuditioning() ? 1 : 0) << "\n";
        require (restored.getMonitorMode() == 0, "Monitor mode was saved in the session");
        require (! restored.isAuditioning(), "Audition was saved in the session");
    }

    {
        float hz = 0.0f;
        float db = 0.0f;
        require (ValueFormat::parseFrequency ("2.5k", hz) && std::abs (hz - 2500.0f) < 1.0f, "2.5k did not parse");
        require (ValueFormat::parseFrequency ("A4", hz) && std::abs (hz - 440.0f) < 0.5f, "A4 did not parse");
        require (ValueFormat::parseGainDb ("-3 dB", db) && std::abs (db + 3.0f) < 0.01f, "-3 dB did not parse");
        std::cout << "  parsed_hz " << hz << " parsed_db " << db << "\n";
    }

    {
        const float frequency[] = { 100.0f, 1000.0f, 10000.0f };
        const float target[] = { 0.0f, 6.0f, 0.0f };
        std::vector<CurveFit::Band> fitted;
        CurveFit::fitBells (frequency, target, 3, 4, 48000.0, fitted);
        require (! fitted.empty(), "Curve fit produced no bands");
        std::cout << "  fit_freq " << fitted.front().frequency << " fit_gain " << fitted.front().gainDb << "\n";
        require (std::abs (fitted.front().frequency - 1000.0f) < 1.0f, "Curve fit missed the 1 kHz bump");

        const float source[] = { -80.0f, -80.0f, -80.0f, -80.0f, -80.0f, -20.0f, -20.0f, -20.0f, -20.0f, -20.0f };
        const float reference[] = { -80.0f, -80.0f, -80.0f, -80.0f, -80.0f, -8.0f, -8.0f, -8.0f, -8.0f, -8.0f };
        float difference[10] = {};
        CurveFit::matchDifference (source, reference, 10, difference, 12.0f);
        std::cout << "  match_quiet " << difference[0] << " match_band " << difference[9] << "\n";
        require (std::abs (difference[0]) < 0.01f, "Match used bins that were both quiet");
        require (difference[9] > 5.0f, "Match did not report the louder reference");
    }

    {
        OpenEQAudioProcessor processor;
        silenceBands (processor);
        setRaw (processor.apvts, "spectralAmount", 1.0f);
        prepareStereo (processor, 48000.0, 512);
        juce::AudioBuffer<float> tone (2, 4096);
        fillTone (tone, 48000.0, 1000.0, 0.2f, 0, Tone::Centered);
        process (processor, tone);
        std::cout << "  spectral_latency " << processor.getReportedLatencySamples() << "\n";
        require (allFinite (tone), "Spectral path produced a non-finite sample");
        require (processor.getReportedLatencySamples() >= 1000, "Spectral path did not add its STFT latency");
    }

    {
        OpenEQAudioProcessor processor;
        silenceBands (processor);
        setRaw (processor.apvts, "drive", 0.4f);
        prepareStereo (processor, 48000.0, 512);
        juce::AudioBuffer<float> tone (2, 2048);
        fillTone (tone, 48000.0, 1000.0, 0.8f, 0, Tone::Centered);
        process (processor, tone);
        std::cout << "  drive_latency " << processor.getReportedLatencySamples() << "\n";
        require (allFinite (tone), "Drive path produced a non-finite sample");
        require (processor.getReportedLatencySamples() > 0, "Drive did not report oversampler latency");
    }

    {
        OpenEQAudioProcessor processor;
        silenceBands (processor);
        configureBand (processor, 0, FilterType::Bell, 1000.0f, 6.0f, 1.0f, 1, (int) StereoMode::Stereo, true);
        prepareStereo (processor, 48000.0, 512);
        juce::AudioBuffer<float> dirty (2, 512);
        fillTone (dirty, 48000.0, 1000.0, 0.2f, 0, Tone::Centered);
        dirty.setSample (0, 10, std::numeric_limits<float>::quiet_NaN());
        dirty.setSample (1, 11, std::numeric_limits<float>::infinity());
        process (processor, dirty);
        require (allFinite (dirty), "Non-finite input survived the scrub");

        OpenEQAudioProcessor wide;
        silenceBands (wide);
        configureBand (wide, 0, FilterType::Bell, 1000.0f, 6.0f, 1.0f, 1, (int) StereoMode::Stereo, true);
        wide.setRateAndBufferSizeDetails (48000.0, 512);
        wide.prepareToPlay (48000.0, 512);
        juce::AudioBuffer<double> doubled (2, 512);
        for (int i = 0; i < 512; ++i)
        {
            const double sample = 0.2 * std::sin (juce::MathConstants<double>::twoPi * 1000.0 * i / 48000.0);
            doubled.setSample (0, i, sample);
            doubled.setSample (1, i, sample);
        }
        doubled.setSample (0, 4, std::numeric_limits<double>::quiet_NaN());
        juce::MidiBuffer midi;
        wide.processBlock (doubled, midi);
        bool finite = true;
        for (int channel = 0; channel < doubled.getNumChannels() && finite; ++channel)
            for (int i = 0; i < doubled.getNumSamples(); ++i)
                if (! std::isfinite (doubled.getSample (channel, i)))
                    finite = false;
        std::cout << "  double_path_finite " << (finite ? 1 : 0) << "\n";
        require (finite, "Double-precision conversion path produced a non-finite sample");
    }

    {
        OpenEQAudioProcessor processor;
        silenceBands (processor);
        setRaw (processor.apvts, "processingMode", 1.0f);
        setRaw (processor.apvts, "linearPhaseQuality", 0.0f);
        prepareStereo (processor, 48000.0, 512);
        juce::AudioBuffer<float> warmup (2, 256);
        warmup.clear();
        process (processor, warmup);
        const auto started = processor.getLinearPhaseDesigns();
        const auto deadline = juce::Time::getMillisecondCounter() + 1500;
        while (processor.getLinearPhaseDesigns() == started && juce::Time::getMillisecondCounter() < deadline)
            juce::Thread::sleep (10);
        flushLinearPhase (processor);
        constexpr int length = 8192;
        juce::AudioBuffer<float> impulse (2, length);
        impulse.clear();
        impulse.setSample (0, 0, 1.0f);
        impulse.setSample (1, 0, 1.0f);
        process (processor, impulse);
        const int latency = processor.getReportedLatencySamples();
        const int peak = peakIndex (impulse.getReadPointer (0), length);
        const float peakValue = std::abs (impulse.getSample (0, peak));
        std::cout << "  linear_phase_latency " << latency
                  << " impulse_peak_index " << peak
                  << " impulse_peak " << peakValue
                  << " designs " << processor.getLinearPhaseDesigns() << "\n";
        require (allFinite (impulse), "Linear-phase impulse produced a non-finite sample");
        require (latency == 1024, "Low quality linear phase did not report 1024 samples");
        require (std::abs (peak - latency) <= 2, "Linear-phase impulse peak was not at the reported latency");
        require (peakValue > 0.5f && peakValue < 1.5f, "Flat linear-phase impulse was not near unity");
    }

    auto renderLinearPhaseImpulse = [] (float quality, double sampleRate, int reported)
    {
        OpenEQAudioProcessor processor;
        silenceBands (processor);
        setRaw (processor.apvts, "processingMode", 1.0f);
        setRaw (processor.apvts, "linearPhaseQuality", quality);
        prepareStereo (processor, sampleRate, 512);
        juce::AudioBuffer<float> warmup (2, 256);
        warmup.clear();
        process (processor, warmup);
        const auto started = processor.getLinearPhaseDesigns();
        const auto deadline = juce::Time::getMillisecondCounter() + 2000;
        while (processor.getLinearPhaseDesigns() == started && juce::Time::getMillisecondCounter() < deadline)
            juce::Thread::sleep (10);
        flushLinearPhase (processor);
        constexpr int length = 16384;
        juce::AudioBuffer<float> impulse (2, length);
        impulse.clear();
        impulse.setSample (0, 0, 1.0f);
        impulse.setSample (1, 0, 1.0f);
        process (processor, impulse);
        const int latency = processor.getReportedLatencySamples();
        const int peak = peakIndex (impulse.getReadPointer (0), length);
        const float peakValue = std::abs (impulse.getSample (0, peak));
        std::cout << "  linear_phase_quality " << quality
                  << " rate " << sampleRate
                  << " latency " << latency
                  << " peak " << peak
                  << " value " << peakValue << "\n";
        require (latency == reported, "Linear-phase latency did not follow the quality");
        require (std::abs (peak - latency) <= 2, "Linear-phase impulse peak missed the reported latency");
        require (peakValue > 0.5f && peakValue < 1.5f, "Flat linear-phase impulse was not near unity");
    };
    renderLinearPhaseImpulse (1.0f, 48000.0, 2048);
    renderLinearPhaseImpulse (0.0f, 44100.0, 1024);

    {
        OpenEQAudioProcessor processor;
        silenceBands (processor);
        setRaw (processor.apvts, "bypass", 1.0f);
        setRaw (processor.apvts, "processingMode", 1.0f);
        setRaw (processor.apvts, "linearPhaseQuality", 0.0f);
        prepareStereo (processor, 48000.0, 512);
        juce::AudioBuffer<float> warmup (2, 256);
        warmup.clear();
        process (processor, warmup);
        constexpr int length = 8192;
        juce::AudioBuffer<float> impulse (2, length);
        impulse.clear();
        impulse.setSample (0, 0, 1.0f);
        impulse.setSample (1, 0, 1.0f);
        process (processor, impulse);
        const int latency = processor.getReportedLatencySamples();
        const int peak = peakIndex (impulse.getReadPointer (0), length);
        const float peakValue = std::abs (impulse.getSample (0, peak));
        std::cout << "  linear_phase_bypass_latency " << latency
                  << " peak " << peak
                  << " value " << peakValue << "\n";
        require (latency == 1024, "Bypassed linear phase dropped its latency report");
        require (std::abs (peak - latency) <= 2, "Bypassed linear phase did not keep the dry impulse aligned");
        require (peakValue > 0.9f && peakValue < 1.1f, "Bypassed linear-phase impulse was not unity");
    }

    {
        OpenEQAudioProcessor processor;
        silenceBands (processor);
        configureBand (processor, 0, FilterType::Bell, 1000.0f, 12.0f, 0.5f, 1, (int) StereoMode::Left, true);
        setRaw (processor.apvts, "processingMode", 1.0f);
        setRaw (processor.apvts, "linearPhaseQuality", 0.0f);
        prepareStereo (processor, 48000.0, 512);
        juce::AudioBuffer<float> warmup (2, 512);
        warmup.clear();
        process (processor, warmup);
        const auto started = processor.getLinearPhaseDesigns();
        const auto deadline = juce::Time::getMillisecondCounter() + 2000;
        while (processor.getLinearPhaseDesigns() == started && juce::Time::getMillisecondCounter() < deadline)
            juce::Thread::sleep (10);
        flushLinearPhase (processor);
        constexpr int length = 8192;
        juce::AudioBuffer<float> impulse (2, length);
        impulse.clear();
        impulse.setSample (0, 0, 1.0f);
        impulse.setSample (1, 0, 1.0f);
        process (processor, impulse);
        const int latency = processor.getReportedLatencySamples();
        const int leftPeak = peakIndex (impulse.getReadPointer (0), length);
        const int rightPeak = peakIndex (impulse.getReadPointer (1), length);
        const float leftValue = std::abs (impulse.getSample (0, leftPeak));
        const float rightValue = std::abs (impulse.getSample (1, rightPeak));
        std::cout << "  linear_phase_left_peak " << leftPeak
                  << " left_value " << leftValue
                  << " right_peak " << rightPeak
                  << " right_value " << rightValue << "\n";
        require (std::abs (leftPeak - latency) <= 4, "Left linear-phase band moved the shared delay");
        require (std::abs (rightPeak - latency) <= 2, "Untouched right path left the shared delay");
        require (rightValue > 0.5f && rightValue < 1.5f, "Untouched right path was not near unity");
        require (leftValue > rightValue + 0.05f, "Left boost did not raise the left impulse above the right path");
    }

    {
        OpenEQAudioProcessor loudSide;
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add (juce::AudioChannelSet::stereo());
        layout.inputBuses.add (juce::AudioChannelSet::stereo());
        layout.outputBuses.add (juce::AudioChannelSet::stereo());
        require (loudSide.setBusesLayout (layout), "Processor rejected a stereo sidechain layout");
        silenceBands (loudSide);
        configureBand (loudSide, 0, FilterType::Bell, 1000.0f, 0.0f, 1.0f, 1, (int) StereoMode::Stereo, true);
        setRaw (loudSide.apvts, Params::bandDynOnID (0), 1.0f);
        setRaw (loudSide.apvts, Params::bandDynSourceID (0), 5.0f);
        setRaw (loudSide.apvts, Params::bandDynThreshID (0), -30.0f);
        setRaw (loudSide.apvts, Params::bandDynAttackID (0), 1.0f);
        prepareStereo (loudSide, 48000.0, 512);

        OpenEQAudioProcessor quietSide;
        require (quietSide.setBusesLayout (layout), "Processor rejected a stereo sidechain layout");
        silenceBands (quietSide);
        configureBand (quietSide, 0, FilterType::Bell, 1000.0f, 0.0f, 1.0f, 1, (int) StereoMode::Stereo, true);
        setRaw (quietSide.apvts, Params::bandDynOnID (0), 1.0f);
        setRaw (quietSide.apvts, Params::bandDynSourceID (0), 5.0f);
        setRaw (quietSide.apvts, Params::bandDynThreshID (0), -30.0f);
        setRaw (quietSide.apvts, Params::bandDynAttackID (0), 1.0f);
        prepareStereo (quietSide, 48000.0, 512);

        constexpr int length = 9600;
        juce::AudioBuffer<float> driven (4, length);
        juce::AudioBuffer<float> idle (4, length);
        driven.clear();
        idle.clear();
        for (int i = 0; i < length; ++i)
        {
            const float main = 0.2f * std::sin (juce::MathConstants<float>::twoPi * 1000.0f * (float) i / 48000.0f);
            const float side = 0.5f * std::sin (juce::MathConstants<float>::twoPi * 1000.0f * (float) i / 48000.0f);
            driven.setSample (0, i, main);
            driven.setSample (1, i, main);
            driven.setSample (2, i, side);
            driven.setSample (3, i, side);
            idle.setSample (0, i, main);
            idle.setSample (1, i, main);
        }
        process (loudSide, driven);
        process (quietSide, idle);
        const float hot = loudSide.getDynamicEnvelopeDb (0);
        const float cold = quietSide.getDynamicEnvelopeDb (0);
        std::cout << "  sidechain_hot_db " << hot << " sidechain_cold_db " << cold << "\n";
        require (allFinite (driven) && allFinite (idle), "Sidechain render produced a non-finite sample");
        require (hot > -18.0f, "External sidechain energy did not open the detector");
        require (cold < -40.0f, "External detector followed the main input while the sidechain was silent");
    }

    {
        SpectralEngine spectral;
        spectral.prepare (48000.0);
        constexpr int length = 8192;
        std::vector<float> left ((size_t) length, 0.0f), right ((size_t) length, 0.0f);
        left[0] = 1.0f;
        right[0] = 0.5f;
        spectral.process (left.data(), right.data(), length, 0.0f, -18.0f, 3000.0f, 1.0f);
        const int latency = spectral.getLatencySamples();
        const int peak = peakIndex (left.data(), length);
        double noise = 0.0;
        double signal = 0.0;
        for (int i = 0; i < length; ++i)
        {
            const float expected = (i == latency ? 1.0f : 0.0f);
            const double error = (double) left[(size_t) i] - (double) expected;
            noise += error * error;
            signal += (double) expected * (double) expected;
            require (std::isfinite (left[(size_t) i]) && std::isfinite (right[(size_t) i]),
                     "Spectral reconstruction produced a non-finite sample");
        }
        const double snr = 10.0 * std::log10 (signal / std::max (1.0e-20, noise));
        std::cout << "  spectral_unity_peak " << peak
                  << " value " << left[(size_t) peak]
                  << " right " << right[(size_t) peak]
                  << " snr_db " << snr << "\n";
        require (latency == 1023, "Spectral engine latency is not fftSize - 1");
        require (std::abs (peak - latency) <= 1, "Spectral unity impulse was not at the documented latency");
        require (std::abs (left[(size_t) peak] - 1.0f) < 0.05f, "Spectral unity impulse was not near 1");
        require (std::abs (right[(size_t) peak] - 0.5f) < 0.05f, "Spectral unity did not keep the right-channel level");
        require (snr > 40.0, "Spectral unity reconstruction SNR was below 40 dB");
    }

    std::cout << "PASS: roadmap shapes, dynamics, editing, spectral, drive, and linear phase\n";
}

static juce::MouseEvent mouseAt (juce::Component& component, float x, float y, juce::ModifierKeys mods, int clicks = 1)
{
    const juce::Point<float> position (x, y);
    return { juce::Desktop::getInstance().getMainMouseSource(),
             position, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
             &component, &component, juce::Time::getCurrentTime(),
             position, juce::Time::getCurrentTime(), clicks, false };
}

static double bufferRms (const juce::AudioBuffer<float>& buffer, int channel, int start, int count)
{
    return rmsRange (buffer.getReadPointer (channel), start, count);
}

static double shapeDb (FilterType type, float q, double frequency)
{
    const auto designed = ResponseModel::designBand (type, 1000.0f, 12.0f, q, 12, 48000.0);
    return juce::Decibels::gainToDecibels (ResponseModel::designedMagnitude (designed, frequency, 48000.0));
}

static double lineDeviation (FilterType type)
{
    const double frequencies[] = { 250.0, 500.0, 1000.0, 2000.0, 4000.0 };
    double worst = 0.0;
    const double low = shapeDb (type, 0.7f, 250.0);
    const double high = shapeDb (type, 0.7f, 4000.0);
    const double slope = (high - low) / std::log2 (4000.0 / 250.0);
    for (double frequency : frequencies)
    {
        const double expected = low + slope * std::log2 (frequency / 250.0);
        worst = std::max (worst, std::abs (shapeDb (type, 0.7f, frequency) - expected));
    }
    return worst;
}

static void checkPlanCoverage()
{
    {
        const auto flat = ResponseModel::designBand (FilterType::FlatTilt, 1000.0f, 6.0f, 0.7f, 12, 48000.0);
        const double low = juce::Decibels::gainToDecibels (ResponseModel::designedMagnitude (flat, 80.0, 48000.0));
        const double pivot = juce::Decibels::gainToDecibels (ResponseModel::designedMagnitude (flat, 1000.0, 48000.0));
        const double high = juce::Decibels::gainToDecibels (ResponseModel::designedMagnitude (flat, 10000.0, 48000.0));
        const double flatDeviation = lineDeviation (FilterType::FlatTilt);
        const double shelfDeviation = lineDeviation (FilterType::TiltShelf);
        std::cout << "  flat_tilt_low_db " << low << " pivot_db " << pivot << " high_db " << high
                  << " line_deviation " << flatDeviation << " shelf_deviation " << shelfDeviation << "\n";
        require (high > low + 2.0, "Flat tilt did not lift the highs above the lows");
        require (std::abs (pivot) < 0.4, "Flat tilt pivot left unity");
        require (low < -1.5 && high > 1.5, "Flat tilt asymptotes did not approach half the commanded gain");
        require (flatDeviation < shelfDeviation, "Flat tilt was less straight than the tilt shelf");

        const double bellCentre = shapeDb (FilterType::Bell, 1.0f, 1000.0);
        const double bellOffset = shapeDb (FilterType::Bell, 1.0f, 1000.0 * std::pow (2.0, 0.2));
        const double flatCentre = shapeDb (FilterType::FlatTop, 1.0f, 1000.0);
        const double flatOffset = shapeDb (FilterType::FlatTop, 1.0f, 1000.0 * std::pow (2.0, 0.2));
        const double narrowCentre = shapeDb (FilterType::NarrowResonance, 1.0f, 1000.0);
        const double narrowOctave = shapeDb (FilterType::NarrowResonance, 1.0f, 2000.0);
        const double bellOctave = shapeDb (FilterType::Bell, 1.0f, 2000.0);
        std::cout << "  flat_top_centre_db " << flatCentre << " offset_db " << flatOffset
                  << " bell_drop_db " << (bellCentre - bellOffset)
                  << " flat_drop_db " << (flatCentre - flatOffset) << "\n";
        std::cout << "  narrow_centre_db " << narrowCentre << " octave_db " << narrowOctave
                  << " bell_octave_db " << bellOctave << "\n";
        require (flatCentre > 11.0, "Flat top missed the commanded centre");
        // The flat top is fitted to reference measured shape (two bells). It keeps most of its gain a quarter octave away from centre.
        const double flatQuarter = shapeDb (FilterType::FlatTop, 6.0f, 1000.0 * std::pow (2.0, 0.25));
        const double flatQuarterCentre = shapeDb (FilterType::FlatTop, 6.0f, 1000.0);
        std::cout << "  flat_top_quarter_octave_db " << flatQuarter << " centre_db " << flatQuarterCentre << "\n";
        require (flatQuarter > 0.6 * flatQuarterCentre, "Flat top fell away too fast within a quarter octave");
        require (std::abs (narrowCentre - 12.0) < 0.3, "Narrow resonance missed its centre gain");
        require (std::abs (narrowOctave) + 1.0 < std::abs (bellOctave), "Narrow resonance was not tighter than a bell");
    }

    {
        const float eqZero = EQCurveComponent::dbToY (0.0f, 100.0f);
        const float analyzerZero = SpectrumAnalyzer::dbToY (0.0f, 100.0f);
        const float tilted = SpectrumAnalyzer::applyDisplayTilt (-12.0f, 2000.0f, 3.0f);
        std::cout << "  eq_zero_y " << eqZero << " analyzer_zero_y " << analyzerZero
                  << " display_tilt_db " << tilted << "\n";
        require (std::abs (eqZero - 50.0f) < 1.0f, "EQ zero dB is not the middle of the curve scale");
        require (analyzerZero < 5.0f, "Analyzer 0 dB is not the top of its own scale");
        require (std::abs (tilted - (-9.0f)) < 0.05f, "Display tilt did not add 3 dB at one octave");
    }

    {
        OpenEQAudioProcessor processor;
        silenceBands (processor);
        prepareStereo (processor, 48000.0, 512);
        juce::AudioBuffer<float> warmup (2, 4096);
        fillTone (warmup, 48000.0, 1000.0, 0.25f, 0, Tone::Centered);
        process (processor, warmup);
        setRaw (processor.apvts, "polarity", 1.0f);
        juce::AudioBuffer<float> tone (2, 512);
        fillTone (tone, 48000.0, 1000.0, 0.25f, 4096, Tone::Centered);
        const auto dry = copyBuffer (tone);
        process (processor, tone);
        const float error = maxAbsDiff (tone, dry, 0, 0, 512);
        float worst = 0.0f;
        for (int i = 0; i < 512; ++i)
            worst = std::max (worst, std::abs (tone.getSample (0, i) + dry.getSample (0, i)));
        std::cout << "  polarity_dry_error " << error << " polarity_invert_error " << worst << "\n";
        require (allFinite (tone), "Polarity path produced a non-finite sample");
        require (worst < 1.0e-4f, "Polarity did not invert the wet output");
    }

    {
        auto renderTail = [] (bool compensate)
        {
            OpenEQAudioProcessor processor;
            silenceBands (processor);
            configureBand (processor, 0, FilterType::Bell, 1000.0f, 12.0f, 1.0f, 1, (int) StereoMode::Stereo, true);
            if (compensate)
                setRaw (processor.apvts, "autoGain", 1.0f);
            prepareStereo (processor, 48000.0, 512);
            juce::AudioBuffer<float> warmup (2, 8192);
            fillTone (warmup, 48000.0, 1000.0, 0.1f, 0, Tone::Centered);
            process (processor, warmup);
            juce::AudioBuffer<float> tone (2, 1024);
            fillTone (tone, 48000.0, 1000.0, 0.1f, 8192, Tone::Centered);
            process (processor, tone);
            return bufferRms (tone, 0, 256, 768);
        };
        const double plain = renderTail (false);
        const double compensated = renderTail (true);
        std::cout << "  autogain_plain_rms " << plain << " compensated_rms " << compensated << "\n";
        require (compensated < plain * 0.85, "Auto gain did not reduce the boosted tone");
        require (compensated > plain * 0.2, "Auto gain removed the band instead of compensating");
    }

    {
        OpenEQAudioProcessor processor;
        silenceBands (processor);
        configureBand (processor, 0, FilterType::Bell, 1000.0f, 0.0f, 1.0f, 1, (int) StereoMode::Stereo, true);
        prepareStereo (processor, 48000.0, 512);
        processor.setAudition (true, 0);
        auto render = [&] (double frequency)
        {
            juce::AudioBuffer<float> tone (2, 4096);
            fillTone (tone, 48000.0, frequency, 0.25f, 0, Tone::Centered);
            process (processor, tone);
            return bufferRms (tone, 0, 2048, 2048);
        };
        const double atBand = render (1000.0);
        const double away = render (100.0);
        std::cout << "  audition_1k_rms " << atBand << " audition_100_rms " << away << "\n";
        require (processor.isAuditioning(), "Audition did not arm");
        require (atBand > away * 4.0, "Audition did not isolate the selected range");
        processor.setAudition (false, 0);
        require (! processor.isAuditioning(), "Audition did not release");
    }

    {
        OpenEQAudioProcessor processor;
        silenceBands (processor);
        prepareStereo (processor, 48000.0, 512);
        juce::AudioBuffer<float> tone (2, 512);
        for (int i = 0; i < 512; ++i)
        {
            tone.setSample (0, i, 0.5f);
            tone.setSample (1, i, 0.1f);
        }
        juce::AudioBuffer<float> warmup (2, 2048);
        warmup.clear();
        process (processor, warmup);
        processor.setMonitorMode (1);
        process (processor, tone);
        std::cout << "  monitor_left " << tone.getSample (0, 256) << " " << tone.getSample (1, 256) << "\n";
        require (std::abs (tone.getSample (0, 256) - 0.5f) < 1.0e-3f, "Left monitor changed the left channel");
        require (std::abs (tone.getSample (1, 256) - 0.5f) < 1.0e-3f, "Left monitor did not copy the left channel");
        processor.setMonitorMode (4);
        for (int i = 0; i < 512; ++i)
        {
            tone.setSample (0, i, 0.5f);
            tone.setSample (1, i, -0.5f);
        }
        process (processor, tone);
        require (std::abs (std::abs (tone.getSample (0, 256)) - 0.5f) < 1.0e-3f, "Side monitor dropped the side signal");
        processor.setMonitorMode (0);
    }

    {
        OpenEQAudioProcessor processor;
        silenceBands (processor);
        prepareStereo (processor, 48000.0, 512);
        juce::AudioBuffer<float> warmup (2, 2048);
        warmup.clear();
        process (processor, warmup);
        juce::AudioBuffer<float> tone (2, 1024);
        fillTone (tone, 48000.0, 1000.0, 0.5f, 0, Tone::Centered);
        process (processor, tone);
        const float peak = processor.getMeterPeak (0);
        const float rms = processor.getMeterRms (0);
        std::cout << "  meter_peak " << peak << " meter_rms " << rms << "\n";
        require (std::abs (peak - 0.5f) < 0.05f, "Peak meter missed a 0.5 sine");
        require (std::abs (rms - 0.353553f) < 0.03f, "RMS meter missed a 0.5 sine");
        tone.setSample (0, 10, 1.0f);
        process (processor, tone);
        require (processor.getMeterClipped(), "Full-scale sample did not clip the meter");
    }

    {
        auto settledRms = [] (float knee, int action, float relative, float amplitude, float threshold)
        {
            OpenEQAudioProcessor processor;
            silenceBands (processor);
            configureBand (processor, 0, FilterType::Bell, 1000.0f, 0.0f, 1.0f, 1, (int) StereoMode::Stereo, true);
            setRaw (processor.apvts, Params::bandDynOnID (0), 1.0f);
            setRaw (processor.apvts, Params::bandDynThreshID (0), threshold);
            setRaw (processor.apvts, Params::bandDynRangeID (0), -12.0f);
            setRaw (processor.apvts, Params::bandDynAttackID (0), 1.0f);
            setRaw (processor.apvts, Params::bandDynReleaseID (0), 40.0f);
            setRaw (processor.apvts, Params::bandDynKneeID (0), knee);
            setRaw (processor.apvts, Params::bandDynActionID (0), (float) action);
            setRaw (processor.apvts, Params::bandDynBlendID (0), relative);
            prepareStereo (processor, 48000.0, 512);
            juce::AudioBuffer<float> tone (2, 8192);
            fillTone (tone, 48000.0, 1000.0, amplitude, 0, Tone::Centered);
            process (processor, tone);
            return bufferRms (tone, 0, 6000, 2000);
        };
        const double hard = settledRms (0.0f, 0, 0.0f, 0.5f, -6.0f);
        const double soft = settledRms (24.0f, 0, 0.0f, 0.5f, -6.0f);
        const double above = settledRms (0.0f, 0, 0.0f, 0.5f, -20.0f);
        const double below = settledRms (0.0f, 1, 0.0f, 0.5f, -20.0f);
        const double absoluteQuiet = settledRms (0.0f, 0, 0.0f, 0.5f, -3.0f);
        const double relativeHot = settledRms (0.0f, 0, 100.0f, 0.5f, -3.0f);
        std::cout << "  knee_hard_rms " << hard << " knee_soft_rms " << soft
                  << " above_rms " << above << " below_rms " << below
                  << " absolute_rms " << absoluteQuiet << " relative_rms " << relativeHot << "\n";
        require (soft < hard * 0.9, "Wide knee did not start the cut around the threshold");
        require (above < below * 0.6, "Above/below did not change a loud tone");
        require (relativeHot < absoluteQuiet * 0.75, "Relative detection did not open when absolute detection stayed closed");
    }

    {
        OpenEQAudioProcessor quiet;
        silenceBands (quiet);
        configureBand (quiet, 0, FilterType::Bell, 1000.0f, 0.0f, 1.0f, 1, (int) StereoMode::Stereo, true);
        setRaw (quiet.apvts, Params::bandDynOnID (0), 1.0f);
        setRaw (quiet.apvts, Params::bandDynThreshID (0), -40.0f);
        setRaw (quiet.apvts, Params::bandDynRangeID (0), 12.0f);
        setRaw (quiet.apvts, Params::bandDynActionID (0), 1.0f);
        setRaw (quiet.apvts, Params::bandDynAttackID (0), 1.0f);
        prepareStereo (quiet, 48000.0, 512);
        juce::AudioBuffer<float> tone (2, 8192);
        fillTone (tone, 48000.0, 1000.0, 1.0e-8f, 0, Tone::Centered);
        process (quiet, tone);
        const double output = bufferRms (tone, 0, 6000, 2000);
        std::cout << "  silence_floor_rms " << output << "\n";
        require (output < 1.0e-6, "Silence floor let a quiet passage expand");
    }

    {
        auto burst = [] (float aheadMs)
        {
            OpenEQAudioProcessor processor;
            silenceBands (processor);
            configureBand (processor, 0, FilterType::Bell, 1000.0f, 0.0f, 1.0f, 1, (int) StereoMode::Stereo, true);
            setRaw (processor.apvts, Params::bandDynOnID (0), 1.0f);
            setRaw (processor.apvts, Params::bandDynThreshID (0), -24.0f);
            setRaw (processor.apvts, Params::bandDynRangeID (0), -12.0f);
            setRaw (processor.apvts, Params::bandDynAttackID (0), 1.0f);
            setRaw (processor.apvts, Params::bandDynAheadID (0), aheadMs);
            prepareStereo (processor, 48000.0, 512);
            juce::AudioBuffer<float> silence (2, 2048);
            silence.clear();
            process (processor, silence);
            juce::AudioBuffer<float> loud (2, 512);
            fillTone (loud, 48000.0, 1000.0, 0.5f, 0, Tone::Centered);
            process (processor, loud);
            return std::pair<double, float> (bufferRms (loud, 0, 0, 512), processor.getDynamicEnvelopeDb (0));
        };
        const auto immediate = burst (0.0f);
        const auto ahead = burst (20.0f);
        std::cout << "  lookahead_now_rms " << immediate.first << " env " << immediate.second
                  << " ahead_rms " << ahead.first << " env " << ahead.second << "\n";
        require (immediate.first > 0.05, "Zero-lookahead output stayed silent on the loud block");
        require (ahead.first < 0.02, "Lookahead output did not stay on the delayed silence");
        require (ahead.second > -18.0f, "Lookahead detector did not see the loud input early");
    }

    {
        auto linearImpulse = [] (FilterType type, bool dynamic)
        {
            OpenEQAudioProcessor processor;
            silenceBands (processor);
            configureBand (processor, 0, type, 1000.0f, 12.0f, 0.5f, 1, (int) StereoMode::Stereo, true);
            if (dynamic)
                setRaw (processor.apvts, Params::bandDynOnID (0), 1.0f);
            setRaw (processor.apvts, "processingMode", 1.0f);
            setRaw (processor.apvts, "linearPhaseQuality", 0.0f);
            prepareStereo (processor, 48000.0, 512);
            juce::AudioBuffer<float> warmup (2, 256);
            warmup.clear();
            process (processor, warmup);
            const auto started = processor.getLinearPhaseDesigns();
            const auto deadline = juce::Time::getMillisecondCounter() + 1500;
            while (processor.getLinearPhaseDesigns() == started && juce::Time::getMillisecondCounter() < deadline)
                juce::Thread::sleep (10);
            flushLinearPhase (processor);
            juce::AudioBuffer<float> impulse (2, 4096);
            impulse.clear();
            impulse.setSample (0, 0, 1.0f);
            process (processor, impulse);
            const int peak = peakIndex (impulse.getReadPointer (0), 4096);
            return std::tuple<float, bool, bool> (std::abs (impulse.getSample (0, peak)),
                                                   processor.linearPhaseOmitsDynamics(),
                                                   processor.linearPhaseOmitsAllPass());
        };
        const auto dynamic = linearImpulse (FilterType::Bell, true);
        const auto allpass = linearImpulse (FilterType::AllPass, false);
        std::cout << "  lp_dynamic_peak " << std::get<0> (dynamic) << " omits " << std::get<1> (dynamic)
                  << " lp_allpass_peak " << std::get<0> (allpass) << " omits " << std::get<2> (allpass) << "\n";
        require (std::get<0> (dynamic) < 1.05f && std::get<1> (dynamic), "Linear phase still applied a dynamic bell");
        require (std::get<0> (allpass) < 1.05f && std::get<2> (allpass), "Linear phase still included an all-pass");
    }

    {
        SpectralEngine spectral;
        spectral.prepare (48000.0);
        constexpr int length = 8192;
        auto render = [&] (float amount, double frequency)
        {
            spectral.reset();
            std::vector<float> left ((size_t) length), right ((size_t) length, 0.0f);
            for (int i = 0; i < length; ++i)
                left[(size_t) i] = 0.4f * std::sin (juce::MathConstants<float>::twoPi * (float) frequency * (float) i / 48000.0f);
            spectral.process (left.data(), right.data(), length, amount, -30.0f, 3000.0f, 1.0f);
            return rmsRange (left.data(), 2000, 4000);
        };
        const double open = render (0.0f, 3000.0);
        const double cut = render (1.0f, 3000.0);
        const double outsideOpen = render (0.0f, 200.0);
        const double outsideCut = render (1.0f, 200.0);
        std::cout << "  spectral_open_rms " << open << " cut_rms " << cut
                  << " outside_open " << outsideOpen << " outside_cut " << outsideCut << "\n";
        require (cut < open * 0.6, "Spectral suppression did not turn the selected region down");
        require (outsideCut > outsideOpen * 0.7, "Spectral suppression turned down a tone outside the region");
    }

    {
        OpenEQAudioProcessor processor;
        const int id = processor.getInstanceId();
        InstanceRegistry::get().publish (id, 440.0f, -12.0f);
        bool found = false;
        for (const auto& entry : InstanceRegistry::get().snapshot())
            if (entry.id == id && std::abs (entry.peakHz - 440.0f) < 0.1f && std::abs (entry.peakDb + 12.0f) < 0.1f)
                found = true;
        std::cout << "  instance_publish " << (found ? 1 : 0) << " id " << id << "\n";
        require (found, "Instance registry did not keep the published peak");
    }

    {
        OpenEQAudioProcessor processor;
        EQCurveComponent curve (processor);
        curve.setSize (900, 420);
        for (int i = 0; i < OpenEQAudioProcessor::numBands; ++i)
        {
            setRaw (processor.apvts, Params::bandShownID (i), 0.0f);
            setRaw (processor.apvts, Params::bandActiveID (i), 0.0f);
        }
        curve.setGrabArmed (true);
        curve.peakFrequencyForGrab = [] (float) { return 1500.0f; };
        const float y = EQCurveComponent::dbToY (6.0f, 420.0f);
        curve.mouseDown (mouseAt (curve, 200.0f, y, {}));
        curve.mouseDrag (mouseAt (curve, 200.0f, y, {}));
        curve.mouseUp (mouseAt (curve, 200.0f, y, {}));
        int grabbed = -1;
        for (int i = 0; i < OpenEQAudioProcessor::numBands; ++i)
            if (rawValue (processor, Params::bandActiveID (i)) > 0.5f)
                grabbed = i;
        require (grabbed >= 0, "Spectrum grab did not create a band");
        std::cout << "  grab_band " << grabbed
                  << " hz " << rawValue (processor, Params::bandFreqID (grabbed))
                  << " db " << rawValue (processor, Params::bandGainID (grabbed)) << "\n";
        require (std::abs (rawValue (processor, Params::bandFreqID (grabbed)) - 1500.0f) < 1.0f, "Spectrum grab missed the peak frequency");
        require (std::abs (rawValue (processor, Params::bandGainID (grabbed)) - 6.0f) < 0.4f, "Spectrum grab missed the dragged gain");
        require (processor.undo(), "Spectrum grab was not one undo step");
        require (rawValue (processor, Params::bandActiveID (grabbed)) < 0.5f, "Undo did not remove the grabbed band");
    }

    {
        OpenEQAudioProcessor processor;
        EQCurveComponent curve (processor);
        curve.setSize (900, 420);
        setRaw (processor.apvts, Params::bandFreqID (0), 2222.0f);
        setRaw (processor.apvts, Params::bandGainID (0), 4.0f);
        curve.keyPressed (juce::KeyPress ('d', juce::ModifierKeys::commandModifier, 0));
        int copy = -1;
        for (int i = 1; i < OpenEQAudioProcessor::numBands; ++i)
            if (std::abs (rawValue (processor, Params::bandFreqID (i)) - 2222.0f) < 1.0f
                && rawValue (processor, Params::bandActiveID (i)) > 0.5f)
                copy = i;
        std::cout << "  duplicate_slot " << copy << "\n";
        require (copy >= 0, "Duplicate did not create a copy of the selected band");
        require (processor.undo(), "Duplicate was not one undo step");
    }

    {
        OpenEQAudioProcessor processor;
        processor.setDisplayTiltDbPerOctave (4.5f);
        processor.setSpectrumSource (2);
        juce::MemoryBlock state;
        processor.getStateInformation (state);
        OpenEQAudioProcessor restored;
        restored.setStateInformation (state.getData(), (int) state.getSize());
        std::cout << "  restored_tilt " << restored.getDisplayTiltDbPerOctave()
                  << " restored_source " << restored.getSpectrumSource() << "\n";
        require (std::abs (restored.getDisplayTiltDbPerOctave()) < 0.01f, "Display tilt was saved in the session");
        require (restored.getSpectrumSource() == 0, "Analyzer source was saved in the session");
    }

    {
        OpenEQAudioProcessor side;
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add (juce::AudioChannelSet::stereo());
        layout.inputBuses.add (juce::AudioChannelSet::stereo());
        layout.outputBuses.add (juce::AudioChannelSet::stereo());
        require (side.setBusesLayout (layout), "Processor rejected a sidechain layout for the analyzer");
        silenceBands (side);
        prepareStereo (side, 48000.0, 512);
        juce::AudioBuffer<float> buffer (4, AnalyzerRing::size);
        buffer.clear();
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const float sample = 0.5f * std::sin (juce::MathConstants<float>::twoPi * 1000.0f * (float) i / 48000.0f);
            buffer.setSample (2, i, sample);
            buffer.setSample (3, i, sample);
        }
        process (side, buffer);
        std::array<float, AnalyzerRing::size> sideLatest {};
        std::array<float, AnalyzerRing::size> preLatest {};
        side.sideL.copyLatest (sideLatest);
        side.preL.copyLatest (preLatest);
        double sideEnergy = 0.0;
        double preEnergy = 0.0;
        for (int i = 0; i < AnalyzerRing::size; ++i)
        {
            sideEnergy += (double) sideLatest[(size_t) i] * (double) sideLatest[(size_t) i];
            preEnergy += (double) preLatest[(size_t) i] * (double) preLatest[(size_t) i];
        }
        std::cout << "  side_spectrum_energy " << sideEnergy << " pre_energy " << preEnergy << "\n";
        require (sideEnergy > 10.0, "Sidechain spectrum ring stayed empty");
        require (sideEnergy > preEnergy * 20.0, "Sidechain spectrum followed the silent main input");
    }

    {
        OpenEQAudioProcessor processor;
        silenceBands (processor);
        configureBand (processor, 0, FilterType::Bell, 1000.0f, 6.0f, 1.0f, 1, (int) StereoMode::Stereo, true);
        prepareStereo (processor, 48000.0, 512);
        juce::AudioBuffer<float> tiny (2, 512);
        fillTone (tiny, 48000.0, 1000.0, 1.0e-40f, 0, Tone::Centered);
        process (processor, tiny);
        float peak = 0.0f;
        for (int i = 0; i < 512; ++i)
            peak = std::max (peak, std::abs (tiny.getSample (0, i)));
        std::cout << "  denormal_peak " << peak << "\n";
        require (allFinite (tiny), "Denormal input produced a non-finite sample");
        require (peak < 1.0e-20f, "Denormal input grew above the silence floor");
    }

    {
        auto render = [] (bool asDouble)
        {
            OpenEQAudioProcessor processor;
            silenceBands (processor);
            configureBand (processor, 0, FilterType::Bell, 1000.0f, 6.0f, 1.0f, 1, (int) StereoMode::Stereo, true);
            prepareStereo (processor, 48000.0, 512);
            std::vector<float> tail (512);
            if (asDouble)
            {
                juce::AudioBuffer<double> buffer (2, 2048);
                for (int i = 0; i < 2048; ++i)
                {
                    const double sample = 0.2 * std::sin (juce::MathConstants<double>::twoPi * 1000.0 * i / 48000.0);
                    buffer.setSample (0, i, sample);
                    buffer.setSample (1, i, sample);
                }
                juce::MidiBuffer midi;
                processor.processBlock (buffer, midi);
                for (int i = 0; i < 512; ++i)
                    tail[(size_t) i] = (float) buffer.getSample (0, 1536 + i);
            }
            else
            {
                juce::AudioBuffer<float> buffer (2, 2048);
                fillTone (buffer, 48000.0, 1000.0, 0.2f, 0, Tone::Centered);
                process (processor, buffer);
                for (int i = 0; i < 512; ++i)
                    tail[(size_t) i] = buffer.getSample (0, 1536 + i);
            }
            return tail;
        };
        const auto floats = render (false);
        const auto doubles = render (true);
        float worst = 0.0f;
        for (int i = 0; i < 512; ++i)
            worst = std::max (worst, std::abs (floats[(size_t) i] - doubles[(size_t) i]));
        std::cout << "  double_float_error " << worst << "\n";
        require (worst < 1.0e-4f, "Double conversion diverged from the float path");
    }

    {
        ResponseModel::GraphSnapshot snapshot;
        snapshot.sampleRate = 48000.0;
        snapshot.bands[0].audible = true;
        snapshot.bands[0].type = FilterType::Bell;
        snapshot.bands[0].stereoMode = StereoMode::Left;
        snapshot.bands[0].stageCount = 1;
        const auto designed = ResponseModel::designBand (FilterType::Bell, 1000.0f, 12.0f, 1.0f, 12, 48000.0);
        snapshot.bands[0].stages[0] = ResponseModel::normalizeBiquad (designed.raw[0]);
        const auto matrix = ResponseModel::responseMatrix (snapshot, 1000.0);
        std::cout << "  mixed_ll_db " << juce::Decibels::gainToDecibels (std::abs (matrix.ll))
                  << " mixed_rr_db " << juce::Decibels::gainToDecibels (std::abs (matrix.rr)) << "\n";
        require (ResponseModel::hasMixedRouting (snapshot), "A left band was not mixed routing");
        require (std::abs (matrix.ll) > std::abs (matrix.rr) + 0.5, "Left curve did not exceed the right curve");
    }

    std::cout << "PASS: remaining plan coverage\n";
}

static void checkLinearPhaseCrossfade()
{
    auto maxStep = [] (const float* data, int length)
    {
        float worst = 0.0f;
        for (int i = 0; i + 1 < length; ++i)
            worst = std::max (worst, std::abs (data[i + 1] - data[i]));
        return worst;
    };

    OpenEQAudioProcessor processor;
    silenceBands (processor);
    configureBand (processor, 0, FilterType::Bell, 1000.0f, 0.0f, 1.0f, 1, (int) StereoMode::Stereo, true);
    setRaw (processor.apvts, "processingMode", 1.0f);
    setRaw (processor.apvts, "linearPhaseQuality", 0.0f);
    prepareStereo (processor, 48000.0, 512);

    int phase = 0;
    auto render = [&] (int samples)
    {
        juce::AudioBuffer<float> tone (2, samples);
        fillTone (tone, 48000.0, 1000.0, 0.2f, phase, Tone::Centered);
        phase += samples;
        process (processor, tone);
        require (allFinite (tone), "Linear-phase crossfade produced a non-finite sample");
        return tone;
    };

    auto settled = render (16384);
    const double quietRms = bufferRms (settled, 0, 14336, 2048);
    const float quietStep = maxStep (settled.getReadPointer (0) + 14336, 2048);
    float previous = settled.getSample (0, settled.getNumSamples() - 1);

    auto includeBlock = [&] (const juce::AudioBuffer<float>& block, float& worst)
    {
        worst = std::max (worst, std::abs (block.getSample (0, 0) - previous));
        worst = std::max (worst, maxStep (block.getReadPointer (0), block.getNumSamples()));
        previous = block.getSample (0, block.getNumSamples() - 1);
    };

    const auto designsBefore = processor.getLinearPhaseDesigns();
    setRaw (processor.apvts, Params::bandGainID (0), 12.0f);
    float transition = quietStep;
    bool published = false;
    const auto deadline = juce::Time::getMillisecondCounter() + 2000;
    while (! published && juce::Time::getMillisecondCounter() < deadline)
    {
        auto block = render (512);
        includeBlock (block, transition);
        published = processor.getLinearPhaseDesigns() != designsBefore;
    }
    auto handoff = render (2048 + 512 + 1024);
    includeBlock (handoff, transition);
    auto loud = render (4096);
    includeBlock (loud, transition);
    const double loudRms = bufferRms (loud, 0, 2048, 2048);
    const float loudStep = maxStep (loud.getReadPointer (0) + 2048, 2047);
    std::cout << "  lp_crossfade_quiet_step " << quietStep
              << " transition_step " << transition
              << " loud_step " << loudStep
              << " quiet_rms " << quietRms
              << " loud_rms " << loudRms << "\n";
    require (published, "Linear-phase edit did not publish a kernel");
    require (loudRms > quietRms * 2.0, "Linear-phase edit did not reach the boosted tone");
    require (transition < std::max (0.05f, loudStep * 3.0f), "Linear-phase edit stepped harder than its settled tone");

    const auto qualityBefore = processor.getLinearPhaseDesigns();
    setRaw (processor.apvts, "linearPhaseQuality", 1.0f);
    float qualityStep = 0.0f;
    const auto qualityDeadline = juce::Time::getMillisecondCounter() + 2000;
    while (juce::Time::getMillisecondCounter() < qualityDeadline)
    {
        auto block = render (512);
        includeBlock (block, qualityStep);
        if (processor.getReportedLatencySamples() == 2048 && processor.getLinearPhaseDesigns() != qualityBefore)
            break;
    }
    std::cout << "  lp_quality_switch_step " << qualityStep
              << " latency " << processor.getReportedLatencySamples() << "\n";
    require (processor.getReportedLatencySamples() == 2048, "Quality change did not report the longer latency");

    std::cout << "PASS: linear-phase same-length edits crossfade\n";
}

static void checkTransientDetector()
{
    auto onsetRms = [] (float weight)
    {
        OpenEQAudioProcessor processor;
        silenceBands (processor);
        configureBand (processor, 0, FilterType::Bell, 1000.0f, 0.0f, 1.0f, 1, (int) StereoMode::Stereo, true);
        setRaw (processor.apvts, Params::bandDynOnID (0), 1.0f);
        setRaw (processor.apvts, Params::bandDynThreshID (0), -30.0f);
        setRaw (processor.apvts, Params::bandDynRangeID (0), -12.0f);
        setRaw (processor.apvts, Params::bandDynAttackID (0), 80.0f);
        setRaw (processor.apvts, Params::bandDynReleaseID (0), 200.0f);
        setRaw (processor.apvts, Params::bandDynTransID (0), weight);
        prepareStereo (processor, 48000.0, 512);
        juce::AudioBuffer<float> silence (2, 2048);
        silence.clear();
        process (processor, silence);
        juce::AudioBuffer<float> loud (2, 512);
        fillTone (loud, 48000.0, 1000.0, 0.5f, 0, Tone::Centered);
        process (processor, loud);
        require (allFinite (loud), "Transient-weight onset produced a non-finite sample");
        return bufferRms (loud, 0, 0, 512);
    };
    const double slowOnset = onsetRms (0.0f);
    const double fastOnset = onsetRms (100.0f);

    auto settledRms = [] (float weight)
    {
        OpenEQAudioProcessor processor;
        silenceBands (processor);
        configureBand (processor, 0, FilterType::Bell, 1000.0f, 0.0f, 1.0f, 1, (int) StereoMode::Stereo, true);
        setRaw (processor.apvts, Params::bandDynOnID (0), 1.0f);
        setRaw (processor.apvts, Params::bandDynThreshID (0), -30.0f);
        setRaw (processor.apvts, Params::bandDynRangeID (0), -12.0f);
        setRaw (processor.apvts, Params::bandDynAttackID (0), 80.0f);
        setRaw (processor.apvts, Params::bandDynReleaseID (0), 200.0f);
        setRaw (processor.apvts, Params::bandDynTransID (0), weight);
        prepareStereo (processor, 48000.0, 512);
        juce::AudioBuffer<float> tone (2, 16384);
        fillTone (tone, 48000.0, 1000.0, 0.5f, 0, Tone::Centered);
        process (processor, tone);
        require (allFinite (tone), "Transient-weight tail produced a non-finite sample");
        return bufferRms (tone, 0, 14336, 2048);
    };
    const double slowTail = settledRms (0.0f);
    const double fastTail = settledRms (100.0f);
    std::cout << "  transient_onset_0 " << slowOnset << " onset_100 " << fastOnset
              << " tail_0 " << slowTail << " tail_100 " << fastTail << "\n";
    require (fastOnset < slowOnset * 0.75, "Full transient weight did not cut the onset harder");
    require (slowTail < 0.16, "Zero transient weight did not settle into the cut");
    require (std::abs (fastTail - slowTail) < slowTail * 0.05, "Settled transient weight left the clamped cut");

    auto autoTail = [] (bool automatic)
    {
        OpenEQAudioProcessor processor;
        silenceBands (processor);
        configureBand (processor, 0, FilterType::Bell, 1000.0f, 0.0f, 1.0f, 1, (int) StereoMode::Stereo, true);
        setRaw (processor.apvts, Params::bandDynOnID (0), 1.0f);
        setRaw (processor.apvts, Params::bandDynThreshID (0), -24.0f);
        setRaw (processor.apvts, Params::bandDynRangeID (0), -12.0f);
        setRaw (processor.apvts, Params::bandDynAttackID (0), 1.0f);
        setRaw (processor.apvts, Params::bandDynReleaseID (0), 40.0f);
        setRaw (processor.apvts, Params::bandDynAutoID (0), automatic ? 1.0f : 0.0f);
        prepareStereo (processor, 48000.0, 512);
        juce::AudioBuffer<float> tone (2, 8192);
        fillTone (tone, 48000.0, 1000.0, 0.5f, 0, Tone::Centered);
        process (processor, tone);
        require (allFinite (tone), "Automatic timing produced a non-finite sample");
        return std::tuple<double, float, float> (bufferRms (tone, 0, 6000, 2000),
                                                  processor.getDynamicGainDb (0),
                                                  processor.getDynamicThresholdDb (0));
    };
    const auto manual = autoTail (false);
    const auto automatic = autoTail (true);
    std::cout << "  auto_off_rms " << std::get<0> (manual) << " gain " << std::get<1> (manual)
              << " threshold " << std::get<2> (manual)
              << " auto_on_rms " << std::get<0> (automatic) << " gain " << std::get<1> (automatic)
              << " threshold " << std::get<2> (automatic) << "\n";
    require (std::get<0> (manual) < 0.15, "Manual threshold did not hold the loud tone down");
    require (std::get<1> (manual) < -8.0f, "Manual cut did not publish its gain offset");
    require (std::abs (std::get<2> (manual) - (-24.0f)) < 0.2f, "Manual threshold was replaced");
    require (std::get<0> (automatic) > std::get<0> (manual) * 2.0, "Automatic threshold kept a settled tone in a deep cut");
    require (std::get<2> (automatic) > -12.0f, "Automatic threshold did not rise with the broadband level");

    {
        OpenEQAudioProcessor processor;
        silenceBands (processor);
        configureBand (processor, 0, FilterType::Bell, 1000.0f, 0.0f, 1.0f, 1, (int) StereoMode::Stereo, true);
        setRaw (processor.apvts, Params::bandDynOnID (0), 1.0f);
        setRaw (processor.apvts, Params::bandDynRangeID (0), -12.0f);
        setRaw (processor.apvts, Params::bandDynAutoID (0), 1.0f);
        prepareStereo (processor, 48000.0, 512);
        juce::AudioBuffer<float> silence (2, 2048);
        silence.clear();
        process (processor, silence);
        juce::AudioBuffer<float> loud (2, 2048);
        fillTone (loud, 48000.0, 1000.0, 0.5f, 0, Tone::Centered);
        process (processor, loud);
        const double burst = bufferRms (loud, 0, 0, 2048);
        std::cout << "  auto_burst_rms " << burst << "\n";
        require (allFinite (loud), "Automatic burst produced a non-finite sample");
        require (burst < 0.22, "Automatic timing did not reduce a burst that starts from silence");
        require (burst > 0.02, "Automatic timing silenced the burst");
    }

    {
        OpenEQAudioProcessor processor;
        silenceBands (processor);
        configureBand (processor, 0, FilterType::Bell, 1000.0f, 0.0f, 1.0f, 1, (int) StereoMode::Stereo, true);
        prepareStereo (processor, 48000.0, 512);
        processor.setDetectorAudition (true, 0);
        auto render = [&] (double frequency)
        {
            juce::AudioBuffer<float> tone (2, 4096);
            fillTone (tone, 48000.0, frequency, 0.25f, 0, Tone::Centered);
            process (processor, tone);
            require (allFinite (tone), "Detector audition produced a non-finite sample");
            return bufferRms (tone, 0, 2048, 2048);
        };
        const double atBand = render (1000.0);
        const double away = render (100.0);
        std::cout << "  detector_audition_1k_rms " << atBand << " detector_audition_100_rms " << away << "\n";
        require (processor.isDetectorAuditioning(), "Detector audition did not arm");
        require (atBand > away * 4.0, "Detector audition did not isolate the selected range");
        juce::MemoryBlock state;
        processor.getStateInformation (state);
        processor.setStateInformation (state.getData(), (int) state.getSize());
        require (! processor.isDetectorAuditioning(), "Restoring state kept detector audition armed");
    }

    auto detectorCase = [] (bool unlink, float detectorHz, float detectorQ, float bandHz)
    {
        OpenEQAudioProcessor processor;
        silenceBands (processor);
        configureBand (processor, 0, FilterType::Bell, bandHz, 0.0f, 1.0f, 1, (int) StereoMode::Stereo, true);
        setRaw (processor.apvts, Params::bandDynOnID (0), 1.0f);
        setRaw (processor.apvts, Params::bandDynThreshID (0), -30.0f);
        setRaw (processor.apvts, Params::bandDynRangeID (0), -12.0f);
        setRaw (processor.apvts, Params::bandDynAttackID (0), 1.0f);
        setRaw (processor.apvts, Params::bandDynReleaseID (0), 40.0f);
        if (unlink)
        {
            setRaw (processor.apvts, Params::bandDynDetLinkID (0), 0.0f);
            setRaw (processor.apvts, Params::bandDynDetFreqID (0), detectorHz);
            setRaw (processor.apvts, Params::bandDynDetQID (0), detectorQ);
        }
        prepareStereo (processor, 48000.0, 512);
        juce::AudioBuffer<float> tone (2, 8192);
        fillTone (tone, 48000.0, 1000.0, 0.5f, 0, Tone::Centered);
        process (processor, tone);
        require (allFinite (tone), "Independent detector produced a non-finite sample");
        return std::tuple<double, float, float> (bufferRms (tone, 0, 6000, 2000),
                                                  processor.getDynamicEnvelopeDb (0),
                                                  processor.getDynamicGainDb (0));
    };
    {
        OpenEQAudioProcessor defaults;
        require (rawValue (defaults, Params::bandDynDetLinkID (0)) > 0.5f, "Detector link does not default on");
    }
    const auto linked = detectorCase (false, 1000.0f, 1.0f, 1000.0f);
    const auto detuned = detectorCase (true, 80.0f, 16.0f, 1000.0f);
    const auto retargeted = detectorCase (true, 1000.0f, 1.0f, 200.0f);
    std::cout << "  detector_linked_rms " << std::get<0> (linked) << " env " << std::get<1> (linked)
              << " gain " << std::get<2> (linked)
              << " detuned_rms " << std::get<0> (detuned) << " env " << std::get<1> (detuned)
              << " retarget_rms " << std::get<0> (retargeted) << " env " << std::get<1> (retargeted)
              << " gain " << std::get<2> (retargeted) << "\n";
    require (std::get<1> (linked) > -18.0f, "Linked detector missed a tone on its band");
    require (std::get<2> (linked) < -8.0f, "Linked detector did not apply the cut");
    require (std::get<1> (detuned) < -40.0f, "Detuned detector still followed the band frequency");
    require (std::get<0> (detuned) > 0.25, "Detuned detector still cut the band");
    require (std::abs (std::get<2> (detuned)) < 1.0f, "A cold detector still published a gain change");
    require (std::get<1> (retargeted) > -18.0f, "Independent detector missed its own frequency");
    require (std::get<2> (retargeted) < -8.0f, "Independent detector did not cut from its own frequency");

    {
        OpenEQAudioProcessor processor;
        silenceBands (processor);
        configureBand (processor, 0, FilterType::Bell, 200.0f, 0.0f, 1.0f, 1, (int) StereoMode::Stereo, true);
        setRaw (processor.apvts, Params::bandDynDetLinkID (0), 0.0f);
        setRaw (processor.apvts, Params::bandDynDetFreqID (0), 1000.0f);
        setRaw (processor.apvts, Params::bandDynDetQID (0), 1.0f);
        prepareStereo (processor, 48000.0, 512);
        processor.setDetectorAudition (true, 0);
        auto render = [&] (double frequency)
        {
            juce::AudioBuffer<float> tone (2, 4096);
            fillTone (tone, 48000.0, frequency, 0.25f, 0, Tone::Centered);
            process (processor, tone);
            return bufferRms (tone, 0, 2048, 2048);
        };
        const double atDetector = render (1000.0);
        const double away = render (100.0);
        std::cout << "  detector_listen_own_1k_rms " << atDetector << " own_100_rms " << away << "\n";
        require (atDetector > 0.05, "Detector listen followed the band frequency instead of DFREQ");
        require (atDetector > away * 4.0, "Detector listen did not isolate DFREQ");
    }

    {
        OpenEQAudioProcessor processor;
        silenceBands (processor);
        configureBand (processor, 0, FilterType::Bell, 1000.0f, 0.0f, 1.0f, 1, (int) StereoMode::Stereo, true);
        setRaw (processor.apvts, Params::bandDynOnID (0), 1.0f);
        setRaw (processor.apvts, Params::bandDynThreshID (0), -30.0f);
        setRaw (processor.apvts, Params::bandDynRangeID (0), -12.0f);
        setRaw (processor.apvts, Params::bandDynAttackID (0), 1.0f);
        prepareStereo (processor, 48000.0, 512);
        juce::AudioBuffer<float> tone (2, 8192);
        fillTone (tone, 48000.0, 1000.0, 0.5f, 0, Tone::Centered);
        process (processor, tone);
        AppLookAndFeel panelLookAndFeel;
        DynamicsPanel panel (processor);
        panel.setLookAndFeel (&panelLookAndFeel);
        panel.setBounds (0, 0, 828, 212);
        panel.setSelectedBand (0);
        // The floating inspector header owns the single readout; the panel supplies its live values.
        const auto readout = panel.dynamicsSummary();
        std::cout << "  dynamics_readout " << readout << "\n";
        require (readout.contains ("T -30"), "Controller readout missed the threshold");
        require (readout.contains ("G -"), "Controller readout missed the applied cut");

        if (std::getenv ("OPENEQ_UI_CAPTURE_DIR") != nullptr)
        {
            setRaw (processor.apvts, Params::bandDynBelowOnID (0), 1.0f);
            setRaw (processor.apvts, Params::bandDynAboveOnID (0), 1.0f);
            setRaw (processor.apvts, Params::bandDynBelowRatioID (0), 50.0f); // 2:1
            setRaw (processor.apvts, Params::bandDynAboveRatioID (0), 75.0f); // 4:1
            setRaw (processor.apvts, Params::bandDynDetLinkID (0), 0.0f);
            setRaw (processor.apvts, Params::bandDynRelFreeID (0), 1.0f);
            panel.setSelectedBand (0);
        }

        // Every visible card must retain its controls inside the card without intersecting siblings.
        for (const auto view : { DynamicsPanel::DynView::Level, DynamicsPanel::DynView::Timing,
                                 DynamicsPanel::DynView::Detector })
        {
            panel.setDynView (view);
            for (auto* child : panel.getChildren())
                if (auto* card = dynamic_cast<DynamicsCard*> (child); card != nullptr && card->isVisible())
                    for (auto* control : card->getChildren())
                    {
                        require (! control->getBounds().isEmpty(), "Dynamics control has no bounds");
                        require (card->getLocalBounds().contains (control->getBounds()), "Dynamics control escapes its card");
                        for (auto* other : card->getChildren())
                            if (control != other)
                                require (! control->getBounds().intersects (other->getBounds()), "Dynamics controls overlap");
                    }
            if (const auto* capturePath = std::getenv ("OPENEQ_UI_CAPTURE_DIR"))
            {
                const juce::File directory (juce::String::fromUTF8 (capturePath));
                require (directory.createDirectory().wasOk(), "Could not create UI capture directory");
                static const char* names[] { "level", "timing", "detector" };
                const auto file = directory.getChildFile (juce::String (names[(int) view]) + ".png");
                auto stream = file.createOutputStream();
                require (stream != nullptr && stream->openedOk(), "Could not open dynamics screenshot");
                stream->setPosition (0);
                stream->truncate();
                const auto snapshot = panel.createComponentSnapshot (panel.getLocalBounds(), true, 1.5f);
                juce::Image preview (juce::Image::RGB, snapshot.getWidth(), snapshot.getHeight(), true);
                {
                    juce::Graphics graphics (preview);
                    graphics.fillAll (Theme::editorBottom);
                    graphics.drawImageAt (snapshot, 0, 0);
                }
                juce::PNGImageFormat png;
                require (png.writeImageToStream (preview, *stream),
                         "Could not write dynamics screenshot");
            }
        }

        EQCurveComponent curve (processor);
        curve.setBounds (0, 0, 900, 420);
        juce::Image image (juce::Image::ARGB, 900, 420, true);
        {
            juce::Graphics graphics (image);
            curve.paint (graphics);
        }
        int ink = 0;
        for (int y = 0; y < image.getHeight(); y += 4)
            for (int x = 0; x < image.getWidth(); x += 4)
                if (image.getPixelAt (x, y).getAlpha() > 0)
                    ++ink;
        std::cout << "  dynamic_graph_ink " << ink << "\n";
        require (ink > 20, "Dynamic graph readout did not paint");
    }

    std::cout << "PASS: transient weight, automatic timing, and detector frequency\n";
}

static void checkBrickwallAndTypes()
{
    require (filterTypeCount == 48, "filterTypeCount should be 48");
    require (Params::filterTypeNames.size() == filterTypeCount, "type name list does not match filterTypeCount");
    require ((int) FilterType::NarrowResonance == 12, "original type indices must not move");
    require ((int) FilterType::BrickwallLowCut == 13 && (int) FilterType::BrickwallHighCut == 14,
             "brickwall types must be appended after the original set");

    // Top-level types have no submenu. Every family type must sit in one.
    for (int i = 17; i < filterTypeCount; ++i)
        requireMsg (! Params::filterTypeGroup (i).isEmpty(), "family type " + std::to_string (i) + " has no menu group");
    require (Params::filterTypeGroup (8) == "Misc" && Params::filterTypeGroup (16) == "Misc", "allpass types belong in Misc");

    require (ResponseModel::isBrickwall (FilterType::BrickwallLowCut)
             && ResponseModel::isBrickwall (FilterType::BrickwallHighCut), "brickwall types not recognised");
    require (! ResponseModel::usesSlope (FilterType::BrickwallLowCut), "brickwall slope must be fixed");
    require (! ResponseModel::usesQ (FilterType::BrickwallHighCut), "brickwall has no Q control");
    require (! ResponseModel::usesGain (FilterType::BrickwallLowCut), "brickwall has no gain");
    require (ResponseModel::supportedInLinearPhase (FilterType::BrickwallLowCut), "brickwall should work in linear phase");

    // Fitted to reference wall (fits/wall_joint_fit.json, measured with response_probe): the -3 dB crossing sits
    // just above the commanded frequency, the knee is extremely sharp, and the stopband is below -120 dB.
    const double sampleRate = 48000.0;
    const auto low = ResponseModel::designBand (FilterType::BrickwallLowCut, 1000.0f, 0.0f, 1.0f, 12, sampleRate);
    require (low.stageCount == 15, "brickwall should use 15 biquad sections regardless of the slope argument");
    const auto lowDb = [&] (double f) { return juce::Decibels::gainToDecibels (ResponseModel::designedMagnitude (low, f, sampleRate), -150.0); };
    const double cutoffDb = lowDb (1000.0);
    requireMsg (cutoffDb > -2.0 && cutoffDb < 2.0, "brickwall LC should be near unity at the commanded frequency: " + std::to_string (cutoffDb));
    requireMsg (lowDb (976.0) < -0.5 && lowDb (976.0) > -4.0, "brickwall LC knee shape off just below the cutoff (measured -2.3 dB at 976 Hz): " + std::to_string (lowDb (976.0)));
    requireMsg (lowDb (2000.0) > -1.0, "brickwall LC passband dip too deep (measured flat above the cutoff): " + std::to_string (lowDb (2000.0)));
    requireMsg (lowDb (500.0) < -120.0, "brickwall LC stopband too shallow: " + std::to_string (lowDb (500.0)));

    const auto high = ResponseModel::designBand (FilterType::BrickwallHighCut, 1000.0f, 0.0f, 1.0f, 12, sampleRate);
    const auto highDb = [&] (double f) { return juce::Decibels::gainToDecibels (ResponseModel::designedMagnitude (high, f, sampleRate), -150.0); };
    requireMsg (highDb (500.0) > -0.5, "brickwall HC passband ripple too high: " + std::to_string (highDb (500.0)));
    requireMsg (highDb (2000.0) < -120.0, "brickwall HC stopband too shallow: " + std::to_string (highDb (2000.0)));

    // The processor reports the fixed slope for brickwall bands regardless of the slope control.
    OpenEQAudioProcessor processor;
    prepareStereo (processor, sampleRate, 512);
    configureBand (processor, 2, FilterType::BrickwallHighCut, 5000.0f, 0.0f, 1.0f, 0, (int) StereoMode::Stereo, true);
    const auto state = processor.readBandParameter (2);
    require (state.type == FilterType::BrickwallHighCut, "brickwall type not read by processor");
    require (state.slopeDb == ResponseModel::brickwallSlopeDb, "processor should report a 180 dB/oct brickwall slope");

    std::cout << "  brickwall_lc_3db " << cutoffDb << " lc_500 " << lowDb (500.0) << " hc_2k " << highDb (2000.0) << "\n";
    std::cout << "PASS: brickwall cuts, fixed slope, and append-only type indices\n";
}

static double magnitudeDb (const ResponseModel::DesignedBand& designed, double frequency, double sampleRate)
{
    return juce::Decibels::gainToDecibels (ResponseModel::designedMagnitude (designed, frequency, sampleRate));
}

static double wrappedPhase (const ResponseModel::DesignedBand& designed, double frequency, double sampleRate)
{
    double phase = 0.0;
    for (int i = 0; i < designed.stageCount; ++i)
        phase += ResponseModel::stagePhase (ResponseModel::normalizeBiquad (designed.raw[(size_t) i]), frequency, sampleRate);
    return std::atan2 (std::sin (phase), std::cos (phase));
}

static void checkFilterTypes()
{
    const double sampleRate = 48000.0;

    // Every type designs to finite, bounded stages across a range of settings.
    for (int type = 0; type < filterTypeCount; ++type)
    {
        for (float gain : { -12.0f, 0.0f, 12.0f })
        {
            const auto designed = ResponseModel::designBand ((FilterType) type, 1000.0f, gain, 1.5f, 24, sampleRate);
            requireMsg (designed.stageCount >= 1 && designed.stageCount <= ResponseModel::maxStages,
                        "type " + std::to_string (type) + " has an invalid stage count");
            for (double f : { 20.0, 200.0, 1000.0, 8000.0, 20000.0 })
            {
                const double mag = ResponseModel::designedMagnitude (designed, f, sampleRate);
                requireMsg (std::isfinite (mag) && mag >= 0.0, "type " + std::to_string (type) + " gave a non-finite magnitude");
            }
        }
    }

    // Control rules follow the base shape and the family's fixed settings.
    require (! ResponseModel::usesQ (FilterType::ELowBell), "E Low Bell has a fixed Q");
    require (! ResponseModel::usesQ (FilterType::EHighBell), "E High Bell has a fixed Q");
    require (! ResponseModel::usesQ (FilterType::Eq250LowShelf), "250 LS has a fixed Q");
    require (ResponseModel::usesQ (FilterType::Sword), "Sword has a Q control");
    require (ResponseModel::usesGain (FilterType::VtBoost), "VT Boost has gain");
    // reference E HP is a fixed two-section high pass: its slope control does nothing, so OpenEQ shows none.
    require (! ResponseModel::usesSlope (FilterType::EHighPass), "E HP has a fixed slope");
    require (! ResponseModel::usesSlope (FilterType::BrickwallLowCut), "brickwall keeps a fixed slope");
    require (ResponseModel::isAllPass (FilterType::AllPassHigh), "Allpass H is an all-pass");
    require (! ResponseModel::supportedInLinearPhase (FilterType::AllPassHigh), "Allpass H is not linear-phase capable");
    require (ResponseModel::supportsDynamics (FilterType::VtBoost), "VT Boost should support dynamics");
    require (! ResponseModel::supportsDynamics (FilterType::EHighPass), "cuts do not support dynamics");

    // Gain limits: boost-only types ignore cuts, reduce-only types ignore boosts.
    const auto boost = ResponseModel::designBand (FilterType::VtBoost, 1000.0f, -6.0f, 1.0f, 12, sampleRate);
    requireMsg (std::abs (magnitudeDb (boost, 1000.0, sampleRate)) < 1.0e-3, "VT Boost should not cut");
    const auto atten = ResponseModel::designBand (FilterType::VtAtten, 4000.0f, 6.0f, 1.0f, 12, sampleRate);
    requireMsg (std::abs (magnitudeDb (atten, 10000.0, sampleRate)) < 1.0e-3, "VT Atten should not boost");
    const auto blue = ResponseModel::designBand (FilterType::BlueLow, 200.0f, -6.0f, 1.0f, 12, sampleRate);
    // reference Blue Low (boost shelf) never cuts: below its minimum it keeps a +0.43 dB bump at the band frequency.
    for (double f : { 20.0, 200.0, 2000.0 })
        requireMsg (magnitudeDb (blue, f, sampleRate) > -1.0e-3, "Blue Low should not cut");
    requireMsg (std::abs (magnitudeDb (blue, 200.0, sampleRate) - 0.43) < 0.05, "Blue Low should keep its +0.43 dB minimum bump");
    const auto boostUp = ResponseModel::designBand (FilterType::VtBoost, 1000.0f, 6.0f, 1.0f, 12, sampleRate);
    requireMsg (magnitudeDb (boostUp, 1000.0, sampleRate) > 5.5, "VT Boost should boost");

    // Allpass L anchors the phase at the bottom, Allpass H at the top.
    const auto allpassL = ResponseModel::designBand (FilterType::AllPass, 1000.0f, 0.0f, 1.0f, 12, sampleRate);
    const auto allpassH = ResponseModel::designBand (FilterType::AllPassHigh, 1000.0f, 0.0f, 1.0f, 12, sampleRate);
    for (double f : { 20.0, 1000.0, 15000.0 })
    {
        requireMsg (std::abs (magnitudeDb (allpassH, f, sampleRate)) < 1.0e-3, "Allpass H should have flat magnitude");
    }
    const double lowPhaseAt1k = std::abs (wrappedPhase (allpassL, 1000.0, sampleRate));
    const double highPhaseAt1k = std::abs (wrappedPhase (allpassH, 1000.0, sampleRate));
    requireMsg (lowPhaseAt1k > 1.0, "Allpass L should rotate phase at its centre");
    requireMsg (highPhaseAt1k < 0.2, "Allpass H should leave 1 kHz nearly untouched");
    requireMsg (std::abs (wrappedPhase (allpassH, 22000.0, sampleRate)) > 1.0,
                "Allpass H should rotate phase near the top of the band");

    // Sword is a bell with twice the Q. Family traits must reproduce that exactly.
    const auto sword = ResponseModel::designBand (FilterType::Sword, 1000.0f, 6.0f, 1.0f, 12, sampleRate);
    const auto swordBell = ResponseModel::designBand (FilterType::Bell, 1000.0f, 6.0f, 2.0f, 12, sampleRate);
    for (int k = 0; k < 6; ++k)
        requireMsg (std::abs (sword.raw[0][(size_t) k] - swordBell.raw[0][(size_t) k]) < 1.0e-6f,
                    "Sword should equal a bell with doubled Q");

    // VT LS is the P-type low section: a second-order circuit. Gain is the DC boost (0 to 24 dB), slope the
    // attenuation, which lowers the response at DC.
    {
        const int slope0 = ResponseModel::encodeSlopeNorm (0.0f), slopeHalf = ResponseModel::encodeSlopeNorm (0.5f);
        const auto vtLs = ResponseModel::designBand (FilterType::VtLowShelf, 100.0f, 6.0f, 1.0f, slope0, sampleRate);
        require (vtLs.stageCount == 1, "VT LS should be one second-order section");
        requireMsg (std::abs (magnitudeDb (vtLs, 20.0, sampleRate) - 5.98) < 0.05, "VT LS should boost 6 dB at DC");
        requireMsg (std::abs (magnitudeDb (vtLs, 10000.0, sampleRate)) < 0.05, "VT LS should be flat at the top");
        const auto vtAtt = ResponseModel::designBand (FilterType::VtLowShelf, 100.0f, 6.0f, 1.0f, slopeHalf, sampleRate);
        // Historical response baseline: 4.06 dB at 20 Hz for 100 Hz, +6 dB, slope 0.5.
        requireMsg (std::abs (magnitudeDb (vtAtt, 20.0, sampleRate) - 4.06) < 0.05, "VT LS attenuation should match the plug-in");
        const auto vtCut = ResponseModel::designBand (FilterType::VtLowShelf, 100.0f, -6.0f, 1.0f, slope0, sampleRate);
        requireMsg (std::abs (magnitudeDb (vtCut, 20.0, sampleRate)) < 1.0e-3, "VT LS should not cut with negative gain");
        require (ResponseModel::slopeIsAttenuation (FilterType::VtLowShelf) && ResponseModel::usesSlope (FilterType::VtLowShelf)
                 && ! ResponseModel::usesQ (FilterType::VtLowShelf), "VT LS uses slope as attenuation and has no Q");
    }
    // Tone Low and Tone High are the passive Baxandall network: fourth order, two sections, gain moves the pot.
    {
        const auto toneLow = ResponseModel::designBand (FilterType::ToneLow, 100.0f, 6.0f, 1.0f, 12, sampleRate);
        require (toneLow.stageCount == 2, "Tone Low should be two sections (the Baxandall network)");
        const auto toneHigh = ResponseModel::designBand (FilterType::ToneHigh, 1000.0f, -12.0f, 1.0f, 12, sampleRate);
        require (toneHigh.stageCount == 2, "Tone High should be two sections (the Baxandall network)");
        // Historical response baseline. The Baxandall network interacts: Tone Low +6 dB also lowers the top by 2 dB.
        requireMsg (std::abs (magnitudeDb (toneLow, 20.0, sampleRate) - 5.88) < 0.1
                        && std::abs (magnitudeDb (toneLow, 10000.0, sampleRate) + 2.08) < 0.1,
                    "Tone Low should match the plug-in (5.88 dB at 20 Hz, -2.08 dB at 10 kHz)");
        requireMsg (std::abs (magnitudeDb (toneHigh, 10000.0, sampleRate) + 10.68) < 0.15
                        && std::abs (magnitudeDb (toneHigh, 30.0, sampleRate) - 0.03) < 0.1,
                    "Tone High should match the plug-in (-10.68 dB at 10 kHz, 0.03 dB at 30 Hz)");
        require (! ResponseModel::usesQ (FilterType::ToneLow) && ! ResponseModel::usesQ (FilterType::ToneHigh), "Tone has no Q");
    }

    // The 250 LS is a fitted cascade and ignores the Q control: two Q settings give the same stages.
    const auto fixedA = ResponseModel::designBand (FilterType::Eq250LowShelf, 300.0f, 6.0f, 3.0f, 12, sampleRate);
    const auto fixedB = ResponseModel::designBand (FilterType::Eq250LowShelf, 300.0f, 6.0f, 0.5f, 12, sampleRate);
    require (fixedA.stageCount == 3 && fixedB.stageCount == 3, "250 LS should be a three-stage fitted cascade");
    for (int stage = 0; stage < 3; ++stage)
        for (int k = 0; k < 6; ++k)
            requireMsg (std::abs (fixedA.raw[(size_t) stage][(size_t) k] - fixedB.raw[(size_t) stage][(size_t) k]) < 1.0e-6f,
                        "250 LS should ignore the Q control");

    // The control list the processor exposes matches the type count.
    OpenEQAudioProcessor processor;
    auto* typeParameter = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (Params::bandTypeID (0)));
    require (typeParameter != nullptr, "type parameter missing");
    require (typeParameter->choices.size() == filterTypeCount, "type choices should match filterTypeCount");

    std::cout << "PASS: reference type set: families, gain limits, allpass H, fixed Q, and control rules\n";
}

static void checkBandOperations()
{
    OpenEQAudioProcessor processor;
    prepareStereo (processor, 48000.0, 512);
    silenceBands (processor);

    // Start with three bands at known frequencies.
    configureBand (processor, 0, FilterType::Bell, 800.0f, 6.0f, 1.0f, 1, (int) StereoMode::Left, true);
    configureBand (processor, 1, FilterType::HighShelf, 4000.0f, -3.0f, 1.0f, 1, (int) StereoMode::Stereo, true);
    configureBand (processor, 2, FilterType::Bell, 120.0f, 2.0f, 2.0f, 1, (int) StereoMode::Mid, true);

    // Duplicate lands in the first free slot with the same values, and inverted gain on request.
    const int copy = processor.duplicateBand (0, false);
    require (copy == 3, "duplicate should use slot 3");
    require (std::abs (rawValue (processor, Params::bandFreqID (copy)) - 800.0f) < 1.0e-3f, "duplicate frequency");
    require (std::abs (rawValue (processor, Params::bandGainID (copy)) - 6.0f) < 1.0e-3f, "duplicate gain");
    const int inverted = processor.duplicateBand (1, true);
    require (inverted == 4, "inverted duplicate should use slot 4");
    require (std::abs (rawValue (processor, Params::bandGainID (inverted)) - 3.0f) < 1.0e-3f, "inverted duplicate of -3 dB should be +3 dB");

    // Undo removes the last duplicate.
    require (processor.undo(), "undo should be available after duplicate");
    require (! processor.isBandUsed (4), "undo should remove the inverted duplicate");

    // Split: Left becomes a Right copy in a free slot. Stereo has no opposite, so it returns -1.
    const int split = processor.splitBand (0);
    require (split >= 0, "split of a Left band should succeed");
    require (rawValue (processor, Params::bandStereoID (split)) == (float) (int) StereoMode::Right, "split should produce Right");
    require (processor.splitBand (1) == -1, "split of a Stereo band should do nothing");

    // Swap moves contents between slots.
    const float freq0 = rawValue (processor, Params::bandFreqID (0));
    const float freq1 = rawValue (processor, Params::bandFreqID (1));
    processor.swapBands (0, 1);
    require (std::abs (rawValue (processor, Params::bandFreqID (0)) - freq1) < 1.0e-3f, "swap should move slot 1 into slot 0");
    require (std::abs (rawValue (processor, Params::bandFreqID (1)) - freq0) < 1.0e-3f, "swap should move slot 0 into slot 1");
    processor.swapBands (0, 1);

    // Sort by frequency packs used bands into the first slots in ascending order.
    processor.sortBands (false);
    float previous = 0.0f;
    int used = 0;
    for (int i = 0; i < OpenEQAudioProcessor::numBands; ++i)
    {
        if (! processor.isBandUsed (i))
            continue;
        require (i == used, "sorted bands should be packed into the first slots");
        const float frequency = rawValue (processor, Params::bandFreqID (i));
        require (frequency >= previous, "sorted bands should ascend in frequency");
        previous = frequency;
        ++used;
    }
    requireMsg (used >= 5, "sort should keep every used band, found " + std::to_string (used));

    // Clipboard paste into the first free slot keeps the copied frequency.
    processor.copyBandToClipboard (2);
    require (processor.hasBandClipboard(), "clipboard should hold the copied band");
    const int pasted = processor.pasteBandClipboard (-1, false);
    require (pasted >= 0 && processor.isBandUsed (pasted), "paste should land in a free slot");

    // Remove and clear.
    processor.removeBands ({ pasted });
    require (! processor.isBandUsed (pasted), "remove should free the slot");
    processor.clearAllBands();
    for (int i = 0; i < OpenEQAudioProcessor::numBands; ++i)
        require (! processor.isBandUsed (i), "clear all should leave every slot free");

    std::cout << "PASS: band duplicate, invert, split, swap, sort, clipboard, remove, clear, and undo\n";
}

static void checkModernDynamics()
{
    const double sampleRate = 48000.0;
    const int block = 512;

    // Runs a 1 kHz sine through one engine and returns the last gain offset.
    const auto runSine = [&] (DynamicsEngine& engine, const DynamicsEngine::Settings& settings,
                              float amplitude, float frequency, int blocks) -> float
    {
        std::vector<float> left (block), right (block);
        float offset = 0.0f;
        int phase = 0;
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < block; ++i)
            {
                const float s = amplitude * (float) std::sin (juce::MathConstants<double>::twoPi * frequency * (phase + i) / sampleRate);
                left[(size_t) i] = s;
                right[(size_t) i] = s;
            }
            phase += block;
            offset = engine.advance (left.data(), right.data(), block, DynamicsEngine::Mix::Energy,
                                     nullptr, nullptr, DynamicsEngine::Mix::Energy, settings);
        }
        return offset;
    };

    DynamicsEngine::Settings base;
    base.attackMs = 1.0f;
    base.releaseMs = 10.0f;
    base.modern = true;

    // Dynamics rule: offset = sign(range) * min(|range|, depth * ratio), depth measured from the threshold.
    const auto expected = [] (float envDb, float thresholdDb, bool above, float rangeDb, float ratio)
    {
        const float depth = above ? (envDb - thresholdDb) : (thresholdDb - envDb);
        if (depth <= 0.0f)
            return 0.0f;
        const float magnitude = juce::jmin (std::abs (rangeDb), depth * ratio);
        return rangeDb < 0.0f ? -magnitude : magnitude;
    };

    // Above panel alone, sine level above threshold.
    {
        DynamicsEngine engine;
        engine.prepare (sampleRate, block);
        engine.setTriggerFilter (1000.0, 1.0);
        auto settings = base;
        settings.thresholdDb = -20.0f;
        settings.aboveOn = true;
        settings.aboveRangeDb = 6.0f;
        settings.aboveRatio = 0.5f;
        const float offset = runSine (engine, settings, 0.3f, 1000.0f, 80);
        const float envDb = engine.getEnvelopeDb();
        requireMsg (envDb > -20.0f, "test tone should sit above the threshold, env " + std::to_string (envDb));
        const float want = expected (envDb, -20.0f, true, 6.0f, 0.5f);
        requireMsg (std::abs (offset - want) < 0.01f, "above +6 dB 50% gave " + std::to_string (offset) + " want " + std::to_string (want));
    }

    // Below panel alone, sine level below threshold, with a negative range (a cut).
    {
        DynamicsEngine engine;
        engine.prepare (sampleRate, block);
        engine.setTriggerFilter (1000.0, 1.0);
        auto settings = base;
        settings.thresholdDb = -8.0f;
        settings.belowOn = true;
        settings.belowRangeDb = -6.0f;
        settings.belowRatio = 0.5f;
        const float offset = runSine (engine, settings, 0.3f, 1000.0f, 80);
        const float envDb = engine.getEnvelopeDb();
        requireMsg (envDb < -8.0f, "test tone should sit below the threshold");
        const float want = expected (envDb, -8.0f, false, -6.0f, 0.5f);
        requireMsg (want < 0.0f, "expected a cut below the threshold");
        requireMsg (std::abs (offset - want) < 0.01f, "below -6 dB 50% gave " + std::to_string (offset) + " want " + std::to_string (want));
    }

    // Ratio above 100% scales the depth before the range limit.
    {
        DynamicsEngine engine;
        engine.prepare (sampleRate, block);
        engine.setTriggerFilter (1000.0, 1.0);
        auto settings = base;
        settings.thresholdDb = -20.0f;
        settings.aboveOn = true;
        settings.aboveRangeDb = 30.0f;
        settings.aboveRatio = 2.0f;
        const float offset = runSine (engine, settings, 0.3f, 1000.0f, 80);
        const float want = expected (engine.getEnvelopeDb(), -20.0f, true, 30.0f, 2.0f);
        requireMsg (std::abs (offset - want) < 0.01f, "above 200% ratio gave " + std::to_string (offset) + " want " + std::to_string (want));
    }

    // Only the panel that matches the side of the threshold responds. Below stays silent above threshold.
    {
        DynamicsEngine engine;
        engine.prepare (sampleRate, block);
        engine.setTriggerFilter (1000.0, 1.0);
        auto settings = base;
        settings.thresholdDb = -20.0f;
        settings.belowOn = true;
        settings.belowRangeDb = 6.0f;
        settings.belowRatio = 1.0f;
        const float offset = runSine (engine, settings, 0.3f, 1000.0f, 80);
        requireMsg (std::abs (offset) < 1.0e-6f, "Below alone should do nothing above the threshold");
    }

    // Both panels on: a tone above the threshold uses only Above.
    {
        DynamicsEngine engine;
        engine.prepare (sampleRate, block);
        engine.setTriggerFilter (1000.0, 1.0);
        auto settings = base;
        settings.thresholdDb = -20.0f;
        settings.aboveOn = true;
        settings.aboveRangeDb = -4.0f;
        settings.aboveRatio = 1.0f;
        settings.belowOn = true;
        settings.belowRangeDb = 12.0f;
        settings.belowRatio = 1.0f;
        const float offset = runSine (engine, settings, 0.3f, 1000.0f, 80);
        const float want = expected (engine.getEnvelopeDb(), -20.0f, true, -4.0f, 1.0f);
        requireMsg (std::abs (offset - want) < 0.01f, "with both panels, above should win above the threshold");
    }

    // Relative Free: a 1 kHz band reference is far below the broadband reference when a 100 Hz tone dominates.
    {
        DynamicsEngine broadband, banded;
        broadband.prepare (sampleRate, block);
        banded.prepare (sampleRate, block);
        broadband.setTriggerFilter (1000.0, 1.0);
        banded.setTriggerFilter (1000.0, 1.0);
        banded.setRelativeFilter (1000.0, 1.0);

        std::vector<float> left (block), right (block);
        auto settingsFree = base;
        settingsFree.relativeFree = true;
        settingsFree.thresholdDb = -20.0f;
        auto settingsWhole = settingsFree;
        settingsWhole.relativeFree = false;
        int phase = 0;
        for (int b = 0; b < 80; ++b)
        {
            for (int i = 0; i < block; ++i)
            {
                const double t = (phase + i) / sampleRate;
                const float s = 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * 100.0 * t)
                              + 0.05f * (float) std::sin (juce::MathConstants<double>::twoPi * 1000.0 * t);
                left[(size_t) i] = s;
                right[(size_t) i] = s;
            }
            phase += block;
            broadband.advance (left.data(), right.data(), block, DynamicsEngine::Mix::Energy,
                               nullptr, nullptr, DynamicsEngine::Mix::Energy, settingsWhole);
            banded.advance (left.data(), right.data(), block, DynamicsEngine::Mix::Energy,
                            nullptr, nullptr, DynamicsEngine::Mix::Energy, settingsFree);
        }
        const float wholeRef = broadband.referenceDb();
        const float bandRef = banded.referenceDb();
        std::cout << "  relative_reference_whole_db " << wholeRef << " band_1k_db " << bandRef << "\n";
        requireMsg (bandRef < wholeRef - 12.0f, "a 1 kHz relative reference should sit well below the broadband reference");
    }

    // Migration equivalence: the legacy single-action path and the migrated panel give the same offset.
    {
        DynamicsEngine legacy, migrated;
        legacy.prepare (sampleRate, block);
        migrated.prepare (sampleRate, block);
        legacy.setTriggerFilter (1000.0, 1.0);
        migrated.setTriggerFilter (1000.0, 1.0);
        auto legacySettings = base;
        legacySettings.modern = false;      // the single-action path
        legacySettings.thresholdDb = -20.0f;
        legacySettings.action = 1;          // below
        legacySettings.rangeDb = 9.0f;
        auto migratedSettings = legacySettings;
        migratedSettings.modern = true;
        migratedSettings.belowOn = true;
        migratedSettings.belowRangeDb = 9.0f;
        migratedSettings.belowRatio = 1.0f;
        const float a = runSine (legacy, legacySettings, 0.05f, 1000.0f, 80);
        const float b = runSine (migrated, migratedSettings, 0.05f, 1000.0f, 80);
        requireMsg (a > 0.5f, "legacy below should boost a quiet tone, got " + std::to_string (a));
        requireMsg (std::abs (a - b) < 1.0e-4f, "migrated panel should match the legacy path");
    }

    std::cout << "PASS: Below and Above panels, ratios, ranges, relative free reference, and legacy equivalence\n";
}

/** Saves the current state, then strips the Below/Above panel children so it reads as a pre-panel session. */
static juce::MemoryBlock makeLegacyDynamicsState (OpenEQAudioProcessor& processor)
{
    juce::MemoryBlock state;
    processor.getStateInformation (state);
    if (auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize()))
    {
        xml->setAttribute ("schema", 2);
        for (int i = 0; i < Params::numBands; ++i)
            for (const auto& id : { Params::bandDynBelowOnID (i), Params::bandDynBelowRangeID (i),
                                    Params::bandDynBelowRatioID (i), Params::bandDynAboveOnID (i),
                                    Params::bandDynAboveRangeID (i), Params::bandDynAboveRatioID (i) })
                for (int c = xml->getNumChildElements(); --c >= 0;)
                    if (auto* child = xml->getChildElement (c))
                        if (child->getStringAttribute ("id") == id)
                            xml->removeChildElement (child, true);
        state.reset();
        juce::AudioProcessor::copyXmlToBinary (*xml, state);
    }
    return state;
}

static void checkLegacyDynamicsMigration()
{
    // A band saved with the old single action comes back with the matching panel switched on.
    OpenEQAudioProcessor original;
    prepareStereo (original, 48000.0, 512);
    setRaw (original.apvts, Params::bandDynOnID (4), 1.0f);
    setRaw (original.apvts, Params::bandDynActionID (4), 1.0f);
    setRaw (original.apvts, Params::bandDynRangeID (4), 9.0f);

    const juce::MemoryBlock state = makeLegacyDynamicsState (original);
    OpenEQAudioProcessor restored;
    restored.setStateInformation (state.getData(), (int) state.getSize());

    require (rawValue (restored, Params::bandDynBelowOnID (4)) > 0.5f, "legacy below action should switch Below on");
    require (rawValue (restored, Params::bandDynAboveOnID (4)) < 0.5f, "Above should stay off after migrating a below action");
    require (std::abs (rawValue (restored, Params::bandDynBelowRangeID (4)) - 9.0f) < 1.0e-3f, "legacy range should carry over");
    require (std::abs (rawValue (restored, Params::bandDynBelowRatioID (4)) - 100.0f) < 1.0e-3f, "migrated ratio should be 100%");

    std::cout << "PASS: legacy dynamics migrate into the Below or Above panel\n";
}

static void checkCurrentDynamicsStatePreserved()
{
    // A current state with both panels deliberately off must survive recall unchanged. The legacy
    // migration used to rewrite it because it read the panel enable values instead of the saved schema.
    OpenEQAudioProcessor original;
    prepareStereo (original, 48000.0, 512);
    setRaw (original.apvts, Params::bandDynOnID (0), 1.0f);
    setRaw (original.apvts, Params::bandDynBelowOnID (0), 0.0f);
    setRaw (original.apvts, Params::bandDynAboveOnID (0), 0.0f);
    setRaw (original.apvts, Params::bandDynBelowRangeID (0), 6.0f);
    setRaw (original.apvts, Params::bandDynBelowRatioID (0), 50.0f);
    setRaw (original.apvts, Params::bandDynAboveRatioID (0), 75.0f);

    juce::MemoryBlock state;
    original.getStateInformation (state);
    OpenEQAudioProcessor restored;
    restored.setStateInformation (state.getData(), (int) state.getSize());

    require (rawValue (restored, Params::bandDynBelowOnID (0)) < 0.5f, "recall must not switch Below on");
    require (rawValue (restored, Params::bandDynAboveOnID (0)) < 0.5f, "recall must not switch Above on");
    require (std::abs (rawValue (restored, Params::bandDynBelowRangeID (0)) - 6.0f) < 1.0e-3f, "Below range must be preserved");
    require (std::abs (rawValue (restored, Params::bandDynBelowRatioID (0)) - 50.0f) < 1.0e-3f, "Below ratio must be preserved");
    require (std::abs (rawValue (restored, Params::bandDynAboveRatioID (0)) - 75.0f) < 1.0e-3f, "Above ratio must be preserved");

    std::cout << "PASS: current dynamics panel state survives recall unchanged\n";
}

static float meterPeakAfterDecay (int blockSize)
{
    OpenEQAudioProcessor processor;
    silenceBands (processor);
    prepareStereo (processor, 48000.0, blockSize);
    constexpr int total = 4800; // 100 ms at 48 kHz
    bool first = true;
    for (int done = 0; done < total; done += blockSize)
    {
        const int n = juce::jmin (blockSize, total - done);
        juce::AudioBuffer<float> buffer (2, n);
        buffer.clear();
        if (first)
        {
            buffer.setSample (0, 0, 0.5f);
            buffer.setSample (1, 0, 0.5f);
            first = false;
        }
        process (processor, buffer);
    }
    return processor.getMeterPeak (0);
}

static void checkMeterBallistics()
{
    // The same impulse must decay the same amount per unit of time, whatever the host block size.
    const float smallBlocks = meterPeakAfterDecay (32);
    const float largeBlocks = meterPeakAfterDecay (1024);
    const float ratio = largeBlocks / juce::jmax (1.0e-6f, smallBlocks);
    std::cout << "  meter_decay_32 " << smallBlocks << " meter_decay_1024 " << largeBlocks << "\n";
    require (smallBlocks > 0.15f && largeBlocks > 0.15f, "meter should still show the impulse after 100 ms");
    require (std::abs (ratio - 1.0f) < 0.05f, "peak release must not depend on the host block size");

    // Clip acknowledgement clears the processor latch and it stays clear until a new overload.
    OpenEQAudioProcessor processor;
    silenceBands (processor);
    prepareStereo (processor, 48000.0, 512);
    juce::AudioBuffer<float> loud (2, 512);
    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < loud.getNumSamples(); ++i)
            loud.setSample (channel, i, 1.0f);
    process (processor, loud);
    require (processor.getMeterClipped(), "a full-scale block should latch the clip indicator");
    processor.clearMeterClip();
    require (! processor.getMeterClipped(), "clearing the clip latch should read back as clear");

    juce::AudioBuffer<float> quiet (2, 512);
    quiet.clear();
    process (processor, quiet);
    require (! processor.getMeterClipped(), "the clip latch must stay clear until a new overload");

    std::cout << "PASS: meter release is time-based and clip reset clears the processor latch\n";
}

static void checkNonFiniteGuard()
{
    // 32 overlapping +30 dB bells are absurd but legal. Their product exceeds float range,
    // and the processor must recover rather than pass inf/NaN to the host.
    OpenEQAudioProcessor processor;
    prepareStereo (processor, 48000.0, 512);
    for (int band = 0; band < 32; ++band)
        configureBand (processor, band, FilterType::Bell, 1000.0f, 30.0f, 20.0f, 1, (int) StereoMode::Stereo, true);

    juce::AudioBuffer<float> buffer (2, 512);
    bool finite = true;
    for (int block = 0; block < 200; ++block)
    {
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                buffer.setSample (channel, i, (i % 5 == 0 ? 1.0f : -1.0f) * 0.5f);
        process (processor, buffer);
        finite = finite && allFinite (buffer);
    }
    float peakAbs = 0.0f;
    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            peakAbs = juce::jmax (peakAbs, std::abs (buffer.getSample (channel, i)));
    std::cout << "  finite_guard_peak " << peakAbs << " recoveries " << processor.getNonFiniteRecoveries() << "\n";
    require (finite, "extreme overlap must not send inf or NaN to the host");
    require (processor.getNonFiniteRecoveries() > 0, "the finite-output guard should have engaged");
    std::cout << "PASS: finite-output guard recovers extreme overlapping boosts ("
              << processor.getNonFiniteRecoveries() << " blocks)\n";
}

static void checkDisplaySettings()
{
    // Graph range: the 12 dB range maps ±12 dB to the top and bottom edges.
    EQCurveComponent::setDisplayRangeDb (12.0f);
    require (std::abs (EQCurveComponent::getDisplayRangeDb() - 12.0f) < 1.0e-6f, "graph range should store 12 dB");
    require (std::abs (EQCurveComponent::dbToY (12.0f, 100.0f)) < 1.0e-3f, "+12 dB should sit at the top of a 12 dB range");
    require (std::abs (EQCurveComponent::dbToY (-12.0f, 100.0f) - 100.0f) < 1.0e-3f, "-12 dB should sit at the bottom of a 12 dB range");
    EQCurveComponent::setDisplayRangeDb (30.0f);
    require (std::abs (EQCurveComponent::dbToY (0.0f, 100.0f) - 50.0f) < 1.0e-3f, "0 dB should sit mid-height at 30 dB");

    // Analyzer settings are stored, clamped, and read back.
    SpectrumAnalyzer::setDisplaySettings ({ 2, 0, 60.0f });
    auto settings = SpectrumAnalyzer::getDisplaySettings();
    require (settings.resolution == 2 && settings.speed == 0 && std::abs (settings.rangeDb - 60.0f) < 1.0e-6f,
             "analyzer settings should round-trip");
    require (std::abs (SpectrumAnalyzer::dbToY (-60.0f, 100.0f) - 100.0f) < 1.0e-3f, "analyzer bottom should follow the range");
    SpectrumAnalyzer::setDisplaySettings ({ 9, 9, 500.0f });
    settings = SpectrumAnalyzer::getDisplaySettings();
    require (settings.resolution == 3 && settings.speed == 2 && std::abs (settings.rangeDb - 120.0f) < 1.0e-6f,
             "analyzer settings should clamp to their ranges");
    SpectrumAnalyzer::setDisplaySettings ({ 0, 1, 90.0f });

    std::cout << "PASS: graph dB range, analyzer resolution, speed, and range\n";
}

static double channelRms (const juce::AudioBuffer<float>& buffer, int channel, int start, int count)
{
    double sum = 0.0;
    for (int i = 0; i < count; ++i)
        sum += (double) buffer.getSample (channel, start + i) * buffer.getSample (channel, start + i);
    return std::sqrt (sum / count);
}

static void fillStereo (juce::AudioBuffer<float>& buffer, double sampleRate, double frequency, float left, float right, int phase)
{
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const float s = (float) std::sin (juce::MathConstants<double>::twoPi * frequency * (phase + i) / sampleRate);
        buffer.setSample (0, i, left * s);
        buffer.setSample (1, i, right * s);
    }
}

static juce::AudioBuffer<float> runBand (StereoMode mode, float pan, float leftIn, float rightIn, int blocks)
{
    OpenEQAudioProcessor processor;
    prepareStereo (processor, 48000.0, 512);
    silenceBands (processor);
    configureBand (processor, 0, FilterType::Bell, 1000.0f, 12.0f, 1.0f, 1, (int) mode, true);
    setRaw (processor.apvts, Params::bandPanID (0), pan);
    juce::AudioBuffer<float> buffer (2, 512);
    for (int block = 0; block < blocks; ++block)
    {
        fillStereo (buffer, 48000.0, 1000.0, leftIn, rightIn, block * 512);
        process (processor, buffer);
    }
    // Return only the last block, for measurement.
    juce::AudioBuffer<float> last (2, 512);
    last.makeCopyOf (buffer);
    return last;
}

static void checkStereoPanAndMidSide()
{
    // Stereo with a full pan to the right: the left channel keeps its dry level, the right gets the boost.
    {
        const auto out = runBand (StereoMode::Stereo, 100.0f, 0.1f, 0.1f, 60);
        const double dryRms = 0.1 / std::sqrt (2.0);
        const double leftRms = channelRms (out, 0, 0, 512);
        const double rightRms = channelRms (out, 1, 0, 512);
        std::cout << "  pan_full_right left_rms " << leftRms << " right_rms " << rightRms << "\n";
        requireMsg (std::abs (leftRms - dryRms) / dryRms < 0.03, "full right pan should leave the left channel dry");
        requireMsg (rightRms / dryRms > 3.5, "full right pan should boost the right channel by about 12 dB");
    }

    // Mid Side with pan 0 is the same as Stereo for the same input.
    {
        const auto stereo = runBand (StereoMode::Stereo, 0.0f, 0.1f, 0.03f, 60);
        const auto midSide = runBand (StereoMode::MidSide, 0.0f, 0.1f, 0.03f, 60);
        float maxDiff = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
                maxDiff = juce::jmax (maxDiff, std::abs (stereo.getSample (ch, i) - midSide.getSample (ch, i)));
        requireMsg (maxDiff < 1.0e-3f, "Mid Side at centre should match Stereo, max diff " + std::to_string (maxDiff));
    }

    // Mid Side with a full pan to the side: mid-only content (L = R) is untouched, side-only content (L = -R) is boosted.
    {
        const auto midOnly = runBand (StereoMode::MidSide, 100.0f, 0.1f, 0.1f, 60);
        const double dry = 0.1 / std::sqrt (2.0);
        requireMsg (std::abs (channelRms (midOnly, 0, 0, 512) - dry) / dry < 0.03, "full side pan should leave mid-only content dry");
        const auto sideOnly = runBand (StereoMode::MidSide, 100.0f, 0.1f, -0.1f, 60);
        requireMsg (channelRms (sideOnly, 0, 0, 512) / dry > 3.5, "full side pan should boost side-only content");
    }

    // The matrix agrees: Mid Side at centre is the plain band, and a full pan removes the band from the dry part.
    {
        const auto centre = ResponseModel::bandMatrix (2.0, StereoMode::MidSide, 0.0f);
        requireMsg (std::abs (centre.ll - 2.0) < 1.0e-9 && std::abs (centre.lr) < 1.0e-9, "Mid Side centre matrix should be the plain band");
        const auto full = ResponseModel::bandMatrix (2.0, StereoMode::Stereo, 1.0f);
        requireMsg (std::abs (full.ll - 1.0) < 1.0e-9 && std::abs (full.rr - 2.0) < 1.0e-9,
                    "full right balance should leave left unity and right at the band gain");
    }

    std::cout << "PASS: stereo balance, Mid Side mode, and their response matrix\n";
}

static void checkGlobalLookahead()
{
    // With the global lookahead on, the plugin reports a fixed 20 ms latency, even at 0 ms lookahead.
    OpenEQAudioProcessor processor;
    prepareStereo (processor, 48000.0, 512);
    silenceBands (processor);
    setRaw (processor.apvts, "lookaheadOn", 1.0f);
    setRaw (processor.apvts, "lookaheadMs", 0.0f);
    juce::AudioBuffer<float> buffer (2, 512);
    fillTone (buffer, 48000.0, 1000.0, 0.1f, 0, Tone::Centered);
    process (processor, buffer);
    require (processor.getReportedLatencySamples() == 960, "global lookahead should report 20 ms (960 samples) at 48 kHz");

    // Off again: no added latency with no linear-phase or dynamic work.
    // The change is applied after a short fade, so the latency drops a block or two later.
    setRaw (processor.apvts, "lookaheadOn", 0.0f);
    for (int i = 0; i < 4; ++i)
        process (processor, buffer);
    require (processor.getReportedLatencySamples() == 0, "lookahead off should report no latency");

    std::cout << "PASS: global lookahead reports a fixed 20 ms when on\n";
}

static void checkUltraPrecision()
{
    // 117-bit mode runs a double-double recursion. It must agree with the double path, be finite, and survive a reset.
    const double sampleRate = 48000.0;
    auto render = [&] (bool ultra, FilterType type, float frequency, float gain, float q)
    {
        OpenEQAudioProcessor processor;
        prepareStereo (processor, sampleRate, 512);
        silenceBands (processor);
        configureBand (processor, 0, type, frequency, gain, q, 1, (int) StereoMode::Stereo, true);
        setRaw (processor.apvts, "ultraPrecision", ultra ? 1.0f : 0.0f);
        juce::AudioBuffer<float> buffer (2, 512);
        std::vector<float> out;
        for (int block = 0; block < 40; ++block)
        {
            fillTone (buffer, sampleRate, 137.0, 0.3f, block * 512, Tone::Centered);
            process (processor, buffer);
            for (int i = 0; i < 512; ++i)
                out.push_back (buffer.getSample (0, i));
        }
        return out;
    };

    for (auto type : { FilterType::Bell, FilterType::LowCut })
    {
        const auto normal = render (false, type, 60.0f, 6.0f, 6.0f);
        const auto ultra = render (true, type, 60.0f, 6.0f, 6.0f);
        float maxDiff = 0.0f;
        for (size_t i = 0; i < normal.size(); ++i)
        {
            requireMsg (std::isfinite (ultra[i]), "117-bit output must be finite");
            maxDiff = juce::jmax (maxDiff, std::abs (normal[i] - ultra[i]));
        }
        std::cout << "  ultra_vs_double_max_diff " << maxDiff << "\n";
        requireMsg (maxDiff < 1.0e-5f, "117-bit path should agree with the double path, max diff " + std::to_string (maxDiff));
    }

    // The band object itself switches path and keeps the output finite.
    EQBand band;
    band.prepare ({ sampleRate, 512, 1 });
    band.applyIfChanged (FilterType::Bell, 60.0f, 6.0f, 6.0f, 12);
    band.setUltraPrecision (true);
    require (band.isUltraPrecision(), "EQBand should report 117-bit mode after it is enabled");
    std::vector<float> tone (512);
    for (int i = 0; i < 512; ++i)
        tone[(size_t) i] = 0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * 137.0 * i / sampleRate);
    band.processChannel (tone.data(), 512, 0);
    for (float v : tone)
        require (std::isfinite (v), "117-bit EQBand output must be finite");

    std::cout << "PASS: 117-bit double-double path agrees with the 64-bit path\n";
}

static void checkOversampling()
{
    // 2x oversampling must not change the steady-state level of a boost, must report a latency, and must restore cleanly.
    const double sampleRate = 48000.0;
    auto render = [&] (bool oversample, double& latencyOut, double& rmsOut)
    {
        OpenEQAudioProcessor processor;
        silenceBands (processor);
        configureBand (processor, 0, FilterType::Bell, 1000.0f, 6.0f, 1.0f, 1, (int) StereoMode::Stereo, true);
        setRaw (processor.apvts, "oversample2x", oversample ? 1.0f : 0.0f);
        prepareStereo (processor, sampleRate, 512);
        require (processor.isOversampling2x() == oversample, "oversampling flag should follow the parameter at prepare");
        juce::AudioBuffer<float> buffer (2, 512);
        for (int block = 0; block < 80; ++block)
        {
            fillTone (buffer, sampleRate, 1000.0, 0.1f, block * 512, Tone::Centered);
            process (processor, buffer);
        }
        rmsOut = channelRms (buffer, 0, 0, 512);
        latencyOut = (double) processor.getReportedLatencySamples();
    };

    double latencyOff = 0.0, latencyOn = 0.0, rmsOff = 0.0, rmsOn = 0.0;
    render (false, latencyOff, rmsOff);
    render (true, latencyOn, rmsOn);

    std::cout << "  oversample_rms_off " << rmsOff << " on " << rmsOn << " latency_on " << latencyOn << "\n";
    require (latencyOff == 0.0, "2x off should report no latency");
    require (latencyOn > 0.0, "2x on should report the oversampler latency");
    requireMsg (std::abs (rmsOn / rmsOff - 1.0) < 0.02, "2x should keep the steady-state level within 2%");
    // A 6 dB boost at 1 kHz doubles the amplitude of a 0.1 sine.
    requireMsg (std::abs (rmsOn / (0.1 / std::sqrt (2.0)) - 2.0) < 0.05, "2x boost should still be about 6 dB at 1 kHz");

    std::cout << "PASS: 2x oversampling keeps the level and reports its latency\n";
}

static void checkPresetFiles()
{
    // Save a preset to a temporary file, load it into a fresh processor, and check the band survived.
    const auto file = juce::File::createTempFile ("openeq-test.opeq");
    {
        OpenEQAudioProcessor original;
        configureBand (original, 4, FilterType::HighShelf, 3333.0f, -4.5f, 1.2f, 1, (int) StereoMode::Mid, true);
        require (original.savePresetFile (file), "preset should save to a temporary file");
    }
    OpenEQAudioProcessor restored;
    require (restored.loadPresetFile (file), "preset should load back");
    require (std::abs (rawValue (restored, Params::bandFreqID (4)) - 3333.0f) < 1.0e-2f, "preset frequency did not round-trip");
    require (rawValue (restored, Params::bandTypeID (4)) == (float) FilterType::HighShelf, "preset type did not round-trip");
    require (rawValue (restored, Params::bandStereoID (4)) == (float) StereoMode::Mid, "preset stereo mode did not round-trip");
    require (restored.listUserPresets().size() >= 0, "user preset listing should not fail");
    file.deleteFile();

    std::cout << "PASS: preset files save and load\n";
}

static std::vector<float> settledImpulseForMode (int mode, FilterType type, float frequency, float gainDb, int crossoverIndex = 1,
                                                 int placement = (int) StereoMode::Stereo)
{
    OpenEQAudioProcessor processor;
    silenceBands (processor);
    configureBand (processor, 0, type, frequency, gainDb, 1.0f, 1, placement, true);
    setRaw (processor.apvts, "processingMode", (float) mode);
    setRaw (processor.apvts, "linearPhaseQuality", 0.0f);
    setRaw (processor.apvts, "mixCrossover", (float) crossoverIndex);
    prepareStereo (processor, 48000.0, 512);
    juce::AudioBuffer<float> warmup (2, 256);
    warmup.clear();
    process (processor, warmup);
    const auto started = processor.getLinearPhaseDesigns();
    const auto deadline = juce::Time::getMillisecondCounter() + 2000;
    while (processor.getLinearPhaseDesigns() == started && juce::Time::getMillisecondCounter() < deadline)
        juce::Thread::sleep (10);
    flushLinearPhase (processor);
    juce::AudioBuffer<float> impulse (2, 4096);
    impulse.clear();
    impulse.setSample (0, 0, 1.0f);
    process (processor, impulse);
    return std::vector<float> (impulse.getReadPointer (0), impulse.getReadPointer (0) + 4096);
}

static void checkAnalogAndMixModes()
{
    const double sampleRate = 48000.0;
    const auto linear = settledImpulseForMode (1, FilterType::Bell, 300.0f, 6.0f);
    const auto analog = settledImpulseForMode (2, FilterType::Bell, 300.0f, 6.0f);
    const auto mixed = settledImpulseForMode (3, FilterType::Bell, 300.0f, 6.0f);

    // Analog: the same magnitude as linear phase, with the same latency.
    const int linearPeak = peakIndex (linear.data(), (int) linear.size());
    const int analogPeak = peakIndex (analog.data(), (int) analog.size());
    std::cout << "  peak_linear " << linearPeak << " peak_analog " << analogPeak << "\n";
    require (std::abs (linearPeak - analogPeak) <= 1, "analog mode should have the same latency as linear phase");
    for (double f : { 100.0, 300.0, 1000.0 })
    {
        const double lin = juce::Decibels::gainToDecibels (impulseMagnitude (linear.data(), 4096, f, sampleRate));
        const double ana = juce::Decibels::gainToDecibels (impulseMagnitude (analog.data(), 4096, f, sampleRate));
        requireMsg (std::abs (lin - ana) < 0.5, "analog magnitude should match linear at " + std::to_string (f) + " Hz");
    }

    // Linear phase is symmetric. Mixed phase is not, because the minimum-phase part below the crossover is one-sided.
    auto asymmetry = [] (const std::vector<float>& h)
    {
        const int peak = peakIndex (h.data(), (int) h.size());
        double diff = 0.0, energy = 0.0;
        for (int k = 1; k < 1500 && peak - k >= 0 && peak + k < (int) h.size(); ++k)
        {
            const double a = h[(size_t) (peak + k)];
            const double b = h[(size_t) (peak - k)];
            diff += (a - b) * (a - b);
            energy += a * a + b * b;
        }
        return energy > 0.0 ? diff / energy : 0.0;
    };
    const double linearAsymmetry = asymmetry (linear);
    const double mixedAsymmetry = asymmetry (mixed);
    std::cout << "  asymmetry_linear " << linearAsymmetry << " asymmetry_mixed " << mixedAsymmetry << "\n";
    require (linearAsymmetry < 1.0e-3, "linear phase impulse should be symmetric");
    requireMsg (mixedAsymmetry > 0.01, "mixed-phase impulse should be asymmetric");

    // Mixed keeps the magnitude at both ends: the boost sits below the crossover and the flat top above it.
    const double low = juce::Decibels::gainToDecibels (impulseMagnitude (mixed.data(), 4096, 300.0, sampleRate));
    const double high = juce::Decibels::gainToDecibels (impulseMagnitude (mixed.data(), 4096, 12000.0, sampleRate));
    std::cout << "  mixed_300_db " << low << " mixed_12k_db " << high << "\n";
    requireMsg (std::abs (low - 6.0) < 0.6, "mixed mode should keep the boost at 300 Hz");
    requireMsg (std::abs (high) < 0.6, "mixed mode should leave 12 kHz flat");

    // A band placed on the left is mixed too, not left as a symmetric magnitude fallback. Its left output keeps the boost.
    const auto leftMixed = settledImpulseForMode (3, FilterType::Bell, 300.0f, 6.0f, 1, (int) StereoMode::Left);
    const double leftAsymmetry = asymmetry (leftMixed);
    const double leftLow = juce::Decibels::gainToDecibels (impulseMagnitude (leftMixed.data(), 4096, 300.0, sampleRate));
    std::cout << "  asymmetry_left_mixed " << leftAsymmetry << " left_mixed_300_db " << leftLow << "\n";
    requireMsg (leftAsymmetry > 0.01, "mixed mode should blend a left-placed band's minimum-phase response");
    requireMsg (std::abs (leftLow - 6.0) < 0.6, "mixed mode should keep the left-placed boost at 300 Hz");

    std::cout << "PASS: analog magnitude matches linear phase, and mixed phase is asymmetric with the boost kept\n";
}

static void checkThemes()
{
    const auto graphite = Theme::panelTop;
    Theme::applyTheme (1);
    require (Theme::panelTop != graphite, "the Light theme should change the panel colour");
    require (Theme::textBright.getPerceivedBrightness() < 0.5f, "the Light theme should use dark text");
    Theme::applyTheme (2);
    require (Theme::textBright == juce::Colours::white, "High contrast should use white text");
    Theme::applyTheme (0);
    require (Theme::panelTop == graphite, "Graphite should restore the stock panel colour");
    std::cout << "PASS: theme palettes change and restore the colours\n";
}

static void checkThirtyTwoBands()
{
    require (OpenEQAudioProcessor::numBands == 32, "numBands should be 32");
    require (ResponseModel::graphBandCount == 32, "graphBandCount should be 32");
    require (Params::numBands == 32, "Params::numBands should be 32");

    OpenEQAudioProcessor processor;
    require (processor.apvts.getParameter ("b31_freq") != nullptr, "b31_freq missing");
    require (processor.apvts.getParameter ("b31_dynDetQ") != nullptr, "b31_dynDetQ missing");
    require (rawValue (processor, "b31_active") < 0.5f, "new band slots must start inactive");
    require (rawValue (processor, "b0_active") > 0.5f, "original default bands must stay active");

    std::cout << "PASS: 32 band slots with inactive defaults\n";
}

static void checkGlobalControls()
{
    OpenEQAudioProcessor processor;
    const double sampleRate = 48000.0;
    prepareStereo (processor, sampleRate, 512);
    silenceBands (processor);
    require (processor.apvts.getParameter ("gainScale") != nullptr, "gainScale missing");
    require (processor.apvts.getParameter ("sideGain") != nullptr, "sideGain missing");
    require (processor.apvts.getParameter ("outputPan") != nullptr, "outputPan missing");

    // Gain Scale multiplies every band's dB gain.
    configureBand (processor, 0, FilterType::Bell, 1000.0f, 12.0f, 1.0f, 1, (int) StereoMode::Stereo, true);
    setRaw (processor.apvts, "gainScale", 50.0f);
    require (std::abs (processor.readBandParameter (0).gainDb - 6.0f) < 1.0e-3f, "gain scale 50% should halve 12 dB to 6 dB");
    setRaw (processor.apvts, "gainScale", 0.0f);
    require (std::abs (processor.readBandParameter (0).gainDb) < 1.0e-6f, "gain scale 0% should remove gain");
    setRaw (processor.apvts, "gainScale", 100.0f);
    require (std::abs (processor.readBandParameter (0).gainDb - 12.0f) < 1.0e-3f, "gain scale 100% should be unity");
    silenceBands (processor);

    // Input trim: -6 dB halves the signal, and the input meter still reads the untrimmed level.
    setRaw (processor.apvts, "inputGain", -6.0206f);
    juce::AudioBuffer<float> trimmed (2, 512);
    for (int block = 0; block < 80; ++block)
    {
        for (int i = 0; i < trimmed.getNumSamples(); ++i)
        {
            trimmed.setSample (0, i, 0.5f);
            trimmed.setSample (1, i, 0.5f);
        }
        process (processor, trimmed);
    }
    const float trimmedOut = trimmed.getSample (0, 511);
    requireMsg (std::abs (trimmedOut - 0.25f) < 0.01f, "input trim -6 dB should halve 0.5, got " + std::to_string (trimmedOut));
    requireMsg (processor.getInputMeterPeak (0) > 0.45f, "input meter should read the signal before the trim");
    setRaw (processor.apvts, "inputGain", 0.0f);

    // Side Level on pure side content (L = +1, R = -1): -6.02 dB halves the side, so L settles at 0.5.
    setRaw (processor.apvts, "stereoWidth", 100.0f);
    setRaw (processor.apvts, "outputGain", 0.0f);
    setRaw (processor.apvts, "outputPan", 0.0f);
    setRaw (processor.apvts, "sideGain", -6.0206f);
    juce::AudioBuffer<float> buffer (2, 512);
    for (int block = 0; block < 60; ++block)
    {
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            buffer.setSample (0, i, 1.0f);
            buffer.setSample (1, i, -1.0f);
        }
        process (processor, buffer);
    }
    const float sideHalf = buffer.getSample (0, 511);
    requireMsg (std::abs (sideHalf - 0.5f) < 0.01f, "side -6 dB should settle L near 0.5, got " + std::to_string (sideHalf));

    // Side at -inf removes the side component entirely.
    setRaw (processor.apvts, "sideGain", -100.0f);
    for (int block = 0; block < 60; ++block)
    {
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            buffer.setSample (0, i, 1.0f);
            buffer.setSample (1, i, -1.0f);
        }
        process (processor, buffer);
    }
    requireMsg (std::abs (buffer.getSample (0, 511)) < 0.01f, "side -inf should silence pure side content");

    // Output Pan is a balance on a centred signal (L = R = 1). Each block refills the buffer.
    const auto runCentred = [&] (float pan)
    {
        setRaw (processor.apvts, "outputPan", pan);
        for (int block = 0; block < 60; ++block)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                    buffer.setSample (ch, i, 1.0f);
            process (processor, buffer);
        }
    };

    setRaw (processor.apvts, "sideGain", 0.0f);
    runCentred (100.0f);
    requireMsg (std::abs (buffer.getSample (0, 511)) < 0.01f, "full-right pan should silence the left channel");
    requireMsg (std::abs (buffer.getSample (1, 511) - 1.0f) < 0.01f, "full-right pan should keep the right channel");

    runCentred (-50.0f);
    requireMsg (std::abs (buffer.getSample (0, 511) - 1.0f) < 0.01f, "left pan should keep the left channel at unity");
    requireMsg (std::abs (buffer.getSample (1, 511) - 0.5f) < 0.01f, "left pan -50% should attenuate the right to 0.5");

    std::cout << "PASS: gain scale, side level, and output pan\n";
}

static void checkBehaviourSettings()
{
    // The settings file is the user's own, so each check restores what it found.
    auto& settings = AppSettings::get();
    const float wheelBefore = settings.knobWheelSpeed();
    settings.setKnobWheelSpeed (1000.0f);
    require (settings.knobWheelSpeed() == 400.0f, "wheel speed should clamp to 400%");
    settings.setKnobWheelSpeed (wheelBefore);

    const float factorBefore = settings.knobSpeedFactor (0, false);
    settings.setKnobSpeedFactor (0, false, 0.0f);
    require (settings.knobSpeedFactor (0, false) == 0.1f, "per-control speed should clamp to 0.1");
    settings.setKnobSpeedFactor (0, false, factorBefore);

    const int actionBefore = settings.knobDoubleClickAction();
    settings.setKnobDoubleClickAction (7);
    require (settings.knobDoubleClickAction() == 1, "double-click action should clamp to reset or typed entry");
    settings.setKnobDoubleClickAction (actionBefore);

    const int ruleBefore = settings.newBandStereoRule();
    settings.setNewBandStereoRule (9);
    require (settings.newBandStereoRule() == 2, "stereo rule should clamp to the three rules");
    settings.setNewBandStereoRule (ruleBefore);

    const int guiBefore = settings.guiScalePercent();
    settings.setGuiScalePercent (117);
    require (settings.guiScalePercent() == 110, "GUI scale should snap to the nearest step");
    settings.setGuiScalePercent (guiBefore);

    // The graph's displayed range is shared with the knobs through AppSettings.
    const float rangeBefore = EQCurveComponent::getDisplayRangeDb();
    EQCurveComponent::setDisplayRangeDb (12.0f);
    require (AppSettings::displayRangeDb().load() == 12.0f, "the knobs should read the graph's displayed range");
    EQCurveComponent::setDisplayRangeDb (rangeBefore);

    std::cout << "PASS: reference behaviour settings clamp and share state\n";
}

static void checkThemeFile()
{
    // A minimal reference theme: ARGB integers, as the real files store them.
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("openeq_test.opeq_theme");
    file.replaceWithText ("{\"whole_EQ_line_colour\": -14066, \"band_0_colour\": -10831873, \"use_customer_background\": false}");

    Theme::applyTheme (0);
    require (ThemeFile::load (file), "a theme file with mapped colours should load");
    require (Theme::curveCol == juce::Colour ((juce::uint32) (juce::int32) -14066), "the whole-EQ line colour should map to the curve");
    require (Theme::bandColourOverride[0] == juce::Colour ((juce::uint32) (juce::int32) -10831873), "band 0 colour should map to band 0");

    Theme::applyTheme (0);
    require (Theme::bandColourOverride[0].getAlpha() == 0, "a built-in theme should clear band colour overrides");
    require (! ThemeFile::load (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("openeq_missing.opeq_theme")),
             "a missing theme file should not load");
    // Saving writes the same keys, so a saved theme loads back to the same colours.
    const auto saved = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("openeq_saved.opeq_theme");
    Theme::applyTheme (0);
    require (ThemeFile::load (file), "the test theme should load before saving");
    const auto curve = Theme::curveCol;
    const auto band0 = Theme::bandColourOverride[0];
    require (ThemeFile::save (saved, [] (int band) { return EQCurveComponent::bandColour (band); }), "a theme should save");
    Theme::applyTheme (0);
    require (ThemeFile::load (saved), "a saved theme should load");
    require (Theme::curveCol == curve, "a saved theme should restore the curve colour");
    require (Theme::bandColourOverride[0] == band0, "a saved theme should restore band colours");
    saved.deleteFile();

    file.deleteFile();
    std::cout << "PASS: reference theme file maps curve and band colours\n";
}

static void checkPresetImport()
{
    // A small synthetic preset in the .json layout: JSON, with each value normalised 0 to 1.
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("openeq_import.json");
    file.replaceWithText (R"({
        "Band1 Used": "1.000000000e+00", "Band1 Enabled": "1.000000000e+00",
        "Band1 Type": "4.225352113e-02", "Band1 Freq.": "5.000000000e-01",
        "Band1 Gain": "7.500000000e-01", "Band1 Q": "5.000000000e-01",
        "Band1 Slope": "0.000000000e+00", "Band1 Stereo Placement": "4.000000000e-01",
        "Band1 Pan/Width": "2.500000000e-01",
        "Band2 Used": "1.000000000e+00", "Band2 Enabled": "0.000000000e+00",
        "Band2 Type": "1.408450704e-02", "Band2 Freq.": "0.000000000e+00",
        "Band3 Used": "1.000000000e+00", "Band3 Enabled": "1.000000000e+00",
        "Band3 Type": "2.676056338e-01", "Band3 Freq.": "5.000000000e-01",
        "Band3 Gain": "5.000000000e-01", "Band3 Q": "5.000000000e-01",
        "Band3 Dyn. On": "1.000000000e+00", "Band3 Dyn. Threshold": "5.000000000e-01",
        "Pan": "5.000000000e-01", "Width": "5.000000000e-01", "Gain Scale": "7.500000000e-01",
        "Master Vol.": "5.000000000e-01", "Phase Mode": "0.000000000e+00"
    })");

    OpenEQAudioProcessor processor;
    const auto result = PresetImport::importPreset (processor, file);
    auto raw = [&processor] (const juce::String& id) { return processor.apvts.getRawParameterValue (id)->load(); };

    require (result.bandsImported == 3, "three used bands should import");
    require (raw (Params::bandTypeID (0)) == (float) Params::filterTypeNames.indexOf ("Bell"), "slot 3 should import as Bell");
    requireMsg (std::abs (raw (Params::bandFreqID (0)) - 547.72f) < 0.5f, "a normalised 0.5 frequency should be 547.7 Hz");
    requireMsg (std::abs (raw (Params::bandGainID (0)) - 15.0f) < 0.01f, "a normalised 0.75 gain should be +15 dB");
    requireMsg (std::abs (raw (Params::bandQID (0)) - 1.0f) < 0.001f, "a normalised 0.5 Q should be 1.0");
    require (raw (Params::bandSlopeID (0)) == 1.0f, "12 dB/oct should map to the 12 dB/oct choice");
    require (raw (Params::bandStereoID (0)) == 3.0f, "placement step 2 should be Left");
    requireMsg (std::abs (raw (Params::bandPanID (0)) + 50.0f) < 0.01f, "a pan of 0.25 should be -50");
    require (raw (Params::bandShownID (0)) == 1.0f && raw (Params::bandActiveID (0)) == 1.0f, "an enabled used band should show and be active");
    require (raw (Params::bandShownID (1)) == 1.0f && raw (Params::bandActiveID (1)) == 0.0f, "a disabled used band should show but not be active");
    require (raw (Params::bandFreqID (1)) == 20.0f, "a frequency below 20 Hz should clamp to 20 Hz");
    require (raw (Params::bandShownID (3)) == 0.0f, "bands not in the file should be cleared");
    require (raw (Params::bandDynOnID (2)) == 1.0f && raw (Params::bandDynBelowOnID (2)) == 1.0f
             && raw (Params::bandDynAboveOnID (2)) == 1.0f, "a dynamic band should switch both panels on");
    require (raw (Params::bandTypeID (2)) == (float) Params::filterTypeNames.indexOf ("G HP"),
             "slot 19 (G HP) should import as G HP");
    requireMsg (std::abs (raw ("outputPan")) < 0.01f, "a centred pan should stay centred");
    requireMsg (std::abs (raw ("stereoWidth") - 100.0f) < 0.01f, "a width of 0.5 should be 100%");
    requireMsg (std::abs (raw ("gainScale") - 100.0f) < 0.01f, "a gain scale of 0.75 should be 100%");
    requireMsg (std::abs (raw ("outputGain")) < 0.05f, "a master volume of 0.5 should be 0 dB");

    // Every reference type is now matched within 0.5 dB RMS, so no type import is flagged as approximated.
    // The note mechanism must still fire for settings that genuinely cannot be imported (Phase Mode > 0.5).
    const auto noteFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("openeq_import_note.json");
    noteFile.replaceWithText (R"({ "Band1 Used": "1.0", "Band1 Enabled": "1.0", "Band1 Type": "5.774647887e-01",
                                  "Band1 Freq.": "0.5", "Phase Mode": "1.0" })");
    OpenEQAudioProcessor approx;
    const auto approxResult = PresetImport::importPreset (approx, noteFile);
    bool noted = false;
    for (const auto& note : approxResult.notes)
        noted = noted || note.contains ("Phase mode");
    require (noted, "a Phase Mode other than Minimum Phase should be reported on import");
    bool approxNoted = false;
    for (const auto& note : approxResult.notes)
        approxNoted = approxNoted || note.contains ("approximated");
    require (! approxNoted, "the wall is matched within 0.5 dB RMS and must not be reported as approximated");

    // A synthetic bass preset exercises low-frequency, small-gain, and non-unity-Q conversion.
    const auto cleanBass = juce::File (__FILE__).getParentDirectory().getChildFile ("fixtures/import-bass.json");
    require (cleanBass.existsAsFile(), "the synthetic bass import fixture must be available");
    {
        OpenEQAudioProcessor bass;
        PresetImport::importPreset (bass, cleanBass);
        auto bassRaw = [&bass] (const juce::String& id) { return bass.apvts.getRawParameterValue (id)->load(); };
        require (bassRaw (Params::bandTypeID (0)) == (float) Params::filterTypeNames.indexOf ("High Pass"),
                 "Bass fixture band 1 should be High Pass");
        requireMsg (std::abs (bassRaw (Params::bandFreqID (0)) - 39.679f) < 0.05f, "Bass fixture band 1 should be 39.68 Hz");
        requireMsg (std::abs (bassRaw (Params::bandGainID (0)) + 0.09f) < 0.02f, "Bass fixture band 1 gain should be -0.09 dB");
        requireMsg (std::abs (bassRaw (Params::bandQID (0)) - 1.176f) < 0.01f, "Bass fixture band 1 Q should be 1.176");
        std::cout << "  clean_bass_band1 freq " << bassRaw (Params::bandFreqID (0)) << " gain " << bassRaw (Params::bandGainID (0))
                  << " q " << bassRaw (Params::bandQID (0)) << "\n";
    }

    file.deleteFile();
    noteFile.deleteFile();
    std::cout << "PASS: reference .json import maps types, values and dynamics\n";
}

static void fillToneRange (juce::AudioBuffer<float>& buffer, double sampleRate, double frequency,
                            float amplitude, int start, int end, int phaseSample, Tone tone)
{
    for (int i = start; i < end && i < buffer.getNumSamples(); ++i)
    {
        const float sample = amplitude * (float) std::sin (juce::MathConstants<double>::twoPi
                                                            * frequency * (double) (phaseSample + i) / sampleRate);
        buffer.setSample (0, i, sample);
        if (buffer.getNumChannels() > 1)
            buffer.setSample (1, i, tone == Tone::Side ? -sample : sample);
    }
}

static void checkCrossInstance()
{
    // Registry: processors appear while alive and vanish on destruction.
    {
        const int before = (int) InstanceRegistry::get().snapshot().size();
        auto processor = std::make_unique<OpenEQAudioProcessor>();
        require ((int) InstanceRegistry::get().snapshot().size() == before + 1, "a processor should register itself");
        require (InstanceRegistry::get().isRegistered (processor.get()), "the registry should know the processor");
        require (processor->getInstanceId() > 0, "each instance should have an id");
        processor.reset();
        require ((int) InstanceRegistry::get().snapshot().size() == before, "a destroyed processor should deregister");
    }

    // Relay: two processors mirror each other in both directions while linked.
    {
        OpenEQAudioProcessor a;
        OpenEQAudioProcessor b;
        require (a.getRemoteTarget() == nullptr, "no target before linking");
        a.setRemoteTarget (&b);
        require (a.getRemoteTarget() == &b, "the target should be set");
        // a -> b
        setRaw (a.apvts, Params::bandGainID (0), 5.0f);
        requireMsg (std::abs (rawValue (b, Params::bandGainID (0)) - 5.0f) < 0.01f, "edits on A should reach B");
        // b -> a
        setRaw (b.apvts, Params::bandFreqID (2), 4321.0f);
        requireMsg (std::abs (rawValue (a, Params::bandFreqID (2)) - 4321.0f) < 0.5f, "edits on B should reach A");
        // Unlink stops the mirror.
        a.setRemoteTarget (nullptr);
        setRaw (a.apvts, Params::bandGainID (0), -5.0f);
        requireMsg (std::abs (rawValue (b, Params::bandGainID (0)) - 5.0f) < 0.01f, "unlinked edits should stay local");
    }

    // Destroying the target drops the mirror on the next change.
    {
        OpenEQAudioProcessor a;
        {
            auto b = std::make_unique<OpenEQAudioProcessor>();
            a.setRemoteTarget (b.get());
            require (a.getRemoteTarget() != nullptr, "target should link");
        }
        setRaw (a.apvts, Params::bandGainID (0), 3.0f);
        require (a.getRemoteTarget() == nullptr, "a destroyed target should drop the mirror");
    }

    std::cout << "PASS: cross-instance registry and two-way parameter relay\n";
}

static std::vector<float> settledImpulseForPhase (float bandPhase, int mode = 1)
{
    OpenEQAudioProcessor processor;
    silenceBands (processor);
    configureBand (processor, 0, FilterType::Bell, 300.0f, 6.0f, 1.0f, 1, (int) StereoMode::Stereo, true);
    setRaw (processor.apvts, Params::bandPhaseID (0), bandPhase);
    setRaw (processor.apvts, "processingMode", (float) mode);
    setRaw (processor.apvts, "linearPhaseQuality", 0.0f);
    prepareStereo (processor, 48000.0, 512);
    juce::AudioBuffer<float> warmup (2, 256);
    warmup.clear();
    process (processor, warmup);
    const auto started = processor.getLinearPhaseDesigns();
    const auto deadline = juce::Time::getMillisecondCounter() + 2000;
    while (processor.getLinearPhaseDesigns() == started && juce::Time::getMillisecondCounter() < deadline)
        juce::Thread::sleep (10);
    flushLinearPhase (processor);
    juce::AudioBuffer<float> impulse (2, 4096);
    impulse.clear();
    impulse.setSample (0, 0, 1.0f);
    process (processor, impulse);
    return std::vector<float> (impulse.getReadPointer (0), impulse.getReadPointer (0) + 4096);
}

static void checkPhaseModes()
{
    const double sampleRate = 48000.0;

    // Zero-latency analogue: no reported latency, causal impulse (energy arrives at the start).
    {
        OpenEQAudioProcessor processor;
        silenceBands (processor);
        configureBand (processor, 0, FilterType::Bell, 300.0f, 6.0f, 1.0f, 1, (int) StereoMode::Stereo, true);
        setRaw (processor.apvts, "processingMode", 4.0f);
        prepareStereo (processor, sampleRate, 512);
        require (processor.getReportedLatencySamples() == 0, "analog ZL mode should report zero latency");
    }
    {
        const auto zl = settledImpulseForPhase (0.0f, 4);
        const auto linear = settledImpulseForPhase (0.0f, 1);
        const int zlPeak = peakIndex (zl.data(), (int) zl.size());
        const int linearPeak = peakIndex (linear.data(), (int) linear.size());
        std::cout << "  zl_peak " << zlPeak << " linear_peak " << linearPeak << "\n";
        requireMsg (zlPeak < 512, "analog ZL impulse should be causal (peak near the start)");
        // Low quality = 2048-tap kernel = 1024 samples of latency, where the linear impulse peaks.
        requireMsg (linearPeak > 900 && linearPeak < 1200, "linear phase impulse should sit at its latency");
        const double zl300 = juce::Decibels::gainToDecibels (impulseMagnitude (zl.data(), 4096, 300.0, sampleRate));
        const double lin300 = juce::Decibels::gainToDecibels (impulseMagnitude (linear.data(), 4096, 300.0, sampleRate));
        std::cout << "  zl_300_db " << zl300 << " linear_300_db " << lin300 << "\n";
        requireMsg (std::abs (zl300 - 6.0) < 0.8, "analog ZL should keep the boost magnitude at 300 Hz");
        requireMsg (std::abs (lin300 - 6.0) < 0.8, "linear phase should keep the boost magnitude at 300 Hz");
    }

    // Free phase: linear symmetric, minimum one-sided right, inverse one-sided left, same total energy.
    {
        const auto linear = settledImpulseForPhase (0.0f, 1);
        const auto minimum = settledImpulseForPhase (1.0f, 1);
        const auto inverse = settledImpulseForPhase (-1.0f, 1);
        auto asymmetry = [] (const std::vector<float>& h)
        {
            const int peak = peakIndex (h.data(), (int) h.size());
            double diff = 0.0, energy = 0.0;
            for (int k = 1; k < 1500 && peak - k >= 0 && peak + k < (int) h.size(); ++k)
            {
                const double a = h[(size_t) (peak + k)];
                const double b = h[(size_t) (peak - k)];
                diff += (a - b) * (a - b);
                energy += a * a + b * b;
            }
            return energy > 0.0 ? diff / energy : 0.0;
        };
        const double linearAsymmetry = asymmetry (linear);
        const double minimumAsymmetry = asymmetry (minimum);
        const double inverseAsymmetry = asymmetry (inverse);
        std::cout << "  phase_asym_linear " << linearAsymmetry << " min " << minimumAsymmetry << " inv " << inverseAsymmetry << "\n";
        require (linearAsymmetry < 1.0e-3, "linear phase should stay symmetric");
        requireMsg (minimumAsymmetry > 0.1, "minimum free phase should be one-sided");
        requireMsg (inverseAsymmetry > 0.1, "inverse free phase should be one-sided");
        auto energy = [] (const std::vector<float>& h)
        {
            double e = 0.0;
            for (float v : h) e += (double) v * v;
            return e;
        };
        const double eLin = energy (linear), eMin = energy (minimum), eInv = energy (inverse);
        requireMsg (std::abs (eMin - eLin) < 0.05 * eLin, "free phase should keep the impulse energy");
        requireMsg (std::abs (eInv - eLin) < 0.05 * eLin, "inverse free phase should keep the impulse energy");
        // Minimum and inverse phases sit on opposite sides of the peak.
        auto sideBias = [] (const std::vector<float>& h)
        {
            const int peak = peakIndex (h.data(), (int) h.size());
            double before = 0.0, after = 0.0;
            for (int k = 1; k <= 1000; ++k)
            {
                if (peak - k >= 0) before += (double) h[(size_t) (peak - k)] * h[(size_t) (peak - k)];
                if (peak + k < (int) h.size()) after += (double) h[(size_t) (peak + k)] * h[(size_t) (peak + k)];
            }
            return before > 1.0e-12 ? after / before : 0.0;
        };
        const double minBias = sideBias (minimum);
        const double invBias = sideBias (inverse);
        std::cout << "  side_bias_min " << minBias << " inv " << invBias << "\n";
        requireMsg (invBias > minBias, "inverse phase should mirror the minimum phase around the peak");
    }

    // Window shape and parameter: the design completes and keeps the magnitude within tolerance.
    {
        OpenEQAudioProcessor kaiser;
        silenceBands (kaiser);
        configureBand (kaiser, 0, FilterType::Bell, 300.0f, 6.0f, 1.0f, 1, (int) StereoMode::Stereo, true);
        setRaw (kaiser.apvts, "processingMode", 1.0f);
        setRaw (kaiser.apvts, "linearPhaseQuality", 0.0f);
        setRaw (kaiser.apvts, "windowShape", 1.0f);     // Kaiser
        setRaw (kaiser.apvts, "windowParameter", 1.0f); // beta 14
        prepareStereo (kaiser, 48000.0, 512);
        juce::AudioBuffer<float> warmup (2, 256);
        warmup.clear();
        process (kaiser, warmup);
        const auto started = kaiser.getLinearPhaseDesigns();
        const auto deadline = juce::Time::getMillisecondCounter() + 2000;
        while (kaiser.getLinearPhaseDesigns() == started && juce::Time::getMillisecondCounter() < deadline)
            juce::Thread::sleep (10);
        flushLinearPhase (kaiser);
        juce::AudioBuffer<float> impulse (2, 4096);
        impulse.clear();
        impulse.setSample (0, 0, 1.0f);
        process (kaiser, impulse);
        require (allFinite (impulse), "Kaiser window render produced a non-finite sample");
        const double k300 = juce::Decibels::gainToDecibels (impulseMagnitude (impulse.getReadPointer (0), 4096, 300.0, sampleRate));
        std::cout << "  kaiser_300_db " << k300 << "\n";
        requireMsg (std::abs (k300 - 6.0) < 0.8, "the Kaiser window should keep the boost magnitude at 300 Hz");
    }

    // Impulse padding: shorter padding keeps the magnitude within tolerance.
    {
        auto runPadding = [&] (int padding)
        {
            OpenEQAudioProcessor processor;
            silenceBands (processor);
            configureBand (processor, 0, FilterType::Bell, 300.0f, 6.0f, 1.0f, 1, (int) StereoMode::Stereo, true);
            setRaw (processor.apvts, "processingMode", 1.0f);
            setRaw (processor.apvts, "linearPhaseQuality", 0.0f);
            setRaw (processor.apvts, "impulsePadding", (float) padding);
            prepareStereo (processor, 48000.0, 512);
            juce::AudioBuffer<float> warmup (2, 256);
            warmup.clear();
            process (processor, warmup);
            const auto started = processor.getLinearPhaseDesigns();
            const auto deadline = juce::Time::getMillisecondCounter() + 2000;
            while (processor.getLinearPhaseDesigns() == started && juce::Time::getMillisecondCounter() < deadline)
                juce::Thread::sleep (10);
            flushLinearPhase (processor);
            juce::AudioBuffer<float> impulse (2, 4096);
            impulse.clear();
            impulse.setSample (0, 0, 1.0f);
            process (processor, impulse);
            return juce::Decibels::gainToDecibels (impulseMagnitude (impulse.getReadPointer (0), 4096, 300.0, sampleRate));
        };
        const double shortDb = runPadding (0);
        const double fullDb = runPadding (2);
        std::cout << "  padding_short_300_db " << shortDb << " full " << fullDb << "\n";
        requireMsg (std::abs (shortDb - 6.0) < 0.9, "short impulse padding should keep the boost magnitude");
        requireMsg (std::abs (fullDb - 6.0) < 0.9, "full impulse padding should keep the boost magnitude");
    }

    // Digital+: phase-compensating all-pass steps bring the phase response closer to a straight line
    // (the fit objective): the deviation from the best-fit phase line over 0.5..2 fc must shrink.
    {
        auto phaseLineDeviation = [] (int steps)
        {
            EQBand band;
            juce::dsp::ProcessSpec spec { 48000.0, 512, 1 };
            band.prepare (spec);
            band.setDigitalPlusSteps (steps);
            band.applyIfChanged (FilterType::Bell, 1000.0f, 6.0f, 1.0f, 12);
            std::array<ResponseModel::NormalizedStage, ResponseModel::maxStages> stages {};
            int count = 0;
            band.exportStages (stages, count);
            auto phaseAt = [&] (double f)
            {
                double phase = 0.0;
                for (int i = 0; i < count; ++i)
                    phase += ResponseModel::stagePhase (stages[(size_t) i], f, 48000.0);
                return phase;
            };
            double freqs[5], phases[5];
            for (int k = 0; k < 5; ++k)
            {
                freqs[k] = 1000.0 * (0.5 + 0.375 * (double) k);
                phases[k] = phaseAt (freqs[k]);
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
            double deviation = 0.0;
            for (int k = 0; k < 5; ++k)
            {
                const double d = phases[k] - (slope * freqs[k] + intercept);
                deviation += d * d;
            }
            return deviation;
        };
        const double deviation0 = phaseLineDeviation (0);
        const double deviation4 = phaseLineDeviation (4);
        std::cout << "  digitalplus_phase_dev_0 " << deviation0 << " dev_4 " << deviation4 << "\n";
        requireMsg (deviation4 < deviation0 * 0.9, "Digital+ steps should bring the phase response closer to linear");
    }

    // Render DSP: the render quality overrides the realtime quality only while rendering offline.
    {
        OpenEQAudioProcessor processor;
        setRaw (processor.apvts, "linearPhaseQuality", 1.0f); // Medium realtime
        setRaw (processor.apvts, "renderQuality", 4.0f);      // Maximum offline
        require (processor.effectiveLinearPhaseQuality() == 1, "realtime should use the realtime quality");
        processor.setNonRealtime (true);
        require (processor.effectiveLinearPhaseQuality() == 3, "offline render should use the render quality");
        processor.setNonRealtime (false);
        require (processor.effectiveLinearPhaseQuality() == 1, "returning to realtime should restore the realtime quality");
    }

    std::cout << "PASS: zero-latency analogue, free phase, window controls, and render DSP\n";
}

static void checkSplitMode()
{
    // Split separates the audio into a transient stream and a tonal stream that add back to the input.
    // Each stream runs through its own copy of the bands. The separation looks ahead, so it adds latency.
    const double sampleRate = 48000.0;
    const int length = 24000;
    const int clickA = 9000;
    const int clickB = 15000;

    // A steady 1 kHz tone with two clicks on top of it: the two kinds of material overlap in time and in frequency.
    auto makeInput = [&] (bool tone, bool clicks)
    {
        juce::AudioBuffer<float> buffer (2, length);
        buffer.clear();
        if (tone)
            fillToneRange (buffer, sampleRate, 1000.0, 0.25f, 0, length, 0, Tone::Centered);
        if (clicks)
            for (int at : { clickA, clickB })
                for (int channel = 0; channel < 2; ++channel)
                    buffer.addSample (channel, at, 0.8f);
        return buffer;
    };

    auto makeSplit = [&] (OpenEQAudioProcessor& processor, float gainDb, float transDb, float tailDb, int solo)
    {
        silenceBands (processor);
        configureBand (processor, 0, FilterType::Bell, 1000.0f, gainDb, 1.0f, 1, (int) StereoMode::Stereo, true);
        setRaw (processor.apvts, Params::bandSplitOnID (0), 1.0f);
        setRaw (processor.apvts, Params::bandSplitTransID (0), transDb);
        setRaw (processor.apvts, Params::bandSplitTailID (0), tailDb);
        setRaw (processor.apvts, "splitSolo", (float) solo);
        prepareStereo (processor, sampleRate, 512);
    };

    auto peakAround = [] (const juce::AudioBuffer<float>& buffer, int centre, int radius)
    {
        float peak = 0.0f;
        for (int i = juce::jmax (0, centre - radius); i < juce::jmin (buffer.getNumSamples(), centre + radius); ++i)
            peak = juce::jmax (peak, std::abs (buffer.getSample (0, i)));
        return (double) peak;
    };

    // Latency: split reports the separator delay, and nothing without a split band.
    int latency = 0;
    {
        OpenEQAudioProcessor processor;
        makeSplit (processor, 0.0f, 0.0f, 0.0f, 0);
        auto input = makeInput (true, true);
        process (processor, input);
        latency = processor.getReportedLatencySamples();
        std::cout << "  split_latency " << latency << "\n";
        requireMsg (latency > 1000 && latency < 4096, "split should report its separator latency (got " + std::to_string (latency) + ")");

        // Reconstruction: with every gain at 0 dB the two streams add back to the input, late by the latency.
        const auto dry = makeInput (true, true);
        double maxDiff = 0.0;
        for (int i = 0; i + latency < length; ++i)
            maxDiff = juce::jmax (maxDiff, (double) std::abs (input.getSample (0, i + latency) - dry.getSample (0, i)));
        std::cout << "  split_reconstruction_max_abs " << maxDiff << "\n";
        requireMsg (maxDiff < 1.0e-5, "the two streams should add back to the input (max " + std::to_string (maxDiff) + ")");

        // The path leaves after a short fade, so the latency drops a block or two later.
        setRaw (processor.apvts, Params::bandSplitOnID (0), 0.0f);
        juce::AudioBuffer<float> next (2, 512);
        for (int i = 0; i < 4; ++i)
        {
            next.clear();
            process (processor, next);
        }
        require (processor.getReportedLatencySamples() == 0, "split off should report no latency");
    }

    // Streams a steady tone in 256-sample blocks and returns the largest step between neighbouring output
    // samples. The tone itself steps by at most 0.033, so a click or a hard cut shows as a larger step.
    auto streamTone = [&] (OpenEQAudioProcessor& processor, int blocks, int& phase, double& largestStep, float& last,
                           int* silentSamples = nullptr)
    {
        juce::AudioBuffer<float> block (2, 256);
        for (int b = 0; b < blocks; ++b)
        {
            fillTone (block, sampleRate, 1000.0, 0.25f, phase, Tone::Centered);
            phase += 256;
            process (processor, block);
            for (int i = 0; i < 256; ++i)
            {
                const float sample = block.getSample (0, i);
                largestStep = juce::jmax (largestStep, (double) std::abs (sample - last));
                if (silentSamples != nullptr && std::abs (sample) < 1.0e-6f)
                    ++*silentSamples;
                last = sample;
            }
        }
    };

    // Turning Split on and off while audio plays changes the latency. The output fades around the change.
    {
        OpenEQAudioProcessor processor;
        silenceBands (processor);
        configureBand (processor, 0, FilterType::Bell, 1000.0f, 3.0f, 1.0f, 1, (int) StereoMode::Stereo, true);
        prepareStereo (processor, sampleRate, 256);
        int phase = 0;
        double step = 0.0;
        float last = 0.0f;
        streamTone (processor, 40, phase, step, last);
        const double toneStep = step;
        step = 0.0;
        setRaw (processor.apvts, Params::bandSplitOnID (0), 1.0f);
        streamTone (processor, 60, phase, step, last);
        const double onStep = step;
        require (processor.getReportedLatencySamples() == latency, "turning Split on should bring its latency in");
        step = 0.0;
        setRaw (processor.apvts, Params::bandSplitOnID (0), 0.0f);
        streamTone (processor, 60, phase, step, last);
        std::cout << "  split_toggle_step tone " << toneStep << " on " << onStep << " off " << step << "\n";
        require (processor.getReportedLatencySamples() == 0, "turning Split off should take its latency out");
        requireMsg (onStep < toneStep * 1.2, "turning Split on must not click");
        requireMsg (step < toneStep * 1.2, "turning Split off must not click");
    }

    // A/B: with Split in one slot only, both slots keep the split latency, so switching does not change the
    // latency, does not drop out, and does not click.
    {
        OpenEQAudioProcessor processor;
        silenceBands (processor);
        configureBand (processor, 0, FilterType::Bell, 1000.0f, 3.0f, 1.0f, 1, (int) StereoMode::Stereo, true);
        prepareStereo (processor, sampleRate, 256);
        processor.switchAB();   // now in B, a copy of A
        setRaw (processor.apvts, Params::bandSplitOnID (0), 1.0f);
        setRaw (processor.apvts, Params::bandSplitTransID (0), 6.0f);
        int phase = 0;
        double step = 0.0;
        float last = 0.0f;
        streamTone (processor, 80, phase, step, last);
        require (processor.getReportedLatencySamples() == latency, "slot B should run with the split latency");

        step = 0.0;
        int silent = 0;
        for (int round = 0; round < 4; ++round)
        {
            processor.switchAB();
            streamTone (processor, 30, phase, step, last, &silent);
            requireMsg (processor.getReportedLatencySamples() == latency,
                        "A/B switching must keep the latency while either slot uses Split");
        }
        std::cout << "  split_ab_step " << step << " silent_samples " << silent << "\n";
        requireMsg (step < 0.25 * 2.0 * juce::MathConstants<double>::pi * 1000.0 / sampleRate * 1.6, "A/B switching must not click");
        requireMsg (silent < 8, "A/B switching must not drop out");
    }

    // Null test: a split band with both stream gains at 0 dB is the plain band, late by the latency.
    {
        OpenEQAudioProcessor plain;
        silenceBands (plain);
        configureBand (plain, 0, FilterType::Bell, 1000.0f, 6.0f, 1.0f, 1, (int) StereoMode::Stereo, true);
        prepareStereo (plain, sampleRate, 512);
        OpenEQAudioProcessor split;
        makeSplit (split, 6.0f, 0.0f, 0.0f, 0);

        auto inA = makeInput (true, true);
        auto inB = makeInput (true, true);
        process (plain, inA);
        process (split, inB);
        double maxDiff = 0.0;
        for (int i = 0; i + latency < length; ++i)
            maxDiff = juce::jmax (maxDiff, (double) std::abs (inA.getSample (0, i) - inB.getSample (0, i + latency)));
        std::cout << "  split_null_max_abs " << maxDiff << "\n";
        requireMsg (maxDiff < 2.0e-4, "split with unity gains should null against the plain band (max " + std::to_string (maxDiff) + ")");
    }

    // Separation of overlapping material: the transient solo keeps the clicks and drops the tone under them,
    // and the tonal solo keeps the tone and drops the clicks.
    const int steadyStart = 11000;
    const int steadyCount = 2000;
    const double toneRms = 0.25 / std::sqrt (2.0);
    {
        auto runSolo = [&] (int solo)
        {
            OpenEQAudioProcessor processor;
            makeSplit (processor, 0.0f, 0.0f, 0.0f, solo);
            auto input = makeInput (true, true);
            process (processor, input);
            require (allFinite (input), "split solo produced a non-finite sample");
            return input;
        };
        const auto transientSolo = runSolo (1);
        const auto tonalSolo = runSolo (2);

        const double toneInTransient = bufferRms (transientSolo, 0, steadyStart + latency, steadyCount);
        const double toneInTonal = bufferRms (tonalSolo, 0, steadyStart + latency, steadyCount);
        const double clickInTransient = peakAround (transientSolo, clickA + latency, 8);
        const double clickInTonal = peakAround (tonalSolo, clickA + latency, 8);
        std::cout << "  split_tone_in_transient_db " << 20.0 * std::log10 (juce::jmax (1.0e-9, toneInTransient / toneRms))
                  << " tone_in_tonal_db " << 20.0 * std::log10 (juce::jmax (1.0e-9, toneInTonal / toneRms))
                  << " click_in_transient " << clickInTransient
                  << " click_in_tonal " << clickInTonal << "\n";
        requireMsg (toneInTransient < toneRms * 0.03, "the transient stream should not carry a steady tone");
        requireMsg (std::abs (20.0 * std::log10 (toneInTonal / toneRms)) < 0.3, "the tonal stream should carry the steady tone");
        requireMsg (clickInTransient > 0.5, "the transient stream should carry the click");
        requireMsg (clickInTonal < 0.45, "the tonal stream should not carry the click");
    }

    // Stream gains: the transient gain moves the click and leaves the tone alone, and the tonal gain does the reverse.
    {
        auto run = [&] (float transDb, float tailDb, bool tone, bool clicks)
        {
            OpenEQAudioProcessor processor;
            makeSplit (processor, 0.0f, transDb, tailDb, 0);
            auto input = makeInput (tone, clicks);
            process (processor, input);
            require (allFinite (input), "split render produced a non-finite sample");
            return input;
        };
        // Clicks alone: the band is one octave of a full-range click, so the level change is small but clear.
        const double clickBoosted = bufferRms (run (12.0f, -12.0f, false, true), 0, clickA + latency - 256, 512);
        const double clickCut = bufferRms (run (-12.0f, 12.0f, false, true), 0, clickA + latency - 256, 512);
        // Tone and clicks together: the tone follows the tonal gain while the clicks play through it.
        const double toneCut = bufferRms (run (12.0f, -12.0f, true, true), 0, steadyStart + latency, steadyCount);
        const double toneBoosted = bufferRms (run (-12.0f, 12.0f, true, true), 0, steadyStart + latency, steadyCount);
        std::cout << "  split_gain_click_rms " << clickBoosted << " / " << clickCut
                  << " tone_db " << 20.0 * std::log10 (toneCut / toneRms)
                  << " / " << 20.0 * std::log10 (toneBoosted / toneRms) << "\n";
        requireMsg (clickBoosted > clickCut * 1.25, "boosting the transient stream must lift the click");
        requireMsg (std::abs (20.0 * std::log10 (toneCut / toneRms) + 12.0) < 1.0, "a -12 dB tonal gain must lower the tone by 12 dB");
        requireMsg (std::abs (20.0 * std::log10 (toneBoosted / toneRms) - 12.0) < 1.0, "a +12 dB tonal gain must lift the tone by 12 dB");
    }

    // State round trip keeps the split settings.
    {
        OpenEQAudioProcessor processor;
        setRaw (processor.apvts, Params::bandSplitOnID (3), 1.0f);
        setRaw (processor.apvts, Params::bandSplitTransID (3), 4.5f);
        setRaw (processor.apvts, Params::bandSplitTailID (3), -7.0f);
        juce::MemoryBlock state;
        processor.getStateInformation (state);
        OpenEQAudioProcessor restored;
        restored.setStateInformation (state.getData(), (int) state.getSize());
        require (rawValue (restored, Params::bandSplitOnID (3)) == 1.0f, "split on must survive state round trip");
        requireMsg (std::abs (rawValue (restored, Params::bandSplitTransID (3)) - 4.5f) < 0.01f, "transient gain must round trip");
        requireMsg (std::abs (rawValue (restored, Params::bandSplitTailID (3)) + 7.0f) < 0.01f, "tail gain must round trip");
    }

    // Out of band: stream gains at 1 kHz must not change a 100 Hz tone.
    {
        OpenEQAudioProcessor processor;
        silenceBands (processor);
        configureBand (processor, 0, FilterType::Bell, 1000.0f, 0.0f, 2.0f, 1, (int) StereoMode::Stereo, true);
        setRaw (processor.apvts, Params::bandSplitOnID (0), 1.0f);
        setRaw (processor.apvts, Params::bandSplitTransID (0), 12.0f);
        setRaw (processor.apvts, Params::bandSplitTailID (0), -12.0f);
        prepareStereo (processor, sampleRate, 512);

        juce::AudioBuffer<float> in (2, 12000), dry (2, 12000);
        fillTone (in, 48000.0, 100.0, 0.3f, 40, Tone::Centered);
        dry = in;
        process (processor, in);
        const double rmsIn = bufferRms (in, 0, 6000, 4800);
        const double rmsDry = bufferRms (dry, 0, 6000, 4800);
        const double deltaDb = std::abs (20.0 * std::log10 (rmsIn / rmsDry));
        std::cout << "  split_out_of_band_delta_db " << deltaDb << "\n";
        requireMsg (deltaDb < 0.1, "out-of-band 100 Hz signal must remain untouched within 0.1 dB (delta " + std::to_string (deltaDb) + " dB)");
    }

    // Mid/Side and mono: the streams still add back to the input.
    {
        OpenEQAudioProcessor processor;
        silenceBands (processor);
        configureBand (processor, 0, FilterType::Bell, 1000.0f, 0.0f, 1.0f, 1, (int) StereoMode::Side, true);
        setRaw (processor.apvts, Params::bandSplitOnID (0), 1.0f);
        prepareStereo (processor, sampleRate, 512);
        auto input = makeInput (true, true);
        for (int i = 0; i < length; ++i)
            input.setSample (1, i, 0.5f * input.getSample (1, i));
        const auto dry = copyBuffer (input);
        process (processor, input);
        double maxDiff = 0.0;
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i + latency < length; ++i)
                maxDiff = juce::jmax (maxDiff, (double) std::abs (input.getSample (channel, i + latency) - dry.getSample (channel, i)));
        std::cout << "  split_side_reconstruction_max_abs " << maxDiff << "\n";
        requireMsg (maxDiff < 1.0e-5, "a Side split band at unity should pass the input (max " + std::to_string (maxDiff) + ")");
    }

    // Every other latency change gets the same fade: lookahead, drive, spectral, and the processing mode.
    {
        struct Switch { const char* name; const char* id; float on; float off; };
        const Switch switches[] = { { "lookahead", "lookaheadOn", 1.0f, 0.0f },
                                    { "drive", "drive", 0.3f, 0.0f },
                                    { "spectral", "spectralAmount", 0.5f, 0.0f },
                                    { "linear_phase", "processingMode", 1.0f, 0.0f },
                                    { "analog_zl", "processingMode", 4.0f, 0.0f } };
        for (const auto& item : switches)
        {
            OpenEQAudioProcessor processor;
            silenceBands (processor);
            configureBand (processor, 0, FilterType::Bell, 1000.0f, 3.0f, 1.0f, 1, (int) StereoMode::Stereo, true);
            setRaw (processor.apvts, "spectralThresh", 0.0f);
            prepareStereo (processor, sampleRate, 256);
            int phase = 0;
            double step = 0.0;
            float last = 0.0f;
            streamTone (processor, 40, phase, step, last);
            const double toneStep = step;
            const int latencyBefore = processor.getReportedLatencySamples();
            step = 0.0;
            setRaw (processor.apvts, item.id, item.on);
            streamTone (processor, 200, phase, step, last);
            const double onStep = step;
            const int latencyOn = processor.getReportedLatencySamples();
            step = 0.0;
            setRaw (processor.apvts, item.id, item.off);
            streamTone (processor, 200, phase, step, last);
            std::cout << "  latency_switch " << item.name << " latency " << latencyBefore << " -> " << latencyOn
                      << " -> " << processor.getReportedLatencySamples()
                      << " step tone " << toneStep << " on " << onStep << " off " << step << "\n";
            requireMsg (processor.getReportedLatencySamples() == latencyBefore,
                        std::string (item.name) + ": switching back should restore the latency");
            requireMsg (onStep < 0.08 && step < 0.08, std::string (item.name) + ": a latency change must not click");
        }
    }

    std::cout << "PASS: split separates overlapping transient and tonal material, reconstructs, and reports latency\n";
}

static void checkNoteAndValueFormats()
{
    require (ValueFormat::noteName (440.0f) == "A4", "440 Hz should read A4");
    require (ValueFormat::noteName (261.6256f) == "C4", "middle C should read C4");
    require (ValueFormat::noteName (880.0f) == "A5", "880 Hz should read A5");
    require (ValueFormat::noteName (27.5f) == "A0", "27.5 Hz should read A0");
    require (ValueFormat::noteName (0.0f).isEmpty(), "non-positive frequency has no note");
    require (ValueFormat::frequencyWithNote (880.0f) == "880 Hz  A5", "frequency readout should include the note");
    require (ValueFormat::pan (0.0f) == "C", "centre pan should read C");
    require (ValueFormat::pan (-30.0f) == "L30", "left pan should read L30");
    require (ValueFormat::pan (12.0f) == "R12", "right pan should read R12");
    require (ValueFormat::sideDB (-100.0f) == "-inf", "side floor should read -inf");

    // Display policy: three digits at most, whole dynamics steps, no 0.1 ms attack.
    require (ValueFormat::qFactor (0.7f) == "0.70", "low Q should keep two decimals");
    require (ValueFormat::qFactor (8.5f) == "8.50", "mid Q should keep two decimals");
    require (ValueFormat::qFactor (40.0f) == "40.0", "high Q must stop at three digits");
    require (ValueFormat::timeMs (15.0f) == "15 ms", "attack should read whole milliseconds");
    require (ValueFormat::timeMs (150.0f) == "150 ms", "release should read whole milliseconds");
    require (ValueFormat::timeMs (1500.0f) == "1.50 s", "long times should read in seconds");
    require (ValueFormat::timeMs (0.4f) == "<1 ms", "sub-millisecond times must not show a fraction");
    require (Params::bandDynBelowRangeID (0).isNotEmpty(), "dynamics range IDs should exist");

    std::cout << "PASS: note names, knob value formats, and numeric display policy\n";
}

static void checkCompactSliderClickMapping()
{
    const auto clickAt = [] (juce::Component& c, float x, float y)
    {
        const auto now = juce::Time::getCurrentTime();
        const juce::Point<float> pos (x, y);
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(),
                                 pos,
                                 juce::ModifierKeys(),
                                 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                 &c, &c,
                                 now,
                                 pos,
                                 now,
                                 1,
                                 false);
    };

    // 1. Horizontal CompactSlider (e.g. FREQ / GAIN fader in floating HUD: 316x52)
    {
        CompactSlider slider ("GAIN", SliderValueType::Gain, Theme::stereoCol, juce::Slider::LinearHorizontal);
        slider.setRange (-30.0, 30.0, 0.1);
        slider.setValue (0.0, juce::dontSendNotification);
        slider.setBounds (0, 0, 316, 52);

        const auto slot = slider.getTrackSlotBounds();
        requireMsg (slot.getWidth() > 100, "horizontal groove must be wider than 100px");

        const float midX = (float) slot.getCentreX();
        const float midY = (float) slot.getCentreY();
        const float leftX = (float) slot.getX();
        const float rightX = (float) slot.getRight();

        // Click middle of groove -> lands near 0.0 dB (50% of -30..+30)
        slider.setValue (-30.0, juce::dontSendNotification);
        slider.mouseDown (clickAt (slider, midX, midY));
        requireMsg (std::abs (slider.getValue() - 0.0) < 1.5,
                    "horizontal click in middle of groove must land near 0 dB, got " + std::to_string (slider.getValue()));

        // Click near left of groove (25% across) -> must land near -15 dB, NOT jump to maximum (+30 dB)
        slider.setValue (0.0, juce::dontSendNotification);
        const float quarterX = leftX + (rightX - leftX) * 0.25f;
        slider.mouseDown (clickAt (slider, quarterX, midY));
        requireMsg (std::abs (slider.getValue() - (-15.0)) < 2.0,
                    "horizontal click at 25% must land near -15 dB (got " + std::to_string (slider.getValue()) + "), not jump to maximum");

        // Click left end of groove -> minimum (-30 dB)
        slider.setValue (0.0, juce::dontSendNotification);
        slider.mouseDown (clickAt (slider, leftX, midY));
        requireMsg (std::abs (slider.getValue() - (-30.0)) < 0.5,
                    "horizontal click at left end must be -30 dB, got " + std::to_string (slider.getValue()));

        // Click right end of groove -> maximum (+30 dB)
        slider.setValue (0.0, juce::dontSendNotification);
        slider.mouseDown (clickAt (slider, rightX, midY));
        requireMsg (std::abs (slider.getValue() - 30.0) < 0.5,
                    "horizontal click at right end must be +30 dB, got " + std::to_string (slider.getValue()));

        // Click on title label (left of track, e.g. x=10) -> must NOT change value
        slider.setValue (5.0, juce::dontSendNotification);
        slider.mouseDown (clickAt (slider, 10.0f, midY));
        requireMsg (std::abs (slider.getValue() - 5.0) < 0.01,
                    "click on title label must not jump slider value");
    }

    // 2. Vertical CompactSlider (e.g. Q / SLOPE or Dynamics fader: 102x110)
    {
        CompactSlider slider ("Q", SliderValueType::Q, Theme::stereoCol, juce::Slider::LinearVertical);
        slider.setRange (0.1, 10.0, 0.01);
        slider.setValue (1.0, juce::dontSendNotification);
        slider.setBounds (0, 0, 102, 110);

        const auto slot = slider.getTrackSlotBounds();
        requireMsg (slot.getHeight() > 50, "vertical groove must be taller than 50px");

        const float midX = (float) slot.getCentreX();
        const float midY = (float) slot.getCentreY();
        const float topY = (float) slot.getY();
        const float botY = (float) slot.getBottom();

        // Click middle of vertical groove -> lands near midpoint (0.1 + 9.9*0.5 = 5.05)
        slider.setValue (0.1, juce::dontSendNotification);
        slider.mouseDown (clickAt (slider, midX, midY));
        requireMsg (std::abs (slider.getValue() - 5.05) < 0.6,
                    "vertical click in middle of groove must land near midpoint ~5.05, got " + std::to_string (slider.getValue()));

        // Click near bottom (25% up from bottom) -> must land near 0.1 + 9.9*0.25 = 2.57, NOT jump to maximum (10.0)
        slider.setValue (10.0, juce::dontSendNotification);
        const float quarterY = botY - (botY - topY) * 0.25f;
        slider.mouseDown (clickAt (slider, midX, quarterY));
        requireMsg (std::abs (slider.getValue() - 2.575) < 0.6,
                    "vertical click at 25% height must land near 2.57 (got " + std::to_string (slider.getValue()) + "), not jump to maximum");

        // Click bottom of groove -> minimum (0.1)
        slider.setValue (5.0, juce::dontSendNotification);
        slider.mouseDown (clickAt (slider, midX, botY));
        requireMsg (std::abs (slider.getValue() - 0.1) < 0.1,
                    "vertical click at bottom must be 0.1, got " + std::to_string (slider.getValue()));

        // Click top of groove -> maximum (10.0)
        slider.setValue (5.0, juce::dontSendNotification);
        slider.mouseDown (clickAt (slider, midX, topY));
        requireMsg (std::abs (slider.getValue() - 10.0) < 0.1,
                    "vertical click at top must be 10.0, got " + std::to_string (slider.getValue()));

        // Click on title label (top of control, e.g. y=6) -> must NOT change value
        slider.setValue (2.0, juce::dontSendNotification);
        slider.mouseDown (clickAt (slider, midX, 6.0f));
        requireMsg (std::abs (slider.getValue() - 2.0) < 0.01,
                    "click on top title label must not jump slider value");
    }

    std::cout << "PASS: CompactSlider groove mapping and click hit-testing (no jump-to-maximum)\n";
}

// Resonance suppressor: reconstruction, what it cuts, what it leaves alone, level independence and delta.
static void checkResonanceSuppressor()
{
    const double sr = 48000.0;
    const int length = 48000 * 4;

    auto makeNoise = [&] (float slopeDbPerOct, float levelDb, std::uint32_t seed)
    {
        // Noise with a spectral slope, from a bank of first-order sections fed with white noise.
        std::vector<float> x ((size_t) length);
        juce::Random random ((juce::int64) seed);
        for (auto& v : x) v = random.nextFloat() * 2.0f - 1.0f;
        if (slopeDbPerOct < -1.0f)
        {
            // Paul Kellet's pink filter.
            float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
            for (auto& v : x)
            {
                const float w = v;
                b0 = 0.99886f * b0 + w * 0.0555179f; b1 = 0.99332f * b1 + w * 0.0750759f; b2 = 0.96900f * b2 + w * 0.1538520f;
                b3 = 0.86650f * b3 + w * 0.3104856f; b4 = 0.55000f * b4 + w * 0.5329522f; b5 = -0.7616f * b5 - w * 0.0168980f;
                v = (b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f) * 0.11f; b6 = w * 0.115926f;
            }
        }
        double sum = 0.0;
        for (auto v : x) sum += (double) v * v;
        const float scale = juce::Decibels::decibelsToGain (levelDb) / (float) std::sqrt (sum / length);
        for (auto& v : x) v *= scale;
        return x;
    };
    auto addResonance = [&] (std::vector<float>& x, double freq, double gainDb, double q)
    {
        const double a = std::pow (10.0, gainDb / 40.0), w = juce::MathConstants<double>::twoPi * freq / sr, alpha = std::sin (w) / (2.0 * q);
        const double b0 = 1 + alpha * a, b1 = -2 * std::cos (w), b2 = 1 - alpha * a, a0 = 1 + alpha / a, a1 = b1, a2 = 1 - alpha / a;
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        for (auto& v : x)
        {
            const double y = (b0 * v + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2) / a0;
            x2 = x1; x1 = v; y2 = y1; y1 = y; v = (float) y;
        }
    };
    // Level in a third-octave band over the last two seconds, in dB.
    auto bandLevel = [&] (const std::vector<float>& x, double freq, int delay)
    {
        const double lo = freq * std::pow (2.0, -1.0 / 6.0), hi = freq * std::pow (2.0, 1.0 / 6.0);
        const int start = length / 2;
        double power = 0.0;
        for (double f = lo; f <= hi; f += (hi - lo) / 12.0)
        {
            double re = 0, im = 0;
            const double w = juce::MathConstants<double>::twoPi * f / sr;
            for (int i = start; i < length - 4096; ++i)
            {
                const double v = x[(size_t) juce::jmin (length - 1, i + delay)];
                re += v * std::cos (w * i); im += v * std::sin (w * i);
            }
            power += re * re + im * im;
        }
        return 10.0 * std::log10 (power + 1.0e-30);
    };
    auto run = [&] (std::vector<float> x, const ResonanceEngine::Settings& settings)
    {
        ResonanceEngine engine;
        engine.prepare (sr);
        auto right = x;
        for (int pos = 0; pos < length; pos += 512)
            engine.process (x.data() + pos, right.data() + pos, juce::jmin (512, length - pos), settings);
        return x;
    };

    ResonanceEngine probe;
    probe.prepare (sr);
    const int latency = probe.getLatencySamples();
    require (latency == 2047, "Resonance engine latency is not fftSize - 1 at 48 kHz");

    // Depth 0 reconstructs the input.
    {
        ResonanceEngine::Settings off;
        auto dry = makeNoise (-3.0f, -20.0f, 3);
        auto wet = run (dry, off);
        double error = 0, reference = 0;
        for (int i = 4096; i < length - latency; ++i)
        {
            const double d = (double) wet[(size_t) (i + latency)] - dry[(size_t) i];
            error += d * d; reference += (double) dry[(size_t) i] * dry[(size_t) i];
        }
        const double snr = 10.0 * std::log10 (reference / (error + 1.0e-30));
        std::cout << "  resonance_unity_snr " << snr << "\n";
        require (snr > 80.0, "Resonance engine does not reconstruct the input at depth 0");
    }

    ResonanceEngine::Settings on;
    on.depth = 0.8f;

    // A ringing peak is cut; the rest of the spectrum, and plain noise of either tilt, is left alone.
    auto reductionAt = [&] (float levelDb, float gainDb, double q, const ResonanceEngine::Settings& settings, double probeHz, double peakHz = 3000.0)
    {
        auto dry = makeNoise (-3.0f, levelDb, 5);
        addResonance (dry, peakHz, gainDb, q);
        const auto wet = run (dry, settings);
        return bandLevel (wet, probeHz, latency) - bandLevel (dry, probeHz, 0);
    };
    const double peakCut = reductionAt (-20.0f, 12.0f, 8.0, on, 3000.0);
    const double offPeak = reductionAt (-20.0f, 12.0f, 8.0, on, 600.0);
    const double quietCut = reductionAt (-45.0f, 12.0f, 8.0, on, 3000.0);
    std::cout << "  resonance_peak_cut " << peakCut << " off_peak " << offPeak << " at_-45dB " << quietCut << "\n";
    require (peakCut < -4.0 && peakCut > -12.5, "Resonance suppressor did not cut a +12 dB peak by a sensible amount");
    require (std::abs (offPeak) < 0.6, "Resonance suppressor changed the spectrum away from the peak");
    require (std::abs (quietCut - peakCut) < 0.5, "Resonance suppressor result depends on input level");

    for (float slope : { 0.0f, -3.0f })
    {
        const auto dry = makeNoise (slope, -20.0f, 9);
        const auto wet = run (dry, on);
        double worst = 0.0;
        for (double f : { 100.0, 400.0, 1000.0, 3000.0, 8000.0 })
            worst = juce::jmax (worst, std::abs (bandLevel (wet, f, latency) - bandLevel (dry, f, 0)));
        std::cout << "  resonance_plain_noise slope " << slope << " worst_band_change " << worst << "\n";
        require (worst < 1.0, "Resonance suppressor changed plain noise by more than 1 dB");
    }

    // Controls move the result the way they say.
    auto with = [&] (auto change) { auto s = on; change (s); return s; };
    const double lowDepth = reductionAt (-20.0f, 12.0f, 8.0, with ([] (auto& s) { s.depth = 0.3f; }), 3000.0);
    const double hardCut = reductionAt (-20.0f, 12.0f, 8.0, with ([] (auto& s) { s.depth = 1.0f; s.hard = true; }), 3000.0);
    const double softMax = reductionAt (-20.0f, 12.0f, 8.0, with ([] (auto& s) { s.depth = 1.0f; }), 3000.0);
    const double smallPeakPicky = reductionAt (-20.0f, 5.0f, 8.0, with ([] (auto& s) { s.selectivity = 1.0f; }), 3000.0);
    const double smallPeakOpen = reductionAt (-20.0f, 5.0f, 8.0, with ([] (auto& s) { s.selectivity = 0.0f; }), 3000.0);
    const double outsideRegion = reductionAt (-20.0f, 12.0f, 8.0, with ([] (auto& s) { s.centreHz = 300.0f; s.widthOctaves = 1.0f; }), 3000.0);
    const double halfMix = reductionAt (-20.0f, 12.0f, 8.0, with ([] (auto& s) { s.mix = 0.5f; }), 3000.0);
    const double narrowSharp = reductionAt (-20.0f, 12.0f, 30.0, with ([] (auto& s) { s.sharpness = 1.0f; }), 3000.0);
    const double narrowBroad = reductionAt (-20.0f, 12.0f, 30.0, with ([] (auto& s) { s.sharpness = 0.0f; }), 3000.0);
    std::cout << "  resonance_controls depth0.3 " << lowDepth << " soft_max " << softMax << " hard " << hardCut
              << " small_peak sel1 " << smallPeakPicky << " sel0 " << smallPeakOpen << " outside_region " << outsideRegion
              << " mix0.5 " << halfMix << " q30 sharp1 " << narrowSharp << " sharp0 " << narrowBroad << "\n";
    require (lowDepth > peakCut + 1.0, "Lower depth did not cut less");
    require (hardCut < softMax - 2.0, "Hard mode did not cut deeper than soft mode");
    require (smallPeakPicky > -0.5 && smallPeakOpen < smallPeakPicky - 0.5, "Selectivity did not spare a small peak");
    require (std::abs (outsideRegion) < 0.3, "A peak outside the processed region was cut");
    require (halfMix > peakCut + 1.0 && halfMix < -1.0, "Mix did not blend the cut");
    require (narrowSharp < narrowBroad - 1.0, "Sharpness did not deepen the cut on a narrow peak");

    // Delta plus the normal output is the delayed input.
    {
        auto dry = makeNoise (-3.0f, -20.0f, 5);
        addResonance (dry, 3000.0, 12.0, 8.0);
        const auto wet = run (dry, on);
        const auto removed = run (dry, with ([] (auto& s) { s.delta = true; }));
        double error = 0, reference = 0;
        for (int i = 4096; i < length - latency; ++i)
        {
            const double d = (double) wet[(size_t) (i + latency)] + removed[(size_t) (i + latency)] - dry[(size_t) i];
            error += d * d; reference += (double) dry[(size_t) i] * dry[(size_t) i];
        }
        const double snr = 10.0 * std::log10 (reference / (error + 1.0e-30));
        std::cout << "  resonance_delta_sum_snr " << snr << "\n";
        require (snr > 70.0, "Delta plus processed output does not equal the input");
    }

    // Focus bands: more cut where the band says, less where it says less, nothing changed elsewhere.
    {
        auto focused = [&] (float amount)
        {
            auto s = on;
            s.focus[0] = { 3000.0f, amount, 1.0f };
            s.focusCount = 1;
            return s;
        };
        const double more = reductionAt (-20.0f, 12.0f, 8.0, focused (1.0f), 3000.0);
        const double less = reductionAt (-20.0f, 12.0f, 8.0, focused (-1.0f), 3000.0);
        const double elsewhere = reductionAt (-20.0f, 12.0f, 8.0, focused (1.0f), 300.0, 300.0)
                               - reductionAt (-20.0f, 12.0f, 8.0, on, 300.0, 300.0);
        std::cout << "  resonance_focus +1 " << more << " -1 " << less << " plain " << peakCut << " three_octaves_away " << elsewhere << "\n";
        require (more < peakCut - 2.0, "A positive focus band did not deepen the cut");
        require (less > peakCut + 2.0, "A negative focus band did not lighten the cut");
        require (std::abs (elsewhere) < 0.3, "A focus band changed the cut three octaves away");
    }

    // Stereo: unlinked left/right keeps a one-sided cut on its side; mid/side keeps a mid-only cut out of the side.
    {
        auto stereoRun = [&] (std::vector<float>& l, std::vector<float>& r, const ResonanceEngine::Settings& settings)
        {
            ResonanceEngine engine;
            engine.prepare (sr);
            for (int pos = 0; pos < length; pos += 512)
                engine.process (l.data() + pos, r.data() + pos, juce::jmin (512, length - pos), settings);
        };
        auto resonant = makeNoise (-3.0f, -20.0f, 5);
        addResonance (resonant, 3000.0, 12.0, 8.0);
        const auto plain = makeNoise (-3.0f, -20.0f, 11);

        auto l = resonant; auto r = plain;
        stereoRun (l, r, with ([] (auto& s) { s.link = 0.0f; }));
        const double unlinkedL = bandLevel (l, 3000.0, latency) - bandLevel (resonant, 3000.0, 0);
        const double unlinkedR = bandLevel (r, 3000.0, latency) - bandLevel (plain, 3000.0, 0);
        l = resonant; r = plain;
        stereoRun (l, r, on);
        const double linkedR = bandLevel (r, 3000.0, latency) - bandLevel (plain, 3000.0, 0);

        // Mid carries the resonance, side carries different plain noise.
        std::vector<float> ml ((size_t) length), mr ((size_t) length), sideOnly ((size_t) length);
        for (int i = 0; i < length; ++i)
        {
            ml[(size_t) i] = resonant[(size_t) i] + plain[(size_t) i];
            mr[(size_t) i] = resonant[(size_t) i] - plain[(size_t) i];
        }
        auto outL = ml, outR = mr;
        stereoRun (outL, outR, with ([] (auto& s) { s.midSide = true; s.link = 0.0f; }));
        std::vector<float> outMid ((size_t) length), outSide ((size_t) length);
        for (int i = 0; i < length; ++i)
        {
            outMid[(size_t) i] = 0.5f * (outL[(size_t) i] + outR[(size_t) i]);
            outSide[(size_t) i] = 0.5f * (outL[(size_t) i] - outR[(size_t) i]);
        }
        const double midCut = bandLevel (outMid, 3000.0, latency) - bandLevel (resonant, 3000.0, 0);
        const double sideCut = bandLevel (outSide, 3000.0, latency) - bandLevel (plain, 3000.0, 0);
        std::cout << "  resonance_stereo unlinked L " << unlinkedL << " R " << unlinkedR << " linked R " << linkedR
                  << " mid/side mid " << midCut << " side " << sideCut << "\n";
        require (unlinkedL < -3.0 && std::abs (unlinkedR) < 0.6, "Unlinked left/right did not keep the cut on the resonant side");
        require (linkedR < -3.0, "Full stereo link did not carry the cut to the other channel");
        require (midCut < -3.0 && std::abs (sideCut) < 0.6, "Mid/side did not keep a mid-only cut out of the side");
    }

    // Sidechain: plain noise is cut where the sidechain has a peak, and not at all when the sidechain is plain.
    {
        auto keyedRun = [&] (std::vector<float> x, const std::vector<float>& key)
        {
            ResonanceEngine engine;
            engine.prepare (sr);
            auto right = x;
            for (int pos = 0; pos < length; pos += 512)
                engine.process (x.data() + pos, right.data() + pos, juce::jmin (512, length - pos), on, key.data() + pos, key.data() + pos);
            return x;
        };
        const auto dry = makeNoise (-3.0f, -20.0f, 5);
        auto key = makeNoise (-3.0f, -20.0f, 21);
        const auto plainKey = key;
        addResonance (key, 2000.0, 15.0, 6.0);
        const auto keyed = keyedRun (dry, key);
        const auto unkeyed = keyedRun (dry, plainKey);
        const double atKey = bandLevel (keyed, 2000.0, latency) - bandLevel (dry, 2000.0, 0);
        const double away = bandLevel (keyed, 500.0, latency) - bandLevel (dry, 500.0, 0);
        const double plainKeyCut = bandLevel (unkeyed, 2000.0, latency) - bandLevel (dry, 2000.0, 0);
        std::cout << "  resonance_sidechain at_key " << atKey << " away " << away << " plain_key " << plainKeyCut << "\n";
        require (atKey < -3.0, "Sidechain peak did not cut the main signal at that frequency");
        require (std::abs (away) < 0.6 && std::abs (plainKeyCut) < 0.8, "Sidechain cut where it has no peak");
    }

    // Timing: slow attack takes longer to bite, slow release takes longer to let go.
    {
        // Steady noise with a 3 kHz tone that switches on at 1 s and off at 2.5 s.
        auto gated = makeNoise (-3.0f, -30.0f, 5);
        for (int i = 48000; i < 120000; ++i)
            gated[(size_t) i] += 0.1f * (float) std::sin (juce::MathConstants<double>::twoPi * 3000.0 * i / sr);
        // Time in ms after `from` at which the reduction readout near 3 kHz first passes `fraction` of `full`,
        // going up (rising) or coming back down.
        auto timeTo = [&] (const ResonanceEngine::Settings& settings, int from, bool rising, double fraction)
        {
            ResonanceEngine engine;
            engine.prepare (sr);
            auto x = gated; auto right = gated;
            int band = 0;
            for (int b = 0; b < ResonanceEngine::displayBands; ++b)
                if (std::abs (ResonanceEngine::displayBandHz (b) - 3000.0f) < std::abs (ResonanceEngine::displayBandHz (band) - 3000.0f))
                    band = b;
            double full = 0.0; int crossed = -1;
            std::vector<float> trace;
            for (int pos = 0; pos < length; pos += 64)
            {
                engine.process (x.data() + pos, right.data() + pos, 64, settings);
                trace.push_back (engine.getDisplayReductionDb (band));
            }
            for (int i = 100000 / 64; i < 118000 / 64; ++i) full = juce::jmax (full, (double) trace[(size_t) i]);
            for (int i = from / 64; i < (int) trace.size(); ++i)
                if (rising ? trace[(size_t) i] >= fraction * full : trace[(size_t) i] <= fraction * full) { crossed = i; break; }
            return crossed < 0 ? 1.0e9 : (crossed * 64 - from) * 1000.0 / sr;
        };
        const double fastAttack = timeTo (with ([] (auto& s) { s.attack = 0.0f; }), 48000, true, 0.5);
        const double slowAttack = timeTo (with ([] (auto& s) { s.attack = 1.0f; }), 48000, true, 0.5);
        const double fastRelease = timeTo (with ([] (auto& s) { s.release = 0.0f; }), 120000, false, 0.5);
        const double slowRelease = timeTo (with ([] (auto& s) { s.release = 1.0f; }), 120000, false, 0.5);
        std::cout << "  resonance_timing_ms attack fast " << fastAttack << " slow " << slowAttack
                  << " release fast " << fastRelease << " slow " << slowRelease << "\n";
        require (fastAttack < 80.0, "Fast attack took more than 80 ms to reach half the cut");
        require (slowAttack > fastAttack + 150.0 && slowAttack < 1200.0, "Slow attack is not clearly slower than fast attack");
        require (fastRelease < 150.0, "Fast release took more than 150 ms to recover halfway");
        require (slowRelease > fastRelease + 600.0, "Slow release is not clearly slower than fast release");
    }

    // Through the plug-in: switching it on reports the latency and stays finite.
    {
        OpenEQAudioProcessor processor;
        processor.prepareToPlay (sr, 512);
        setRaw (processor.apvts, "spectralAmount", 0.8f);
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        auto dry = makeNoise (-3.0f, -20.0f, 5);
        bool finite = true;
        for (int pos = 0; pos + 512 <= 48000; pos += 512)
        {
            for (int ch = 0; ch < 2; ++ch)
                buffer.copyFrom (ch, 0, dry.data() + pos, 512);
            processor.processBlock (buffer, midi);
            finite = finite && allFinite (buffer);
        }
        require (finite, "Resonance suppressor produced a non-finite sample in the plug-in");
        require (processor.getReportedLatencySamples() == latency, "Plug-in did not report the suppressor latency");
    }

    std::cout << "PASS: resonance suppressor reconstruction, selectivity, level independence, controls, focus, stereo, sidechain, timing and delta\n";
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI initialise;
    std::cout << std::setprecision (8);
    std::cout << "MEASUREMENTS\n";

    const juce::String filter (argc > 1 ? argv[1] : "");

    int failures = 0;
    const auto run = [&] (const char* name, auto&& function)
    {
        if (filter.isNotEmpty() && ! juce::String (name).containsIgnoreCase (filter))
            return;
        try
        {
            function();
        }
        catch (const std::exception& error)
        {
            std::cerr << "FAIL: " << name << ": " << error.what() << "\n";
            ++failures;
        }
    };

    run ("cuts", checkCuts);
    run ("controls", checkBandControls);
    run ("routing", checkRouting);
    run ("blocks", checkBlockHandling);
    run ("smoothing", checkSmoothing);
    run ("snapshots", checkSnapshotsAndResponse);
    run ("dynamics", checkDynamicsShortBlock);
    run ("nyquist", checkNyquistAndTypes);
    run ("state", checkStateRoundTrip);
    run ("analyzer", checkAnalyzerRing);
    run ("curve-request", checkCurveRequestIsolation);
    run ("graph", checkGraphPaint);
    run ("allocations", checkSteadyStateAllocations);
    run ("roadmap", checkRoadmap);
    run ("plan", checkPlanCoverage);
    run ("detector", checkTransientDetector);
    run ("lp-crossfade", checkLinearPhaseCrossfade);
    run ("brickwall-types", checkBrickwallAndTypes);
    run ("filter-types", checkFilterTypes);
    run ("band-operations", checkBandOperations);
    run ("modern-dynamics", checkModernDynamics);
    run ("legacy-dynamics", checkLegacyDynamicsMigration);
    run ("current-dynamics-recall", checkCurrentDynamicsStatePreserved);
    run ("meter-ballistics", checkMeterBallistics);
    run ("finite-guard", checkNonFiniteGuard);
    run ("display-settings", checkDisplaySettings);
    run ("stereo-pan", checkStereoPanAndMidSide);
    run ("global-lookahead", checkGlobalLookahead);
    run ("ultra-precision", checkUltraPrecision);
    run ("oversampling", checkOversampling);
    run ("resonance", checkResonanceSuppressor);
    run ("presets", checkPresetFiles);
    run ("analog-mix", checkAnalogAndMixModes);
    run ("themes", checkThemes);
    run ("thirty-two-bands", checkThirtyTwoBands);
    run ("global-controls", checkGlobalControls);
    run ("note-formats", checkNoteAndValueFormats);
    run ("slider-click", checkCompactSliderClickMapping);
    run ("behaviour-settings", checkBehaviourSettings);
    run ("theme-file", checkThemeFile);
    run ("preset-import", checkPresetImport);
    run ("filter-validation", runFilterResponseTests);
    run ("split-mode", checkSplitMode);
    run ("cross-instance", checkCrossInstance);
    run ("phase-modes", checkPhaseModes);

    if (failures != 0)
    {
        std::cerr << "FAIL: " << failures << " section(s)\n";
        return 1;
    }

    std::cout << "PASS: audio foundation batch\n";
    return 0;
}
