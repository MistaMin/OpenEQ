#pragma once
#include <JuceHeader.h>
#include <memory>

/** Global OpenEQ settings, shared by every instance and kept in the user's Application Support folder.
    reference keeps its settings global in the same way. Each write saves at once. */
class AppSettings
{
public:
    static AppSettings& get()
    {
        static AppSettings instance;
        return instance;
    }

    enum class FloatingController { All = 0, MonitorOnly, Off };

    /** The graph's displayed dB range. The graph sets it and the knobs read it, so it is not saved here. */
    static std::atomic<float>& displayRangeDb() noexcept
    {
        static std::atomic<float> range { 30.0f };
        return range;
    }

    bool reverseWheel() const { return store().getBoolValue ("reverseWheel", false); }
    void setReverseWheel (bool value) { set ("reverseWheel", value); }

    /** Knob drag speed as a percentage of the default. 100 is the default. */
    float knobDragSpeed() const { return juce::jlimit (25.0f, 400.0f, (float) store().getDoubleValue ("knobDragSpeed", 100.0)); }
    void setKnobDragSpeed (float percent) { set ("knobDragSpeed", juce::jlimit (25.0f, 400.0f, percent)); }

    /** Modifier flags use JUCE's ModifierKeys bits (Shift 1, Ctrl 2, Alt 4, Command 8), which is also how reference stores them. */

    /** GUI scale in percent (reference global scale). Allowed steps: 80, 90, 100, 110, 125, 150. */
    int guiScalePercent() const
    {
        const int value = store().getIntValue ("guiScalePercent", 100);
        static const int steps[] = { 80, 90, 100, 110, 125, 150 };
        int best = 100;
        for (int step : steps)
            if (std::abs (step - value) < std::abs (best - value))
                best = step;
        return best;
    }
    void setGuiScalePercent (int percent) { set ("guiScalePercent", percent); }

    /** Holding this modifier while using the wheel reverses its direction (wheel-invert key).
        Default: Command (8). */
    int wheelInvertModifier() const { return store().getIntValue ("wheelInvertModifier", 8); }
    void setWheelInvertModifier (int flags) { set ("wheelInvertModifier", flags); }

    /** Frequency the analyzer tilt turns around (spectrum centre frequency, default 547.72 Hz). */
    float spectrumCentreHz() const { return juce::jlimit (20.0f, 20000.0f, (float) store().getDoubleValue ("spectrumCentreHz", 547.7225952148438)); }
    void setSpectrumCentreHz (float hz) { set ("spectrumCentreHz", juce::jlimit (20.0f, 20000.0f, hz)); }

    /** Fine-tuning modifier for knobs and drags. Default: Shift. */
    int fineTuneModifier() const { return store().getIntValue ("fineTuneModifier", 1); }
    void setFineTuneModifier (int flags) { set ("fineTuneModifier", flags); }

    /** Click a knob with this modifier to reset it. Default: Command (8). */
    int knobResetModifier() const { return store().getIntValue ("knobResetModifier", 8); }
    void setKnobResetModifier (int flags) { set ("knobResetModifier", flags); }

    /** Double-click on a knob: 0 resets to the default, 1 opens a typed value entry. Default: reset. */
    int knobDoubleClickAction() const { return juce::jlimit (0, 1, store().getIntValue ("knobDoubleClickAction", 0)); }
    void setKnobDoubleClickAction (int action) { set ("knobDoubleClickAction", juce::jlimit (0, 1, action)); }

    /** Mouse wheel speed on knobs, as a percentage. 100 is the default. */
    float knobWheelSpeed() const { return juce::jlimit (25.0f, 400.0f, (float) store().getDoubleValue ("knobWheelSpeed", 100.0)); }
    void setKnobWheelSpeed (float percent) { set ("knobWheelSpeed", juce::jlimit (25.0f, 400.0f, percent)); }

    /** Per-control speed multiplier for frequency, gain and Q. 1.0 is the default.
        Index 0 frequency, 1 gain, 2 Q. */
    float knobSpeedFactor (int control, bool wheel) const
    {
        const juce::String key = juce::String ("knobSpeed.") + juce::String (control) + (wheel ? ".wheel" : ".drag");
        return juce::jlimit (0.1f, 4.0f, (float) store().getDoubleValue (key, 1.0));
    }
    void setKnobSpeedFactor (int control, bool wheel, float factor)
    {
        const juce::String key = juce::String ("knobSpeed.") + juce::String (control) + (wheel ? ".wheel" : ".drag");
        set (key, juce::jlimit (0.1f, 4.0f, factor));
    }

    /** Gain knob limited to the displayed dB range. */
    bool gainKnobFollowsDisplayRange() const { return store().getBoolValue ("gainKnobFollowsDisplayRange", false); }
    void setGainKnobFollowsDisplayRange (bool value) { set ("gainKnobFollowsDisplayRange", value); }

    /** Gain knob speed scales with the displayed dB range. */
    bool gainKnobSpeedFollowsDisplayRange() const { return store().getBoolValue ("gainKnobSpeedFollowsDisplayRange", false); }
    void setGainKnobSpeedFollowsDisplayRange (bool value) { set ("gainKnobSpeedFollowsDisplayRange", value); }

