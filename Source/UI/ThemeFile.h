#pragma once
#include <JuceHeader.h>
#include "Theme.h"
#include <functional>

/** Reads a .opeq_theme theme file. The file is JSON with ARGB integers for each colour key.
    OpenEQ applies the colours it has a place for: the chassis background, the EQ curve, the pre and post spectrum,
    the left, right, mid and side colours, and the 32 band colours. Other keys (meters, fonts, axis lines, fills)
    have no equivalent yet and are ignored. */
namespace ThemeFile
{
    inline bool load (const juce::File& file)
    {
        const auto parsed = juce::JSON::parse (file);
        auto* object = parsed.getDynamicObject();
        if (object == nullptr)
            return false;

        int applied = 0;
        auto colour = [&] (const juce::String& key, juce::Colour& target)
        {
            const auto value = object->getProperty (juce::Identifier (key));
            if (! value.isInt() && ! value.isInt64() && ! value.isDouble())
                return;
            const auto argb = static_cast<juce::uint32> (static_cast<juce::int64> (value));
            target = juce::Colour (argb);
            ++applied;
        };

        colour ("background_top_colour", Theme::chassisTop);
        colour ("background_bottom_colour", Theme::chassisBottom);
        colour ("whole_EQ_line_colour", Theme::curveCol);
        colour ("input_spectrum_line_colour", Theme::preSpectrum);
        colour ("output_spectrum_line_colour", Theme::postSpectrum);
        colour ("left_line_colour", Theme::leftCol);
        colour ("right_line_colour", Theme::rightCol);
        colour ("mid_line_colour", Theme::midCol);
        colour ("side_line_colour", Theme::sideCol);

        for (int band = 0; band < (int) Theme::bandColourOverride.size(); ++band)
        {
            const auto key = "band_" + juce::String (band) + "_colour";
            const auto value = object->getProperty (juce::Identifier (key));
            if (value.isInt() || value.isInt64() || value.isDouble())
            {
                Theme::bandColourOverride[(size_t) band] = juce::Colour (static_cast<juce::uint32> (static_cast<juce::int64> (value)));
                ++applied;
            }
        }

        return applied > 0;
    }

    /** Writes the current colours as a .opeq_theme theme file, using the keys that load() reads. */
    inline bool save (const juce::File& file, const std::function<juce::Colour (int)>& bandColour)
    {
        auto* object = new juce::DynamicObject();
        auto put = [object] (const juce::String& key, juce::Colour colour)
        {
            object->setProperty (juce::Identifier (key), (juce::int64) (juce::int32) colour.getARGB());
        };
        put ("background_top_colour", Theme::chassisTop);
        put ("background_bottom_colour", Theme::chassisBottom);
        put ("whole_EQ_line_colour", Theme::curveCol);
        put ("input_spectrum_line_colour", Theme::preSpectrum);
        put ("output_spectrum_line_colour", Theme::postSpectrum);
        put ("left_line_colour", Theme::leftCol);
        put ("right_line_colour", Theme::rightCol);
        put ("mid_line_colour", Theme::midCol);
        put ("side_line_colour", Theme::sideCol);
        for (int band = 0; band < 32; ++band)
            put ("band_" + juce::String (band) + "_colour", bandColour (band));

        const auto text = juce::JSON::toString (juce::var (object), true);
        return file.replaceWithText (text);
    }
}
