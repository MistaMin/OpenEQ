#include "SpectrumAnalyzer.h"
#include "PluginProcessor.h"
#include "InstanceRegistry.h"
#include "AppSettings.h"
#include "UI/Theme.h"
#include <atomic>

namespace
{
    constexpr float minFreq = 20.0f;
    constexpr float maxFreq = 20000.0f;
    constexpr float minDb = SpectrumAnalyzer::rangeMinDb;
    constexpr float maxDb = SpectrumAnalyzer::rangeMaxDb;

    std::atomic<float> gRangeDb { 90.0f };
    std::atomic<int> gResolution { 0 };
    std::atomic<int> gSpeed { 1 };

    template <size_t N>
    void blendInto (std::array<float, N>& shown, const std::array<float, N>& fresh, float amount) noexcept
    {
        for (size_t i = 0; i < N; ++i)
            shown[i] += amount * (fresh[i] - shown[i]);
    }
}

void SpectrumAnalyzer::setDisplaySettings (DisplaySettings settings) noexcept
{
    gRangeDb.store (juce::jlimit (30.0f, 120.0f, settings.rangeDb));
    gResolution.store (juce::jlimit (0, 3, settings.resolution));
    gSpeed.store (juce::jlimit (0, 2, settings.speed));
}

SpectrumAnalyzer::DisplaySettings SpectrumAnalyzer::getDisplaySettings() noexcept
{
    DisplaySettings settings;
    settings.rangeDb = gRangeDb.load();
    settings.resolution = gResolution.load();
    settings.speed = gSpeed.load();
    return settings;
}

float SpectrumAnalyzer::speedBlend() noexcept
{
    static constexpr float amounts[] = { 0.9f, 0.5f, 0.2f };
    return amounts[juce::jlimit (0, 2, gSpeed.load())];
}

SpectrumAnalyzer::SpectrumAnalyzer (OpenEQAudioProcessor& processorToUse)
    : processor (processorToUse),
      fft (AnalyzerRing::order),
      window (AnalyzerRing::size, juce::dsp::WindowingFunction<float>::hann)
{
    setInterceptsMouseClicks (false, false);
    preMagnitudes.fill (minDb);
    postMagnitudes.fill (minDb);
    sideMagnitudes.fill (minDb);
    startTimerHz (AppSettings::get().analyzerRefreshHz());
}

SpectrumAnalyzer::~SpectrumAnalyzer() = default;

float SpectrumAnalyzer::frequencyToX (float freqHz, float width)
{
    const float logMin = std::log10 (minFreq);
    const float logMax = std::log10 (maxFreq);
    const float logF = std::log10 (juce::jlimit (minFreq, maxFreq, freqHz));
    return width * (logF - logMin) / (logMax - logMin);
}

float SpectrumAnalyzer::dbToY (float db, float height)
{
    const float bottom = -gRangeDb.load();
    const float clamped = juce::jlimit (bottom, maxDb, db);
    return height * (1.0f - (clamped - bottom) / (maxDb - bottom));
}

