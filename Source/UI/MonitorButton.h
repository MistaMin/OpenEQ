#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"
#include <functional>

// Channel monitoring key: cycles through LR / L / R / M / S, rerouting the
// spectrum analyser to that channel. This is
// intentionally NOT a plugin parameter -- it is a UI-only listening aid.
class MonitorButton : public juce::Component
{
public:
    MonitorButton()
    {
        label.setText ("MONITOR", juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setFont (juce::FontOptions (9.0f, juce::Font::bold));
        label.setColour (juce::Label::textColourId, Theme::textMid);
        label.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (label);

        button.setButtonText ("LR");
        button.setLookAndFeel (&lnf);
        button.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2c3439));
        button.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff35c6f0));
        button.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        button.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        button.setClickingTogglesState (true);
        button.onClick = [this] { cycle(); };
        addAndMakeVisible (button);
    }

    void cycle()
    {
        setMode ((mode + 1) % 5);
    }

    void setMode (int newMode)
    {
        mode = juce::jlimit (0, 4, newMode);
        static const char* names[] = { "LR", "L", "R", "M", "S" };
        button.setButtonText (names[mode]);
        button.setToggleState (mode != 0, juce::dontSendNotification);
        if (onModeChanged != nullptr)
            onModeChanged (mode);
    }

    int getMonitorMode() const { return mode; }

    std::function<void(int)> onModeChanged;

    void resized() override
    {
        auto b = getLocalBounds();
        label.setBounds (b.removeFromTop (14));
        button.setBounds (b.reduced (2, 1));
    }

private:
    struct MonitorLNF final : public juce::LookAndFeel_V4
    {
        void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
        {
            auto r = b.getLocalBounds().toFloat().reduced (1.5f);
            const bool on = b.getToggleState();

            juce::Colour base = on ? juce::Colour (0xff35c6f0) : juce::Colour (0xff2c3439);
            if (! on && over) base = base.brighter (0.08f);
            if (down) base = base.darker (0.1f);

            g.setColour (base);
            g.fillRoundedRectangle (r, 4.0f);

            // Subtle top highlight.
            if (! on)
            {
                g.setColour (juce::Colours::white.withAlpha (0.08f));
                g.drawHorizontalLine (juce::roundToInt (r.getY()) + 1, r.getX() + 3, r.getRight() - 3);
            }

            // Outline.
            g.setColour (on ? juce::Colour (0xff5ad4f5) : juce::Colour (0xff151a1d));
            g.drawRoundedRectangle (r, 4.0f, 1.0f);
        }

        juce::Font getTextButtonFont (juce::TextButton&, int) override
        {
            return juce::FontOptions (11.0f, juce::Font::bold);
        }
    };

    int mode = 0;
    MonitorLNF lnf;
    juce::TextButton button;
    juce::Label label;
};
