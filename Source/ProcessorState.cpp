#include "PluginProcessor.h"
#include "AppSettings.h"
#include <cmath>

void OpenEQAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();

    state.setProperty ("schema", 3, nullptr);
    state.setProperty ("abSlot", abSlot, nullptr);
    if (auto otherXml = abSlots[1 - abSlot].createXml())
        state.setProperty ("abOther", otherXml->toString(), nullptr);
    if (const auto typedName = getUserInstanceName(); typedName.isNotEmpty())
        state.setProperty ("instanceName", typedName, nullptr);
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

/** True when the saved tree carries a parameter child, whatever its value. */
static bool stateContainsParameter (const juce::ValueTree& state, const juce::String& parameterID)
{
    for (int i = 0; i < state.getNumChildren(); ++i)
    {
        const auto child = state.getChild (i);
        if (child.getProperty ("id").toString() == parameterID || child.getType().toString() == parameterID)
            return true;
    }
    return false;
}

/** Saved sessions from before the Below/Above panels have no panel children at all. */
static bool stateHasDynamicsPanels (const juce::ValueTree& state)
{
    for (int i = 0; i < Params::numBands; ++i)
        if (stateContainsParameter (state, Params::bandDynBelowOnID (i))
            || stateContainsParameter (state, Params::bandDynAboveOnID (i)))
            return true;
    return false;
}

void OpenEQAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (applyState (data, sizeInBytes) && wrapperType == wrapperType_LV2)
    {
        hostResyncNext = 0;
        hostResync.startTimerHz (30);
    }
}

void OpenEQAudioProcessor::resendParametersToHost()
{
    const auto& all = getParameters();
    for (int sent = 0; sent < 64 && hostResyncNext < all.size(); ++sent, ++hostResyncNext)
        all[hostResyncNext]->sendValueChangedMessageToListeners (all[hostResyncNext]->getValue());
    if (hostResyncNext >= all.size())
        hostResync.stopTimer();
}

bool OpenEQAudioProcessor::applyState (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return false;

    auto tree = juce::ValueTree::fromXml (*xml);
    const int schema = (int) tree.getProperty ("schema", 1);
    // Presence of the panel parameters is the migration test, not the live panel switches: a current
    // state may legitimately have both panels off, and must then keep the legacy single-action values out.
    const bool hasDynamicsPanels = stateHasDynamicsPanels (tree);
    const int slot = juce::jlimit (0, 1, (int) tree.getProperty ("abSlot", 0));
    const auto otherText = tree.getProperty ("abOther").toString();
    tree.removeProperty ("schema", nullptr);
    tree.removeProperty ("abSlot", nullptr);
    tree.removeProperty ("abOther", nullptr);
    // The typed instance name belongs to the session, not to the parameter tree or the A/B slots. A state
    // without it (older sessions, presets) leaves the current name alone.
    if (tree.hasProperty ("instanceName"))
        setUserInstanceName (tree.getProperty ("instanceName").toString());
    tree.removeProperty ("instanceName", nullptr);
    apvts.replaceState (tree);
    abSlot = slot;
    abSlots[abSlot] = apvts.copyState();
    if (otherText.isNotEmpty())
        if (auto otherXml = juce::parseXML (otherText))
            abSlots[1 - abSlot] = juce::ValueTree::fromXml (*otherXml);
    if (! abSlots[1 - abSlot].isValid())
        abSlots[1 - abSlot] = abSlots[abSlot].createCopy();
    refreshOtherSlotSplit();

    if (schema < 2)
    {
        for (int i = 0; i < numBands; ++i)
        {
            const float defaultFreq = juce::jlimit (20.0f, 20000.0f, 60.0f * std::pow (1.75f, (float) i));
            const bool active = apvts.getRawParameterValue (Params::bandActiveID (i))->load() > 0.5f;
            const bool nonDefault = active
                || std::abs (apvts.getRawParameterValue (Params::bandFreqID (i))->load() - defaultFreq) > 0.5f
                || std::abs (apvts.getRawParameterValue (Params::bandGainID (i))->load()) > 0.01f
                || (int) apvts.getRawParameterValue (Params::bandTypeID (i))->load() != 0
                || std::abs (apvts.getRawParameterValue (Params::bandQID (i))->load() - 1.0f) > 0.001f
                || (int) apvts.getRawParameterValue (Params::bandSlopeID (i))->load() != 1
                || (int) apvts.getRawParameterValue (Params::bandStereoID (i))->load() != 0
                || apvts.getRawParameterValue (Params::bandSoloID (i))->load() > 0.5f
                || apvts.getRawParameterValue (Params::bandDynOnID (i))->load() > 0.5f;
            if (nonDefault)
                if (auto* shown = apvts.getParameter (Params::bandShownID (i)))
                    shown->setValueNotifyingHost (1.0f);
        }
    }

    if (! hasDynamicsPanels)
        migrateLegacyDynamics();
    if (schema < 3)
        migrateLegacySlopes();

    monitorMode.store (0);
    auditionBandIndex.store (-1);
    detectorAuditionIndex.store (-1);
    return true;
}

void OpenEQAudioProcessor::migrateLegacySlopes()
{
    // Sessions saved before schema 3 had a 7-step slope menu that only the cuts used. The continuous slope replaces it:
    // a cut keeps its dB per octave, and every other type starts at slope 0 (a second-order bell, a first-order shelf).
    for (int i = 0; i < numBands; ++i)
    {
        auto* slopeNorm = apvts.getParameter (Params::bandSlopeNormID (i));
        if (slopeNorm == nullptr)
            continue;
        const auto type = (FilterType) juce::jlimit (0, filterTypeCount - 1, (int) apvts.getRawParameterValue (Params::bandTypeID (i))->load());
        float value = 0.0f;
        if (type == FilterType::LowCut || type == FilterType::HighCut)
            value = Params::slopeNormForCutDb ((float) Params::slopeIndexToDb ((int) apvts.getRawParameterValue (Params::bandSlopeID (i))->load()));
        slopeNorm->setValueNotifyingHost (slopeNorm->convertTo0to1 (value));
    }
}

