// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marcos Deida
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>

// Procedural meters, readouts, lamps, keys and faders. Everything is drawn at the
// component's current scale (no bitmaps). Levels are passed in as plain numbers so
// the same drawing works for any plugin.
namespace goodlookinui::juce_adapter::meters {
using juce::Colour;
using juce::Graphics;
using juce::Point;
using juce::Rectangle;

inline constexpr float pi = juce::MathConstants<float>::pi;

// ---- scales ---------------------------------------------------------------
// VU needle position (0..1) for an amplitude relative to the 0 VU reference.
// Deflection is proportional to voltage, with +3 VU at full scale.
inline float vuPosition(float amplitudeOverReference) {
    return juce::jlimit(0.0f, 1.0f, amplitudeOverReference * 0.7079f);
}
inline float vuPositionForDb(float vu) { return vuPosition(std::pow(10.0f, vu / 20.0f)); }
// 0..1 position on a plain dB scale.
inline float dbPosition(float db, float floorDb = -48.0f, float ceilDb = 0.0f) {
    return juce::jlimit(0.0f, 1.0f, (db - floorDb) / (ceilDb - floorDb));
}
inline Colour ladderColour(float db) {
    return db > -3.0f ? Colour(0xffe5382a) : db > -12.0f ? Colour(0xfff0b323) : Colour(0xff3ccf5a);
}

// ---- VU needle meter ------------------------------------------------------
enum class VuFace { Cream, Black };

inline void drawVuMeter(Graphics& g, Rectangle<float> r, float needle, VuFace face) {
    const bool cream = face == VuFace::Cream;
    g.setColour(Colour(0xff0b0c0d)); g.fillRoundedRectangle(r, 4);
    auto f = r.reduced(3);
    g.setGradientFill(cream ? juce::ColourGradient(Colour(0xfff7ecc4), f.getX(), f.getY(), Colour(0xffd9c88b), f.getX(), f.getBottom(), false)
                            : juce::ColourGradient(Colour(0xff2b2519), f.getX(), f.getY(), Colour(0xff0d0b08), f.getX(), f.getBottom(), false));
    g.fillRoundedRectangle(f, 2);
    g.saveState();
    juce::Path clip; clip.addRoundedRectangle(f, 2.0f); g.reduceClipRegion(clip, {});

    const float h = f.getHeight(), cx = f.getCentreX();
    const float R = h * 1.55f, arcTop = f.getY() + h * 0.30f, py = arcTop + R;
    const float span = juce::jmin(0.62f, (f.getWidth() * 0.43f) / R);          // half-angle in radians
    auto angleFor = [&](float pos) { return -span + 2.0f * span * pos; };
    auto pt = [&](float ang, float rad) { return Point<float>(cx + std::sin(ang) * rad, py - std::cos(ang) * rad); };
    const Colour ink = cream ? Colour(0xff1c1812) : Colour(0xffffb347);
    const Colour red = cream ? Colour(0xffc0281c) : Colour(0xffff6a3a);

    // red zone (0 .. +3 VU) and the main arc
    auto arc = [&](float from, float to, float rad, float w, Colour c) {
        juce::Path p; p.addCentredArc(cx, py, rad, rad, 0.0f, from, to, true);
        g.setColour(c); g.strokePath(p, juce::PathStrokeType(w, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    };
    arc(angleFor(0.0f), angleFor(vuPositionForDb(0.0f)), R, h * 0.025f, ink);
    arc(angleFor(vuPositionForDb(0.0f)), angleFor(1.0f), R, h * 0.07f, red);
    // ticks and numbers
    struct Mark { float vu; const char* text; bool major; };
    static const Mark marks[] = {{-20, "20", true}, {-10, "10", true}, {-7, "7", true}, {-5, "5", true}, {-3, "3", true},
                                 {-2, "2", false}, {-1, "1", false}, {0, "0", true}, {1, "1", false}, {2, "2", false}, {3, "3", true}};
    g.setFont(juce::FontOptions(h * 0.15f, juce::Font::bold));
    for (auto& m : marks) {
        const float a = angleFor(vuPositionForDb(m.vu));
        const Colour c = m.vu > 0 ? red : ink;
        g.setColour(c);
        g.drawLine({pt(a, R), pt(a, R - h * (m.major ? 0.11f : 0.07f))}, m.major ? 1.4f : 1.0f);
        auto lp = pt(a, R + h * 0.12f);
        g.drawText(m.text, Rectangle<float>(h * 0.4f, h * 0.2f).withCentre(lp), juce::Justification::centred);
    }
    g.setColour(ink); g.setFont(juce::FontOptions(h * 0.30f, juce::Font::bold));
    g.drawText("VU", f.removeFromBottom(h * 0.40f).withTrimmedRight(h * 0.15f), juce::Justification::centredRight);

    // needle with a soft shadow
    const float na = angleFor(juce::jlimit(0.0f, 1.0f, needle));
    g.setColour(Colour(0x44000000)); g.drawLine({pt(na, R * 0.45f) + Point<float>(1.5f, 1.5f), pt(na, R * 1.01f) + Point<float>(1.5f, 1.5f)}, 1.6f);
    g.setColour(cream ? Colour(0xff0f0d0a) : Colour(0xffff8a26)); g.drawLine({pt(na, R * 0.45f), pt(na, R * 1.01f)}, 1.5f);
    // glass sheen
    g.setGradientFill(juce::ColourGradient(Colour(0x30ffffff), 0, r.getY(), Colour(0x00ffffff), 0, r.getCentreY(), false));
    g.fillRect(r.withHeight(r.getHeight() * 0.5f));
    g.restoreState();
    g.setColour(Colour(0xff050505)); g.drawRoundedRectangle(r, 4, 1.5f);
}

// ---- PPM needle meter (broadcast-style 1..7 scale, 4 = reference) -------------
inline void drawPpm(Graphics& g, Rectangle<float> r, float level01) {
    g.setColour(Colour(0xff0b0c0d)); g.fillRoundedRectangle(r, 4);
    auto f = r.reduced(3);
    g.setGradientFill(juce::ColourGradient(Colour(0xfff2f2ee), f.getX(), f.getY(), Colour(0xffcfd1cb), f.getX(), f.getBottom(), false)); g.fillRoundedRectangle(f, 2);
    g.saveState(); juce::Path clip; clip.addRoundedRectangle(f, 2.0f); g.reduceClipRegion(clip, {});
    const float h = f.getHeight(), cx = f.getCentreX(), R = h * 1.55f, py = f.getY() + h * 0.30f + R;
    const float span = juce::jmin(0.62f, (f.getWidth() * 0.43f) / R);
    auto ang = [&](float p) { return -span + 2.0f * span * p; };
    auto pt = [&](float a, float rad) { return Point<float>(cx + std::sin(a) * rad, py - std::cos(a) * rad); };
    juce::Path arc; arc.addCentredArc(cx, py, R, R, 0, ang(0), ang(1), true);
    g.setColour(Colour(0xff1b1d1c)); g.strokePath(arc, juce::PathStrokeType(h * 0.025f));
    g.setFont(juce::FontOptions(h * 0.19f, juce::Font::bold));
    for (int i = 1; i <= 7; ++i) {                      // 6 dB between marks, mark 4 = 0 dBu reference
        const float p = float(i - 1) / 6.0f, a = ang(p);
        g.setColour(i == 7 ? Colour(0xffc0281c) : Colour(0xff1b1d1c));
        g.drawLine({pt(a, R), pt(a, R - h * 0.12f)}, 1.5f);
        g.drawText(juce::String(i), Rectangle<float>(h * 0.3f, h * 0.22f).withCentre(pt(a, R + h * 0.14f)), juce::Justification::centred);
    }
    g.setColour(Colour(0xff1b1d1c)); g.setFont(juce::FontOptions(h * 0.28f, juce::Font::bold));
    g.drawText("PPM", f.removeFromBottom(h * 0.38f).withTrimmedRight(h * 0.15f), juce::Justification::centredRight);
    const float na = ang(juce::jlimit(0.0f, 1.0f, level01));
    g.setColour(Colour(0x44000000)); g.drawLine({pt(na, R * 0.45f) + Point<float>(1.5f, 1.5f), pt(na, R * 1.01f) + Point<float>(1.5f, 1.5f)}, 1.6f);
    g.setColour(Colour(0xff0d0d0b)); g.drawLine({pt(na, R * 0.45f), pt(na, R * 1.01f)}, 1.5f);
    g.setGradientFill(juce::ColourGradient(Colour(0x30ffffff), 0, r.getY(), Colour(0x00ffffff), 0, r.getCentreY(), false)); g.fillRect(r.withHeight(r.getHeight() * 0.5f));
    g.restoreState(); g.setColour(Colour(0xff050505)); g.drawRoundedRectangle(r, 4, 1.5f);
}

// ---- LED ladder -----------------------------------------------------------
// levelDb / peakDb on a -48..0 dB scale. Lit segments use green / amber / red.
inline void drawLedLadder(Graphics& g, Rectangle<float> r, bool horizontal, float levelDb, float peakDb, int segments = 16) {
    const float gap = 1.5f;
    const float segLen = ((horizontal ? r.getWidth() : r.getHeight()) - gap * float(segments - 1)) / float(segments);
    const float peakIdx = std::floor(dbPosition(peakDb) * float(segments - 1) + 0.5f);
    for (int i = 0; i < segments; ++i) {
        const float db = -48.0f + 48.0f * float(i + 1) / float(segments);
        const bool lit = levelDb >= db - 48.0f / float(segments) * 0.5f || float(i) == peakIdx;
        const Colour c = ladderColour(db);
        Rectangle<float> s = horizontal ? Rectangle<float>(r.getX() + float(i) * (segLen + gap), r.getY(), segLen, r.getHeight())
                                        : Rectangle<float>(r.getX(), r.getBottom() - float(i + 1) * segLen - float(i) * gap, r.getWidth(), segLen);
        if (lit) { g.setColour(c.withAlpha(0.28f)); g.fillRoundedRectangle(s.expanded(1.2f), 2); }
        g.setColour(lit && (levelDb >= db - 1.5f || float(i) == peakIdx) ? c : lit ? c.darker(0.2f) : c.withAlpha(0.14f));
        g.fillRoundedRectangle(s, 1.2f);
    }
}

// Two tall ladders with a shared dB scale between them (L left, R right).
inline void drawStereoLadder(Graphics& g, Rectangle<float> r, float dbL, float dbR, float peakDb) {
    g.setColour(Colour(0xff0b0c0d)); g.fillRoundedRectangle(r, 4);
    auto b = r.reduced(10, 8); const float barW = juce::jmin(30.0f, b.getWidth() * 0.26f);
    auto lb = Rectangle<float>(b.getX() + b.getWidth() * 0.5f - barW - 16, b.getY(), barW, b.getHeight());
    auto rb = Rectangle<float>(b.getX() + b.getWidth() * 0.5f + 16, b.getY(), barW, b.getHeight());
    drawLedLadder(g, lb, false, dbL, peakDb, 24);
    drawLedLadder(g, rb, false, dbR, peakDb, 24);
    g.setColour(Colour(0xff8b9498)); g.setFont(juce::FontOptions(7.5f, juce::Font::bold));
    for (int d : {0, -6, -12, -24, -36, -48}) {
        const float y = b.getBottom() - dbPosition(float(d)) * b.getHeight();
        g.drawText(juce::String(d), Rectangle<float>(30, 9).withCentre({b.getCentreX(), juce::jlimit(b.getY() + 5, b.getBottom() - 5, y)}), juce::Justification::centred);
    }
    g.setFont(juce::FontOptions(8.0f, juce::Font::bold));
    g.drawText("L", lb.withY(r.getBottom() - 11).withHeight(10), juce::Justification::centred);
    g.drawText("R", rb.withY(r.getBottom() - 11).withHeight(10), juce::Justification::centred);
    g.setColour(Colour(0xff2a3033)); g.drawRoundedRectangle(r, 4, 1.0f);
}

// ---- digital meter faces --------------------------------------------------
// Lit arc of many segments (bar-graph dash style). level01 on 0..1.
inline void drawSegmentArc(Graphics& g, Rectangle<float> r, float level01, Colour lit) {
    const int n = 32;
    // a wide shallow arc that fills the face: centre sits below the bottom edge
    const float rad = r.getHeight() * 1.05f, half = std::asin(juce::jmin(0.98f, (r.getWidth() * 0.46f) / rad));
    const Point<float> centre(r.getCentreX(), r.getBottom() + rad * 0.18f);
    for (int i = 0; i < n; ++i) {
        const float f = (float(i) + 0.5f) / float(n), a = (-1.0f + 2.0f * f) * half;
        const bool on = f <= level01 + 0.5f / float(n);
        const Colour col = f > 0.86f ? Colour(0xffe4412c) : lit;
        auto dirv = Point<float>(std::sin(a), -std::cos(a));
        auto p0 = centre + dirv * (rad * 0.70f), p1 = centre + dirv * (rad * (0.82f + 0.16f * f));
        if (on) { g.setColour(col.withAlpha(0.25f)); g.drawLine({p0, p1}, r.getWidth() / float(n) * 0.95f); }
        g.setColour(on ? col : col.withAlpha(0.13f)); g.drawLine({p0, p1}, r.getWidth() / float(n) * 0.55f);
    }
}
// Fan of vector lines, lit up to level01 (phosphor display style).
inline void drawVectorFan(Graphics& g, Rectangle<float> r, float level01, Colour lit) {
    g.setColour(Colour(0xff031009)); g.fillRoundedRectangle(r, 4);
    auto b = r.reduced(6); const int n = 26;
    for (int i = 0; i < n; ++i) {
        const float f = float(i) / float(n - 1), x = b.getX() + f * b.getWidth();
        const float hgt = b.getHeight() * (0.28f + 0.72f * f);
        const bool on = f <= level01 + 0.5f / float(n);
        if (on) { g.setColour(lit.withAlpha(0.25f)); g.drawLine(x, b.getBottom(), x, b.getBottom() - hgt, 3.4f); }
        g.setColour(on ? lit.brighter(0.2f) : lit.withAlpha(0.14f)); g.drawLine(x, b.getBottom(), x, b.getBottom() - hgt, 1.2f);
    }
    g.setColour(Colour(0x22ffffff)); g.drawRoundedRectangle(r, 4, 1.0f);
}

// ---- 7-segment readout ----------------------------------------------------
// Draws text made of digits, '-', '+', ' ' and '.' (a dot attaches to the digit before it).
inline void drawSevenSegment(Graphics& g, Rectangle<float> r, const juce::String& text, Colour lit, bool background = true) {
    static const char* segs[10] = {"abcdef", "bc", "abged", "abgcd", "fgbc", "afgcd", "afgedc", "abc", "abcdefg", "abcdfg"};
    juce::String cells; std::vector<bool> dots;
    for (auto ch : text) {
        if (ch == '.' && !dots.empty()) { dots.back() = true; continue; }
        cells += ch; dots.push_back(false);
    }
    const int n = cells.length(); if (n == 0) return;
    if (background) { g.setColour(Colour(0xff0b0f0c)); g.fillRoundedRectangle(r, 3); }
    auto b = r.reduced(r.getHeight() * 0.14f, r.getHeight() * 0.14f);
    const float cw = b.getWidth() / float(n), ch = b.getHeight(), th = juce::jmin(cw * 0.16f, ch * 0.11f);
    for (int i = 0; i < n; ++i) {
        const float x0 = b.getX() + float(i) * cw + cw * 0.12f, x1 = b.getX() + float(i + 1) * cw - cw * 0.12f;
        const float y0 = b.getY(), y1 = b.getBottom(), ym = (y0 + y1) * 0.5f;
        auto seg = [&](char s) -> juce::Line<float> {
            switch (s) {
                case 'a': return {x0 + th, y0, x1 - th, y0};  case 'b': return {x1, y0 + th, x1, ym - th * 0.5f};
                case 'c': return {x1, ym + th * 0.5f, x1, y1 - th}; case 'd': return {x0 + th, y1, x1 - th, y1};
                case 'e': return {x0, ym + th * 0.5f, x0, y1 - th}; case 'f': return {x0, y0 + th, x0, ym - th * 0.5f};
                default:  return {x0 + th, ym, x1 - th, ym};
            }
        };
        juce::String on;
        const auto c = cells[i];
        if (c >= '0' && c <= '9') on = segs[c - '0']; else if (c == '-') on = "g"; else if (c == '+') on = "g";
        for (char s : {'a', 'b', 'c', 'd', 'e', 'f', 'g'}) {
            const bool isOn = on.containsChar(s);
            if (!isOn && !background) continue;
            auto l = seg(s);
            if (isOn) { g.setColour(lit.withAlpha(0.22f)); g.drawLine(l, th * 2.4f); }
            g.setColour(isOn ? lit : lit.withAlpha(0.07f)); g.drawLine(l, th);
        }
        if (dots[size_t(i)]) {
            g.setColour(lit.withAlpha(0.22f)); g.fillEllipse(x1 + th * 0.2f - th, y1 - th * 1.0f, th * 2.0f, th * 2.0f);
            g.setColour(lit); g.fillEllipse(x1 + th * 0.2f - th * 0.6f, y1 - th * 0.6f, th * 1.2f, th * 1.2f);
        }
    }
}

// ---- lamp and lit key -------------------------------------------------------
inline void drawLamp(Graphics& g, Point<float> c, float radius, Colour colour, bool on) {
    g.setColour(Colour(0xff0a0a0a)); g.fillEllipse(c.x - radius - 1.5f, c.y - radius - 1.5f, (radius + 1.5f) * 2, (radius + 1.5f) * 2);
    g.setGradientFill(juce::ColourGradient(Colour(0xffb9bec0), c.x - radius, c.y - radius, Colour(0xff3a3f42), c.x + radius, c.y + radius, false));
    g.fillEllipse(c.x - radius - 1.0f, c.y - radius - 1.0f, (radius + 1.0f) * 2, (radius + 1.0f) * 2);
    if (on) { g.setColour(colour.withAlpha(0.30f)); g.fillEllipse(c.x - radius * 2.0f, c.y - radius * 2.0f, radius * 4, radius * 4); }
    g.setGradientFill(juce::ColourGradient(on ? colour.brighter(0.7f) : colour.darker(0.9f), c.x - radius * 0.3f, c.y - radius * 0.4f,
                                           on ? colour.darker(0.2f) : colour.darker(1.4f), c.x + radius, c.y + radius, true));
    g.fillEllipse(c.x - radius, c.y - radius, radius * 2, radius * 2);
    g.setColour(Colour(0x66ffffff)); g.fillEllipse(c.x - radius * 0.55f, c.y - radius * 0.75f, radius * 0.8f, radius * 0.45f);
}

// Square key with a lamp strip along the top; `lit` lights the strip in `lamp`.
inline void drawLitKey(Graphics& g, Rectangle<float> r, const juce::String& text, bool lit, Colour lamp, bool down = false) {
    g.setColour(Colour(0xff0e1112)); g.fillRoundedRectangle(r.expanded(1.5f), 3);
    auto face = r.translated(0, down ? 1.0f : 0.0f);
    g.setColour(Colour(0xff060808)); g.fillRoundedRectangle(face.translated(0, 1.5f), 2);
    g.setGradientFill(juce::ColourGradient(Colour(0xffdfe2d0), face.getX(), face.getY(), Colour(0xff9da79e), face.getRight(), face.getBottom(), false));
    g.fillRoundedRectangle(face, 2);
    auto strip = face.reduced(face.getWidth() * 0.14f, 0).removeFromTop(juce::jmax(2.5f, face.getHeight() * 0.16f)).translated(0, 2.0f);
    if (lit) { g.setColour(lamp.withAlpha(0.35f)); g.fillRoundedRectangle(strip.expanded(2), 3); }
    g.setColour(lit ? lamp : Colour(0xff3a423d)); g.fillRoundedRectangle(strip, 1.5f);
    g.setColour(Colour(0xff26312e)); g.setFont(juce::FontOptions(face.getHeight() * 0.42f, juce::Font::bold));
    g.drawText(text, face.withTrimmedTop(strip.getBottom() - face.getY()), juce::Justification::centred);
    g.setColour(Colour(0x55ffffff)); g.drawHorizontalLine(int(face.getY() + 1), face.getX() + 2, face.getRight() - 2);
}

// ---- faders ------------------------------------------------------------------
enum class FaderCap { Oval, Console };
// Horizontal slot fader. prop01 = cap position along the slot.
inline void drawSlotFader(Graphics& g, Rectangle<float> r, float prop01, FaderCap cap, Colour capColour = Colour(0xfff0f0ee)) {
    const float slotH = juce::jmin(r.getHeight() * 0.42f, 12.0f);
    auto slot = Rectangle<float>(r.getWidth(), slotH).withCentre(r.getCentre());
    g.setColour(Colour(0xff030405)); g.fillRoundedRectangle(slot, slotH * 0.5f);
    g.setColour(Colour(0xff2a3236)); g.drawRoundedRectangle(slot, slotH * 0.5f, 1.0f);
    g.setColour(Colour(0xff39b6c8).withAlpha(0.55f));                                       // printed scale lines
    for (int i = 0; i <= 8; ++i) {
        const float x = r.getX() + r.getWidth() * float(i) / 8.0f;
        g.drawLine(x, slot.getBottom() + 1.5f, x, slot.getBottom() + (i % 4 == 0 ? 5.0f : 3.0f), 0.9f);
    }
    const float x = r.getX() + juce::jlimit(0.0f, 1.0f, prop01) * r.getWidth(), cy = r.getCentreY();
    if (cap == FaderCap::Oval) {
        auto o = Rectangle<float>(slotH * 2.1f, slotH * 1.05f).withCentre({x, cy});
        g.setColour(Colour(0x66000000)); g.fillRoundedRectangle(o.translated(0, 1.5f), o.getHeight() * 0.5f);
        g.setGradientFill(juce::ColourGradient(capColour.brighter(0.3f), o.getX(), o.getY(), capColour.darker(0.35f), o.getX(), o.getBottom(), false));
        g.fillRoundedRectangle(o, o.getHeight() * 0.5f);
        g.setColour(Colour(0x55ffffff)); g.drawHorizontalLine(int(o.getY() + 1.5f), o.getX() + o.getHeight() * 0.4f, o.getRight() - o.getHeight() * 0.4f);
    } else {
        auto c = Rectangle<float>(slotH * 1.5f, slotH * 2.2f).withCentre({x, cy});
        g.setColour(Colour(0x77000000)); g.fillRoundedRectangle(c.translated(1.0f, 2.0f), 2);
        g.setGradientFill(juce::ColourGradient(capColour.brighter(0.25f), c.getX(), c.getY(), capColour.darker(0.4f), c.getRight(), c.getBottom(), false));
        g.fillRoundedRectangle(c, 2);
        g.setColour(Colour(0xaaffffff)); g.drawLine(x, c.getY() + 2, x, c.getBottom() - 2, 1.4f);
        g.setColour(Colour(0x55000000)); g.drawRoundedRectangle(c, 2, 0.8f);
    }
}
} // namespace goodlookinui::juce_adapter::meters
