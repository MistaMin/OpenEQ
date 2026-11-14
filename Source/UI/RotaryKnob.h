#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "ValueFormat.h"
#include "Theme.h"
#include "SectionPanel.h"
#include "../AppSettings.h"
#include <cmath>
#include <functional>
#include <GoodLookinUI.h>

enum class KnobValueType { Frequency, Gain, Q, Level, TimeMs, SideDb, Pan, Slope, Attenuation, Ratio, WholeDb, Octaves, Db };

// Rotary control drawn flat: a glowing value ring around a dark glass face with a short pointer bar.
// Controls that are symmetric about zero fill the ring from the centre. The value sits on a pill below.
class RotaryKnob : public juce::Slider, private juce::Timer
{
public:
    /** Remembers the attached parameter so double-click and modifier-click can reset to its default.
        Call with the same state and id as the knob's attachment. */
    void bindDefault (juce::AudioProcessorValueTreeState& state, const juce::String& id)
    {
        parameter = state.getParameter (id);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto& settings = AppSettings::get();
        const int resetFlags = settings.knobResetModifier();
        if (resetFlags != 0 && (e.mods.getRawFlags() & resetFlags) == resetFlags)
        {
            resetToDefault();
            return;
        }

        // Drag speed is a setting. 100% matches the stock sensitivity of 250 px for a full drag.
        // Per-control factors (frequency, gain, Q) and the gain range option scale it further.
        float speed = settings.knobDragSpeed() / 100.0f;
        if (const int control = speedControl(); control >= 0)
            speed *= settings.knobSpeedFactor (control, false);
        if (valueType == KnobValueType::Gain && settings.gainKnobSpeedFollowsDisplayRange())
            speed *= juce::jlimit (6.0f, 30.0f, AppSettings::displayRangeDb().load()) / 30.0f;
        const int fineFlags = settings.fineTuneModifier();
        if (fineFlags != 0 && (e.mods.getRawFlags() & fineFlags) == fineFlags)
            speed *= 0.1f;
        setMouseDragSensitivity (juce::roundToInt (250.0f / juce::jmax (0.01f, speed)));
        juce::Slider::mouseDown (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        juce::Slider::mouseDrag (e);
        clampToDisplayRange();
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (AppSettings::get().knobDoubleClickAction() == 1)
            openTypedEntry();
        else
            resetToDefault();
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (wheel.deltaY == 0.0f)
            return;

        const auto& settings = AppSettings::get();
        float direction = settings.reverseWheel() ? -1.0f : 1.0f;
        const int invertFlags = settings.wheelInvertModifier();
        if (invertFlags != 0 && (e.mods.getRawFlags() & invertFlags) == invertFlags)
            direction = -direction;
        float speed = settings.knobWheelSpeed() / 100.0f;
        if (const int control = speedControl(); control >= 0)
            speed *= settings.knobSpeedFactor (control, true);
        if (valueType == KnobValueType::Gain && settings.gainKnobSpeedFollowsDisplayRange())
            speed *= juce::jlimit (6.0f, 30.0f, AppSettings::displayRangeDb().load()) / 30.0f;
        const int fineFlags = settings.fineTuneModifier();
        if (fineFlags != 0 && (e.mods.getRawFlags() & fineFlags) == fineFlags)
            speed *= 0.1f;

        // A notch of the wheel moves a quarter of the knob's travel at 100%, so one wheel tick is audible without jumping.
        const double proportion = juce::jlimit (0.0, 1.0, valueToProportionOfLength (getValue())
                                                             + (double) direction * (double) wheel.deltaY * 0.25 * speed);
        setValue (proportionOfLengthToValue (proportion), juce::sendNotificationSync);
        clampToDisplayRange();
    }

    RotaryKnob (const juce::String& labelText, KnobValueType type, juce::Colour accent)
        : valueType (type), accentColour (accent)
    {
        setSliderStyle (juce::Slider::RotaryVerticalDrag);
        setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                             juce::MathConstants<float>::pi * 2.75f, true);
        setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        setMouseDragSensitivity (220);
        setScrollWheelEnabled (false);

        title.setText (labelText, juce::dontSendNotification);
        title.setJustificationType (juce::Justification::centred);
        title.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, Theme::labelColour);
        title.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (title);