void SpectrumAnalyzer::computeSpectrum (AnalyzerRing& ringL, AnalyzerRing& ringR, std::array<float, AnalyzerRing::size / 2>& magnitudesOut)
{
    // Build the L/R/M/S display channel the same way reference monitor does:
    // standalone L/R from the two rings, M = (L+R)/2, S = (L-R)/2.
    switch (monitorMode)
    {
        case 1:  ringL.copyLatest (channelScratchL); memcpy (channelScratchR.data(), channelScratchL.data(), sizeof (float) * AnalyzerRing::size); break;
        case 2:  ringR.copyLatest (channelScratchL); memcpy (channelScratchR.data(), channelScratchL.data(), sizeof (float) * AnalyzerRing::size); break;
        case 3:
        case 4:
        {
            ringL.copyLatest (channelScratchL);
            ringR.copyLatest (channelScratchR);
            for (size_t i = 0; i < AnalyzerRing::size; ++i)
            {
                const float m = 0.5f * (channelScratchL[i] + channelScratchR[i]);
                const float s = 0.5f * (channelScratchL[i] - channelScratchR[i]);
                channelScratchL[i] = (monitorMode == 3) ? m : s;
            }
            break;
        }
        default:
            ringL.copyLatest (channelScratchL);
            ringR.copyLatest (channelScratchR);
            break;
    }

    auto transformChannel = [this] (const std::array<float, AnalyzerRing::size>& time,
                                    std::array<float, AnalyzerRing::size / 2>& magnitudes)
    {
        memcpy (fftData.data(), time.data(), sizeof (float) * AnalyzerRing::size);
        window.multiplyWithWindowingTable (fftData.data(), AnalyzerRing::size);
        std::array<float, AnalyzerRing::size * 2> complexData {};
        std::copy (fftData.begin(), fftData.end(), complexData.begin());
        fft.performFrequencyOnlyForwardTransform (complexData.data());
        for (int i = 0; i < AnalyzerRing::size / 2; ++i)
            magnitudes[(size_t) i] = complexData[(size_t) i] / (float) AnalyzerRing::size;
    };

    std::array<float, AnalyzerRing::size / 2> leftMagnitudes {};
    transformChannel (channelScratchL, leftMagnitudes);
    if (monitorMode == 0)
    {
        std::array<float, AnalyzerRing::size / 2> rightMagnitudes {};
        transformChannel (channelScratchR, rightMagnitudes);
        for (int i = 0; i < AnalyzerRing::size / 2; ++i)
        {
            const float left = leftMagnitudes[(size_t) i];
            const float right = rightMagnitudes[(size_t) i];
            const float energy = std::sqrt (0.5f * (left * left + right * right));
            magnitudesOut[(size_t) i] = juce::Decibels::gainToDecibels (energy, minDb);
        }
    }
    else
    {
        for (int i = 0; i < AnalyzerRing::size / 2; ++i)
            magnitudesOut[(size_t) i] = juce::Decibels::gainToDecibels (leftMagnitudes[(size_t) i], minDb);
    }
}

juce::Path SpectrumAnalyzer::buildPath (const std::array<float, AnalyzerRing::size / 2>& magnitudes, float width, float height)
{
    juce::Path p;
    const double sampleRate = processor.getSampleRate_() > 0.0 ? processor.getSampleRate_() : 44100.0;
    bool started = false;
    const int stride = 1 << gResolution.load();

    // Resolution groups neighbouring bins and keeps the largest value in each group.
    for (int i = 1; i < AnalyzerRing::size / 2; i += stride)
    {
        float peak = magnitudes[(size_t) i];
        for (int k = 1; k < stride && i + k < AnalyzerRing::size / 2; ++k)
            peak = juce::jmax (peak, magnitudes[(size_t) (i + k)]);

        const float freq = (float) ((i + 0.5 * (stride - 1)) * sampleRate / AnalyzerRing::size);
        if (freq < minFreq || freq > maxFreq)
            continue;

        const float x = frequencyToX (freq, width);
        const float shown = applyDisplayTilt (peak, freq, processor.getDisplayTiltDbPerOctave(), AppSettings::get().spectrumCentreHz());
        const float y = dbToY (shown, height);

        if (! started) { p.startNewSubPath (x, y); started = true; }
        else p.lineTo (x, y);
    }

    return p;
}

float SpectrumAnalyzer::peakNear (float freqHz) const
{
    const double sampleRate = processor.getSampleRate_() > 0.0 ? processor.getSampleRate_() : 44100.0;
    const float target = std::log (juce::jlimit (minFreq, maxFreq, freqHz));
    int best = -1;
    float bestDistance = 1000.0f;

    for (int i = 2; i < AnalyzerRing::size / 2 - 1; ++i)
    {
        const float freq = (float) (i * sampleRate / (double) AnalyzerRing::size);
        if (freq < minFreq || freq > maxFreq)
            continue;
        const float db = postMagnitudes[(size_t) i];
        if (db < -80.0f)
            continue;
        if (db < postMagnitudes[(size_t) (i - 1)] || db < postMagnitudes[(size_t) (i + 1)])
            continue;
        const float distance = std::abs (std::log (freq) - target);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = i;
        }
    }

    if (best < 0)
        return juce::jlimit (minFreq, maxFreq, freqHz);
    return (float) (best * sampleRate / (double) AnalyzerRing::size);
}

