#pragma once
#include <JuceHeader.h>
#include <functional>
#include "AppSettings.h"
#include "Parameters.h"

/** Settings overlay. Each control writes to AppSettings immediately.
    The left column contains behaviour, knob, and display settings. Favourite filter types
    appear at the top of the band menu. */
class SettingsPanel : public juce::Component
{
public:
    SettingsPanel()
    {
        title.setText ("Settings", juce::dontSendNotification);
        title.setFont (juce::FontOptions (18.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible (title);

        const auto& settings = AppSettings::get();

        // Behaviours.
        addToggle ("Reverse mouse wheel direction on knobs and the graph", settings.reverseWheel(),
                   [] (bool on) { AppSettings::get().setReverseWheel (on); });
        addCombo ("Fine-tune modifier", modifierNames(), flagsToIndex (settings.fineTuneModifier()),
                  [] (int index) { AppSettings::get().setFineTuneModifier (modifierFlags (index)); });
        addCombo ("Wheel invert modifier (reverses the wheel)", modifierNames(), flagsToIndex (settings.wheelInvertModifier()),
                  [] (int index) { AppSettings::get().setWheelInvertModifier (modifierFlags (index)); });
        addCombo ("Click knob to reset with", modifierNames(), flagsToIndex (settings.knobResetModifier()),
                  [] (int index) { AppSettings::get().setKnobResetModifier (modifierFlags (index)); });
        addCombo ("Double-click on a knob", { "Reset to default", "Type a value" }, settings.knobDoubleClickAction(),
                  [] (int index) { AppSettings::get().setKnobDoubleClickAction (index); });
        addToggle ("Ctrl/Command + right-click on a band deletes it", settings.ctrlRightClickDeletesBand(),
                   [] (bool on) { AppSettings::get().setCtrlRightClickDeletesBand (on); });
        addToggle ("Double right-click on empty graph enables spectrum grab", settings.doubleRightClickGrab(),
                   [] (bool on) { AppSettings::get().setDoubleRightClickGrab (on); });
        addCombo ("New band stereo mode", { "Always last created", "By selection", "Always default" },
                  settings.newBandStereoRule(), [] (int index) { AppSettings::get().setNewBandStereoRule (index); });
        addCombo ("Default stereo mode", stereoItems(), settings.defaultStereoMode(),
                  [] (int index) { AppSettings::get().setDefaultStereoMode (index); });

        // Knobs. Speeds are percentages of the default.
        addSlider ("Knob drag speed", 25.0, 400.0, "%", settings.knobDragSpeed(),
                   [] (double value) { AppSettings::get().setKnobDragSpeed ((float) value); });
        addSlider ("Knob wheel speed", 25.0, 400.0, "%", settings.knobWheelSpeed(),
                   [] (double value) { AppSettings::get().setKnobWheelSpeed ((float) value); });
        const char* controlNames[] = { "Frequency", "Gain", "Q" };
        for (int control = 0; control < 3; ++control)
        {
            addSlider (juce::String (controlNames[control]) + " drag speed", 10.0, 400.0, "%",
                       settings.knobSpeedFactor (control, false) * 100.0f,
                       [control] (double value) { AppSettings::get().setKnobSpeedFactor (control, false, (float) value / 100.0f); });
            addSlider (juce::String (controlNames[control]) + " wheel speed", 10.0, 400.0, "%",
                       settings.knobSpeedFactor (control, true) * 100.0f,
                       [control] (double value) { AppSettings::get().setKnobSpeedFactor (control, true, (float) value / 100.0f); });
        }
        addToggle ("Gain knob limited to the displayed dB range", settings.gainKnobFollowsDisplayRange(),
                   [] (bool on) { AppSettings::get().setGainKnobFollowsDisplayRange (on); });
        addToggle ("Gain knob speed follows the displayed dB range", settings.gainKnobSpeedFollowsDisplayRange(),
                   [] (bool on) { AppSettings::get().setGainKnobSpeedFollowsDisplayRange (on); });

        // Display info.
        addToggle ("Show band numbers on the graph", settings.showBandNumbers(),
                   [] (bool on) { AppSettings::get().setShowBandNumbers (on); });
        addToggle ("Show cursor axes with frequency and gain", settings.showCursorAxes(),
                   [] (bool on) { AppSettings::get().setShowCursorAxes (on); });
        addSlider ("Spectrum centre frequency (analyzer tilt pivot)", 20.0, 20000.0, " Hz", settings.spectrumCentreHz(),
                   [] (double value) { AppSettings::get().setSpectrumCentreHz ((float) value); });
        addToggle ("Show a remove button on the floating band controller", settings.showFloatingRemoveButton(),
                   [] (bool on) { AppSettings::get().setShowFloatingRemoveButton (on); });
        addToggle ("Show readout for the selected band", settings.showSelectedBandInfo(),
                   [] (bool on) { AppSettings::get().setShowSelectedBandInfo (on); });

        // Interface.
        addCombo ("Floating band controller", { "All", "Monitor only", "Off" }, (int) settings.floatingController(),
                  [] (int index) { AppSettings::get().setFloatingController ((AppSettings::FloatingController) index); });
        addCombo ("Analyzer refresh", { "15 Hz", "30 Hz", "60 Hz" },
                  settings.analyzerRefreshHz() == 15 ? 0 : (settings.analyzerRefreshHz() == 60 ? 2 : 1),
                  [this] (int index)
                  {
                      const int hz = index == 0 ? 15 : (index == 2 ? 60 : 30);
                      AppSettings::get().setAnalyzerRefreshHz (hz);
                      if (onRefreshChanged != nullptr)
                          onRefreshChanged (hz);
                  });
        addCombo ("GUI scale", { "80%", "90%", "100%", "110%", "125%", "150%" },
                  [] (int percent) -> int
                  {
                      static const int steps[] = { 80, 90, 100, 110, 125, 150 };
                      for (int i = 0; i < 6; ++i)
                          if (steps[i] == percent)
                              return i;
                      return 2;
                  }(settings.guiScalePercent()),
                  [this] (int index)
                  {
                      static const int steps[] = { 80, 90, 100, 110, 125, 150 };
                      AppSettings::get().setGuiScalePercent (steps[juce::jlimit (0, 5, index)]);
                      if (onGuiScaleChanged != nullptr)
                          onGuiScaleChanged (steps[juce::jlimit (0, 5, index)]);
                  });
        addButton ("Save current colours as a theme file...", [this]
        {
            themeChooser = std::make_unique<juce::FileChooser> ("Save a theme", juce::File(), "*.opeq_theme");
            themeChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                                       [this] (const juce::FileChooser& chooser)
                                       {
                                           auto file = chooser.getResult();
                                           if (file != juce::File() && onThemeFileSaveRequested != nullptr)
                                               onThemeFileSaveRequested (file.withFileExtension ("opeq_theme"));
                                       });
        });
        addButton ("Load theme file (.opeq_theme)...", [this]
        {
            themeChooser = std::make_unique<juce::FileChooser> ("Choose a theme", juce::File(), "*.opeq_theme;*.json;*");
            themeChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                       [this] (const juce::FileChooser& chooser)
                                       {
                                           const auto file = chooser.getResult();
                                           if (file.existsAsFile() && onThemeFileChosen != nullptr)
                                               onThemeFileChosen (file);
                                       });
        });
        addCombo ("Theme", { "Graphite", "Light", "High contrast" }, settings.themeIndex(),
                  [this] (int index)
                  {
                      AppSettings::get().setThemeIndex (index);
                      if (onThemeChanged != nullptr)
                          onThemeChanged (index);
                  });

        settingsView.setViewedComponent (&settingsList, false);
        settingsView.setScrollBarsShown (true, false);
        addAndMakeVisible (settingsView);

        favouritesLabel.setText ("Favourite types (shown first in the band menu)", juce::dontSendNotification);
        addAndMakeVisible (favouritesLabel);
        const auto favourites = settings.favouriteTypes();
        for (int type = 0; type < filterTypeCount; ++type)
        {
            auto* toggle = new juce::ToggleButton (Params::filterTypeNames[type]);
            toggle->setToggleState (favourites.contains (type), juce::dontSendNotification);
            toggle->onClick = [type, toggle]
            {
                auto list = AppSettings::get().favouriteTypes();
                if (toggle->getToggleState())
                {
                    if (! list.contains (type))
                        list.add (type);
                }
                else
                {
                    list.removeAllInstancesOf (type);
                }
                AppSettings::get().setFavouriteTypes (list);
            };
            favouriteToggles.add (toggle);
            favouriteList.addAndMakeVisible (toggle);
        }
        favouriteViewport.setViewedComponent (&favouriteList, false);
        favouriteViewport.setScrollBarsShown (true, false);
        addAndMakeVisible (favouriteViewport);

        restoreSizeButton.setButtonText ("Restore Size");
        restoreSizeButton.onClick = [this] { if (onRestoreSize != nullptr) onRestoreSize(); };
        addAndMakeVisible (restoreSizeButton);

        resetButton.setButtonText ("Reset Settings");
        resetButton.onClick = [this]
        {
            AppSettings::get().resetToDefaults();
            if (onReset != nullptr)
                onReset();
        };
        addAndMakeVisible (resetButton);

        closeButton.setButtonText ("Close");
        closeButton.onClick = [this] { if (onClose != nullptr) onClose(); };
        addAndMakeVisible (closeButton);
    }

