#pragma once
#include <JuceHeader.h>
#include "Theme.h"
#include "ValueFormat.h"
#include "../AppSettings.h"

enum class SliderValueType { Frequency, Gain, Q, Level, TimeMs, SideDb, Pan, Slope, Attenuation, Phase, Ratio, Percent, WholeDb, Octaves };

class CompactSlider : public juce::Slider
{
public:
    CompactSlider (const juce::String& name, SliderValueType type = SliderValueType::Gain,
                   juce::Colour accent = Theme::stereoCol,
                   juce::Slider::SliderStyle style = juce::Slider::LinearVertical)
        : valueType (type), accentColour (accent)
    {
        setLookAndFeel (&sliderLnf());
        setSliderStyle (style);
        setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        title.setText (name, juce::dontSendNotification);
        title.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        title.setJustificationType (style == juce::Slider::LinearHorizontal
                                        ? juce::Justification::centredLeft
                                        : juce::Justification::centred);
        title.setColour (juce::Label::textColourId, Theme::labelColour);
        title.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (title);
    }

    ~CompactSlider() override
    {
        setLookAndFeel (nullptr);
    }

    void setAccentColour (juce::Colour c) { accentColour = c; repaint(); }
    void setAccent (juce::Colour c) { setAccentColour (c); }
    void setTitle (const juce::String& text) { title.setText (text, juce::dontSendNotification); }

    /** Re-reads the theme colours a widget copied when it was built. */
    void refreshTheme()
    {
        title.setColour (juce::Label::textColourId, Theme::labelColour);
        repaint();
    }
    void setValueType (SliderValueType type, const juce::String& name = {})
    {
        valueType = type;
        if (name.isNotEmpty())
            setTitle (name);
        repaint();
    }

    void bindDefault (juce::AudioProcessorValueTreeState& state, const juce::String& id)
    {
        parameter = state.getParameter (id);
    }

    void resetToDefault()
    {
        if (parameter != nullptr)
            setValue (parameter->convertFrom0to1 (parameter->getDefaultValue()), juce::sendNotificationSync);
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        resetToDefault();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        // Ignore clicks on title label or value readout badge to prevent jumping
        if (! trackArea.contains (e.getPosition()))
            return;

        juce::Slider::mouseDown (e);
    }

    juce::Rectangle<int> getTrackBounds() const noexcept { return trackArea; }

    juce::Rectangle<int> getTrackSlotBounds() const noexcept
    {
        if (getSliderStyle() == juce::Slider::LinearHorizontal)
        {
            auto bounds = trackArea.toFloat();
            if (bounds.getWidth() < 10.0f || bounds.getHeight() < 4.0f)
                return trackArea;
            const float trackH = juce::jlimit (5.0f, 10.0f, bounds.getHeight() * 0.22f);
            const float trackY = bounds.getCentreY() - trackH * 0.5f;
            return juce::Rectangle<float> (bounds.getX() + 2.0f, trackY, bounds.getWidth() - 4.0f, trackH).toNearestInt();
        }
        else
        {
            auto bounds = trackArea.toFloat();
            if (bounds.getWidth() < 4.0f || bounds.getHeight() < 10.0f)
                return trackArea;
            const float trackW = juce::jlimit (6.0f, 12.0f, bounds.getWidth() * 0.12f);
            const float trackX = bounds.getCentreX() - trackW * 0.5f;
            return juce::Rectangle<float> (trackX, bounds.getY() + 3.0f, trackW, bounds.getHeight() - 6.0f).toNearestInt();
        }
    }

    struct CompactSliderLnf : juce::LookAndFeel_V4
    {
        juce::Slider::SliderLayout getSliderLayout (juce::Slider& s) override
        {
            juce::Slider::SliderLayout layout;
            if (auto* cs = dynamic_cast<const CompactSlider*> (&s))
            {
                layout.sliderBounds = cs->getTrackSlotBounds();
            }
            else
            {
                layout.sliderBounds = s.getLocalBounds();
            }
            return layout;
        }

        int getSliderThumbRadius (juce::Slider&) override
        {
            return 6;
        }
    };

    static CompactSliderLnf& sliderLnf()
    {
        static CompactSliderLnf instance;
        return instance;
    }

