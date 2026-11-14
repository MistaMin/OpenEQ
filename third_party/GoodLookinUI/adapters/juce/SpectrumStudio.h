// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marcos Deida
#pragma once
#include "SpectrumRenderer.h"
#include "GoodLookinUI.h"
#if GOODLOOKINUI_ENABLE_EDITOR
#include "Studio.h"

namespace goodlookinui::juce_adapter {

// Developer-only controls for the spectrum analyser. Every change applies immediately.
class SpectrumStudio : public juce::Component {
public:
    std::function<void(const SpectrumSettings&)> onChanged;

    SpectrumStudio() {
        for (auto* c : std::vector<juce::Component*>{&onBox, &sizeBox, &styleBox, &sourceBox, &smoothBox, &mapBox, &holdBox, &stdChip, &tilt, &fall, &opacity, &note})
            addAndMakeVisible(c);
        for (auto& chip : chips) addAndMakeVisible(chip);
        onBox.setButtonText("Spectrum on");
        for (int n : spectrum::fftSizes) sizeBox.addItem(juce::String(n) + " samples", n);
        styleBox.addItem("Standard (one colour)", 1); styleBox.addItem("Multicolour (per frequency range)", 2); styleBox.addItem("Bars (multicolour)", 3); styleBox.addItem("Spectrogram (waterfall)", 4);
        sourceBox.addItem("Analyse: output", 1); sourceBox.addItem("Analyse: input", 2);
        holdBox.setButtonText("Peak hold");
        const char* sm[] = {"Smoothing: off", "Smoothing: 1 octave", "Smoothing: 1/3 octave", "Smoothing: 1/6 octave", "Smoothing: 1/12 octave", "Smoothing: 1/24 octave"};
        for (int i = 0; i < 6; ++i) smoothBox.addItem(sm[i], i + 1);
        mapBox.addItem("Spectrogram: heat map", 1); mapBox.addItem("Spectrogram: range colours", 2);
        auto slider = [](juce::Slider& s, double lo, double hi, double step, const char* suffix) {
            s.setSliderStyle(juce::Slider::LinearHorizontal); s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 62, 20);
            s.setRange(lo, hi, step); s.setTextValueSuffix(suffix); };
        slider(tilt, -6, 9, 0.5, " dB/oct"); slider(fall, 2, 120, 1, " dB/s"); slider(opacity, 0.1, 1.0, 0.05, "");
        tilt.setTooltip("Spectral tilt around 1 kHz. 4.5 dB/oct makes pink noise look flat; 0 is the true response.");
        fall.setTooltip("How fast the display drops after a peak");
        sizeBox.setTooltip("FFT block size: bigger = finer frequency detail and slower response; smaller = faster and coarser");
        for (int i = 0; i < 7; ++i) chips[size_t(i)].onChange = [this](juce::Colour) { push(); };
        stdChip.onChange = [this](juce::Colour) { push(); };
        for (auto* b : {&onBox, &holdBox}) b->onClick = [this] { push(); };
        sizeBox.onChange = [this] { push(); }; styleBox.onChange = [this] { push(); }; sourceBox.onChange = [this] { push(); }; smoothBox.onChange = [this] { push(); }; mapBox.onChange = [this] { push(); };
        for (auto* s : {&tilt, &fall, &opacity}) s->onValueChange = [this] { push(); };
        note.setText("Colour of each frequency range (click a chip for the wheel)", juce::dontSendNotification);
    }
    void show(const SpectrumSettings& s) {
        busy = true;
        onBox.setToggleState(s.on, juce::dontSendNotification); sizeBox.setSelectedId(spectrum::fftSizes[s.sizeIndex], juce::dontSendNotification);
        styleBox.setSelectedId(s.style == SpectrumSettings::Style::Standard ? 1 : s.style == SpectrumSettings::Style::Multicolour ? 2 : s.style == SpectrumSettings::Style::Bars ? 3 : 4, juce::dontSendNotification);
        for (int i = 0; i < 6; ++i) if (SpectrumSettings::smoothingChoices[i] == s.smoothing) smoothBox.setSelectedId(i + 1, juce::dontSendNotification);
        mapBox.setSelectedId(s.spectrogramMap + 1, juce::dontSendNotification);
        sourceBox.setSelectedId(s.useOutput ? 1 : 2, juce::dontSendNotification); holdBox.setToggleState(s.peakHold, juce::dontSendNotification);
        tilt.setValue(s.tilt, juce::dontSendNotification); fall.setValue(s.fall, juce::dontSendNotification); opacity.setValue(s.opacity, juce::dontSendNotification);
        stdChip.set(juce::Colour(s.standard)); for (int i = 0; i < 7; ++i) chips[size_t(i)].set(juce::Colour(s.range[i]));
        busy = false;
    }
    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xff18222c)); g.setColour(juce::Colour(0xff9dff3a)); g.drawRect(getLocalBounds(), 2);
        g.setColour(juce::Colour(0xff9fb0b8)); g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        g.drawText("SPECTRUM (FFT)    tilt  /  fall-off  /  opacity", 10, 3, 360, 12, juce::Justification::centredLeft);
        for (int i = 0; i < 7; ++i) g.drawText(juce::String(spectrum::ranges[i].name) + " " + rangeText(i), captionRects[size_t(i)], juce::Justification::centred);
        g.drawText("STANDARD", captionRects[7], juce::Justification::centred);
    }
    void resized() override {
        auto a = getLocalBounds().reduced(8, 6); a.removeFromTop(8);
        auto r1 = a.removeFromTop(24); a.removeFromTop(4);
        onBox.setBounds(r1.removeFromLeft(120)); sizeBox.setBounds(r1.removeFromLeft(140)); r1.removeFromLeft(6);
        styleBox.setBounds(r1.removeFromLeft(280)); r1.removeFromLeft(6); sourceBox.setBounds(r1.removeFromLeft(160)); r1.removeFromLeft(6);
        holdBox.setBounds(r1.removeFromLeft(110));
        auto r2 = a.removeFromTop(24); a.removeFromTop(4);
        tilt.setBounds(r2.removeFromLeft(250)); r2.removeFromLeft(8); fall.setBounds(r2.removeFromLeft(220)); r2.removeFromLeft(8); opacity.setBounds(r2.removeFromLeft(200)); r2.removeFromLeft(8); smoothBox.setBounds(r2.removeFromLeft(190)); r2.removeFromLeft(6); mapBox.setBounds(r2.removeFromLeft(190));
        auto cap = a.removeFromTop(11); auto r3 = a.removeFromTop(24); a.removeFromTop(2);
        const int w = juce::jmin(108, a.getWidth() / 8 - 4);
        for (int i = 0; i < 8; ++i) {
            auto rc = r3.removeFromLeft(w); auto cc = cap.removeFromLeft(w); r3.removeFromLeft(4); cap.removeFromLeft(4);
            (i < 7 ? chips[size_t(i)] : stdChip).setBounds(rc); captionRects[size_t(i)] = cc;
        }
        note.setBounds(a);
    }
