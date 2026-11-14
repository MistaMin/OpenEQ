#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "RotaryKnob.h"
#include "Theme.h"
#include "LevelMeter.h"
#include "../PluginProcessor.h"

/** Input section: the same meter as the output column, and the input trim.
    The meter reads the host signal before the trim so an overload is visible even when trimming down.
    The readout on top holds the highest input peak; click it or the header to restart it. */
class InputColumn : public juce::Component, private juce::Timer
{
public:
    explicit InputColumn (OpenEQAudioProcessor& proc)
        : processor (proc),
          trimKnob ("TRIM", KnobValueType::Gain, Theme::outputCol)
    {
        trimKnob.setTooltip ("Input trim, applied before the EQ and the analyzer (-24 dB to +24 dB)");
        auto& state = proc.apvts;
        trimAttachment = std::make_unique<SliderAttachment> (state, "inputGain", trimKnob);
        trimKnob.bindDefault (state, "inputGain");
        addAndMakeVisible (trimKnob);
        startTimerHz (30);
    }

    ~InputColumn() override { stopTimer(); }

    void timerCallback() override
    {
        meter.update (processor.getInputMeterPeak (0), processor.getInputMeterPeak (1),
                      processor.getInputMeterRms (0), processor.getInputMeterRms (1));
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (LevelMeter::readoutArea (meterBounds).contains (e.getPosition()) || e.y < 34)
        {
            processor.clearInputMeterMax();
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (2.0f);

        g.setColour (juce::Colours::black.withAlpha (0.40f));
        g.fillRoundedRectangle (bounds.translated (0.0f, 1.0f), 6.0f);
        g.setGradientFill (juce::ColourGradient (Theme::panelTop, 0.0f, bounds.getY(),
                                                 Theme::panelBottom, 0.0f, bounds.getBottom(), false));
        g.fillRoundedRectangle (bounds, 6.0f);
        g.setColour (Theme::panelEdge);
        g.drawRoundedRectangle (bounds, 6.0f, 1.0f);

        auto header = bounds.removeFromTop (24.0f);
        g.setColour (Theme::textBright);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("INPUT", header, juce::Justification::centred);

        g.setColour (Theme::panelEdge.withAlpha (0.6f));
        g.drawHorizontalLine (25, bounds.getX() + 6.0f, bounds.getRight() - 6.0f);

        const float maxPeak = processor.getInputMeterMaxPeak();
        meter.paint (g, meterBounds, maxPeak, maxPeak >= 0.999f);
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (6, 6);
        b.removeFromTop (24);
        meterBounds = b.removeFromLeft (LevelMeter::width).reduced (2, 4);
        b.removeFromLeft (4);
        // Trim sits at the top of the strip; the meter runs the full height beside it.
        trimKnob.setBounds (b.removeFromTop (juce::jmin (b.getHeight(), 132)));
    }

private:
    OpenEQAudioProcessor& processor;
    RotaryKnob trimKnob;
    juce::Rectangle<int> meterBounds;
    LevelMeter meter;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SliderAttachment> trimAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InputColumn)
};
