// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marcos Deida
#pragma once
#include "GoodLookinUI.h"
#include "Meters.h"
#include "Retro.h"
#include "Switches.h"
#include "SpectrumRenderer.h"
#include <goodlookinui/Spectrum.h>

// A window that shows every part the toolkit can draw, with its code name beside it:
// knobs, plates, meters, sliders, switches, lamps and the spectrum looks. It needs no plugin; it feeds the
// meters and the analyser with a synthetic signal.
namespace goodlookinui::juce_adapter {

class Gallery : public juce::Component, private juce::Timer {
public:
    enum class Page { Knobs, Plates, Meters, Sliders, Switches, Spectrum };
    static constexpr int numPages = 6;
    static const char* pageName(int i) { static const char* n[] = {"Knobs", "Plates", "Meters", "Sliders", "Switches & lamps", "Spectrum"}; return n[i]; }

    Gallery() {
        static const struct { SpectrumSettings::Style style; int smooth; int map; } looks[] = {
            {SpectrumSettings::Style::Standard, 0, 0}, {SpectrumSettings::Style::Multicolour, 0, 0}, {SpectrumSettings::Style::Bars, 0, 0},
            {SpectrumSettings::Style::Multicolour, 3, 0}, {SpectrumSettings::Style::Spectrogram, 0, 0}, {SpectrumSettings::Style::Spectrogram, 3, 1}};
        for (int i = 0; i < 6; ++i) { views[size_t(i)] = std::make_unique<SpectrumRenderer>(ring, ring, [] { return 48000.0; });
            SpectrumSettings s; s.style = looks[i].style; s.smoothing = looks[i].smooth; s.spectrogramMap = looks[i].map; s.sizeIndex = 4; views[size_t(i)]->setSettings(s); }
        for (int i = 0; i < numPages; ++i) { auto& b = tabs[size_t(i)]; b.setButtonText(pageName(i)); b.setClickingTogglesState(true); b.setRadioGroupId(7); addAndMakeVisible(b); b.onClick = [this, i] { setPage(i); }; }
        for (auto* b : {&glowBtn, &animBtn}) { addAndMakeVisible(b); b->setToggleState(true, juce::dontSendNotification); b->onClick = [this] { retro::glowEnabled = glowBtn.getToggleState(); repaint(); }; }
        glowBtn.setButtonText("Glow / neon"); animBtn.setButtonText("Animate");
        tabs[0].setToggleState(true, juce::dontSendNotification);
        setSize(1280, 860); startTimerHz(30);
    }
    ~Gallery() override { retro::glowEnabled = true; }

    int getPage() const { return page; }
    void setPage(int i) { page = juce::jlimit(0, numPages - 1, i); for (int k = 0; k < numPages; ++k) tabs[size_t(k)].setToggleState(k == page, juce::dontSendNotification); repaint(); }
    // Advances the synthetic signal by `seconds` (also used by the self-test).
    void advance(float seconds) {
        clock += seconds;
        synth(seconds);
        for (auto& v : views) v->update(seconds);
        metersTick(seconds);
    }

    void resized() override {
        auto a = getLocalBounds().reduced(10, 8); auto top = a.removeFromTop(28);
        for (auto& b : tabs) b.setBounds(top.removeFromLeft(150).reduced(2, 0));
        animBtn.setBounds(top.removeFromRight(110)); glowBtn.setBounds(top.removeFromRight(130));
    }
    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xff14181c));
        auto area = getLocalBounds().reduced(10, 8).withTrimmedTop(36).toFloat();
        switch (Page(page)) {
            case Page::Knobs: paintKnobs(g, area); break;
            case Page::Plates: paintPlates(g, area); break;
            case Page::Meters: paintMeters(g, area); break;
            case Page::Sliders: paintSliders(g, area); break;
            case Page::Switches: paintSwitches(g, area); break;
            case Page::Spectrum: paintSpectrum(g, area); break;
        }
    }

    // Renders one page to an image (for tests and documentation).
    juce::Image renderPage(int p, int seconds = 3, float scale = 1.0f) {
        setPage(p);
        createComponentSnapshot(getLocalBounds(), true, 0.25f);          // one paint first, so the spectrogram history gets its size
        for (int i = 0; i < seconds * 30; ++i) advance(1.0f / 30.0f);
        return createComponentSnapshot(getLocalBounds(), true, scale);
    }