    /** Ctrl/Command + right-click on a band removes it. */
    bool ctrlRightClickDeletesBand() const { return store().getBoolValue ("ctrlRightClickDeletesBand", true); }
    void setCtrlRightClickDeletesBand (bool value) { set ("ctrlRightClickDeletesBand", value); }

    /** Double right-click on empty graph space toggles spectrum grab. When off, a single right-click does not grab. */
    bool doubleRightClickGrab() const { return store().getBoolValue ("doubleRightClickGrab", true); }
    void setDoubleRightClickGrab (bool value) { set ("doubleRightClickGrab", value); }

    /** Default stereo mode for new bands: index into StereoMode. 0 Stereo. */
    int defaultStereoMode() const { return juce::jlimit (0, 5, store().getIntValue ("defaultStereoMode", 0)); }
    void setDefaultStereoMode (int mode) { set ("defaultStereoMode", juce::jlimit (0, 5, mode)); }

    /** Stereo rule for new bands: 0 always last created, 1 by selection, 2 always default. */
    int newBandStereoRule() const { return juce::jlimit (0, 2, store().getIntValue ("newBandStereoRule", 0)); }
    void setNewBandStereoRule (int rule) { set ("newBandStereoRule", juce::jlimit (0, 2, rule)); }

    /** Stereo mode of the last band created from the graph, used by rule 0. */
    int lastCreatedStereoMode() const { return juce::jlimit (0, 5, store().getIntValue ("lastCreatedStereoMode", 0)); }
    void setLastCreatedStereoMode (int mode) { set ("lastCreatedStereoMode", juce::jlimit (0, 5, mode)); }

    /** Display info around the graph. Defaults follow the application settings. */
    bool showBandNumbers() const { return store().getBoolValue ("showBandNumbers", false); }
    void setShowBandNumbers (bool value) { set ("showBandNumbers", value); }

    /** Remove button on the floating band controller (floating remove button). Off by default. */
    bool showFloatingRemoveButton() const { return store().getBoolValue ("showFloatingRemoveButton", false); }
    void setShowFloatingRemoveButton (bool value) { set ("showFloatingRemoveButton", value); }

    bool showCursorAxes() const { return store().getBoolValue ("showCursorAxes", false); }
    void setShowCursorAxes (bool value) { set ("showCursorAxes", value); }

    bool showSelectedBandInfo() const { return store().getBoolValue ("showSelectedBandInfo", true); }
    void setShowSelectedBandInfo (bool value) { set ("showSelectedBandInfo", value); }

    FloatingController floatingController() const
    {
        return (FloatingController) juce::jlimit (0, 2, store().getIntValue ("floatingController", 0));
    }
    void setFloatingController (FloatingController mode) { set ("floatingController", (int) mode); }

    /** Colour theme: 0 Graphite, 1 Light, 2 High contrast. */
    int themeIndex() const { return juce::jlimit (0, 2, store().getIntValue ("theme", 0)); }
    void setThemeIndex (int index) { set ("theme", juce::jlimit (0, 2, index)); }

    /** Path of an .opeq_theme file applied over the built-in theme. Empty when none is used. */
    juce::String customThemeFile() const { return store().getValue ("customThemeFile", ""); }
    void setCustomThemeFile (const juce::String& path) { set ("customThemeFile", path); }

    /** Analyzer redraw rate. */
    int analyzerRefreshHz() const { return juce::jlimit (15, 60, store().getIntValue ("analyzerRefreshHz", 30)); }
    void setAnalyzerRefreshHz (int hz) { set ("analyzerRefreshHz", juce::jlimit (15, 60, hz)); }

    /** Favourite filter types, as type indices. They appear at the top of the band menu. */
    juce::Array<int> favouriteTypes() const
    {
        juce::Array<int> types;
        juce::StringArray parts;
        parts.addTokens (store().getValue ("favouriteTypes", ""), ",", "");
        for (const auto& part : parts)
            if (part.isNotEmpty())
                types.add (part.getIntValue());
        return types;
    }

    void setFavouriteTypes (const juce::Array<int>& types)
    {
        juce::StringArray parts;
        for (int type : types)
            parts.add (juce::String (type));
        set ("favouriteTypes", parts.joinIntoString (","));
    }

    void resetToDefaults()
    {
        if (properties != nullptr)
        {
            properties->clear();
            properties->saveIfNeeded();
        }
    }

private:
    AppSettings()
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "OpenEQ";
        options.filenameSuffix = ".xml";
        options.osxLibrarySubFolder = "Application Support";
        options.folderName = "OpenEQ";
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        options.commonToAllUsers = false;
        properties = std::make_unique<juce::PropertiesFile> (options.getDefaultFile(), options);
    }

    const juce::PropertiesFile& store() const
    {
        return *properties;
    }

    template <typename T>
    void set (const juce::String& key, T value)
    {
        if (properties == nullptr)
            return;
        properties->setValue (key, value);
        properties->saveIfNeeded();
    }

    std::unique_ptr<juce::PropertiesFile> properties;
};
