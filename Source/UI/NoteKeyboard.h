#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <functional>

/** Professional logarithmic piano keyboard under the EQ curve (band editing).
    Aligns exactly with the EQ graph's logarithmic frequency scale (20 Hz to 20,000 Hz),
    covering MIDI note 16 (E0 = 20.6 Hz) through note 135 (D#10 = 19.9 kHz).
    Clicking or dragging a key sets the selected EQ band to that exact musical pitch. */
class NoteKeyboardStrip : public juce::Component
{
public:
    NoteKeyboardStrip()
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    ~NoteKeyboardStrip() override = default;

    /** Callback invoked when a key is clicked or dragged. Argument is MIDI note number (16..135). */
    std::function<void (int)> onNoteChosen;

    /** Equal temperament frequency for MIDI note, A4 = 440 Hz. */
    static float frequencyForNote (int note) noexcept
    {
        return 440.0f * std::pow (2.0f, ((float) note - 69.0f) / 12.0f);
    }

    static constexpr int minMidiNote = 16;   // E0 = 20.6 Hz
    static constexpr int maxMidiNote = 135;  // D#10 = 19,912 Hz

    static constexpr float minFreqHz = 20.0f;
    static constexpr float maxFreqHz = 20000.0f;

    static bool isBlackNote (int note) noexcept
    {
        const int n = (note % 12 + 12) % 12;
        return (n == 1 || n == 3 || n == 6 || n == 8 || n == 10);
    }

    static juce::String noteName (int note)
    {
        static const char* const names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const int n = (note % 12 + 12) % 12;
        const int octave = (note / 12) - 1;
        return juce::String (names[n]) + juce::String (octave);
    }

    float frequencyToX (float freqHz) const noexcept
    {
        const float w = (float) getWidth();
        if (w <= 0.0f)
            return 0.0f;
        const float logMin = std::log10 (minFreqHz);
        const float logMax = std::log10 (maxFreqHz);
        const float logF = std::log10 (juce::jlimit (minFreqHz, maxFreqHz, freqHz));
        return w * (logF - logMin) / (logMax - logMin);
    }

    float noteToX (float note) const noexcept
    {
        return frequencyToX (440.0f * std::pow (2.0f, (note - 69.0f) / 12.0f));
    }

