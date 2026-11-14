#include "DynamicsPanel.h"
#include "PluginProcessor.h"
#include "Parameters.h"

DynamicsPanel::DynamicsPanel (OpenEQAudioProcessor& processorToUse)
    : processor (processorToUse),
      belowToggle ("ON", Theme::stereoCol),
      aboveToggle ("ON", Theme::stereoCol),
      belowRangeSlider ("RANGE", KnobValueType::Db, Theme::stereoCol),
      belowRatioSlider ("RATIO", KnobValueType::Ratio, Theme::stereoCol),
      threshSlider ("THRESH", KnobValueType::Db, Theme::stereoCol),
      aboveRangeSlider ("RANGE", KnobValueType::Db, Theme::stereoCol),
      aboveRatioSlider ("RATIO", KnobValueType::Ratio, Theme::stereoCol),
      attackKnob ("ATTACK", KnobValueType::TimeMs, Theme::stereoCol),
      releaseKnob ("RELEASE", KnobValueType::TimeMs, Theme::stereoCol),
      kneeSlider ("KNEE", KnobValueType::Db, Theme::stereoCol),
      onsetsSlider ("ONSETS", KnobValueType::Level, Theme::stereoCol),
      relativeKnob ("REL", KnobValueType::Level, Theme::stereoCol),
      aheadKnob ("BAND AHEAD", KnobValueType::TimeMs, Theme::stereoCol),
      autoToggle ("AUTO", Theme::stereoCol),
      lookToggle ("GLOBAL", Theme::stereoCol),
      lookKnob ("GLOBAL MS", KnobValueType::TimeMs, Theme::stereoCol),
      detSourceCycle (processorToUse.apvts, Params::bandDynSourceID (0), "Source", true),
      linkToggle ("LINK", Theme::stereoCol),
      listenToggle ("LISTEN", Theme::stereoCol),
      detFreqKnob ("FREQ", KnobValueType::Frequency, Theme::stereoCol),
      detQKnob ("Q", KnobValueType::Q, Theme::stereoCol),
      relFreeToggle ("FREE", Theme::stereoCol),
      relSourceCycle (processorToUse.apvts, Params::bandDynRelSourceID (0), "Source", true),
      relFreqKnob ("FREQ", KnobValueType::Frequency, Theme::stereoCol),
      relQKnob ("Q", KnobValueType::Q, Theme::stereoCol)
{
    belowToggle.setTooltip ("Apply the Below panel: gain change below the threshold");
    aboveToggle.setTooltip ("Apply the Above panel: gain change above the threshold");
    belowRangeSlider.setTooltip ("Largest gain change below the threshold. The sign sets the direction.");
    belowRatioSlider.setTooltip ("Below threshold expansion/compression ratio. 1:1 is unexpanded, 2:1, 4:1 etc.");
    aboveRangeSlider.setTooltip ("Largest gain change above the threshold. The sign sets the direction.");
    aboveRatioSlider.setTooltip ("Above threshold compression ratio. 1:1 is uncompressed, 2:1, 4:1, inf:1 is limiting.");
    threshSlider.setTooltip ("Shared threshold for the Below and Above panels.");
    kneeSlider.setTooltip ("Soft knee width in dB. Zero is a hard threshold.");
    onsetsSlider.setTooltip ("Transient weight. Zero uses the normal envelope. Higher values follow attacks faster.");
    relativeKnob.setTooltip ("Relative detection. Zero compares against nothing; 100% uses only the level relative to the reference.");
    aheadKnob.setTooltip ("Lookahead up to 20 ms. Any amount uses a fixed 20 ms delay.");
    autoToggle.setTooltip ("Automatic timing and threshold: 8 ms attack, 140 ms release, threshold 8 dB above the broadband level.");
    linkToggle.setTooltip ("Detector follows this band's frequency and Q. Turn off to use the detect FREQ and Q.");
    listenToggle.setTooltip ("Hear the detector band-pass. This is temporary and is not saved.");
    detSourceCycle.setButtonTooltip ("Detect channel: Follow the band's stereo placement, a fixed channel, or the sidechain");
    relFreeToggle.setTooltip ("Free relative: compare against a band-pass at the relative FREQ and Q instead of the whole signal.");
    relSourceCycle.setButtonTooltip ("Relative channel: energy, a fixed channel, or the sidechain");

    for (auto* btn : { &levelViewButton, &timingViewButton, &detectViewButton })
    {
        btn->setClickingTogglesState (false);
        btn->setColour (juce::TextButton::buttonColourId, juce::Colour (0x18ffffff));
        btn->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0x5035c6f0));
        btn->setColour (juce::TextButton::textColourOffId, juce::Colour (0xff9aa0a6));
        btn->setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        addAndMakeVisible (btn);
    }
    levelViewButton.onClick = [this] { setDynView (DynView::Level); };
    timingViewButton.onClick = [this] { setDynView (DynView::Timing); };
    detectViewButton.onClick = [this] { setDynView (DynView::Detector); };

    dynamicsToggle.setTooltip ("Enable dynamic processing for the selected band.");
    addAndMakeVisible (dynamicsToggle);

    listenToggle.onClick = [this]
    {
        processor.setDetectorAudition (listenToggle.getToggleState(), currentBand);
    };

    // Header switches have their own compact bounds; controls use aligned grid cells.
    for (auto* panel : { &threshPanel, &belowPanel, &abovePanel, &timingPanel,
                         &lookaheadPanel, &detectPanel, &relativePanel })
        addAndMakeVisible (*panel);

    for (auto* control : { &threshSlider, &kneeSlider, &onsetsSlider })
        threshPanel.addAndMakeVisible (*control);
    belowPanel.addAndMakeVisible (belowToggle);
    belowPanel.addAndMakeVisible (belowRangeSlider);
    belowPanel.addAndMakeVisible (belowRatioSlider);
    abovePanel.addAndMakeVisible (aboveToggle);
    abovePanel.addAndMakeVisible (aboveRangeSlider);
    abovePanel.addAndMakeVisible (aboveRatioSlider);
    timingPanel.addAndMakeVisible (attackKnob);
    timingPanel.addAndMakeVisible (releaseKnob);
    timingPanel.addAndMakeVisible (autoToggle);
    lookaheadPanel.addAndMakeVisible (aheadKnob);
    lookaheadPanel.addAndMakeVisible (lookToggle);
    lookaheadPanel.addAndMakeVisible (lookKnob);
    lookToggle.setTooltip ("Global lookahead. The audio runs 20 ms late, and dynamic bands detect ahead by GLOBAL MS.");
    lookKnob.setTooltip ("Global lookahead time, 0 to 20 ms. Used while GLOBAL is on.");
    lookAttachment = std::make_unique<ButtonAttachment> (processorToUse.apvts, "lookaheadOn", lookToggle);
    lookTimeAttachment = std::make_unique<SliderAttachment> (processorToUse.apvts, "lookaheadMs", lookKnob);
    lookKnob.bindDefault (processorToUse.apvts, "lookaheadMs");
    detectPanel.addAndMakeVisible (detSourceCycle);
    detectPanel.addAndMakeVisible (linkToggle);
    detectPanel.addAndMakeVisible (listenToggle);
    detectPanel.addAndMakeVisible (detFreqKnob);
    detectPanel.addAndMakeVisible (detQKnob);
    relativePanel.addAndMakeVisible (relativeKnob);
    relativePanel.addAndMakeVisible (relFreeToggle);
    relativePanel.addAndMakeVisible (relSourceCycle);
    relativePanel.addAndMakeVisible (relFreqKnob);
    relativePanel.addAndMakeVisible (relQKnob);

    setDynView (DynView::Level);

    setSelectedBand (0);
    processorToUse.apvts.addParameterListener (Params::bandDynOnID (currentBand), this);
    processorToUse.apvts.addParameterListener (Params::bandDynDetLinkID (currentBand), this);
    processorToUse.apvts.addParameterListener (Params::bandDynRelFreeID (currentBand), this);
    processorToUse.apvts.addParameterListener (Params::bandDynBelowOnID (currentBand), this);
    processorToUse.apvts.addParameterListener (Params::bandDynAboveOnID (currentBand), this);
    processorToUse.apvts.addParameterListener (Params::bandTypeID (currentBand), this);
}