    void resized() override
    {
        auto b = getLocalBounds();
        if (getSliderStyle() == juce::Slider::LinearHorizontal)
        {
            const int titleW = juce::jlimit (42, 60, juce::roundToInt (b.getWidth() * 0.16f));
            const int valW   = juce::jlimit (50, 72, juce::roundToInt (b.getWidth() * 0.20f));
            title.setBounds (b.removeFromLeft (titleW));
            title.setJustificationType (juce::Justification::centredLeft);
            title.setFont (juce::FontOptions (juce::jlimit (9.5f, 11.0f, b.getHeight() * 0.12f + 7.0f), juce::Font::bold));
            valueArea = b.removeFromRight (valW);
            trackArea = b.reduced (4, 2);
        }
        else
        {
            title.setBounds (b.removeFromTop (16));
            title.setJustificationType (juce::Justification::centred);
            valueArea = b.removeFromBottom (18);
            trackArea = b.reduced (2, 2);
        }

        // Slider::resized() queries getSliderLayout() and caches the sliderRegion track.
        // Skipping it leaves sliderRegion at 0px/1px, causing any click to jump to maximum!
        juce::Slider::resized();
    }

    void paint (juce::Graphics& g) override
    {
        if (getSliderStyle() == juce::Slider::LinearHorizontal)
            paintHorizontal (g);
        else
            paintVertical (g);
    }

private:
    void paintVertical (juce::Graphics& g)
    {
        const auto slotRect = getTrackSlotBounds().toFloat();
        if (slotRect.getWidth() < 4.0f || slotRect.getHeight() < 10.0f)
            return;

        auto bounds = trackArea.toFloat();
        const float trackW = slotRect.getWidth();
        const float trackX = slotRect.getX();

        // Scale ticks alongside track for wider sliders (width >= 50)
        if (bounds.getWidth() >= 50.0f)
        {
            const bool centerZero = (getMinimum() < 0.0 && getMaximum() > 0.0);
            constexpr int numTicks = 5;
            for (int i = 0; i < numTicks; ++i)
            {
                const float frac = static_cast<float> (i) / static_cast<float> (numTicks - 1);
                const float ty = slotRect.getBottom() - frac * slotRect.getHeight();
                const bool isCenter = centerZero && (i == 2);
                const float tickLen = isCenter ? 7.0f : 4.0f;
                g.setColour (isCenter ? accentColour.withAlpha (0.65f) : Theme::textDim.withAlpha (0.35f));
                g.drawHorizontalLine (juce::roundToInt (ty), slotRect.getX() - tickLen - 2.0f, slotRect.getX() - 2.0f);
                g.drawHorizontalLine (juce::roundToInt (ty), slotRect.getRight() + 2.0f, slotRect.getRight() + tickLen + 2.0f);
            }
        }

        // Recessed slot background
        g.setColour (Theme::readoutFill);
        g.fillRoundedRectangle (slotRect, trackW * 0.5f);
        g.setColour (Theme::readoutEdge);
        g.drawRoundedRectangle (slotRect, trackW * 0.5f, 1.0f);

        // Normalized position: 0 (bottom) .. 1 (top)
        const float norm = (float) valueToProportionOfLength (getValue());
        const float thumbY = slotRect.getBottom() - norm * slotRect.getHeight();

        // Active fill from bottom or center
        const bool centerZero = (getMinimum() < 0.0 && getMaximum() > 0.0);
        const float zeroNorm = centerZero ? (float) valueToProportionOfLength (0.0) : 0.0f;
        const float zeroY = slotRect.getBottom() - zeroNorm * slotRect.getHeight();

        const float fillTop = juce::jmin (thumbY, zeroY);
        const float fillBottom = juce::jmax (thumbY, zeroY);
        const float fillH = juce::jmax (1.0f, fillBottom - fillTop);

        g.setColour (accentColour.withAlpha (isEnabled() ? 0.70f : 0.25f));
        g.fillRoundedRectangle (juce::Rectangle<float> (trackX + 1.0f, fillTop, trackW - 2.0f, fillH), (trackW - 2.0f) * 0.5f);

        // Tactile Fader Cap (Thumb)
        const float thumbW = juce::jlimit (20.0f, 44.0f, bounds.getWidth() * 0.40f);
        const float thumbH = juce::jlimit (9.0f, 14.0f, bounds.getHeight() * 0.16f);
        auto thumbRect = juce::Rectangle<float> (bounds.getCentreX() - thumbW * 0.5f, thumbY - thumbH * 0.5f, thumbW, thumbH);

        // Drop shadow for thumb
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (thumbRect.translated (0, 2.0f), 2.5f);

        // Thumb body gradient (machined gunmetal fader)
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff58646b), thumbRect.getCentreX(), thumbRect.getY(),
                                                 juce::Colour (0xff21272b), thumbRect.getCentreX(), thumbRect.getBottom(), false));
        g.fillRoundedRectangle (thumbRect, 2.5f);
        g.setColour (juce::Colour (0x45ffffff));
        g.drawRoundedRectangle (thumbRect, 2.5f, 1.0f);

        // Etched grip lines across thumb cap
        if (thumbH >= 11.0f)
        {
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.drawHorizontalLine (juce::roundToInt (thumbRect.getCentreY() - 3.0f), thumbRect.getX() + 4.0f, thumbRect.getRight() - 4.0f);
            g.drawHorizontalLine (juce::roundToInt (thumbRect.getCentreY() + 3.0f), thumbRect.getX() + 4.0f, thumbRect.getRight() - 4.0f);
        }

        // Center indicator notch / neon pip on thumb
        const float pipW = juce::jlimit (6.0f, 14.0f, thumbW * 0.35f);
        g.setColour (isEnabled() ? accentColour : Theme::textDim);
        g.fillRoundedRectangle (juce::Rectangle<float> (thumbRect.getCentreX() - pipW * 0.5f, thumbRect.getCentreY() - 1.0f, pipW, 2.0f), 1.0f);

        // Readout plate at bottom
        auto valRect = valueArea.toFloat();
        const float badgeW = juce::jmin (valRect.getWidth() - 4.0f, 80.0f);
        auto badgeRect = valRect.withSizeKeepingCentre (badgeW, valRect.getHeight() - 2.0f);

        g.setColour (Theme::readoutFill);
        g.fillRoundedRectangle (badgeRect, 2.5f);
        g.setColour (Theme::readoutEdge);
        g.drawRoundedRectangle (badgeRect, 2.5f, 0.8f);

        const float fontSize = juce::jlimit (9.0f, 10.5f, bounds.getWidth() * 0.10f);
        g.setFont (juce::FontOptions (fontSize, juce::Font::bold));
        g.setColour (isEnabled() ? Theme::readoutText : Theme::textDim);
        g.drawText (formatValue (getValue()), badgeRect, juce::Justification::centred, false);
    }

    void paintHorizontal (juce::Graphics& g)
    {
        const auto slotRect = getTrackSlotBounds().toFloat();
        if (slotRect.getWidth() < 10.0f || slotRect.getHeight() < 4.0f)
            return;

        auto bounds = trackArea.toFloat();
        const float trackH = slotRect.getHeight();
        const float trackY = slotRect.getY();

        // Recessed slot background
        g.setColour (Theme::readoutFill);
        g.fillRoundedRectangle (slotRect, trackH * 0.5f);
        g.setColour (Theme::readoutEdge);
        g.drawRoundedRectangle (slotRect, trackH * 0.5f, 1.0f);

        // Scale ticks above and below track for taller sliders (height >= 32px)
        const bool centerZero = (getMinimum() < 0.0 && getMaximum() > 0.0);
        if (bounds.getHeight() >= 32.0f)
        {
            constexpr int numTicks = 5;
            for (int i = 0; i < numTicks; ++i)
            {
                const float frac = static_cast<float> (i) / static_cast<float> (numTicks - 1);
                const float tx = slotRect.getX() + frac * slotRect.getWidth();
                const bool isCenter = centerZero && (i == 2);
                const float tickLen = isCenter ? 6.0f : 3.5f;
                g.setColour (isCenter ? accentColour.withAlpha (0.65f) : Theme::textDim.withAlpha (0.35f));
                g.drawVerticalLine (juce::roundToInt (tx), slotRect.getY() - tickLen - 1.0f, slotRect.getY() - 1.0f);
                g.drawVerticalLine (juce::roundToInt (tx), slotRect.getBottom() + 1.0f, slotRect.getBottom() + tickLen + 1.0f);
            }
        }

        // Normalized position: 0 (left) .. 1 (right)
        const float norm = (float) valueToProportionOfLength (getValue());
        const float thumbX = slotRect.getX() + norm * slotRect.getWidth();

        // Active fill from center or left
        const float zeroNorm = centerZero ? (float) valueToProportionOfLength (0.0) : 0.0f;
        const float zeroX = slotRect.getX() + zeroNorm * slotRect.getWidth();

        const float fillLeft  = juce::jmin (thumbX, zeroX);
        const float fillRight = juce::jmax (thumbX, zeroX);
        const float fillW = juce::jmax (1.0f, fillRight - fillLeft);

        g.setColour (accentColour.withAlpha (isEnabled() ? 0.70f : 0.25f));
        g.fillRoundedRectangle (juce::Rectangle<float> (fillLeft, trackY + 1.0f, fillW, trackH - 2.0f), (trackH - 2.0f) * 0.5f);

        // Center detent tick
        if (centerZero && bounds.getHeight() < 32.0f)
        {
            g.setColour (juce::Colour (0x50ffffff));
            g.drawVerticalLine (juce::roundToInt (zeroX), slotRect.getY() - 3.0f, slotRect.getBottom() + 3.0f);
        }

        // Thumb cap (vertical slider pip)
        const float thumbW = juce::jlimit (10.0f, 16.0f, bounds.getWidth() * 0.05f);
        const float thumbH = juce::jlimit (18.0f, 32.0f, bounds.getHeight() * 0.68f);
        auto thumbRect = juce::Rectangle<float> (thumbX - thumbW * 0.5f, bounds.getCentreY() - thumbH * 0.5f, thumbW, thumbH);

        g.setColour (juce::Colours::black.withAlpha (0.50f));
        g.fillRoundedRectangle (thumbRect.translated (0, 1.5f), 2.0f);

        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff58646b), thumbRect.getX(), thumbRect.getY(),
                                                 juce::Colour (0xff21272b), thumbRect.getRight(), thumbRect.getBottom(), false));
        g.fillRoundedRectangle (thumbRect, 2.0f);
        g.setColour (juce::Colour (0x45ffffff));
        g.drawRoundedRectangle (thumbRect, 2.0f, 1.0f);

        // Vertical grip line on tall thumbs
        if (thumbH >= 24.0f)
        {
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.drawVerticalLine (juce::roundToInt (thumbRect.getCentreX() - 3.0f), thumbRect.getY() + 3.0f, thumbRect.getBottom() - 3.0f);
            g.drawVerticalLine (juce::roundToInt (thumbRect.getCentreX() + 3.0f), thumbRect.getY() + 3.0f, thumbRect.getBottom() - 3.0f);
        }

        // Center indicator neon line on thumb
        const float pipH = juce::jlimit (10.0f, thumbH - 4.0f, 20.0f);
        g.setColour (isEnabled() ? accentColour : Theme::textDim);
        g.fillRect (juce::Rectangle<float> (thumbRect.getCentreX() - 0.75f, thumbRect.getCentreY() - pipH * 0.5f, 1.5f, pipH));

        // Readout plate at right
        auto valRect = valueArea.toFloat().reduced (1.0f, 1.0f);
        const float badgeH = juce::jmin (22.0f, valRect.getHeight() - 4.0f);
        auto badgeRect = valRect.withSizeKeepingCentre (valRect.getWidth() - 2.0f, badgeH);
        g.setColour (Theme::readoutFill);
        g.fillRoundedRectangle (badgeRect, 2.5f);
        g.setColour (Theme::readoutEdge);
        g.drawRoundedRectangle (badgeRect, 2.5f, 0.8f);

        const float fontSize = juce::jlimit (9.0f, 10.5f, bounds.getHeight() * 0.12f + 7.0f);
        g.setFont (juce::FontOptions (fontSize, juce::Font::bold));
        g.setColour (isEnabled() ? Theme::readoutText : Theme::textDim);
        g.drawText (formatValue (getValue()), badgeRect, juce::Justification::centred, false);
    }

