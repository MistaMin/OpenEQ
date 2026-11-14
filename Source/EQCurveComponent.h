#pragma once
#include <JuceHeader.h>
#include <array>
#include "PluginProcessor.h"

/** The main interactive EQ graph: draws the combined frequency response curve
    and a draggable circular handle per active band, colour-coded by stereo
    placement (Stereo/Mid/Side/Left/Right), graph editing. */
class EQCurveComponent : public juce::Component, private juce::Timer
{
public:
    explicit EQCurveComponent (OpenEQAudioProcessor& processorToUse);
    ~EQCurveComponent() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;
    bool keyPressed (const juce::KeyPress& key) override;

    std::function<void (int)> onBandSelected;
    /** While armed, a drag on empty graph space previews a bell at the nearest frozen peak and commits it on mouse-up. */
    void setGrabArmed (bool armed);
    bool isGrabArmed() const noexcept { return grabArmed; }
    std::function<float (float requestedHz)> peakFrequencyForGrab;
    /** Called when a double right-click on empty space asks to toggle grab. The editor keeps its Grab button and analyzer freeze in step. */
    std::function<void (bool armed)> onGrabRequested;
    int getSelectedBand() const { return selectedBand; }
    void setSelectedBandExternally (int index) { selectedBand = index; repaint(); }
    /** Opens the typed value entry for a band, as a double-click on its node does. */
    void editBandValues (int index) { openValueEntry (index); }

    /** Display range of the EQ curve: 12, 18, or 30 dB either side of zero. Global, and not saved. */
    static void setDisplayRangeDb (float rangeDb) noexcept;
    static float getDisplayRangeDb() noexcept;

    static float frequencyToX (float freqHz, float width);
    static float xToFrequency (float x, float width);
    static float dbToY (float db, float height);
    static float yToDb (float y, float height);

private:
    void timerCallback() override;
    void refreshBandView();
    bool isBandAudible (int index) const;
    bool isBandVisible (int index) const;
    juce::Point<float> getHandlePosition (int bandIndex) const;
    juce::Point<float> getTransientHandlePosition (int bandIndex) const;
    juce::Point<float> getTailHandlePosition (int bandIndex) const;
    /** True when the band shows its own transient and tonal gain nodes. */
    bool hasSplitHandles (int bandIndex) const;
    float splitStreamY (int bandIndex, bool transient) const;
    bool isSplitStreamCollapsed (int bandIndex, bool transient) const;
    bool anySplitBandAudible() const;
    int currentSplitSolo() const;
    juce::Rectangle<float> splitLegendRect (int which) const;
    bool clickSplitLegend (juce::Point<float> pos);
    juce::Rectangle<float> floatIconRect (int which, juce::Point<float> handle) const;
    enum class DragKind
    {
        None,
        Center,
        TransientSatellite,
        TailSatellite
    };
    struct HandleHit
    {
        int band = -1;
        DragKind kind = DragKind::None;
    };
    HandleHit findHandleHitAt (juce::Point<float> pos) const;
    int findHandleAt (juce::Point<float> pos) const;
    int findFreeBandSlot() const;
    void selectBand (int index);
    void openValueEntry (int index);
    void commitValueEntry();
    void removeSelected();
    void duplicateSelected();
    void copySelected();
    void pasteClipboard();
    static juce::Colour colourForBand (int index);
public:
    /** The colour a band is drawn in, as the theme file sees it. */
    static juce::Colour bandColour (int index) { return colourForBand (index); }
private:
    static const char* badgeForMode (StereoMode mode);

    OpenEQAudioProcessor& processor;
    std::array<BandParameterState, OpenEQAudioProcessor::numBands> bandView {};

    // Display motion. Nodes and curves ease toward their targets when a value changes from outside the graph
    // (inspector knobs, presets, A/B, automation). A drag on the graph itself is drawn without easing.
    struct NodeMotion
    {
        float logFrequency = 0.0f, gainDb = 0.0f, transDb = 0.0f, tailDb = 0.0f;
        float appear = 0.0f; // 0 just added, 1 fully shown
        float hover = 0.0f;  // 0 idle, 1 under the pointer or dragged
        bool primed = false;
    };
    static constexpr int curvePoints = 400;
    void stepNodeMotion();
    bool graphDragActive() const noexcept { return draggingBand >= 0 || floatDragKind != 0; }
    std::array<NodeMotion, OpenEQAudioProcessor::numBands> nodeMotion {};
    std::array<std::array<float, curvePoints + 1>, 4> shownCurveDb {}; // total/left, right, transient, tonal
    bool curvePrimed = false, rightCurveShown = false, splitCurvesShown = false;
    double lastNodeStepMs = 0.0, lastCurveStepMs = 0.0;
    bool nodeMotionActive = false, curveMotionActive = false;
    int timerRateHz = 30;
    bool anySoloView = false;
    int selectedBand = 0;
    int draggingBand = -1;
    DragKind dragKind = DragKind::None;
    int hoverSatelliteBand = -1;
    DragKind hoverSatelliteKind = DragKind::None;
    float dragSplitGain = 0.0f;
    float originSplitTrans = 0.0f;
    float originSplitTail = 0.0f;
    bool dragGesture = false;
    bool grabArmed = false;
    // Floating band controller state: 0 none, 1 Q, 2 slope, 3 monitor.
    int hoverBand = -1;
    int floatDragKind = 0;
    int floatBand = -1;
    float floatDragStartY = 0.0f;
    float floatStartValue = 0.0f;
    bool grabPreview = false;
    // Double right-click detection for grab, and the pointer position for the cursor axes display.
    juce::uint32 lastRightClickMs = 0;
    juce::Point<float> lastRightClickPos;
    juce::Point<float> cursorPos;
    bool cursorInside = false;
    float previewHz = 1000.0f;
    float previewDb = 0.0f;
    float dragFrequency = 1000.0f;
    float dragGain = 0.0f;
    std::array<float, OpenEQAudioProcessor::numBands> originFrequency {};
    std::array<float, OpenEQAudioProcessor::numBands> originGain {};
    std::vector<int> selection;
    juce::TextEditor valueEditor;
    int valueBand = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EQCurveComponent)
};
