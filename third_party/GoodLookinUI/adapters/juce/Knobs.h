// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marcos Deida
#pragma once
#include <goodlookinui/Design.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>

// Procedural knob classes (see goodlookinui::KnobStyle). Every knob is drawn
// into a square of diameter 2*r centred on c, pointing at angle a (radians,
// 0 = straight up, sweep +-0.8*pi). `accent` is the cap colour for capped
// styles and the indicator colour for material styles.
namespace goodlookinui::juce_adapter::knobs {
using juce::Colour;
using juce::Point;

inline constexpr float pi = juce::MathConstants<float>::pi;

struct Ctx {
    juce::Graphics& g;
    Point<float> c;
    float r, a;
    Colour accent;
    bool darkMarks = false;   // printed marks are drawn dark (for light plates) instead of light
};

// Printed marks must stay readable on any plate. A mark colour that is light is swapped for a dark one when the
// plate is light, and a dark one for a light one when the plate is dark; mid/coloured marks keep their hue.
inline Colour markColour(const Ctx& k, Colour c) {
    const float b = c.getPerceivedBrightness();
    if (k.darkMarks && b > 0.45f) return Colour(0xff252a2d).withAlpha(c.getFloatAlpha());
    if (!k.darkMarks && b < 0.30f) return Colour(0xffe6eaec).withAlpha(c.getFloatAlpha());
    return c;
}

inline Point<float> dir(float a) { return {std::sin(a), -std::cos(a)}; }
inline Point<float> at(const Ctx& k, float angle, float radius) { return k.c + dir(angle) * radius; }

inline void disc(const Ctx& k, float radius, Colour top, Colour bottom, Point<float> centre = {0, 0}, bool offset = false) {
    auto c = offset ? centre : k.c;
    k.g.setGradientFill(juce::ColourGradient(top, c.x - radius, c.y - radius, bottom, c.x + radius, c.y + radius, false));
    k.g.fillEllipse(c.x - radius, c.y - radius, radius * 2, radius * 2);
}
inline void rim(const Ctx& k, float radius, Colour col, float w = 0.8f) {
    k.g.setColour(col);
    k.g.drawEllipse(k.c.x - radius, k.c.y - radius, radius * 2, radius * 2, w);
}
inline void shadow(const Ctx& k, float radius) {
    for (int n = 5; n > 0; --n) {
        k.g.setColour(Colour(0x0b000000));
        const float e = radius + float(n) * 0.8f;
        k.g.fillEllipse(k.c.x - e + 1, k.c.y - e + 3, e * 2, e * 2);
    }
}
// Fixed printed marks over the knob's sweep.
inline void dots(const Ctx& k, float radiusFrac, int n, float size, Colour col) {
    k.g.setColour(markColour(k, col));
    for (int i = 0; i < n; ++i) {
        auto p = at(k, (-0.8f + 1.6f * float(i) / float(n - 1)) * pi, k.r * radiusFrac);
        k.g.fillEllipse(p.x - size * 0.5f, p.y - size * 0.5f, size, size);
    }
}
inline void line(const Ctx& k, float from, float to, float w, Colour col, float angle) {
    k.g.setColour(Colour(0x66000000));
    k.g.drawLine({at(k, angle, from) + Point<float>(0.6f, 0.8f), at(k, angle, to) + Point<float>(0.6f, 0.8f)}, w + 0.6f);
    k.g.setColour(col);
    k.g.drawLine({at(k, angle, from), at(k, angle, to)}, w);
}
inline void flutes(const Ctx& k, float r0, float r1, int n, Colour dark, Colour light, float w) {
    for (int i = 0; i < n; ++i) {
        const float t = k.a + float(i) * juce::MathConstants<float>::twoPi / float(n);
        const auto v = dir(t);
        const float l = juce::jlimit(0.0f, 1.0f, 0.5f - 0.35f * (v.x + v.y));
        k.g.setColour(dark.interpolatedWith(light, l));
        k.g.drawLine({k.c + v * r0, k.c + v * r1}, w);
    }
}
inline void gloss(const Ctx& k, float radius, float strength = 0.22f) {
    auto r = juce::Rectangle<float>(radius * 1.2f, radius * 0.7f).withCentre({k.c.x - radius * 0.25f, k.c.y - radius * 0.45f});
    k.g.setGradientFill(juce::ColourGradient(Colour::fromFloatRGBA(1, 1, 1, strength), r.getX(), r.getY(),
                                             Colour::fromFloatRGBA(1, 1, 1, 0), r.getX(), r.getBottom(), false));
    k.g.fillEllipse(r);
}
inline juce::Path wedge(const Ctx& k, float baseHalf, float tip, float tipHalf, float angle) {
    const auto v = dir(angle);
    const Point<float> n(-v.y, v.x);
    juce::Path p;
    p.startNewSubPath(k.c + n * baseHalf);
    p.lineTo(k.c + v * tip + n * tipHalf);
    p.lineTo(k.c + v * tip - n * tipHalf);
    p.lineTo(k.c - n * baseHalf);
    p.closeSubPath();
    return p;
}

// N: chrome collar, grey fluted grip, flat cap, white dotted ring.
inline void drawN(const Ctx& k) {
    auto& g = k.g;
    dots(k, 0.94f, 21, k.r * 0.05f, Colour(0xffe9eef2));
    const float R = k.r * 0.80f, G = R * 0.78f;
    shadow(k, R);
    disc(k, R, Colour(0xfff6f8f8), Colour(0xff656b6f));
    rim(k, R, Colour(0xff2b3033));
    disc(k, G, Colour(0xff9a9ea1), Colour(0xff45494c));
    flutes(k, G * 0.70f, G * 0.97f, 30, Colour(0xff34383b), Colour(0xffb4b8ba), 1.6f);
    const float C = R * 0.44f;
    disc(k, C, k.accent.brighter(0.2f), k.accent.darker(0.4f));
    rim(k, C, k.accent.darker(0.7f));
    g.setColour(Colour(0xff0a0b0c));
    g.drawLine({at(k, k.a, G * 1.01f), at(k, k.a, R * 0.99f)}, k.r * 0.05f);
}

// A: flared silver crown with a large flat matte cap (a pointer lug on top, small side lugs).
inline void drawA(const Ctx& k) {
    auto& g = k.g;
    dots(k, 0.97f, 21, k.r * 0.05f, k.accent.brighter(0.55f));
    const float R = k.r * 0.74f;
    auto body = wedge(k, R * 0.92f, k.r * 0.97f, k.r * 0.20f, k.a);
    const Point<float> lugA = at(k, k.a + 1.72f, R * 1.0f), lugB = at(k, k.a - 1.72f, R * 1.0f);
    for (int n = 3; n > 0; --n) {                                          // contact shadow follows the shape
        g.setColour(Colour(0x16000000));
        const auto t = juce::AffineTransform::translation(float(n) * 0.6f, float(n) * 1.1f);
        g.fillPath(body, t); g.fillEllipse(k.c.x - R + float(n) * 0.6f, k.c.y - R + float(n) * 1.1f, R * 2, R * 2);
    }
    g.setGradientFill(juce::ColourGradient(Colour(0xfff1f3f3), k.c.x - R, k.c.y - R, Colour(0xff8a9195), k.c.x + R, k.c.y + R, false));
    g.fillPath(body); g.fillEllipse(k.c.x - R, k.c.y - R, R * 2, R * 2);
    for (auto l : {lugA, lugB}) g.fillEllipse(l.x - R * 0.17f, l.y - R * 0.17f, R * 0.34f, R * 0.34f);
    g.setColour(Colour(0xff2a2f32)); g.strokePath(body, juce::PathStrokeType(1.0f)); g.drawEllipse(k.c.x - R, k.c.y - R, R * 2, R * 2, 1.0f);
    for (auto l : {lugA, lugB}) g.drawEllipse(l.x - R * 0.17f, l.y - R * 0.17f, R * 0.34f, R * 0.34f, 0.8f);
    g.setColour(Colour(0x55000000)); g.drawEllipse(k.c.x - R * 0.90f, k.c.y - R * 0.90f, R * 1.80f, R * 1.80f, 1.4f);   // groove around the cap
    const float C = R * 0.86f;
    g.setGradientFill(juce::ColourGradient(k.accent.brighter(0.10f), k.c.x - C, k.c.y - C, k.accent.darker(0.22f), k.c.x + C, k.c.y + C, false));
    g.fillEllipse(k.c.x - C, k.c.y - C, C * 2, C * 2);                        // flat matte: no gloss
    g.setColour(k.accent.darker(0.6f)); g.drawEllipse(k.c.x - C, k.c.y - C, C * 2, C * 2, 0.9f);
    g.setColour(Colour(0x1affffff)); g.drawEllipse(k.c.x - C * 0.96f, k.c.y - C * 0.96f, C * 1.92f, C * 1.92f, 0.7f);
    g.setColour(Colour(0x88000000)); g.drawLine({at(k, k.a, R * 0.80f), at(k, k.a, k.r * 0.93f)}, 1.2f);               // ridge on the pointer lug
}

// Mic: translucent crown pointer knob in the item colour with a white indicator.
inline void drawMic(const Ctx& k) {
    auto& g = k.g;
    dots(k, 0.97f, 21, k.r * 0.045f, Colour(0xffe9eef2));
    const float R = k.r * 0.70f;
    auto body = wedge(k, R * 0.90f, k.r * 0.96f, k.r * 0.24f, k.a);
    for (int n = 3; n > 0; --n) { g.setColour(Colour(0x16000000)); const auto t = juce::AffineTransform::translation(float(n) * 0.6f, float(n) * 1.1f); g.fillPath(body, t); g.fillEllipse(k.c.x - R + float(n) * 0.6f, k.c.y - R + float(n) * 1.1f, R * 2, R * 2); }
    g.setGradientFill(juce::ColourGradient(k.accent.brighter(0.55f), k.c.x - R, k.c.y - R, k.accent.darker(0.65f), k.c.x + R, k.c.y + R, false));
    g.fillPath(body); g.fillEllipse(k.c.x - R, k.c.y - R, R * 2, R * 2);
    g.setColour(k.accent.darker(0.8f)); g.strokePath(body, juce::PathStrokeType(1.0f)); g.drawEllipse(k.c.x - R, k.c.y - R, R * 2, R * 2, 1.0f);
    const float C = R * 0.62f;
    g.setGradientFill(juce::ColourGradient(k.accent.brighter(0.25f), k.c.x - C, k.c.y - C, k.accent.darker(0.5f), k.c.x + C, k.c.y + C, false)); g.fillEllipse(k.c.x - C, k.c.y - C, C * 2, C * 2);
    gloss(k, R, 0.22f);
    line(k, R * 0.05f, k.r * 0.93f, k.r * 0.06f, Colour(0xfff8f4e6), k.a);
}

// FS: pale-grey skirt, coloured cap, black pointer line.
inline void drawFS(const Ctx& k) {
    dots(k, 0.95f, 11, k.r * 0.04f, Colour(0xffdfe3e2));
    const float R = k.r * 0.84f;
    shadow(k, R);
    disc(k, R, Colour(0xffe9ebea), Colour(0xff8d9492));
    rim(k, R, Colour(0xff4a504f));
    const float C = R * 0.66f;
    disc(k, C, k.accent.brighter(0.25f), k.accent.darker(0.35f));
    rim(k, C, k.accent.darker(0.7f));
    gloss(k, C, 0.16f);
    line(k, R * 0.10f, R * 0.98f, k.r * 0.07f, Colour(0xff101213), k.a);
}

// Mtl: knurled aluminium body, brushed top, dark pointer line.
inline void drawMtl(const Ctx& k) {
    auto& g = k.g;
    dots(k, 0.95f, 11, k.r * 0.035f, Colour(0xffc7ccce));
    const float R = k.r * 0.86f, T = R * 0.76f;
    shadow(k, R);
    disc(k, R, Colour(0xffe4e7e9), Colour(0xff6b7378));
    flutes(k, R * 0.84f, R * 0.99f, 72, Colour(0xff4f575b), Colour(0xffeff1f2), 0.9f);
    rim(k, R, Colour(0xff2f3538));
    disc(k, T, Colour(0xfffafbfb), Colour(0xff9ea5a9));
    g.setColour(Colour(0x14000000));
    for (int i = 1; i <= 6; ++i) {
        const float e = T * float(i) / 6.0f;
        g.drawEllipse(k.c.x - e, k.c.y - e, e * 2, e * 2, 0.5f);
    }
    rim(k, T, Colour(0x66ffffff));
    line(k, T * 0.08f, T * 0.98f, k.r * 0.07f, k.accent.darker(0.2f), k.a);
}

inline void woodKnob(const Ctx& k, Colour side1, Colour side2, Colour top1, Colour top2, Colour grain) {
    auto& g = k.g;
    const float R = k.r * 0.86f, T = R * 0.80f;
    shadow(k, R);
    disc(k, R, side1, side2);
    rim(k, R, Colour(0xff25150a));
    disc(k, T, top1, top2);
    g.saveState();
    juce::Path clip;
    clip.addEllipse(k.c.x - T, k.c.y - T, T * 2, T * 2);
    g.reduceClipRegion(clip, {});
    g.addTransform(juce::AffineTransform::rotation(k.a, k.c.x, k.c.y));
    g.setColour(grain);
    for (int i = -5; i <= 5; ++i) {
        juce::Path grain;
        for (int s = 0; s <= 16; ++s) {
            const float x = k.c.x - T + T * 2 * float(s) / 16.0f;
            const float y = k.c.y + float(i) * T * 0.17f + std::sin(float(s) * 0.55f + float(i)) * T * 0.045f;
            s == 0 ? grain.startNewSubPath(x, y) : grain.lineTo(x, y);
        }
        g.strokePath(grain, juce::PathStrokeType(0.7f + 0.2f * float(i & 1)));
    }
    g.restoreState();
    rim(k, T, Colour(0x55ffffff));
    gloss(k, T, 0.20f);
    line(k, T * 0.16f, T * 0.92f, k.r * 0.065f, k.accent.brighter(0.3f), k.a);
}

// Wd: turned wood with grain and an inlaid pointer.
inline void drawWd(const Ctx& k) { woodKnob(k, Colour(0xffa97444), Colour(0xff3e2412), Colour(0xffc08855), Colour(0xff6d4421), Colour(0x40301808)); }
// Mpl: light maple.
inline void drawMpl(const Ctx& k) { woodKnob(k, Colour(0xffe6c590), Colour(0xff9a7442), Colour(0xfff3dcb0), Colour(0xffc29a64), Colour(0x387a5428)); }

// Bk: black bakelite teardrop pointer over a ribbed base.
inline void drawBk(const Ctx& k) {
    auto& g = k.g;
    const float R = k.r * 0.88f, T = k.r * 0.60f;
    shadow(k, R);
    disc(k, R, Colour(0xff323336), Colour(0xff09090a));
    flutes(k, R * 0.88f, R * 0.99f, 36, Colour(0xff050506), Colour(0xff4c4e52), 1.3f);
    auto drop = wedge(k, T * 0.96f, k.r * 0.97f, k.r * 0.07f, k.a);
    g.setGradientFill(juce::ColourGradient(Colour(0xff4a4b4f), k.c.x - T, k.c.y - T, Colour(0xff060607), k.c.x + T, k.c.y + T, false));
    g.fillPath(drop);
    g.fillEllipse(k.c.x - T, k.c.y - T, T * 2, T * 2);
    g.setColour(Colour(0x44ffffff));
    g.drawEllipse(k.c.x - T, k.c.y - T, T * 2, T * 2, 0.7f);
    gloss(k, T, 0.26f);
    line(k, T * 0.25f, k.r * 0.90f, k.r * 0.055f, k.accent, k.a);
}

// Pie: white serrated skirt around a black cap.
inline void drawPie(const Ctx& k) {
    auto& g = k.g;
    const float R = k.r * 0.90f, inner = R * 0.88f;
    shadow(k, R);
    juce::Path skirt;
    const int teeth = 24;
    for (int i = 0; i < teeth * 2; ++i) {
        const float t = k.a + float(i) * pi / float(teeth);
        auto p = k.c + dir(t) * (i % 2 ? inner : R);
        i == 0 ? skirt.startNewSubPath(p) : skirt.lineTo(p);
    }
    skirt.closeSubPath();
    g.setGradientFill(juce::ColourGradient(Colour(0xfff6f6f4), k.c.x - R, k.c.y - R, Colour(0xffaaaca9), k.c.x + R, k.c.y + R, false));
    g.fillPath(skirt);
    g.setColour(Colour(0xff3a3c3b));
    g.strokePath(skirt, juce::PathStrokeType(0.6f));
    const float C = R * 0.70f;
    disc(k, C, Colour(0xff2f3032), Colour(0xff060607));
    rim(k, C, Colour(0x55ffffff));
    gloss(k, C, 0.12f);
    line(k, C * 0.10f, R * 0.98f, k.r * 0.065f, k.accent, k.a);
}

// Snk: matte black cylinder with a fixed tick ring.
inline void drawSnk(const Ctx& k) {
    dots(k, 0.93f, 11, k.r * 0.05f, Colour(0xffb4b8ba));
    const float R = k.r * 0.78f;
    shadow(k, R);
    disc(k, R, Colour(0xff34363a), Colour(0xff0b0b0c));
    flutes(k, R * 0.90f, R * 0.99f, 48, Colour(0xff0a0a0b), Colour(0xff2f3134), 0.8f);
    rim(k, R * 0.88f, Colour(0x22ffffff));
    rim(k, R, Colour(0xff000000));
    gloss(k, R * 0.9f, 0.08f);
    line(k, R * 0.12f, R * 0.84f, k.r * 0.07f, k.accent.brighter(0.4f), k.a);
}

// Slv: silver cylinder on a dark skirt ring with dots.
inline void drawSlv(const Ctx& k) {
    const float R = k.r * 0.95f, T = k.r * 0.64f;
    shadow(k, R);
    disc(k, R, Colour(0xff595d60), Colour(0xff1b1e20));
    dots(k, 0.82f, 21, k.r * 0.05f, Colour(0xffe6e9ea));
    shadow(k, T);
    disc(k, T, Colour(0xfff4f6f6), Colour(0xff8c9397));
    flutes(k, T * 0.86f, T * 0.98f, 60, Colour(0xff6c7377), Colour(0xffe9ecee), 0.8f);
    rim(k, T, Colour(0xff30363a));
    disc(k, T * 0.78f, Colour(0xffffffff), Colour(0xffaab1b5));
    line(k, T * 0.10f, T * 0.76f, k.r * 0.075f, k.accent.darker(0.5f), k.a);
}

// Rd: round black body with a coloured cap and tick ring.
inline void drawRd(const Ctx& k) {
    dots(k, 0.95f, 11, k.r * 0.045f, k.accent.brighter(0.4f));
    const float R = k.r * 0.84f, C = R * 0.60f;
    shadow(k, R);
    disc(k, R, Colour(0xff3d3f43), Colour(0xff09090a));
    flutes(k, R * 0.74f, R * 0.98f, 36, Colour(0xff060607), Colour(0xff4b4d51), 1.4f);
    rim(k, R, Colour(0xff000000));
    disc(k, C, k.accent.brighter(0.25f), k.accent.darker(0.45f));
    rim(k, C, k.accent.darker(0.75f));
    gloss(k, C, 0.2f);
    line(k, R * 0.66f, R * 0.96f, k.r * 0.07f, Colour(0xfff1efe6), k.a);
}

// Ptr: flat-top tapered aluminium pointer knob.
inline void drawPtr(const Ctx& k) {
    auto& g = k.g;
    dots(k, 0.95f, 11, k.r * 0.035f, Colour(0xffc7ccce));
    const float R = k.r * 0.60f;
    shadow(k, R);
    auto body = wedge(k, R * 0.95f, k.r * 0.90f, k.r * 0.26f, k.a);
    g.setGradientFill(juce::ColourGradient(Colour(0xfff4f6f6), k.c.x - R, k.c.y - R, Colour(0xff7c8387), k.c.x + R, k.c.y + R, false));
    g.fillPath(body);
    g.setColour(Colour(0xff2f3538));
    g.strokePath(body, juce::PathStrokeType(0.7f));
    g.setGradientFill(juce::ColourGradient(Colour(0xfff4f6f6), k.c.x - R, k.c.y - R, Colour(0xff7c8387), k.c.x + R, k.c.y + R, false));
    g.fillEllipse(k.c.x - R, k.c.y - R, R * 2, R * 2);
    g.setColour(Colour(0xff2f3538));
    g.drawEllipse(k.c.x - R, k.c.y - R, R * 2, R * 2, 0.7f);
    gloss(k, R, 0.18f);
    line(k, R * 0.05f, k.r * 0.86f, k.r * 0.07f, k.accent.darker(0.6f), k.a);
}

// Dm: chunky glossy domed cap.
inline void drawDm(const Ctx& k) {
    auto& g = k.g;
    dots(k, 0.95f, 11, k.r * 0.04f, Colour(0xff9aa3a6));
    const float R = k.r * 0.80f;
    shadow(k, R);
    g.setGradientFill(juce::ColourGradient(k.accent.brighter(0.7f), k.c.x - R * 0.35f, k.c.y - R * 0.45f,
                                           k.accent.darker(0.75f), k.c.x + R, k.c.y + R, true));
    g.fillEllipse(k.c.x - R, k.c.y - R, R * 2, R * 2);
    rim(k, R, k.accent.darker(0.8f), 1.0f);
    gloss(k, R, 0.34f);
    auto p = at(k, k.a, R * 0.68f);
    const float d = k.r * 0.14f;
    g.setColour(Colour(0x66000000));
    g.fillEllipse(p.x - d * 0.5f + 0.6f, p.y - d * 0.5f + 0.8f, d, d);
    g.setColour(Colour(0xfffaf7ea));
    g.fillEllipse(p.x - d * 0.5f, p.y - d * 0.5f, d, d);
}

// Sq: square rounded keycap with a dished top and a legend line.
inline void drawSq(const Ctx& k) {
    auto& g = k.g;
    const float S = k.r * 0.78f;
    shadow(k, S * 1.1f);
    g.saveState();
    g.addTransform(juce::AffineTransform::rotation(k.a, k.c.x, k.c.y));
    auto outer = juce::Rectangle<float>(S * 2, S * 2).withCentre(k.c);
    g.setGradientFill(juce::ColourGradient(Colour(0xff3b3d41), outer.getX(), outer.getY(), Colour(0xff080809), outer.getRight(), outer.getBottom(), false));
    g.fillRoundedRectangle(outer, S * 0.28f);
    g.setColour(Colour(0xff000000));
    g.drawRoundedRectangle(outer, S * 0.28f, 1.0f);
    auto top = outer.reduced(S * 0.20f);
    g.setGradientFill(juce::ColourGradient(Colour(0xff0d0e10), top.getX(), top.getY(), Colour(0xff2d2f33), top.getRight(), top.getBottom(), false));
    g.fillRoundedRectangle(top, S * 0.2f);
    g.setColour(Colour(0x30ffffff));
    g.drawRoundedRectangle(top, S * 0.2f, 0.8f);
    g.setColour(Colour(0x66000000));
    g.drawLine(k.c.x + 0.6f, k.c.y - S * 0.1f + 0.8f, k.c.x + 0.6f, k.c.y - S * 0.84f + 0.8f, k.r * 0.09f);
    g.setColour(k.accent);
    g.drawLine(k.c.x, k.c.y - S * 0.1f, k.c.x, k.c.y - S * 0.84f, k.r * 0.09f);
    g.restoreState();
}

// Seg: segmented lit arc around a dark cylinder (bar-graph dash style).
inline void drawSeg(const Ctx& k) {
    auto& g = k.g;
    const int n = 21;
    const float prop = juce::jlimit(0.0f, 1.0f, (k.a / pi + 0.8f) / 1.6f);
    const float w = k.r * 0.11f;
    for (int i = 0; i < n; ++i) {
        const float f = (float(i) + 0.5f) / float(n);
        const float t = (-0.8f + 1.6f * f) * pi;
        const bool lit = f <= prop + 0.5f / float(n);
        const bool red = f > 0.88f;
        const Colour col = red ? Colour(0xffe4412c) : k.accent;
        if (lit) {
            g.setColour(col.withAlpha(0.28f));
            g.drawLine({at(k, t, k.r * 0.70f), at(k, t, k.r * 0.99f)}, w * 1.7f);
        }
        g.setColour(lit ? col : col.withAlpha(0.13f));
        g.drawLine({at(k, t, k.r * 0.74f), at(k, t, k.r * 0.96f)}, w);
    }
    const float R = k.r * 0.60f;
    shadow(k, R);
    disc(k, R, Colour(0xff2d2f33), Colour(0xff08090a));
    rim(k, R, Colour(0xff000000));
    rim(k, R * 0.88f, Colour(0x1affffff));
    gloss(k, R, 0.07f);
    line(k, R * 0.14f, R * 0.84f, k.r * 0.07f, k.accent.brighter(0.5f), k.a);
}

// Vfd: phosphor glass with a lit vector fan.
inline void drawVfd(const Ctx& k) {
    auto& g = k.g;
    const float R = k.r * 0.92f;
    shadow(k, R);
    disc(k, R, Colour(0xff10221a), Colour(0xff020504));
    rim(k, R, Colour(0xff435248), 1.2f);
    const float prop = juce::jlimit(0.0f, 1.0f, (k.a / pi + 0.8f) / 1.6f);
    const int n = 17;
    for (int i = 0; i < n; ++i) {
        const float f = float(i) / float(n - 1);
        const float t = (-0.8f + 1.6f * f) * pi;
        const float outer = k.r * (0.40f + 0.46f * f);
        const bool lit = f <= prop + 0.5f / float(n);
        if (lit) {
            g.setColour(k.accent.withAlpha(0.25f));
            g.drawLine({at(k, t, k.r * 0.22f), at(k, t, outer)}, 3.0f);
        }
        g.setColour(lit ? k.accent.brighter(0.2f) : k.accent.withAlpha(0.16f));
        g.drawLine({at(k, t, k.r * 0.22f), at(k, t, outer)}, 1.1f);
    }
    g.setColour(k.accent.withAlpha(0.5f));
    g.drawEllipse(k.c.x - k.r * 0.16f, k.c.y - k.r * 0.16f, k.r * 0.32f, k.r * 0.32f, 1.0f);
    gloss(k, R, 0.10f);
    line(k, k.r * 0.10f, k.r * 0.30f, k.r * 0.07f, k.accent.brighter(0.7f), k.a);
}

// Cyb: chamfered dark frame with a glowing outline and a notched ring.
inline void drawCyb(const Ctx& k) {
    auto& g = k.g;
    const float R = k.r * 0.90f, ch = R * 0.38f;
    juce::Path frame;
    frame.startNewSubPath(k.c.x - R + ch, k.c.y - R);
    frame.lineTo(k.c.x + R - ch, k.c.y - R);
    frame.lineTo(k.c.x + R, k.c.y - R + ch);
    frame.lineTo(k.c.x + R, k.c.y + R - ch);
    frame.lineTo(k.c.x + R - ch, k.c.y + R);
    frame.lineTo(k.c.x - R + ch, k.c.y + R);
    frame.lineTo(k.c.x - R, k.c.y + R - ch);
    frame.lineTo(k.c.x - R, k.c.y - R + ch);
    frame.closeSubPath();
    g.setColour(Colour(0x55000000));
    g.fillPath(frame, juce::AffineTransform::translation(1.5f, 3.0f));
    g.setGradientFill(juce::ColourGradient(Colour(0xff1b2024), k.c.x - R, k.c.y - R, Colour(0xff050607), k.c.x + R, k.c.y + R, false));
    g.fillPath(frame);
    g.setColour(k.accent.withAlpha(0.22f));
    g.strokePath(frame, juce::PathStrokeType(4.0f));
    g.setColour(k.accent);
    g.strokePath(frame, juce::PathStrokeType(1.4f));
    const float ring = R * 0.62f;
    rim(k, ring, k.accent.withAlpha(0.55f), 1.0f);
    g.setColour(k.accent.withAlpha(0.7f));
    for (int i = 0; i < 12; ++i) {
        const float t = k.a + float(i) * juce::MathConstants<float>::twoPi / 12.0f;
        g.drawLine({at(k, t, ring * 1.08f), at(k, t, ring * 1.22f)}, 1.1f);
    }
    line(k, ring * 0.18f, ring * 0.86f, k.r * 0.07f, k.accent.brighter(0.4f), k.a);
    auto tip = at(k, k.a, ring * 0.86f);
    g.fillEllipse(tip.x - k.r * 0.06f, tip.y - k.r * 0.06f, k.r * 0.12f, k.r * 0.12f);
}

// ---- Batch 3 ----------------------------------------------------------
inline void drawFat(const Ctx& k) {
    const float R = k.r * 0.90f;
    shadow(k, R);
    disc(k, R, Colour(0xff3c3d40), Colour(0xff050506));
    flutes(k, R * 0.80f, R * 0.99f, 40, Colour(0xff050506), Colour(0xff55575b), 1.4f);
    disc(k, R * 0.74f, Colour(0xff2d2e30), Colour(0xff09090a));
    rim(k, R * 0.74f, Colour(0x30ffffff));
    line(k, R * 0.08f, R * 0.97f, k.r * 0.11f, k.accent, k.a);
}
inline void drawPag(const Ctx& k) {
    auto& g = k.g;
    const float R = k.r * 0.88f;
    shadow(k, R);
    juce::Path oct;
    for (int i = 0; i < 8; ++i) {
        auto p = at(k, k.a + (float(i) + 0.5f) * pi / 4.0f, R);
        i == 0 ? oct.startNewSubPath(p) : oct.lineTo(p);
    }
    oct.closeSubPath();
    g.setGradientFill(juce::ColourGradient(Colour(0xff414246), k.c.x - R, k.c.y - R, Colour(0xff050506), k.c.x + R, k.c.y + R, false));
    g.fillPath(oct);
    g.setColour(Colour(0x44ffffff));
    g.strokePath(oct, juce::PathStrokeType(0.8f));
    flutes(k, R * 0.62f, R * 0.84f, 32, Colour(0xff050506), Colour(0xff4a4c50), 1.5f);
    disc(k, R * 0.56f, Colour(0xff3a3b3e), Colour(0xff060607));
    gloss(k, R * 0.56f, 0.28f);
    line(k, R * 0.30f, R * 0.54f, k.r * 0.07f, k.accent, k.a);
}
inline void drawLvr(const Ctx& k) {
    auto& g = k.g;
    dots(k, 0.95f, 11, k.r * 0.04f, Colour(0xffc7ccce));
    shadow(k, k.r * 0.5f);
    g.saveState();
    g.addTransform(juce::AffineTransform::rotation(k.a, k.c.x, k.c.y));
    auto bar = juce::Rectangle<float>(k.r * 0.34f, k.r * 1.30f).withCentre({k.c.x, k.c.y - k.r * 0.28f});
    g.setGradientFill(juce::ColourGradient(Colour(0xff4c4d51), bar.getX(), 0, Colour(0xff050506), bar.getRight(), 0, false));
    g.fillRoundedRectangle(bar, k.r * 0.08f);
    g.setColour(Colour(0x40ffffff));
    g.drawRoundedRectangle(bar, k.r * 0.08f, 0.7f);
    g.setColour(k.accent);
    g.fillRect(juce::Rectangle<float>(k.r * 0.08f, k.r * 0.32f).withCentre({k.c.x, k.c.y - k.r * 0.74f}));
    g.restoreState();
    disc(k, k.r * 0.30f, Colour(0xff3e3f43), Colour(0xff060607));
    rim(k, k.r * 0.30f, Colour(0x44ffffff));
}
inline void drawTab(const Ctx& k) {
    auto& g = k.g;
    dots(k, 0.95f, 11, k.r * 0.04f, Colour(0xffc7ccce));
    const float R = k.r * 0.50f;
    shadow(k, R);
    auto body = wedge(k, R * 0.5f, k.r * 0.92f, k.r * 0.10f, k.a);
    g.setGradientFill(juce::ColourGradient(Colour(0xff4a4b4f), k.c.x - R, k.c.y - R, Colour(0xff060607), k.c.x + R, k.c.y + R, false));
    g.fillPath(body);
    disc(k, R, Colour(0xff4a4b4f), Colour(0xff060607));
    rim(k, R, Colour(0x44ffffff));
    line(k, R * 0.2f, k.r * 0.88f, k.r * 0.05f, k.accent, k.a);
}
inline void drawWh(const Ctx& k) {
    const float R = k.r * 0.86f, C = R * 0.64f;
    shadow(k, R);
    disc(k, R, Colour(0xff3a3b3e), Colour(0xff060607));
    flutes(k, R * 0.74f, R * 0.99f, 44, Colour(0xff060607), Colour(0xff55575b), 1.3f);
    disc(k, C, Colour(0xfffbfbfa), Colour(0xff9ea3a5));
    rim(k, C, Colour(0xff2c3033));
    gloss(k, C, 0.15f);
    line(k, 0.0f, C * 0.96f, k.r * 0.07f, k.accent.darker(0.6f), k.a);
}
inline void drawKnl(const Ctx& k) {
    auto& g = k.g;
    const float R = k.r * 0.86f, C = R * 0.70f;
    shadow(k, R);
    disc(k, R, Colour(0xff35363a), Colour(0xff050506));
    flutes(k, R * 0.70f, R * 0.99f, 64, Colour(0xff050506), Colour(0xff5a5c60), 1.2f);
    disc(k, C, Colour(0xfffcfdfd), Colour(0xff8f969a));
    g.setColour(Colour(0x14000000));
    for (int i = 1; i <= 5; ++i) {
        const float e = C * float(i) / 5.0f;
        g.drawEllipse(k.c.x - e, k.c.y - e, e * 2, e * 2, 0.5f);
    }
    rim(k, C, Colour(0xff2c3033));
    line(k, C * 0.10f, C * 0.96f, k.r * 0.07f, k.accent.darker(0.4f), k.a);
}
inline void drawSkt(const Ctx& k) {
    dots(k, 0.95f, 11, k.r * 0.04f, Colour(0xffc7ccce));
    const float R = k.r * 0.86f;
    shadow(k, R);
    disc(k, R, Colour(0xff3a3b3e), Colour(0xff060607));
    flutes(k, R * 0.88f, R * 0.99f, 48, Colour(0xff050506), Colour(0xff4a4c50), 1.0f);
    disc(k, R * 0.78f, Colour(0xff2a2b2d), Colour(0xff0a0a0b));
    rim(k, R * 0.78f, Colour(0x2affffff));
    line(k, R * 0.32f, R * 0.99f, k.r * 0.08f, k.accent, k.a);
}
inline void drawRib(const Ctx& k) {
    const float R = k.r * 0.88f;
    shadow(k, R);
    disc(k, R, k.accent.brighter(0.2f), k.accent.darker(0.6f));
    flutes(k, R * 0.62f, R * 0.98f, 34, k.accent.darker(0.7f), k.accent.brighter(0.5f), 1.8f);
    disc(k, R * 0.58f, k.accent.brighter(0.15f), k.accent.darker(0.5f));
    rim(k, R * 0.58f, k.accent.darker(0.8f));
    gloss(k, R * 0.58f, 0.18f);
    line(k, R * 0.10f, R * 0.54f, k.r * 0.07f, Colour(0xfff7f4e8), k.a);
}
inline void drawChr(const Ctx& k) {
    auto& g = k.g;
    dots(k, 0.95f, 21, k.r * 0.04f, Colour(0xffe9eef2));
    const float R = k.r * 0.82f, C = R * 0.66f;
    shadow(k, R);
    disc(k, R, Colour(0xfffdfdfd), Colour(0xff52585c));
    g.setColour(Colour(0x55000000));
    g.drawEllipse(k.c.x - R * 0.9f, k.c.y - R * 0.9f, R * 1.8f, R * 1.8f, 1.2f);
    rim(k, R, Colour(0xff25292b));
    disc(k, C, Colour(0xff4a4c50), Colour(0xff030304));
    rim(k, C, Colour(0xff000000));
    gloss(k, C, 0.45f);
    line(k, C * 0.10f, R * 0.96f, k.r * 0.065f, Colour(0xfff5f2e6), k.a);
}
inline void arcStroke(const Ctx& k, float rad, float from, float to, float w, Colour col) {
    if (to <= from + 0.001f) return;
    juce::Path p;
    p.addCentredArc(k.c.x, k.c.y, rad, rad, 0.0f, from, to, true);
    k.g.setColour(col);
    k.g.strokePath(p, juce::PathStrokeType(w, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
}
inline void drawArc(const Ctx& k) {
    const float lo = -0.8f * pi, hi = 0.8f * pi, w = k.r * 0.17f, rad = k.r * 0.84f;
    const float split = lo + (hi - lo) * 0.86f;
    arcStroke(k, rad, lo, hi, w, k.accent.withAlpha(0.14f));
    arcStroke(k, rad, lo, juce::jmin(k.a, split), w * 1.6f, k.accent.withAlpha(0.22f));
    arcStroke(k, rad, lo, juce::jmin(k.a, split), w, k.accent);
    arcStroke(k, rad, split, k.a, w, Colour(0xffe4412c));
    const float R = k.r * 0.60f;
    shadow(k, R);
    disc(k, R, Colour(0xff2a2c30), Colour(0xff08090a));
    rim(k, R, Colour(0xff000000));
    gloss(k, R, 0.07f);
    line(k, R * 0.14f, R * 0.84f, k.r * 0.07f, k.accent.brighter(0.5f), k.a);
}
inline void drawBrg(const Ctx& k) {
    auto& g = k.g;
    const int n = 14;
    const float prop = juce::jlimit(0.0f, 1.0f, (k.a / pi + 0.8f) / 1.6f);
    for (int i = 0; i < n; ++i) {
        const float f = (float(i) + 0.5f) / float(n);
        const float t = (-0.8f + 1.6f * f) * pi;
        const float len = k.r * (0.10f + 0.26f * f);
        const bool lit = f <= prop + 0.5f / float(n);
        const Colour col = f > 0.88f ? Colour(0xffe4412c) : k.accent;
        g.setColour(lit ? col : col.withAlpha(0.14f));
        g.drawLine({at(k, t, k.r * 0.64f), at(k, t, k.r * 0.64f + len)}, k.r * 0.10f);
    }
    const float R = k.r * 0.54f;
    shadow(k, R);
    disc(k, R, Colour(0xff2a2c30), Colour(0xff08090a));
    rim(k, R, Colour(0xff000000));
    line(k, R * 0.14f, R * 0.84f, k.r * 0.07f, k.accent.brighter(0.5f), k.a);
}
inline void drawNeo(const Ctx& k) {
    auto& g = k.g;
    const float lo = -0.8f * pi, rad = k.r * 0.80f;
    disc(k, rad, Colour(0xff0a0f14), Colour(0xff020305));
    arcStroke(k, rad, lo, 0.8f * pi, k.r * 0.04f, k.accent.withAlpha(0.25f));
    arcStroke(k, rad, lo, k.a, k.r * 0.16f, k.accent.withAlpha(0.20f));
    arcStroke(k, rad, lo, k.a, k.r * 0.05f, k.accent);
    rim(k, rad * 0.62f, k.accent.withAlpha(0.35f), 0.8f);
    auto p = at(k, k.a, rad);
    g.setColour(k.accent.withAlpha(0.35f));
    g.fillEllipse(p.x - k.r * 0.13f, p.y - k.r * 0.13f, k.r * 0.26f, k.r * 0.26f);
    g.setColour(k.accent.brighter(0.7f));
    g.fillEllipse(p.x - k.r * 0.07f, p.y - k.r * 0.07f, k.r * 0.14f, k.r * 0.14f);
    line(k, rad * 0.20f, rad * 0.58f, k.r * 0.04f, k.accent.withAlpha(0.8f), k.a);
}
inline void drawHex(const Ctx& k) {
    auto& g = k.g;
    const float R = k.r * 0.92f;
    juce::Path hx;
    for (int i = 0; i < 6; ++i) {
        auto p = at(k, float(i) * pi / 3.0f, R);
        i == 0 ? hx.startNewSubPath(p) : hx.lineTo(p);
    }
    hx.closeSubPath();
    g.setColour(Colour(0x55000000));
    g.fillPath(hx, juce::AffineTransform::translation(1.5f, 3.0f));
    g.setGradientFill(juce::ColourGradient(k.accent.darker(0.8f), k.c.x - R, k.c.y - R, Colour(0xff050607), k.c.x + R, k.c.y + R, false));
    g.fillPath(hx);
    g.setColour(k.accent.withAlpha(0.25f));
    g.strokePath(hx, juce::PathStrokeType(4.0f));
    g.setColour(k.accent);
    g.strokePath(hx, juce::PathStrokeType(1.3f));
    juce::Path inner;
    inner.addPolygon(k.c, 6, R * 0.62f, pi * 0.0f);
    g.setColour(k.accent.withAlpha(0.45f));
    g.strokePath(inner, juce::PathStrokeType(0.9f));
    line(k, R * 0.12f, R * 0.60f, k.r * 0.07f, k.accent.brighter(0.5f), k.a);
}
inline void drawYel(const Ctx& k) {
    auto& g = k.g;
    const float S = k.r * 0.90f;
    auto outer = juce::Rectangle<float>(S * 2, S * 2).withCentre(k.c);
    g.setColour(Colour(0x55000000));
    g.fillRoundedRectangle(outer.translated(1.5f, 3.0f), S * 0.30f);
    g.setGradientFill(juce::ColourGradient(k.accent.brighter(0.2f), outer.getX(), outer.getY(), k.accent.darker(0.5f), outer.getRight(), outer.getBottom(), false));
    g.fillRoundedRectangle(outer, S * 0.30f);
    g.setColour(k.accent.darker(0.8f));
    g.drawRoundedRectangle(outer, S * 0.30f, 1.0f);
    auto screen = outer.reduced(S * 0.20f);
    g.setColour(Colour(0xff06090b));
    g.fillRoundedRectangle(screen, S * 0.18f);
    g.setColour(Colour(0xff3ae6ee).withAlpha(0.35f));
    g.drawRoundedRectangle(screen, S * 0.18f, 1.0f);
    const float ring = screen.getWidth() * 0.34f;
    g.setColour(Colour(0xff3ae6ee).withAlpha(0.45f));
    g.drawEllipse(k.c.x - ring, k.c.y - ring, ring * 2, ring * 2, 0.9f);
    line(k, ring * 0.2f, ring, k.r * 0.07f, Colour(0xff6cf2f8), k.a);
}
inline void drawKey(const Ctx& k) {
    const float R = k.r * 0.80f;
    shadow(k, R);
    disc(k, R, Colour(0xff3a3c40), Colour(0xff070708));
    rim(k, R, Colour(0xff000000));
    disc(k, R * 0.76f, Colour(0xff0b0c0d), Colour(0xff2b2d31));
    rim(k, R * 0.76f, Colour(0x2affffff));
    line(k, R * 0.10f, R * 0.66f, k.r * 0.09f, k.accent, k.a);
}
inline void drawBrz(const Ctx& k) {
    const float R = k.r * 0.84f, T = R * 0.76f;
    shadow(k, R);
    disc(k, R, Colour(0xfff3d684), Colour(0xff6f4d14));
    flutes(k, R * 0.82f, R * 0.99f, 56, Colour(0xff5a3d0e), Colour(0xfffbe9a8), 1.1f);
    rim(k, R, Colour(0xff3d2907));
    disc(k, T, Colour(0xfffff0b4), Colour(0xff9a7122));
    rim(k, T, Colour(0x66ffffff));
    gloss(k, T, 0.25f);
    line(k, T * 0.1f, T * 0.96f, k.r * 0.07f, k.accent.darker(0.7f), k.a);
}
inline void drawChk(const Ctx& k) {
    auto& g = k.g;
    dots(k, 0.95f, 11, k.r * 0.04f, Colour(0xffc7ccce));
    const float R = k.r * 0.58f;
    shadow(k, R);
    auto body = wedge(k, R * 0.96f, k.r * 0.92f, k.r * 0.22f, k.a);
    g.setGradientFill(juce::ColourGradient(Colour(0xff4c4d51), k.c.x - R, k.c.y - R, Colour(0xff050506), k.c.x + R, k.c.y + R, false));
    g.fillPath(body);
    g.setColour(Colour(0x44ffffff));
    g.strokePath(body, juce::PathStrokeType(0.7f));
    disc(k, R, Colour(0xff4c4d51), Colour(0xff050506));
    rim(k, R, Colour(0x33ffffff));
    gloss(k, R, 0.22f);
    line(k, R * 0.05f, k.r * 0.88f, k.r * 0.06f, k.accent, k.a);
}
inline void drawIvr(const Ctx& k) {
    const float R = k.r * 0.86f, T = R * 0.74f;
    shadow(k, R);
    disc(k, R, Colour(0xfff6efd8), Colour(0xffa89c76));
    flutes(k, R * 0.76f, R * 0.99f, 36, Colour(0xff857a58), Colour(0xfffffbea), 1.4f);
    rim(k, R, Colour(0xff4d4630));
    disc(k, T, Colour(0xfffaf4de), Colour(0xffc2b78f));
    rim(k, T, Colour(0x66ffffff));
    gloss(k, T, 0.2f);
    line(k, T * 0.1f, T * 0.96f, k.r * 0.07f, k.accent.darker(0.7f), k.a);
}
inline void drawLed(const Ctx& k) {
    auto& g = k.g;
    const int n = 15;
    const float prop = juce::jlimit(0.0f, 1.0f, (k.a / pi + 0.8f) / 1.6f);
    for (int i = 0; i < n; ++i) {
        const float f = float(i) / float(n - 1);
        const bool lit = f <= prop + 0.5f / float(n);
        auto p = at(k, (-0.8f + 1.6f * f) * pi, k.r * 0.86f);
        const float d = k.r * 0.10f;
        if (lit) {
            g.setColour(k.accent.withAlpha(0.3f));
            g.fillEllipse(p.x - d, p.y - d, d * 2, d * 2);
        }
        g.setColour(lit ? k.accent.brighter(0.3f) : k.accent.withAlpha(0.16f));
        g.fillEllipse(p.x - d * 0.5f, p.y - d * 0.5f, d, d);
    }
    const float R = k.r * 0.66f;
    shadow(k, R);
    disc(k, R, Colour(0xff35373b), Colour(0xff060607));
    flutes(k, R * 0.88f, R * 0.99f, 40, Colour(0xff060607), Colour(0xff2f3134), 0.8f);
    rim(k, R, Colour(0xff000000));
    line(k, R * 0.14f, R * 0.84f, k.r * 0.07f, Colour(0xfff1efe6), k.a);
}
inline void drawDbl(const Ctx& k) {
    const float R = k.r * 0.90f, I = R * 0.52f;
    shadow(k, R);
    disc(k, R, Colour(0xfff2f4f4), Colour(0xff5d6367));
    flutes(k, R * 0.80f, R * 0.99f, 48, Colour(0xff4b5155), Colour(0xffe9ecee), 1.1f);
    rim(k, R, Colour(0xff2a2f32));
    line(k, I * 1.05f, R * 0.96f, k.r * 0.06f, Colour(0xff15181a), k.a);
    shadow(k, I);
    disc(k, I, Colour(0xff3e4044), Colour(0xff050506));
    flutes(k, I * 0.7f, I * 0.98f, 28, Colour(0xff050506), Colour(0xff5a5c60), 1.0f);
    disc(k, I * 0.62f, k.accent.brighter(0.2f), k.accent.darker(0.5f));
    rim(k, I * 0.62f, k.accent.darker(0.8f));
    line(k, 0.0f, I * 0.58f, k.r * 0.05f, Colour(0xfff7f4e8), k.a);
}
inline void drawMini(const Ctx& k) {
    dots(k, 0.92f, 11, k.r * 0.05f, Colour(0xffc7ccce));
    const float R = k.r * 0.46f;
    shadow(k, R);
    disc(k, R, Colour(0xff414246), Colour(0xff060607));
    rim(k, R, Colour(0xff000000));
    gloss(k, R, 0.12f);
    line(k, R * 0.1f, R * 0.88f, k.r * 0.09f, k.accent.brighter(0.6f), k.a);
}
inline void drawTl(const Ctx& k) {
    const float R = k.r * 0.80f;
    shadow(k, R);
    disc(k, R, Colour(0xff3a3b3e), Colour(0xff060607));
    flutes(k, R * 0.74f, R * 0.99f, 60, Colour(0xff060607), Colour(0xff55575b), 1.1f);
    disc(k, R * 0.70f, Colour(0xff2c2d30), Colour(0xff09090a));
    rim(k, R * 0.70f, Colour(0x2affffff));
    line(k, R * 0.0f, R * 0.96f, k.r * 0.08f, k.accent.brighter(0.6f), k.a);
}

// ---- Batch 4: neon / retro dash ------------------------------------------
inline void drawGlw(const Ctx& k) {
    auto& g = k.g;
    const float R = k.r * 0.70f;
    disc(k, R, Colour(0xff12181d), Colour(0xff020304));
    rim(k, R, k.accent.withAlpha(0.55f), 1.2f);
    const float lo = -0.8f * pi;
    juce::Path track; track.addCentredArc(k.c.x, k.c.y, k.r * 0.88f, k.r * 0.88f, 0, lo + juce::MathConstants<float>::halfPi * 0.0f, 0.8f * pi, true);
    g.setColour(k.accent.withAlpha(0.14f)); g.strokePath(track, juce::PathStrokeType(k.r * 0.07f));
    if (k.a > lo + 0.01f) {
        juce::Path val; val.addCentredArc(k.c.x, k.c.y, k.r * 0.88f, k.r * 0.88f, 0, lo, k.a, true);
        for (int n = 3; n >= 1; --n) { g.setColour(k.accent.withAlpha(0.07f * float(4 - n))); g.strokePath(val, juce::PathStrokeType(k.r * (0.07f + 0.07f * float(n)))); }
        g.setColour(k.accent); g.strokePath(val, juce::PathStrokeType(k.r * 0.07f));
        g.setColour(k.accent.brighter(0.8f)); g.strokePath(val, juce::PathStrokeType(k.r * 0.025f));
    }
    auto p = at(k, k.a, R * 0.86f);
    g.setColour(k.accent.withAlpha(0.35f)); g.fillEllipse(p.x - k.r * 0.12f, p.y - k.r * 0.12f, k.r * 0.24f, k.r * 0.24f);
    line(k, R * 0.30f, R * 0.88f, k.r * 0.06f, k.accent.brighter(0.7f), k.a);
    gloss(k, R, 0.07f);
}
inline void drawGrd(const Ctx& k) {
    auto& g = k.g;
    dots(k, 0.95f, 21, k.r * 0.04f, k.accent.withAlpha(0.8f));
    const float R = k.r * 0.80f;
    disc(k, R, Colour(0xff0a0f16), Colour(0xff020306));
    g.saveState();
    juce::Path clip; clip.addEllipse(k.c.x - R, k.c.y - R, R * 2, R * 2); g.reduceClipRegion(clip, {});
    g.setColour(k.accent.withAlpha(0.28f));
    for (int i = -4; i <= 4; ++i) g.drawLine(k.c.x + float(i) * R * 0.09f, k.c.y + R * 0.05f, k.c.x + float(i) * R * 0.5f, k.c.y + R, 0.8f);
    for (int j = 1; j <= 5; ++j) { const float y = k.c.y + R * 0.05f + std::pow(float(j) / 5.0f, 2.0f) * R * 0.95f; g.drawHorizontalLine(int(y), k.c.x - R, k.c.x + R); }
    g.setGradientFill(juce::ColourGradient(k.accent.withAlpha(0.35f), k.c.x, k.c.y + R * 0.05f, Colour(0x00000000), k.c.x, k.c.y - R * 0.6f, false));
    g.fillRect(k.c.x - R, k.c.y - R * 0.6f, R * 2, R * 0.65f);
    g.restoreState();
    rim(k, R, k.accent, 1.3f);
    line(k, R * 0.10f, R * 0.96f, k.r * 0.07f, k.accent.brighter(0.8f), k.a);
}
inline void drawDsh(const Ctx& k) {
    auto& g = k.g;
    const float prop = juce::jlimit(0.0f, 1.0f, (k.a / pi + 0.8f) / 1.6f);
    disc(k, k.r * 0.98f, Colour(0xff15110c), Colour(0xff050404));
    const int n = 16;
    for (int i = 0; i < n; ++i) {
        const float f = float(i) / float(n - 1), t = (-0.8f + 1.6f * f) * pi;
        const float len = k.r * (0.10f + 0.28f * f);
        const bool lit = f <= prop + 0.5f / float(n);
        const Colour col = f > 0.84f ? Colour(0xffff3b2a) : k.accent;
        g.setColour(lit ? col : col.withAlpha(0.12f));
        g.drawLine({at(k, t, k.r * 0.60f), at(k, t, k.r * 0.60f + len)}, k.r * 0.075f);
    }
    const float R = k.r * 0.50f;
    disc(k, R, Colour(0xff2a2c30), Colour(0xff070809));
    rim(k, R, Colour(0xff000000));
    line(k, R * 0.1f, R * 0.95f, k.r * 0.06f, Colour(0xffff8a26), k.a);
    disc(k, k.r * 0.10f, Colour(0xff9a9fa3), Colour(0xff2a2d30));
}

// Ebn: ebony wood with a brass cap and an inlaid pointer line.
inline void drawEbn(const Ctx& k) {
    auto& g = k.g;
    woodKnob(k, Colour(0xff3a2c22), Colour(0xff0b0806), Colour(0xff2b2018), Colour(0xff0e0a07), Colour(0x30c8a070));
    const float C = k.r * 0.86f * 0.80f * 0.46f;
    disc(k, C, Colour(0xfff1d98a), Colour(0xff8a6218)); rim(k, C, Colour(0xff4a3208), 1.0f);
    g.setColour(Colour(0x44000000)); for (int i = 1; i <= 3; ++i) { const float e = C * float(i) / 3.2f; g.drawEllipse(k.c.x - e, k.c.y - e, e * 2, e * 2, 0.5f); }
    gloss(k, C, 0.25f);
}
// Wnt: walnut top inside a chrome collar.
inline void drawWnt(const Ctx& k) {
    auto& g = k.g;
    dots(k, 0.96f, 21, k.r * 0.04f, Colour(0xffe9eef2));
    const float R = k.r * 0.82f, W = R * 0.78f;
    shadow(k, R);
    disc(k, R, Colour(0xfffcfdfd), Colour(0xff535a5e)); rim(k, R, Colour(0xff25292b));
    g.setColour(Colour(0x55000000)); g.drawEllipse(k.c.x - R * 0.90f, k.c.y - R * 0.90f, R * 1.8f, R * 1.8f, 1.0f);
    disc(k, W, Colour(0xff9a6a3e), Colour(0xff3a2210));
    g.saveState(); juce::Path clip; clip.addEllipse(k.c.x - W, k.c.y - W, W * 2, W * 2); g.reduceClipRegion(clip, {});
    g.addTransform(juce::AffineTransform::rotation(k.a, k.c.x, k.c.y)); g.setColour(Colour(0x40201008));
    for (int i = -5; i <= 5; ++i) { juce::Path grain; for (int s2 = 0; s2 <= 16; ++s2) { const float x = k.c.x - W + W * 2 * float(s2) / 16.0f, y = k.c.y + float(i) * W * 0.17f + std::sin(float(s2) * 0.6f + float(i)) * W * 0.05f; s2 == 0 ? grain.startNewSubPath(x, y) : grain.lineTo(x, y); } g.strokePath(grain, juce::PathStrokeType(0.8f)); }
    g.restoreState();
    rim(k, W, Colour(0x55ffffff)); gloss(k, W, 0.22f);
    line(k, W * 0.12f, W * 0.94f, k.r * 0.065f, k.accent.brighter(0.4f), k.a);
}

using Drawer = void (*)(const Ctx&);
// Order must match goodlookinui::KnobStyle.
inline constexpr Drawer drawers[] = {
    nullptr, drawN, drawA, drawFS, drawMtl, drawWd, drawBk, drawPie, drawSnk, drawSlv, drawRd,
    drawPtr, drawDm, drawSq, drawSeg, drawVfd, drawCyb,
    drawFat, drawPag, drawLvr, drawTab, drawWh, drawKnl, drawSkt, drawRib, drawChr, drawArc, drawBrg, drawNeo,
    drawHex, drawYel, drawKey, drawBrz, drawChk, drawIvr, drawMpl, drawLed, drawDbl, drawMini, drawTl,
    drawGlw, drawGrd, drawDsh, drawMic, drawEbn, drawWnt};
static_assert(std::size(drawers) == std::size(goodlookinui::knobStyles), "one drawer per knob style");
inline void draw(goodlookinui::KnobStyle style, const Ctx& k) {
    if (auto* d = drawers[int(style)]) d(k);
}
} // namespace goodlookinui::juce_adapter::knobs
