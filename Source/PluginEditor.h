#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "SpectrumAnalyzer.h"
#include "EQCurveComponent.h"
#include "BandControlPanel.h"
#include "BandListStrip.h"
#include "DynamicsPanel.h"
#include "SplitGlobalPanel.h"
#include "SpectralPanel.h"
#include "UI/NoteKeyboard.h"
#include "UI/InputColumn.h"
#include "UI/OutputColumn.h"
#include "UI/FloatingInspector.h"
#include "SettingsPanel.h"
#include "UI/RotaryKnob.h"
#include "UI/CycleButton.h"
#include "UI/BypassButton.h"
#include "UI/SmallToggle.h"
#include "UI/MonitorButton.h"
#include "UI/SectionPanel.h"
#include "UI/AppLookAndFeel.h"

/** Holds the whole interface. The editor scales this stage for the GUI scale setting,
    so the layout works in unscaled coordinates and the window size follows the scale. */
class EditorStage : public juce::Component
{
public:
    std::function<void (juce::Graphics&)> onPaint;
    void paint (juce::Graphics& g) override
    {
        if (onPaint != nullptr)
            onPaint (g);
    }
};

class OpenEQAudioProcessorEditor : public juce::AudioProcessorEditor,
                                   private juce::Timer
{
public:
    explicit OpenEQAudioProcessorEditor (OpenEQAudioProcessor&);
    ~OpenEQAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
    bool keyPressed (const juce::KeyPress& key) override;

private:
    void paintStage (juce::Graphics&);
    juce::Rectangle<int> logicalBounds() const;
    void applyGuiScale (float newScale);

    // The stage must be declared first, so the controls it parents are destroyed before it.
    EditorStage stage;
    float guiScale = 1.0f;

    OpenEQAudioProcessor& processorRef;
    AppLookAndFeel appLookAndFeel;

    juce::Label titleLabel;
    juce::Label subtitleLabel;
    CycleButton processingModeCycle, linearPhaseQualityCycle, mixCrossoverCycle;
    MonitorButton monitorButton;
    BypassButton bypassButton;
    SmallToggle polarityToggle;
    juce::TextButton undoButton { "Undo" }, redoButton { "Redo" };
    juce::TextButton slotAButton { "A" }, slotBButton { "B" }, presetButton { "Preset" }, grabButton { "Grab" };
    juce::TextButton instButton { "INSTANCE" };
    juce::TextButton spectrumSourceButton { "POST" };
    juce::TextButton rangeButton { "30 dB" };
    juce::TextButton fftButton { "FFT" };
    juce::TextButton keysButton { "Keys" };
    juce::TextButton ultraButton { "117-BIT" };
    juce::TextButton oversampleButton { "2x OVERSAMPLE" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> ultraAttachment;
    juce::TextButton fullButton { "Full" };
    juce::TextButton settingsButton { "Settings" };
    std::unique_ptr<SettingsPanel> settingsPanel;
    std::unique_ptr<juce::FileChooser> presetChooser;
    bool fullScreen = false;
    int restoreWidth = 1500, restoreHeight = 860;
    juce::TextButton listButton { "List" };
    juce::Label noticeLabel;
    bool bandListVisible = false;

    InputColumn inputColumn;
    OutputColumn outputColumn;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<ButtonAttachment> polarityAttachment;

    SpectrumAnalyzer spectrumAnalyzer;
    EQCurveComponent eqCurve;
    BandControlPanel bandControls;
    BandListStrip bandList;
    DynamicsPanel dynamicsPanel;
    SplitGlobalPanel splitGlobalPanel;
    SpectralPanel spectralPanel;
    bool dynamicsVisible = false;
    NoteKeyboardStrip keyboardStrip;
    bool keysVisible = true;
    void refreshDynamicsStrip();
    juce::Rectangle<int> graphBounds;

    enum class InspectorTab
    {
        EqFilter = 0,
        Dynamics,
        Split,
        Spectral,
        BandList
    };
    InspectorTab activeTab = InspectorTab::EqFilter;
    void setInspectorTab (InspectorTab tab);

    FloatingInspector floatingInspector;
    bool inspectorMovedByUser = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OpenEQAudioProcessorEditor)
};

using GodEQAudioProcessorEditor = OpenEQAudioProcessorEditor;
