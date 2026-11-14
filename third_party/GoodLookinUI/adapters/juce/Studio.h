// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marcos Deida
#pragma once
#include "GoodLookinUI.h"
#include <fstream>
#include <functional>
#include <sstream>

// Development-only tools. Include only when GOODLOOKINUI_ENABLE_EDITOR is set;
// release builds never see this header.
#if GOODLOOKINUI_ENABLE_EDITOR
namespace goodlookinui::juce_adapter {

// Hue ring with a saturation/value square inside it.
class ColourWheel : public juce::Component {
public:
    std::function<void(juce::Colour)> onChange;
    void setColour(juce::Colour c) {
        float nh, ns, nv; c.getHSB(nh, ns, nv);
        if (ns > 0.002f && nv > 0.002f) h = nh;   // keep the hue while the colour is grey/black
        s = ns; v = nv; repaint();
    }
    juce::Colour getColour() const { return juce::Colour::fromHSV(h, s, v, 1.0f); }
    void paint(juce::Graphics& g) override {
        auto c = centre(); const float mid = outerR() - ringW() * 0.5f;
        for (int i = 0; i < 180; ++i) {
            const float a0 = float(i) / 180.0f * juce::MathConstants<float>::twoPi;
            juce::Path seg; seg.addCentredArc(c.x, c.y, mid, mid, 0.0f, a0, a0 + 0.045f, true);
            g.setColour(juce::Colour::fromHSV(float(i) / 180.0f, 1.0f, 1.0f, 1.0f));
            g.strokePath(seg, juce::PathStrokeType(ringW(), juce::PathStrokeType::curved, juce::PathStrokeType::butt));
        }
        auto sq = square();
        g.setGradientFill(juce::ColourGradient(juce::Colours::white, sq.getX(), 0, juce::Colour::fromHSV(h, 1.0f, 1.0f, 1.0f), sq.getRight(), 0, false));
        g.fillRect(sq);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0x00000000), 0, sq.getY(), juce::Colours::black, 0, sq.getBottom(), false));
        g.fillRect(sq);
        g.setColour(juce::Colour(0xff000000)); g.drawRect(sq, 1.0f);
        auto hp = c + juce::Point<float>(std::sin(h * juce::MathConstants<float>::twoPi), -std::cos(h * juce::MathConstants<float>::twoPi)) * mid;
        marker(g, hp); marker(g, {sq.getX() + s * sq.getWidth(), sq.getY() + (1.0f - v) * sq.getHeight()});
    }
    void mouseDown(const juce::MouseEvent& e) override {
        auto p = e.position; const float d = p.getDistanceFrom(centre());
        mode = d >= outerR() - ringW() - 2.0f && d <= outerR() + 2.0f ? 1 : (square().expanded(2).contains(p) ? 2 : 0);
        update(p);
    }
    void mouseDrag(const juce::MouseEvent& e) override { update(e.position); }
private:
    float h = 0, s = 1, v = 1; int mode = 0;
    juce::Point<float> centre() const { return getLocalBounds().toFloat().getCentre(); }
    float outerR() const { return juce::jmin(getWidth(), getHeight()) * 0.5f - 2.0f; }
    float ringW() const { return outerR() * 0.20f; }
    juce::Rectangle<float> square() const { const float side = (outerR() - ringW()) * 1.30f; return juce::Rectangle<float>(side, side).withCentre(centre()); }
    static void marker(juce::Graphics& g, juce::Point<float> p) {
        g.setColour(juce::Colours::black); g.drawEllipse(p.x - 6, p.y - 6, 12, 12, 3.0f);
        g.setColour(juce::Colours::white); g.drawEllipse(p.x - 6, p.y - 6, 12, 12, 1.5f);
    }
    void update(juce::Point<float> p) {
        if (mode == 1) {
            auto c = centre(); float a = std::atan2(p.x - c.x, -(p.y - c.y)) / juce::MathConstants<float>::twoPi;
            h = a < 0 ? a + 1.0f : a;
        } else if (mode == 2) {
            auto sq = square(); s = juce::jlimit(0.0f, 1.0f, (p.x - sq.getX()) / sq.getWidth());
            v = 1.0f - juce::jlimit(0.0f, 1.0f, (p.y - sq.getY()) / sq.getHeight());
        } else return;
        repaint(); if (onChange) onChange(getColour());
    }
};