private:
    int page = 0; float clock = 0.0f;
    std::array<juce::TextButton, numPages> tabs; juce::ToggleButton glowBtn, animBtn;
    spectrum::SampleRing ring; double phase[8] = {0}; float pink[3] = {0, 0, 0}; juce::Random rng{12};
    // meter state (own ballistics: gallery has no processor)
    float vuPos = 0, ladder = -99, ladderPeak = -99, levelL = 0, levelR = 0, hold = -99; std::vector<float> history;

    void timerCallback() override { if (animBtn.getToggleState()) { advance(1.0f / 30.0f); repaint(); } }

    void synth(float seconds) {
        const int n = juce::jmax(1, int(seconds * 48000.0f)); std::vector<float> buf(std::size_t(n), 0.0f);
        static const double tones[] = {55, 140, 400, 1000, 2800, 6000, 12000};
        const float sweep = 0.5f + 0.5f * std::sin(clock * 0.9f);
        for (int i = 0; i < n; ++i) {
            float v = 0; for (int t = 0; t < 7; ++t) { v += (0.03f + 0.04f * sweep * float(t % 3 == 0)) * std::sin(float(phase[t])); phase[t] += juce::MathConstants<double>::twoPi * tones[t] / 48000.0; }
            const float w = rng.nextFloat() * 2 - 1; pink[0] = 0.99765f * pink[0] + w * 0.099046f; pink[1] = 0.963f * pink[1] + w * 0.2965164f; pink[2] = 0.57f * pink[2] + w * 1.0526913f;
            buf[size_t(i)] = v + 0.03f * (pink[0] + pink[1] + pink[2] + w * 0.1848f) * (0.5f + sweep);
        }
        const float* ch[1] = {buf.data()}; ring.push(ch, 1, n);
        float pk = 0, ss = 0; for (float v : buf) { pk = std::max(pk, std::fabs(v)); ss += v * v; }
        const float rms = std::sqrt(ss / float(n)); lastPeak = pk; lastRms = rms;
    }
    float lastPeak = 0, lastRms = 0;
    void metersTick(float dt) {
        auto db = [](float a) { return a < 1e-5f ? -99.0f : 20.0f * std::log10(a); };
        const float pkDb = db(lastPeak); ladder = pkDb >= ladder ? pkDb : std::max(pkDb, ladder - 24.0f * dt);
        if (pkDb >= hold) hold = pkDb; else hold = std::max(-99.0f, hold - 10.0f * dt);
        const float target = meters::vuPosition(lastRms / 0.12589f); vuPos += (target - vuPos) * std::min(1.0f, 12.0f * dt);
        levelL = meters::dbPosition(ladder); levelR = meters::dbPosition(ladder - 3.0f * (0.5f + 0.5f * std::sin(clock * 1.7f)));
        history.push_back(levelL); if (history.size() > 90) history.erase(history.begin());
    }

    static void label(juce::Graphics& g, juce::Rectangle<float> r, const juce::String& t, float size = 10.0f) {
        g.setColour(juce::Colour(0xff9fb0b8)); g.setFont(juce::FontOptions(size, juce::Font::bold)); g.drawText(t, r, juce::Justification::centred, true);
    }
    static juce::Colour paletteColour(int i) { static const juce::Colour c[] = {juce::Colour(0xff5793b8), juce::Colour(0xffb58c59), juce::Colour(0xff6ea68b), juce::Colour(0xffb96057), juce::Colour(0xffc7a66a), juce::Colour(0xffcbcbbc), juce::Colour(0xff3ae6ee), juce::Colour(0xffff4fd8)}; return c[i % 8]; }

    void paintKnobs(juce::Graphics& g, juce::Rectangle<float> a) {
        const int cols = 9, rows = (int(std::size(knobStyles)) + cols - 1) / cols;
        const float cw = a.getWidth() / float(cols), ch = a.getHeight() / float(rows);
        for (int i = 0; i < int(std::size(knobStyles)); ++i) {
            auto cell = juce::Rectangle<float>(a.getX() + float(i % cols) * cw, a.getY() + float(i / cols) * ch, cw, ch).reduced(4);
            g.setColour(juce::Colour(0xff20272c)); g.fillRoundedRectangle(cell, 6);
            Item item; item.style = knobStyles[i].code; item.colour = "#" + paletteColour(i).toDisplayString(false).toStdString();
            const double prop = 0.5 + 0.5 * std::sin(double(clock) * 0.8 + double(i) * 0.5);
            drawKnob(g, cell.withTrimmedBottom(20).reduced(8), prop, item, true);
            label(g, cell.removeFromBottom(18), knobStyles[i].code, 11.0f);
        }
    }
    void paintPlates(juce::Graphics& g, juce::Rectangle<float> a) {
        const int cols = 8, rows = (int(std::size(faceplates)) + cols - 1) / cols;
        const float cw = a.getWidth() / float(cols), ch = a.getHeight() / float(rows);
        for (int i = 0; i < int(std::size(faceplates)); ++i) {
            const auto& f = faceplates[i]; auto cell = juce::Rectangle<float>(a.getX() + float(i % cols) * cw, a.getY() + float(i / cols) * ch, cw, ch).reduced(4);
            drawPanel(g, cell, juce::Colour(f.panelTop), juce::Colour(f.panelBottom), f.finish);
            g.setColour(juce::Colour(f.text)); g.setFont(juce::FontOptions(12.0f, juce::Font::bold)); g.drawText(f.code, cell.reduced(8, 6).removeFromTop(16), juce::Justification::centredLeft);
            g.setColour(juce::Colour(f.textMid)); g.setFont(juce::FontOptions(9.0f)); g.drawText(juce::String(f.name) + "  (finish " + juce::String(f.finish) + ")", cell.reduced(8, 6).withTrimmedTop(17).removeFromTop(13), juce::Justification::centredLeft, true);
            Item item; item.style = "Brit"; item.colour = "#" + juce::Colour(f.accent).toDisplayString(false).toStdString();
            drawKnob(g, cell.withTrimmedTop(cell.getHeight() * 0.42f).reduced(cell.getWidth() * 0.28f, 6), 0.35 + 0.3 * std::sin(double(clock) + double(i)), item, true);
        }
    }
    void paintMeters(juce::Graphics& g, juce::Rectangle<float> a) {
        struct Face { const char* name; int kind; };
        static const Face faces[] = {{"VU cream", 0}, {"VU black", 1}, {"PPM", 2}, {"Stereo ladders", 3}, {"Flat bars", 4}, {"Segmented arc", 5}, {"Vector fan", 6}, {"Bar wedge", 7},
                                     {"Vector mountains", 8}, {"Orange LCD", 9}, {"Arc gauge", 10}, {"Colour columns", 11}, {"Neon bars", 12}, {"Hex ring", 13}, {"Glitch scope", 14}, {"Yellow chassis", 15}};
        const int cols = 4, rows = 4; const float cw = a.getWidth() / float(cols), ch = a.getHeight() / float(rows);
        for (int i = 0; i < 16; ++i) {
            auto cell = juce::Rectangle<float>(a.getX() + float(i % cols) * cw, a.getY() + float(i / cols) * ch, cw, ch).reduced(6);
            auto lab = cell.removeFromBottom(16); auto r = cell; const float t = clock;
            switch (faces[i].kind) {
                case 0: meters::drawVuMeter(g, r, vuPos, meters::VuFace::Cream); break;
                case 1: meters::drawVuMeter(g, r, vuPos, meters::VuFace::Black); break;
                case 2: meters::drawPpm(g, r, levelL); break;
                case 3: meters::drawStereoLadder(g, r, ladder, ladder - 2.0f, hold); break;
                case 4: retro::drawFlatBars(g, r, levelL, levelR, meters::dbPosition(hold), meters::dbPosition(hold), juce::Colour(0xff3ccf5a)); break;
                case 5: g.setColour(juce::Colour(0xff08090a)); g.fillRoundedRectangle(r, 4); meters::drawSegmentArc(g, r.reduced(3), levelL, juce::Colour(0xffffb02e)); break;
                case 6: meters::drawVectorFan(g, r, levelL, juce::Colour(0xff39ff7a)); break;
                case 7: retro::drawBarWedge(g, r, levelL, juce::Colour(0xffd6ff2e), t); break;
                case 8: retro::drawMountains(g, r, levelL, levelR, juce::Colour(0xff39ff7a), t); break;
                case 9: retro::drawLcdPanel(g, r, juce::String(20.0f * std::log10(std::max(lastRms, 1e-5f)), 1), "RMS dBFS", juce::String(hold, 1), "PEAK dBFS", juce::Colour(0xffff9a1f), t); break;
                case 10: retro::drawArcGauge(g, r, levelL, juce::Colour(0xffffa31a), t); break;
                case 11: retro::drawColourColumns(g, r, levelL, levelR, meters::dbPosition(hold), meters::dbPosition(hold), t); break;
                case 12: retro::drawNeonBars(g, r, levelL, meters::dbPosition(hold), juce::Colour(0xff39f2ff), juce::Colour(0xffff4fd8), t); break;
                case 13: retro::drawHexRing(g, r, levelL, juce::Colour(0xff39f2ff), juce::Colour(0xffff4fd8), t); break;
                case 14: retro::drawScope(g, r, history, std::fmod(clock, 6.0f) < 1.5f, juce::Colour(0xff39ff9a), t); break;
                default: retro::drawChassis(g, r, levelL, juce::Colour(0xfff2c81e), juce::Colour(0xff39f2ff), t); break;
            }
            label(g, lab, faces[i].name, 11.0f);
        }
    }
    void paintSliders(juce::Graphics& g, juce::Rectangle<float> a) {
        const double v = 0.5 + 0.45 * std::sin(double(clock) * 0.9);
        auto left = a.removeFromLeft(a.getWidth() * 0.62f).reduced(6);
        static const char* names[] = {"Oval cap", "Console cap", "Neon", "Bar graph", "Chamfer", "Wedge ramp", "Flat"};
        const float rh = left.getHeight() / 7.0f;
        for (int i = 0; i < 7; ++i) {
            auto row = juce::Rectangle<float>(left.getX(), left.getY() + float(i) * rh, left.getWidth(), rh).reduced(4);
            g.setColour(juce::Colour(0xff1c2328)); g.fillRoundedRectangle(row, 6);
            auto sl = row.reduced(150, 8);
            label(g, row.removeFromLeft(140), names[i], 11.0f);
            const float p = float(v);
            if (i == 0) meters::drawSlotFader(g, sl, p, meters::FaderCap::Oval); else if (i == 1) meters::drawSlotFader(g, sl, p, meters::FaderCap::Console, juce::Colour(0xff3f8fd8));
            else retro::drawSliderH(g, sl, p, static_cast<retro::SliderStyle>(i - 2), i == 3 ? juce::Colour(0xffd6ff2e) : i == 5 ? juce::Colour(0xffffa31a) : i == 6 ? juce::Colour(0xff3ccf5a) : juce::Colour(0xff39f2ff), clock);
        }
        // vertical set
        auto right = a.reduced(6); g.setColour(juce::Colour(0xff1c2328)); g.fillRoundedRectangle(right, 6);
        const float cw = right.getWidth() / 5.0f;
        for (int i = 0; i < 5; ++i) {
            auto c = juce::Rectangle<float>(right.getX() + float(i) * cw, right.getY(), cw, right.getHeight()).reduced(12, 14); auto lab = c.removeFromBottom(16);
            retro::drawSlider(g, c, float(0.5 + 0.45 * std::sin(double(clock) * 0.9 + double(i) * 0.7)), static_cast<retro::SliderStyle>(i), i == 1 ? juce::Colour(0xffd6ff2e) : i == 3 ? juce::Colour(0xffffa31a) : i == 4 ? juce::Colour(0xff3ccf5a) : juce::Colour(0xff39f2ff), true, clock);
            static const char* vn[] = {"Neon", "Bar", "Chamfer", "Wedge", "Flat"}; label(g, lab, vn[i], 10.0f);
        }
    }
    void paintSwitches(juce::Graphics& g, juce::Rectangle<float> a) {
        const bool phaseOn = std::fmod(clock, 2.0f) < 1.0f;
        auto section = [&](juce::Rectangle<float> r, const char* title) { g.setColour(juce::Colour(0xff1c2328)); g.fillRoundedRectangle(r, 6); label(g, r.removeFromTop(18), title, 11.0f); return r.reduced(8); };
        auto top = a.removeFromTop(a.getHeight() * 0.30f), mid = a.removeFromTop(a.getHeight() * 0.42f), bot = a;
        auto rk = section(top.removeFromLeft(top.getWidth() * 0.5f).reduced(4), "Rocker switches (on / off)"); auto tg = section(top.reduced(4), "Toggles");
        const float cw = rk.getWidth() / 8.0f;
        for (int i = 0; i < 4; ++i) for (int s = 0; s < 2; ++s) {
            auto c = juce::Rectangle<float>(rk.getX() + float(i * 2 + s) * cw, rk.getY(), cw, rk.getHeight()).reduced(8, 4); auto lab = c.removeFromBottom(14);
            switches::drawRocker(g, c, s == 0 ? phaseOn : !phaseOn, juce::Colour(i == 3 ? 0xff39f2ff : 0xffff3b30), static_cast<switches::RockerStyle>(i)); label(g, lab, juce::String(switches::rockerNames[i]).replaceFirstOccurrenceOf(" rocker", ""), 9.0f); }
        const float tw = tg.getWidth() / 6.0f;
        for (int i = 0; i < 3; ++i) for (int s = 0; s < 2; ++s) {
            auto c = juce::Rectangle<float>(tg.getX() + float(i * 2 + s) * tw, tg.getY(), tw, tg.getHeight()).reduced(8, 4); auto lab = c.removeFromBottom(14);
            switches::drawToggle(g, c, s == 0 ? phaseOn : !phaseOn, static_cast<switches::ToggleStyle>(i)); label(g, lab, juce::String(switches::toggleNames[i]).replaceFirstOccurrenceOf(" toggle", ""), 9.0f); }
        auto pb = section(mid.reduced(4), "Push buttons (up = off, lit = on, alternating)");
        const float pw = pb.getWidth() / float(switches::numPushStyles);
        for (int i = 0; i < switches::numPushStyles; ++i) for (int s = 0; s < 2; ++s) {
            auto c = juce::Rectangle<float>(pb.getX() + float(i) * pw, pb.getY() + float(s) * pb.getHeight() * 0.5f, pw, pb.getHeight() * 0.5f).reduced(10, 6); auto lab = c.removeFromBottom(s == 1 ? 12.0f : 0.0f);
            switches::drawPush(g, c.withTrimmedBottom(0), s == 0 ? "EQ" : "IN", static_cast<switches::PushStyle>(i), juce::Colour(i == 4 ? 0xff39f2ff : i == 5 ? 0xff9dff3a : 0xffff9a1f), s == 0 ? phaseOn : !phaseOn, false);
            if (s == 1) label(g, lab, switches::pushNames[i], 9.0f); }
        auto lm = section(bot.reduced(4), "Lamps, icons, keys, readouts");
        float x = lm.getX() + 10;
        for (auto ic : {retro::Icon::Battery, retro::Icon::Warning, retro::Icon::ArrowLeft, retro::Icon::ArrowRight, retro::Icon::Beam, retro::Icon::Temp}) { retro::drawIconLamp(g, {x, lm.getY() + 6, 40, 40}, ic, juce::Colour(0xffff9a1f), phaseOn); x += 50; }
        for (int i = 0; i < 4; ++i) { meters::drawLamp(g, {x + 20, lm.getY() + 26}, 9, juce::Colour(i % 2 ? 0xff39ff7a : 0xffff3b30), (i % 2 == 0) == phaseOn); x += 44; }
        meters::drawLitKey(g, {x + 6, lm.getY() + 8, 60, 34}, "EQ", phaseOn, juce::Colour(0xffff9a1f)); meters::drawLitKey(g, {x + 76, lm.getY() + 8, 60, 34}, "IN", !phaseOn, juce::Colour(0xff39d6ff)); x += 156;
        meters::drawSevenSegment(g, {x, lm.getY() + 6, 150, 44}, juce::String(hold, 1), juce::Colour(0xff39ff7a)); x += 170;
        meters::drawSevenSegment(g, {x, lm.getY() + 6, 150, 44}, juce::String(std::fmod(clock * 7.3f, 100.0f), 1), juce::Colour(0xffffb02e));
        switches::drawKeyBank(g, {lm.getX() + 10, lm.getY() + 62, lm.getWidth() * 0.5f, 36}, juce::StringArray{"FLAT", "WARM", "BRIGHT", "AIR"}, int(std::fmod(clock, 4.0f)), switches::PushStyle::Square, juce::Colour(0xffff9a1f));
        switches::drawKeyBank(g, {lm.getX() + lm.getWidth() * 0.55f, lm.getY() + 62, lm.getWidth() * 0.43f, 36}, juce::StringArray{"A", "B", "C", "D"}, int(std::fmod(clock, 4.0f)), switches::PushStyle::Neon, juce::Colour(0xff39f2ff));
    }
    void paintSpectrum(juce::Graphics& g, juce::Rectangle<float> a) {
        static const char* names[] = {"Standard", "Multicolour", "Bars", "Multicolour, 1/3-octave smoothing", "Spectrogram (heat map)", "Spectrogram (range colours, 1/3 oct)"};
        const int cols = 2, rows = 3; const float cw = a.getWidth() / float(cols), ch = a.getHeight() / float(rows);
        for (int i = 0; i < 6; ++i) {
            auto cell = juce::Rectangle<float>(a.getX() + float(i % cols) * cw, a.getY() + float(i / cols) * ch, cw, ch).reduced(5); auto lab = cell.removeFromBottom(15);
            g.setColour(juce::Colour(0xff0e161a)); g.fillRoundedRectangle(cell, 6);
            auto plot = cell.reduced(6);
            views[size_t(i)]->paint(g, plot);
            label(g, lab, names[i], 10.0f);
        }
    }
    std::array<std::unique_ptr<SpectrumRenderer>, 6> views;
};

} // namespace goodlookinui::juce_adapter