private:
    juce::String formatValue (double val) const
    {
        switch (valueType)
        {
            case SliderValueType::Gain:
                return ValueFormat::gainDB ((float) val);
            case SliderValueType::Level:
                return juce::String (juce::roundToInt (val)) + "%";
            case SliderValueType::Percent:
                return juce::String (juce::roundToInt (val * 100.0)) + "%";
            case SliderValueType::WholeDb:
                return juce::String (juce::roundToInt (val)) + " dB";
            case SliderValueType::Octaves:
                return juce::String (val, 1) + " oct";
            case SliderValueType::Ratio:
                return ValueFormat::ratio ((float) val);
            case SliderValueType::Frequency:
                return ValueFormat::frequency ((float) val);
            case SliderValueType::TimeMs:
                return juce::String (juce::roundToInt (val)) + "ms";
            case SliderValueType::Q:
                return ValueFormat::qFactor ((float) val);
            case SliderValueType::SideDb:
                return ValueFormat::sideDB ((float) val);
            case SliderValueType::Pan:
                return ValueFormat::pan ((float) val);
            case SliderValueType::Phase:
                if (std::abs (val) < 0.005)
                    return "0.00";
                return (val > 0.0 ? "+" : "") + juce::String (val, 2);
            case SliderValueType::Slope:
            case SliderValueType::Attenuation:
            default:
                return juce::String (val, 1);
        }
    }

    SliderValueType valueType = SliderValueType::Gain;
    juce::Colour accentColour = Theme::stereoCol;
    juce::Label title;
    juce::Rectangle<int> valueArea, trackArea;
    juce::RangedAudioParameter* parameter = nullptr;
};
