// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marcos Deida
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Retro.h"

// Rocker switches, toggles and push buttons, drawn in code. Every function draws one
// state (`on` / `down`); the host component decides what a click means.
namespace goodlookinui::juce_adapter::switches {
using juce::Colour; using juce::Graphics; using juce::Point; using juce::Rectangle;

// ---- rockers -----------------------------------------------------------------
enum class RockerStyle { Lamp, Classic, Slim, Neon };
inline constexpr const char* rockerNames[] = {"Lamp rocker", "Classic rocker", "Slim rocker", "Neon rocker"};

// A rocker is wider than tall when horizontal = true (I/O left-right), else vertical.
inline void drawRocker(Graphics& g, Rectangle<float> r, bool on, Colour lamp, RockerStyle style, bool horizontal = false) {
    g.saveState();
    if (horizontal) g.addTransform(juce::AffineTransform::rotation(juce::MathConstants<float>::halfPi, r.getCentreX(), r.getCentreY()));
    auto b = horizontal ? Rectangle<float>(r.getHeight(), r.getWidth()).withCentre(r.getCentre()) : r;
    const float w = b.getWidth(), h = b.getHeight(), cx = b.getCentreX();
    g.setColour(Colour(0xff0a0b0c)); g.fillRoundedRectangle(b.expanded(1.5f), 4);                      // bezel
    auto plate = b.reduced(w * 0.08f, h * 0.05f);
    const float split = plate.getCentreY();
    auto top = plate.withBottom(split + 1), bottom = plate.withTop(split - 1);
    auto raised = on ? top : bottom, sunk = on ? bottom : top;                                      // the half pushed toward the user
    Colour body = style == RockerStyle::Classic ? Colour(0xff25272a) : style == RockerStyle::Slim ? Colour(0xffd8dad6) : style == RockerStyle::Neon ? Colour(0xff05090d) : Colour(0xff2a2c2f);
    const bool light = style == RockerStyle::Slim;
    // sunk half: darker, shadowed
    g.setGradientFill(juce::ColourGradient(body.darker(0.5f), 0, sunk.getY(), body.darker(0.2f), 0, sunk.getBottom(), false)); g.fillRoundedRectangle(sunk, 3);
    // raised half: catches the light
    g.setGradientFill(juce::ColourGradient(body.brighter(light ? 0.05f : 0.35f), 0, raised.getY(), body.darker(0.15f), 0, raised.getBottom(), false)); g.fillRoundedRectangle(raised, 3);
    g.setColour(Colour(0x55ffffff)); g.drawHorizontalLine(int(raised.getY() + 1.5f), raised.getX() + 3, raised.getRight() - 3);
    g.setColour(Colour(0xff000000).withAlpha(0.7f)); g.drawHorizontalLine(int(split), plate.getX(), plate.getRight());
    // markings
    g.setColour(light ? Colour(0xff222426) : Colour(0xfff2f2ee));
    const float ink = juce::jmin(w, h) * 0.17f;
    auto mark = Rectangle<float>(ink * 0.45f, ink * 2.0f).withCentre({cx, top.getCentreY()});             // "I"
    g.fillRoundedRectangle(mark, 1.0f);
    g.drawEllipse(Rectangle<float>(ink * 1.7f, ink * 1.7f).withCentre({cx, bottom.getCentreY()}), ink * 0.38f);    // "O"
    // lamp window (the lit half)
    if (style == RockerStyle::Lamp || style == RockerStyle::Neon) {
        auto win = Rectangle<float>(plate.getWidth() * 0.62f, h * 0.075f).withCentre({cx, top.getY() + h * 0.075f});
        if (on) { g.setColour(lamp.withAlpha(0.30f)); g.fillRoundedRectangle(win.expanded(3), 3); }
        g.setColour(on ? lamp : lamp.darker(0.9f)); g.fillRoundedRectangle(win, 1.5f);
        if (on) { g.setColour(Colour(0x66ffffff)); g.fillRoundedRectangle(win.withHeight(win.getHeight() * 0.4f), 1.0f); }
    }
    if (style == RockerStyle::Neon) {
        juce::Path p; p.addRoundedRectangle(plate, 3.0f);
        retro::glowStroke(g, p, on ? lamp : lamp.withAlpha(0.35f), 1.2f, on ? 1.0f : 0.2f);
    }
    g.restoreState();
}

// ---- toggles -----------------------------------------------------------------
enum class ToggleStyle { Bat, Lever, Mini };
inline constexpr const char* toggleNames[] = {"Bat toggle", "Lever toggle", "Mini toggle"};

inline void drawToggle(Graphics& g, Rectangle<float> r, bool on, ToggleStyle style, Colour accent = Colour(0xfff2a31b)) {
    const auto c = r.getCentre(); const float s = juce::jmin(r.getWidth(), r.getHeight() * 0.8f);
    const float nutR = s * (style == ToggleStyle::Mini ? 0.30f : 0.38f);
    // hex nut
    juce::Path nut; for (int i = 0; i < 6; ++i) { const float a = float(i) * juce::MathConstants<float>::pi / 3.0f; auto p = Point<float>(c.x + std::cos(a) * nutR * 1.15f, c.y + std::sin(a) * nutR * 1.15f); i == 0 ? nut.startNewSubPath(p) : nut.lineTo(p); } nut.closeSubPath();
    g.setColour(Colour(0x66000000)); g.fillPath(nut, juce::AffineTransform::translation(1.0f, 2.0f));
    g.setGradientFill(juce::ColourGradient(Colour(0xffe9ecee), c.x - nutR, c.y - nutR, Colour(0xff6b7378), c.x + nutR, c.y + nutR, false)); g.fillPath(nut);
    g.setColour(Colour(0xff2a2f32)); g.strokePath(nut, juce::PathStrokeType(0.8f));
    g.setGradientFill(juce::ColourGradient(Colour(0xff40454a), c.x - nutR, c.y - nutR, Colour(0xff0f1113), c.x + nutR, c.y + nutR, false)); g.fillEllipse(c.x - nutR * 0.62f, c.y - nutR * 0.62f, nutR * 1.24f, nutR * 1.24f);
    // lever
    const float len = r.getHeight() * (style == ToggleStyle::Bat ? 0.30f : 0.42f), dir = on ? -1.0f : 1.0f;
    const Point<float> tip(c.x, c.y + dir * len);
    const float w0 = s * (style == ToggleStyle::Bat ? 0.18f : 0.10f), w1 = s * (style == ToggleStyle::Bat ? 0.30f : 0.13f);
    juce::Path lev; lev.startNewSubPath(c.x - w0, c.y); lev.lineTo(tip.x - w1, tip.y); lev.lineTo(tip.x + w1, tip.y); lev.lineTo(c.x + w0, c.y); lev.closeSubPath();
    g.setColour(Colour(0x66000000)); g.fillPath(lev, juce::AffineTransform::translation(1.5f, 1.5f));
    g.setGradientFill(juce::ColourGradient(Colour(0xfff7f9f9), c.x - w1, 0, Colour(0xff7f8689), c.x + w1, 0, false)); g.fillPath(lev);
    g.setColour(Colour(0xff2a2f32)); g.strokePath(lev, juce::PathStrokeType(0.8f));
    const float tr = w1 * (style == ToggleStyle::Bat ? 1.1f : 1.0f);
    g.setGradientFill(juce::ColourGradient(Colour(0xffffffff), tip.x - tr, tip.y - tr, Colour(0xff8f979b), tip.x + tr, tip.y + tr, false));
    g.fillEllipse(tip.x - tr, tip.y - tr * 0.8f, tr * 2, tr * 1.6f);
    if (style == ToggleStyle::Lever && on) { g.setColour(accent); g.fillEllipse(tip.x - tr * 0.5f, tip.y - tr * 0.4f, tr, tr * 0.8f); }
}

// ---- push buttons --------------------------------------------------------------
enum class PushStyle { Square, Round, Dome, Chamfer, Neon, Flat, Arcade, Pill };
inline constexpr const char* pushNames[] = {"Square key", "Round chrome", "Illuminated dome", "Chamfer pad", "Neon outline", "Flat label", "Arcade", "Pill"};
inline constexpr int numPushStyles = 8;

inline void drawPush(Graphics& g, Rectangle<float> r, const juce::String& text, PushStyle style, Colour colour, bool lit, bool down) {
    const float off = down ? 1.5f : 0.0f;
    auto face = r.translated(0, off);
    auto label = [&](Rectangle<float> a, Colour c) { g.setColour(c); g.setFont(juce::FontOptions(juce::jmin(a.getHeight() * 0.42f, 13.0f), juce::Font::bold)); g.drawText(text, a, juce::Justification::centred); };
    switch (style) {
        case PushStyle::Square: {
            g.setColour(Colour(0xff0e1112)); g.fillRoundedRectangle(r.expanded(1.5f), 3);
            g.setColour(Colour(0xff060808)); g.fillRoundedRectangle(face.translated(0, 2), 2);
            const Colour base = lit ? colour : Colour(0xffd6d9c8);
            g.setGradientFill(juce::ColourGradient(base.brighter(0.2f), face.getX(), face.getY(), base.darker(down ? 0.35f : 0.18f), face.getRight(), face.getBottom(), false)); g.fillRoundedRectangle(face, 2);
            g.setColour(Colour(0x66ffffff)); g.drawHorizontalLine(int(face.getY() + 1), face.getX() + 2, face.getRight() - 2);
            label(face, lit ? Colour(0xff101214) : Colour(0xff26312e)); break; }
        case PushStyle::Round: {
            const float d = juce::jmin(r.getWidth(), r.getHeight()); auto cir = Rectangle<float>(d, d).withCentre(r.getCentre());
            g.setColour(Colour(0xff0a0b0c)); g.fillEllipse(cir.expanded(2));
            g.setGradientFill(juce::ColourGradient(Colour(0xfff6f8f8), cir.getX(), cir.getY(), Colour(0xff5e6569), cir.getRight(), cir.getBottom(), false)); g.fillEllipse(cir);
            auto inner = cir.reduced(d * 0.12f).translated(0, off);
            g.setGradientFill(juce::ColourGradient(Colour(0xffe2e6e8), inner.getX(), inner.getY(), Colour(0xff80888c), inner.getRight(), inner.getBottom(), false)); g.fillEllipse(inner);
            if (lit) { g.setColour(colour.withAlpha(0.35f)); g.drawEllipse(cir.reduced(d * 0.03f), d * 0.07f); g.setColour(colour); g.drawEllipse(cir.reduced(d * 0.07f), d * 0.03f); }
            label(inner, Colour(0xff222629)); break; }
        case PushStyle::Dome: {
            const float d = juce::jmin(r.getWidth(), r.getHeight()); auto cir = Rectangle<float>(d, d).withCentre(r.getCentre()).translated(0, off * 0.5f);
            g.setColour(Colour(0xff060708)); g.fillEllipse(cir.expanded(2.5f));
            if (lit) { g.setColour(colour.withAlpha(0.28f)); g.fillEllipse(cir.expanded(d * 0.25f)); }
            const Colour base = lit ? colour : colour.darker(0.85f);
            g.setGradientFill(juce::ColourGradient(base.brighter(lit ? 0.9f : 0.2f), cir.getX() + d * 0.3f, cir.getY() + d * 0.25f, base.darker(0.5f), cir.getRight(), cir.getBottom(), true)); g.fillEllipse(cir);
            g.setColour(Colour(0x77ffffff)); g.fillEllipse(Rectangle<float>(d * 0.5f, d * 0.26f).withCentre({cir.getCentreX() - d * 0.06f, cir.getY() + d * 0.3f}));
            label(cir, lit ? Colour(0xff101214) : Colour(0xffe9e9e2)); break; }
        case PushStyle::Chamfer: {
            auto path = retro::chamfer(face.reduced(1), face.getHeight() * 0.28f);
            g.setColour(Colour(0x66000000)); g.fillPath(path, juce::AffineTransform::translation(0, 2));
            g.setGradientFill(juce::ColourGradient((lit ? colour : Colour(0xff3b4147)).brighter(0.15f), 0, face.getY(), (lit ? colour : Colour(0xff1c2024)).darker(0.4f), 0, face.getBottom(), false)); g.fillPath(path);
            g.setColour(colour.withAlpha(lit ? 0.9f : 0.35f)); g.strokePath(path, juce::PathStrokeType(1.2f));
            label(face, lit ? Colour(0xff0b0d0f) : Colour(0xffd8dcde)); break; }
        case PushStyle::Neon: {
            auto path = retro::chamfer(face.reduced(2), face.getHeight() * 0.3f);
            g.setColour(Colour(0xff04080b)); g.fillPath(path);
            if (lit) { g.setColour(colour.withAlpha(0.18f)); g.fillPath(path); }
            retro::glowStroke(g, path, lit ? colour : colour.withAlpha(0.4f), 1.3f, lit ? 1.0f : 0.15f);
            label(face, lit ? colour.brighter(0.8f) : colour.withAlpha(0.6f)); break; }
        case PushStyle::Flat: {
            g.setColour(lit ? colour : Colour(0xff2b3033)); g.fillRoundedRectangle(face, 2);
            g.setColour(lit ? colour.darker(0.5f) : Colour(0xff0d0f10)); g.drawRoundedRectangle(face, 2, 1.0f);
            label(face, lit ? Colour(0xff0d0f10) : Colour(0xffb6bdc0)); break; }
        case PushStyle::Arcade: {
            const float d = juce::jmin(r.getWidth(), r.getHeight()); auto cir = Rectangle<float>(d, d).withCentre(r.getCentre());
            g.setColour(Colour(0xff17191b)); g.fillEllipse(cir.expanded(1));
            g.setGradientFill(juce::ColourGradient(Colour(0xff404448), cir.getX(), cir.getY(), Colour(0xff0b0c0d), cir.getRight(), cir.getBottom(), false)); g.fillEllipse(cir);
            auto cap = cir.reduced(d * 0.12f).translated(0, off);
            const Colour base = lit ? colour : colour.darker(0.55f);
            g.setColour(base.darker(0.6f)); g.fillEllipse(cap.translated(0, down ? 0.0f : 2.5f));
            g.setGradientFill(juce::ColourGradient(base.brighter(0.5f), cap.getX(), cap.getY(), base.darker(0.2f), cap.getRight(), cap.getBottom(), false)); g.fillEllipse(cap);
            g.setColour(Colour(0x66ffffff)); g.fillEllipse(Rectangle<float>(cap.getWidth() * 0.5f, cap.getHeight() * 0.24f).withCentre({cap.getCentreX(), cap.getY() + cap.getHeight() * 0.24f}));
            label(cap, Colour(0xff101214)); break; }
        case PushStyle::Pill: {
            const float rad = face.getHeight() * 0.5f;
            g.setColour(Colour(0xff0a0b0c)); g.fillRoundedRectangle(r.expanded(1.5f), rad + 1.5f);
            g.setGradientFill(juce::ColourGradient((lit ? colour : Colour(0xffcfd2d0)).brighter(0.25f), 0, face.getY(), (lit ? colour : Colour(0xff8d9593)).darker(0.25f), 0, face.getBottom(), false)); g.fillRoundedRectangle(face, rad);
            g.setColour(Colour(0x55ffffff)); g.drawHorizontalLine(int(face.getY() + 2), face.getX() + rad, face.getRight() - rad);
            label(face, Colour(0xff1d2123)); break; }
    }
}

// Four-position bank of illuminated keys that behave like radio buttons (index `selected`).
inline void drawKeyBank(Graphics& g, Rectangle<float> r, const juce::StringArray& names, int selected, PushStyle style, Colour colour) {
    const int n = names.size(); if (n == 0) return;
    const float gap = 4.0f, w = (r.getWidth() - gap * float(n - 1)) / float(n);
    for (int i = 0; i < n; ++i) drawPush(g, {r.getX() + float(i) * (w + gap), r.getY(), w, r.getHeight()}, names[i], style, colour, i == selected, false);
}
} // namespace goodlookinui::juce_adapter::switches
