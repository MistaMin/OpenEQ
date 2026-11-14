#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Theme.h"
#include <functional>
#include <memory>
#include <utility>
#include <vector>

// Modern flat cycle button look with subtle hover/down states.
class CycleButtonLNF final : public juce::LookAndFeel_V4 {
public:
    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override {
        auto r = b.getLocalBounds().toFloat().reduced(1.5f);
        juce::Colour base = juce::Colour(0xff2c3439);
        if (over) base = base.brighter(0.08f);
        if (down) base = base.darker(0.1f);

        g.setColour(base);
        g.fillRoundedRectangle(r, 4.0f);

        g.setColour(juce::Colours::white.withAlpha(0.08f));
        g.drawHorizontalLine(juce::roundToInt(r.getY()) + 1, r.getX() + 3, r.getRight() - 3);

        g.setColour(juce::Colour(0xff151a1d));
        g.drawRoundedRectangle(r, 4.0f, 1.0f);
    }
    juce::Font getTextButtonFont(juce::TextButton&, int) override
    {
        return juce::FontOptions(10.0f, juce::Font::bold);
    }
};

// A mechanical choice key opens a dropdown bound to stable DSP choice values.
class CycleButton : public juce::Component, private juce::AudioProcessorValueTreeState::Listener {
public:
    CycleButton(juce::AudioProcessorValueTreeState& state, const juce::String& paramIDToUse,
                const juce::String& labelText, bool horizontalLayout = false)
        : apvts(state), paramID(paramIDToUse), isHorizontal(horizontalLayout)
    {
        param = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(paramID));
        jassert(param != nullptr);

        title.setText(labelText, juce::dontSendNotification);
        title.setJustificationType(juce::Justification::centredLeft);
        title.setFont(juce::FontOptions(isHorizontal ? 9.5f : 10.5f, juce::Font::bold));
        title.setColour(juce::Label::textColourId,
                        isHorizontal ? Theme::textMid : Theme::textDark);
        addAndMakeVisible(title);

        lnf = std::make_unique<CycleButtonLNF>();
        button.setLookAndFeel(lnf.get());
        button.setColour(juce::TextButton::buttonColourId, Theme::buttonTop);
        button.setColour(juce::TextButton::buttonOnColourId, Theme::buttonBottom);
        button.setColour(juce::TextButton::textColourOffId, Theme::buttonText);
        button.setColour(juce::TextButton::textColourOnId, Theme::buttonText);
        button.onClick = [this] { showChoices(); };
        addAndMakeVisible(button);

        updateText();
        apvts.addParameterListener(paramID, this);
    }

    ~CycleButton() override
    {
        apvts.removeParameterListener(paramID, this);
    }

    // Re-point this cycle button at a different parameter (used when the
    // selected band changes). Safe to call anytime; the button's choice list
    // and current value refresh immediately.
    void rebind(const juce::String& newID)
    {
        if (newID == paramID)
            return;
        apvts.removeParameterListener(paramID, this);
        paramID = newID;
        param = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(paramID));
        jassert(param != nullptr);
        updateText();
        apvts.addParameterListener(paramID, this);
    }

    void resized() override
    {
        auto b = getLocalBounds();
        if (isHorizontal) {
            title.setBounds(b.removeFromLeft(42));
            button.setBounds(b.reduced(0, 1));
        } else if(getHeight()>44) {
            title.setJustificationType(juce::Justification::centred);
            title.setBounds(b.removeFromTop(14));
            button.setBounds(b.withHeight(28).withY(b.getY()+6).reduced(2,1));
        } else {
            title.setJustificationType(juce::Justification::centredLeft);
            // The whole title is shown: the label has no side padding, and its width follows the text.
            title.setFont(juce::FontOptions(8.5f, juce::Font::bold));
            title.setBorderSize(juce::BorderSize<int>(0, 1, 0, 3));
            title.setMinimumHorizontalScale(1.0f);
            const int titleW = juce::jmin (getWidth() / 2, juce::roundToInt (title.getFont().getStringWidthFloat (title.getText()) + 8.0f));
            title.setBounds(b.removeFromLeft(titleW));
            button.setBounds(b.reduced(1, 1));
        }
    }

    // Grey-out styling used when the owning band is bypassed.
    void setActive(bool active)
    {
        if (isActive == active)
            return;
        isActive = active;
        refreshTheme();
    }

    void setPlateText(const juce::Colour* text, const juce::Colour* mid)
    {
        hasPlateText = text != nullptr;
        if (hasPlateText) { plateText = *text; plateMid = *mid; }
        refreshTheme();
    }

    void refreshTheme()
    {
        if (isActive) {
            button.setColour(juce::TextButton::buttonColourId, Theme::buttonTop);
            button.setColour(juce::TextButton::buttonOnColourId, Theme::buttonBottom);
            button.setColour(juce::TextButton::textColourOffId, Theme::buttonText);
            button.setColour(juce::TextButton::textColourOnId, Theme::buttonText);
        } else {
            button.setColour(juce::TextButton::buttonColourId, Theme::buttonInactiveTop);
            button.setColour(juce::TextButton::buttonOnColourId, Theme::buttonInactiveBottom);
            button.setColour(juce::TextButton::textColourOffId, Theme::buttonTextInactive);
            button.setColour(juce::TextButton::textColourOnId, Theme::buttonTextInactive);
        }
        title.setColour(juce::Label::textColourId, isActive
                             ? (hasPlateText ? (isHorizontal ? plateMid : plateText)
                                             : (isHorizontal ? Theme::textMid : Theme::textDark))
                             : Theme::buttonTextInactive);
        button.repaint();
    }

    bool getActive() const { return isActive; }

    void setButtonTooltip(const juce::String& tip) { button.setTooltip(tip); }

    // Optional submenu grouping. Returns the group for a choice index, or an empty string for none.
    // Ungrouped choices come first in the menu. Groups follow `order`, then any others in first-appearance order.
    // Unset keeps the flat list.
    void setChoiceGroups(std::function<juce::String(int)> groupFor, juce::StringArray order = {})
    {
        choiceGroupFor = std::move(groupFor);
        choiceGroupOrder = std::move(order);
    }

