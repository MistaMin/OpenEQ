#include "EQCurveComponent.h"
#include "BandMenu.h"
#include "AppSettings.h"
#include "PluginProcessor.h"
#include "Parameters.h"
#include "UI/Theme.h"
#include "UI/ValueFormat.h"
#include <algorithm>
#include <atomic>
#include <vector>

namespace
{
    constexpr float minFreq = 20.0f;
    constexpr float maxFreq = 20000.0f;
    // The display range is a global setting, using 12/18/30 dB. It is not saved with the session.
    std::atomic<float>& displayRangeDb = AppSettings::displayRangeDb();

    float maxDbNow() noexcept { return displayRangeDb.load(); }
    float minDbNow() noexcept { return -displayRangeDb.load(); }

    /** Grid lines and their labels for the current range. Steps of 3 dB for 12 dB, 6 dB otherwise. */
    float gridStepDb() noexcept { return displayRangeDb.load() <= 12.0f ? 3.0f : 6.0f; }

    /** Grid lines at +/- k * step, inside the range. */
    std::vector<float> gridLinesDb()
    {
        std::vector<float> lines;
        const float step = gridStepDb();
        for (int k = 1; k * step < displayRangeDb.load() - 0.01f; ++k)
        {
            lines.push_back ((float) k * step);
            lines.push_back (-(float) k * step);
        }
        return lines;
    }

    /** Every other grid line gets a label, which keeps the axis readable. */
    std::vector<float> labelLinesDb()
    {
        std::vector<float> lines;
        const float step = gridStepDb();
        for (int k = 1; k * step < displayRangeDb.load() - 0.01f; k += 2)
        {
            lines.push_back ((float) k * step);
            lines.push_back (-(float) k * step);
        }
        return lines;
    }
    constexpr float handleRadius = 7.0f;
    // A stream node closer than this to its band node is drawn as a tab on the band node.
    constexpr float splitCollapsePx = 15.0f;
    /** Distance from the band node centre to the centre of a stream tab. */
    constexpr float splitTabOffset (bool selected) noexcept { return (selected ? 9.0f : 7.5f) + 6.5f; }
}

EQCurveComponent::EQCurveComponent (OpenEQAudioProcessor& processorToUse)
    : processor (processorToUse)
{
    setWantsKeyboardFocus (true);
    valueEditor.setJustification (juce::Justification::centred);
    valueEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff151a1d));
    valueEditor.setColour (juce::TextEditor::textColourId, juce::Colours::white);
    valueEditor.onReturnKey = [this] { commitValueEntry(); };
    valueEditor.onFocusLost = [this] { commitValueEntry(); };
    addChildComponent (valueEditor);
    startTimerHz (30);
}

EQCurveComponent::~EQCurveComponent() = default;

void EQCurveComponent::setDisplayRangeDb (float rangeDb) noexcept
{
    displayRangeDb.store (juce::jlimit (6.0f, 30.0f, rangeDb));
}

float EQCurveComponent::getDisplayRangeDb() noexcept
{
    return displayRangeDb.load();
}

float EQCurveComponent::frequencyToX (float freqHz, float width)
{
    const float logMin = std::log10 (minFreq);
    const float logMax = std::log10 (maxFreq);
    const float logF = std::log10 (juce::jlimit (minFreq, maxFreq, freqHz));
    return width * (logF - logMin) / (logMax - logMin);
}

float EQCurveComponent::xToFrequency (float x, float width)
{
    const float logMin = std::log10 (minFreq);
    const float logMax = std::log10 (maxFreq);
    const float t = juce::jlimit (0.0f, 1.0f, x / width);
    return std::pow (10.0f, logMin + t * (logMax - logMin));
}

float EQCurveComponent::dbToY (float db, float height)
{
    const float clamped = juce::jlimit (minDbNow(), maxDbNow(), db);
    return height * (1.0f - (clamped - minDbNow()) / (maxDbNow() - minDbNow()));
}

float EQCurveComponent::yToDb (float y, float height)
{
    const float t = juce::jlimit (0.0f, 1.0f, y / height);
    return maxDbNow() - t * (maxDbNow() - minDbNow());
}

void EQCurveComponent::resized() {}
void EQCurveComponent::timerCallback()
{
    processor.serviceIdleGraph();
    refreshBandView();
    stepNodeMotion();
    repaint();

    // 30 frames a second is enough for the idle graph; run at 60 while something is easing.
    const int wanted = (nodeMotionActive || curveMotionActive) ? 60 : 30;
    if (wanted != timerRateHz)
    {
        timerRateHz = wanted;
        startTimerHz (wanted);
    }
}

void EQCurveComponent::stepNodeMotion()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float dt = lastNodeStepMs > 0.0 ? (float) juce::jlimit (0.0, 0.1, (now - lastNodeStepMs) * 0.001) : 0.0f;
    lastNodeStepMs = now;
    const bool snap = graphDragActive() || ! isShowing();
    const float ease = 1.0f - std::exp (-dt / 0.06f);
    const float easeState = 1.0f - std::exp (-dt / 0.08f);

    nodeMotionActive = false;
    for (int i = 0; i < OpenEQAudioProcessor::numBands; ++i)
    {
        auto& m = nodeMotion[(size_t) i];
        const auto& band = bandView[(size_t) i];
        const bool visible = isBandVisible (i);
        const float targets[4] = { std::log (juce::jmax (1.0f, band.frequency)), band.gainDb, band.splitTransDb, band.splitTailDb };
        float* const shown[4] = { &m.logFrequency, &m.gainDb, &m.transDb, &m.tailDb };

        if (! m.primed || ! visible || snap)
        {
            for (int k = 0; k < 4; ++k)
                *shown[k] = targets[k];
            m.primed = true;
        }
        else
        {
            static constexpr float done[4] = { 0.0005f, 0.01f, 0.01f, 0.01f };
            for (int k = 0; k < 4; ++k)
            {
                const float delta = targets[k] - *shown[k];
                if (std::abs (delta) <= done[k])
                {
                    *shown[k] = targets[k];
                    continue;
                }
                *shown[k] += delta * ease;
                nodeMotionActive = true;
            }
        }

        if (! visible)
        {
            m.appear = 0.0f;
            m.hover = 0.0f;
            continue;
        }
        const float hoverTarget = (hoverBand == i || draggingBand == i) ? 1.0f : 0.0f;
        if (! isShowing())
            m.appear = 1.0f;
        m.appear += (1.0f - m.appear) * easeState;
        m.hover += (hoverTarget - m.hover) * easeState;
        if (m.appear > 0.995f) m.appear = 1.0f;
        if (std::abs (hoverTarget - m.hover) < 0.01f) m.hover = hoverTarget;
        if (m.appear < 1.0f || m.hover != hoverTarget)
            nodeMotionActive = true;
    }
}

void EQCurveComponent::refreshBandView()
{
    processor.readAllBandParameters (bandView);
    anySoloView = false;
    for (const auto& band : bandView)
    {
        if (band.active && band.solo)
        {
            anySoloView = true;
            break;
        }
    }
}

bool EQCurveComponent::isBandAudible (int index) const
{
    if (index < 0 || index >= OpenEQAudioProcessor::numBands)
        return false;
    const auto& band = bandView[(size_t) index];
    return band.active && (! anySoloView || band.solo);
}

bool EQCurveComponent::isBandVisible (int index) const
{
    if (index < 0 || index >= OpenEQAudioProcessor::numBands)
        return false;
    const auto& band = bandView[(size_t) index];
    return band.shown || band.active;
}

