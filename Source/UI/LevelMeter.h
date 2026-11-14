#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Theme.h"

/** Stereo peak and RMS meter shared by the input and output columns, so both read the same way: two bars
    with a peak-hold line each, a dB scale beside them, and a numeric readout on top showing the highest
    peak since it was last cleared. The owner feeds it levels from its timer and paints it into a rectangle. */
class LevelMeter
{
public:
    static constexpr int width = 48;
    static constexpr int readoutHeight = 16;

    /** Call at the owner's timer rate with linear levels. */
    void update (float peakL, float peakR, float rmsL, float rmsR, float decay = 0.88f) noexcept
    {
        shownPeak[0] = juce::jmax (peakL, shownPeak[0] * decay);
        shownPeak[1] = juce::jmax (peakR, shownPeak[1] * decay);
        shownRms[0]  = juce::jmax (rmsL, shownRms[0] * decay);
        shownRms[1]  = juce::jmax (rmsR, shownRms[1] * decay);

        // The hold line stays at the highest recent peak for about 1.5 s, then falls.
        const float peaks[2] = { peakL, peakR };
        for (int ch = 0; ch < 2; ++ch)
        {
            if (peaks[ch] >= held[ch]) { held[ch] = peaks[ch]; holdAge[ch] = 0; }
            else if (++holdAge[ch] > 45) held[ch] *= 0.94f;
        }
    }

    /** The clickable readout for a meter painted into `bounds`. */
    static juce::Rectangle<int> readoutArea (juce::Rectangle<int> bounds) noexcept
    {
        return bounds.removeFromTop (readoutHeight);
    }

    /** @param maxPeak  highest linear peak since the last clear, shown as the number
        @param over     true draws the readout red; it reads "CLIP" when the number alone would not show why */
    void paint (juce::Graphics& g, juce::Rectangle<int> bounds, float maxPeak, bool over) const
    {
        if (bounds.isEmpty())
            return;

        auto mb = bounds.toFloat();

        auto readout = mb.removeFromTop ((float) readoutHeight);
        g.setColour (over ? juce::Colour (0xffd92626) : Theme::readoutFill);
        g.fillRoundedRectangle (readout, 3.0f);
        g.setColour (over ? juce::Colours::white.withAlpha (0.7f) : Theme::readoutEdge);
        g.drawRoundedRectangle (readout, 3.0f, 1.0f);
        g.setColour (over ? juce::Colours::white : Theme::readoutText);
        g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        const float maxDb = juce::Decibels::gainToDecibels (maxPeak, -100.0f);
        juce::String text = maxDb <= -99.0f ? juce::String ("-inf") : juce::String (maxDb, 1);
        if (over && maxDb < -0.05f)
            text = "CLIP";
        else if (maxDb > 0.0f)
            text = "+" + text;
        g.drawText (text, readout, juce::Justification::centred);

        mb.removeFromTop (4.0f);

        const float barW = 11.0f;
        const juce::Rectangle<float> bars[2] = { { mb.getX() + 1.0f, mb.getY(), barW, mb.getHeight() },
                                                 { mb.getX() + 14.0f, mb.getY(), barW, mb.getHeight() } };

        // +6 dB at the top, -60 dB at the bottom.
        auto dbToY = [&mb] (float db)
        {
            const float norm = (juce::jlimit (-60.0f, 6.0f, db) + 60.0f) / 66.0f;
            return mb.getBottom() - norm * mb.getHeight();
        };

        juce::ColourGradient grad (juce::Colour (0xff22c55e), 0.0f, mb.getBottom(),
                                   juce::Colour (0xffef4444), 0.0f, mb.getY(), false);
        grad.addColour (0.70, juce::Colour (0xffeab308));
        grad.addColour (0.85, juce::Colour (0xfff97316));

        for (int ch = 0; ch < 2; ++ch)
        {
            const auto bar = bars[ch];
            g.setColour (juce::Colour (0xff0a0e10));
            g.fillRoundedRectangle (bar, 2.0f);

            const float yPk = dbToY (juce::Decibels::gainToDecibels (shownPeak[ch], -60.0f));
            const float yRms = dbToY (juce::Decibels::gainToDecibels (shownRms[ch], -60.0f));

            if (yPk < mb.getBottom())
            {
                g.setGradientFill (grad);
                g.fillRect (juce::Rectangle<float> (bar.getX(), yPk, bar.getWidth(), mb.getBottom() - yPk));
            }
            if (yRms < mb.getBottom())
            {
                g.setColour (juce::Colours::white.withAlpha (0.45f));
                g.fillRect (juce::Rectangle<float> (bar.getX() + 2.0f, yRms, bar.getWidth() - 4.0f, mb.getBottom() - yRms));
            }

            const float heldDb = juce::Decibels::gainToDecibels (held[ch], -60.0f);
            if (heldDb > -59.5f)
            {
                g.setColour (heldDb >= 0.0f ? juce::Colour (0xffff5a5a) : juce::Colours::white.withAlpha (0.9f));
                g.fillRect (bar.getX(), dbToY (heldDb) - 1.0f, bar.getWidth(), 2.0f);
            }

            g.setColour (juce::Colour (0x22ffffff));
            g.drawRoundedRectangle (bar, 2.0f, 0.75f);
        }

        const float tickX = mb.getX() + 27.0f;
        static const float ticks[] = { 6.0f, 0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -36.0f, -48.0f };
        g.setFont (juce::FontOptions (8.0f));
        for (float t : ticks)
        {
            const float y = dbToY (t);
            g.setColour (t >= 0.0f ? juce::Colour (0xffff6666) : juce::Colour (0x66ffffff));
            g.drawHorizontalLine (juce::roundToInt (y), tickX - 2.0f, tickX + 2.0f);
            g.drawText (juce::String ((int) t), juce::Rectangle<float> (tickX + 3.0f, y - 5.0f, 16.0f, 10.0f),
                        juce::Justification::centredLeft);
        }
    }

private:
    float shownPeak[2] = { 0.0f, 0.0f };
    float shownRms[2] = { 0.0f, 0.0f };
    float held[2] = { 0.0f, 0.0f };
    int holdAge[2] = { 0, 0 };
};
