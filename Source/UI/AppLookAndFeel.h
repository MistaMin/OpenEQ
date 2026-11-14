#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"

// Dark LookAndFeel for the standalone host chrome (menu bar, window
// background) so the whole app matches the plugin UI instead of JUCE's light
// default menubar sitting above a dark editor.
class AppLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    AppLookAndFeel()
    {
        setColour (juce::LookAndFeel_V4::ColourScheme::windowBackground,
                   Theme::editorBottom);
        setColour (juce::ResizableWindow::backgroundColourId, Theme::editorBottom);
        setColour (juce::PopupMenu::backgroundColourId, Theme::panelBottom);
        setColour (juce::PopupMenu::textColourId, Theme::textDark);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Theme::panelTop);
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        setColour (juce::TextEditor::backgroundColourId, Theme::panelBottom);
        setColour (juce::TextEditor::textColourId, Theme::textDark);
    }

    /** Dark glass key. A key that is switched on fills with its on-colour and glows. */
    void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                               bool over, bool down) override
    {
        const auto bounds = button.getLocalBounds().toFloat().reduced (1.0f);
        const float radius = juce::jmin (7.0f, bounds.getHeight() * 0.5f);
        const bool on = button.getToggleState();

        auto base = backgroundColour;
        if (over && ! on) base = base.brighter (0.10f);
        if (down) base = base.darker (0.15f);

        if (on)
        {
            for (int ring = 3; ring >= 1; --ring)
            {
                g.setColour (base.withAlpha (0.07f * (float) (4 - ring)));
                g.fillRoundedRectangle (bounds.expanded ((float) ring), radius + (float) ring);
            }
        }

        g.setGradientFill (juce::ColourGradient (base.brighter (on ? 0.18f : 0.10f), 0.0f, bounds.getY(),
                                                 base.darker (on ? 0.10f : 0.22f), 0.0f, bounds.getBottom(), false));
        g.fillRoundedRectangle (bounds, radius);
        g.setColour (on ? base.brighter (0.6f) : juce::Colours::white.withAlpha (over ? 0.22f : 0.12f));
        g.drawRoundedRectangle (bounds, radius, 1.0f);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
    {
        return juce::FontOptions (juce::jlimit (9.5f, 12.5f, (float) buttonHeight * 0.48f), juce::Font::bold);
    }
};
