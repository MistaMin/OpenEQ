#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "UI/SectionPanel.h"
#include "UI/RotaryKnob.h"
#include "UI/CycleButton.h"
#include "UI/SmallToggle.h"
#include "UI/Theme.h"
#include "Parameters.h"
#include "PluginProcessor.h"

/** Real-time split energy balance visualizer meter. */
class SplitMeter : public juce::Component, private juce::Timer
{
public:
    explicit SplitMeter (OpenEQAudioProcessor& p) : processor (p)
    {
        startTimerHz (30);
    }
    ~SplitMeter() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat().reduced (2.0f, 2.0f);
        const float w = b.getWidth();

        // Background plate
        g.setColour (juce::Colour (0xff14191d));
        g.fillRoundedRectangle (b, 4.0f);
        g.setColour (juce::Colour (0xff283138));
        g.drawRoundedRectangle (b, 4.0f, 1.0f);

        // Center content vertically if card is tall
        const float contentH = 58.0f;
        const float startY = b.getY() + juce::jmax (2.0f, (b.getHeight() - contentH) * 0.5f);

        // Header Title
        g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
        g.setColour (juce::Colour (0xff8faab3));
        g.drawText ("SPLIT BALANCE", (int) b.getX(), (int) startY, (int) w, 11, juce::Justification::centred);

        const float transVal = juce::jlimit (0.0f, 1.0f, smoothedTrans);
        const float tonalVal = juce::jlimit (0.0f, 1.0f, smoothedTonal);
        const float ratioVal = juce::jlimit (0.0f, 1.0f, smoothedRatio);

        const float barH = 9.0f;
        const float labelW = 34.0f;
        const float barX = b.getX() + labelW + 6.0f;
        const float barW = juce::jmax (10.0f, w - labelW - 14.0f);

        // 1. Transient bar
        const float yT = startY + 14.0f;
        g.setColour (Theme::splitTransCol);
        g.setFont (juce::FontOptions (8.0f, juce::Font::bold));
        g.drawText ("TRANS", (int) b.getX() + 6, (int) yT - 1, (int) labelW, (int) barH, juce::Justification::centredLeft);

        g.setColour (juce::Colour (0xff1c2328));
        g.fillRoundedRectangle (barX, yT, barW, barH, 2.0f);

        const float fillWT = barW * transVal;
        if (fillWT > 1.0f)
        {
            g.setColour (Theme::splitTransCol);
            g.fillRoundedRectangle (barX, yT, fillWT, barH, 2.0f);
        }

        // 2. Tonal bar
        const float yS = startY + 27.0f;
        g.setColour (Theme::splitTonalCol);
        g.setFont (juce::FontOptions (8.0f, juce::Font::bold));
        g.drawText ("TONAL", (int) b.getX() + 6, (int) yS - 1, (int) labelW, (int) barH, juce::Justification::centredLeft);

        g.setColour (juce::Colour (0xff1c2328));
        g.fillRoundedRectangle (barX, yS, barW, barH, 2.0f);

        const float fillWS = barW * tonalVal;
        if (fillWS > 1.0f)
        {
            g.setColour (Theme::splitTonalCol);
            g.fillRoundedRectangle (barX, yS, fillWS, barH, 2.0f);
        }

        // 3. Dynamic Ratio Balance Track
        const float yBal = startY + 41.0f;
        const float trackH = 6.0f;

        g.setColour (juce::Colour (0xff8faab3));
        g.setFont (juce::FontOptions (7.5f, juce::Font::bold));
        g.drawText ("RATIO", (int) b.getX() + 6, (int) yBal - 2, (int) labelW, (int) trackH + 4, juce::Justification::centredLeft);

        // Gradient track: transient on the left, tonal on the right
        juce::ColourGradient grad (Theme::splitTransCol.withAlpha (0.45f), barX, yBal,
                                   Theme::splitTonalCol.withAlpha (0.45f), barX + barW, yBal, false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (barX, yBal, barW, trackH, 3.0f);

        // Center split notch
        g.setColour (juce::Colours::white.withAlpha (0.4f));
        g.drawVerticalLine ((int) (barX + barW * 0.5f), yBal - 1.0f, yBal + trackH + 1.0f);

        // Ratio Needle Pip: ratio 1.0 = 100% transient, 0.0 = 100% tonal
        const float needleX = barX + barW * (1.0f - ratioVal);
        g.setColour (juce::Colours::white);
        g.fillEllipse (needleX - 3.5f, yBal + (trackH * 0.5f) - 3.5f, 7.0f, 7.0f);
        g.setColour (juce::Colour (0xff121619));
        g.drawEllipse (needleX - 3.5f, yBal + (trackH * 0.5f) - 3.5f, 7.0f, 7.0f, 1.2f);
    }

private:
    void timerCallback() override
    {
        const float t = processor.getSplitTransientMeter();
        const float s = processor.getSplitTonalMeter();
        const float r = processor.getSplitRatioMeter();
        smoothedTrans += (t - smoothedTrans) * 0.35f;
        smoothedTonal += (s - smoothedTonal) * 0.35f;
        smoothedRatio += (r - smoothedRatio) * 0.25f;
        repaint();
    }