// Wheel + preview + hex field, shown in a call-out.
class ColourPickerPanel : public juce::Component {
public:
    ColourPickerPanel(juce::Colour start, std::function<void(juce::Colour)> changed) : onChange(std::move(changed)) {
        addAndMakeVisible(wheel); addAndMakeVisible(hex);
        hex.setFont(juce::FontOptions(14.0f, juce::Font::bold)); hex.setJustification(juce::Justification::centred);
        hex.setInputRestrictions(7, "#0123456789abcdefABCDEF");
        wheel.onChange = [this](juce::Colour c) { setHex(c); onChange(c); };
        auto commit = [this] {
            auto t = hex.getText().trim(); if (t.startsWithChar('#')) t = t.substring(1);
            if (t.length() == 6 && t.containsOnly("0123456789abcdefABCDEF")) {
                auto c = juce::Colour::fromString("ff" + t); wheel.setColour(c); current = c; onChange(c); repaint();
            } else setHex(current);
        };
        hex.onReturnKey = commit; hex.onFocusLost = commit;
        current = start; wheel.setColour(start); setHex(start);
        setSize(210, 262);
    }
    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xff18222c));
        g.setColour(current); g.fillRoundedRectangle(getLocalBounds().removeFromBottom(62).removeFromTop(26).reduced(10, 0).toFloat(), 4);
    }
    void resized() override {
        auto b = getLocalBounds().reduced(8);
        wheel.setBounds(b.removeFromTop(190));
        b.removeFromTop(4); b.removeFromBottom(0);
        auto row = b.removeFromTop(26); (void)row;
        hex.setBounds(getLocalBounds().removeFromBottom(32).reduced(40, 2));
    }
private:
    void setHex(juce::Colour c) { current = c; hex.setText("#" + c.toDisplayString(false), false); repaint(); }
    ColourWheel wheel; juce::TextEditor hex; juce::Colour current; std::function<void(juce::Colour)> onChange;
};

// Clickable colour chip that opens the wheel.
class ColourChip : public juce::Component {
public:
    std::function<void(juce::Colour)> onChange;
    void set(juce::Colour c) { colour = c; repaint(); }
    juce::Colour get() const { return colour; }
    void paint(juce::Graphics& g) override {
        auto b = getLocalBounds().toFloat().reduced(1);
        g.setColour(colour); g.fillRoundedRectangle(b, 4);
        g.setColour(juce::Colours::white.withAlpha(0.7f)); g.drawRoundedRectangle(b, 4, 1.2f);
        g.setColour(colour.getPerceivedBrightness() > 0.55f ? juce::Colours::black : juce::Colours::white);
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText("#" + colour.toDisplayString(false), getLocalBounds(), juce::Justification::centred);
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (!getLocalBounds().contains(e.getPosition())) return;
        auto panel = std::make_unique<ColourPickerPanel>(colour, [safe = juce::Component::SafePointer<ColourChip>(this)](juce::Colour c) {
            if (!safe) return; safe->colour = c; safe->repaint(); if (safe->onChange) safe->onChange(c);
        });
        juce::CallOutBox::launchAsynchronously(std::move(panel), getScreenBounds(), nullptr);
    }
private:
    juce::Colour colour = juce::Colours::grey;
};

// Inspector for the knobs. Every change applies immediately.
class Studio : public juce::Component {
public:
    struct Entry { Item item; juce::Component* component; std::function<void(const Item&)> apply; std::string name; };
    std::function<void(juce::Component*)> onSelect;   // selection changed (for an outline)
    std::function<void()> onChanged;                  // an edit was applied (for persistence)
    // Lets the host change what gets saved (for example, save a knob's base style instead of a section override).
    std::function<Item(const Item&)> saveTransform;
    void setStatus(const juce::String& t) { status.setText(t, juce::dontSendNotification); }

