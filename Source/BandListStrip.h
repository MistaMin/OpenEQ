#pragma once
#include <JuceHeader.h>
#include <cmath>
#include <functional>
#include <vector>
#include "PluginProcessor.h"
#include "Parameters.h"
#include "BandMenu.h"
#include "UI/ValueFormat.h"

/** band list. One card per used band, in slot order.
    Click selects, drag reorders, double-click a value edits it, double-click empty space adds a band,
    and right-click opens the shared band menu. */
class BandListStrip : public juce::Component, private juce::Timer
{
public:
    explicit BandListStrip (OpenEQAudioProcessor& processorToUse) : processor (processorToUse)
    {
        startTimerHz (10);
    }

    ~BandListStrip() override { stopTimer(); }

    std::function<void (int)> onBandSelected;
    std::function<void (int)> onBandEdit;

    void setSelectedBand (int index)
    {
        selectedBand = index;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff0d1114));
        g.setColour (juce::Colour (0xff222b30));
        g.drawHorizontalLine (0, 0.0f, (float) getWidth());

        const auto used = usedBands();
        for (int k = 0; k < (int) used.size(); ++k)
        {
            const auto bounds = cardBounds (k);
            if (bounds.getRight() < 0 || bounds.getX() > getWidth())
                continue;
            paintCard (g, used[(size_t) k], bounds);
        }

        if (used.empty())
        {
            g.setColour (juce::Colours::white.withAlpha (0.45f));
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText ("Double-click here to add a band", getLocalBounds().reduced (12, 0), juce::Justification::centredLeft);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto used = usedBands();
        const int k = cardAt (e.position);
        if (k < 0)
            return;
        const int index = used[(size_t) k];

        if (e.mods.isPopupMenu())
        {
            BandMenu::show (processor, index, this, [this] (int next) { selectAndNotify (next); });
            return;
        }

        if (powerRect (cardBounds (k)).contains (e.position.toInt()))
        {
            const bool active = processor.apvts.getRawParameterValue (Params::bandActiveID (index))->load() > 0.5f;
            processor.beginEdit ("Toggle Band");
            processor.setRawFromUI (Params::bandActiveID (index), active ? 0.0f : 1.0f);
            processor.endEdit();
            repaint();
            return;
        }

        selectAndNotify (index);
        dragSource = index;
        dragStart = e.position;
        dragging = false;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragSource < 0)
            return;
        if (! dragging && e.position.getDistanceFrom (dragStart) > 6.0f)
            dragging = true;
        if (dragging)
        {
            dropTarget = cardAt (e.position);
            repaint();
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (dragSource >= 0 && dragging)
        {
            const auto used = usedBands();
            const int target = cardAt (e.position);
            int destination = -1;
            if (target >= 0)
                destination = used[(size_t) target];
            else if (! used.empty() && e.position.x > (float) cardBounds ((int) used.size() - 1).getRight())
                destination = processor.findFreeBandSlot();

            if (destination >= 0 && destination != dragSource)
            {
                processor.swapBands (dragSource, destination);
                selectAndNotify (destination); // the dragged band now lives in the destination slot
            }
            else
            {
                selectAndNotify (dragSource);
            }
        }
        dragSource = -1;
        dragging = false;
        dropTarget = -1;
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int k = cardAt (e.position);
        if (k < 0)
        {
            const int slot = processor.findFreeBandSlot();
            if (slot < 0)
                return;
            processor.beginEdit ("Add Band");
            processor.setRawFromUI (Params::bandActiveID (slot), 1.0f);
            processor.setRawFromUI (Params::bandShownID (slot), 1.0f);
            processor.endEdit();
            selectAndNotify (slot);
            return;
        }

        const int index = usedBands()[(size_t) k];
        selectAndNotify (index);
        if (onBandEdit != nullptr && e.position.y > 20.0f)
            onBandEdit (index);
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
    {
        const int span = (int) usedBands().size() * cardStride() + 16;
        const int maxScroll = juce::jmax (0, span - getWidth());
        scrollX = juce::jlimit (0, maxScroll, scrollX - juce::roundToInt (wheel.deltaX * 240.0f + wheel.deltaY * 240.0f));
        repaint();
    }

private:
    static constexpr int cardWidth = 104;
    static constexpr int cardGap = 6;
    static constexpr int margin = 8;

    static int cardStride() noexcept { return cardWidth + cardGap; }

    std::vector<int> usedBands() const
    {
        std::vector<int> used;
        for (int i = 0; i < OpenEQAudioProcessor::numBands; ++i)
            if (processor.isBandUsed (i))
                used.push_back (i);
        return used;
    }

    juce::Rectangle<int> cardBounds (int k) const
    {
        return { margin + k * cardStride() - scrollX, 8, cardWidth, getHeight() - 16 };
    }

    int cardAt (juce::Point<float> p) const
    {
        const auto used = usedBands();
        for (int k = 0; k < (int) used.size(); ++k)
            if (cardBounds (k).toFloat().contains (p))
                return k;
        return -1;
    }

    static juce::Rectangle<int> powerRect (juce::Rectangle<int> card) noexcept
    {
        return { card.getRight() - 18, card.getY() + 3, 14, 14 };
    }

    static juce::Colour colourFor (int index)
    {
        const float hue = std::fmod (0.08f + 0.17f * (float) index, 1.0f);
        return juce::Colour::fromHSV (hue, 0.72f, 0.95f, 1.0f);
    }

    void selectAndNotify (int index)
    {
        selectedBand = index;
        if (onBandSelected != nullptr)
            onBandSelected (index);
        repaint();
    }

    void timerCallback() override { repaint(); }

    void paintCard (juce::Graphics& g, int index, juce::Rectangle<int> bounds)
    {
        const auto colour = colourFor (index);
        const auto card = bounds.toFloat();
        const bool active = processor.apvts.getRawParameterValue (Params::bandActiveID (index))->load() > 0.5f;
        const int type = juce::jlimit (0, filterTypeCount - 1, (int) processor.apvts.getRawParameterValue (Params::bandTypeID (index))->load());
        const int stereo = juce::jlimit (0, 5, (int) processor.apvts.getRawParameterValue (Params::bandStereoID (index))->load());
        const float freq = processor.apvts.getRawParameterValue (Params::bandFreqID (index))->load();
        const float gain = processor.apvts.getRawParameterValue (Params::bandGainID (index))->load();
        const float q = processor.apvts.getRawParameterValue (Params::bandQID (index))->load();
        const float slopeNorm = processor.apvts.getRawParameterValue (Params::bandSlopeNormID (index))->load();
        const auto shape = (FilterType) type;

        g.setColour (juce::Colour (0xff1a2226).withAlpha (active ? 1.0f : 0.55f));
        g.fillRoundedRectangle (card, 5.0f);

        // Header in the band colour.
        g.setColour (colour.withAlpha (active ? 0.85f : 0.35f));
        g.fillRoundedRectangle (card.withHeight (18.0f), 5.0f);
        g.fillRect (card.withY (card.getY() + 12.0f).withHeight (6.0f));

        const bool isSelected = index == selectedBand;
        const bool isDropTarget = dragging && cardIndexOf (index) == dropTarget;
        g.setColour (isSelected ? juce::Colours::white : (isDropTarget ? colour : juce::Colours::black.withAlpha (0.4f)));
        g.drawRoundedRectangle (card, 5.0f, isSelected || isDropTarget ? 1.6f : 1.0f);

        g.setColour (juce::Colours::black);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (juce::String (index + 1) + "  " + Params::filterTypeNames[type],
                    bounds.reduced (6, 0).withTrimmedRight (20).withHeight (18), juce::Justification::centredLeft);

        // Power dot.
        const auto power = powerRect (bounds).toFloat();
        g.setColour (active ? juce::Colour (0xffffd060) : juce::Colours::white.withAlpha (0.25f));
        g.fillEllipse (power.reduced (3.0f));

        // Stereo badge, then the value rows.
        const char* badges[] = { "", "M", "S", "L", "R", "MS" };
        g.setColour (colour);
        g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
        g.drawText (badges[stereo], bounds.getRight() - 30, bounds.getBottom() - 16, 26, 12, juce::Justification::centredRight);

        const bool gainMatters = ResponseModel::usesGain (shape);
        const bool qMatters = ResponseModel::usesQ (shape);
        const bool slopeMatters = ResponseModel::usesSlope (shape);

        const juce::String rows[4][2] = {
            { "F", ValueFormat::frequency (freq) + " Hz" },
            { "G", gainMatters ? ValueFormat::gainDB (gain) + " dB" : "--" },
            { "Q", qMatters ? ValueFormat::qFactor (q) : "--" },
            { "S", ! slopeMatters ? juce::String ("--")
                     : ResponseModel::slopeIsAttenuation (shape) ? "Att " + ValueFormat::percent (slopeNorm * 100.0f)
                                                                 : ValueFormat::slope (slopeNorm) }
        };
        const bool enabled[4] = { true, gainMatters, qMatters, slopeMatters };

        for (int r = 0; r < 4; ++r)
        {
            const int y = bounds.getY() + 24 + r * 16;
            g.setColour (juce::Colours::white.withAlpha (enabled[r] ? 0.55f : 0.22f));
            g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
            g.drawText (rows[r][0], bounds.getX() + 6, y, 10, 14, juce::Justification::centredLeft);
            g.setColour (juce::Colours::white.withAlpha (enabled[r] ? 0.92f : 0.3f));
            g.setFont (juce::FontOptions (10.0f));
            g.drawText (rows[r][1], bounds.getX() + 18, y, bounds.getWidth() - 22, 14, juce::Justification::centredLeft);
        }
    }

    int cardIndexOf (int index) const
    {
        const auto used = usedBands();
        for (int k = 0; k < (int) used.size(); ++k)
            if (used[(size_t) k] == index)
                return k;
        return -1;
    }

    OpenEQAudioProcessor& processor;
    int selectedBand = 0;
    int scrollX = 0;
    int dragSource = -1;
    int dropTarget = -1;
    bool dragging = false;
    juce::Point<float> dragStart;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandListStrip)
};