juce::Colour EQCurveComponent::colourForBand (int index)
{
    if (index >= 0 && index < (int) Theme::bandColourOverride.size() && Theme::bandColourOverride[(size_t) index].getAlpha() > 0)
        return Theme::bandColourOverride[(size_t) index];
    const float hue = std::fmod (0.08f + 0.17f * (float) index, 1.0f);
    return juce::Colour::fromHSV (hue, 0.72f, 0.95f, 1.0f);
}

const char* EQCurveComponent::badgeForMode (StereoMode mode)
{
    switch (mode)
    {
        case StereoMode::Left:  return "L";
        case StereoMode::Right: return "R";
        case StereoMode::Mid:   return "M";
        case StereoMode::Side:  return "S";
        case StereoMode::MidSide: return "MS";
        case StereoMode::Stereo:
        default:                 return "";
    }
}

void EQCurveComponent::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const float w = bounds.getWidth();
    const float h = bounds.getHeight();

    const juce::Colour gridCol = juce::Colour (0xff8faab3);

    // Frequency grid: every step of each decade. The decades are brightest, then the 2 and 5 steps.
    for (int decade = 10; decade <= 10000; decade *= 10)
    {
        for (int step = 1; step <= 9; ++step)
        {
            const float freq = (float) (decade * step);
            if (freq <= minFreq || freq >= maxFreq)
                continue;
            const bool major = step == 1;
            const bool mid = step == 2 || step == 5;
            g.setColour (gridCol.withAlpha (major ? 0.20f : (mid ? 0.10f : 0.045f)));
            g.drawVerticalLine ((int) frequencyToX (freq, w), 0.0f, h);
        }
    }

    g.setColour (gridCol.withAlpha (0.07f));
    for (float db : gridLinesDb())
        g.drawHorizontalLine ((int) dbToY (db, h), 0.0f, w);

    g.setColour (gridCol.withAlpha (0.32f));
    g.drawHorizontalLine ((int) dbToY (0.0f, h), 0.0f, w);

    // Frequency labels along the top and the bottom, so a panel over one edge leaves the other readable.
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    g.setColour (gridCol.withAlpha (0.70f));
    for (float freq : { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f })
    {
        const float x = frequencyToX (freq, w);
        const juce::String label = freq >= 1000.0f ? juce::String ((int) (freq / 1000.0f)) + "k" : juce::String ((int) freq);
        const auto justification = freq <= minFreq ? juce::Justification::centredLeft
                                 : (freq >= maxFreq ? juce::Justification::centredRight : juce::Justification::centred);
        const int left = freq <= minFreq ? 30 : (freq >= maxFreq ? (int) w - 40 : (int) x - 18);
        g.drawText (label, left, 3, 36, 14, justification);
        g.drawText (label, left, (int) h - 16, 36, 14, justification);
    }
    for (float db : labelLinesDb())
    {
        const float y = dbToY (db, h);
        g.drawText ((db > 0.0f ? "+" : "") + juce::String ((int) db), 4, (int) y - 7, 28, 14, juce::Justification::centredLeft);
    }
    g.drawText ("0", 4, (int) dbToY (0.0f, h) - 7, 28, 14, juce::Justification::centredLeft);

    juce::Path responsePath;
    juce::Path rightPath;
    juce::Path transPath;
    juce::Path tailPath;
    const int numPoints = curvePoints;
    const auto snapshot = processor.getGraphSnapshot();
    const bool mixed = ResponseModel::hasMixedRouting (snapshot);
    const bool hasSplit = ResponseModel::hasSplitBands (snapshot);
    // The curves ease toward the response instead of jumping, except while a node is dragged on the graph.
    const double curveNow = juce::Time::getMillisecondCounterHiRes();
    const float curveDt = lastCurveStepMs > 0.0 ? (float) juce::jlimit (0.0, 0.1, (curveNow - lastCurveStepMs) * 0.001) : 0.0f;
    lastCurveStepMs = curveNow;
    const float curveEase = (! curvePrimed || graphDragActive()) ? 1.0f : 1.0f - std::exp (-curveDt / 0.06f);
    curveMotionActive = false;
    auto easeCurve = [&] (int curve, int point, float target, bool snap)
    {
        float& shown = shownCurveDb[(size_t) curve][(size_t) point];
        const float delta = target - shown;
        if (snap || std::abs (delta) < 0.02f)
            shown = target;
        else
        {
            shown += delta * curveEase;
            curveMotionActive = true;
        }
        return shown;
    };
    for (int i = 0; i <= numPoints; ++i)
    {
        const float x = w * (float) i / (float) numPoints;
        const float freq = xToFrequency (x, w);
        float db = 0.0f;
        float rightDb = 0.0f;
        if (mixed)
        {
            const auto matrix = ResponseModel::responseMatrix (snapshot, (double) freq);
            const float leftMag = (float) std::abs (matrix.ll);
            const float rightMag = (float) std::abs (matrix.rr);
            db = leftMag > 0.0f ? juce::Decibels::gainToDecibels (leftMag) : 0.0f;
            rightDb = rightMag > 0.0f ? juce::Decibels::gainToDecibels (rightMag) : 0.0f;
        }
        else
        {
            const float mag = ResponseModel::totalMagnitude (snapshot, (double) freq);
            db = (std::isfinite (mag) && mag > 0.0f) ? juce::Decibels::gainToDecibels (mag) : 0.0f;
        }
        const float previousDb = curvePrimed ? shownCurveDb[0][(size_t) i] : db;
        db = easeCurve (0, i, db, false);
        const float y = dbToY (db, h);
        if (i == 0) responsePath.startNewSubPath (x, y);
        else responsePath.lineTo (x, y);
        if (mixed)
        {
            // A curve that has just come into view starts from the main curve and opens out from it.
            if (! rightCurveShown)
                shownCurveDb[1][(size_t) i] = previousDb;
            const float yR = dbToY (easeCurve (1, i, rightDb, false), h);
            if (i == 0) rightPath.startNewSubPath (x, yR);
            else rightPath.lineTo (x, yR);
        }
        if (hasSplit)
        {
            const float transMag = ResponseModel::splitTransientMagnitude (snapshot, (double) freq);
            const float tailMag = ResponseModel::splitTonalMagnitude (snapshot, (double) freq);
            const float tDb = (std::isfinite (transMag) && transMag > 0.0f) ? juce::Decibels::gainToDecibels (transMag) : 0.0f;
            const float sDb = (std::isfinite (tailMag) && tailMag > 0.0f) ? juce::Decibels::gainToDecibels (tailMag) : 0.0f;
            if (! splitCurvesShown)
                shownCurveDb[2][(size_t) i] = shownCurveDb[3][(size_t) i] = previousDb;
            const float yT = dbToY (easeCurve (2, i, tDb, false), h);
            const float yS = dbToY (easeCurve (3, i, sDb, false), h);
            if (i == 0)
            {
                transPath.startNewSubPath (x, yT);
                tailPath.startNewSubPath (x, yS);
            }
            else
            {
                transPath.lineTo (x, yT);
                tailPath.lineTo (x, yS);
            }
        }
    }
    curvePrimed = true;
    rightCurveShown = mixed;
    splitCurvesShown = hasSplit;

    const float fillAnchorY = dbToY (0.0f, h);
    // Split view: the transient stream is green and the tonal stream is blue. A soloed stream dims the other one.
    const int splitSoloMode = hasSplit ? currentSplitSolo() : 0;
    const float transDim = splitSoloMode == 2 ? 0.3f : 1.0f;
    const float tonalDim = splitSoloMode == 1 ? 0.3f : 1.0f;
    if (hasSplit)
    {
        auto fillStream = [&] (const juce::Path& path, juce::Colour colour, float dim)
        {
            juce::Path fill = path;
            fill.lineTo (w, fillAnchorY);
            fill.lineTo (0.0f, fillAnchorY);
            fill.closeSubPath();
            juce::ColourGradient gradient (colour.withAlpha (0.42f * dim), 0.0f, 0.0f,
                                           colour.withAlpha (0.42f * dim), 0.0f, h, false);
            gradient.addColour (juce::jlimit (0.01, 0.99, (double) (fillAnchorY / h)), colour.withAlpha (0.07f * dim));
            g.setGradientFill (gradient);
            g.fillPath (fill);
        };
        fillStream (tailPath, Theme::splitTonalCol, tonalDim);
        fillStream (transPath, Theme::splitTransCol, transDim);
    }
    else
    {
        juce::Path fillPath = responsePath;
        fillPath.lineTo (w, h);
        fillPath.lineTo (0.0f, h);
        fillPath.closeSubPath();
        g.setGradientFill (juce::ColourGradient (Theme::curveCol.withAlpha (0.25f), 0.0f, fillAnchorY,
                                                 Theme::curveCol.withAlpha (0.03f), 0.0f, h, false));
        g.fillPath (fillPath);
    }

    // Individual band responses (per-band): a faint fill from 0 dB and a thin line in the band colour.
    // All-pass bands have flat magnitude, so they are skipped here and shown through the phase trace.
    for (int i = 0; i < OpenEQAudioProcessor::numBands; ++i)
    {
        const auto& band = snapshot.bands[(size_t) i];
        if (! band.audible || band.stageCount <= 0 || ResponseModel::isAllPass (band.type))
            continue;

        if (band.splitOn)
        {
            juce::Path bandTrans, bandTonal;
            for (int p = 0; p <= numPoints; ++p)
            {
                const float x = w * (float) p / (float) numPoints;
                const double freq = (double) xToFrequency (x, w);
                const double tMag = ResponseModel::bandTransientMagnitude (band, freq, snapshot.sampleRate);
                const double sMag = ResponseModel::bandTonalMagnitude (band, freq, snapshot.sampleRate);
                const float yT = dbToY (tMag > 1.0e-9 ? (float) juce::Decibels::gainToDecibels (tMag) : minDbNow(), h);
                const float yS = dbToY (sMag > 1.0e-9 ? (float) juce::Decibels::gainToDecibels (sMag) : minDbNow(), h);
                if (p == 0) { bandTrans.startNewSubPath (x, yT); bandTonal.startNewSubPath (x, yS); }
                else        { bandTrans.lineTo (x, yT);          bandTonal.lineTo (x, yS); }
            }
            g.setColour (Theme::splitTonalCol.withAlpha (0.45f * tonalDim));
            g.strokePath (bandTonal, juce::PathStrokeType (1.0f));
            g.setColour (Theme::splitTransCol.withAlpha (0.45f * transDim));
            g.strokePath (bandTrans, juce::PathStrokeType (1.0f));
            continue;
        }

        juce::Path bandPath;
        for (int p = 0; p <= numPoints; ++p)
        {
            const float x = w * (float) p / (float) numPoints;
            const double mag = ResponseModel::bandMagnitude (band, (double) xToFrequency (x, w), snapshot.sampleRate);
            const float db = mag > 1.0e-9 ? (float) juce::Decibels::gainToDecibels (mag) : minDbNow();
            const float y = dbToY (db, h);
            if (p == 0) bandPath.startNewSubPath (x, y);
            else bandPath.lineTo (x, y);
        }

        juce::Path bandFill = bandPath;
        bandFill.lineTo (w, fillAnchorY);
        bandFill.lineTo (0.0f, fillAnchorY);
        bandFill.closeSubPath();

        const auto colour = colourForBand (i);
        g.setColour (colour.withAlpha (0.10f));
        g.fillPath (bandFill);
        g.setColour (colour.withAlpha (0.50f));
        g.strokePath (bandPath, juce::PathStrokeType (1.0f));
    }

    if (hasSplit)
    {
        // Two curves, one per stream. Where no band is split they lie on top of each other.
        auto strokeStream = [&] (const juce::Path& path, juce::Colour colour, float dim)
        {
            g.setColour (colour.withAlpha (0.20f * dim));
            g.strokePath (path, juce::PathStrokeType (5.0f));
            g.setColour (colour.withAlpha (dim));
            g.strokePath (path, juce::PathStrokeType (2.0f));
        };
        strokeStream (tailPath, Theme::splitTonalCol, tonalDim);
        strokeStream (transPath, Theme::splitTransCol, transDim);

        // Stream keys, top right. A click solos that stream, and a second click ends the solo.
        for (int which = 1; which <= 2; ++which)
        {
            const auto rect = splitLegendRect (which);
            const auto colour = which == 1 ? Theme::splitTransCol : Theme::splitTonalCol;
            const bool soloed = splitSoloMode == which;
            g.setColour (soloed ? colour : juce::Colour (0xee121619));
            g.fillRoundedRectangle (rect, rect.getHeight() * 0.5f);
            g.setColour (soloed ? colour.brighter (0.3f) : colour.withAlpha (0.55f));
            g.drawRoundedRectangle (rect, rect.getHeight() * 0.5f, 1.0f);
            g.setColour (soloed ? juce::Colour (0xff0b0f11) : colour);
            g.fillEllipse (rect.getX() + 8.0f, rect.getCentreY() - 3.5f, 7.0f, 7.0f);
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText (which == 1 ? "TRANSIENT" : "TONAL", rect.withTrimmedLeft (20.0f).withTrimmedRight (8.0f).toNearestInt(),
                        juce::Justification::centredLeft);
        }
    }
    else
    {
        // Glow under the curve: several widening passes.
        g.setColour (Theme::curveCol.withAlpha (0.08f));
        g.strokePath (responsePath, juce::PathStrokeType (16.0f));
        g.setColour (Theme::curveCol.withAlpha (0.14f));
        g.strokePath (responsePath, juce::PathStrokeType (10.0f));
        g.setColour (Theme::curveCol.withAlpha (0.28f));
        g.strokePath (responsePath, juce::PathStrokeType (5.0f));
        g.setColour (Theme::curveCol.brighter (0.15f));
        g.strokePath (responsePath, juce::PathStrokeType (2.4f));
        if (mixed)
        {
            g.setColour (juce::Colour (0xffef5f5f).withAlpha (0.9f));
            g.strokePath (rightPath, juce::PathStrokeType (1.6f));
        }
    }

    bool anyAllPass = false;
    for (const auto& band : snapshot.bands)
        if (band.audible && ResponseModel::isAllPass (band.type))
            anyAllPass = true;
    if (anyAllPass)
    {
        juce::Path phasePath;
        for (int i = 0; i <= numPoints; ++i)
        {
            const float x = w * (float) i / (float) numPoints;
            const float freq = xToFrequency (x, w);
            double phase = 0.0;
            for (const auto& band : snapshot.bands)
            {
                if (! band.audible || ! ResponseModel::isAllPass (band.type))
                    continue;
                for (int stage = 0; stage < band.stageCount; ++stage)
                    phase += ResponseModel::stagePhase (band.stages[(size_t) stage], (double) freq, snapshot.sampleRate);
            }
            const float wrapped = (float) std::atan2 (std::sin (phase), std::cos (phase));
            const float y = h * (0.5f - wrapped / juce::MathConstants<float>::twoPi);
            if (i == 0) phasePath.startNewSubPath (x, y);
            else phasePath.lineTo (x, y);
        }
        g.setColour (juce::Colour (0xffffc46b).withAlpha (0.9f));
        g.strokePath (phasePath, juce::PathStrokeType (1.2f));
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText ("PHASE", (int) w - 58, 46, 50, 12, juce::Justification::centredRight);
    }

    refreshBandView();
    for (int i = 0; i < OpenEQAudioProcessor::numBands; ++i)
    {
        if (! isBandVisible (i))
            continue;

        const auto pos = getHandlePosition (i);
        auto colour = colourForBand (i);
        if (! isBandAudible (i))
            colour = colour.withAlpha (0.45f);
        const bool selected = (i == selectedBand);
        const auto& live = bandView[(size_t) i];
        // A new node grows into place, and a node under the pointer swells slightly.
        const auto& motion = nodeMotion[(size_t) i];
        const float radius = ((selected ? 9.0f : 7.5f) + 1.5f * motion.hover) * (0.35f + 0.65f * motion.appear);
        colour = colour.withMultipliedAlpha (0.3f + 0.7f * motion.appear);

        if (hasSplitHandles (i))
        {
            // Each stream has its own gain node on the band's frequency line. A stream that sits at the band gain
            // folds into a small tab on the band node: green above for the transient, blue below for the tonal.
            auto drawStream = [&] (bool transient)
            {
                const auto kind = transient ? DragKind::TransientSatellite : DragKind::TailSatellite;
                const auto p = transient ? getTransientHandlePosition (i) : getTailHandlePosition (i);
                const bool dragged = draggingBand == i && dragKind == kind;
                const bool hot = dragged || (hoverSatelliteBand == i && hoverSatelliteKind == kind);
                const float dim = isBandAudible (i) ? (transient ? transDim : tonalDim) : 0.45f;
                const auto base = transient ? Theme::splitTransCol : Theme::splitTonalCol;
                const auto col = (hot ? base.brighter (0.35f) : base).withMultipliedAlpha (dim);

                if (isSplitStreamCollapsed (i, transient))
                {
                    const float half = hot ? 6.5f : 5.5f;
                    const float dir = transient ? -1.0f : 1.0f;
                    juce::Path tab;
                    tab.addTriangle (p.x - half, p.y - dir * 3.5f, p.x + half, p.y - dir * 3.5f, p.x, p.y + dir * 3.5f);
                    g.setColour (juce::Colour (0xff0b0f11));
                    g.strokePath (tab, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved));
                    g.setColour (col);
                    g.fillPath (tab);
                }
                else
                {
                    const float r = hot ? 6.5f : 5.5f;
                    g.setColour (col.withMultipliedAlpha (0.7f));
                    g.drawLine (pos.x, pos.y, p.x, p.y, 1.5f);
                    g.setColour (juce::Colour (0xff0b0f11));
                    g.fillEllipse (p.x - r - 1.5f, p.y - r - 1.5f, (r + 1.5f) * 2.0f, (r + 1.5f) * 2.0f);
                    g.setColour (col);
                    g.fillEllipse (p.x - r, p.y - r, r * 2.0f, r * 2.0f);
                }

                if (hot)
                {
                    const float offset = dragged ? dragSplitGain : (transient ? live.splitTransDb : live.splitTailDb);
                    g.setColour (base.brighter (0.2f));
                    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
                    g.drawText (juce::String (transient ? "TRANSIENT  " : "TONAL  ") + ValueFormat::gainDB (live.gainDb + offset) + " dB",
                                (int) p.x + 13, (int) p.y - 7, 170, 14, juce::Justification::centredLeft);
                }
            };
            drawStream (false);
            drawStream (true);
        }

        // Node: a lit orb in the band colour with a soft halo. The selected node is larger and ringed in white.
        {
            const juce::Rectangle<float> orb (pos.x - radius, pos.y - radius, radius * 2.0f, radius * 2.0f);
            // The halo of the selected node breathes slowly. A dynamic band's halo also swells with the gain
            // change its dynamics are applying right now, so a working band is visible at a glance.
            float glow = motion.hover * 0.25f;
            if (selected)
                glow += 0.18f * (0.5f + 0.5f * (float) std::sin (juce::Time::getMillisecondCounterHiRes() * 0.0026));
            if (live.dynamic && isBandAudible (i) && ResponseModel::supportsDynamics (live.type))
                glow += juce::jmin (1.2f, std::abs (processor.getDynamicGainDb (i)) / 6.0f);
            for (int ring = 3; ring >= 1; --ring)
            {
                g.setColour (colour.withMultipliedAlpha ((selected ? 0.20f : 0.10f) * (1.0f + 0.6f * glow) / (float) ring));
                g.fillEllipse (orb.expanded ((float) ring * (selected ? 3.5f : 2.2f) * (1.0f + glow)));
            }
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.fillEllipse (orb.translated (0.0f, 1.5f));
            g.setGradientFill (juce::ColourGradient (colour.brighter (0.7f), pos.x - radius * 0.35f, pos.y - radius * 0.45f,
                                                     colour.darker (0.6f), pos.x + radius * 0.7f, pos.y + radius * 0.9f, true));
            g.fillEllipse (orb);
            g.setColour (juce::Colour (0xff0b0f11).withAlpha (0.7f));
            g.drawEllipse (orb, 1.0f);
            if (selected)
            {
                g.setColour (juce::Colours::white.withAlpha (0.95f));
                g.drawEllipse (orb.expanded (2.5f), 1.5f);
            }
        }

        // Band number, shown when the display setting asks for it (show_bands_index).
        if (AppSettings::get().showBandNumbers())
        {
            g.setColour (juce::Colour (0xff0b0f11).withAlpha (isBandAudible (i) ? 0.95f : 0.6f));
            g.setFont (juce::FontOptions (selected ? 10.5f : 9.5f, juce::Font::bold));
            g.drawText (juce::String (i + 1), (int) (pos.x - radius), (int) (pos.y - radius),
                        (int) (radius * 2.0f), (int) (radius * 2.0f), juce::Justification::centred);
        }
        const char* badge = badgeForMode (bandView[(size_t) i].stereoMode);
        if (badge[0] != 0)
        {
            g.setColour (colour);
            g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
            g.drawText (badge, (int) (pos.x + radius - 2.0f), (int) (pos.y - radius - 10.0f), 16, 12,
                        juce::Justification::centredLeft);
        }

        if (selected && live.dynamic && isBandAudible (i) && ResponseModel::supportsDynamics (live.type))
        {
            const float offset = processor.getDynamicGainDb (i);
            const float env = processor.getDynamicEnvelopeDb (i);
            const float threshold = processor.getDynamicThresholdDb (i);
            if (ResponseModel::usesGain (live.type) && std::abs (offset) > 0.05f)
            {
                const float staticGain = (draggingBand == i) ? dragGain : live.gainDb;
                const float y = dbToY (juce::jlimit (minDbNow(), maxDbNow(), staticGain + offset), h);
                g.setColour (colour.withAlpha (0.95f));
                g.drawLine (pos.x, pos.y, pos.x, y, 1.0f);
                g.drawEllipse (pos.x - 4.0f, y - 4.0f, 8.0f, 8.0f, 1.2f);
            }
            g.setColour (juce::Colours::white.withAlpha (0.88f));
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText ("E " + juce::String (env, 1)
                            + "  T " + juce::String (threshold, 1)
                            + "  R " + juce::String (live.dynRangeDb, 1)
                            + "  G " + juce::String (offset, 1),
                        (int) pos.x + 12, (int) pos.y - 18, 188, 14, juce::Justification::centredLeft);
        }
    }

    // Floating band controller: Q, monitor, and slope icons above the selected node.
    const auto floatingMode = AppSettings::get().floatingController();
    if (selectedBand >= 0 && isBandVisible (selectedBand) && ! grabPreview && floatingMode != AppSettings::FloatingController::Off)
    {
        const auto& live = bandView[(size_t) selectedBand];
        const auto pos = getHandlePosition (selectedBand);
        const bool showQ = ResponseModel::usesQ (live.type);
        const bool showSlope = ResponseModel::usesSlope (live.type);
        const bool showRemove = AppSettings::get().showFloatingRemoveButton();
        for (int which = 0; which < 4; ++which)
        {
            if ((which == 0 && ! showQ) || (which == 2 && ! showSlope))
                continue;
            if (which == 3 && ! showRemove)
                continue;
            if (floatingMode == AppSettings::FloatingController::MonitorOnly && which != 1)
                continue;
            const auto rect = floatIconRect (which, pos);
            g.setColour (juce::Colour (0xff151a1d).withAlpha (0.9f));
            g.fillEllipse (rect);
            g.setColour (colourForBand (selectedBand).withAlpha (floatDragKind == which + 1 ? 1.0f : 0.8f));
            g.drawEllipse (rect, 1.4f);
            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
            g.drawText (which == 0 ? "Q" : (which == 1 ? "H" : (which == 2 ? "S" : "X")), rect.toNearestInt(), juce::Justification::centred);
        }
    }

    if (grabPreview)
    {
        const float x = frequencyToX (previewHz, w);
        const float y = dbToY (previewDb, h);
        g.setColour (juce::Colours::white.withAlpha (0.92f));
        g.drawEllipse (x - 8.0f, y - 8.0f, 16.0f, 16.0f, 1.5f);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (ValueFormat::frequencyWithNote (previewHz) + "  " + ValueFormat::gainDB (previewDb),
                    (int) x + 10, (int) y - 16, 190, 14, juce::Justification::centredLeft);
    }

    // Hover readout for the node under the pointer. With no pointer on a node, the selected band's readout can stay
    // on screen (Band display info around the selected band).
    const int infoBand = hoverBand >= 0 ? hoverBand
                                        : (AppSettings::get().showSelectedBandInfo() ? selectedBand : -1);
    if (infoBand >= 0 && draggingBand < 0 && hoverSatelliteBand < 0 && isBandVisible (infoBand) && ! grabPreview)
    {
        const auto& live = bandView[(size_t) infoBand];
        const auto pos = getHandlePosition (infoBand);
        juce::String text = ValueFormat::frequencyWithNote (live.frequency);
        if (ResponseModel::usesGain (live.type))
            text += "  " + ValueFormat::gainDB (live.gainDb) + " dB";
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText (text, (int) pos.x + 12, (int) pos.y + 8, 200, 14, juce::Justification::centredLeft);
    }

    // Cursor axes: crosshair lines through the pointer, with its frequency and gain (cursor axes).
    if (AppSettings::get().showCursorAxes() && cursorInside && draggingBand < 0)
    {
        g.setColour (juce::Colours::white.withAlpha (0.22f));
        g.drawVerticalLine ((int) cursorPos.x, 0.0f, h);
        g.drawHorizontalLine ((int) cursorPos.y, 0.0f, w);
        const float freq = xToFrequency (cursorPos.x, w);
        const float db = yToDb (cursorPos.y, h);
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (ValueFormat::frequencyWithNote (freq) + "  " + ValueFormat::gainDB (db) + " dB",
                    (int) cursorPos.x + 10, (int) cursorPos.y - 18, 200, 14, juce::Justification::centredLeft);
    }

    // Live readout while a band is dragged: frequency with its nearest note, and gain for gain-bearing types.
    if (draggingBand >= 0 && dragGesture && dragKind == DragKind::Center)
    {
        const float x = frequencyToX (dragFrequency, w);
        const float y = dbToY (dragGain, h);
        juce::String text = ValueFormat::frequencyWithNote (dragFrequency);
        if (ResponseModel::usesGain (bandView[(size_t) draggingBand].type))
            text += "  " + ValueFormat::gainDB (dragGain) + " dB";
        g.setColour (juce::Colours::white.withAlpha (0.92f));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (text, (int) x + 12, (int) y - 24, 200, 14, juce::Justification::centredLeft);
    }

    if (processor.linearPhaseOmitsDynamics() || processor.linearPhaseOmitsAllPass() || processor.getMonitorMode() != 0)
    {
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (processor.restrictionNotice(), 38, 22, (int) w - 76, 16, juce::Justification::centredLeft);
    }
}