    std::function<void()> onClose;
    std::function<void()> onReset;
    std::function<void (int)> onRefreshChanged;
    std::function<void (int)> onThemeChanged;
    std::function<void (int)> onGuiScaleChanged;
    std::function<void (const juce::File&)> onThemeFileChosen;
    std::function<void (const juce::File&)> onThemeFileSaveRequested;
    std::function<void()> onRestoreSize;

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xe6080b0e));
        g.setColour (juce::Colour (0xff222b30));
        g.drawRoundedRectangle (getLocalBounds().reduced (12).toFloat(), 8.0f, 1.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (28);
        title.setBounds (area.removeFromTop (30));
        area.removeFromTop (8);

        auto left = area.removeFromLeft (juce::jmin (560, area.getWidth() / 2 + 60));
        auto buttons = left.removeFromBottom (36);
        resetButton.setBounds (buttons.removeFromLeft (140).reduced (0, 4));
        buttons.removeFromLeft (10);
        restoreSizeButton.setBounds (buttons.removeFromLeft (120).reduced (0, 4));
        buttons.removeFromLeft (10);
        closeButton.setBounds (buttons.removeFromLeft (100).reduced (0, 4));

        settingsView.setBounds (left.reduced (0, 4));
        const int rowHeight = 28;
        const int listWidth = juce::jmax (100, settingsView.getMaximumVisibleWidth());
        settingsList.setSize (listWidth, rows.size() * rowHeight);
        for (int i = 0; i < rows.size(); ++i)
            rows[i]->setBounds (0, i * rowHeight, listWidth, rowHeight);

        auto right = area;
        favouritesLabel.setBounds (right.removeFromTop (22));
        favouriteViewport.setBounds (right.reduced (0, 4));

        // Favourite toggles in three columns inside the viewport.
        const int columns = 3;
        const int rowsNeeded = (filterTypeCount + columns - 1) / columns;
        const int itemHeight = 24;
        const int columnWidth = juce::jmax (180, favouriteViewport.getWidth() / columns - 8);
        for (int i = 0; i < favouriteToggles.size(); ++i)
            favouriteToggles[i]->setBounds ((i / rowsNeeded) * columnWidth, (i % rowsNeeded) * itemHeight,
                                            columnWidth - 8, itemHeight - 2);
        favouriteList.setSize (columns * columnWidth, rowsNeeded * itemHeight);
    }