DynamicsPanel::~DynamicsPanel()
{
    cancelPendingUpdate();
    processor.apvts.removeParameterListener (Params::bandDynOnID (currentBand), this);
    processor.apvts.removeParameterListener (Params::bandDynDetLinkID (currentBand), this);
    processor.apvts.removeParameterListener (Params::bandDynRelFreeID (currentBand), this);
    processor.apvts.removeParameterListener (Params::bandDynBelowOnID (currentBand), this);
    processor.apvts.removeParameterListener (Params::bandDynAboveOnID (currentBand), this);
    processor.apvts.removeParameterListener (Params::bandTypeID (currentBand), this);
}

void DynamicsPanel::setSelectedBand (int bandIndex)
{
    if (currentBand == bandIndex)
    {
        rebuildAttachments();
        return;
    }

    processor.apvts.removeParameterListener (Params::bandDynOnID (currentBand), this);
    processor.apvts.removeParameterListener (Params::bandDynDetLinkID (currentBand), this);
    processor.apvts.removeParameterListener (Params::bandDynRelFreeID (currentBand), this);
    processor.apvts.removeParameterListener (Params::bandDynBelowOnID (currentBand), this);
    processor.apvts.removeParameterListener (Params::bandDynAboveOnID (currentBand), this);
    processor.apvts.removeParameterListener (Params::bandTypeID (currentBand), this);
    currentBand = bandIndex;
    processor.apvts.addParameterListener (Params::bandDynOnID (currentBand), this);
    processor.apvts.addParameterListener (Params::bandDynDetLinkID (currentBand), this);
    processor.apvts.addParameterListener (Params::bandDynRelFreeID (currentBand), this);
    processor.apvts.addParameterListener (Params::bandDynBelowOnID (currentBand), this);
    processor.apvts.addParameterListener (Params::bandDynAboveOnID (currentBand), this);
    processor.apvts.addParameterListener (Params::bandTypeID (currentBand), this);

    rebuildAttachments();
}