juce::Rectangle<float> EQCurveComponent::floatIconRect (int which, juce::Point<float> handle) const
{
    // which: 0 = Q, 1 = monitor, 2 = slope, 3 = remove. The icons sit in a row above the handle.
    // A split band keeps that column for its transient node, so its icons sit in a row beside the handle:
    // on the left, or on the right when the handle is close to the left edge.
    if (hasSplitHandles (selectedBand))
    {
        const bool onRight = handle.x < 104.0f;
        const float x = onRight ? handle.x + 26.0f + (float) which * 22.0f
                                : handle.x - 26.0f - (float) (3 - which) * 22.0f;
        return { x - 8.0f, handle.y - 8.0f, 16.0f, 16.0f };
    }
    const float x = handle.x + (float) (which - 1) * 24.0f;
    return { x - 8.0f, handle.y - 34.0f, 16.0f, 16.0f };
}

juce::Point<float> EQCurveComponent::getHandlePosition (int bandIndex) const
{
    const auto& band = bandView[(size_t) bandIndex];
    const float w = (float) getWidth();
    const float h = (float) getHeight();
    const bool dragging = bandIndex == draggingBand;
    const auto& motion = nodeMotion[(size_t) bandIndex];
    const bool eased = motion.primed && ! graphDragActive();
    const float frequency = dragging ? dragFrequency : (eased ? std::exp (motion.logFrequency) : band.frequency);
    const float gain = dragging ? dragGain : (eased ? motion.gainDb : band.gainDb);
    const float x = frequencyToX (frequency, w);
    const float y = ResponseModel::usesGain (band.type) ? dbToY (gain, h) : dbToY (0.0f, h);
    return { x, y };
}