void SpectrumAnalyzer::dominantPeak (float& freqHz, float& db) const
{
    const double sampleRate = processor.getSampleRate_() > 0.0 ? processor.getSampleRate_() : 44100.0;
    freqHz = 0.0f;
    db = minDb;
    for (int i = 1; i < AnalyzerRing::size / 2; ++i)
    {
        const float freq = (float) (i * sampleRate / (double) AnalyzerRing::size);
        if (freq < minFreq || freq > maxFreq)
            continue;
        if (postMagnitudes[(size_t) i] > db)
        {
            db = postMagnitudes[(size_t) i];
            freqHz = freq;
        }
    }
}

void SpectrumAnalyzer::timerCallback()
{
    if (! frozen)
    {
        // Speed sets how far each frame moves toward the new spectrum.
        const float amount = speedBlend();
        std::array<float, AnalyzerRing::size / 2> fresh {};
        computeSpectrum (processor.preL, processor.preR, fresh);
        blendInto (preMagnitudes, fresh, amount);
        computeSpectrum (processor.postL, processor.postR, fresh);
        blendInto (postMagnitudes, fresh, amount);
        computeSpectrum (processor.sideL, processor.sideR, fresh);
        blendInto (sideMagnitudes, fresh, amount);
        if (processor.isSplitSpectrumActive())
        {
            computeSpectrum (processor.splitTransL, processor.splitTransR, fresh);
            blendInto (transientMagnitudes, fresh, amount);
            computeSpectrum (processor.splitTonalL, processor.splitTonalR, fresh);
            blendInto (tonalMagnitudes, fresh, amount);
        }

        if (processor.getSpectrumSource() == 3)
        {
            auto& sl = waterfallSlices[(size_t) waterfallWrite];
            const double sr = processor.getSampleRate_() > 0.0 ? processor.getSampleRate_() : 44100.0;
            const float binHz = (float) (sr / (double) AnalyzerRing::size);
            const float tilt = processor.getDisplayTiltDbPerOctave();
            const float centreHz = AppSettings::get().spectrumCentreHz();

            for (int col = 0; col < waterfallColumns; ++col)
            {
                const float f = 20.0f * std::pow (1000.0f, (float) col / (float) (waterfallColumns - 1));
                const float bin = f / binHz;
                const int i0 = juce::jlimit (1, (int) (AnalyzerRing::size / 2) - 2, (int) std::floor (bin));
                const float frac = bin - (float) i0;
                const float rawMag = (1.0f - frac) * postMagnitudes[(size_t) i0] + frac * postMagnitudes[(size_t) (i0 + 1)];
                const float tilted = applyDisplayTilt (rawMag, f, tilt, centreHz);
                sl.mags[(size_t) col] = tilted;
            }
            waterfallWrite = (waterfallWrite + 1) % waterfallDepth;
            waterfallCount = juce::jmin (waterfallCount + 1, waterfallDepth);
        }
        if (processor.getSpectrumSource() >= 4)
            pushSpectrogramRow();
    }

    float peakHz = 0.0f;
    float peakDb = minDb;
    dominantPeak (peakHz, peakDb);
    InstanceRegistry::get().publish (processor.getInstanceId(), peakHz, peakDb);
    repaint();
}