void DynamicsPanel::parameterChanged (const juce::String&, float)
{
    // Host automation may invoke this callback on the audio thread.
    triggerAsyncUpdate();
}

void DynamicsPanel::handleAsyncUpdate()
{
    refreshActiveState();
}

void DynamicsPanel::rebuildAttachments()
{
    const int stereoIndex = static_cast<int> (
        processor.apvts.getRawParameterValue (Params::bandStereoID (currentBand))->load());
    auto accent = Theme::accentForStereoMode (stereoIndex);
    threshPanel.setAccent (accent, "Threshold & Curve");
    belowPanel.setAccent (accent, "Below Exp/Comp");
    abovePanel.setAccent (accent, "Above Comp/Lim");
    timingPanel.setAccent (accent, "Envelope");
    lookaheadPanel.setAccent (accent, "Transient / Delay");
    detectPanel.setAccent (accent, "Detector Filter");
    relativePanel.setAccent (accent, "Relative EQ");

    for (auto* toggle : { &dynamicsToggle, &belowToggle, &aboveToggle, &autoToggle,
                          &lookToggle, &linkToggle, &listenToggle, &relFreeToggle })
        toggle->setAccent (accent);

    auto& s = processor.apvts;
    dynamicsAttachment = std::make_unique<ButtonAttachment> (s, Params::bandDynOnID (currentBand), dynamicsToggle);
    belowRangeAttachment = std::make_unique<SliderAttachment> (s, Params::bandDynBelowRangeID (currentBand), belowRangeSlider);
    belowRangeSlider.bindDefault (s, Params::bandDynBelowRangeID (currentBand));
    belowRatioAttachment = std::make_unique<SliderAttachment> (s, Params::bandDynBelowRatioID (currentBand), belowRatioSlider);
    belowRatioSlider.bindDefault (s, Params::bandDynBelowRatioID (currentBand));
    threshAttachment = std::make_unique<SliderAttachment> (s, Params::bandDynThreshID (currentBand), threshSlider);
    threshSlider.bindDefault (s, Params::bandDynThreshID (currentBand));
    aboveRangeAttachment = std::make_unique<SliderAttachment> (s, Params::bandDynAboveRangeID (currentBand), aboveRangeSlider);
    aboveRangeSlider.bindDefault (s, Params::bandDynAboveRangeID (currentBand));
    aboveRatioAttachment = std::make_unique<SliderAttachment> (s, Params::bandDynAboveRatioID (currentBand), aboveRatioSlider);
    aboveRatioSlider.bindDefault (s, Params::bandDynAboveRatioID (currentBand));
    attackAttachment = std::make_unique<SliderAttachment> (s, Params::bandDynAttackID (currentBand), attackKnob);
    attackKnob.bindDefault (s, Params::bandDynAttackID (currentBand));
    releaseAttachment = std::make_unique<SliderAttachment> (s, Params::bandDynReleaseID (currentBand), releaseKnob);
    releaseKnob.bindDefault (s, Params::bandDynReleaseID (currentBand));
    kneeAttachment = std::make_unique<SliderAttachment> (s, Params::bandDynKneeID (currentBand), kneeSlider);
    kneeSlider.bindDefault (s, Params::bandDynKneeID (currentBand));
    onsetsAttachment = std::make_unique<SliderAttachment> (s, Params::bandDynTransID (currentBand), onsetsSlider);
    onsetsSlider.bindDefault (s, Params::bandDynTransID (currentBand));
    relativeAttachment = std::make_unique<SliderAttachment> (s, Params::bandDynBlendID (currentBand), relativeKnob);
    relativeKnob.bindDefault (s, Params::bandDynBlendID (currentBand));
    aheadAttachment = std::make_unique<SliderAttachment> (s, Params::bandDynAheadID (currentBand), aheadKnob);
    aheadKnob.bindDefault (s, Params::bandDynAheadID (currentBand));
    detFreqAttachment = std::make_unique<SliderAttachment> (s, Params::bandDynDetFreqID (currentBand), detFreqKnob);
    detFreqKnob.bindDefault (s, Params::bandDynDetFreqID (currentBand));
    detQAttachment = std::make_unique<SliderAttachment> (s, Params::bandDynDetQID (currentBand), detQKnob);
    detQKnob.bindDefault (s, Params::bandDynDetQID (currentBand));
    relFreqAttachment = std::make_unique<SliderAttachment> (s, Params::bandDynRelFreqID (currentBand), relFreqKnob);
    relFreqKnob.bindDefault (s, Params::bandDynRelFreqID (currentBand));
    relQAttachment = std::make_unique<SliderAttachment> (s, Params::bandDynRelQID (currentBand), relQKnob);
    relQKnob.bindDefault (s, Params::bandDynRelQID (currentBand));

    belowAttachment = std::make_unique<ButtonAttachment> (s, Params::bandDynBelowOnID (currentBand), belowToggle);
    aboveAttachment = std::make_unique<ButtonAttachment> (s, Params::bandDynAboveOnID (currentBand), aboveToggle);
    autoAttachment = std::make_unique<ButtonAttachment> (s, Params::bandDynAutoID (currentBand), autoToggle);
    linkAttachment = std::make_unique<ButtonAttachment> (s, Params::bandDynDetLinkID (currentBand), linkToggle);
    relFreeAttachment = std::make_unique<ButtonAttachment> (s, Params::bandDynRelFreeID (currentBand), relFreeToggle);

    detSourceCycle.rebind (Params::bandDynSourceID (currentBand));
    relSourceCycle.rebind (Params::bandDynRelSourceID (currentBand));

    if (listenToggle.getToggleState())
        processor.setDetectorAudition (true, currentBand);

    refreshActiveState();
}

