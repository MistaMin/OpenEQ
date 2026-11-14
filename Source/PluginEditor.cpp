#include "PluginEditor.h"
#include "UI/ThemeFile.h"
#include "PresetImport.h"
#include "Parameters.h"
#include "Version.h"
#include "UI/Theme.h"
#include <GoodLookinUI.h>

/** Widgets that copy a theme colour when they are built are re-read after a live theme change. */
static void refreshThemeRecursive (juce::Component& component)
{
    if (auto* slider = dynamic_cast<CompactSlider*> (&component))
        slider->refreshTheme();
    else if (auto* knob = dynamic_cast<RotaryKnob*> (&component))
        knob->refreshTheme();
    else if (auto* cycle = dynamic_cast<CycleButton*> (&component))
        cycle->refreshTheme();

    for (auto* child : component.getChildren())
        if (child != nullptr)
            refreshThemeRecursive (*child);
}

OpenEQAudioProcessorEditor::OpenEQAudioProcessorEditor (OpenEQAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processorRef (p),
      processingModeCycle (p.apvts, "processingMode", "MODE"),
      linearPhaseQualityCycle (p.apvts, "linearPhaseQuality", "QUALITY"),
      mixCrossoverCycle (p.apvts, "mixCrossover", "XOVER"),
      bypassButton (p.apvts, "bypass", Theme::faceAccent),
      polarityToggle (juce::String::fromUTF8 ("\xc3\x98 INVERT"), Theme::outputCol),
      inputColumn (p),
      outputColumn (p),
      spectrumAnalyzer (p),
      eqCurve (p),
      bandControls (p),
      bandList (p),
      dynamicsPanel (p),
      splitGlobalPanel (p),
      spectralPanel (p)
{
    setLookAndFeel (&appLookAndFeel);
    setOpaque (true); // paint() fills the whole window with the chassis colour

    guiScale = (float) AppSettings::get().guiScalePercent() / 100.0f;
    addAndMakeVisible (stage);
    stage.onPaint = [this] (juce::Graphics& g) { paintStage (g); };

    titleLabel.setText ("OpenEQ", juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    stage.addAndMakeVisible (titleLabel);

    subtitleLabel.setText (OPENEQ_VERSION_STRING, juce::dontSendNotification);
    subtitleLabel.setFont (juce::FontOptions (8.5f, juce::Font::bold));
    subtitleLabel.setJustificationType (juce::Justification::centredLeft);
    subtitleLabel.setColour (juce::Label::textColourId, juce::Colour (0xff8a949b));
    stage.addAndMakeVisible (subtitleLabel);

    stage.addAndMakeVisible (processingModeCycle);
    mixCrossoverCycle.setButtonTooltip ("Mix mode: the frequency where minimum phase hands over to linear phase");
    stage.addAndMakeVisible (mixCrossoverCycle);
    stage.addAndMakeVisible (linearPhaseQualityCycle);
    stage.addAndMakeVisible (monitorButton);
    stage.addAndMakeVisible (bypassButton);
    bypassButton.setLabel ("BYPASS");
    bypassButton.setTooltip ("Bypass the whole plugin");
    stage.addAndMakeVisible (polarityToggle);
    polarityToggle.setTooltip ("Invert the wet output polarity. Dry bypass is unchanged.");
    polarityAttachment = std::make_unique<ButtonAttachment> (p.apvts, "polarity", polarityToggle);

    monitorButton.onModeChanged = [this] (int mode)
    {
        spectrumAnalyzer.setMonitorMode (mode);
        processorRef.setMonitorMode (mode);
    };

    rangeButton.setTooltip ("Graph range: 12, 18, or 30 dB. Global, and not saved.");
    rangeButton.onClick = [this]
    {
        const float current = EQCurveComponent::getDisplayRangeDb();
        const float next = current < 13.0f ? 18.0f : (current < 19.0f ? 30.0f : 12.0f);
        EQCurveComponent::setDisplayRangeDb (next);
        rangeButton.setButtonText (juce::String ((int) next) + " dB");
        eqCurve.repaint();
    };
    rangeButton.setButtonText (juce::String ((int) EQCurveComponent::getDisplayRangeDb()) + " dB");

    fftButton.setTooltip ("Analyzer resolution, speed, and range. Global, and not saved.");
    fftButton.onClick = [this]
    {
        const auto current = SpectrumAnalyzer::getDisplaySettings();
        juce::PopupMenu resolution, speed, range;
        static const char* resolutionNames[] = { "Every bin", "Pairs of bins", "Groups of four", "Groups of eight" };
        for (int i = 0; i < 4; ++i)
            resolution.addItem (10 + i, resolutionNames[i], true, i == current.resolution);
        static const char* speedNames[] = { "Fast", "Medium", "Slow" };
        for (int i = 0; i < 3; ++i)
            speed.addItem (20 + i, speedNames[i], true, i == current.speed);
        for (int i = 0; i < 3; ++i)
        {
            const float value = 60.0f + 30.0f * (float) i;
            range.addItem (30 + i, juce::String ((int) value) + " dB", true, std::abs (current.rangeDb - value) < 0.5f);
        }

        juce::PopupMenu windowMenu, windowTight, renderQualityMenu, renderOverMenu, paddingMenu, digitalPlusMenu;
        static const char* windowNames[] = { "Blackman-Harris", "Kaiser", "Hann", "Hamming", "Blackman", "Rectangular" };
        const int windowShape = (int) processorRef.apvts.getRawParameterValue ("windowShape")->load();
        for (int i = 0; i < 6; ++i)
            windowMenu.addItem (100 + i, windowNames[i], true, i == windowShape);
        static const char* tightNames[] = { "None", "25%", "50%", "75%", "Full" };
        const float windowParameter = processorRef.apvts.getRawParameterValue ("windowParameter")->load();
        for (int i = 0; i < 5; ++i)
            windowTight.addItem (110 + i, tightNames[i], true, std::abs (windowParameter - 0.25f * (float) i) < 0.01f);
        static const char* renderQualityNames[] = { "Realtime", "Low", "Medium", "High", "Maximum" };
        const int renderQuality = (int) processorRef.apvts.getRawParameterValue ("renderQuality")->load();
        for (int i = 0; i < 5; ++i)
            renderQualityMenu.addItem (120 + i, renderQualityNames[i], true, i == renderQuality);
        static const char* renderOverNames[] = { "Realtime", "Off", "2x" };
        const int renderOver = (int) processorRef.apvts.getRawParameterValue ("renderOversample")->load();
        for (int i = 0; i < 3; ++i)
            renderOverMenu.addItem (130 + i, renderOverNames[i], true, i == renderOver);
        static const char* paddingNames[] = { "Short", "Normal", "Full" };
        const int padding = (int) processorRef.apvts.getRawParameterValue ("impulsePadding")->load();
        for (int i = 0; i < 3; ++i)
            paddingMenu.addItem (140 + i, paddingNames[i], true, i == padding);
        static const char* digitalPlusNames[] = { "Off", "1", "2", "3", "4" };
        const int digitalPlus = (int) processorRef.apvts.getRawParameterValue ("digitalPlus")->load();
        for (int i = 0; i < 5; ++i)
            digitalPlusMenu.addItem (150 + i, digitalPlusNames[i], true, i == digitalPlus);

        juce::PopupMenu menu;
        menu.addSubMenu ("Resolution", resolution);
        menu.addSubMenu ("Speed", speed);
        menu.addSubMenu ("Range", range);
        menu.addSeparator();
        menu.addSubMenu ("Window shape (FIR)", windowMenu);
        menu.addSubMenu ("Window tightness", windowTight);
        menu.addSubMenu ("Render quality (offline)", renderQualityMenu);
        menu.addSubMenu ("Render oversampling (offline)", renderOverMenu);
        menu.addSubMenu ("Impulse padding", paddingMenu);
        menu.addSubMenu ("Digital+ steps (IIR phase)", digitalPlusMenu);
        const auto setParameterFromMenu = [this] (const juce::String& id, float rawValue)
        {
            if (auto* parameter = processorRef.apvts.getParameter (id))
                parameter->setValueNotifyingHost (parameter->convertTo0to1 (rawValue));
        };
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (fftButton),
            [this, setParameterFromMenu] (int result)
            {
                if (result <= 0)
                    return;
                auto settings = SpectrumAnalyzer::getDisplaySettings();
                if (result >= 10 && result < 14)
                    settings.resolution = result - 10;
                else if (result >= 20 && result < 23)
                    settings.speed = result - 20;
                else if (result >= 30 && result < 33)
                    settings.rangeDb = 60.0f + 30.0f * (float) (result - 30);
                else if (result >= 100 && result < 106)
                    setParameterFromMenu ("windowShape", (float) (result - 100));
                else if (result >= 110 && result < 115)
                    setParameterFromMenu ("windowParameter", 0.25f * (float) (result - 110));
                else if (result >= 120 && result < 125)
                    setParameterFromMenu ("renderQuality", (float) (result - 120));
                else if (result >= 130 && result < 133)
                    setParameterFromMenu ("renderOversample", (float) (result - 130));
                else if (result >= 140 && result < 143)
                    setParameterFromMenu ("impulsePadding", (float) (result - 140));
                else if (result >= 150 && result < 155)
                    setParameterFromMenu ("digitalPlus", (float) (result - 150));
                SpectrumAnalyzer::setDisplaySettings (settings);
                spectrumAnalyzer.repaint();
            });
    };

    ultraButton.setTooltip ("117-bit ultra-high precision: double-double filter state. Costs more CPU. Saved with the session.");
    ultraButton.setClickingTogglesState (true);
    ultraButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff1d9fc6));
    ultraButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff232b30));
    ultraAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, "ultraPrecision", ultraButton);
    stage.addAndMakeVisible (ultraButton);

    oversampleButton.setTooltip ("2x oversampling: the processing runs at twice the rate. Reports its latency. Costs more CPU.");
    oversampleButton.setClickingTogglesState (true);
    oversampleButton.setToggleState (p.isOversampling2x(), juce::dontSendNotification);
    oversampleButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff1d9fc6));
    oversampleButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff232b30));
    oversampleButton.onClick = [this] { processorRef.setOversampling2x (oversampleButton.getToggleState()); };
    stage.addAndMakeVisible (oversampleButton);

    settingsButton.setTooltip ("Global settings: wheel direction, knob speed, floating controller, refresh rate, favourites.");
    settingsButton.onClick = [this]
    {
        if (settingsPanel == nullptr)
        {
            settingsPanel = std::make_unique<SettingsPanel>();
            settingsPanel->onClose = [this] { settingsPanel->setVisible (false); };
            settingsPanel->onReset = [this]
            {
                spectrumAnalyzer.setRefreshHz (AppSettings::get().analyzerRefreshHz());
                applyGuiScale ((float) AppSettings::get().guiScalePercent() / 100.0f);
                repaint();
            };
            settingsPanel->onRefreshChanged = [this] (int hz) { spectrumAnalyzer.setRefreshHz (hz); };
            settingsPanel->onGuiScaleChanged = [this] (int percent) { applyGuiScale ((float) percent / 100.0f); };
            settingsPanel->onRestoreSize = [this]
            {
                fullScreen = false;
                fullButton.setButtonText ("Full");
                restoreWidth = juce::roundToInt (1500.0f * guiScale);
                restoreHeight = juce::roundToInt (860.0f * guiScale);
                setSize (restoreWidth, restoreHeight);
            };
            settingsPanel->onThemeChanged = [this] (int index)
            {
                // A built-in theme replaces any reference theme file.
                AppSettings::get().setCustomThemeFile ({});
                Theme::applyTheme (index);
                refreshThemeRecursive (*this);
                repaint();
                settingsPanel->repaint();
            };
            settingsPanel->onThemeFileSaveRequested = [] (const juce::File& file)
            {
                ThemeFile::save (file, [] (int band) { return EQCurveComponent::bandColour (band); });
            };
            settingsPanel->onThemeFileChosen = [this] (const juce::File& file)
            {
                if (! ThemeFile::load (file))
                    return;
                AppSettings::get().setCustomThemeFile (file.getFullPathName());
                repaint();
                settingsPanel->repaint();
            };
            stage.addAndMakeVisible (*settingsPanel);
        }
        settingsPanel->setBounds (logicalBounds());
        settingsPanel->setVisible (true);
        settingsPanel->toFront (true);
    };

    keysButton.setTooltip ("Show a piano keyboard. Click a key to set the selected band's frequency to that note.");
    keysButton.onClick = [this]
    {
        keysVisible = ! keysVisible;
        keyboardStrip.setVisible (keysVisible);
        resized();
    };
    keyboardStrip.onNoteChosen = [this] (int note)
    {
        const int band = eqCurve.getSelectedBand();
        processorRef.beginEdit ("Set Frequency from Keyboard");
        processorRef.setRawFromUI (Params::bandFreqID (band), NoteKeyboardStrip::frequencyForNote (note));
        processorRef.endEdit();
    };
    stage.addAndMakeVisible (keyboardStrip);
    keyboardStrip.setVisible (true);

    fullButton.setTooltip ("Fill the display, or return to the previous size. Hosts may not allow plugin windows to resize.");
    fullButton.onClick = [this]
    {
        if (! fullScreen)
        {
            restoreWidth = getWidth();
            restoreHeight = getHeight();
            if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
            {
                const auto area = display->userArea;
                fullScreen = true;
                setSize (area.getWidth(), area.getHeight());
            }
        }
        else
        {
            fullScreen = false;
            setSize (restoreWidth, restoreHeight);
        }
        fullButton.setButtonText (fullScreen ? "Window" : "Full");
    };

    for (auto* button : { &undoButton, &redoButton, &slotAButton, &slotBButton, &presetButton, &instButton, &grabButton, &spectrumSourceButton, &listButton, &rangeButton, &fftButton, &keysButton, &fullButton, &settingsButton })
    {
        button->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff232b30));
        button->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff1d9fc6));
        button->setColour (juce::TextButton::textColourOffId, juce::Colour (0xffe6edf0));
        button->setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        stage.addAndMakeVisible (button);
    }
    undoButton.onClick = [this] { processorRef.undo(); };
    redoButton.onClick = [this] { processorRef.redo(); };
    undoButton.setTooltip ("Undo the last graph edit");
    redoButton.setTooltip ("Redo the last graph edit");
    slotAButton.onClick = [this] { if (processorRef.getAudibleABSlot() != 0) processorRef.switchAB(); };
    slotBButton.onClick = [this] { if (processorRef.getAudibleABSlot() != 1) processorRef.switchAB(); };
    slotAButton.setTooltip ("Hear slot A");
    slotBButton.setTooltip ("Hear slot B");
    instButton.onClick = [this]
    {
        // Cross-instance access: view and control any other OpenEQ instance from this window, and vice versa.
        juce::PopupMenu menu;
        const auto entries = InstanceRegistry::get().snapshot();
        const int ownId = processorRef.getInstanceId();
        menu.addItem (1, "This window (" + processorRef.getInstanceName() + ")", true, processorRef.getRemoteTarget() == nullptr);
        int itemId = 2;
        for (const auto& entry : entries)
        {
            if (entry.id == ownId)
                continue;
            menu.addItem (itemId, entry.name, true, processorRef.getRemoteTarget() == entry.processor);
            ++itemId;
        }
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (instButton),
                             [this, entries] (int result)
        {
            if (result == 1)
            {
                processorRef.setRemoteTarget (nullptr);
                instButton.setButtonText ("INSTANCE");
                return;
            }
            int itemId = 2;
            for (const auto& entry : entries)
            {
                if (entry.id == processorRef.getInstanceId())
                    continue;
                if (result == itemId && InstanceRegistry::get().isRegistered (entry.processor))
                {
                    processorRef.setRemoteTarget ((OpenEQAudioProcessor*) entry.processor);
                    instButton.setButtonText ("INSTANCE " + juce::String (entry.id));
                    return;
                }
                ++itemId;
            }
        });
    };
    instButton.setTooltip ("Access another OpenEQ instance: view and control it from this window (both directions).");

    presetButton.onClick = [this]
    {
        // Factory presets, user presets, and the save, load, and default actions (preset toolbar).
        const auto userFiles = processorRef.listUserPresets();
        juce::PopupMenu menu;
        menu.addItem (1, "Init");
        menu.addItem (2, "Tilt");
        menu.addItem (3, "Presence");
        menu.addSeparator();
        juce::PopupMenu userMenu;
        for (int i = 0; i < userFiles.size(); ++i)
            userMenu.addItem (100 + i, userFiles[i].getFileNameWithoutExtension());
        menu.addSubMenu ("User Presets", userMenu, userFiles.size() > 0);
        menu.addItem (200, "Save Preset...");
        menu.addItem (201, "Load Preset...");
        menu.addItem (204, "Import JSON Preset...");
        menu.addSeparator();
        menu.addItem (202, "Save as Default");
        menu.addItem (203, "Reset to Default");
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetButton),
                            [this, userFiles] (int result)
                            {
                                if (result >= 1 && result <= 3)
                                {
                                    processorRef.loadFactoryPreset (result - 1);
                                }
                                else if (result >= 100 && result < 100 + userFiles.size())
                                {
                                    processorRef.loadPresetFile (userFiles[result - 100]);
                                }
                                else if (result == 200)
                                {
                                    presetChooser = std::make_unique<juce::FileChooser> ("Save OpenEQ preset",
                                        processorRef.userPresetDirectory().getChildFile ("Preset.opeq"), "*.opeq");
                                    presetChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                                                | juce::FileBrowserComponent::canSelectFiles,
                                        [this] (const juce::FileChooser& chooser)
                                        {
                                            const auto file = chooser.getResult();
                                            if (file != juce::File())
                                                processorRef.savePresetFile (file.withFileExtension ("opeq"));
                                        });
                                }
                                else if (result == 204)
                                {
                                    presetChooser = std::make_unique<juce::FileChooser> ("Import a JSON preset",
                                        juce::File(), "*.json;*");
                                    presetChooser->launchAsync (juce::FileBrowserComponent::openMode
                                                                | juce::FileBrowserComponent::canSelectFiles,
                                        [this] (const juce::FileChooser& chooser)
                                        {
                                            const auto file = chooser.getResult();
                                            if (! file.existsAsFile())
                                                return;
                                            const auto imported = PresetImport::importPreset (processorRef, file);
                                            juce::String text = juce::String (imported.bandsImported) + " bands imported from "
                                                                + file.getFileName() + ".";
                                            for (const auto& note : imported.notes)
                                                text << "\n" << note;
                                            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
                                                                                    "Preset Import", text);
                                        });
                                }
                                else if (result == 201)
                                {
                                    presetChooser = std::make_unique<juce::FileChooser> ("Load OpenEQ preset",
                                        processorRef.userPresetDirectory(), "*.opeq;*.godeq");
                                    presetChooser->launchAsync (juce::FileBrowserComponent::openMode
                                                                | juce::FileBrowserComponent::canSelectFiles,
                                        [this] (const juce::FileChooser& chooser)
                                        {
                                            const auto file = chooser.getResult();
                                            if (file.existsAsFile())
                                                processorRef.loadPresetFile (file);
                                        });
                                }
                                else if (result == 202)
                                {
                                    processorRef.saveAsDefault();
                                }
                                else if (result == 203)
                                {
                                    processorRef.loadFactoryPreset (0);
                                }
                            });
    };
    presetButton.setTooltip ("Factory presets. User files live in Application Support/OpenEQ/Presets.");
    grabButton.setClickingTogglesState (true);
    grabButton.setTooltip ("Freeze the spectrum, then drag on empty graph space to place a bell on the nearest peak. The grab is not saved.");
    grabButton.onClick = [this]
    {
        const bool armed = grabButton.getToggleState();
        spectrumAnalyzer.setFrozen (armed);
        eqCurve.setGrabArmed (armed);
    };
    eqCurve.peakFrequencyForGrab = [this] (float hz) { return spectrumAnalyzer.peakNear (hz); };
    eqCurve.onGrabRequested = [this] (bool armed)
    {
        grabButton.setToggleState (armed, juce::dontSendNotification);
        spectrumAnalyzer.setFrozen (armed);
        eqCurve.setGrabArmed (armed);
    };
    spectrumSourceButton.setTooltip ("Analyzer view: post, pre, external sidechain, 3D waterfall, spectrogram, or 3D spectrogram. Not saved.");
    spectrumSourceButton.onClick = [this]
    {
        const int next = (processorRef.getSpectrumSource() + 1) % 6;
        processorRef.setSpectrumSource (next);
        static const char* names[] = { "POST", "PRE", "SIDE", "WATER", "SPECTRO", "SPEC 3D" };
        spectrumSourceButton.setButtonText (names[next]);
    };
    noticeLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    noticeLabel.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    noticeLabel.setJustificationType (juce::Justification::centredLeft);
    stage.addAndMakeVisible (noticeLabel);

    stage.addAndMakeVisible (inputColumn);
    stage.addAndMakeVisible (outputColumn);
    startTimerHz (8);

    stage.addAndMakeVisible (spectrumAnalyzer);
    stage.addAndMakeVisible (eqCurve);

    // Floating Inspector Module (Movable, Collapsible, Bottom-Middle of monitoring window)
    floatingInspector.getBody().addAndMakeVisible (bandControls);
    floatingInspector.getBody().addAndMakeVisible (dynamicsPanel);
    floatingInspector.getBody().addAndMakeVisible (splitGlobalPanel);
    floatingInspector.getBody().addAndMakeVisible (spectralPanel);
    floatingInspector.getBody().addAndMakeVisible (bandList);

    floatingInspector.onFieldSelected = [this] (FloatingInspector::Field f)
    {
        switch (f)
        {
            case FloatingInspector::Field::Eq:    setInspectorTab (InspectorTab::EqFilter); break;
            case FloatingInspector::Field::Dyn:   setInspectorTab (InspectorTab::Dynamics); break;
            case FloatingInspector::Field::Split: setInspectorTab (InspectorTab::Split); break;
            case FloatingInspector::Field::Spec:  setInspectorTab (InspectorTab::Spectral); break;
        }
    };
    floatingInspector.onMoved = [this] { inspectorMovedByUser = true; };
    floatingInspector.onStateChanged = [this] { resized(); };
    stage.addAndMakeVisible (floatingInspector);

    listButton.setTooltip ("Show or hide the band list. Drag cards to reorder, double-click a value to edit it.");
    listButton.onClick = [this]
    {
        if (activeTab == InspectorTab::BandList)
            setInspectorTab (InspectorTab::EqFilter);
        else
            setInspectorTab (InspectorTab::BandList);
    };
    bandList.setVisible (false);
    dynamicsPanel.setVisible (false);
    spectralPanel.setVisible (false);
    bandList.onBandSelected = [this] (int bandIndex)
    {
        eqCurve.setSelectedBandExternally (bandIndex);
        bandControls.setSelectedBand (bandIndex);
        bandList.setSelectedBand (bandIndex);
        refreshDynamicsStrip();
    };
    bandList.onBandEdit = [this] (int bandIndex) { eqCurve.editBandValues (bandIndex); };

    eqCurve.onBandSelected = [this] (int bandIndex)
    {
        bandControls.setSelectedBand (bandIndex);
        bandList.setSelectedBand (bandIndex);
        dynamicsPanel.setSelectedBand (bandIndex);
        splitGlobalPanel.setSelectedBand (bandIndex);
        spectralPanel.setSelectedBand (bandIndex);
        refreshDynamicsStrip();
    };

    bandControls.onDynamicsChanged = [this]
    {
        const int band = eqCurve.getSelectedBand();
        const bool dynamic = processorRef.apvts.getRawParameterValue (Params::bandDynOnID (band))->load() > 0.5f;
        if (dynamic && activeTab != InspectorTab::Dynamics)
            setInspectorTab (InspectorTab::Dynamics);
        refreshDynamicsStrip();
    };
    bandControls.onSplitToggled = [this]
    {
        setInspectorTab (InspectorTab::Split);
    };
    bandControls.onStepBand = [this] (int delta)
    {
        const int next = (eqCurve.getSelectedBand() + delta + OpenEQAudioProcessor::numBands) % OpenEQAudioProcessor::numBands;
        eqCurve.setSelectedBandExternally (next);
        bandControls.setSelectedBand (next);
        bandList.setSelectedBand (next);
        dynamicsPanel.setSelectedBand (next);
        splitGlobalPanel.setSelectedBand (next);
        refreshDynamicsStrip();
    };

    bandControls.setSelectedBand (eqCurve.getSelectedBand());
    dynamicsPanel.setSelectedBand (eqCurve.getSelectedBand());
    splitGlobalPanel.setSelectedBand (eqCurve.getSelectedBand());
    if (const char* t = std::getenv ("OPENEQ_MOCKUP_TAB"))
        setInspectorTab (static_cast<InspectorTab> (juce::jlimit (0, 3, std::atoi (t))));
    else
        setInspectorTab (InspectorTab::EqFilter);

    if (const char* dv = std::getenv ("OPENEQ_MOCKUP_DYN_VIEW"))
    {
        const int viewIdx = std::atoi (dv);
        dynamicsPanel.setDynView (static_cast<DynamicsPanel::DynView> (juce::jlimit (0, 2, viewIdx)));
    }

    if (std::getenv ("OPENEQ_MOCKUP_CLOSED") != nullptr)
        floatingInspector.setOpen (false);

    if (std::getenv ("OPENEQ_MOCKUP_MOVED") != nullptr)
        inspectorMovedByUser = true;

    refreshDynamicsStrip();
    for (auto* btn : { &spectrumSourceButton, &rangeButton, &fftButton, &keysButton, &grabButton, &listButton })
        btn->toFront (false);

    setResizable (true, true);
    setResizeLimits (juce::roundToInt (1100.0f * guiScale), juce::roundToInt (640.0f * guiScale),
                     juce::roundToInt (1900.0f * guiScale), juce::roundToInt (1200.0f * guiScale));
    setSize (juce::roundToInt (1500.0f * guiScale), juce::roundToInt (860.0f * guiScale));
    setWantsKeyboardFocus (true);
}

