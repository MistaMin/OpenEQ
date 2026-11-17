#pragma once
#include <JuceHeader.h>
#include <vector>
#include "InstanceRegistry.h"

/** The model behind the instance (INST) menu, kept apart from the UI so it can be built and checked without
    a window. Rows name every OpenEQ instance in this process by its display name (host track name, else
    "Instance N", with " (2)" for duplicates). Results carry registry ids, never pointers, so a row whose
    instance closed while the menu was open resolves to nothing. */
namespace InstanceMenu
{
    struct Row
    {
        int instanceId = 0;
        juce::String label;          // display name
        bool hasColour = false;      // the host reported a track colour
        juce::Colour colour;
        bool isOwn = false;          // the instance this window belongs to
        bool isControlled = false;   // the instance this window edits now
        bool isLinked = false;       // mirrored with this window's own instance
    };

    struct Model
    {
        std::vector<Row> rows;       // registration order
        juce::String emptyNote;      // set when this window's instance is the only one visible
    };

    constexpr int unlinkItem = 2;
    constexpr int renameItem = 3;
    constexpr int switchItemBase = 1000;
    constexpr int linkItemBase = 3000;
    constexpr int maxRows = 1000;

    inline const char* processScopeNote()
    {
        return "Only instances in this host process are listed";
    }

    inline const char* noOtherInstancesNote()
    {
        return "No other OpenEQ instance in this process. Hosts that run each plug-in in its own process cannot show them here.";
    }

    inline Model build (const std::vector<InstanceRegistry::Summary>& entries, int ownId, int controlledId, int linkedId)
    {
        Model model;
        for (const auto& entry : entries)
        {
            if ((int) model.rows.size() >= maxRows)
                break;
            Row row;
            row.instanceId = entry.id;
            row.label = entry.name.isNotEmpty() ? entry.name : entry.fallbackName;
            row.hasColour = entry.hasTrackColour;
            row.colour = juce::Colour (entry.trackColour);
            row.isOwn = entry.id == ownId;
            row.isControlled = entry.id == controlledId;
            row.isLinked = linkedId != 0 && entry.id == linkedId;
            model.rows.push_back (row);
        }
        bool anyOther = false;
        for (const auto& row : model.rows)
            anyOther = anyOther || ! row.isOwn;
        if (! anyOther)
            model.emptyNote = noOtherInstancesNote();
        return model;
    }

    /** Text of a row in the "edit in this window" list. */
    inline juce::String switchLabel (const Row& row)
    {
        return row.isOwn ? row.label + "  (this plug-in)" : row.label;
    }

    struct Choice
    {
        enum class Kind { None, Switch, Link, Unlink, Rename };
        Kind kind = Kind::None;
        int instanceId = 0;
    };

    /** Maps a popup result back to an action on a registry id. */
    inline Choice decode (const Model& model, int result)
    {
        Choice choice;
        if (result == unlinkItem)
        {
            choice.kind = Choice::Kind::Unlink;
            return choice;
        }
        if (result == renameItem)
        {
            // Renames the instance this window edits now.
            for (const auto& row : model.rows)
                if (row.isControlled)
                {
                    choice.kind = Choice::Kind::Rename;
                    choice.instanceId = row.instanceId;
                }
            return choice;
        }
        const auto rowFor = [&] (int base) -> const Row*
        {
            const int index = result - base;
            if (index < 0 || index >= (int) model.rows.size() || index >= maxRows)
                return nullptr;
            return &model.rows[(size_t) index];
        };
        if (const auto* row = rowFor (switchItemBase))
        {
            choice.kind = Choice::Kind::Switch;
            choice.instanceId = row->instanceId;
        }
        else if (const auto* linked = rowFor (linkItemBase); linked != nullptr && ! linked->isOwn)
        {
            choice.kind = Choice::Kind::Link;
            choice.instanceId = linked->instanceId;
        }
        return choice;
    }
}