private:
    void showChoices()
    {
        if(!param)return;
        juce::PopupMenu menu;
        if (choiceGroupFor)
        {
            // Ungrouped choices first, in index order.
            juce::StringArray seen;
            for (int n = 0; n < param->choices.size(); ++n)
            {
                const auto group = choiceGroupFor(n);
                if (group.isEmpty())
                    menu.addItem(n+1, param->choices[n], true, n==param->getIndex());
                else if (! seen.contains(group))
                    seen.add(group);
            }

            // Groups in the requested order, then any unlisted groups in first-appearance order.
            juce::StringArray groupOrder;
            for (const auto& name : choiceGroupOrder)
                if (seen.contains(name))
                    groupOrder.add(name);
            for (const auto& name : seen)
                if (! groupOrder.contains(name))
                    groupOrder.add(name);

            for (const auto& group : groupOrder)
            {
                juce::PopupMenu sub;
                for (int n = 0; n < param->choices.size(); ++n)
                    if (choiceGroupFor(n) == group)
                        sub.addItem(n+1, param->choices[n], true, n==param->getIndex());
                menu.addSubMenu(group, sub);
            }
        }
        else
        {
            for(int n=0;n<param->choices.size();++n)
                menu.addItem(n+1,param->choices[n],true,n==param->getIndex());
        }
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&button),
            [safe=juce::Component::SafePointer<CycleButton>(this)](int result){
                if(!safe || result<=0)return;
                auto* parameter=safe->param;
                parameter->beginChangeGesture();
                parameter->setValueNotifyingHost(parameter->getNormalisableRange().convertTo0to1(float(result-1)));
                parameter->endChangeGesture();safe->updateText();
                if(safe->onChoiceSelected) safe->onChoiceSelected(result-1);
            });
    }

    void parameterChanged(const juce::String&, float) override
    {
        juce::MessageManager::callAsync([safeThis = juce::Component::SafePointer<CycleButton>(this)] {
            if (safeThis != nullptr)
                safeThis->updateText();
        });
    }

    void updateText()
    {
        if (param != nullptr)
            button.setButtonText(param->getCurrentChoiceName());
    }

    juce::AudioProcessorValueTreeState& apvts;
    juce::String paramID;
    juce::AudioParameterChoice* param = nullptr;
    std::function<juce::String(int)> choiceGroupFor;
public:
    /** Called after the user picks a choice from the menu, with the choice index. */
    std::function<void(int)> onChoiceSelected;
private:
    juce::StringArray choiceGroupOrder;
    bool isActive = true;
    bool hasPlateText = false;
    juce::Colour plateText, plateMid;
    bool isHorizontal = false;
    std::unique_ptr<juce::LookAndFeel> lnf;
    juce::Label title;
    juce::TextButton button;
};