OpenEQAudioProcessorEditor::~OpenEQAudioProcessorEditor()
{
    stopTimer();
    processorRef.setMonitorMode (0);
    processorRef.setAudition (false, -1);
    processorRef.setDetectorAudition (false, -1);
    setLookAndFeel (nullptr);
}

bool OpenEQAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    if (key.getTextCharacter() == 'c' || key.getTextCharacter() == 'C' || key.isKeyCode (juce::KeyPress::spaceKey))
    {
        floatingInspector.setOpen (! floatingInspector.isOpen());
        return true;
    }
    if (key.getTextCharacter() == 'm' || key.getTextCharacter() == 'M')
    {
        inspectorMovedByUser = true;
        floatingInspector.setTopLeftPosition (graphBounds.getX() + 60, graphBounds.getY() + 60);
        return true;
    }
    if (key.getTextCharacter() == 'r' || key.getTextCharacter() == 'R')
    {
        inspectorMovedByUser = false;
        floatingInspector.resetToBottomCenter();
        return true;
    }
    if (key.getTextCharacter() == '1')
    {
        setInspectorTab (InspectorTab::EqFilter);
        return true;
    }
    if (key.getTextCharacter() == '2')
    {
        setInspectorTab (InspectorTab::Dynamics);
        return true;
    }
    if (key.getTextCharacter() == '3')
    {
        setInspectorTab (InspectorTab::Split);
        return true;
    }
    if (key.getTextCharacter() == '4')
    {
        setInspectorTab (InspectorTab::BandList);
        return true;
    }
    return false;
}

