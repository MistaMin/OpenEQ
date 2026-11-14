// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marcos Deida
#pragma once
#include "Meters.h"
#include <vector>

// Retro-futuristic meters, sliders, lamps and frames: 80s car-dash displays and
// cyberpunk HUD parts. Everything is generated in code. `t` is a time in seconds used
// for gentle animation (flicker, sweeps); pass 0 for a still image.
namespace goodlookinui::juce_adapter::retro {
using juce::Colour; namespace Colours = juce::Colours; using juce::Graphics; using juce::Point; using juce::Rectangle; using juce::Path;

// Global look switch: glow + scanlines + flicker on (neon) or off (clean, flat). Both are first-class.
inline bool glowEnabled = true;

// ---- glow primitives ------------------------------------------------------
inline void glowStroke(Graphics& g, const Path& p, Colour c, float w, float strength = 1.0f) {
    if (!glowEnabled) { g.setColour(c); g.strokePath(p, juce::PathStrokeType(w)); return; }
    for (int k = 4; k >= 1; --k) { g.setColour(c.withAlpha(0.05f * strength * float(5 - k))); g.strokePath(p, juce::PathStrokeType(w * (1.0f + 1.6f * float(k)), juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); }
    g.setColour(c); g.strokePath(p, juce::PathStrokeType(w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(c.brighter(0.7f).withAlpha(0.9f)); g.strokePath(p, juce::PathStrokeType(w * 0.35f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}
inline void glowLine(Graphics& g, Point<float> a, Point<float> b, Colour c, float w, float strength = 1.0f) { Path p; p.startNewSubPath(a); p.lineTo(b); glowStroke(g, p, c, w, strength); }
inline void glowDot(Graphics& g, Point<float> c, float r, Colour col) {
    if (!glowEnabled) { g.setColour(col); g.fillEllipse(c.x - r, c.y - r, r * 2, r * 2); return; }
    g.setColour(col.withAlpha(0.18f)); g.fillEllipse(c.x - r * 3, c.y - r * 3, r * 6, r * 6);
    g.setColour(col.withAlpha(0.35f)); g.fillEllipse(c.x - r * 1.8f, c.y - r * 1.8f, r * 3.6f, r * 3.6f);
    g.setColour(col.brighter(0.8f)); g.fillEllipse(c.x - r * 0.7f, c.y - r * 0.7f, r * 1.4f, r * 1.4f);
}
inline Path chamfer(Rectangle<float> r, float ch, bool topRightOnly = false) {
    Path p;
    if (topRightOnly) { p.startNewSubPath(r.getX(), r.getY()); p.lineTo(r.getRight() - ch, r.getY()); p.lineTo(r.getRight(), r.getY() + ch);
        p.lineTo(r.getRight(), r.getBottom()); p.lineTo(r.getX() + ch, r.getBottom()); p.lineTo(r.getX(), r.getBottom() - ch); }
    else { p.startNewSubPath(r.getX() + ch, r.getY()); p.lineTo(r.getRight() - ch, r.getY()); p.lineTo(r.getRight(), r.getY() + ch);
        p.lineTo(r.getRight(), r.getBottom() - ch); p.lineTo(r.getRight() - ch, r.getBottom()); p.lineTo(r.getX() + ch, r.getBottom());
        p.lineTo(r.getX(), r.getBottom() - ch); p.lineTo(r.getX(), r.getY() + ch); }
    p.closeSubPath(); return p;
}
inline void scanlines(Graphics& g, Rectangle<float> r, float t, float alpha = 0.18f) {
    if (!glowEnabled) return;
    g.setColour(Colour::fromFloatRGBA(0, 0, 0, alpha));
    for (float y = r.getY(); y < r.getBottom(); y += 2.5f) g.drawHorizontalLine(int(y), r.getX(), r.getRight());
    const float sweep = r.getY() + std::fmod(t * 0.35f, 1.0f) * r.getHeight();   // slow bright sweep
    g.setGradientFill(juce::ColourGradient(Colour(0x00ffffff), 0, sweep - 14, Colour(0x16ffffff), 0, sweep, false));
    g.fillRect(r.getX(), sweep - 14, r.getWidth(), 14.0f);
}
inline float flicker(float t) { if (!glowEnabled) return 1.0f; return 0.94f + 0.06f * std::sin(t * 31.0f) * std::sin(t * 7.3f); }
inline Colour heat(float f, Colour lo, Colour hi) { return lo.interpolatedWith(hi, juce::jlimit(0.0f, 1.0f, f)); }
inline Colour neon(int i) { static const Colour c[] = {Colour(0xff39f2ff), Colour(0xffff4fd8), Colour(0xff9dff3a), Colour(0xffffb02e), Colour(0xffff3b4a), Colour(0xff7a5cff)}; return c[i % 6]; }

// ---- faces: all take level01 (0..1) ---------------------------------------
// 1. Bar-graph wedge (speedo / tach ramp): bars climb toward the red end.
inline void drawBarWedge(Graphics& g, Rectangle<float> r, float level, Colour lit, float t) {
    g.setGradientFill(juce::ColourGradient(Colour(0xff17140a), 0, r.getY(), Colour(0xff060502), 0, r.getBottom(), false)); g.fillRoundedRectangle(r, 5);
    auto b = r.reduced(8, 6); b.removeFromBottom(10);
    const int n = 26;
    for (int i = 0; i < n; ++i) {
        const float f = float(i) / float(n - 1), x = b.getX() + f * (b.getWidth() - 4);
        const float h = b.getHeight() * (0.14f + 0.86f * std::pow(f, 1.35f)), w = b.getWidth() / float(n) * 0.68f;
        const bool on = f <= level + 0.5f / float(n);
        const Colour c = f > 0.84f ? Colour(0xffff3b2a) : f > 0.68f ? lit.interpolatedWith(Colour(0xffff6a1a), 0.55f) : lit;
        auto bar = Rectangle<float>(x, b.getBottom() - h, w, h);
        if (on && glowEnabled) { g.setColour(c.withAlpha(0.22f * flicker(t + f))); g.fillRoundedRectangle(bar.expanded(2.5f), 2); }
        g.setColour(on ? c.withAlpha(flicker(t + f * 3.0f)) : c.withAlpha(0.10f)); g.fillRoundedRectangle(bar, 1.2f);
        if (on) { g.setColour(Colour(0x33ffffff)); g.fillRect(bar.withWidth(bar.getWidth() * 0.35f)); }
    }
    g.setColour(lit.withAlpha(0.7f)); g.setFont(juce::FontOptions(7.5f, juce::Font::bold));
    auto labels = r.reduced(8, 2).removeFromBottom(10);
    for (int i = 0; i <= 5; ++i) g.drawText(juce::String(i * 12), labels.withX(b.getX() + b.getWidth() * float(i) / 5.0f - 10).withWidth(20), juce::Justification::centred);
    scanlines(g, r, t, 0.14f);
    g.setColour(lit.withAlpha(0.35f)); g.drawRoundedRectangle(r, 5, 1.2f);
}

// 2. Vector mountains: mirrored green fans (two channels) with a centre scale.
inline void drawMountains(Graphics& g, Rectangle<float> r, float left, float right, Colour lit, float t) {
    g.setColour(Colour(0xff020a06)); g.fillRoundedRectangle(r, 5);
    auto b = r.reduced(8, 7); const float cx = b.getCentreX(), half = b.getWidth() * 0.5f - 12;
    const int n = 22;
    for (int side = 0; side < 2; ++side) {
        const float level = side == 0 ? left : right, dirn = side == 0 ? -1.0f : 1.0f;
        for (int i = 0; i < n; ++i) {
            const float f = float(i) / float(n - 1);                 // 0 at the centre, 1 at the outer edge
            const float x = cx + dirn * (10.0f + f * half);
            const float h = b.getHeight() * (0.92f - 0.80f * f);     // tall near the centre like the dash
            const bool on = (1.0f - f) <= level + 0.5f / float(n);
            const Point<float> base(x, b.getBottom()), top(x - dirn * (b.getWidth() * 0.02f), b.getBottom() - h);
            if (on && glowEnabled) { g.setColour(lit.withAlpha(0.22f * flicker(t + f))); g.drawLine({base, top}, 3.6f); }
            g.setColour(on ? lit.brighter(0.15f) : lit.withAlpha(0.12f)); g.drawLine({base, top}, 1.2f);
        }
    }
    g.setColour(lit.withAlpha(0.8f)); g.setFont(juce::FontOptions(7.5f, juce::Font::bold));
    for (int i = 0; i < 6; ++i) { const float y = b.getBottom() - b.getHeight() * float(i) / 5.0f; g.drawText(juce::String(i * 10), Rectangle<float>(20, 9).withCentre({cx, y - 3}), juce::Justification::centred); g.drawHorizontalLine(int(y), cx - 8, cx - 5); g.drawHorizontalLine(int(y), cx + 5, cx + 8); }
    g.setColour(lit.withAlpha(0.6f)); g.drawHorizontalLine(int(b.getBottom() + 1), b.getX(), b.getRight());
    scanlines(g, r, t, 0.2f);
    g.setColour(lit.withAlpha(0.3f)); g.drawRoundedRectangle(r, 5, 1.0f);
}

// 3. Orange LCD panel with two digit boxes and printed labels (range / trip style).
inline void drawLcdPanel(Graphics& g, Rectangle<float> r, const juce::String& a, const juce::String& aLabel, const juce::String& b, const juce::String& bLabel, Colour lit, float t) {
    g.setGradientFill(juce::ColourGradient(Colour(0xff1b1108), 0, r.getY(), Colour(0xff070402), 0, r.getBottom(), false)); g.fillRoundedRectangle(r, 6);
    g.setColour(lit.withAlpha(0.55f)); g.drawRoundedRectangle(r, 6, 1.4f);
    const float pad = 7.0f; auto in = r.reduced(pad);
    auto half = in.getWidth() * 0.5f - 3.0f;
    for (int i = 0; i < 2; ++i) {
        auto cell = Rectangle<float>(in.getX() + float(i) * (half + 6), in.getY(), half, in.getHeight());
        auto lab = cell.removeFromBottom(12);
        g.setColour(lit.withAlpha(0.35f)); g.drawRoundedRectangle(cell, 4, 1.0f);
        meters::drawSevenSegment(g, cell.reduced(5, 5), i == 0 ? a : b, lit.withAlpha(flicker(t + float(i))), true);
        g.setColour(lit.withAlpha(0.85f)); g.setFont(juce::FontOptions(8.5f, juce::Font::bold));
        g.drawText(i == 0 ? aLabel : bLabel, lab, juce::Justification::centred);
    }
    scanlines(g, r, t, 0.12f);
}

// 4. Arc gauge: orange sweep, printed 0..9 scale and a red-line zone.
inline void drawArcGauge(Graphics& g, Rectangle<float> r, float level, Colour lit, float t) {
    g.setGradientFill(juce::ColourGradient(Colour(0xff15110c), 0, r.getY(), Colour(0xff050404), 0, r.getBottom(), false)); g.fillRoundedRectangle(r, 6);
    const float rad = juce::jmin(r.getWidth() * 0.46f, r.getHeight() * 0.92f); const Point<float> c(r.getCentreX(), r.getBottom() - 6);
    auto arc = [&](float f0, float f1, float rr, float w, Colour col) { Path p; p.addCentredArc(c.x, c.y, rr, rr, 0, (-0.5f + f0) * juce::MathConstants<float>::pi, (-0.5f + f1) * juce::MathConstants<float>::pi, true);
        g.setColour(col); g.strokePath(p, juce::PathStrokeType(w, juce::PathStrokeType::curved, juce::PathStrokeType::butt)); };
    arc(0, 1, rad, rad * 0.22f, lit.withAlpha(0.12f));
    arc(0, juce::jmin(level, 0.86f), rad, rad * 0.30f, lit.withAlpha(0.22f * flicker(t)));
    arc(0, juce::jmin(level, 0.86f), rad, rad * 0.20f, lit);
    if (level > 0.86f) arc(0.86f, level, rad, rad * 0.20f, Colour(0xffff3b2a));
    arc(0.86f, 1.0f, rad * 0.78f, rad * 0.06f, Colour(0xffff3b2a).withAlpha(0.85f));
    g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
    for (int i = 0; i <= 9; ++i) {
        const float f = float(i) / 9.0f, a = (-0.5f + f) * juce::MathConstants<float>::pi; Point<float> d(std::sin(a), -std::cos(a)); (void)d;
        const Point<float> dir2(std::sin(a * 1.0f), -std::cos(a));
        auto q = Point<float>(c.x + std::sin(a) * rad * 1.0f, c.y - std::cos(a) * rad * 1.0f);
        (void)dir2; (void)q;
        const Point<float> tip0 = {c.x + std::sin(a) * rad * 0.82f, c.y - std::cos(a) * rad * 0.82f}, tip1 = {c.x + std::sin(a) * rad * 0.74f, c.y - std::cos(a) * rad * 0.74f};
        g.setColour(f > 0.86f ? Colour(0xffff6a4a) : lit.withAlpha(0.9f)); g.drawLine({tip0, tip1}, 1.3f);
        g.drawText(juce::String(i), Rectangle<float>(14, 10).withCentre({c.x + std::sin(a) * rad * 0.64f, c.y - std::cos(a) * rad * 0.64f}), juce::Justification::centred);
    }
    meters::drawSevenSegment(g, Rectangle<float>(rad * 0.55f, rad * 0.30f).withCentre({c.x, c.y - rad * 0.22f}), juce::String(int(level * 99.0f)), lit, false);
    scanlines(g, r, t, 0.12f);
    g.setColour(lit.withAlpha(0.4f)); g.drawRoundedRectangle(r, 6, 1.2f);
}

// 5. Colour columns: two stereo stacks of colour-coded blocks (green > yellow > red) with a peak cap.
inline void drawColourColumns(Graphics& g, Rectangle<float> r, float l, float rr, float peakL, float peakR, float t) {
    g.setColour(Colour(0xff060907)); g.fillRoundedRectangle(r, 5);
    auto b = r.reduced(10, 7); b.removeFromLeft(12);
    const int n = 22; const float seg = b.getWidth() / float(n), rowH = (b.getHeight() - 6) * 0.5f;
    for (int c = 0; c < 2; ++c) {
        const float lv = c == 0 ? l : rr, pk = c == 0 ? peakL : peakR;
        auto row = Rectangle<float>(b.getX(), b.getY() + float(c) * (rowH + 6), b.getWidth(), rowH);
        g.setColour(Colour(0xff8b9498)); g.setFont(juce::FontOptions(8.0f, juce::Font::bold));
        g.drawText(c == 0 ? "L" : "R", Rectangle<float>(10, rowH).withCentre({r.getX() + 10, row.getCentreY()}), juce::Justification::centred);
        for (int i = 0; i < n; ++i) {
            const float f = float(i) / float(n - 1);
            const Colour cc = f > 0.85f ? Colour(0xffff3b2a) : f > 0.62f ? Colour(0xffffcf2a) : Colour(0xff35e04a);
            const bool on = f <= lv + 0.5f / float(n) || (pk > 0.0f && std::abs(f - pk) < 0.5f / float(n));
            auto s = Rectangle<float>(row.getX() + float(i) * seg + 1.0f, row.getY(), seg - 2.0f, row.getHeight());
            if (on && glowEnabled) { g.setColour(cc.withAlpha(0.25f * flicker(t + f))); g.fillRect(s.expanded(1.6f)); }
            g.setColour(on ? cc : cc.withAlpha(0.10f)); g.fillRect(s);
            if (on) { g.setColour(Colour(0x40ffffff)); g.fillRect(s.withHeight(2.0f)); }
        }
    }
    scanlines(g, r, t, 0.10f); g.setColour(Colour(0x44ffffff)); g.drawRoundedRectangle(r, 5, 1.0f);
}

// 6. Neon bars: chamfered frame, tall glowing bars that fade cyan -> magenta, peak comet.
inline void drawNeonBars(Graphics& g, Rectangle<float> r, float level, float peak, Colour lo, Colour hi, float t) {
    auto frame = chamfer(r.reduced(1.5f), 10);
    g.setGradientFill(juce::ColourGradient(Colour(0xff06161c), 0, r.getY(), Colour(0xff020609), 0, r.getBottom(), false)); g.fillPath(frame);
    g.saveState(); g.reduceClipRegion(frame);
    g.setColour(lo.withAlpha(0.07f)); for (float x = r.getX(); x < r.getRight(); x += 12) g.drawVerticalLine(int(x), r.getY(), r.getBottom());
    for (float y = r.getY(); y < r.getBottom(); y += 12) g.drawHorizontalLine(int(y), r.getX(), r.getRight());
    auto b = r.reduced(14, 12); const int n = 24;
    for (int i = 0; i < n; ++i) {
        const float f = float(i) / float(n - 1), x = b.getX() + f * b.getWidth(), w = b.getWidth() / float(n) * 0.55f;
        const float shape = 0.45f + 0.55f * std::sin(f * juce::MathConstants<float>::pi * 0.5f + 0.4f);
        const float h = b.getHeight() * shape, fill = juce::jlimit(0.0f, 1.0f, (level - f * 0.6f) / 0.4f);
        const Colour c = heat(f, lo, hi);
        auto bar = Rectangle<float>(x, b.getBottom() - h, w, h);
        g.setColour(c.withAlpha(0.10f)); g.fillRect(bar);
        if (fill > 0) { auto lit = bar.withTop(bar.getBottom() - h * fill); if (glowEnabled) { g.setColour(c.withAlpha(0.28f * flicker(t + f * 2))); g.fillRect(lit.expanded(2.5f, 1)); } g.setColour(c); g.fillRect(lit);
            g.setColour(c.brighter(0.9f)); g.fillRect(lit.withHeight(2.0f)); }
    }
    const float px = b.getX() + juce::jlimit(0.0f, 1.0f, peak) * b.getWidth();
    glowLine(g, {px, b.getY()}, {px, b.getBottom()}, hi, 1.2f, 0.8f);
    scanlines(g, r, t, 0.16f);
    g.restoreState();
    glowStroke(g, frame, lo, 1.4f);
    glowLine(g, {r.getX() + 14, r.getY() + 1}, {r.getX() + 44, r.getY() + 1}, hi, 2.2f);       // accent notch
}

// 7. Hex ring: hexagonal frame with a glowing ring that fills cyan -> pink.
inline void drawHexRing(Graphics& g, Rectangle<float> r, float level, Colour lo, Colour hi, float t) {
    const auto c = r.getCentre(); const float R = juce::jmin(r.getWidth(), r.getHeight()) * 0.48f;
    Path hx; for (int i = 0; i < 6; ++i) { const float a = float(i) * juce::MathConstants<float>::pi / 3.0f + juce::MathConstants<float>::pi / 6.0f; auto p = Point<float>(c.x + std::cos(a) * R * 1.12f, c.y + std::sin(a) * R * 1.12f); i == 0 ? hx.startNewSubPath(p) : hx.lineTo(p); } hx.closeSubPath();
    g.setColour(Colour(0xff03080d)); g.fillRoundedRectangle(r, 6);
    g.setGradientFill(juce::ColourGradient(lo.darker(0.9f), c.x, c.y - R, Colour(0xff020406), c.x, c.y + R, false)); g.fillPath(hx);
    glowStroke(g, hx, lo.withAlpha(0.7f), 1.2f, 0.8f);
    const float a0 = -juce::MathConstants<float>::pi * 1.25f, a1 = juce::MathConstants<float>::pi * 0.25f, ring = R * 0.80f;
    Path track; track.addCentredArc(c.x, c.y, ring, ring, 0, a0 + juce::MathConstants<float>::halfPi, a1 + juce::MathConstants<float>::halfPi, true);
    g.setColour(lo.withAlpha(0.18f)); g.strokePath(track, juce::PathStrokeType(R * 0.10f));
    Path val; val.addCentredArc(c.x, c.y, ring, ring, 0, a0 + juce::MathConstants<float>::halfPi, a0 + (a1 - a0) * level + juce::MathConstants<float>::halfPi, true);
    glowStroke(g, val, heat(level, lo, hi), R * 0.10f);
    for (int i = 0; i < 24; ++i) { const float a = a0 + (a1 - a0) * float(i) / 23.0f + juce::MathConstants<float>::halfPi; const bool on = float(i) / 23.0f <= level;
        g.setColour(on ? Colour(0xffffffff).withAlpha(0.7f) : lo.withAlpha(0.3f)); g.drawLine(c.x + std::cos(a) * ring * 0.80f, c.y + std::sin(a) * ring * 0.80f, c.x + std::cos(a) * ring * 0.70f, c.y + std::sin(a) * ring * 0.70f, 1.0f); }
    meters::drawSevenSegment(g, Rectangle<float>(R * 0.9f, R * 0.46f).withCentre({c.x, c.y + R * 0.05f}), juce::String(int(level * 100.0f)), heat(level, lo, hi).withAlpha(flicker(t)), false);
    scanlines(g, r, t, 0.12f);
}

// 8. Glitch scope: scrolling level history as a mirrored envelope with a clip warning.
inline void drawScope(Graphics& g, Rectangle<float> r, const std::vector<float>& hist, bool clip, Colour lit, float t) {
    auto frame = chamfer(r.reduced(1.5f), 8, true);
    g.setColour(Colour(0xff020b0d)); g.fillPath(frame); g.saveState(); g.reduceClipRegion(frame);
    g.setColour(lit.withAlpha(0.09f)); for (float x = r.getX(); x < r.getRight(); x += 14) g.drawVerticalLine(int(x), r.getY(), r.getBottom());
    g.drawHorizontalLine(int(r.getCentreY()), r.getX(), r.getRight());
    if (!hist.empty()) {
        Path top, bot; const float cy = r.getCentreY(), amp = r.getHeight() * 0.44f;
        for (size_t i = 0; i < hist.size(); ++i) {
            const float x = r.getX() + r.getWidth() * float(i) / float(hist.size() - 1), v = hist[i] * amp;
            const float jitter = (hist[i] > 0.02f) ? 0.5f * std::sin(float(i) * 1.7f + t * 30.0f) : 0.0f;
            i == 0 ? top.startNewSubPath(x, cy - v) : top.lineTo(x, cy - v - jitter); i == 0 ? bot.startNewSubPath(x, cy + v) : bot.lineTo(x, cy + v + jitter);
        }
        Path fill = top; fill.lineTo(r.getRight(), r.getCentreY()); for (int i = int(hist.size()) - 1; i >= 0; --i) fill.lineTo(r.getX() + r.getWidth() * float(i) / float(hist.size() - 1), cy + hist[size_t(i)] * amp); fill.closeSubPath();
        g.setGradientFill(juce::ColourGradient(lit.withAlpha(0.30f), 0, cy - amp, lit.withAlpha(0.05f), 0, cy, false)); g.fillPath(fill);
        glowStroke(g, top, lit, 1.2f, 0.9f); glowStroke(g, bot, lit.withAlpha(0.7f), 1.0f, 0.6f);
        const float ex = r.getRight() - 3; glowDot(g, {ex, cy - hist.back() * amp}, 2.0f, lit);
        // RGB-split ghost for the glitch look
        g.setColour(Colour(0x30ff2a6a)); g.strokePath(top, juce::PathStrokeType(1.0f), juce::AffineTransform::translation(1.5f, 0));
    }
    if (clip && std::fmod(t, 0.8f) < 0.5f) { g.setColour(Colour(0xaaff2a3a)); g.fillRect(r.withHeight(14).translated(0, 3)); g.setColour(Colours::white); g.setFont(juce::FontOptions(9.0f, juce::Font::bold)); g.drawText("! ATTENTION  CLIP", r.withHeight(14).translated(0, 3), juce::Justification::centred); }
    scanlines(g, r, t, 0.2f);
    g.restoreState(); glowStroke(g, frame, lit.withAlpha(0.8f), 1.2f, 0.7f);
}

// 9. Chassis: rugged yellow frame around a black screen with a cyan segmented bar.
inline void drawChassis(Graphics& g, Rectangle<float> r, float level, Colour body, Colour lit, float t) {
    g.setColour(Colour(0x66000000)); g.fillRoundedRectangle(r.translated(0, 2), 9);
    g.setGradientFill(juce::ColourGradient(body.brighter(0.25f), 0, r.getY(), body.darker(0.45f), 0, r.getBottom(), false)); g.fillRoundedRectangle(r, 9);
    g.setColour(body.darker(0.8f)); g.drawRoundedRectangle(r, 9, 1.4f);
    for (auto p : {Point<float>(r.getX() + 7, r.getY() + 7), Point<float>(r.getRight() - 7, r.getY() + 7), Point<float>(r.getX() + 7, r.getBottom() - 7), Point<float>(r.getRight() - 7, r.getBottom() - 7)}) { g.setColour(body.darker(0.7f)); g.fillEllipse(p.x - 2.2f, p.y - 2.2f, 4.4f, 4.4f); }
    auto s = r.reduced(13, 11); g.setColour(Colour(0xff04080a)); g.fillRoundedRectangle(s, 5);
    g.setColour(lit.withAlpha(0.4f)); g.drawRoundedRectangle(s, 5, 1.0f);
    auto bars = s.reduced(8, 0).withTrimmedBottom(14).withTrimmedTop(8); const int n = 28;
    for (int i = 0; i < n; ++i) { const float f = float(i) / float(n - 1), x = bars.getX() + f * bars.getWidth(); const bool on = f <= level + 0.5f / float(n);
        const Colour c = f > 0.85f ? Colour(0xffff4a5a) : lit; auto seg = Rectangle<float>(x, bars.getY() + (i % 4 == 0 ? 0.0f : 5.0f), bars.getWidth() / float(n) * 0.62f, bars.getHeight() - (i % 4 == 0 ? 0.0f : 5.0f));
        if (on && glowEnabled) { g.setColour(c.withAlpha(0.25f * flicker(t + f))); g.fillRect(seg.expanded(2)); } g.setColour(on ? c : c.withAlpha(0.10f)); g.fillRect(seg); }
    g.setColour(lit.withAlpha(0.8f)); g.setFont(juce::FontOptions(8.0f, juce::Font::bold)); g.drawText("OUTPUT  //  LEVEL", s.removeFromBottom(13).reduced(8, 0), juce::Justification::centredLeft);
    scanlines(g, s, t, 0.14f);
}

// 10. Flat bars: clean, matte, no glow or scanlines. Two stereo bars with a dB scale.
inline void drawFlatBars(Graphics& g, Rectangle<float> r, float l, float rr, float peakL, float peakR, Colour lo) {
    g.setColour(Colour(0xff0d0f10)); g.fillRoundedRectangle(r, 4);
    auto b = r.reduced(12, 9); b.removeFromBottom(10);
    const float h = (b.getHeight() - 8) * 0.5f; const int n = 30; const float seg = b.getWidth() / float(n);
    for (int c = 0; c < 2; ++c) {
        const float lv = c == 0 ? l : rr, pk = c == 0 ? peakL : peakR;
        auto row = Rectangle<float>(b.getX(), b.getY() + float(c) * (h + 8), b.getWidth(), h);
        for (int i = 0; i < n; ++i) {
            const float f = float(i) / float(n - 1);
            const Colour cc = f > 0.88f ? Colour(0xffe5382a) : f > 0.70f ? Colour(0xffe8b923) : lo;
            const bool on = f <= lv + 0.5f / float(n) || std::abs(f - pk) < 0.5f / float(n);
            g.setColour(on ? cc : cc.withAlpha(0.13f)); g.fillRect(Rectangle<float>(row.getX() + float(i) * seg + 0.6f, row.getY(), seg - 1.2f, row.getHeight()));
        }
    }
    g.setColour(Colour(0xff8b9498)); g.setFont(juce::FontOptions(7.5f, juce::Font::bold));
    for (int i = 0; i <= 6; ++i) g.drawText(juce::String(-48 + i * 8), Rectangle<float>(24, 9).withCentre({b.getX() + b.getWidth() * float(i) / 6.0f, r.getBottom() - 9}), juce::Justification::centred);
    g.setColour(Colour(0xff2a3033)); g.drawRoundedRectangle(r, 4, 1.0f);
}

// ---- icon lamps -------------------------------------------------------------
enum class Icon { Battery, Warning, ArrowLeft, ArrowRight, Beam, Temp };
inline void drawIconLamp(Graphics& g, Rectangle<float> r, Icon icon, Colour c, bool on) {
    g.setColour(Colour(0xff050606)); g.fillRoundedRectangle(r, 3);
    g.setColour(on ? c.withAlpha(0.7f) : Colour(0xff2a2f31)); g.drawRoundedRectangle(r, 3, 1.0f);
    if (on) { g.setColour(c.withAlpha(0.18f)); g.fillRoundedRectangle(r.expanded(2), 5); }
    const auto col = on ? c : c.withAlpha(0.18f); auto a = r.reduced(r.getWidth() * 0.24f); Path p;
    switch (icon) {
        case Icon::Battery: p.addRectangle(a.withTrimmedTop(a.getHeight() * 0.2f)); p.addRectangle(Rectangle<float>(a.getWidth() * 0.3f, a.getHeight() * 0.16f).withCentre({a.getCentreX(), a.getY() + a.getHeight() * 0.08f})); g.setColour(col); g.strokePath(p, juce::PathStrokeType(1.4f)); g.setFont(juce::FontOptions(a.getHeight() * 0.55f, juce::Font::bold)); g.drawText("+", a.withTrimmedTop(a.getHeight() * 0.2f), juce::Justification::centred); return;
        case Icon::Warning: p.addTriangle(a.getCentreX(), a.getY(), a.getRight(), a.getBottom(), a.getX(), a.getBottom()); g.setColour(col); g.strokePath(p, juce::PathStrokeType(1.5f)); g.setFont(juce::FontOptions(a.getHeight() * 0.62f, juce::Font::bold)); g.drawText("!", a.withTrimmedTop(a.getHeight() * 0.25f), juce::Justification::centred); return;
        case Icon::ArrowLeft: p.addTriangle(a.getX(), a.getCentreY(), a.getRight(), a.getY(), a.getRight(), a.getBottom()); break;
        case Icon::ArrowRight: p.addTriangle(a.getRight(), a.getCentreY(), a.getX(), a.getY(), a.getX(), a.getBottom()); break;
        case Icon::Beam: for (int i = 0; i < 4; ++i) { const float y = a.getY() + a.getHeight() * (0.15f + 0.23f * float(i)); g.setColour(col); g.drawLine(a.getX() + a.getWidth() * 0.35f, y, a.getRight(), y, 1.6f); } g.setColour(col); g.fillEllipse(a.getX(), a.getCentreY() - a.getHeight() * 0.3f, a.getWidth() * 0.4f, a.getHeight() * 0.6f); return;
        case Icon::Temp: g.setColour(col); g.drawLine(a.getCentreX(), a.getY(), a.getCentreX(), a.getBottom() - a.getWidth() * 0.3f, 2.4f); g.fillEllipse(a.getCentreX() - a.getWidth() * 0.28f, a.getBottom() - a.getWidth() * 0.56f, a.getWidth() * 0.56f, a.getWidth() * 0.56f); return;
    }
    g.setColour(col); g.fillPath(p);
}

// ---- sliders ---------------------------------------------------------------
enum class SliderStyle { Neon, Bar, Chamfer, Wedge, Flat };
// Draws a horizontal slider; for vertical sliders see drawSlider(), which rotates it.
inline void drawSliderH(Graphics& g, Rectangle<float> r, float prop, SliderStyle st, Colour c, float t) {
    prop = juce::jlimit(0.0f, 1.0f, prop); const float cy = r.getCentreY(), x = r.getX() + prop * r.getWidth();
    switch (st) {
        case SliderStyle::Neon: {
            glowLine(g, {r.getX(), cy}, {r.getRight(), cy}, c.withAlpha(0.28f), 1.4f, 0.3f);
            for (int i = 0; i <= 10; ++i) { const float tx = r.getX() + r.getWidth() * float(i) / 10.0f; g.setColour(c.withAlpha(0.35f)); g.drawLine(tx, cy + 5, tx, cy + (i % 5 == 0 ? 11.0f : 8.0f), 0.9f); }
            glowLine(g, {r.getX(), cy}, {x, cy}, c, 2.4f);
            Path cap; cap.addRectangle(Rectangle<float>(8, 18).withCentre({x, cy})); g.setColour(Colour(0xff04090c)); g.fillPath(cap);
            Path d; d.addTriangle(x, cy - 10, x + 6, cy, x, cy + 10); d.addTriangle(x, cy - 10, x - 6, cy, x, cy + 10);
            g.setColour(Colour(0xff06161c)); g.fillPath(d); glowStroke(g, d, c.brighter(0.2f), 1.3f, 0.9f);
            glowDot(g, {x, cy}, 1.6f, Colours::white); break; }
        case SliderStyle::Bar: {
            const int n = 30; const float seg = r.getWidth() / float(n);
            for (int i = 0; i < n; ++i) { const float f = float(i) / float(n - 1); const bool on = f <= prop + 0.5f / float(n);
                const Colour cc = f > 0.85f ? Colour(0xffff3b2a) : f > 0.66f ? Colour(0xffffcf2a) : c;
                auto s = Rectangle<float>(seg - 1.8f, 12.0f + 10.0f * f).withCentre({r.getX() + float(i) * seg + 0.8f + (seg - 1.8f) * 0.5f, cy});
                if (on && glowEnabled) { g.setColour(cc.withAlpha(0.22f * flicker(t + f))); g.fillRect(s.expanded(2)); } g.setColour(on ? cc : cc.withAlpha(0.10f)); g.fillRect(s); }
            g.setColour(Colour(0xfff6f6f0)); g.fillRect(Rectangle<float>(2.5f, 28).withCentre({x, cy})); break; }
        case SliderStyle::Chamfer: {
            auto track = Rectangle<float>(r.getWidth(), 12).withCentre({r.getCentreX(), cy}); auto frame = chamfer(track, 4);
            g.setColour(Colour(0xff04080b)); g.fillPath(frame);
            g.saveState(); g.reduceClipRegion(frame); g.setGradientFill(juce::ColourGradient(c.darker(0.2f), track.getX(), 0, c.brighter(0.3f), x, 0, false)); g.fillRect(track.withRight(x)); g.restoreState();
            glowStroke(g, frame, c, 1.2f, 0.7f);
            auto hex = chamfer(Rectangle<float>(16, 22).withCentre({x, cy}), 5); g.setColour(Colour(0xff0a1218)); g.fillPath(hex); glowStroke(g, hex, c.brighter(0.3f), 1.4f);
            g.setColour(c.brighter(0.6f)); g.fillRect(Rectangle<float>(2, 10).withCentre({x, cy})); break; }
        case SliderStyle::Flat: {
            auto track = Rectangle<float>(r.getWidth(), 6).withCentre({r.getCentreX(), cy});
            g.setColour(Colour(0xff0b0d0e)); g.fillRoundedRectangle(track, 1.5f);
            g.setColour(c); g.fillRoundedRectangle(track.withRight(x), 1.5f);
            g.setColour(Colour(0xfff1f1ec)); g.fillRoundedRectangle(Rectangle<float>(7, 20).withCentre({x, cy}), 1.5f);
            g.setColour(Colour(0xff1b1d1e)); g.drawRoundedRectangle(Rectangle<float>(7, 20).withCentre({x, cy}), 1.5f, 0.8f); break; }
        case SliderStyle::Wedge: {
            Path ramp; ramp.startNewSubPath(r.getX(), cy + 7); ramp.lineTo(r.getRight(), cy - 11); ramp.lineTo(r.getRight(), cy + 7); ramp.closeSubPath();
            g.setColour(c.withAlpha(0.12f)); g.fillPath(ramp);
            g.saveState(); g.reduceClipRegion(ramp); g.setGradientFill(juce::ColourGradient(c, r.getX(), 0, Colour(0xffff4a2a), r.getRight(), 0, false)); g.fillRect(r.withRight(x).expanded(0, 14));
            g.setColour(Colour(0x66000000)); for (float sx = r.getX(); sx < r.getRight(); sx += 4.0f) g.drawVerticalLine(int(sx), cy - 12, cy + 8); g.restoreState();
            g.setColour(c.withAlpha(0.7f)); g.strokePath(ramp, juce::PathStrokeType(1.0f));
            g.setColour(Colours::white); g.fillRect(Rectangle<float>(2.5f, 22).withCentre({x, cy - 2})); break; }
    }
}
inline void drawSlider(Graphics& g, Rectangle<float> r, float prop, SliderStyle st, Colour c, bool vertical, float t) {
    if (!vertical) { drawSliderH(g, r, prop, st, c, t); return; }
    g.saveState();
    g.addTransform(juce::AffineTransform::rotation(-juce::MathConstants<float>::halfPi, r.getCentreX(), r.getCentreY()));
    drawSliderH(g, Rectangle<float>(r.getHeight(), r.getWidth()).withCentre(r.getCentre()), prop, st, c, t);
    g.restoreState();
}
} // namespace goodlookinui::juce_adapter::retro
