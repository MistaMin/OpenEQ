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
#include "InstanceMenu.h"

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

class OpenEQAudioProcessorEditor;

/** The whole interface for one processor: every panel, attachment and listener. The editor window owns one
    view and replaces it to control another instance, so nothing in a view outlives its processor's link to it. */
class OpenEQEditorView : public juce::Component,
                         private juce::Timer
{
public:
    OpenEQEditorView (OpenEQAudioProcessor& controlled, OpenEQAudioProcessorEditor& window);
    ~OpenEQEditorView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
    bool handleKey (const juce::KeyPress& key);

    OpenEQAudioProcessor& getProcessor() noexcept { return processorRef; }
    /** The instance badge text: the controlled instance's display name, prefixed while it is another instance. */
    juce::String getBadgeText() const { return instButton.getButtonText(); }

private:
    void paintStage (juce::Graphics&);
    juce::Rectangle<int> logicalBounds() const;
    void applyGuiScale (float newScale);
    void refreshInstanceBadge();

    // The stage must be declared first, so the controls it parents are destroyed before it.
    EditorStage stage;
    float guiScale = 1.0f;

    OpenEQAudioProcessor& processorRef;
    OpenEQAudioProcessorEditor& window;
    AppLookAndFeel appLookAndFeel;
    juce::uint32 badgeGeneration = 0;
    bool badgeValid = false;
    bool hasBadgeColour = false;
    juce::Colour badgeColour;
    juce::Rectangle<int> badgeSwatch;

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
    /** refreshDynamicsStrip on the next message-loop turn, for callers inside a control callback. */
    void refreshDynamicsStripSoon();
    bool dynamicsRefreshPending = false;
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OpenEQEditorView)
};

/** The plug-in window. It belongs to one processor (the host's), and shows a view that controls either that
    processor or another OpenEQ instance in the same process, chosen in the instance menu. Switching happens
    on a later message-loop turn, never inside the click or menu callback that asked for it. When the other
    instance closes, the window returns to its own processor before that instance's state is destroyed. */
class OpenEQAudioProcessorEditor : public juce::AudioProcessorEditor,
                                   private OpenEQAudioProcessor::ClosingListener
{
public:
    explicit OpenEQAudioProcessorEditor (OpenEQAudioProcessor&);
    ~OpenEQAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;

    /** Asks the window to control the instance with this registry id; it switches on the next message-loop
        turn. An id that is no longer registered is ignored. Pass the own instance's id to switch back. */
    void showInstance (int registryId);
    /** Registry id of the instance this window edits now. */
    int getControlledInstanceId() const noexcept { return controlledId; }
    OpenEQAudioProcessor& getOwnProcessor() noexcept { return ownProcessor; }
    OpenEQAudioProcessor& getControlledProcessor() noexcept { return *controlled; }
    bool isControllingOtherInstance() const noexcept { return controlled != &ownProcessor; }
    /** Counts view rebuilds. Diagnostics and tests. */
    int getViewRebuilds() const noexcept { return viewRebuilds; }
    OpenEQEditorView* getView() noexcept { return view.get(); }
    /** Applies an instance menu choice (switch is deferred; link and unlink act on this window's own instance;
        rename opens a name entry for the chosen instance). */
    void applyInstanceChoice (const InstanceMenu::Choice& choice);
    /** Sets the typed name of the instance with this registry id, if it is still open. Message thread. */
    bool renameInstance (int registryId, const juce::String& name);

    // Window state that survives a view rebuild.
    bool fullScreen = false;
    int restoreWidth = 1500, restoreHeight = 860;

private:
    void instanceClosing (OpenEQAudioProcessor& closing) override;
    bool switchToId (int registryId);
    void switchTo (OpenEQAudioProcessor& target);

    OpenEQAudioProcessor& ownProcessor;
    OpenEQAudioProcessor* controlled = nullptr;
    int controlledId = 0;
    int viewRebuilds = 0;
    std::unique_ptr<OpenEQEditorView> view;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OpenEQAudioProcessorEditor)
};

using GodEQAudioProcessorEditor = OpenEQAudioProcessorEditor;