void OpenEQAudioProcessorEditor::timerCallback()
{
    noticeLabel.setText (processorRef.restrictionNotice(), juce::dontSendNotification);
    const bool slotA = processorRef.getAudibleABSlot() == 0;
    slotAButton.setToggleState (slotA, juce::dontSendNotification);
    slotBButton.setToggleState (! slotA, juce::dontSendNotification);
    if (monitorButton.getMonitorMode() != processorRef.getMonitorMode())
        monitorButton.setMode (processorRef.getMonitorMode());
    static const char* sourceNames[] = { "POST", "PRE", "SIDE", "WATER", "SPECTRO", "SPEC 3D" };
    const int source = juce::jlimit (0, 5, processorRef.getSpectrumSource());
    if (spectrumSourceButton.getButtonText() != sourceNames[source])
        spectrumSourceButton.setButtonText (sourceNames[source]);

    const int currentBand = eqCurve.getSelectedBand();
    const bool dynOn = processorRef.apvts.getRawParameterValue (Params::bandDynOnID (currentBand))->load() > 0.5f;
    const bool splitOn = processorRef.apvts.getRawParameterValue (Params::bandSplitOnID (currentBand))->load() > 0.5f;
    floatingInspector.setFieldIndicators (dynOn, splitOn);

    const float freq = processorRef.apvts.getRawParameterValue (Params::bandFreqID (currentBand))->load();
    const float gain = processorRef.apvts.getRawParameterValue (Params::bandGainID (currentBand))->load();
    const float q = processorRef.apvts.getRawParameterValue (Params::bandQID (currentBand))->load();
    const int type = (int) processorRef.apvts.getRawParameterValue (Params::bandTypeID (currentBand))->load();
    const juce::String typeStr = (type >= 0 && type < Params::filterTypeNames.size()) ? Params::filterTypeNames[type] : "Bell";

    juce::String sum;
    if (activeTab == InspectorTab::Dynamics)
    {
        sum = dynamicsPanel.dynamicsSummary();
    }
    else if (activeTab == InspectorTab::Split)
    {
        const float transG = processorRef.apvts.getRawParameterValue (Params::bandSplitTransID (currentBand))->load();
        const float tailG = processorRef.apvts.getRawParameterValue (Params::bandSplitTailID (currentBand))->load();
        sum = "TRANSIENT " + ValueFormat::gainDB (transG) + "   TONAL " + ValueFormat::gainDB (tailG);
    }
    else if (activeTab == InspectorTab::Spectral)
    {
        const float amount = processorRef.apvts.getRawParameterValue ("spectralAmount")->load();
        const float sharp = processorRef.apvts.getRawParameterValue ("spectralSharp")->load();
        sum = amount > 1.0e-4f
                  ? "DEPTH " + juce::String (juce::roundToInt (amount * 100.0f)) + "%  SHARP "
                        + juce::String (juce::roundToInt (sharp * 100.0f)) + "%"
                  : "SUPPRESSOR OFF";
    }
    else
    {
        sum = ValueFormat::frequency (freq) + "  " + ValueFormat::gainDB (gain) + "  Q " + ValueFormat::qFactor (q) + "  " + typeStr;
    }

    floatingInspector.setTitle ("BAND " + juce::String (currentBand + 1));
    floatingInspector.setSummary (sum);
    floatingInspector.setAccent (splitOn ? Theme::splitTransCol : (dynOn ? juce::Colour (0xff35c6f0) : Theme::stereoCol));
}

void OpenEQAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (Theme::chassisBottom);
}

void OpenEQAudioProcessorEditor::paintStage (juce::Graphics& g)
{
    const auto full = logicalBounds().toFloat();

    // Chassis: deep graphite with a soft radial vignette and rim.
    g.setGradientFill (juce::ColourGradient (Theme::chassisTop, 0.0f, 0.0f,
                                             Theme::chassisBottom, 0.0f, full.getHeight(), false));
    g.fillRect (full);
    {
        auto centre = full.getCentre();
        const float vMax = full.getWidth() * 0.72f;
        for (int i = 0; i < 3; ++i)
        {
            const float rr = vMax * (0.42f + 0.2f * (float) i);
            g.setColour (juce::Colours::black.withAlpha (0.05f + 0.1f * (float) i));
            g.fillEllipse (centre.x - rr, centre.y - rr, rr * 2.0f, rr * 2.0f);
        }
    }
    g.setColour (Theme::chassisRim);
    g.drawRect (full, 1.0f);

    // Top console bar: beveled gradient + hairline.
    auto topBar = logicalBounds().removeFromTop (70).toFloat().reduced (1.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2b3338), 0.0f, topBar.getY(),
                                             juce::Colour (0xff171d21), 0.0f, topBar.getBottom(), false));
    g.fillRoundedRectangle (topBar, 4.0f);
    g.setColour (Theme::panelEdge);
    g.drawRoundedRectangle (topBar, 4.0f, 1.0f);
    g.setColour (juce::Colour (0x2effffff));
    g.drawHorizontalLine (juce::roundToInt (topBar.getY()) + 1, topBar.getX() + 4, topBar.getRight() - 4);

    // Scope: recessed screen with glowing rim.
    g.setColour (Theme::scopeFill);
    g.fillRoundedRectangle (graphBounds.toFloat(), 6.0f);
    {
        juce::ColourGradient inner (juce::Colour (0x00000000), 0.0f, 0.0f,
                                    Theme::scopeInner.withAlpha (0.45f), 0.0f, 34.0f, false);
        g.setGradientFill (inner);
        g.fillRoundedRectangle (graphBounds.toFloat(), 6.0f);
    }
    {
        // A faint pool of signal colour behind the curve, strongest at the centre of the graph.
        const auto gb = graphBounds.toFloat();
        juce::ColourGradient glow (Theme::signalGlow.withAlpha (0.10f), gb.getCentreX(), gb.getCentreY(),
                                   Theme::signalGlow.withAlpha (0.0f), gb.getCentreX(), gb.getY() - gb.getHeight() * 0.25f, true);
        g.setGradientFill (glow);
        g.fillRoundedRectangle (gb, 6.0f);
    }
    g.setColour (Theme::scopeBorder);
    g.drawRoundedRectangle (graphBounds.toFloat(), 6.0f, 1.0f);
    g.setColour (juce::Colour (0x2affffff));
    g.drawRoundedRectangle (graphBounds.toFloat().reduced (1.5f), 5.0f, 0.75f);
}

