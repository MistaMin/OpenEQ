#pragma once
#include <JuceHeader.h>
#include "Analyzer.h"

class OpenEQAudioProcessor;

/** Draws pre and post FFT spectra on a log-frequency / dB grid, matching the
    coordinate space used by EQCurveComponent so the two overlay correctly. */
class SpectrumAnalyzer : public juce::Component, private juce::Timer
{
public:
    explicit SpectrumAnalyzer (OpenEQAudioProcessor& processorToUse);
    ~SpectrumAnalyzer() override;

    void paint (juce::Graphics& g) override;

    /** Monitor channel selection, mirroring reference L/R/M/S monitoring:
        0 = LR, 1 = Left, 2 = Right, 3 = Mid, 4 = Side. */
    void setMonitorMode (int mode) { monitorMode = mode; }
    int getMonitorMode() const { return monitorMode; }

    /** Hold the current trace. Grab mode uses the held peaks and does not save them. */
    void setFrozen (bool shouldFreeze) { frozen = shouldFreeze; repaint(); }

    /** Changes the redraw rate. Takes effect at once. */
    void setRefreshHz (int hz) { startTimerHz (juce::jlimit (15, 60, hz)); }
    bool isFrozen() const noexcept { return frozen; }

    /** Frequency of the held local maximum closest to freqHz, or freqHz when the trace is quiet. */
    float peakNear (float freqHz) const;
    void dominantPeak (float& freqHz, float& db) const;

    static float frequencyToX (float freqHz, float width);
    static float dbToY (float db, float height);

    /** Analyzer data floor is -120 dB. The display range (60, 90, or 120 dB below zero) sets where the trace is cut off.
        The EQ curve uses its own ±30 dB scale. */
    static constexpr float rangeMinDb = -120.0f;
    static constexpr float rangeMaxDb = 0.0f;

    /** Display settings, global like reference, and not saved.
        resolution: 0 every bin, 1 pairs, 2 groups of four, 3 groups of eight (the largest value is kept).
        speed: 0 fast, 1 medium, 2 slow (how quickly the trace follows the signal).
        rangeDb: how far the trace goes below zero. */
    struct DisplaySettings
    {
        int resolution = 0;
        int speed = 1;
        float rangeDb = 90.0f;
    };
    static void setDisplaySettings (DisplaySettings settings) noexcept;
    static DisplaySettings getDisplaySettings() noexcept;

    /** Display-only tilt around a pivot frequency (1 kHz unless the setting says otherwise). Audio is unchanged. */
    static float applyDisplayTilt (float db, float freqHz, float dbPerOctave, float pivotHz = 1000.0f) noexcept
    {
        const float octaves = std::log2 (juce::jmax (freqHz, 1.0f) / juce::jmax (pivotHz, 1.0f));
        return db + dbPerOctave * octaves;
    }

private:
    void timerCallback() override;
    static float speedBlend() noexcept;
    /** The last spectrum computed from a pair of rings. It is reused while neither ring has published since and
        the monitor mode is the same, because the transform of the same samples gives the same result. */
    struct SpectrumCache
    {
        bool valid = false;
        juce::uint32 generationL = 0, generationR = 0;
        int monitorMode = 0;
        std::array<float, AnalyzerRing::size / 2> magnitudes {};
    };
    std::array<SpectrumCache, 5> spectrumCache {};
    void computeSpectrum (AnalyzerRing& ringL, AnalyzerRing& ringR, std::array<float, AnalyzerRing::size / 2>& magnitudesOut,
                          SpectrumCache& cache);
    juce::Path buildPath (const std::array<float, AnalyzerRing::size / 2>& magnitudes, float width, float height);

    OpenEQAudioProcessor& processor;
    juce::dsp::FFT fft;
    juce::dsp::WindowingFunction<float> window;

    int monitorMode = 0;
    bool frozen = false;

    std::array<float, AnalyzerRing::size> fftData {};
    std::array<float, AnalyzerRing::size> channelScratchL {};
    std::array<float, AnalyzerRing::size> channelScratchR {};
    std::array<float, AnalyzerRing::size / 2> preMagnitudes {};
    std::array<float, AnalyzerRing::size / 2> postMagnitudes {};
    std::array<float, AnalyzerRing::size / 2> sideMagnitudes {};
    std::array<float, AnalyzerRing::size / 2> transientMagnitudes {};
    std::array<float, AnalyzerRing::size / 2> tonalMagnitudes {};

    /** Waterfall history: 3D spectral slices receding into depth (newest at the front). */
    static constexpr int waterfallColumns = 160;     // log-spaced 20 Hz .. 20 kHz
    static constexpr int waterfallDepth = 80;        // slices kept (one per UI tick: ~2.7 s)
    struct WaterfallSlice
    {
        std::array<float, waterfallColumns> mags {};
    };
    std::array<WaterfallSlice, waterfallDepth> waterfallSlices {};
    int waterfallWrite = 0;
    int waterfallCount = 0;
    void paintWaterfall (juce::Graphics& g);

    /** Spectrogram history: one image row per UI tick, newest in row 0, frequency log-spaced across the width.
        Level is baked into the colour when a row is written. View 4 draws it flat; view 5 draws the same history as a terrain. */
    static constexpr int spectrogramColumns = 512;
    static constexpr int spectrogramRows = 240;      // ~8 s at 30 ticks a second
    juce::Image spectrogram { juce::Image::ARGB, spectrogramColumns, spectrogramRows, true };
    std::array<float, spectrogramColumns> spectrogramFront {}; // newest row, 0..1, for the live line in the 3D view
    int spectrogramCount = 0;
    /** Levels behind the 3D view's terrain: a ring of rows at half the column resolution, 0..1. */
    static constexpr int terrainColumns = spectrogramColumns / 2;
    std::vector<float> terrainLevels = std::vector<float> ((size_t) (terrainColumns * spectrogramRows), 0.0f);
    int terrainWrite = 0;      // ring row the next spectrum goes into
    juce::int64 terrainTick = 0; // rows written so far, so every other row can be drawn without flicker
    void pushSpectrogramRow();
    void paintSpectrogram (juce::Graphics& g);
    void paintSpectrogram3D (juce::Graphics& g);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumAnalyzer)
};
