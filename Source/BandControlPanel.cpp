#include "BandControlPanel.h"
#include "PluginProcessor.h"
#include "Parameters.h"

BandControlPanel::BandControlPanel (OpenEQAudioProcessor& processorToUse)
    : processor (processorToUse),
      activeToggle ("On", Theme::stereoCol),
      soloToggle ("Solo", juce::Colour (0xfff59e0b)),
      dynOnToggle ("D", juce::Colour (0xff38bdf8)),
      typeCycle (processorToUse.apvts, Params::bandTypeID (0), "TYPE", false),
      stereoCycle (processorToUse.apvts, Params::bandStereoID (0), "STEREO", false),
      panKnob ("PAN", KnobValueType::Pan, Theme::stereoCol),
      phaseKnob ("PHASE", KnobValueType::Attenuation, Theme::stereoCol),
      freqKnob ("FREQ", KnobValueType::Frequency, Theme::stereoCol),
      gainKnob ("GAIN", KnobValueType::Gain, Theme::stereoCol),
      qKnob ("Q", KnobValueType::Q, Theme::stereoCol),
      slopeKnob ("SLOPE", KnobValueType::Slope, Theme::stereoCol),
      splitToggle ("SPLIT", Theme::stereoCol),
      splitTransKnob ("TRANS", SliderValueType::Gain, Theme::splitTransCol),
      splitTailKnob ("TONAL", SliderValueType::Gain, Theme::splitTonalCol)
{
    activeToggle.setTooltip ("Enable selected band");
    soloToggle.setTooltip ("Solo selected band");
    dynOnToggle.setTooltip ("Dynamic EQ for the selected band. Switches inspector to Dynamics.");

    typeCycle.setChoiceGroups ([] (int typeIndex) { return Params::filterTypeGroup (typeIndex); },
                               Params::filterTypeGroupOrder());
    typeCycle.setButtonTooltip ("Filter type, grouped by family");
    // A cut at slope 0 has order 0 and does nothing, so picking a cut gives it 12 dB/oct when its slope is still 0.
    typeCycle.onChoiceSelected = [this] (int) { processor.ensureCutSlope (currentBand); };
    slopeKnob.setTooltip ("Slope: the filter order. Bells and notches run 12 to 96 dB/oct; cuts and band pass start at off.");

    // A dynamic band needs at least one panel. Turning D on with neither panel enables Above.
    dynOnToggle.onClick = [this]
    {
        if (! dynOnToggle.getToggleState())
            return;
        const bool belowOn = processor.apvts.getRawParameterValue (Params::bandDynBelowOnID (currentBand))->load() > 0.5f;
        const bool aboveOn = processor.apvts.getRawParameterValue (Params::bandDynAboveOnID (currentBand))->load() > 0.5f;
        if (! belowOn && ! aboveOn)
        {
            processor.beginEdit ("Dynamic Above");
            processor.setRawFromUI (Params::bandDynAboveOnID (currentBand), 1.0f);
            processor.endEdit();
        }
        if (onDynamicsChanged != nullptr)
            onDynamicsChanged();
    };

    prevBandButton.setTooltip ("Previous band");
    nextBandButton.setTooltip ("Next band");
    prevBandButton.onClick = [this] { if (onStepBand != nullptr) onStepBand (-1); };
    nextBandButton.onClick = [this] { if (onStepBand != nullptr) onStepBand (1); };
    for (auto* button : { &prevBandButton, &nextBandButton })
    {
        button->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff242b2f));
        button->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff1a1f23));
        button->setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    }

    panKnob.setTooltip ("Stereo: left and right balance. Mid Side: how much of the band reaches the mid and side.");
    phaseKnob.setTooltip ("Free phase (kernel modes): 0 linear, +1 minimum phase, -1 inverse phase for this band");

    bandPanel.addAndMakeVisible (prevBandButton);
    bandPanel.addAndMakeVisible (nextBandButton);
    bandPanel.addAndMakeVisible (activeToggle);
    bandPanel.addAndMakeVisible (soloToggle);
    bandPanel.addAndMakeVisible (dynOnToggle);
    bandPanel.addAndMakeVisible (typeCycle);
    bandPanel.addAndMakeVisible (stereoCycle);
    addAndMakeVisible (bandPanel);

    freqKnob.setTooltip ("Frequency. Double-click to reset, drag the readout or type a value");
    gainKnob.setTooltip ("Gain of the band in dB");
    qKnob.setTooltip ("Q: how narrow the band is");
    knobPanel.addItem (freqKnob, 84);
    knobPanel.addItem (gainKnob, 84);
    knobPanel.addItem (qKnob, 84);
    knobPanel.addItem (slopeKnob, 84);
    knobPanel.addItem (panKnob, 84);
    knobPanel.addItem (phaseKnob, 84);
    addAndMakeVisible (knobPanel);

    // Retain splitPanel as child component for parameter attachments and tests
    splitPanel.addAndMakeVisible (splitToggle);
    splitPanel.addAndMakeVisible (splitTransKnob);
    splitPanel.addAndMakeVisible (splitTailKnob);
    addChildComponent (splitPanel);

    setSelectedBand (0);
    processorToUse.apvts.addParameterListener (Params::bandStereoID (currentBand), this);
    processorToUse.apvts.addParameterListener (Params::bandActiveID (currentBand), this);
    processorToUse.apvts.addParameterListener (Params::bandDynOnID (currentBand), this);
    processorToUse.apvts.addParameterListener (Params::bandTypeID (currentBand), this);
    processorToUse.apvts.addParameterListener (Params::bandDynBelowOnID (currentBand), this);
    processorToUse.apvts.addParameterListener (Params::bandDynAboveOnID (currentBand), this);
    processorToUse.apvts.addParameterListener (Params::bandSplitOnID (currentBand), this);
}

