// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marcos Deida
#pragma once
#include <goodlookinui/LookTable.h>
#include "GoodLookinUI.h"
#if GOODLOOKINUI_ENABLE_EDITOR
#include "Studio.h"
#include <fstream>

namespace goodlookinui::juce_adapter {

// Developer-only editor for the section looks: choose a parameter and one of its
// choices, then set the knob style, colour, plate and whole-UI faceplate that
// choice produces. Every change applies immediately and is kept per choice.
class LookStudio : public juce::Component {
public:
    struct Section { std::string param, name; };
    std::function<void()> onChanged;                         // table edited
    std::function<juce::String()> note;                      // extra status text (trigger mode)

    LookStudio(LookTable& t, std::vector<Section> sectionList, std::function<juce::StringArray(const std::string&)> choicesOf)
        : table(t), sections(std::move(sectionList)), choices(std::move(choicesOf))
    {
        for (auto* c : std::vector<juce::Component*>{&sectionBox, &choiceBox, &styleBox, &useColour, &chip, &plateBox, &faceBox,
                                                     &resetEntry, &resetAll, &save, &load, &status})
            addAndMakeVisible(c);
        for (size_t n = 0; n < sections.size(); ++n) sectionBox.addItem(sections[n].name, int(n) + 1);
        styleBox.addItem("(keep saved knob style)", 1);
        for (int n = 0; n < int(std::size(knobStyles)); ++n)
            styleBox.addItem(juce::String(knobStyles[n].code) + "  -  " + knobStyles[n].description, n + 2);
        for (auto* box : {&plateBox, &faceBox}) {
            box->addItem("(none)", 1);
            int n = 2;
            for (const auto& f : faceplates) box->addItem(juce::String(f.code) + "  -  " + f.name, n++);
        }
        useColour.setButtonText("Colour");
        resetEntry.setButtonText("Reset this choice"); resetAll.setButtonText("Reset all looks");
        save.setButtonText("Save looks CSV"); load.setButtonText("Load looks CSV");
        sectionBox.onChange = [this] { fillChoices(); };
        choiceBox.onChange = [this] { loadFields(); };
        styleBox.onChange = [this] { commit(); }; plateBox.onChange = [this] { commit(); }; faceBox.onChange = [this] { commit(); };
        useColour.onClick = [this] { commit(); };
        chip.onChange = [this](juce::Colour) { useColour.setToggleState(true, juce::dontSendNotification); commit(); };
        resetEntry.onClick = [this] { table.resetEntry(param(), choice()); loadFields(); notify(); };
        resetAll.onClick = [this] { table.resetToDefaults(); loadFields(); notify(); };
        save.onClick = [this] { choose(true); }; load.onClick = [this] { choose(false); };
        sectionBox.setTooltip("Which parameter drives this look");
        choiceBox.setTooltip("Which value of that parameter you are editing");
        if (!sections.empty()) sectionBox.setSelectedId(1, juce::sendNotificationSync);
    }

    // The plugin's parameter changed: show that choice so it can be edited straight away.
    void follow(const std::string& p, const juce::String& c) {
        for (size_t n = 0; n < sections.size(); ++n)
            if (sections[n].param == p) {
                if (sectionBox.getSelectedId() != int(n) + 1) sectionBox.setSelectedId(int(n) + 1, juce::sendNotificationSync);
                for (int i = 0; i < choiceBox.getNumItems(); ++i)
                    if (choiceBox.getItemText(i) == c) { choiceBox.setSelectedItemIndex(i, juce::sendNotificationSync); break; }
                return;
            }
    }
    void refresh() { loadFields(); }
    void setStatus(const juce::String& t) { status.setText(t, juce::dontSendNotification); }

    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xff18222c)); g.setColour(juce::Colour(0xff39b6c8)); g.drawRect(getLocalBounds(), 2);
        g.setColour(juce::Colour(0xff9fb0b8)); g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        for (auto& [t, r] : captions) g.drawText(t, r, juce::Justification::centredLeft);
    }
    void resized() override {
        captions.clear();
        auto area = getLocalBounds().reduced(8);
        auto cap = area.removeFromTop(12); auto row = area.removeFromTop(26); area.removeFromTop(4);
        auto place = [&](const char* t, juce::Component& c, int wd) {
            auto cc = cap.removeFromLeft(wd); auto rc = row.removeFromLeft(wd); cap.removeFromLeft(6); row.removeFromLeft(6);
            c.setBounds(rc); if (*t) captions.push_back({t, cc});
        };
        place("SECTION LOOK  -  parameter", sectionBox, 170); place("CHOICE", choiceBox, 100); place("KNOB STYLE", styleBox, 220);
        place("", useColour, 66); place("COLOUR (click for wheel)", chip, 100);
        place("SECTION PLATE", plateBox, 150); place("WHOLE-UI FACEPLATE", faceBox, 150);
        auto buttons = area.removeFromTop(26);
        for (auto* b : {&resetEntry, &resetAll, &save, &load}) b->setBounds(buttons.removeFromLeft(130).reduced(2, 0));
        status.setBounds(buttons.withTrimmedLeft(4));
    }
