#pragma once
#include <JuceHeader.h>
#include <functional>
#include "UI/SectionPanel.h"
#include "UI/RotaryKnob.h"
#include "UI/CompactSlider.h"
#include "UI/CycleButton.h"
#include "UI/SmallToggle.h"

class OpenEQAudioProcessor;

/** Control strip for the selected band. The left card has the band switches (on, solo, dynamic), the filter
    type and the stereo placement. The right card is one row of knobs: FREQ, GAIN, Q, SLOPE, PAN and PHASE,
    the same knob the Split tab and the output section use. */
class BandControlPanel : public juce::Component,
                         private juce::AudioProcessorValueTreeState::Listener,
                         private juce::AsyncUpdater
{
public:
    explicit BandControlPanel (OpenEQAudioProcessor& processorToUse);
    ~BandControlPanel() override;

    void resized() override;
    void setSelectedBand (int bandIndex);

    /** Fires when the selected band's dynamic state, type, or on/off changes. The editor refreshes the dynamics strip. */
    std::function<void()> onDynamicsChanged;
    /** Fires when split mode is toggled on for the band. */
    std::function<void()> onSplitToggled;
    /** Steps the selected band by -1 or +1 slot, wrapping at both ends (arrows). */
    std::function<void (int)> onStepBand;

private:
    void parameterChanged (const juce::String& parameterID, float) override;
    void handleAsyncUpdate() override;
    void rebuildAttachments();
    void refreshBandData();
    void refreshActiveState();

    OpenEQAudioProcessor& processor;
    int currentBand = 0;

    SectionPanel bandPanel { "BAND 1" };
    juce::TextButton prevBandButton { "<" }, nextBandButton { ">" };
    SmallToggle activeToggle, soloToggle;
    SmallToggle dynOnToggle;
    CycleButton typeCycle, stereoCycle;
    RotaryKnob panKnob;
    RotaryKnob phaseKnob;

    SectionPanel knobPanel { "PARAMETRIC EQUALIZER" };
    RotaryKnob freqKnob, gainKnob, qKnob;
    RotaryKnob slopeKnob;

    SectionPanel splitPanel { "SPLIT" };
    SmallToggle splitToggle;
    CompactSlider splitTransKnob, splitTailKnob;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SliderAttachment> freqAttachment, gainAttachment, qAttachment, panAttachment, slopeAttachment;
    std::unique_ptr<ButtonAttachment> activeAttachment, soloAttachment, dynOnAttachment, splitAttachment;
    std::unique_ptr<SliderAttachment> splitTransAttachment, splitTailAttachment, phaseAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandControlPanel)
};
