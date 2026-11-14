#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "UI/RotaryKnob.h"
#include "UI/SmallToggle.h"
#include "UI/Theme.h"
#include "Parameters.h"
#include "PluginProcessor.h"

/** Resonance suppressor controls: one row of knobs and four switches. FOCUS belongs to the selected band.
    DEPTH above zero switches the engine on and adds latency, so the card says so. */
class SpectralPanel : public juce::Component, private juce::Timer
{
public:
    explicit SpectralPanel (OpenEQAudioProcessor& processorToUse)
        : processor (processorToUse),
          depth ("DEPTH", KnobValueType::Attenuation, accent()),
          sharpness ("SHARP", KnobValueType::Attenuation, accent()),
          selectivity ("SELECT", KnobValueType::Attenuation, accent()),
          attack ("ATTACK", KnobValueType::Attenuation, accent()),
          release ("RELEASE", KnobValueType::Attenuation, accent()),
          floor ("FLOOR", KnobValueType::WholeDb, accent()),
          frequency ("FREQ", KnobValueType::Frequency, accent()),
          width ("WIDTH", KnobValueType::Octaves, accent()),
          link ("LINK", KnobValueType::Attenuation, accent()),
          mix ("MIX", KnobValueType::Attenuation, accent()),
          focus ("FOCUS B1", KnobValueType::Attenuation, accent()),
          midSideToggle ("M/S", accent()),
          sidechainToggle ("SC", accent()),
          hardToggle ("HARD", accent()),
          deltaToggle ("DELTA", accent())
    {
        depth.setTooltip ("How much of each resonance is removed. 0% is off; above that the engine runs and adds latency.");
        sharpness.setTooltip ("How narrow the cuts are. Low makes broad, gentle cuts; high makes narrow, deep ones.");
        selectivity.setTooltip ("How far a peak has to stand out before it is touched. Higher leaves small peaks alone.");
        attack.setTooltip ("How fast a cut sets in, 1 ms to 500 ms");
        release.setTooltip ("How fast a cut lets go, 20 ms to 2 s");
        floor.setTooltip ("Frequencies quieter than this are left alone");
        frequency.setTooltip ("Centre of the processed region");
        width.setTooltip ("Width of the processed region in octaves. 10 covers the whole spectrum.");
        link.setTooltip ("Stereo link. 0% treats left and right separately; 100% gives both the deeper of the two cuts.");
        mix.setTooltip ("Dry/wet mix of the suppressor");
        hardToggle.setTooltip ("Hard mode: no ceiling on the cut. Off, cuts level out at a ceiling set by DEPTH.");
        deltaToggle.setTooltip ("Delta: hear only what is being removed");
        midSideToggle.setTooltip ("Detect and cut in mid and side instead of left and right");
        sidechainToggle.setTooltip ("Follow the external sidechain: cut here what stands out there, to make room for it");
        focus.setTooltip ("Focus for the selected band: above 0 the suppressor works harder around this band's frequency (width follows its Q), below 0 it works lighter. The band must be on; its gain can stay at 0.");
        focus.setValueText ([] (float v) { return (v > 0.005f ? "+" : "") + juce::String (juce::roundToInt (v * 100.0f)) + "%"; });
        addAndMakeVisible (focus);
        midSideAttachment = std::make_unique<ButtonAttachment> (processorToUse.apvts, "spectralMS", midSideToggle);
        sidechainAttachment = std::make_unique<ButtonAttachment> (processorToUse.apvts, "spectralSidechain", sidechainToggle);
        addAndMakeVisible (midSideToggle);
        addAndMakeVisible (sidechainToggle);
        setSelectedBand (0);

        attack.setValueText ([] (float v) { return timeText (0.001f * std::pow (500.0f, v)); });
        release.setValueText ([] (float v) { return timeText (0.02f * std::pow (100.0f, v)); });
        width.setValueText ([] (float v) { return v >= 9.95f ? juce::String ("ALL") : juce::String (v, 1) + " oct"; });

        auto& state = processorToUse.apvts;
        const std::pair<RotaryKnob*, const char*> bindings[] = {
            { &depth, "spectralAmount" }, { &sharpness, "spectralSharp" }, { &selectivity, "spectralSelect" },
            { &attack, "spectralAttack" }, { &release, "spectralRelease" }, { &floor, "spectralThresh" },
            { &frequency, "spectralFreq" }, { &width, "spectralOct" }, { &link, "spectralLink" }, { &mix, "spectralMix" } };
        for (const auto& [knob, id] : bindings)
        {
            attachments.push_back (std::make_unique<SliderAttachment> (state, id, *knob));
            knob->bindDefault (state, id);
            addAndMakeVisible (*knob);
        }
        hardAttachment = std::make_unique<ButtonAttachment> (state, "spectralHard", hardToggle);
        deltaAttachment = std::make_unique<ButtonAttachment> (state, "spectralDelta", deltaToggle);
        addAndMakeVisible (hardToggle);
        addAndMakeVisible (deltaToggle);

        startTimerHz (8);
    }

