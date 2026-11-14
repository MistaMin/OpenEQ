#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "RotaryKnob.h"
#include "SmallToggle.h"
#include "LevelMeter.h"
#include "Theme.h"
#include "../PluginProcessor.h"

/** Output section, along the right edge. A stereo peak and RMS meter with a peak-hold line and a numeric
    peak readout sits beside the controls, which are grouped by what they do: LEVEL (output, auto gain, gain
    scale), STEREO (width, pan, side level) and COLOUR (drive). */
class OutputColumn : public juce::Component, private juce::Timer
{
public:
    explicit OutputColumn (OpenEQAudioProcessor& proc)
        : processor (proc),
          outputGainKnob ("OUTPUT", KnobValueType::Gain, juce::Colour (0xff35c6f0)),
          scaleKnob ("SCALE", KnobValueType::Level, juce::Colour (0xff38bdf8)),
          stereoWidthKnob ("WIDTH", KnobValueType::Level, juce::Colour (0xffa78bfa)),
          panKnob ("PAN", KnobValueType::Pan, juce::Colour (0xff34d399)),
          driveKnob ("DRIVE", KnobValueType::Level, juce::Colour (0xfff59e0b)),
          sideKnob ("SIDE", KnobValueType::SideDb, juce::Colour (0xffec4899)),
          autoGainToggle ("AUTO", Theme::outputCol)
    {
        outputGainKnob.setTooltip ("Master output gain (-30 dB to +30 dB)");
        scaleKnob.setTooltip ("Gain scale: scales all EQ band gains proportionally (0% to 200%)");
        stereoWidthKnob.setTooltip ("Stereo width: 0% mono, 100% normal stereo, 200% wide");
        panKnob.setTooltip ("Master output pan (Left / Right)");
        driveKnob.setTooltip ("Analogue tape / tube saturation drive");
        sideKnob.setTooltip ("Side channel gain adjustment (-30 dB to +30 dB)");

        auto& s = proc.apvts;
        outputAttachment = std::make_unique<SliderAttachment> (s, "outputGain", outputGainKnob);
        outputGainKnob.bindDefault (s, "outputGain");
        scaleAttachment = std::make_unique<SliderAttachment> (s, "gainScale", scaleKnob);
        scaleKnob.bindDefault (s, "gainScale");
        widthAttachment = std::make_unique<SliderAttachment> (s, "stereoWidth", stereoWidthKnob);
        stereoWidthKnob.bindDefault (s, "stereoWidth");
        panAttachment = std::make_unique<SliderAttachment> (s, "outputPan", panKnob);
        panKnob.bindDefault (s, "outputPan");
        driveAttachment = std::make_unique<SliderAttachment> (s, "drive", driveKnob);
        driveKnob.bindDefault (s, "drive");
        sideAttachment = std::make_unique<SliderAttachment> (s, "sideGain", sideKnob);
        sideKnob.bindDefault (s, "sideGain");
        autoGainAttachment = std::make_unique<ButtonAttachment> (s, "autoGain", autoGainToggle);

        autoGainToggle.setTooltip ("Auto gain: compensates the output so broad EQ boosts or cuts keep a similar level");
        for (auto* k : { &outputGainKnob, &scaleKnob, &stereoWidthKnob, &panKnob, &driveKnob, &sideKnob })
            addAndMakeVisible (*k);
        addAndMakeVisible (autoGainToggle);

        startTimerHz (30);
    }

    ~OutputColumn() override
    {
        stopTimer();
    }

    void timerCallback() override
    {
        meter.update (processor.getMeterPeak (0), processor.getMeterPeak (1),
                      processor.getMeterRms (0), processor.getMeterRms (1));
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        // The peak readout and the column header both acknowledge an overload and restart the held peak.
        if (clipArea.contains (e.getPosition()) || e.y < 34)
        {
            processor.clearMeterClip();
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (2.0f);

        // Column background plate
        g.setColour (juce::Colours::black.withAlpha (0.40f));
        g.fillRoundedRectangle (bounds.translated (0.0f, 1.0f), 6.0f);

        g.setGradientFill (juce::ColourGradient (Theme::panelTop, 0.0f, bounds.getY(),
                                                 Theme::panelBottom, 0.0f, bounds.getBottom(), false));
        g.fillRoundedRectangle (bounds, 6.0f);

        g.setColour (Theme::panelEdge);
        g.drawRoundedRectangle (bounds, 6.0f, 1.0f);

        // Header
        g.setColour (Theme::textBright);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("OUTPUT", bounds.removeFromTop (24), juce::Justification::centred);

        // Hairline under header
        g.setColour (Theme::panelEdge.withAlpha (0.6f));
        g.drawHorizontalLine (25, bounds.getX() + 6.0f, bounds.getRight() - 6.0f);

        // Group captions over each set of controls
        g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
        static const char* const captions[] = { "LEVEL", "STEREO", "COLOUR" };
        for (int i = 0; i < 3; ++i)
        {
            const auto area = captionAreas[i].toFloat();
            if (area.isEmpty())
                continue;
            g.setColour (Theme::textDim);
            g.drawText (captions[i], area, juce::Justification::centredLeft);
            const float textW = g.getCurrentFont().getStringWidthFloat (captions[i]) + 6.0f;
            g.setColour (Theme::panelEdge.withAlpha (0.7f));
            g.drawHorizontalLine (juce::roundToInt (area.getCentreY()), area.getX() + textW, area.getRight());
        }

        meter.paint (g, meterBounds, processor.getMeterMaxPeak(), processor.getMeterClipped());
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (6, 6);
        b.removeFromTop (24); // Header

        // The meter takes the left side, with its peak readout on top
        meterBounds = b.removeFromLeft (LevelMeter::width).reduced (2, 4);
        clipArea = LevelMeter::readoutArea (meterBounds);

        // Controls on the right, in three captioned groups
        b.removeFromLeft (4);
        constexpr int captionH = 13, autoH = 22;
        const int knobH = juce::jmax (40, (b.getHeight() - 3 * captionH - autoH - 8) / 6);
        auto place = [&] (RotaryKnob& knob) { knob.setBounds (b.removeFromTop (knobH).reduced (0, 1)); };

        captionAreas[0] = b.removeFromTop (captionH);
        place (outputGainKnob);
        autoGainToggle.setBounds (b.removeFromTop (autoH));
        b.removeFromTop (2);
        place (scaleKnob);
        b.removeFromTop (3);
        captionAreas[1] = b.removeFromTop (captionH);
        place (stereoWidthKnob);
        place (panKnob);
        place (sideKnob);
        b.removeFromTop (3);
        captionAreas[2] = b.removeFromTop (captionH);
        place (driveKnob);
    }

private:
    OpenEQAudioProcessor& processor;
    juce::Rectangle<int> meterBounds;
    juce::Rectangle<int> clipArea;
    juce::Rectangle<int> captionAreas[3];

    RotaryKnob outputGainKnob, scaleKnob, stereoWidthKnob, panKnob, driveKnob, sideKnob;
    SmallToggle autoGainToggle;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SliderAttachment> outputAttachment, scaleAttachment, widthAttachment, panAttachment, driveAttachment, sideAttachment;
    std::unique_ptr<ButtonAttachment> autoGainAttachment;

    LevelMeter meter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OutputColumn)
};