void DynamicsPanel::refreshActiveState()
{
    const auto type = (FilterType) juce::jlimit (0, filterTypeCount - 1,
        (int) processor.apvts.getRawParameterValue (Params::bandTypeID (currentBand))->load());
    const bool bandOn = processor.apvts.getRawParameterValue (Params::bandActiveID (currentBand))->load() >= 0.5f;
    const bool dynOn = processor.apvts.getRawParameterValue (Params::bandDynOnID (currentBand))->load() >= 0.5f;
    const bool supports = ResponseModel::supportsDynamics (type);
    const bool live = bandOn && dynOn && supports;

    const bool belowIsOn = processor.apvts.getRawParameterValue (Params::bandDynBelowOnID (currentBand))->load() >= 0.5f;
    const bool aboveIsOn = processor.apvts.getRawParameterValue (Params::bandDynAboveOnID (currentBand))->load() >= 0.5f;
    const bool linked = processor.apvts.getRawParameterValue (Params::bandDynDetLinkID (currentBand))->load() > 0.5f;
    const bool relFree = processor.apvts.getRawParameterValue (Params::bandDynRelFreeID (currentBand))->load() > 0.5f;

    dynamicsToggle.setEnabled (bandOn && supports);
    belowToggle.setEnabled (live);
    aboveToggle.setEnabled (live);
    belowRangeSlider.setEnabled (live && belowIsOn);
    belowRatioSlider.setEnabled (live && belowIsOn);
    aboveRangeSlider.setEnabled (live && aboveIsOn);
    aboveRatioSlider.setEnabled (live && aboveIsOn);
    relativeKnob.setEnabled (live);
    threshSlider.setEnabled (live);
    kneeSlider.setEnabled (live);
    onsetsSlider.setEnabled (live);
    attackKnob.setEnabled (live);
    releaseKnob.setEnabled (live);
    autoToggle.setEnabled (live);
    aheadKnob.setEnabled (live);
    lookToggle.setEnabled (live);
    lookKnob.setEnabled (live);
    detSourceCycle.setEnabled (live);
    linkToggle.setEnabled (live);
    listenToggle.setEnabled (live);
    detFreqKnob.setEnabled (live && ! linked);
    detQKnob.setEnabled (live && ! linked);
    relFreeToggle.setEnabled (live);
    relSourceCycle.setEnabled (live);
    relFreqKnob.setEnabled (live && relFree);
    relQKnob.setEnabled (live && relFree);

    const int src = (int) processor.apvts.getRawParameterValue (Params::bandDynSourceID (currentBand))->load();
    const bool customDetect = (src != 0) || (! linked);
    detectViewButton.setButtonText (customDetect ? "Detector *" : "Detector");

    if (! live && listenToggle.getToggleState())
    {
        listenToggle.setToggleState (false, juce::dontSendNotification);
        processor.setDetectorAudition (false, currentBand);
    }
}