void OpenEQAudioProcessor::migrateLegacyDynamics()
{
    // Sessions saved before the Below and Above panels used one action with one range. Moving that into the
    // matching panel gives the same offsets, because the reference rule with a 100% ratio is the old limit.
    for (int i = 0; i < numBands; ++i)
    {
        const bool dynamic = apvts.getRawParameterValue (Params::bandDynOnID (i))->load() > 0.5f;
        const bool hasPanel = apvts.getRawParameterValue (Params::bandDynBelowOnID (i))->load() > 0.5f
                           || apvts.getRawParameterValue (Params::bandDynAboveOnID (i))->load() > 0.5f;
        if (! dynamic || hasPanel)
            continue;

        const float range = apvts.getRawParameterValue (Params::bandDynRangeID (i))->load();
        const int action = (int) apvts.getRawParameterValue (Params::bandDynActionID (i))->load();
        const bool above = action == 0;
        setRawFromUI (above ? Params::bandDynAboveOnID (i) : Params::bandDynBelowOnID (i), 1.0f);
        setRawFromUI (above ? Params::bandDynAboveRangeID (i) : Params::bandDynBelowRangeID (i), range);
        setRawFromUI (above ? Params::bandDynAboveRatioID (i) : Params::bandDynBelowRatioID (i), 100.0f);
    }
}

juce::File OpenEQAudioProcessor::presetRootDirectory() const
{
    const auto redirected = AppSettings::storageFolderOverride();
    if (redirected != juce::File())
        return redirected;
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("OpenEQ");
}

juce::File OpenEQAudioProcessor::userPresetDirectory() const
{
    auto directory = presetRootDirectory().getChildFile ("Presets");
    directory.createDirectory();
    // One-time migration from the pre-rename folder (GodEQ): move presets and the saved default over.
    // Not while the storage folder is redirected: the legacy folder is the user's own.
    const auto legacy = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                            .getChildFile ("GodEQ");
    if (AppSettings::storageFolderOverride() == juce::File() && legacy.isDirectory())
    {
        if (directory.findChildFiles (juce::File::findFiles, false).isEmpty())
            for (const auto& legacyPreset : juce::Array<juce::File> (legacy.getChildFile ("Presets").findChildFiles (juce::File::findFiles, false)))
                legacyPreset.copyFileTo (directory.getChildFile (legacyPreset.getFileName()).getFullPathName());
        const auto legacyDefault = legacy.getChildFile ("default.godeq");
        if (legacyDefault.existsAsFile() && ! defaultPresetFile().existsAsFile())
            legacyDefault.copyFileTo (defaultPresetFile());
    }
    return directory;
}

juce::Array<juce::File> OpenEQAudioProcessor::listUserPresets() const
{
    juce::Array<juce::File> files;
    userPresetDirectory().findChildFiles (files, juce::File::findFiles, false, "*.opeq");
    userPresetDirectory().findChildFiles (files, juce::File::findFiles, false, "*.godeq");
    files.sort();
    return files;
}

juce::File OpenEQAudioProcessor::defaultPresetFile() const
{
    return presetRootDirectory().getChildFile ("default.opeq");
}

bool OpenEQAudioProcessor::saveAsDefault()
{
    const auto file = defaultPresetFile();
    file.getParentDirectory().createDirectory();
    return savePresetFile (file);
}

bool OpenEQAudioProcessor::savePresetFile (const juce::File& file)
{
    auto state = apvts.copyState();
    state.setProperty ("schema", 3, nullptr);
    if (auto xml = state.createXml())
        return xml->writeTo (file);
    return false;
}

bool OpenEQAudioProcessor::loadPresetFile (const juce::File& file)
{
    // True only when a state was applied. A truncated or foreign file changes nothing and returns false.
    if (auto xml = juce::XmlDocument::parse (file))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            juce::MemoryBlock block;
            copyXmlToBinary (*xml, block);
            return applyState (block.getData(), (int) block.getSize());
        }
    }

    juce::MemoryBlock block;
    if (! file.loadFileAsData (block))
        return false;
    return applyState (block.getData(), (int) block.getSize());
}

void OpenEQAudioProcessor::loadFactoryPreset (int index)
{
    beginEdit ("Factory Preset");
    for (auto* parameter : getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter);
        if (ranged == nullptr)
            continue;
        setRawFromUI (ranged->getParameterID(), ranged->convertFrom0to1 (ranged->getDefaultValue()));
    }

    if (index == 1)
    {
        setRawFromUI (Params::bandTypeID (0), (float) FilterType::TiltShelf);
        setRawFromUI (Params::bandFreqID (0), 800.0f);
        setRawFromUI (Params::bandGainID (0), 4.0f);
        setRawFromUI (Params::bandActiveID (0), 1.0f);
        setRawFromUI (Params::bandShownID (0), 1.0f);
    }
    else if (index == 2)
    {
        setRawFromUI (Params::bandTypeID (0), (float) FilterType::Bell);
        setRawFromUI (Params::bandFreqID (0), 3500.0f);
        setRawFromUI (Params::bandGainID (0), 3.0f);
        setRawFromUI (Params::bandQID (0), 0.8f);
        setRawFromUI (Params::bandActiveID (0), 1.0f);
        setRawFromUI (Params::bandShownID (0), 1.0f);
    }
    endEdit();
}