    int getNoteAt (float mouseX, float mouseY) const noexcept
    {
        const int w = getWidth();
        if (w <= 0)
            return minMidiNote;
        const float blackKeyH = (float) getHeight() * 0.60f;

        // If in top 60%, check black keys first
        if (mouseY <= blackKeyH)
        {
            for (int n = minMidiNote; n <= maxMidiNote; ++n)
            {
                if (isBlackNote (n))
                {
                    const float x1 = noteToX ((float) n - 0.5f);
                    const float x2 = noteToX ((float) n + 0.5f);
                    if (mouseX >= x1 && mouseX <= x2)
                        return n;
                }
            }
        }

        // Otherwise find note by logarithmic frequency
        const float logMin = std::log10 (minFreqHz);
        const float logMax = std::log10 (maxFreqHz);
        const float normX = juce::jlimit (0.0f, 1.0f, mouseX / (float) w);
        const float freq = std::pow (10.0f, logMin + normX * (logMax - logMin));
        const int note = juce::roundToInt (69.0f + 12.0f * std::log2 (freq / 440.0f));
        return juce::jlimit (minMidiNote, maxMidiNote, note);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int note = getNoteAt ((float) e.x, (float) e.y);
        if (note != hoveredNote)
        {
            hoveredNote = note;
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        if (hoveredNote != -1)
        {
            hoveredNote = -1;
            repaint();
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int note = getNoteAt ((float) e.x, (float) e.y);
        hoveredNote = note;
        if (onNoteChosen != nullptr)
            onNoteChosen (note);
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const int note = getNoteAt ((float) e.x, (float) e.y);
        if (note != hoveredNote)
        {
            hoveredNote = note;
            if (onNoteChosen != nullptr)
                onNoteChosen (note);
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const int w = getWidth();
        const int h = getHeight();
        if (w <= 0 || h <= 0)
            return;

        // Background well
        g.setColour (juce::Colour (0xff12171a));
        g.fillRect (0, 0, w, h);

        const float blackKeyH = (float) h * 0.60f;

        // 1. Paint white keys base background
        g.setColour (juce::Colour (0xffd6dbe0));
        g.fillRect (0.0f, 0.0f, (float) w, (float) h);

        // 2. White key hover highlight if applicable
        if (hoveredNote >= minMidiNote && hoveredNote <= maxMidiNote && ! isBlackNote (hoveredNote))
        {
            const float x1 = noteToX ((float) hoveredNote - 0.5f);
            const float x2 = noteToX ((float) hoveredNote + 0.5f);
            g.setColour (juce::Colour (0x6635c6f0));
            g.fillRect (x1, 0.0f, x2 - x1, (float) h);
        }

        // 3. Draw vertical hairline dividers between white keys (in the bottom 40% of the keyboard)
        g.setColour (juce::Colour (0x44000000));
        for (int n = minMidiNote; n <= maxMidiNote; ++n)
        {
            if (! isBlackNote (n))
            {
                const float xEdge = noteToX ((float) n + 0.5f);
                g.drawVerticalLine (juce::roundToInt (xEdge), blackKeyH, (float) h);
            }
        }

        // 4. Octave lines and C labels (C1..C10)
        for (int n = minMidiNote; n <= maxMidiNote; ++n)
        {
            if (n % 12 == 0) // C note
            {
                const float xCLeft = noteToX ((float) n - 0.5f);
                const float xCRight = noteToX ((float) n + 0.5f);

                // Subtle cyan octave marker
                g.setColour (juce::Colour (0x3335c6f0));
                g.drawVerticalLine (juce::roundToInt (xCLeft), 0.0f, (float) h);

                // C label (e.g. C1, C2, C3, ..., C10)
                const int octave = (n / 12) - 1;
                g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
                g.setColour (juce::Colour (0xff1b2226));
                g.drawText ("C" + juce::String (octave), xCLeft - 2.0f, (float) h - 14.0f, (xCRight - xCLeft) + 4.0f, 12.0f, juce::Justification::centred);
            }
        }

        // 5. Paint Black keys
        for (int n = minMidiNote; n <= maxMidiNote; ++n)
        {
            if (isBlackNote (n))
            {
                const float x1 = noteToX ((float) n - 0.5f) + 0.5f;
                const float x2 = noteToX ((float) n + 0.5f) - 0.5f;
                const float keyW = juce::jmax (2.0f, x2 - x1);

                auto keyRect = juce::Rectangle<float> (x1, 0.0f, keyW, blackKeyH);

                // Drop shadow
                g.setColour (juce::Colours::black.withAlpha (0.45f));
                g.fillRect (keyRect.translated (0.5f, 1.0f));

                // Black key fill gradient
                if (n == hoveredNote)
                {
                    g.setColour (juce::Colour (0xff35c6f0));
                    g.fillRoundedRectangle (keyRect, 1.5f);
                }
                else
                {
                    juce::ColourGradient grad (juce::Colour (0xff252c30), x1, 0.0f,
                                               juce::Colour (0xff0a0d0e), x1, blackKeyH, false);
                    g.setGradientFill (grad);
                    g.fillRoundedRectangle (keyRect, 1.5f);
                    g.setColour (juce::Colour (0x25ffffff));
                    g.drawRoundedRectangle (keyRect, 1.5f, 0.5f);
                }
            }
        }

        // 6. Top border line separating graph from keys
        g.setColour (juce::Colour (0xff111517));
        g.drawHorizontalLine (0, 0.0f, (float) w);
        g.setColour (juce::Colour (0x20ffffff));
        g.drawHorizontalLine (1, 0.0f, (float) w);

        // 7. Tooltip badge for hovered note
        if (hoveredNote >= minMidiNote && hoveredNote <= maxMidiNote)
        {
            const float freq = frequencyForNote (hoveredNote);
            juce::String tip = noteName (hoveredNote) + "  "
                               + (freq >= 1000.0f ? juce::String (freq / 1000.0f, 2) + " kHz"
                                                  : juce::String (freq, 1) + " Hz");

            const float centerX = noteToX ((float) hoveredNote);
            const float badgeW = 92.0f;
            const float badgeH = 18.0f;
            const float badgeX = juce::jlimit (4.0f, (float) w - badgeW - 4.0f, centerX - badgeW * 0.5f);
            const float badgeY = 2.0f;

            auto badge = juce::Rectangle<float> (badgeX, badgeY, badgeW, badgeH);
            g.setColour (juce::Colour (0xee0e1418));
            g.fillRoundedRectangle (badge, 3.0f);
            g.setColour (juce::Colour (0xff35c6f0));
            g.drawRoundedRectangle (badge, 3.0f, 1.0f);

            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.setColour (juce::Colours::white);
            g.drawText (tip, badge, juce::Justification::centred);
        }
    }

private:
    int hoveredNote = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NoteKeyboardStrip)
};