BandControlPanel::~BandControlPanel()
{
    cancelPendingUpdate();
    processor.apvts.removeParameterListener (Params::bandStereoID (currentBand), this);
    processor.apvts.removeParameterListener (Params::bandActiveID (currentBand), this);
    processor.apvts.removeParameterListener (Params::bandDynOnID (currentBand), this);
    processor.apvts.removeParameterListener (Params::bandTypeID (currentBand), this);
    processor.apvts.removeParameterListener (Params::bandDynBelowOnID (currentBand), this);
    processor.apvts.removeParameterListener (Params::bandDynAboveOnID (currentBand), this);
    processor.apvts.removeParameterListener (Params::bandSplitOnID (currentBand), this);
}

void BandControlPanel::setSelectedBand (int bandIndex)
{
    if (currentBand == bandIndex)
    {
        rebuildAttachments();
        return;
    }

    processor.apvts.removeParameterListener (Params::bandStereoID (currentBand), this);
    processor.apvts.removeParameterListener (Params::bandActiveID (currentBand), this);
    processor.apvts.removeParameterListener (Params::bandDynOnID (currentBand), this);
    processor.apvts.removeParameterListener (Params::bandTypeID (currentBand), this);
    processor.apvts.removeParameterListener (Params::bandDynBelowOnID (currentBand), this);
    processor.apvts.removeParameterListener (Params::bandDynAboveOnID (currentBand), this);
    processor.apvts.removeParameterListener (Params::bandSplitOnID (currentBand), this);
    currentBand = bandIndex;
    processor.apvts.addParameterListener (Params::bandStereoID (currentBand), this);
    processor.apvts.addParameterListener (Params::bandActiveID (currentBand), this);
    processor.apvts.addParameterListener (Params::bandDynOnID (currentBand), this);
    processor.apvts.addParameterListener (Params::bandTypeID (currentBand), this);
    processor.apvts.addParameterListener (Params::bandDynBelowOnID (currentBand), this);
    processor.apvts.addParameterListener (Params::bandDynAboveOnID (currentBand), this);
    processor.apvts.addParameterListener (Params::bandSplitOnID (currentBand), this);

    rebuildAttachments();
}

void BandControlPanel::parameterChanged (const juce::String&, float)
{
    // Host automation may invoke this callback on the audio thread.
    triggerAsyncUpdate();
}