void SpectrumAnalyzer::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    const int source = processor.getSpectrumSource();
    if (source == 3)
    {
        paintWaterfall (g);
        return;
    }
    if (source == 4) { paintSpectrogram (g); return; }
    if (source == 5) { paintSpectrogram3D (g); return; }
    const auto& mainMagnitudes = source == 1 ? preMagnitudes : source == 2 ? sideMagnitudes : postMagnitudes;
    auto mainPath = buildPath (mainMagnitudes, bounds.getWidth(), bounds.getHeight());
    auto prePath = buildPath (preMagnitudes, bounds.getWidth(), bounds.getHeight());

    // While a band is split, the post spectrum is drawn as its two streams: tonal in blue, transient in green.
    const bool twoStreams = source == 0 && processor.isSplitSpectrumActive();
    if (twoStreams)
    {
        auto drawStream = [&] (const std::array<float, AnalyzerRing::size / 2>& magnitudes, juce::Colour colour)
        {
            const auto path = buildPath (magnitudes, bounds.getWidth(), bounds.getHeight());
            juce::Path fill = path;
            fill.lineTo (bounds.getWidth(), bounds.getHeight());
            fill.lineTo (0.0f, bounds.getHeight());
            fill.closeSubPath();
            g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.34f), 0.0f, 0.0f,
                                                     colour.withAlpha (0.02f), 0.0f, bounds.getHeight(), false));
            g.fillPath (fill);
            g.setColour (colour.withAlpha (0.16f));
            g.strokePath (path, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (colour.withAlpha (0.85f));
            g.strokePath (path, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        };
        drawStream (tonalMagnitudes, Theme::splitTonalCol);
        drawStream (transientMagnitudes, Theme::splitTransCol);
    }
    else
    {
        juce::Path fill = mainPath;
        fill.lineTo (bounds.getWidth(), bounds.getHeight());
        fill.lineTo (0.0f, bounds.getHeight());
        fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff35c6f0).withAlpha (0.30f), 0.0f, 0.0f,
                                                 juce::Colour (0xff35c6f0).withAlpha (0.015f), 0.0f, bounds.getHeight(), false));
        g.fillPath (fill);
    }
    if (! twoStreams)
    {
        g.setColour (juce::Colour (0xff35c6f0).withAlpha (0.14f));
        g.strokePath (mainPath, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (Theme::postSpectrum);
        g.strokePath (mainPath, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    if (source == 0)
    {
        g.setColour (Theme::preSpectrum);
        g.strokePath (prePath, juce::PathStrokeType (1.0f));
    }
    else
    {
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (source == 1 ? "PRE" : "SIDECHAIN", 8.0f, 8.0f, 90.0f, 14.0f, juce::Justification::centredLeft);
    }

    if (std::abs (processor.getDisplayTiltDbPerOctave()) > 0.05f)
    {
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("TILT " + juce::String (processor.getDisplayTiltDbPerOctave(), 1),
                    8.0f, 24.0f, 110.0f, 14.0f, juce::Justification::centredLeft);
    }

    if (frozen)
    {
        float peakHz = 0.0f;
        float peakDb = minDb;
        dominantPeak (peakHz, peakDb);
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("FROZEN", bounds.getWidth() - 78.0f, 8.0f, 70.0f, 14.0f, juce::Justification::centredRight);
        if (peakHz > 0.0f)
        {
            const float x = frequencyToX (peakHz, bounds.getWidth());
            const float y = dbToY (peakDb, bounds.getHeight());
            g.drawLine (x, y - 8.0f, x, y + 8.0f, 1.0f);
            g.drawText (juce::String (peakHz, 0) + " Hz", x + 4.0f, y - 16.0f, 70.0f, 14.0f, juce::Justification::centredLeft);
        }
    }
    // Resonance suppressor: what it is removing right now, hanging from the top of the graph.
    if (processor.isSpectralActive())
    {
        const float w = bounds.getWidth();
        const float top = bounds.getY() + 26.0f;
        const float pxPerDb = bounds.getHeight() * 0.018f;
        juce::Path cut;
        cut.startNewSubPath (0.0f, top);
        float deepest = 0.0f;
        for (int band = 0; band < ResonanceEngine::displayBands; ++band)
        {
            const float db = processor.getSpectralReductionDb (band);
            deepest = juce::jmax (deepest, db);
            const float x = w * (float) band / (float) (ResonanceEngine::displayBands - 1);
            cut.lineTo (x, top + juce::jmin (24.0f, db) * pxPerDb);
        }
        cut.lineTo (w, top);
        cut.closeSubPath();
        const juce::Colour colour (0xffb58cff);
        g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.10f), 0.0f, top,
                                                 colour.withAlpha (0.55f), 0.0f, top + 12.0f * pxPerDb, false));
        g.fillPath (cut);
        g.setColour (colour.withAlpha (0.95f));
        g.strokePath (cut, juce::PathStrokeType (1.3f));
        if (deepest > 0.2f)
        {
            g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
            g.drawText ("-" + juce::String (deepest, 1) + " dB", juce::Rectangle<float> (8.0f, top + 2.0f, 70.0f, 12.0f),
                        juce::Justification::centredLeft, false);
        }
    }
}