void OpenEQAudioProcessorEditor::refreshDynamicsStrip()
{
    const int band = eqCurve.getSelectedBand();
    dynamicsPanel.setSelectedBand (band);
}

void OpenEQAudioProcessorEditor::setInspectorTab (InspectorTab tab)
{
    activeTab = tab;
    switch (tab)
    {
        case InspectorTab::EqFilter: floatingInspector.setActiveField (FloatingInspector::Field::Eq); break;
        case InspectorTab::Dynamics: floatingInspector.setActiveField (FloatingInspector::Field::Dyn); break;
        case InspectorTab::Split:    floatingInspector.setActiveField (FloatingInspector::Field::Split); break;
        case InspectorTab::Spectral: floatingInspector.setActiveField (FloatingInspector::Field::Spec); break;
        case InspectorTab::BandList: break;
    }

    bandControls.setVisible (tab == InspectorTab::EqFilter);
    dynamicsPanel.setVisible (tab == InspectorTab::Dynamics);
    splitGlobalPanel.setVisible (tab == InspectorTab::Split);
    spectralPanel.setVisible (tab == InspectorTab::Spectral);
    bandList.setVisible (tab == InspectorTab::BandList);

    if (! floatingInspector.isOpen())
        floatingInspector.setOpen (true);

    resized();
}