void DynamicsPanel::setDynView (DynView v)
{
    currentView = v;
    const bool isLevel = (currentView == DynView::Level);
    const bool isTiming = (currentView == DynView::Timing);
    const bool isDetect = (currentView == DynView::Detector);

    levelViewButton.setColour (juce::TextButton::buttonColourId, isLevel ? juce::Colour (0x5035c6f0) : juce::Colour (0x18ffffff));
    levelViewButton.setColour (juce::TextButton::textColourOffId, isLevel ? juce::Colours::white : juce::Colour (0xff9aa0a6));

    timingViewButton.setColour (juce::TextButton::buttonColourId, isTiming ? juce::Colour (0x5035c6f0) : juce::Colour (0x18ffffff));
    timingViewButton.setColour (juce::TextButton::textColourOffId, isTiming ? juce::Colours::white : juce::Colour (0xff9aa0a6));

    detectViewButton.setColour (juce::TextButton::buttonColourId, isDetect ? juce::Colour (0x5035c6f0) : juce::Colour (0x18ffffff));
    detectViewButton.setColour (juce::TextButton::textColourOffId, isDetect ? juce::Colours::white : juce::Colour (0xff9aa0a6));

    threshPanel.setVisible (isLevel);
    belowPanel.setVisible (isLevel);
    abovePanel.setVisible (isLevel);

    timingPanel.setVisible (isTiming);
    lookaheadPanel.setVisible (isTiming);

    detectPanel.setVisible (isDetect);
    relativePanel.setVisible (isDetect);

    resized();
}