    ~SpectralPanel() override { stopTimer(); }

    /** The FOCUS knob follows the selected band. */
    void setSelectedBand (int bandIndex)
    {
        const auto id = Params::bandSuppressID (bandIndex);
        focusAttachment.reset();
        focusAttachment = std::make_unique<SliderAttachment> (processor.apvts, id, focus);
        focus.bindDefault (processor.apvts, id);
        focus.setValueType (KnobValueType::Attenuation, "FOCUS B" + juce::String (bandIndex + 1));
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (10, 6);
        auto header = area.removeFromTop (26);
        deltaToggle.setBounds (header.removeFromRight (62).reduced (0, 1));
        header.removeFromRight (6);
        hardToggle.setBounds (header.removeFromRight (62).reduced (0, 1));
        header.removeFromRight (6);
        sidechainToggle.setBounds (header.removeFromRight (48).reduced (0, 1));
        header.removeFromRight (6);
        midSideToggle.setBounds (header.removeFromRight (52).reduced (0, 1));

        RotaryKnob* const knobs[] = { &depth, &sharpness, &selectivity, &attack, &release, &floor, &frequency, &width, &link, &mix, &focus };
        const int cellW = area.getWidth() / (int) std::size (knobs);
        for (auto* knob : knobs)
        {
            auto cell = area.removeFromLeft (cellW);
            knob->setBounds (cell.withSizeKeepingCentre (juce::jmin (80, cell.getWidth() - 2), juce::jmin (118, cell.getHeight())));
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour (Theme::panelBottom);
        g.fillRoundedRectangle (bounds, 5.0f);
        g.setColour (Theme::textBright);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText ("RESONANCE SUPPRESSOR", 12, 4, 200, 18, juce::Justification::centredLeft);

        const bool active = processor.apvts.getRawParameterValue ("spectralAmount")->load() > 1.0e-4f;
        g.setColour (active ? accent() : Theme::textStd);
        g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
        g.drawText (active ? "ON / ADDS LATENCY" : "OFF / RAISE DEPTH TO SWITCH ON",
                    214, 5, getWidth() - 214 - 270, 16, juce::Justification::centredLeft);

        g.setColour (accent().withAlpha (0.2f));
        g.drawHorizontalLine (27, 12.0f, (float) getWidth() - 12.0f);
    }

private:
    static juce::Colour accent() { return juce::Colour (0xffb58cff); }
    static juce::String timeText (float seconds)
    {
        return seconds >= 1.0f ? juce::String (seconds, 1) + " s" : juce::String (juce::roundToInt (seconds * 1000.0f)) + " ms";
    }

    void timerCallback() override
    {
        const bool active = processor.apvts.getRawParameterValue ("spectralAmount")->load() > 1.0e-4f;
        if (active != wasActive)
        {
            wasActive = active;
            repaint (0, 0, getWidth(), 28);
        }
    }

    OpenEQAudioProcessor& processor;
    RotaryKnob depth, sharpness, selectivity, attack, release, floor, frequency, width, link, mix, focus;
    SmallToggle hardToggle, deltaToggle, midSideToggle, sidechainToggle;
    bool wasActive = false;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::vector<std::unique_ptr<SliderAttachment>> attachments;
    std::unique_ptr<ButtonAttachment> hardAttachment, deltaAttachment, midSideAttachment, sidechainAttachment;
    std::unique_ptr<SliderAttachment> focusAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectralPanel)
};
