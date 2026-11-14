#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Theme.h"
#include <memory>

// Global bypass "B" toggle in the top-right corner. Lights up in the accent
// colour when bypass is engaged.
class BypassButton : public juce::Button {
public:
    BypassButton(juce::AudioProcessorValueTreeState& state, const juce::String& paramID, juce::Colour accent)
        : juce::Button(paramID), accentColour(accent)
    {
        setClickingTogglesState(true);
        setTooltip("Bypass band");
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(state, paramID, *this);
    }

    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        auto b = getLocalBounds().toFloat().reduced(2);
        const bool on = getToggleState();

        juce::Colour base = on ? accentColour : juce::Colour(0xff2c3439);
        if (shouldDrawButtonAsHighlighted && ! on) base = base.brighter(0.08f);
        if (shouldDrawButtonAsDown) base = base.darker(0.1f);

        g.setColour(base);
        g.fillRoundedRectangle(b, 4.0f);

        if (! on)
        {
            g.setColour(juce::Colours::white.withAlpha(0.08f));
            g.drawHorizontalLine(juce::roundToInt(b.getY()) + 1, b.getX() + 3, b.getRight() - 3);
        }

        g.setColour(on ? accentColour.brighter(0.3f) : juce::Colour(0xff151a1d));
        g.drawRoundedRectangle(b, 4.0f, 1.0f);

        g.setColour(juce::Colours::white.withAlpha(on ? 1.0f : 0.85f));
        g.setFont(juce::FontOptions(label.length() > 1 ? 10.0f : 12.0f, juce::Font::bold));
        g.drawText(label, b, juce::Justification::centred);
    }

    /** Text on the key. The default is a single B. */
    void setLabel(const juce::String& text) { label = text; repaint(); }

    void setAccent(juce::Colour c) { accentColour = c; repaint(); }
    juce::Colour getAccent() const { return accentColour; }

private:
    juce::Colour accentColour;
    juce::String label { "B" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};