void BandControlPanel::handleAsyncUpdate()
{
    refreshBandData();
    refreshActiveState();
}

void BandControlPanel::rebuildAttachments()
{
    freqAttachment.reset(); gainAttachment.reset(); qAttachment.reset(); panAttachment.reset(); slopeAttachment.reset();
    activeAttachment.reset(); soloAttachment.reset(); dynOnAttachment.reset();
    splitAttachment.reset(); splitTransAttachment.reset(); splitTailAttachment.reset(); phaseAttachment.reset();

    auto& s = processor.apvts;
    freqAttachment = std::make_unique<SliderAttachment> (s, Params::bandFreqID (currentBand), freqKnob);
    freqKnob.bindDefault (s, Params::bandFreqID (currentBand));
    gainAttachment = std::make_unique<SliderAttachment> (s, Params::bandGainID (currentBand), gainKnob);
    gainKnob.bindDefault (s, Params::bandGainID (currentBand));
    qAttachment = std::make_unique<SliderAttachment> (s, Params::bandQID (currentBand), qKnob);
    qKnob.bindDefault (s, Params::bandQID (currentBand));
    panAttachment = std::make_unique<SliderAttachment> (s, Params::bandPanID (currentBand), panKnob);
    panKnob.bindDefault (s, Params::bandPanID (currentBand));
    activeAttachment = std::make_unique<ButtonAttachment> (s, Params::bandActiveID (currentBand), activeToggle);
    soloAttachment = std::make_unique<ButtonAttachment> (s, Params::bandSoloID (currentBand), soloToggle);
    dynOnAttachment = std::make_unique<ButtonAttachment> (s, Params::bandDynOnID (currentBand), dynOnToggle);

    typeCycle.rebind (Params::bandTypeID (currentBand));
    stereoCycle.rebind (Params::bandStereoID (currentBand));
    slopeAttachment = std::make_unique<SliderAttachment> (s, Params::bandSlopeNormID (currentBand), slopeKnob);
    slopeKnob.bindDefault (s, Params::bandSlopeNormID (currentBand));
    splitAttachment = std::make_unique<ButtonAttachment> (s, Params::bandSplitOnID (currentBand), splitToggle);
    splitTransAttachment = std::make_unique<SliderAttachment> (s, Params::bandSplitTransID (currentBand), splitTransKnob);
    splitTransKnob.bindDefault (s, Params::bandSplitTransID (currentBand));
    splitTailAttachment = std::make_unique<SliderAttachment> (s, Params::bandSplitTailID (currentBand), splitTailKnob);
    splitTailKnob.bindDefault (s, Params::bandSplitTailID (currentBand));
    phaseAttachment = std::make_unique<SliderAttachment> (s, Params::bandPhaseID (currentBand), phaseKnob);
    phaseKnob.bindDefault (s, Params::bandPhaseID (currentBand));

    refreshBandData();
    refreshActiveState();
}

void BandControlPanel::refreshBandData()
{
    const int stereoIndex = static_cast<int> (
        processor.apvts.getRawParameterValue (Params::bandStereoID (currentBand))->load());
    auto accent = Theme::accentForStereoMode (stereoIndex);

    bandPanel.setTitleText ("BAND " + juce::String (currentBand + 1));
    bandPanel.setAccent (accent, Params::stereoModeNames[stereoIndex]);
    activeToggle.setAccent (accent);
    soloToggle.setAccent (juce::Colour (0xfff59e0b));
    dynOnToggle.setAccent (juce::Colour (0xff38bdf8));
    typeCycle.repaint();
    stereoCycle.repaint();
    panKnob.setAccent (accent);
    phaseKnob.setAccent (accent);
    freqKnob.setAccent (accent);
    gainKnob.setAccent (accent);
    qKnob.setAccent (accent);
    slopeKnob.setAccent (accent);
    knobPanel.setAccentColour (accent);
    splitToggle.setAccent (accent);
    splitTransKnob.setAccent (accent);
    splitTailKnob.setAccent (accent);
}