private:
    static juce::String rangeText(int i) {
        auto f = [](float hz) { return hz >= 1000 ? juce::String(hz / 1000.0f, hz >= 10000 ? 0 : 1) + "k" : juce::String(int(hz)); };
        return f(spectrum::ranges[i].lo) + "-" + f(spectrum::ranges[i].hi);
    }
    void push() {
        if (busy || !onChanged) return;
        SpectrumSettings s;
        s.on = onBox.getToggleState(); s.sizeIndex = 0; for (int i = 0; i < int(std::size(spectrum::fftSizes)); ++i) if (spectrum::fftSizes[i] == sizeBox.getSelectedId()) s.sizeIndex = i;
        const int st = styleBox.getSelectedId(); s.style = st == 2 ? SpectrumSettings::Style::Multicolour : st == 3 ? SpectrumSettings::Style::Bars : st == 4 ? SpectrumSettings::Style::Spectrogram : SpectrumSettings::Style::Standard;
        s.smoothing = SpectrumSettings::smoothingChoices[juce::jlimit(0, 5, smoothBox.getSelectedId() - 1)]; s.spectrogramMap = juce::jmax(0, mapBox.getSelectedId() - 1);
        s.useOutput = sourceBox.getSelectedId() != 2; s.peakHold = holdBox.getToggleState();
        s.tilt = float(tilt.getValue()); s.fall = float(fall.getValue()); s.opacity = float(opacity.getValue());
        s.standard = stdChip.get().getARGB(); for (int i = 0; i < 7; ++i) s.range[i] = chips[size_t(i)].get().getARGB();
        onChanged(s);
    }
    juce::ToggleButton onBox, holdBox; juce::ComboBox sizeBox, styleBox, sourceBox, smoothBox, mapBox; juce::Slider tilt, fall, opacity; juce::Label note;
    ColourChip chips[7], stdChip; juce::Rectangle<int> captionRects[8]; bool busy = false;
};

} // namespace goodlookinui::juce_adapter
#endif