namespace
{
    /** Level colour for the spectrogram: silence is transparent, then deep blue, cyan, green, amber and pink. */
    juce::Colour spectrogramColour (float norm) noexcept
    {
        struct Stop { float at; juce::uint32 argb; };
        static constexpr Stop stops[] = { { 0.00f, 0x00081428 }, { 0.18f, 0x701a3fb0 }, { 0.40f, 0xc81f9fe6 },
                                          { 0.60f, 0xe634e0a1 }, { 0.80f, 0xfffbbf24 }, { 1.00f, 0xffff5a7a } };
        const float n = juce::jlimit (0.0f, 1.0f, norm);
        for (size_t i = 1; i < std::size (stops); ++i)
            if (n <= stops[i].at)
                return juce::Colour (stops[i - 1].argb).interpolatedWith (juce::Colour (stops[i].argb),
                                                                         (n - stops[i - 1].at) / (stops[i].at - stops[i - 1].at));
        return juce::Colour (stops[std::size (stops) - 1].argb);
    }
}

void SpectrumAnalyzer::pushSpectrogramRow()
{
    // Scroll the history back one row and write the newest spectrum into row 0.
    spectrogram.moveImageSection (0, 1, 0, 0, spectrogramColumns, spectrogramRows - 1);

    const double sr = processor.getSampleRate_() > 0.0 ? processor.getSampleRate_() : 44100.0;
    const float binHz = (float) (sr / (double) AnalyzerRing::size);
    const float tilt = processor.getDisplayTiltDbPerOctave();
    const float centreHz = AppSettings::get().spectrumCentreHz();
    const float floorDb = -gRangeDb.load();

    juce::Image::BitmapData row (spectrogram, 0, 0, spectrogramColumns, 1, juce::Image::BitmapData::writeOnly);
    for (int col = 0; col < spectrogramColumns; ++col)
    {
        const float f = 20.0f * std::pow (1000.0f, (float) col / (float) (spectrogramColumns - 1));
        const float bin = f / binHz;
        const int i0 = juce::jlimit (1, (int) (AnalyzerRing::size / 2) - 2, (int) std::floor (bin));
        const float frac = juce::jlimit (0.0f, 1.0f, bin - (float) i0);
        const float raw = (1.0f - frac) * postMagnitudes[(size_t) i0] + frac * postMagnitudes[(size_t) (i0 + 1)];
        const float db = applyDisplayTilt (raw, f, tilt, centreHz);
        const float norm = juce::jlimit (0.0f, 1.0f, (db - floorDb) / (maxDb - floorDb));
        spectrogramFront[(size_t) col] = norm;
        if ((col & 1) == 0)
            terrainLevels[(size_t) (terrainWrite * terrainColumns + col / 2)] = norm;
        row.setPixelColour (col, 0, spectrogramColour (norm));
    }
    terrainWrite = (terrainWrite + 1) % spectrogramRows;
    ++terrainTick;
    spectrogramCount = juce::jmin (spectrogramCount + 1, spectrogramRows);
}

void SpectrumAnalyzer::paintSpectrogram (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.fillAll (Theme::graphBackground);
    if (spectrogramCount == 0)
    {
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("Waiting for audio...", bounds, juce::Justification::centred, false);
        return;
    }

    // Frequency runs across the full width, in line with the EQ. The newest row is at the top and history
    // scrolls down, so the present is never under the inspector.
    g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
    g.setOpacity (0.88f);
    g.drawImage (spectrogram, bounds, juce::RectanglePlacement::stretchToFit);
    g.setOpacity (1.0f);

    const float seconds = (float) spectrogramRows / 30.0f;
    g.setFont (juce::FontOptions (9.0f));
    for (float sec = 1.0f; sec < seconds; sec += 1.0f)
    {
        const float y = bounds.getY() + bounds.getHeight() * sec / seconds;
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.drawHorizontalLine (juce::roundToInt (y), bounds.getX(), bounds.getRight());
        g.setColour (Theme::textDim);
        g.drawText ("-" + juce::String ((int) sec) + " s", juce::Rectangle<float> (bounds.getRight() - 40.0f, y - 12.0f, 34.0f, 11.0f),
                    juce::Justification::centredRight, false);
    }
}

