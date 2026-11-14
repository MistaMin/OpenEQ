#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Theme.h"
#include <memory>

// Modern illuminated on/off toggle. When enabled it glows in the accent
// colour with white text; when disabled it sits as a dark recessed key.
// Call setActive(false) to grey it out while its owning section is bypassed.
class SmallToggle : public juce::ToggleButton {
public:
    SmallToggle(const juce::String& labelText, juce::Colour accent)
        : label(labelText), accentColour(accent)
    {
        setButtonText(labelText);
        setClickingTogglesState(true);
    }

    void setActive(bool active)
    {
        if (isActive == active)
            return;
        isActive = active;
        repaint();
    }

    void setAccent (juce::Colour c)
    {
        accentColour = c;
        repaint();
    }

    bool getActive() const { return isActive; }

    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        auto full = getLocalBounds().toFloat();
        // Keep the button compact and centred regardless of how large the
        // parent slot is.
        const float btnH = juce::jmin (full.getHeight() - 4.0f, 34.0f);
        auto b = full.withSizeKeepingCentre (full.getWidth() - 6.0f, btnH);
        const bool on = getToggleState() && isActive;

        juce::Colour base = on ? accentColour.darker (0.18f) : Theme::buttonInactiveTop;
        if (shouldDrawButtonAsHighlighted && ! on) base = base.brighter (0.08f);
        if (shouldDrawButtonAsDown) base = base.darker (0.1f);

        // Subtle shadow for recessed look when off.
        if (! on)
        {
            g.setColour (juce::Colours::black.withAlpha (0.4f));
            g.fillRoundedRectangle (b.translated (0.0f, 1.0f), 4.0f);
        }

        g.setColour (base);
        g.fillRoundedRectangle (b, 4.0f);

        // Glow halo when on.
        if (on)
        {
            for (int i = 2; i <= 6; i += 2)
            {
                g.setColour (accentColour.withAlpha (0.10f - (float) i * 0.012f));
                g.fillRoundedRectangle (b.expanded ((float) i * 0.5f), 4.0f + (float) i * 0.5f);
            }
        }

        // Top highlight / outline.
        if (on)
        {
            g.setColour (accentColour.brighter (0.25f));
            g.drawRoundedRectangle (b, 4.0f, 1.0f);
        }
        else
        {
            g.setColour (juce::Colours::white.withAlpha (0.06f));
            g.drawHorizontalLine (juce::roundToInt (b.getY()) + 1, b.getX() + 3, b.getRight() - 3);
            g.setColour (Theme::buttonOutline);
            g.drawRoundedRectangle (b, 4.0f, 1.0f);
        }

        // Text.
        juce::Colour textCol = on ? juce::Colours::white
                                  : (isActive ? Theme::textStd : Theme::buttonTextInactive);
        g.setColour (textCol);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (label, b, juce::Justification::centred);
    }

private:
    juce::String label;
    juce::Colour accentColour;
    bool isActive = true;
};