private:
    /** One line of the settings list: a label and a control, or a full-width toggle. */
    class Row : public juce::Component
    {
    public:
        Row (const juce::String& text, std::unique_ptr<juce::Component> controlToOwn, bool fullWidth)
            : control (std::move (controlToOwn)), wide (fullWidth)
        {
            label.setText (text, juce::dontSendNotification);
            label.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.85f));
            if (! wide)
                addAndMakeVisible (label);
            addAndMakeVisible (*control);
        }

        void resized() override
        {
            auto b = getLocalBounds().reduced (0, 3);
            if (wide)
            {
                control->setBounds (b.withWidth (juce::jmin (b.getWidth(), 420)));
                return;
            }
            label.setBounds (b.removeFromLeft (290));
            b.removeFromLeft (8);
            control->setBounds (b.removeFromLeft (220));
        }

    private:
        juce::Label label;
        std::unique_ptr<juce::Component> control;
        bool wide;
    };

    static juce::StringArray modifierNames() { return { "None", "Shift", "Ctrl", "Alt", "Command" }; }
    static int modifierFlags (int index)
    {
        static const int flags[] = { 0, 1, 2, 4, 8 };
        return flags[juce::jlimit (0, 4, index)];
    }
    static int flagsToIndex (int flags)
    {
        for (int i = 0; i < 5; ++i)
            if (modifierFlags (i) == flags)
                return i;
        return 0;
    }
    static juce::StringArray stereoItems()
    {
        juce::StringArray items;
        for (int i = 0; i < Params::stereoModeNames.size(); ++i)
            items.add (Params::stereoModeNames[i]);
        return items;
    }

    void addToggle (const juce::String& text, bool value, std::function<void (bool)> apply)
    {
        auto toggle = std::make_unique<juce::ToggleButton> (text);
        toggle->setToggleState (value, juce::dontSendNotification);
        toggle->onClick = [button = toggle.get(), apply] { apply (button->getToggleState()); };
        rows.add (new Row (text, std::move (toggle), true));
        settingsList.addAndMakeVisible (*rows.getLast());
    }

    void addCombo (const juce::String& text, const juce::StringArray& items, int selected, std::function<void (int)> apply)
    {
        auto combo = std::make_unique<juce::ComboBox>();
        for (int i = 0; i < items.size(); ++i)
            combo->addItem (items[i], i + 1);
        combo->setSelectedId (juce::jlimit (0, items.size() - 1, selected) + 1, juce::dontSendNotification);
        combo->onChange = [box = combo.get(), apply] { apply (box->getSelectedId() - 1); };
        rows.add (new Row (text, std::move (combo), false));
        settingsList.addAndMakeVisible (*rows.getLast());
    }

    void addButton (const juce::String& text, std::function<void()> onPressed)
    {
        auto button = std::make_unique<juce::TextButton> (text);
        button->onClick = std::move (onPressed);
        rows.add (new Row (text, std::move (button), true));
        settingsList.addAndMakeVisible (*rows.getLast());
    }

    void addSlider (const juce::String& text, double minimum, double maximum, const juce::String& suffix, double value,
                    std::function<void (double)> apply)
    {
        auto slider = std::make_unique<juce::Slider> (juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
        slider->setRange (minimum, maximum, 1.0);
        slider->setTextValueSuffix (suffix);
        slider->setValue (value, juce::dontSendNotification);
        slider->onValueChange = [box = slider.get(), apply] { apply (box->getValue()); };
        rows.add (new Row (text, std::move (slider), false));
        settingsList.addAndMakeVisible (*rows.getLast());
    }

    juce::Label title, favouritesLabel;
    juce::TextButton resetButton, closeButton, restoreSizeButton;
    juce::Viewport settingsView;
    juce::Component settingsList;
    juce::OwnedArray<Row> rows;
    juce::OwnedArray<juce::ToggleButton> favouriteToggles;
    juce::Component favouriteList;
    juce::Viewport favouriteViewport;
    std::unique_ptr<juce::FileChooser> themeChooser;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsPanel)
};