bool EQCurveComponent::hasSplitHandles (int bandIndex) const
{
    if (! isBandVisible (bandIndex))
        return false;
    const auto& band = bandView[(size_t) bandIndex];
    return band.splitOn && ResponseModel::usesGain (band.type);
}

int EQCurveComponent::currentSplitSolo() const
{
    const auto* solo = processor.apvts.getRawParameterValue ("splitSolo");
    return solo != nullptr ? juce::jlimit (0, 2, (int) solo->load()) : 0;
}

bool EQCurveComponent::anySplitBandAudible() const
{
    for (int i = 0; i < OpenEQAudioProcessor::numBands; ++i)
        if (isBandAudible (i) && bandView[(size_t) i].splitOn)
            return true;
    return false;
}

juce::Rectangle<float> EQCurveComponent::splitLegendRect (int which) const
{
    // which: 1 = transient, 2 = tonal. Right-aligned at the top of the graph.
    const float tonalW = 62.0f, transW = 92.0f, height = 20.0f, gap = 6.0f;
    const float right = (float) getWidth() - 12.0f;
    if (which == 2)
        return { right - tonalW, 22.0f, tonalW, height };
    return { right - tonalW - gap - transW, 22.0f, transW, height };
}

float EQCurveComponent::splitStreamY (int bandIndex, bool transient) const
{
    const auto& band = bandView[(size_t) bandIndex];
    const bool centreDragged = bandIndex == draggingBand && dragKind == DragKind::Center;
    const bool streamDragged = bandIndex == draggingBand
        && dragKind == (transient ? DragKind::TransientSatellite : DragKind::TailSatellite);
    const auto& motion = nodeMotion[(size_t) bandIndex];
    const bool eased = motion.primed && ! graphDragActive();
    const float gain = centreDragged ? dragGain : (eased ? motion.gainDb : band.gainDb);
    const float offset = streamDragged ? dragSplitGain
                       : eased ? (transient ? motion.transDb : motion.tailDb)
                               : (transient ? band.splitTransDb : band.splitTailDb);
    return dbToY (gain + offset, (float) getHeight());
}