juce::String DynamicsPanel::dynamicsSummary() const
{
    return "E " + juce::String (processor.getDynamicEnvelopeDb (currentBand), 1)
         + "  T " + juce::String (processor.getDynamicThresholdDb (currentBand), 1)
         + "  G " + juce::String (processor.getDynamicGainDb (currentBand), 1);
}

void DynamicsPanel::resized()
{
    auto bounds = getLocalBounds().reduced (4, 2);
    auto navigation = bounds.removeFromTop (28);
    levelViewButton.setBounds (navigation.removeFromLeft (80).reduced (1, 2));
    timingViewButton.setBounds (navigation.removeFromLeft (80).reduced (1, 2));
    detectViewButton.setBounds (navigation.removeFromLeft (90).reduced (1, 2));
    dynamicsToggle.setBounds (navigation.removeFromRight (104));
    bounds.removeFromTop (6);

    constexpr int gap = 10;
    if (currentView == DynView::Level)
    {
        const int cardWidth = (bounds.getWidth() - gap * 2) / 3;
        threshPanel.setBounds (bounds.removeFromLeft (cardWidth));
        bounds.removeFromLeft (gap);
        belowPanel.setBounds (bounds.removeFromLeft (cardWidth));
        bounds.removeFromLeft (gap);
        abovePanel.setBounds (bounds);
    }
    else
    {
        const int halfWidth = (bounds.getWidth() - gap) / 2;
        auto left = bounds.removeFromLeft (halfWidth);
        bounds.removeFromLeft (gap);
        timingPanel.setBounds (left);
        lookaheadPanel.setBounds (bounds);
        detectPanel.setBounds (left);
        relativePanel.setBounds (bounds);
    }

    const auto headerSwitch = [] (juce::Component& card, juce::Component& control, int width = 64)
    {
        control.setBounds (card.getWidth() - width - 8, 2, width, 28);
    };
    const auto row = [] (juce::Component& card, std::initializer_list<juce::Component*> controls, int top)
    {
        auto area = card.getLocalBounds().reduced (12, 0);
        area.removeFromTop (top);
        area.removeFromBottom (8);
        const int count = (int) controls.size();
        const int cellWidth = area.getWidth() / count;
        for (auto* control : controls)
        {
            auto cell = area.removeFromLeft (cellWidth);
            const int width = juce::jmin (92, cell.getWidth() - 8);
            control->setBounds (cell.withSizeKeepingCentre (width, juce::jmin (112, cell.getHeight())));
        }
    };

    headerSwitch (belowPanel, belowToggle, 52);
    headerSwitch (abovePanel, aboveToggle, 52);
    row (threshPanel, { &threshSlider, &kneeSlider, &onsetsSlider }, 40);
    row (belowPanel, { &belowRangeSlider, &belowRatioSlider }, 40);
    row (abovePanel, { &aboveRangeSlider, &aboveRatioSlider }, 40);

    headerSwitch (timingPanel, autoToggle);
    headerSwitch (lookaheadPanel, lookToggle, 76);
    row (timingPanel, { &attackKnob, &releaseKnob }, 40);
    row (lookaheadPanel, { &aheadKnob, &lookKnob }, 40);

    headerSwitch (detectPanel, listenToggle);
    linkToggle.setBounds (detectPanel.getWidth() - 140, 2, 64, 28);
    headerSwitch (relativePanel, relFreeToggle);
    detSourceCycle.setBounds (12, 38, detectPanel.getWidth() - 24, 26);
    relSourceCycle.setBounds (12, 38, relativePanel.getWidth() - 24, 26);
    row (detectPanel, { &detFreqKnob, &detQKnob }, 70);
    row (relativePanel, { &relativeKnob, &relFreqKnob, &relQKnob }, 70);
}