        startTimerHz (60);
    }

    /** Replaces the readout text for controls whose unit is not one of the built-in types. */
    void setValueText (std::function<juce::String (float)> text) { valueText = std::move (text); repaint(); }

    void setActive (bool active)
    {
        if (isActive == active)
            return;
        isActive = active;
        repaint();
    }
    bool getActive() const { return isActive; }

    /** Changes what the knob shows, for controls whose meaning depends on the band type (slope or attenuation). */
    void setValueType (KnobValueType type, const juce::String& labelText)
    {
        if (valueType == type && title.getText() == labelText)
            return;
        valueType = type;
        title.setText (labelText, juce::dontSendNotification);
        repaint();
    }

    void setAccent (juce::Colour c)
    {
        accentColour = c;
        repaint();
    }
    juce::Colour getAccent() const { return accentColour; }

    void setPlateText (const juce::Colour* text, const juce::Colour* mid)
    {
        hasPlateText = text != nullptr;
        if (hasPlateText) { plateText = *text; plateMid = *mid; }
        title.setColour (juce::Label::textColourId, hasPlateText ? plateText : Theme::labelColour);
        repaint();
    }

    /** Re-reads the theme colours a widget copied when it was built. */
    void refreshTheme()
    {
        title.setColour (juce::Label::textColourId, hasPlateText ? plateText : Theme::labelColour);
        repaint();
    }

    void enablementChanged() override { repaint(); }

    void resized() override
    {
        auto b = getLocalBounds();
        title.setBounds (b.removeFromTop (14));
        valueArea = b.removeFromBottom (18);
        knobArea = b.reduced (0, 2);
    }

    void paint (juce::Graphics& g) override
    {
        // A disabled knob reads the same as an inactive one.
        const bool lit = isActive && isEnabled();
        auto bounds = knobArea.toFloat();
        const float diameter = juce::jmin (bounds.getWidth(), bounds.getHeight()) - 2.0f;
        if (diameter < 8.0f)
            return;

        const float r = diameter * 0.5f;
        const auto centre = bounds.getCentre();
        const float startA = getRotaryParameters().startAngleRadians;
        const float endA   = getRotaryParameters().endAngleRadians;
        const float angle  = startA + (float) valueToProportionOfLength (getValue()) * (endA - startA);

        // A control whose range is symmetric about zero (gain, pan, phase) fills from the centre, so the arc
        // shows direction as well as amount. Everything else fills from the start.
        const bool bipolar = getMinimum() < 0.0 && std::abs (getMinimum() + getMaximum()) < 1.0e-6;
        const float originA = bipolar ? startA + (float) valueToProportionOfLength (0.0) * (endA - startA) : startA;

        const juce::Colour accent = lit ? accentColour : Theme::textDim;
        const float ringW = juce::jlimit (3.0f, 6.0f, r * 0.17f);
        const float ringR = r - ringW * 0.5f - 1.0f;
        const auto rounded = juce::PathStrokeType (ringW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

        // Track ring.
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, ringR, ringR, 0.0f, startA, endA, true);
        g.setColour (juce::Colour (0xff070a0c));
        g.strokePath (track, juce::PathStrokeType (ringW + 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (juce::Colours::white.withAlpha (0.09f));
        g.strokePath (track, rounded);

        // Value arc: a soft bloom underneath, then a gradient stroke that brightens toward the current value.
        if (std::abs (angle - originA) > 0.01f)
        {
            juce::Path arc;
            arc.addCentredArc (centre.x, centre.y, ringR, ringR, 0.0f, juce::jmin (originA, angle), juce::jmax (originA, angle), true);
            if (lit)
            {
                g.setColour (accent.withAlpha (0.10f));
                g.strokePath (arc, juce::PathStrokeType (ringW + 8.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setColour (accent.withAlpha (0.16f));
                g.strokePath (arc, juce::PathStrokeType (ringW + 4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            const juce::Point<float> from (centre.x + std::sin (originA) * ringR, centre.y - std::cos (originA) * ringR);
            const juce::Point<float> to (centre.x + std::sin (angle) * ringR, centre.y - std::cos (angle) * ringR);
            g.setGradientFill (juce::ColourGradient (accent.darker (0.35f).withAlpha (lit ? 0.9f : 0.5f), from,
                                                     accent.brighter (0.25f).withAlpha (lit ? 1.0f : 0.6f), to, false));
            g.strokePath (arc, rounded);
        }

        // Centre mark on the ring for bipolar controls.
        if (bipolar)
        {
            const float inner = ringR - ringW * 0.5f - 1.0f, outer = ringR + ringW * 0.5f + 1.0f;
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            g.drawLine (centre.x + std::sin (originA) * inner, centre.y - std::cos (originA) * inner,
                        centre.x + std::sin (originA) * outer, centre.y - std::cos (originA) * outer, 1.2f);
        }

        // Flat face inside the ring: near-black glass with a faint top light and a hairline rim.
        const float faceR = ringR - ringW * 0.5f - 4.0f;
        if (faceR > 3.0f)
        {
            const juce::Rectangle<float> face (centre.x - faceR, centre.y - faceR, faceR * 2.0f, faceR * 2.0f);
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.fillEllipse (face.translated (0.0f, 2.0f));
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a3238), centre.x, centre.y - faceR,
                                                     juce::Colour (0xff12171a), centre.x, centre.y + faceR, false));
            g.fillEllipse (face);
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.22f), centre.x, centre.y - faceR,
                                                     juce::Colours::white.withAlpha (0.02f), centre.x, centre.y + faceR, false));
            g.drawEllipse (face.reduced (0.5f), 1.0f);

            // Pointer: a short bright bar near the edge of the face, tinted by the accent.
            const float p1 = faceR * 0.48f, p2 = faceR * 0.86f;
            juce::Path pointer;
            pointer.startNewSubPath (centre.x + std::sin (angle) * p1, centre.y - std::cos (angle) * p1);
            pointer.lineTo (centre.x + std::sin (angle) * p2, centre.y - std::cos (angle) * p2);
            const float pointerW = juce::jlimit (2.0f, 3.5f, faceR * 0.14f);
            if (lit)
            {
                g.setColour (accent.withAlpha (0.35f));
                g.strokePath (pointer, juce::PathStrokeType (pointerW + 3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            g.setColour (lit ? accent.interpolatedWith (juce::Colours::white, 0.65f) : Theme::textDim);
            g.strokePath (pointer, juce::PathStrokeType (pointerW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // Value readout: plain text on a quiet pill, tinted while the pointer is over the knob or dragging it.
        const float maxPlateW = juce::jmax (24.0f, valueArea.toFloat().getWidth() - 2.0f);
        const float plateW = juce::jmin (76.0f, maxPlateW);
        auto readout = valueArea.toFloat().withSizeKeepingCentre (plateW, 16.0f);
        const bool hot = lit && (isMouseOverOrDragging() || isMouseButtonDown());
        g.setColour (Theme::readoutFill.withMultipliedAlpha (0.75f));
        g.fillRoundedRectangle (readout, 8.0f);
        g.setColour (hot ? accent.withAlpha (0.8f) : Theme::readoutEdge.withMultipliedAlpha (0.6f));
        g.drawRoundedRectangle (readout, 8.0f, 1.0f);
        g.setColour (lit ? (hot ? accent.interpolatedWith (juce::Colours::white, 0.6f) : Theme::readoutText) : Theme::knobTextInactive);
        const float fontSize = (plateW < 48.0f) ? 9.0f : 10.5f;
        g.setFont (juce::FontOptions (fontSize, juce::Font::bold));
        g.drawText (formattedValue(), readout, juce::Justification::centred);
    }

    void mouseEnter (const juce::MouseEvent& e) override { juce::Slider::mouseEnter (e); repaint(); }
    void mouseExit (const juce::MouseEvent& e) override  { juce::Slider::mouseExit (e); repaint(); }

private:
    /** Frequency 0, gain 1, Q 2: the controls with their own speed settings. -1 for the rest. */
    int speedControl() const noexcept
    {
        switch (valueType)
        {
            case KnobValueType::Frequency: return 0;
            case KnobValueType::Gain:      return 1;
            case KnobValueType::Q:         return 2;
            default:                       return -1;
        }
    }

    void resetToDefault()
    {
        if (parameter != nullptr)
            setValue (parameter->convertFrom0to1 (parameter->getDefaultValue()), juce::sendNotificationSync);
    }

    /** reference "limit gain knob to displayed dB range" option. */
    void clampToDisplayRange()
    {
        if (valueType != KnobValueType::Gain || ! AppSettings::get().gainKnobFollowsDisplayRange())
            return;
        const double limit = (double) AppSettings::displayRangeDb().load();
        const double value = getValue();
        if (value > limit)
            setValue (limit, juce::sendNotificationSync);
        else if (value < -limit)
            setValue (-limit, juce::sendNotificationSync);
    }

    /** Typed-value entry on knob double-click behaviour. */
    void openTypedEntry()
    {
        auto* window = new juce::AlertWindow ("Set value", "Type a value for " + title.getText() + ".", juce::AlertWindow::NoIcon);
        window->addTextEditor ("value", formattedValue(), {});
        window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

        juce::Component::SafePointer<RotaryKnob> safe (this);
        window->enterModalState (true, juce::ModalCallbackFunction::create ([safe, window] (int result)
        {
            if (result == 1 && safe != nullptr)
                safe->applyTypedValue (window->getTextEditorContents ("value"));
            delete window;
        }), false);
    }

    void applyTypedValue (const juce::String& text)
    {
        float value = 0.0f;
        bool parsed = true;
        switch (valueType)
        {
            case KnobValueType::Frequency: parsed = ValueFormat::parseFrequency (text, value); break;
            case KnobValueType::Gain:      parsed = ValueFormat::parseGainDb (text, value); break;
            case KnobValueType::Slope:
                // "24" or "24 dB/oct" is a slope in dB/oct; a value up to 1 is the normalised slope.
                parsed = text.containsAnyOf ("0123456789");
                value = (float) text.getDoubleValue();
                if (value > 1.0f) value = ValueFormat::slopeNormForLabelDb (value);
                break;
            case KnobValueType::Attenuation:
                parsed = text.containsAnyOf ("0123456789");
                value = (float) text.getDoubleValue();
                if (value > 1.0f || text.containsChar ('%')) value *= 0.01f;
                break;
            default:
                parsed = text.containsAnyOf ("0123456789");
                value = (float) text.getDoubleValue();
                break;
        }
        if (! parsed)
            return;
        setValue (juce::jlimit (getMinimum(), getMaximum(), (double) value), juce::sendNotificationSync);
        clampToDisplayRange();
    }

    juce::RangedAudioParameter* parameter = nullptr;

    void timerCallback() override
    {
        const auto target = valueToProportionOfLength (getValue());
        if (! isShowing()) { motion.position = target; motion.velocity = 0; return; }
        if (std::abs (motion.position - target) < 0.0001 && std::abs (motion.velocity) < 0.0001)
            return;
        motion.step (target, 1.0 / 60.0);
        repaint();
    }

    goodlookinui::Motion motion;

    juce::String formattedValue() const
    {
        if (valueText != nullptr)
            return valueText (static_cast<float> (getValue()));
        return formatValue (static_cast<float> (getValue()));
    }

    juce::String formatValue (float v) const
    {
        switch (valueType)
        {
            case KnobValueType::Frequency: return ValueFormat::frequency (v);
            case KnobValueType::Gain:      return ValueFormat::gainDB (v);
            case KnobValueType::Q:         return ValueFormat::qFactor (v);
            case KnobValueType::Level:     return ValueFormat::percent (v);
            case KnobValueType::TimeMs:    return ValueFormat::timeMs (v);
            case KnobValueType::SideDb:    return ValueFormat::sideDB (v);
            case KnobValueType::Pan:       return ValueFormat::pan (v);
            case KnobValueType::Slope:     return ValueFormat::slope (v);
            case KnobValueType::Attenuation: return ValueFormat::percent (v * 100.0f);
            case KnobValueType::Db:        return ValueFormat::gainDB (v);
            case KnobValueType::Ratio:     return ValueFormat::ratio (v);
            case KnobValueType::WholeDb:   return juce::String (juce::roundToInt (v)) + " dB";
            case KnobValueType::Octaves:   return juce::String (v, 1) + " oct";
        }
        return {};
    }

    std::function<juce::String (float)> valueText;
    KnobValueType valueType;
    juce::Colour accentColour;
    bool isActive = true;
    bool hasPlateText = false;
    juce::Colour plateText, plateMid;
    juce::Label title;
    juce::Rectangle<int> knobArea;
    juce::Rectangle<int> valueArea;
};