bool EQCurveComponent::isSplitStreamCollapsed (int bandIndex, bool transient) const
{
    // A stream being dragged stays a node, so it follows the pointer through the band gain.
    if (bandIndex == draggingBand && dragKind == (transient ? DragKind::TransientSatellite : DragKind::TailSatellite))
        return false;
    return std::abs (splitStreamY (bandIndex, transient) - getHandlePosition (bandIndex).y) < splitCollapsePx;
}

juce::Point<float> EQCurveComponent::getTransientHandlePosition (int bandIndex) const
{
    const auto centre = getHandlePosition (bandIndex);
    if (isSplitStreamCollapsed (bandIndex, true))
        return { centre.x, centre.y - splitTabOffset (bandIndex == selectedBand) };
    return { centre.x, splitStreamY (bandIndex, true) };
}

juce::Point<float> EQCurveComponent::getTailHandlePosition (int bandIndex) const
{
    const auto centre = getHandlePosition (bandIndex);
    if (isSplitStreamCollapsed (bandIndex, false))
        return { centre.x, centre.y + splitTabOffset (bandIndex == selectedBand) };
    return { centre.x, splitStreamY (bandIndex, false) };
}

EQCurveComponent::HandleHit EQCurveComponent::findHandleHitAt (juce::Point<float> pos) const
{
    // First priority: the stream nodes and tabs of split bands.
    for (int i = 0; i < OpenEQAudioProcessor::numBands; ++i)
    {
        if (! hasSplitHandles (i))
            continue;
        const auto pT = getTransientHandlePosition (i);
        const auto pS = getTailHandlePosition (i);
        auto hits = [&] (juce::Point<float> p, bool transient)
        {
            if (isSplitStreamCollapsed (i, transient))
                return std::abs (pos.x - p.x) <= 8.0f && std::abs (pos.y - p.y) <= 5.5f;
            return p.getDistanceFrom (pos) <= 9.5f;
        };
        const bool hitT = hits (pT, true);
        const bool hitS = hits (pS, false);
        // Two nodes at the same gain share one spot: the upper half takes the transient, the lower half the tonal.
        if (hitT && hitS)
            return { i, pos.y <= 0.5f * (pT.y + pS.y) ? DragKind::TransientSatellite : DragKind::TailSatellite };
        if (hitT)
            return { i, DragKind::TransientSatellite };
        if (hitS)
            return { i, DragKind::TailSatellite };
    }
    // Second priority: center handles
    for (int i = 0; i < OpenEQAudioProcessor::numBands; ++i)
    {
        if (! isBandVisible (i))
            continue;
        if (getHandlePosition (i).getDistanceFrom (pos) <= handleRadius + 4.0f)
            return { i, DragKind::Center };
    }
    return { -1, DragKind::None };
}