void SpectrumAnalyzer::paintSpectrogram3D (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.fillAll (Theme::graphBackground);
    if (spectrogramCount == 0)
    {
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("Waiting for audio...", bounds, juce::Justification::centred, false);
        return;
    }

    // A terrain: frequency across, time receding toward the top, level as height and colour. The front edge is
    // full width, in line with the EQ, and sits above the inspector. Rows are solid, so near ridges hide the
    // ground behind them the way a landscape does.
    const float frontBase = bounds.getBottom() - bounds.getHeight() * 0.30f;
    const float backBase = bounds.getY() + bounds.getHeight() * 0.26f;
    const float frontH = bounds.getHeight() * 0.34f;
    constexpr float backScale = 0.46f;

    auto rowGeometry = [&] (float t, float& left, float& width, float& base, float& height)
    {
        const float depth = 1.0f - (1.0f - t) * (1.0f - t);
        const float scale = 1.0f - (1.0f - backScale) * depth;
        width = bounds.getWidth() * scale;
        left = bounds.getCentreX() - width * 0.5f;
        base = frontBase + (backBase - frontBase) * depth;
        height = frontH * scale;
    };

    // Ground plane with second markers, drawn first so the terrain stands on it.
    {
        float bl, bw, bb, bh;
        rowGeometry (1.0f, bl, bw, bb, bh);
        juce::Path ground;
        ground.startNewSubPath (bounds.getX(), frontBase);
        ground.lineTo (bounds.getRight(), frontBase);
        ground.lineTo (bl + bw, bb);
        ground.lineTo (bl, bb);
        ground.closeSubPath();
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1a3fb0).withAlpha (0.16f), 0.0f, frontBase,
                                                 juce::Colour (0xff1a3fb0).withAlpha (0.03f), 0.0f, bb, false));
        g.fillPath (ground);

        const float seconds = (float) spectrogramRows / 30.0f;
        g.setFont (juce::FontOptions (9.0f));
        for (float sec = 1.0f; sec < seconds; sec += 1.0f)
        {
            float l, w, b, h;
            rowGeometry (sec / seconds, l, w, b, h);
            const bool labelled = ((int) sec % 2) == 0;
            g.setColour (juce::Colours::white.withAlpha (labelled ? 0.10f : 0.05f));
            g.drawLine (l, b, l + w, b, 1.0f);
            if (labelled)
            {
                g.setColour (Theme::textDim);
                g.drawText ("-" + juce::String ((int) sec) + " s", juce::Rectangle<float> (l + w + 4.0f, b - 6.0f, 30.0f, 12.0f),
                            juce::Justification::centredLeft, false);
            }
        }
    }

    // Colour by height, matching the flat spectrogram's scale. `shade` darkens rows with distance.
    auto heightGradient = [] (float base, float height, float shade, float lowAlpha)
    {
        auto at = [shade] (float norm, float alpha)
        {
            return spectrogramColour (norm).withAlpha (alpha).withMultipliedBrightness (shade);
        };
        juce::ColourGradient grad (at (0.18f, lowAlpha), 0.0f, base, at (1.0f, 1.0f), 0.0f, base - height, false);
        grad.addColour (0.12, at (0.22f, juce::jmax (lowAlpha, 0.75f)));
        grad.addColour (0.40, at (0.40f, 1.0f));
        grad.addColour (0.60, at (0.60f, 1.0f));
        grad.addColour (0.80, at (0.80f, 1.0f));
        return grad;
    };

    const auto occlude = Theme::graphBackground;
    const int rows = spectrogramCount;
    for (int age = rows - 1; age >= 0; --age)
    {
        // Every other row is enough for a solid surface. The choice follows the row, not its age, so the
        // surface scrolls instead of shimmering.
        if (age != 0 && ((terrainTick - 1 - age) & 1) != 0)
            continue;

        const int ringRow = ((terrainWrite - 1 - age) % spectrogramRows + spectrogramRows) % spectrogramRows;
        const float* levels = terrainLevels.data() + (size_t) (ringRow * terrainColumns);

        float left, width, base, height;
        rowGeometry ((float) age / (float) spectrogramRows, left, width, base, height);
        const float t = (float) age / (float) spectrogramRows;
        const float shade = 1.0f - 0.55f * t;

        juce::Path fill, ridge;
        fill.startNewSubPath (left, base);
        for (int col = 0; col < terrainColumns; ++col)
        {
            const float x = left + width * (float) col / (float) (terrainColumns - 1);
            const float y = base - height * levels[col];
            fill.lineTo (x, y);
            if (col == 0) ridge.startNewSubPath (x, y);
            else          ridge.lineTo (x, y);
        }
        fill.lineTo (left + width, base);
        fill.closeSubPath();

        g.setColour (occlude);
        g.fillPath (fill);
        g.setGradientFill (heightGradient (base, height, shade, 0.35f));
        g.fillPath (fill);

        if (age == 0)
        {
            g.setGradientFill (heightGradient (base, height, 1.0f, 0.5f));
            g.strokePath (ridge, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.strokePath (ridge, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else
        {
            // A dark edge under a faint light one separates each row from the one behind it.
            g.setColour (juce::Colours::black.withAlpha (0.28f));
            g.strokePath (ridge, juce::PathStrokeType (1.6f));
            g.setColour (juce::Colours::white.withAlpha (0.10f * shade));
            g.strokePath (ridge, juce::PathStrokeType (0.6f));
        }
    }

    g.setColour (juce::Colours::white.withAlpha (0.22f));
    g.drawLine (bounds.getX(), frontBase, bounds.getRight(), frontBase, 1.0f);
    g.setFont (juce::FontOptions (9.0f));
    g.setColour (Theme::textDim);
    g.drawText ("now", juce::Rectangle<float> (bounds.getRight() - 40.0f, frontBase + 3.0f, 34.0f, 12.0f),
                juce::Justification::centredRight, false);
}

void SpectrumAnalyzer::paintWaterfall (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.fillAll (Theme::graphBackground);

    if (waterfallCount == 0)
    {
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("Waiting for audio...", bounds, juce::Justification::centred, false);
        return;
    }

    const float floorDb = -gRangeDb.load();
    const float ceilingDb = maxDb;

    // The newest slice spans the full width, so it lines up with the frequency axis and the EQ nodes above it.
    // History recedes upward and narrows toward the centre. The front edge sits above the bottom of the graph,
    // clear of the keyboard and of the inspector in its usual place.
    const float frontBase = bounds.getBottom() - bounds.getHeight() * 0.30f;
    const float backBase = bounds.getY() + bounds.getHeight() * 0.20f;
    const float frontH = bounds.getHeight() * 0.30f;
    constexpr float backScale = 0.74f;

    auto geometry = [&] (float t, float& left, float& width, float& base, float& height)
    {
        // Slices bunch together toward the back, as distance does.
        const float depth = 1.0f - (1.0f - t) * (1.0f - t);
        const float scale = 1.0f - (1.0f - backScale) * depth;
        width = bounds.getWidth() * scale;
        left = bounds.getCentreX() - width * 0.5f;
        base = frontBase + (backBase - frontBase) * depth;
        height = frontH * scale;
    };
    auto ageT = [] (int age) { return (float) age / (float) (waterfallDepth - 1); };

    // Floor: the plane the slices stand on, with a line every half second.
    {
        float fl, fw, fb, fh, bl, bw, bb, bh;
        geometry (0.0f, fl, fw, fb, fh);
        geometry (1.0f, bl, bw, bb, bh);
        juce::Path floor;
        floor.startNewSubPath (fl, fb);
        floor.lineTo (fl + fw, fb);
        floor.lineTo (bl + bw, bb);
        floor.lineTo (bl, bb);
        floor.closeSubPath();
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff38bdf8).withAlpha (0.07f), 0.0f, fb,
                                                 juce::Colour (0xff38bdf8).withAlpha (0.015f), 0.0f, bb, false));
        g.fillPath (floor);

        const float seconds = (float) waterfallDepth / 30.0f;
        g.setFont (juce::FontOptions (9.0f));
        for (float sec = 0.5f; sec < seconds; sec += 0.5f)
        {
            float l, w, b, h;
            geometry (sec / seconds, l, w, b, h);
            const bool whole = std::abs (sec - std::round (sec)) < 0.01f;
            g.setColour (juce::Colours::white.withAlpha (whole ? 0.10f : 0.05f));
            g.drawLine (l, b, l + w, b, 1.0f);
            if (whole)
            {
                g.setColour (Theme::textDim);
                g.drawText ("-" + juce::String ((int) std::round (sec)) + " s",
                            juce::Rectangle<float> (l + w + 4.0f, b - 6.0f, 30.0f, 12.0f), juce::Justification::centredLeft, false);
            }
        }
    }

    // Level colours, bottom of a slice to its ceiling: quiet is deep blue, loud runs through cyan and green to amber.
    auto levelGradient = [] (float base, float height, float alpha)
    {
        juce::ColourGradient grad (juce::Colour (0xff1e4fd8).withAlpha (alpha * 0.55f), 0.0f, base,
                                   juce::Colour (0xffff5a7a).withAlpha (alpha), 0.0f, base - height, false);
        grad.addColour (0.30, juce::Colour (0xff22b8f0).withAlpha (alpha * 0.8f));
        grad.addColour (0.55, juce::Colour (0xff34e0a1).withAlpha (alpha));
        grad.addColour (0.78, juce::Colour (0xfffbbf24).withAlpha (alpha));
        return grad;
    };

    const auto occlude = Theme::graphBackground;

    // Oldest slice first, so nearer slices cover the ones behind them.
    for (int age = waterfallCount - 1; age >= 0; --age)
    {
        const int idx = (waterfallWrite - 1 - age + 2 * waterfallDepth) % waterfallDepth;
        const auto& sl = waterfallSlices[(size_t) idx];

        float left, width, base, height;
        geometry (ageT (age), left, width, base, height);
        const float fade = 1.0f - 0.80f * ageT (age);

        juce::Path fill, line;
        fill.startNewSubPath (left, base);
        for (int i = 0; i < waterfallColumns; ++i)
        {
            const float x = left + width * (float) i / (float) (waterfallColumns - 1);
            const float norm = juce::jlimit (0.0f, 1.0f, (sl.mags[(size_t) i] - floorDb) / (ceilingDb - floorDb));
            const float y = base - height * norm;
            fill.lineTo (x, y);
            if (i == 0) line.startNewSubPath (x, y);
            else        line.lineTo (x, y);
        }
        fill.lineTo (left + width, base);
        fill.closeSubPath();

        g.setColour (occlude);
        g.fillPath (fill);
        g.setGradientFill (levelGradient (base, height, (age == 0 ? 0.42f : 0.20f) * fade));
        g.fillPath (fill);

        if (age == 0)
        {
            g.setGradientFill (levelGradient (base, height, 0.30f));
            g.strokePath (line, juce::PathStrokeType (4.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setGradientFill (levelGradient (base, height, 1.0f));
            g.strokePath (line, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else
        {
            g.setGradientFill (levelGradient (base, height, 0.75f * fade));
            g.strokePath (line, juce::PathStrokeType (1.0f));
        }
    }

    // Front edge and level labels (on the right, away from the EQ gain scale). The frequency axis is the graph's own, which the front slice lines up with.
    g.setColour (juce::Colours::white.withAlpha (0.22f));
    g.drawLine (bounds.getX(), frontBase, bounds.getRight(), frontBase, 1.0f);
    g.setFont (juce::FontOptions (9.0f));
    g.setColour (Theme::textDim);
    g.drawText ("now", juce::Rectangle<float> (bounds.getRight() - 40.0f, frontBase + 3.0f, 34.0f, 12.0f),
                juce::Justification::centredRight, false);
    for (float db : { 0.0f, -30.0f, -60.0f })
    {
        if (db > floorDb && db <= ceilingDb)
        {
            const float y = frontBase - frontH * (db - floorDb) / (ceilingDb - floorDb);
            g.drawText (juce::String ((int) db) + " dB", juce::Rectangle<float> (bounds.getRight() - 48.0f, y - 6.0f, 42.0f, 12.0f),
                        juce::Justification::centredRight, false);
        }
    }
}