    OpenEQAudioProcessor& processor;
    float smoothedTrans = 0.0f;
    float smoothedTonal = 0.0f;
    float smoothedRatio = 0.5f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SplitMeter)
};

/** Comprehensive transient/tonal inspector tab.
    Left card holds the shared transient/tonal split engine (Source, Separation, Decay, Smoothing, stream solo keys, Live Balance).
    Right card holds the selected band's Split mode toggle, Transient gain, Tonal gain, and Free Phase.
    The transient stream is green and the tonal stream is blue, here and in the graph. */
class SplitGlobalPanel : public juce::Component,
                         private juce::AudioProcessorValueTreeState::Listener,
                         private juce::AsyncUpdater
{
public:
    /** Source preset above two solo keys, one per stream. The keys are exclusive, and both off means no solo. */
    struct SourceSoloStack : public juce::Component
    {
        SourceSoloStack (CycleButton& s, SmallToggle& t, SmallToggle& n) : src (s), soloTrans (t), soloTonal (n)
        {
            addAndMakeVisible (src);
            addAndMakeVisible (soloTrans);
            addAndMakeVisible (soloTonal);
        }
        void resized() override
        {
            auto b = getLocalBounds().reduced (2, 2);
            const int h = 24;
            const int totalH = h * 2 + 10;
            const int startY = juce::jmax (0, (b.getHeight() - totalH) / 2);
            b.removeFromTop (startY);
            src.setBounds (b.removeFromTop (h));
            b.removeFromTop (10);
            auto row = b.removeFromTop (h);
            soloTrans.setBounds (row.removeFromLeft (row.getWidth() / 2));
            soloTonal.setBounds (row);
        }
        CycleButton& src;
        SmallToggle& soloTrans;
        SmallToggle& soloTonal;
    };

    explicit SplitGlobalPanel (OpenEQAudioProcessor& processorToUse)
        : processor (processorToUse),
          splitMeter (processorToUse),
          sourceCycle (processorToUse.apvts, "splitSource", "SOURCE", false),
          separationKnob ("SEPARATION", KnobValueType::Attenuation, Theme::stereoCol),
          decayKnob ("DECAY", KnobValueType::Attenuation, Theme::stereoCol),
          smoothKnob ("SMOOTH", KnobValueType::TimeMs, Theme::stereoCol),
          soloTransToggle ("SOLO T", Theme::splitTransCol),
          soloTonalToggle ("SOLO S", Theme::splitTonalCol),
          sourceSoloStack (sourceCycle, soloTransToggle, soloTonalToggle),
          splitToggle ("SPLIT ON", Theme::stereoCol),
          splitTransKnob ("TRANSIENT", KnobValueType::Gain, Theme::splitTransCol),
          splitTailKnob ("TONAL", KnobValueType::Gain, Theme::splitTonalCol),
          phaseKnob ("PHASE", KnobValueType::Attenuation, Theme::stereoCol)
    {
        globalPanel.setAccent (Theme::stereoCol, "Transient / Tonal Engine");
        globalPanel.addItem (sourceSoloStack, 120);
        globalPanel.addItem (separationKnob, 84);
        globalPanel.addItem (decayKnob, 84);
        globalPanel.addItem (smoothKnob, 84);
        globalPanel.addItem (splitMeter, 108);

        sourceCycle.setButtonTooltip ("Split source preset: scales the separation decision for the material");
        separationKnob.setTooltip ("Transient separation: how much of the signal counts as transient");
        decayKnob.setTooltip ("Transient decay: how long a region stays transient");
        smoothKnob.setTooltip ("Split smoothing: slows the transient/tonal decision");
        soloTransToggle.setTooltip ("Solo the transient stream to tune the split");
        soloTonalToggle.setTooltip ("Solo the tonal stream to tune the split");
        soloTransToggle.setClickingTogglesState (false);
        soloTonalToggle.setClickingTogglesState (false);
        soloTransToggle.onClick = [this] { toggleSolo (1); };
        soloTonalToggle.onClick = [this] { toggleSolo (2); };
        processor.apvts.addParameterListener ("splitSolo", this);
        refreshSoloKeys();

        auto& state = processorToUse.apvts;
        separationAttachment = std::make_unique<SliderAttachment> (state, "splitSeparation", separationKnob);
        separationKnob.bindDefault (state, "splitSeparation");
        decayAttachment = std::make_unique<SliderAttachment> (state, "splitDecay", decayKnob);
        decayKnob.bindDefault (state, "splitDecay");
        smoothAttachment = std::make_unique<SliderAttachment> (state, "splitSmoothingMs", smoothKnob);
        smoothKnob.bindDefault (state, "splitSmoothingMs");

        addAndMakeVisible (globalPanel);

        bandPanel.addItem (splitToggle, 62);
        bandPanel.addItem (splitTransKnob, 84);
        bandPanel.addItem (splitTailKnob, 84);
        bandPanel.addItem (phaseKnob, 84);

        splitToggle.setTooltip ("Split this band: separate gains for the transient stream and the tonal stream. While any band is split the plugin adds about 37 ms of latency");
        splitTransKnob.setTooltip ("Transient gain for this band, added to the band gain (-30 dB to +30 dB). The green node in the graph");
        splitTailKnob.setTooltip ("Tonal gain for this band, added to the band gain (-30 dB to +30 dB). The blue node in the graph");
        phaseKnob.setTooltip ("Free phase (kernel modes): 0 linear, +1 minimum phase, -1 inverse phase for this band");

        addAndMakeVisible (bandPanel);

        setSelectedBand (0);
    }

    ~SplitGlobalPanel() override
    {
        processor.apvts.removeParameterListener ("splitSolo", this);
    }

    void setSelectedBand (int bandIndex)
    {
        currentBand = bandIndex;
        bandPanel.setTitleText ("BAND " + juce::String (currentBand + 1) + " SPLIT");

        const int stereoIndex = static_cast<int> (
            processor.apvts.getRawParameterValue (Params::bandStereoID (currentBand))->load());
        auto accent = Theme::accentForStereoMode (stereoIndex);
        bandPanel.setAccent (accent, Params::stereoModeNames[stereoIndex]);

        rebuildBandAttachments();
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (4, 2);
        const int gap = 8;
        const int globalW = juce::roundToInt ((float) (b.getWidth() - gap) * 0.588f);
        globalPanel.setBounds (b.removeFromLeft (globalW));
        b.removeFromLeft (gap);
        bandPanel.setBounds (b);
    }

private:
    int soloMode() const
    {
        return juce::jlimit (0, 2, (int) processor.apvts.getRawParameterValue ("splitSolo")->load());
    }

    void toggleSolo (int which)
    {
        processor.beginEdit (which == 1 ? "Solo Transient" : "Solo Tonal");
        processor.setRawFromUI ("splitSolo", soloMode() == which ? 0.0f : (float) which);
        processor.endEdit();
        refreshSoloKeys();
    }

    void refreshSoloKeys()
    {
        const int mode = soloMode();
        soloTransToggle.setToggleState (mode == 1, juce::dontSendNotification);
        soloTonalToggle.setToggleState (mode == 2, juce::dontSendNotification);
    }

    // The solo can also change from the graph keys, host automation, or undo.
    void parameterChanged (const juce::String&, float) override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override { refreshSoloKeys(); }

    void rebuildBandAttachments()
    {
        auto& s = processor.apvts;
        splitAttachment = std::make_unique<ButtonAttachment> (s, Params::bandSplitOnID (currentBand), splitToggle);
        splitTransAttachment = std::make_unique<SliderAttachment> (s, Params::bandSplitTransID (currentBand), splitTransKnob);
        splitTransKnob.bindDefault (s, Params::bandSplitTransID (currentBand));
        splitTailAttachment = std::make_unique<SliderAttachment> (s, Params::bandSplitTailID (currentBand), splitTailKnob);
        splitTailKnob.bindDefault (s, Params::bandSplitTailID (currentBand));
        phaseAttachment = std::make_unique<SliderAttachment> (s, Params::bandPhaseID (currentBand), phaseKnob);
        phaseKnob.bindDefault (s, Params::bandPhaseID (currentBand));
    }

    OpenEQAudioProcessor& processor;
    int currentBand = 0;

    SectionPanel globalPanel { "GLOBAL SPLIT ENGINE" };
    SplitMeter splitMeter;
    CycleButton sourceCycle;
    RotaryKnob separationKnob;
    RotaryKnob decayKnob;
    RotaryKnob smoothKnob;
    SmallToggle soloTransToggle;
    SmallToggle soloTonalToggle;
    SourceSoloStack sourceSoloStack;

    SectionPanel bandPanel { "BAND 1 SPLIT" };
    SmallToggle splitToggle;
    RotaryKnob splitTransKnob;
    RotaryKnob splitTailKnob;
    RotaryKnob phaseKnob;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SliderAttachment> separationAttachment, decayAttachment, smoothAttachment;
    std::unique_ptr<ButtonAttachment> splitAttachment;
    std::unique_ptr<SliderAttachment> splitTransAttachment, splitTailAttachment, phaseAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SplitGlobalPanel)
};