    void add(Item item, juce::Component& component, std::function<void(const Item&)> applyItem, std::string name = {}) {
        if (name.empty()) name = item.id;
        entries.push_back({std::move(item), &component, std::move(applyItem), std::move(name)});
        selector.addItem(entries.back().name, int(entries.size()));
        if (entries.size() == 1) selector.setSelectedId(1, juce::sendNotificationSync);
    }
    void select(juce::Component* c) {
        for (size_t n = 0; n < entries.size(); ++n) if (entries[n].component == c) { selector.setSelectedId(int(n) + 1, juce::sendNotificationSync); return; }
    }
    juce::Component* selected() const { auto n = selector.getSelectedId() - 1; return n >= 0 && n < int(entries.size()) ? entries[size_t(n)].component : nullptr; }
    std::string toCsv() const {
        auto items = snapshot(); if (saveTransform) for (auto& i : items) i = saveTransform(i);
        std::ostringstream out; writeDesign(out, items); return out.str();
    }
    bool fromCsv(const std::string& text) {
        try { std::istringstream in(text); restore(readDesign(in), false); return true; } catch (...) { return false; }
    }

    Studio() {
        for (auto* c : std::vector<juce::Component*>{&selector, &style, &chip, &marksBox, &label, &font, &x, &y, &w, &h, &save, &load, &undo, &status})
            addAndMakeVisible(c);
        for (int n = 0; n < int(std::size(knobStyles)); ++n) style.addItem(juce::String(knobStyles[n].code) + "  -  " + knobStyles[n].description, n + 1);
        for (auto* t : {&label, &font, &x, &y, &w, &h}) {
            t->setSelectAllWhenFocused(true);
            t->onReturnKey = [this] { edit(); }; t->onFocusLost = [this] { edit(); };
        }
        marksBox.addItem("Auto (follows plate)", 1); marksBox.addItem("Light marks (for dark plates)", 2); marksBox.addItem("Dark marks (for light plates)", 3);
        marksBox.setTooltip("Colour of the printed scale marks and dots around the knob. Auto picks dark marks on a light plate and light marks on a dark plate.");
        marksBox.onChange = [this] { edit(); };
        style.onChange = [this] { edit(); };
        chip.onChange = [this](juce::Colour c) { colourHex = "#" + c.toDisplayString(false).toStdString(); edit(); };
        save.setButtonText("Save knobs CSV"); load.setButtonText("Load knobs CSV"); undo.setButtonText("Undo");
        selector.onChange = [this] { show(); if (onSelect) onSelect(selected()); };
        undo.onClick = [this] { if (!history.empty()) { restore(history.back(), true); history.pop_back(); } };
        save.onClick = [this] { choose(true); }; load.onClick = [this] { choose(false); };
        selector.setTooltip("Pick a knob here, or click it on the plugin.");
        style.setTooltip("Knob class. Changes apply immediately.");
    }
    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xff18222c)); g.setColour(juce::Colour(0xffce9435)); g.drawRect(getLocalBounds(), 2);
        g.setColour(juce::Colour(0xff9fb0b8)); g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        for (auto& [t, r] : captions) g.drawText(t, r, juce::Justification::centredLeft);
    }
    void resized() override {
        captions.clear();
        auto area = getLocalBounds().reduced(8);
        auto cap = area.removeFromTop(12); auto top = area.removeFromTop(26); area.removeFromTop(2);
        auto row2cap = area.removeFromTop(12); auto row2 = area.removeFromTop(26);
        auto place = [&](const char* t, juce::Rectangle<int>& capRow, juce::Rectangle<int>& row, juce::Component& c, int wd, int gap = 6) {
            auto rc = row.removeFromLeft(wd); auto cc = capRow.removeFromLeft(wd);
            capRow.removeFromLeft(gap); row.removeFromLeft(gap); c.setBounds(rc); captions.push_back({t, cc});
        };
        place("KNOB (name)", cap, top, selector, 250); place("STYLE", cap, top, style, 330);
        place("COLOUR (click for wheel)", cap, top, chip, 120); place("LABEL", cap, top, label, 120); place("FONT", cap, top, font, 50);
        place("MARKS (on plate)", row2cap, row2, marksBox, 210); place("X", row2cap, row2, x, 56); place("Y", row2cap, row2, y, 56); place("W", row2cap, row2, w, 56); place("H", row2cap, row2, h, 56);
        row2.removeFromLeft(10);
        for (auto* b : {&undo, &save, &load}) b->setBounds(row2.removeFromLeft(b == &undo ? 70 : 130).reduced(2, 0));
        status.setBounds(area.reduced(0, 0));
    }