void BandControlPanel::refreshActiveState()
{
    const bool bandOn = processor.apvts.getRawParameterValue (Params::bandActiveID (currentBand))->load() >= 0.5f;
    const auto type = (FilterType) juce::jlimit (0, filterTypeCount - 1,
        (int) processor.apvts.getRawParameterValue (Params::bandTypeID (currentBand))->load());

    activeToggle.setEnabled (true);
    soloToggle.setEnabled (bandOn);
    freqKnob.setEnabled (bandOn);
    typeCycle.setEnabled (bandOn);
    stereoCycle.setEnabled (bandOn);
    slopeKnob.setEnabled (bandOn);
    dynOnToggle.setEnabled (bandOn && ResponseModel::supportsDynamics (type));
    const bool splitOn = processor.apvts.getRawParameterValue (Params::bandSplitOnID (currentBand))->load() > 0.5f;
    splitToggle.setEnabled (bandOn);
    splitTransKnob.setEnabled (bandOn && splitOn);
    splitTailKnob.setEnabled (bandOn && splitOn);
    const int processingMode = (int) processor.apvts.getRawParameterValue ("processingMode")->load();
    phaseKnob.setEnabled (bandOn && processingMode != 0); // free phase applies to the kernel modes

    const bool gainMatters = bandOn && ResponseModel::usesGain (type);
    const bool qMatters = bandOn && ResponseModel::usesQ (type);
    const bool slopeMatters = bandOn && ResponseModel::usesSlope (type);
    gainKnob.setEnabled (gainMatters);
    qKnob.setEnabled (qMatters);
    // Pan means something only for L/R balance (Stereo) and mid/side balance (Mid Side).
    const int stereo = (int) processor.apvts.getRawParameterValue (Params::bandStereoID (currentBand))->load();
    const bool panMatters = bandOn && (stereo == (int) StereoMode::Stereo || stereo == (int) StereoMode::MidSide);
    panKnob.setEnabled (panMatters);
    slopeKnob.setEnabled (slopeMatters);
    if (ResponseModel::slopeIsAttenuation (type))
        slopeKnob.setValueType (KnobValueType::Attenuation, "ATTEN");
    else
        slopeKnob.setValueType (KnobValueType::Slope, "SLOPE");

    if (onDynamicsChanged != nullptr)
        onDynamicsChanged();
}

void BandControlPanel::resized()
{
    auto bounds = getLocalBounds().reduced (6, 4);
    const int spacer = 8;
    const int cardHeight = bounds.getHeight();

    // Left card: band switches, filter type and stereo placement
    const int bandW = juce::jlimit (200, 236, juce::roundToInt (bounds.getWidth() * 0.27f));
    bandPanel.setBounds (bounds.removeFromLeft (bandW).withHeight (cardHeight));
    bounds.removeFromLeft (spacer);

    // Right card: one row of knobs (takes the remaining width)
    knobPanel.setBounds (bounds.withHeight (cardHeight));

    // Hidden splitPanel for test isolation & attachments
    splitPanel.setBounds (0, 0, 0, 0);

    // Layout inside bandPanel:
    auto bArea = bandPanel.getLocalBounds().reduced (8, 0);
    const int topH = 26;
    bArea.removeFromTop (topH + 3);
    bArea.removeFromBottom (6);

    // Row 1: < > On Solo D (h = 22)
    auto r1 = bArea.removeFromTop (22);
    prevBandButton.setBounds (r1.removeFromLeft (22));
    r1.removeFromLeft (3);
    nextBandButton.setBounds (r1.removeFromLeft (22));
    r1.removeFromLeft (8);
    activeToggle.setBounds (r1.removeFromLeft (42));
    r1.removeFromLeft (4);
    soloToggle.setBounds (r1.removeFromLeft (42));
    r1.removeFromLeft (4);
    dynOnToggle.setBounds (r1.removeFromLeft (28));

    bArea.removeFromTop (5);

    // Rows 2 and 3: TYPE, then STEREO, each across the card so the choice is never cut short
    typeCycle.setBounds (bArea.removeFromTop (26));
    bArea.removeFromTop (6);
    stereoCycle.setBounds (bArea.removeFromTop (26));
}