bool EQCurveComponent::clickSplitLegend (juce::Point<float> pos)
{
    if (! anySplitBandAudible())
        return false;
    for (int which = 1; which <= 2; ++which)
    {
        if (! splitLegendRect (which).contains (pos))
            continue;
        processor.beginEdit (which == 1 ? "Solo Transient" : "Solo Tonal");
        processor.setRawFromUI ("splitSolo", currentSplitSolo() == which ? 0.0f : (float) which);
        processor.endEdit();
        repaint();
        return true;
    }
    return false;
}

int EQCurveComponent::findHandleAt (juce::Point<float> pos) const
{
    return findHandleHitAt (pos).band;
}

int EQCurveComponent::findFreeBandSlot() const
{
    for (int i = 0; i < OpenEQAudioProcessor::numBands; ++i)
        if (! bandView[(size_t) i].shown && ! bandView[(size_t) i].active)
            return i;
    return -1;
}

void EQCurveComponent::setGrabArmed (bool armed)
{
    grabArmed = armed;
    if (! armed)
        grabPreview = false;
    repaint();
}

void EQCurveComponent::selectBand (int index)
{
    selectedBand = index;
    if (onBandSelected != nullptr)
        onBandSelected (index);
    repaint();
}

void EQCurveComponent::mouseDown (const juce::MouseEvent& e)
{
    refreshBandView();
    grabKeyboardFocus();
    if (! e.mods.isPopupMenu() && clickSplitLegend (e.position))
        return;
    const auto hit = findHandleHitAt (e.position);

    if (e.mods.isPopupMenu())
    {
        if (hit.band >= 0)
        {
            if (hit.kind == DragKind::TransientSatellite)
            {
                processor.beginEdit ("Reset Transient Gain");
                processor.setRawFromUI (Params::bandSplitTransID (hit.band), 0.0f);
                processor.endEdit();
                repaint();
                return;
            }
            if (hit.kind == DragKind::TailSatellite)
            {
                processor.beginEdit ("Reset Tail Gain");
                processor.setRawFromUI (Params::bandSplitTailID (hit.band), 0.0f);
                processor.endEdit();
                repaint();
                return;
            }
            // Ctrl/Command + right-click on a band deletes it when the setting allows.
            if ((e.mods.isCtrlDown() || e.mods.isCommandDown()) && AppSettings::get().ctrlRightClickDeletesBand())
            {
                selectBand (hit.band);
                selection = { hit.band };
                removeSelected();
                return;
            }
            selectBand (hit.band);
            BandMenu::show (processor, hit.band, this, [this] (int next) { selectBand (next); });
            return;
        }

        // Double right-click on empty space toggles spectrum grab.
        if (AppSettings::get().doubleRightClickGrab())
        {
            const auto now = juce::Time::getMillisecondCounter();
            if (now - lastRightClickMs < 450 && e.position.getDistanceFrom (lastRightClickPos) < 12.0f)
            {
                lastRightClickMs = 0;
                if (onGrabRequested != nullptr)
                    onGrabRequested (! grabArmed);
                else
                    setGrabArmed (! grabArmed);
            }
            else
            {
                lastRightClickMs = now;
                lastRightClickPos = e.position;
            }
        }
        return;
    }

    if (e.mods.isAltDown() && hit.band >= 0)
    {
        processor.setAudition (true, hit.band);
        selectBand (hit.band);
        return;
    }

    if (grabArmed && hit.band < 0)
    {
        const float clicked = xToFrequency (e.position.x, (float) getWidth());
        previewHz = peakFrequencyForGrab != nullptr ? peakFrequencyForGrab (clicked) : clicked;
        previewDb = 0.0f;
        grabPreview = true;
        repaint();
        return;
    }

    if (selectedBand >= 0 && isBandVisible (selectedBand)
        && AppSettings::get().floatingController() != AppSettings::FloatingController::Off)
    {
        const auto& live = bandView[(size_t) selectedBand];
        const auto pos = getHandlePosition (selectedBand);
        const bool monitorOnly = AppSettings::get().floatingController() == AppSettings::FloatingController::MonitorOnly;
        for (int which = 0; which < 4; ++which)
        {
            if (monitorOnly && which != 1)
                continue;
            if (which == 3)
            {
                if (! AppSettings::get().showFloatingRemoveButton() || ! floatIconRect (3, pos).expanded (3.0f).contains (e.position))
                    continue;
                selection = { selectedBand };
                removeSelected();
                return;
            }
            if (which == 0 && ! ResponseModel::usesQ (live.type))
                continue;
            if (which == 2 && ! ResponseModel::usesSlope (live.type))
                continue;
            if (! floatIconRect (which, pos).expanded (3.0f).contains (e.position))
                continue;

            floatBand = selectedBand;
            floatDragStartY = e.position.y;
            if (which == 0)
            {
                floatDragKind = 1;
                floatStartValue = live.q;
                processor.beginEdit ("Adjust Q");
            }
            else if (which == 2)
            {
                floatDragKind = 2;
                floatStartValue = processor.apvts.getRawParameterValue (Params::bandSlopeNormID (selectedBand))->load();
                processor.beginEdit ("Adjust Slope");
            }
            else
            {
                floatDragKind = 3;
                processor.setAudition (true, selectedBand);
            }
            return;
        }
    }

    if (hit.band >= 0)
    {
        if (hit.kind == DragKind::Center)
        {
            if (e.mods.isShiftDown())
            {
                if (std::find (selection.begin(), selection.end(), hit.band) == selection.end())
                    selection.push_back (hit.band);
            }
            else if (std::find (selection.begin(), selection.end(), hit.band) == selection.end())
            {
                selection.clear();
                selection.push_back (hit.band);
            }
        }
        else
        {
            selection.clear();
            selection.push_back (hit.band);
        }

        draggingBand = hit.band;
        dragKind = hit.kind;
        for (int i = 0; i < OpenEQAudioProcessor::numBands; ++i)
        {
            originFrequency[(size_t) i] = bandView[(size_t) i].frequency;
            originGain[(size_t) i] = bandView[(size_t) i].gainDb;
        }
        originSplitTrans = bandView[(size_t) hit.band].splitTransDb;
        originSplitTail = bandView[(size_t) hit.band].splitTailDb;
        dragFrequency = originFrequency[(size_t) hit.band];
        dragGain = originGain[(size_t) hit.band];
        if (dragKind == DragKind::TransientSatellite)
            dragSplitGain = originSplitTrans;
        else if (dragKind == DragKind::TailSatellite)
            dragSplitGain = originSplitTail;

        selectBand (hit.band);
        if (dragKind == DragKind::TransientSatellite)
            processor.beginEdit ("Edit Transient Gain");
        else if (dragKind == DragKind::TailSatellite)
            processor.beginEdit ("Edit Tail Gain");
        else
            processor.beginEdit ("Edit Band");

        dragGesture = true;
    }
}

void EQCurveComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (floatDragKind == 1 || floatDragKind == 2)
    {
        const float dy = floatDragStartY - e.position.y;
        if (floatDragKind == 1)
        {
            // Up raises Q by an octave per 60 px. Shift gives finer control.
            const float scale = e.mods.isShiftDown() ? 0.25f : 1.0f;
            const float q = juce::jlimit (0.025f, 40.0f, floatStartValue * std::pow (2.0f, scale * dy / 60.0f));
            processor.setRawFromUI (Params::bandQID (floatBand), q);
        }
        else
        {
            // Up makes the slope steeper: the full range over 300 px (a quarter of that with Shift).
            const float scale = e.mods.isShiftDown() ? 0.25f : 1.0f;
            const float slope = juce::jlimit (0.0f, 1.0f, floatStartValue + scale * dy / 300.0f);
            processor.setRawFromUI (Params::bandSlopeNormID (floatBand), slope);
        }
        repaint();
        return;
    }

    if (grabPreview)
    {
        previewDb = juce::jlimit (-30.0f, 30.0f, yToDb (e.position.y, (float) getHeight()));
        repaint();
        return;
    }

    if (draggingBand < 0 || ! dragGesture)
        return;

    const float w = (float) getWidth();
    const float h = (float) getHeight();

    if (dragKind == DragKind::TransientSatellite || dragKind == DragKind::TailSatellite)
    {
        // A stream node moves in gain only, and by the distance dragged, so picking up a tab does not jump the value.
        // The band node moves the frequency and both streams.
        const bool transient = dragKind == DragKind::TransientSatellite;
        float delta = yToDb (e.position.y, h) - yToDb (e.mouseDownPosition.y, h);
        if (e.mods.isShiftDown())
            delta *= 0.15f;
        dragSplitGain = juce::jlimit (-30.0f, 30.0f, (transient ? originSplitTrans : originSplitTail) + delta);
        processor.setRawFromUI (transient ? Params::bandSplitTransID (draggingBand) : Params::bandSplitTailID (draggingBand),
                                dragSplitGain);
        repaint();
        return;
    }

    float nextFrequency = xToFrequency (e.position.x, w);
    float nextGain = yToDb (e.position.y, h);
    if (e.mods.isShiftDown())
    {
        nextFrequency = originFrequency[(size_t) draggingBand] + 0.15f * (nextFrequency - originFrequency[(size_t) draggingBand]);
        nextGain = originGain[(size_t) draggingBand] + 0.15f * (nextGain - originGain[(size_t) draggingBand]);
    }

    const float frequencyRatio = originFrequency[(size_t) draggingBand] > 1.0f
        ? nextFrequency / originFrequency[(size_t) draggingBand] : 1.0f;
    const float gainDelta = nextGain - originGain[(size_t) draggingBand];
    dragFrequency = nextFrequency;
    dragGain = nextGain;

    for (int index : selection)
    {
        const float frequency = juce::jlimit (20.0f, 20000.0f, originFrequency[(size_t) index] * frequencyRatio);
        processor.setRawFromUI (Params::bandFreqID (index), frequency);
        if (ResponseModel::usesGain (bandView[(size_t) index].type))
            processor.setRawFromUI (Params::bandGainID (index), juce::jlimit (-30.0f, 30.0f, originGain[(size_t) index] + gainDelta));
    }
    repaint();
}

void EQCurveComponent::mouseUp (const juce::MouseEvent&)
{
    if (floatDragKind == 1 || floatDragKind == 2)
        processor.endEdit();
    else if (floatDragKind == 3)
        processor.setAudition (false, floatBand);
    if (floatDragKind != 0)
    {
        floatDragKind = 0;
        floatBand = -1;
        repaint();
        return;
    }

    if (grabPreview)
    {
        const int slot = findFreeBandSlot();
        if (slot >= 0)
        {
            processor.beginEdit ("Spectrum Grab");
            processor.setRawFromUI (Params::bandTypeID (slot), (float) FilterType::Bell);
            processor.setRawFromUI (Params::bandFreqID (slot), previewHz);
            processor.setRawFromUI (Params::bandGainID (slot), previewDb);
            processor.setRawFromUI (Params::bandQID (slot), 2.0f);
            processor.setRawFromUI (Params::bandShownID (slot), 1.0f);
            processor.setRawFromUI (Params::bandActiveID (slot), 1.0f);
            processor.endEdit();
            selectBand (slot);
        }
        grabPreview = false;
        repaint();
        return;
    }

    if (dragGesture)
        processor.endEdit();
    dragGesture = false;
    draggingBand = -1;
    dragKind = DragKind::None;
    processor.setAudition (false, -1);
}

void EQCurveComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (grabArmed)
        return;

    refreshBandView();
    // The second click of a double-click on a stream key already toggled the solo in mouseDown.
    if (anySplitBandAudible() && (splitLegendRect (1).contains (e.position) || splitLegendRect (2).contains (e.position)))
        return;
    const auto hit = findHandleHitAt (e.position);
    if (hit.band >= 0)
    {
        if (hit.kind == DragKind::TransientSatellite)
        {
            processor.beginEdit ("Reset Transient Gain");
            processor.setRawFromUI (Params::bandSplitTransID (hit.band), 0.0f);
            processor.endEdit();
            repaint();
            return;
        }
        if (hit.kind == DragKind::TailSatellite)
        {
            processor.beginEdit ("Reset Tail Gain");
            processor.setRawFromUI (Params::bandSplitTailID (hit.band), 0.0f);
            processor.endEdit();
            repaint();
            return;
        }
        openValueEntry (hit.band);
        return;
    }

    const int freeSlot = findFreeBandSlot();
    if (freeSlot < 0)
        return;

    const float freq = xToFrequency (e.position.x, (float) getWidth());
    const float db = yToDb (e.position.y, (float) getHeight());

    // New band stereo mode follows the rule in settings.
    const auto& settings = AppSettings::get();
    const bool singleSelection = selection.size() <= 1 && selectedBand >= 0;
    int stereo = settings.defaultStereoMode();
    if (settings.newBandStereoRule() == 0)
        stereo = settings.lastCreatedStereoMode();
    else if (settings.newBandStereoRule() == 1 && singleSelection)
        stereo = (int) bandView[(size_t) selectedBand].stereoMode;
    AppSettings::get().setLastCreatedStereoMode (stereo);

    processor.beginEdit ("Create Band");
    processor.setRawFromUI (Params::bandFreqID (freeSlot), freq);
    processor.setRawFromUI (Params::bandGainID (freeSlot), db);
    processor.setRawFromUI (Params::bandStereoID (freeSlot), (float) stereo);
    processor.setRawFromUI (Params::bandShownID (freeSlot), 1.0f);
    processor.setRawFromUI (Params::bandActiveID (freeSlot), 1.0f);
    processor.endEdit();
    selectBand (freeSlot);
}

void EQCurveComponent::mouseMove (const juce::MouseEvent& e)
{
    // Band display info: hovering a node shows its frequency, note, and gain.
    const auto hit = findHandleHitAt (e.position);
    hoverSatelliteBand = (hit.kind == DragKind::TransientSatellite || hit.kind == DragKind::TailSatellite) ? hit.band : -1;
    hoverSatelliteKind = hit.kind;
    cursorPos = e.position;
    cursorInside = true;
    if (hit.band != hoverBand || AppSettings::get().showCursorAxes() || hoverSatelliteBand >= 0)
    {
        hoverBand = hit.band;
        repaint();
    }
}

void EQCurveComponent::mouseExit (const juce::MouseEvent&)
{
    cursorInside = false;
    hoverSatelliteBand = -1;
    hoverSatelliteKind = DragKind::None;
    if (hoverBand != -1 || AppSettings::get().showCursorAxes())
    {
        hoverBand = -1;
        repaint();
    }
}

void EQCurveComponent::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    refreshBandView();
    const int hit = findHandleAt (e.position);
    if (hit < 0)
    {
        if (e.mods.isAltDown())
            processor.setDisplayTiltDbPerOctave (processor.getDisplayTiltDbPerOctave() + wheel.deltaY * 3.0f);
        return;
    }
    if (! ResponseModel::usesQ ((FilterType) bandView[(size_t) hit].type))
        return;

    // Settings can reverse the wheel direction.
    float direction = AppSettings::get().reverseWheel() ? -1.0f : 1.0f;
    const int invertFlags = AppSettings::get().wheelInvertModifier();
    if (invertFlags != 0 && (e.mods.getRawFlags() & invertFlags) == invertFlags)
        direction = -direction;
    processor.beginEdit ("Edit Q");
    const float current = bandView[(size_t) hit].q;
    const float next = juce::jlimit (0.025f, 40.0f, current * std::pow (2.0f, direction * wheel.deltaY * 0.5f));
    processor.setRawFromUI (Params::bandQID (hit), next);
    processor.endEdit();
}

bool EQCurveComponent::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0))
        return processor.undo();
    if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)
        || key == juce::KeyPress ('y', juce::ModifierKeys::commandModifier, 0))
        return processor.redo();
    if (key == juce::KeyPress ('c', juce::ModifierKeys::commandModifier, 0))
    {
        copySelected();
        return true;
    }
    if (key == juce::KeyPress ('v', juce::ModifierKeys::commandModifier, 0))
    {
        pasteClipboard();
        return true;
    }
    if (key == juce::KeyPress ('d', juce::ModifierKeys::commandModifier, 0))
    {
        duplicateSelected();
        return true;
    }
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        removeSelected();
        return true;
    }
    return false;
}

void EQCurveComponent::openValueEntry (int index)
{
    valueBand = index;
    const auto& band = bandView[(size_t) index];
    valueEditor.setText (ValueFormat::frequency (band.frequency) + " " + ValueFormat::gainDB (band.gainDb) + " dB", false);
    const auto pos = getHandlePosition (index);
    valueEditor.setBounds ((int) pos.x - 40, (int) pos.y - 28, 110, 22);
    valueEditor.setVisible (true);
    valueEditor.grabKeyboardFocus();
    valueEditor.selectAll();
}

void EQCurveComponent::commitValueEntry()
{
    if (! valueEditor.isVisible() || valueBand < 0)
        return;
    const auto text = valueEditor.getText().trim();
    valueEditor.setVisible (false);
    const int index = valueBand;
    valueBand = -1;
    if (text.isEmpty())
        return;

    juce::StringArray tokens;
    tokens.addTokens (text, " ", "");
    tokens.removeEmptyStrings();
    processor.beginEdit ("Type Value");
    for (const auto& token : tokens)
    {
        float hz = 0.0f;
        float db = 0.0f;
        if (ValueFormat::parseFrequency (token, hz))
            processor.setRawFromUI (Params::bandFreqID (index), hz);
        else if (ValueFormat::parseGainDb (token, db))
            processor.setRawFromUI (Params::bandGainID (index), db);
    }
    processor.endEdit();
    repaint();
}

void EQCurveComponent::removeSelected()
{
    refreshBandView();
    if (selection.empty() && selectedBand >= 0)
        selection = { selectedBand };
    processor.removeBands (selection);
    selection.clear();
    repaint();
}

void EQCurveComponent::copySelected()
{
    processor.copyBandToClipboard (selectedBand);
}

void EQCurveComponent::pasteClipboard()
{
    const int slot = processor.pasteBandClipboard (selectedBand, false);
    if (slot >= 0)
        selectBand (slot);
}

void EQCurveComponent::duplicateSelected()
{
    const int slot = processor.duplicateBand (selectedBand, false);
    if (slot >= 0)
        selectBand (slot);
}
