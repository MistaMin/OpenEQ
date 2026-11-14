#pragma once
#include <JuceHeader.h>
#include "UI/SectionPanel.h"
#include "UI/RotaryKnob.h"
#include "UI/CompactSlider.h"
#include "UI/CycleButton.h"
#include "UI/SmallToggle.h"
#include "Parameters.h"

class OpenEQAudioProcessor;

/** A quiet card surface; DynamicsPanel places controls explicitly on a shared grid. */
class DynamicsCard : public juce::Component
{
public:
    explicit DynamicsCard (juce::String name) : title (std::move (name)) {}
    void setTitleText (const juce::String& name) { title = name; repaint(); }
    void setAccent (juce::Colour colour, const juce::String&) { accent = colour; repaint(); }
    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour (Theme::panelBottom);
        g.fillRoundedRectangle (bounds, 5.0f);
        g.setColour (Theme::textBright);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (title, 12, 7, getWidth() - 24, 18, juce::Justification::centredLeft);
        g.setColour (accent.withAlpha (0.3f));
        g.drawHorizontalLine (31, 12.0f, (float) getWidth() - 12.0f);
    }
private:
    juce::String title;
    juce::Colour accent = Theme::stereoCol;
};

/** Dynamic EQ inspector: level, timing, and detector controls for the selected band. */
class DynamicsPanel : public juce::Component,
                      private juce::AudioProcessorValueTreeState::Listener,
                      private juce::AsyncUpdater
{
public:
    explicit DynamicsPanel (OpenEQAudioProcessor& processorToUse);
    ~DynamicsPanel() override;

    void resized() override;
    void setSelectedBand (int bandIndex);
    juce::String dynamicsSummary() const;

    enum class DynView
    {
        Level = 0,
        Timing,
        Detector
    };
    void setDynView (DynView v);

private:
    void parameterChanged (const juce::String&, float) override;
    void handleAsyncUpdate() override;
    void rebuildAttachments();
    void refreshActiveState();

    OpenEQAudioProcessor& processor;
    int currentBand = 0;
    DynView currentView = DynView::Level;

    juce::TextButton levelViewButton { "Level" };
    juce::TextButton timingViewButton { "Timing" };
    juce::TextButton detectViewButton { "Detector" };
    SmallToggle dynamicsToggle { "DYNAMICS", Theme::stereoCol };

    DynamicsCard threshPanel { "Threshold" };
    RotaryKnob threshSlider, kneeSlider, onsetsSlider;

    DynamicsCard belowPanel { "Below threshold" };
    SmallToggle belowToggle;
    RotaryKnob belowRangeSlider, belowRatioSlider;

    DynamicsCard abovePanel { "Above threshold" };
    SmallToggle aboveToggle;
    RotaryKnob aboveRangeSlider, aboveRatioSlider;

    DynamicsCard timingPanel { "Envelope" };
    RotaryKnob attackKnob, releaseKnob;
    SmallToggle autoToggle;

    DynamicsCard lookaheadPanel { "Lookahead" };
    RotaryKnob aheadKnob;
    SmallToggle lookToggle;
    RotaryKnob lookKnob;

    DynamicsCard detectPanel { "Detector filter" };
    CycleButton detSourceCycle;
    SmallToggle linkToggle, listenToggle;
    RotaryKnob detFreqKnob, detQKnob;

    DynamicsCard relativePanel { "Relative detection" };
    RotaryKnob relativeKnob;
    SmallToggle relFreeToggle;
    CycleButton relSourceCycle;
    RotaryKnob relFreqKnob, relQKnob;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SliderAttachment> belowRangeAttachment, belowRatioAttachment, threshAttachment;
    std::unique_ptr<SliderAttachment> aboveRangeAttachment, aboveRatioAttachment;
    std::unique_ptr<SliderAttachment> attackAttachment, releaseAttachment, kneeAttachment, onsetsAttachment;
    std::unique_ptr<SliderAttachment> relativeAttachment, aheadAttachment;
    std::unique_ptr<SliderAttachment> detFreqAttachment, detQAttachment, relFreqAttachment, relQAttachment;
    std::unique_ptr<ButtonAttachment> belowAttachment, aboveAttachment, autoAttachment, linkAttachment, relFreeAttachment;
    std::unique_ptr<ButtonAttachment> dynamicsAttachment;
    // The global lookahead belongs to the whole plugin, so these attach once and do not follow the selected band.
    std::unique_ptr<ButtonAttachment> lookAttachment;
    std::unique_ptr<SliderAttachment> lookTimeAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DynamicsPanel)
};
