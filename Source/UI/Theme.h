#pragma once
#include <array>
#include <juce_gui_basics/juce_gui_basics.h>
#include <goodlookinui/Design.h>

// OpenEQ console palette: deep layered graphite, machined-metal panels,
// cyan signal glow. Band accents follow the stereo placement
// (Stereo=yellow, Mid=green, Side=blue, Left=white, Right=red) and tint the
// knob caps / LED bars so the console mirrors the curve.
namespace Theme
{
inline juce::Colour stereoCol = juce::Colour (0xffffd04a);
inline juce::Colour midCol    = juce::Colour (0xff7aed76);
inline juce::Colour sideCol   = juce::Colour (0xff5cb0ff);
inline juce::Colour leftCol   = juce::Colour (0xfff5f7fa);
inline juce::Colour rightCol  = juce::Colour (0xffff7a7a);
inline juce::Colour outputCol = juce::Colour (0xff35c6f0);
inline juce::Colour widthCol  = juce::Colour (0xff35c6f0);
inline juce::Colour signalGlow = juce::Colour (0xff35c6f0);
// Split view: the transient stream is green and the tonal stream is blue, in the graph and in the inspector.
inline juce::Colour splitTransCol = juce::Colour (0xff4ade80);
inline juce::Colour splitTonalCol = juce::Colour (0xff4aa3ff);

inline juce::Colour accentForStereoMode (int stereoMode)
{
    switch (stereoMode)
    {
        case 1:  return midCol;
        case 2:  return sideCol;
        case 3:  return leftCol;
        case 4:  return rightCol;
        case 5:  return juce::Colour (0xffb58cff); // Mid Side
        default: return stereoCol;
    }
}

// ---- chassis ----
inline juce::Colour chassisTop    = juce::Colour (0xff22282c);
inline juce::Colour chassisBottom = juce::Colour (0xff0d1114);
inline juce::Colour chassisRim    = juce::Colour (0xff060809);

// ---- panels (machined plate) ----
inline juce::Colour panelTop      = juce::Colour (0xff3a4248);
inline juce::Colour panelBottom   = juce::Colour (0xff262c31);
inline juce::Colour panelEdge     = juce::Colour (0xff101417);
inline juce::Colour panelHeavy    = juce::Colour (0xff161b1f);
inline juce::Colour wellFill      = juce::Colour (0xff1a2024);   // recessed wells
inline juce::Colour wellEdge      = juce::Colour (0xff080b0d);
inline juce::Colour graphBackground = juce::Colour (0xff0b0f11);   // graph / waterfall well

/** Waterfall intensity: black -> deep blue -> cyan -> yellow -> red, dimmed toward the back rows. */
inline juce::Colour waterfallColour (float level, float depth)
{
    juce::Colour c;
    if (level < 0.25f)       c = juce::Colours::black.interpolatedWith (juce::Colour (0xff12345e), level / 0.25f);
    else if (level < 0.5f)   c = juce::Colour (0xff12345e).interpolatedWith (juce::Colour (0xff2fd6c8), (level - 0.25f) / 0.25f);
    else if (level < 0.75f)  c = juce::Colour (0xff2fd6c8).interpolatedWith (juce::Colour (0xfff2d94e), (level - 0.5f) / 0.25f);
    else                     c = juce::Colour (0xfff2d94e).interpolatedWith (juce::Colour (0xffff5a3c), (level - 0.75f) / 0.25f);
    return c.withMultipliedAlpha (0.55f + 0.45f * (1.0f - depth));
}

// ---- text ----
inline juce::Colour textBright    = juce::Colour (0xffe8ece8);
inline juce::Colour textStd       = juce::Colour (0xffaab3b6);
inline juce::Colour textDim       = juce::Colour (0xff707a80);
inline juce::Colour labelColour   = juce::Colour (0xffcfd6d3);
inline juce::Colour valueText     = juce::Colour (0xffeef2f4);

// ---- numeric readout plates ----
// A readout is dark glass with light digits in every theme. The plate supplies the contrast on
// both graphite and light panels, so these three colours never change with the theme.
inline juce::Colour readoutFill = juce::Colour (0xff090c0e);
inline juce::Colour readoutEdge = juce::Colour (0x28ffffff);
inline juce::Colour readoutText = juce::Colour (0xffeef2f4);

// ---- control hardware ----
inline juce::Colour bezelHi       = juce::Colour (0xff6a7479);
inline juce::Colour bezelLo       = juce::Colour (0xff2e3438);
inline juce::Colour bodyHi        = juce::Colour (0xff4c555b);
inline juce::Colour bodyLo        = juce::Colour (0xff23292d);
inline juce::Colour capHi         = juce::Colour (0xff5d676c);
inline juce::Colour capLo         = juce::Colour (0xff394045);
inline juce::Colour pointerCol    = juce::Colour (0xfffff1c2);
inline juce::Colour flutingDark   = juce::Colour (0xff1b2023);

// ---- curve scope ----
inline juce::Colour scopeFill     = juce::Colour (0xff0c1116);
inline juce::Colour scopeBorder   = juce::Colour (0xff39444c);
inline juce::Colour scopeInner    = juce::Colour (0xff232b31);
inline juce::Colour gridMinor     = juce::Colour (0x14aab6bd);
inline juce::Colour gridMajor     = juce::Colour (0x28aab6bd);
inline juce::Colour zeroLine      = juce::Colour (0x3fc4d4da);
inline juce::Colour curveCol      = juce::Colour (0xff35c6f0);

inline int finish = 0;

// ---- compatibility aliases for ported components ----
inline juce::Colour accent        = signalGlow;
inline juce::Colour golden        = outputCol;
inline juce::Colour faceAccent    = outputCol;
inline juce::Colour editorTop           = chassisTop;
inline juce::Colour editorBottom        = chassisBottom;
inline juce::Colour panelOutline  = panelEdge;
inline juce::Colour panelHighlight = juce::Colour (0x2effffff);
inline juce::Colour textDark            = textBright;
inline juce::Colour textMid             = textStd;
inline juce::Colour buttonTop     = juce::Colour (0xff343c41);
inline juce::Colour buttonBottom  = juce::Colour (0xff20262a);
inline juce::Colour buttonOutline = juce::Colour (0xff0b0e10);
inline juce::Colour buttonText    = juce::Colour (0xffd5dcd9);
inline juce::Colour buttonInactiveTop    = juce::Colour (0xff2a3034);
inline juce::Colour buttonInactiveBottom = juce::Colour (0xff21262a);
inline juce::Colour buttonTextInactive   = juce::Colour (0xff767c80);
inline juce::Colour knobRim       = juce::Colour (0xff3b454a);
inline juce::Colour knobText      = valueText;
inline juce::Colour knobTextInactive = juce::Colour (0xff5f6569);
inline juce::Colour axisText      = juce::Colour (0x66ffffff);
inline juce::Colour curveActive   = curveCol;
inline juce::Colour curveMuted    = juce::Colour (0xff5d6a70);
inline juce::Colour preSpectrum   = juce::Colour (0x4fafb8b6);
inline juce::Colour postSpectrum  = juce::Colour (0x8f35c6f0);

// ---- themes ----
// The palette is set in one place. 0 is Graphite (the default), 1 is Light, 2 is High contrast.
// Components that copy a colour when they are built keep the colour they were built with until they are rebuilt.
// Per-band colours from a theme file. An alpha of zero means the default colour is used.
inline std::array<juce::Colour, 32> bandColourOverride {};

inline void applyTheme (int index)
{
    bandColourOverride.fill (juce::Colour());
    if (index == 1)
    {
        chassisTop = juce::Colour (0xffe6e9ec);  chassisBottom = juce::Colour (0xffd3d8dc);  chassisRim = juce::Colour (0xffb9c0c6);
        panelTop = juce::Colour (0xfff5f7f9);    panelBottom = juce::Colour (0xffe2e6ea);    panelEdge = juce::Colour (0xffb3bcc4);
        panelHeavy = juce::Colour (0xffeef1f4);  wellFill = juce::Colour (0xffdfe4e8);      wellEdge = juce::Colour (0xffc4ccd3);
        textBright = juce::Colour (0xff1c2327);  textStd = juce::Colour (0xff3d474e);       textDim = juce::Colour (0xff6c777f);
        labelColour = juce::Colour (0xff2a3237); valueText = juce::Colour (0xff10161a);
        bezelHi = juce::Colour (0xffd0d6db);     bezelLo = juce::Colour (0xff9aa3a9);
        bodyHi = juce::Colour (0xffe0e5e9);      bodyLo = juce::Colour (0xffb8c0c6);
        capHi = juce::Colour (0xffeef1f3);       capLo = juce::Colour (0xffc7cdd2);
        pointerCol = juce::Colour (0xff2a3237);  flutingDark = juce::Colour (0xffaeb6bc);
        scopeFill = juce::Colour (0xfff2f5f7);   scopeBorder = juce::Colour (0xffb5bec6);   scopeInner = juce::Colour (0xffe6eaee);
        gridMinor = juce::Colour (0x14000000);   gridMajor = juce::Colour (0x28000000);     zeroLine = juce::Colour (0x3f000000);
        buttonTop = juce::Colour (0xffe9edf0);   buttonBottom = juce::Colour (0xffd4dadf);  buttonOutline = juce::Colour (0xff9aa3aa);
        buttonText = juce::Colour (0xff1c2327);  buttonInactiveTop = juce::Colour (0xffd6dbdf); buttonInactiveBottom = juce::Colour (0xffc8ced4);
        buttonTextInactive = juce::Colour (0xff8c959b); knobRim = juce::Colour (0xffb3bcc3);
        knobTextInactive = juce::Colour (0xff9aa2a8); axisText = juce::Colour (0x88000000);
        curveMuted = juce::Colour (0xffa9b3b9);
    }
    else if (index == 2)
    {
        chassisTop = juce::Colour (0xff000000);  chassisBottom = juce::Colour (0xff000000);  chassisRim = juce::Colour (0xff000000);
        panelTop = juce::Colour (0xff1a1a1a);    panelBottom = juce::Colour (0xff101010);    panelEdge = juce::Colour (0xffffffff);
        panelHeavy = juce::Colour (0xff141414);  wellFill = juce::Colour (0xff000000);      wellEdge = juce::Colour (0xffffffff);
        textBright = juce::Colour (0xffffffff);  textStd = juce::Colour (0xffe6e6e6);       textDim = juce::Colour (0xffb0b0b0);
        labelColour = juce::Colour (0xffffffff); valueText = juce::Colour (0xffffffff);
        bezelHi = juce::Colour (0xffffffff);     bezelLo = juce::Colour (0xff404040);
        bodyHi = juce::Colour (0xff3a3a3a);      bodyLo = juce::Colour (0xff111111);
        capHi = juce::Colour (0xff4a4a4a);       capLo = juce::Colour (0xff1a1a1a);
        pointerCol = juce::Colour (0xffffffff);  flutingDark = juce::Colour (0xff000000);
        scopeFill = juce::Colour (0xff000000);   scopeBorder = juce::Colour (0xffffffff);   scopeInner = juce::Colour (0xff0a0a0a);
        gridMinor = juce::Colour (0x33ffffff);   gridMajor = juce::Colour (0x66ffffff);     zeroLine = juce::Colour (0xccffffff);
        buttonTop = juce::Colour (0xff202020);   buttonBottom = juce::Colour (0xff000000);  buttonOutline = juce::Colour (0xffffffff);
        buttonText = juce::Colour (0xffffffff);  buttonInactiveTop = juce::Colour (0xff101010); buttonInactiveBottom = juce::Colour (0xff000000);
        buttonTextInactive = juce::Colour (0xff909090); knobRim = juce::Colour (0xffffffff);
        knobTextInactive = juce::Colour (0xff909090); axisText = juce::Colour (0xccffffff);
        curveMuted = juce::Colour (0xff808080);
    }
    else
    {
        // Graphite: the stock palette.
        chassisTop = juce::Colour (0xff22282c);  chassisBottom = juce::Colour (0xff0d1114);  chassisRim = juce::Colour (0xff060809);
        panelTop = juce::Colour (0xff3a4248);    panelBottom = juce::Colour (0xff262c31);    panelEdge = juce::Colour (0xff101417);
        panelHeavy = juce::Colour (0xff161b1f);  wellFill = juce::Colour (0xff1a2024);      wellEdge = juce::Colour (0xff080b0d);
        textBright = juce::Colour (0xffe8ece8);  textStd = juce::Colour (0xffaab3b6);       textDim = juce::Colour (0xff707a80);
        labelColour = juce::Colour (0xffcfd6d3); valueText = juce::Colour (0xffeef2f4);
        bezelHi = juce::Colour (0xff6a7479);     bezelLo = juce::Colour (0xff2e3438);
        bodyHi = juce::Colour (0xff4c555b);      bodyLo = juce::Colour (0xff23292d);
        capHi = juce::Colour (0xff5d676c);       capLo = juce::Colour (0xff394045);
        pointerCol = juce::Colour (0xfffff1c2);  flutingDark = juce::Colour (0xff1b2023);
        scopeFill = juce::Colour (0xff0c1116);   scopeBorder = juce::Colour (0xff39444c);   scopeInner = juce::Colour (0xff232b31);
        gridMinor = juce::Colour (0x14aab6bd);   gridMajor = juce::Colour (0x28aab6bd);     zeroLine = juce::Colour (0x3fc4d4da);
        buttonTop = juce::Colour (0xff343c41);   buttonBottom = juce::Colour (0xff20262a);  buttonOutline = juce::Colour (0xff0b0e10);
        buttonText = juce::Colour (0xffd5dcd9);  buttonInactiveTop = juce::Colour (0xff2a3034); buttonInactiveBottom = juce::Colour (0xff21262a);
        buttonTextInactive = juce::Colour (0xff767c80); knobRim = juce::Colour (0xff3b454a);
        knobTextInactive = juce::Colour (0xff5f6569); axisText = juce::Colour (0x66ffffff);
        curveMuted = juce::Colour (0xff5d6a70);
    }

    // Aliases copy the base values above.
    golden = outputCol;
    faceAccent = outputCol;
    accent = signalGlow;
    editorTop = chassisTop;
    editorBottom = chassisBottom;
    panelOutline = panelEdge;
    textDark = textBright;
    textMid = textStd;
    knobText = valueText;
    curveActive = curveCol;
}

} // namespace Theme