private:
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> captions;
    std::vector<Entry> entries; std::vector<std::vector<Item>> history; std::string colourHex = "#CE9435";
    juce::ComboBox selector, style, marksBox; ColourChip chip; juce::TextEditor label, font, x, y, w, h;
    juce::TextButton save, load, undo; juce::Label status;
    std::unique_ptr<juce::FileChooser> chooser; bool busy = false;
    std::vector<Item> snapshot() const { std::vector<Item> out; for (auto& e : entries) out.push_back(e.item); return out; }
    void say(const juce::String& t) { status.setText(t, juce::dontSendNotification); }
    void show() {
        auto n = selector.getSelectedId() - 1; if (n < 0 || n >= int(entries.size())) return;
        busy = true; auto& e = entries[size_t(n)]; auto& i = e.item;
        label.setText(i.label, false); colourHex = i.colour; chip.set(colour(i.colour));
        if (auto* st = findStyle(i.style)) style.setSelectedId(int(st - knobStyles) + 1, juce::dontSendNotification);
        marksBox.setSelectedId(i.marks=="light" ? 2 : i.marks=="dark" ? 3 : 1, juce::dontSendNotification);
        font.setText(juce::String(i.fontSize), false); x.setText(juce::String(i.x), false); y.setText(juce::String(i.y), false);
        w.setText(juce::String(i.width), false); h.setText(juce::String(i.height), false);
        busy = false;
        say(juce::String("Editing:  ") + e.name + "   (parameter " + i.parameter + ").  Every change applies immediately.");
    }
    void restore(const std::vector<Item>& items, bool notify) {
        if (items.size() != entries.size()) throw std::runtime_error("Design must contain all registered controls");
        for (auto& e : entries) {
            auto i = std::find_if(items.begin(), items.end(), [&](auto& v) { return v.id == e.item.id; });
            if (i == items.end() || i->parameter != e.item.parameter) throw std::runtime_error("Design parameter bindings do not match this plugin");
            validate(*i);
        }
        for (auto& e : entries) {
            e.item = *std::find_if(items.begin(), items.end(), [&](auto& v) { return v.id == e.item.id; });
            e.component->setBounds(juce::roundToInt(e.item.x), juce::roundToInt(e.item.y), juce::roundToInt(e.item.width), juce::roundToInt(e.item.height));
            e.apply(e.item);
        }
        show(); if (notify && onChanged) onChanged();
    }
    void edit() {
        if (busy) return;
        try {
            auto n = selector.getSelectedId() - 1; if (n < 0) return;
            auto items = snapshot(); auto& i = items.at(size_t(n));
            auto num = [](const juce::TextEditor& t) { auto s = t.getText().toStdString(); size_t end; float v = std::stof(s, &end); if (end != s.size()) throw std::runtime_error("Invalid number"); return v; };
            i.label = label.getText().toStdString(); i.colour = colourHex;
            if (auto sel = style.getSelectedId(); sel > 0) i.style = knobStyles[sel - 1].code;
            i.marks = marksBox.getSelectedId() == 2 ? "light" : marksBox.getSelectedId() == 3 ? "dark" : "auto";
            i.fontSize = num(font); i.x = num(x); i.y = num(y); i.width = num(w); i.height = num(h); validate(i);
            if (i == entries[size_t(n)].item) return;                    // nothing changed
            auto before = snapshot(); restore(items, true); history.push_back(before);
        } catch (const std::exception& e) { say(e.what()); }
    }
    void choose(bool writing) {
        chooser = std::make_unique<juce::FileChooser>(writing ? "Save knob design" : "Load knob design", juce::File{}, "*.csv");
        auto flags = writing ? (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting) : juce::FileBrowserComponent::openMode;
        chooser->launchAsync(flags | juce::FileBrowserComponent::canSelectFiles, [safe = juce::Component::SafePointer<Studio>(this), writing](const juce::FileChooser& c) {
            if (!safe || c.getResult() == juce::File{}) return;
            try {
                if (writing) { if (!c.getResult().replaceWithText(safe->toCsv(), false, false, "\n")) throw std::runtime_error("Save failed"); }
                else { std::ifstream in(c.getResult().getFullPathName().toStdString()); auto items = readDesign(in); auto before = safe->snapshot(); safe->restore(items, true); safe->history.push_back(before); }
                safe->say(writing ? "Knob design saved to the chosen file." : "Knob design loaded");
            } catch (const std::exception& e) { safe->say(e.what()); }
        });
    }
};
} // namespace goodlookinui::juce_adapter
#endif