juce::Rectangle<int> OpenEQAudioProcessorEditor::logicalBounds() const
{
    return { 0, 0, juce::roundToInt ((float) getWidth() / guiScale), juce::roundToInt ((float) getHeight() / guiScale) };
}

void OpenEQAudioProcessorEditor::applyGuiScale (float newScale)
{
    const auto logical = logicalBounds();
    guiScale = juce::jlimit (0.5f, 2.0f, newScale);
    setResizeLimits (juce::roundToInt (1100.0f * guiScale), juce::roundToInt (640.0f * guiScale),
                     juce::roundToInt (1900.0f * guiScale), juce::roundToInt (1200.0f * guiScale));
    setSize (juce::roundToInt ((float) logical.getWidth() * guiScale), juce::roundToInt ((float) logical.getHeight() * guiScale));
    resized();
    repaint();
}

void OpenEQAudioProcessorEditor::resized()
{
    // The stage keeps the window's full size and draws at the GUI scale. Layout below uses unscaled coordinates.
    stage.setBounds (getLocalBounds());
    stage.setTransform (juce::AffineTransform::scale (guiScale));
    auto bounds = logicalBounds();
    if (settingsPanel != nullptr && settingsPanel->isVisible())
        settingsPanel->setBounds (bounds);

    // Row 1 & Row 2 Top Console Bar (Total height: 68px)
    auto topArea = bounds.removeFromTop (68);
    auto row1 = topArea.removeFromTop (36).reduced (8, 2);
    auto row2 = topArea.reduced (8, 2);

    // Row 1: Left (Title + INST cross-instance badge)
    titleLabel.setBounds (row1.removeFromLeft (90).withHeight (28));
    // The beta channel label stays visible: this build is 0.3.0-beta, not a finished release.
    subtitleLabel.setBounds (row1.removeFromLeft (58).withSizeKeepingCentre (58, 16));
    row1.removeFromLeft (4);
    instButton.setBounds (row1.removeFromLeft (92).reduced (0, 2));

    // Row 1: Right (Master Utilities)
    settingsButton.setBounds (row1.removeFromRight (68).reduced (2, 2));
    fullButton.setBounds (row1.removeFromRight (48).reduced (2, 2));
    bypassButton.setBounds (row1.removeFromRight (78).reduced (4, 2));
    polarityToggle.setBounds (row1.removeFromRight (86).reduced (2, 2));
    monitorButton.setBounds (row1.removeFromRight (84).reduced (2, 2));

    // Row 1: Center (Presets & A/B & Undo/Redo)
    const int centerWidth = 360;
    auto centerSlot = row1.withSizeKeepingCentre (juce::jmin (row1.getWidth(), centerWidth), row1.getHeight());
    undoButton.setBounds (centerSlot.removeFromLeft (56).reduced (2, 2));
    redoButton.setBounds (centerSlot.removeFromLeft (56).reduced (2, 2));
    centerSlot.removeFromLeft (6);
    slotAButton.setBounds (centerSlot.removeFromLeft (30).reduced (1, 2));
    slotBButton.setBounds (centerSlot.removeFromLeft (30).reduced (1, 2));
    centerSlot.removeFromLeft (6);
    presetButton.setBounds (centerSlot.reduced (2, 2));

    // Row 2: DSP Engine & Mode Strip
    processingModeCycle.setBounds (row2.removeFromLeft (176).reduced (2, 2));
    row2.removeFromLeft (10);
    linearPhaseQualityCycle.setBounds (row2.removeFromLeft (156).reduced (2, 2));
    row2.removeFromLeft (10);
    mixCrossoverCycle.setBounds (row2.removeFromLeft (124).reduced (2, 2));

    oversampleButton.setBounds (row2.removeFromRight (112).reduced (2, 2));
    row2.removeFromRight (6);
    ultraButton.setBounds (row2.removeFromRight (84).reduced (2, 2));

    // Right Side: Dedicated Output Column (width 140px, full height below top console)
    const int outputWidth = 140;
    outputColumn.setBounds (bounds.removeFromRight (outputWidth).reduced (4, 4));

    // Left Side: Input section, mirrored against the output column
    const int inputWidth = 112;
    inputColumn.setBounds (bounds.removeFromLeft (inputWidth).reduced (4, 4));

    // Center Stage: Graph Area (NOW SPANS FULL HEIGHT - NO FIXED BOTTOM DECK)
    graphBounds = bounds.reduced (6);
    noticeLabel.setBounds (graphBounds.getX() + 12, graphBounds.getY() + 6, graphBounds.getWidth() - 360, 16);
    auto graphArea = graphBounds.reduced (4);

    // Graph Top-Right HUD Controls:
    auto hud = graphArea.removeFromTop (24).removeFromRight (380);
    spectrumSourceButton.setBounds (hud.removeFromRight (70).reduced (2, 2));
    rangeButton.setBounds (hud.removeFromRight (56).reduced (2, 2));
    fftButton.setBounds (hud.removeFromRight (50).reduced (2, 2));
    keysButton.setBounds (hud.removeFromRight (50).reduced (2, 2));
    grabButton.setBounds (hud.removeFromRight (54).reduced (2, 2));
    listButton.setBounds (hud.removeFromRight (50).reduced (2, 2));

    for (auto* btn : { &spectrumSourceButton, &rangeButton, &fftButton, &keysButton, &grabButton, &listButton })
        btn->toFront (false);

    // Piano Keyboard sits at bottom of graphArea if active, aligning directly with EQ frequency grid
    if (keysVisible)
    {
        auto keyArea = graphArea.removeFromBottom (34);
        keyboardStrip.setBounds (keyArea);
        keyboardStrip.toFront (false);
    }

    spectrumAnalyzer.setBounds (graphArea);
    eqCurve.setBounds (graphArea);

    // Floating Inspector Console (Collapsible & User-Movable in bottom-middle)
    const int closedW = 480, closedH = 28;
    const int openW = 840;
    const int openH = activeTab == InspectorTab::Dynamics ? 252 : 196;
    floatingInspector.setDimensions ({ closedW, closedH }, { openW, openH });

    if (! inspectorMovedByUser)
    {
        const int cx = graphBounds.getCentreX();
        const int by = graphBounds.getBottom() - (keysVisible ? 38 : 6);
        const auto curSize = floatingInspector.isOpen() ? juce::Point<int> { openW, openH } : juce::Point<int> { closedW, closedH };
        floatingInspector.setBounds (cx - curSize.x / 2, by - curSize.y, curSize.x, curSize.y);
    }
    else if (std::getenv ("OPENEQ_MOCKUP_MOVED") != nullptr)
    {
        floatingInspector.setTopLeftPosition (graphBounds.getX() + 50, graphBounds.getY() + 70);
    }
    else
    {
        floatingInspector.clampToParent();
    }
    floatingInspector.toFront (false);

    // Layout active tab content inside floatingInspector body (clean content area without redundant tab bar)
    if (floatingInspector.isOpen())
    {
        auto bodyBounds = floatingInspector.getBody().getLocalBounds();
        auto contentArea = bodyBounds.reduced (4, 4);
        bandControls.setBounds (contentArea);
        dynamicsPanel.setBounds (contentArea);
        splitGlobalPanel.setBounds (contentArea);
        spectralPanel.setBounds (contentArea);
        bandList.setBounds (contentArea);
    }
}
