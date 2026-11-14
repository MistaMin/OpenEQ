#pragma once
#include <JuceHeader.h>
#include <functional>
#include "PluginProcessor.h"
#include "Parameters.h"
#include "AppSettings.h"

/** The band menu shared by the graph and the band list (band operations). */
namespace BandMenu
{
    enum Action
    {
        ToggleOn = 1,
        ToggleDynamic,
        Invert,
        Split,
        Copy,
        CopyInPlace,
        Cut,
        Paste,
        PasteInverted,
        Remove,
        SortFrequency,
        SortType,
        ClearAll
    };

    constexpr int typeBase = 1000;
    constexpr int stereoBase = 2000;

    /** Types grouped as in the band controller: common types first, then filter families. */
    inline void addTypeItems (juce::PopupMenu& menu, int currentType)
    {
        // Favourites come first, as set in the settings.
        const auto favourites = AppSettings::get().favouriteTypes();
        if (! favourites.isEmpty())
        {
            juce::PopupMenu favouriteMenu;
            for (int type : favourites)
                if (type >= 0 && type < filterTypeCount)
                    favouriteMenu.addItem (typeBase + type, Params::filterTypeNames[type], true, type == currentType);
            menu.addSubMenu ("Favourites", favouriteMenu);
            menu.addSeparator();
        }

        juce::StringArray seen;
        for (int t = 0; t < filterTypeCount; ++t)
        {
            const auto group = Params::filterTypeGroup (t);
            if (group.isEmpty())
                menu.addItem (typeBase + t, Params::filterTypeNames[t], true, t == currentType);
            else if (! seen.contains (group))
                seen.add (group);
        }

        juce::StringArray order;
        for (const auto& name : Params::filterTypeGroupOrder())
            if (seen.contains (name))
                order.add (name);
        for (const auto& name : seen)
            if (! order.contains (name))
                order.add (name);

        for (const auto& group : order)
        {
            juce::PopupMenu sub;
            for (int t = 0; t < filterTypeCount; ++t)
                if (Params::filterTypeGroup (t) == group)
                    sub.addItem (typeBase + t, Params::filterTypeNames[t], true, t == currentType);
            menu.addSubMenu (group, sub);
        }
    }

    /** Shows the menu for one band. `onDone` receives the band index that should be selected afterwards. */
    inline void show (OpenEQAudioProcessor& processor, int index, juce::Component* target,
                      std::function<void (int)> onDone)
    {
        juce::PopupMenu menu;
        const bool used = processor.isBandUsed (index);

        if (used)
        {
            const int type = (int) processor.apvts.getRawParameterValue (Params::bandTypeID (index))->load();
            const int stereo = (int) processor.apvts.getRawParameterValue (Params::bandStereoID (index))->load();
            const bool active = processor.apvts.getRawParameterValue (Params::bandActiveID (index))->load() > 0.5f;
            const bool dynamic = processor.apvts.getRawParameterValue (Params::bandDynOnID (index))->load() > 0.5f;
            const bool splittable = stereo == (int) StereoMode::Left || stereo == (int) StereoMode::Right
                                 || stereo == (int) StereoMode::Mid || stereo == (int) StereoMode::Side;

            juce::PopupMenu types;
            addTypeItems (types, type);
            menu.addSubMenu ("Types", types);

            juce::PopupMenu stereoMenu;
            for (int m = 0; m < Params::stereoModeNames.size(); ++m)
                stereoMenu.addItem (stereoBase + m, Params::stereoModeNames[m], true, m == stereo);
            menu.addSubMenu ("Stereo", stereoMenu);

            menu.addSeparator();
            menu.addItem (ToggleOn, active ? "Disable" : "Enable");
            menu.addItem (ToggleDynamic, dynamic ? "Disable Dynamic" : "Enable Dynamic",
                          ResponseModel::supportsDynamics ((FilterType) type));
            menu.addItem (Invert, "Invert Gain");
            menu.addItem (Split, "Split to Opposite Channel", splittable);
            menu.addSeparator();
            menu.addItem (Copy, "Copy");
            menu.addItem (CopyInPlace, "Copy In Place");
            menu.addItem (Cut, "Cut");
            menu.addItem (Remove, "Remove");
        }

        if (processor.hasBandClipboard())
        {
            menu.addSeparator();
            menu.addItem (Paste, "Paste");
            menu.addItem (PasteInverted, "Paste Inverted");
        }

        menu.addSeparator();
        menu.addItem (SortFrequency, "Sort by Frequency");
        menu.addItem (SortType, "Sort by Type");
        menu.addItem (ClearAll, "Clear All Bands");

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target),
            [&processor, index, onDone] (int result)
            {
                if (result <= 0)
                    return;

                int next = index;
                if (result >= stereoBase && result < stereoBase + 10)
                {
                    processor.beginEdit ("Change Stereo Mode");
                    processor.setRawFromUI (Params::bandStereoID (index), (float) (result - stereoBase));
                    processor.endEdit();
                }
                else if (result >= typeBase && result < typeBase + filterTypeCount)
                {
                    processor.beginEdit ("Change Type");
                    processor.setRawFromUI (Params::bandTypeID (index), (float) (result - typeBase));
                    processor.ensureCutSlope (index);
                    processor.endEdit();
                }
                else
                {
                    switch (result)
                    {
                        case ToggleOn:
                        {
                            const bool active = processor.apvts.getRawParameterValue (Params::bandActiveID (index))->load() > 0.5f;
                            processor.beginEdit ("Toggle Band");
                            processor.setRawFromUI (Params::bandActiveID (index), active ? 0.0f : 1.0f);
                            processor.endEdit();
                            break;
                        }
                        case ToggleDynamic:
                        {
                            const bool dynamic = processor.apvts.getRawParameterValue (Params::bandDynOnID (index))->load() > 0.5f;
                            processor.beginEdit ("Toggle Dynamic");
                            processor.setRawFromUI (Params::bandDynOnID (index), dynamic ? 0.0f : 1.0f);
                            processor.endEdit();
                            break;
                        }
                        case Invert:
                        {
                            const float gain = processor.apvts.getRawParameterValue (Params::bandGainID (index))->load();
                            processor.beginEdit ("Invert Gain");
                            processor.setRawFromUI (Params::bandGainID (index), -gain);
                            processor.endEdit();
                            break;
                        }
                        case Split:
                        {
                            const int slot = processor.splitBand (index);
                            if (slot >= 0)
                                next = slot;
                            break;
                        }
                        case Copy:
                            processor.copyBandToClipboard (index);
                            break;
                        case CopyInPlace:
                        {
                            const int slot = processor.duplicateBand (index, false);
                            if (slot >= 0)
                                next = slot;
                            break;
                        }
                        case Cut:
                            processor.copyBandToClipboard (index);
                            processor.removeBands ({ index });
                            break;
                        case Paste:
                        {
                            const int slot = processor.pasteBandClipboard (index, false);
                            if (slot >= 0)
                                next = slot;
                            break;
                        }
                        case PasteInverted:
                        {
                            const int slot = processor.pasteBandClipboard (index, true);
                            if (slot >= 0)
                                next = slot;
                            break;
                        }
                        case Remove:
                            processor.removeBands ({ index });
                            break;
                        case SortFrequency:
                            processor.sortBands (false);
                            break;
                        case SortType:
                            processor.sortBands (true);
                            break;
                        case ClearAll:
                            processor.clearAllBands();
                            break;
                        default:
                            break;
                    }
                }

                if (onDone)
                    onDone (next);
            });
    }
}
