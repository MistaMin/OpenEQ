#pragma once
#include <juce_core/juce_core.h>
#include <cmath>

namespace ValueFormat
{
// 220 -> "220"   2200 -> "2.2k"   16800 -> "16.8k"
inline juce::String frequency (float hz)
{
    if (hz < 1000.0f)
        return juce::String (juce::roundToInt (hz));
    float k = hz / 1000.0f;
    return juce::String (k, 1) + "k";
}

inline juce::String gainDB (float db)
{
    juce::String sign = db > 0.01f ? "+" : "";
    return sign + juce::String (db, 1);
}

inline juce::String qFactor (float q)
{
    // Three digits at most: 0.70, 8.50, 40.0.
    if (q >= 100.0f)
        return juce::String (juce::roundToInt (q));
    if (q >= 10.0f)
        return juce::String (q, 1);
    return juce::String (q, 2);
}

// -100 dB and below reads as silence, matching the side level's floor.
inline juce::String sideDB (float db)
{
    if (db <= -99.9f)
        return "-inf";
    return gainDB (db) + " dB";
}

// 0 -> "C", -30 -> "L30", +12 -> "R12"
inline juce::String pan (float percent)
{
    const int amount = juce::roundToInt (std::abs (percent));
    if (amount == 0)
        return "C";
    return (percent < 0.0f ? "L" : "R") + juce::String (amount);
}

// Musical note nearest to a frequency, with octave (A4 = 440 Hz). Empty for non-positive input.
inline juce::String noteName (float hz)
{
    if (! (hz > 0.0f))
        return {};
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int midi = juce::roundToInt (69.0f + 12.0f * std::log2 (hz / 440.0f));
    const int octave = midi / 12 - 1;
    return juce::String (names[((midi % 12) + 12) % 12]) + juce::String (octave);
}

// "880 Hz  A5" style readout used beside the graph and in value entry.
inline juce::String frequencyWithNote (float hz)
{
    const auto note = noteName (hz);
    return note.isEmpty() ? frequency (hz) + " Hz" : frequency (hz) + " Hz  " + note;
}

inline juce::String percent (float v)
{
    return juce::String (juce::roundToInt (v)) + "%";
}

// Compression / dynamic EQ ratio: 0% -> 1:1, 50% -> 2:1, 75% -> 4:1, 100% -> inf:1
inline juce::String ratio (float percent)
{
    const float s = percent / 100.0f;
    if (s <= 0.01f)
        return "1:1";
    if (s >= 0.98f && s <= 1.02f)
        return "inf:1";
    if (s > 1.02f)
        return juce::String (s, 1) + ":1";
    const float r = 1.0f / (1.0f - s);
    if (r >= 20.0f)
        return juce::String (juce::roundToInt (r)) + ":1";
    if (r >= 10.0f)
        return juce::String (r, 0) + ":1";
    return juce::String (r, 1) + ":1";
}

// 12.5 ms, 350 ms, 1.8 s
/** Slope label in dB per octave for a normalised slope: 12 at 0, 24 at 0.5, 48 at 0.75, 96 at 1. */
inline float slopeLabelDb (float slopeNorm)
{
    const float s = juce::jlimit (0.0f, 1.0f, slopeNorm);
    if (s <= 0.5f)  return 12.0f + 24.0f * s;
    if (s <= 0.75f) return 24.0f + 96.0f * (s - 0.5f);
    return 48.0f + 192.0f * (s - 0.75f);
}

inline juce::String slope (float slopeNorm)
{
    return juce::String (slopeLabelDb (slopeNorm), 1) + " dB/oct";
}

/** Inverse of slopeLabelDb, clamped to 12..96 dB/oct. */
inline float slopeNormForLabelDb (float db)
{
    const float d = juce::jlimit (12.0f, 96.0f, db);
    if (d <= 24.0f) return (d - 12.0f) / 24.0f;
    if (d <= 48.0f) return 0.5f + (d - 24.0f) / 96.0f;
    return 0.75f + (d - 48.0f) / 192.0f;
}

inline juce::String timeMs (float ms)
{
    if (ms >= 1000.0f)
        return juce::String (ms / 1000.0f, 2) + " s";
    if (ms < 1.0f)
        return "<1 ms";
    return juce::String (juce::roundToInt (ms)) + " ms";
}

inline bool parseFrequency (const juce::String& text, float& hz)
{
    auto trimmed = text.trim().toLowerCase();
    if (trimmed.endsWith ("hz"))
        trimmed = trimmed.dropLastCharacters (2).trim();
    if (trimmed.endsWith ("k"))
    {
        hz = trimmed.dropLastCharacters (1).trim().getFloatValue() * 1000.0f;
        return hz > 0.0f;
    }

    if (trimmed.length() >= 2 && trimmed.length() <= 4 && juce::CharacterFunctions::isLetter (trimmed[0]))
    {
        const int letter = juce::CharacterFunctions::toUpperCase (trimmed[0]);
        int accidental = 0;
        int octaveIndex = 1;
        if (trimmed[1] == '#') { accidental = 1; octaveIndex = 2; }
        else if (trimmed[1] == 'b') { accidental = -1; octaveIndex = 2; }
        const int octave = trimmed.substring (octaveIndex).getIntValue();
        const int semisFromC[] = { 9, 11, 0, 2, 4, 5, 7 };
        const int letterIndex = letter - 'A';
        if (letterIndex >= 0 && letterIndex < 7 && trimmed.substring (octaveIndex).containsOnly ("0123456789-"))
        {
            const int midi = (octave + 1) * 12 + semisFromC[letterIndex] + accidental;
            hz = 440.0f * std::pow (2.0f, (midi - 69) / 12.0f);
            return hz > 0.0f;
        }
    }

    hz = trimmed.getFloatValue();
    return hz > 0.0f;
}

inline bool parseGainDb (const juce::String& text, float& db)
{
    auto trimmed = text.trim().toLowerCase();
    if (trimmed.endsWith ("db"))
        trimmed = trimmed.dropLastCharacters (2).trim();
    if (trimmed.isEmpty())
        return false;
    db = trimmed.getFloatValue();
    return true;
}
} // namespace ValueFormat