private:
    LookTable& table;
    std::vector<Section> sections;
    std::function<juce::StringArray(const std::string&)> choices;
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> captions;
    juce::ComboBox sectionBox, choiceBox, styleBox, plateBox, faceBox;
    juce::ToggleButton useColour;
    ColourChip chip;
    juce::TextButton resetEntry, resetAll, save, load;
    juce::Label status;
    std::unique_ptr<juce::FileChooser> chooser;
    bool busy = false;

    std::string param() const { auto n = sectionBox.getSelectedId() - 1; return n >= 0 && n < int(sections.size()) ? sections[size_t(n)].param : ""; }
    std::string choice() const { return choiceBox.getText().toStdString(); }
    void say(const juce::String& t) { status.setText(t, juce::dontSendNotification); }
    void notify() { if (onChanged) onChanged(); }
    void fillChoices() {
        busy = true; choiceBox.clear(juce::dontSendNotification);
        auto list = choices(param());
        for (int i = 0; i < list.size(); ++i) choiceBox.addItem(list[i], i + 1);
        busy = false;
        if (list.size() > 0) choiceBox.setSelectedId(1, juce::sendNotificationSync);
    }
    static int indexIn(const std::string& code, bool style) {
        if (code.empty()) return 1;
        if (style) { if (auto* s = goodlookinui::findStyle(code)) return int(s - knobStyles) + 2; }
        else if (auto* f = goodlookinui::findFaceplate(code)) return int(f - faceplates) + 2;
        return 1;
    }
    void loadFields() {
        busy = true;
        const Look* l = table.find(param(), choice());
        Look blank; if (!l) l = &blank;
        styleBox.setSelectedId(indexIn(l->style, true), juce::dontSendNotification);
        plateBox.setSelectedId(indexIn(l->plate, false), juce::dontSendNotification);
        faceBox.setSelectedId(indexIn(l->faceplate, false), juce::dontSendNotification);
        useColour.setToggleState(!l->colour.empty(), juce::dontSendNotification);
        chip.set(l->colour.empty() ? juce::Colour(0xff697170) : colour(l->colour));
        busy = false;
        say(juce::String("Editing ") + sectionBox.getText() + " = " + choiceBox.getText() + ". Changes apply immediately; they are kept when you switch the type."
            + (note ? "   " + note() : juce::String()));
    }
    void commit() {
        if (busy || param().empty() || choice().empty()) return;
        Look l; l.parameter = param(); l.choice = choice();
        if (useColour.getToggleState()) l.colour = "#" + chip.get().toDisplayString(false).toStdString();
        if (auto id = styleBox.getSelectedId(); id > 1) l.style = knobStyles[id - 2].code;
        if (auto id = plateBox.getSelectedId(); id > 1) l.plate = faceplates[id - 2].code;
        if (auto id = faceBox.getSelectedId(); id > 1) l.faceplate = faceplates[id - 2].code;
        if (!table.set(l)) { say("Invalid look"); return; }
        notify(); say(juce::String("Applied: ") + sectionBox.getText() + " = " + choiceBox.getText() + (note ? "   " + note() : juce::String()));
    }
    void choose(bool writing) {
        chooser = std::make_unique<juce::FileChooser>(writing ? "Save section looks" : "Load section looks", juce::File{}, "*.csv");
        auto flags = writing ? (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting) : juce::FileBrowserComponent::openMode;
        chooser->launchAsync(flags | juce::FileBrowserComponent::canSelectFiles, [safe = juce::Component::SafePointer<LookStudio>(this), writing](const juce::FileChooser& c) {
            if (!safe || c.getResult() == juce::File{}) return;
            if (writing) {
                safe->say(c.getResult().replaceWithText(safe->table.toCsv(), false, false, "\n") ? "Looks saved to the chosen file." : "Save failed");
            } else {
                std::ifstream in(c.getResult().getFullPathName().toStdString());
                std::stringstream ss; ss << in.rdbuf();
                if (safe->table.fromCsv(ss.str())) { safe->loadFields(); safe->notify(); safe->say("Looks loaded"); }
                else safe->say("Could not read that looks file");
            }
        });
    }
};

} // namespace goodlookinui::juce_adapter
#endif
