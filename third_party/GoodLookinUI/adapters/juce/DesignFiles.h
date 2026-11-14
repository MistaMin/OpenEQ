// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marcos Deida
#pragma once
#include "GoodLookinUI.h"
#if GOODLOOKINUI_ENABLE_EDITOR
#include <functional>
#include <map>

// Developer mode only. Writes every design change straight into a folder of the project (for example its
// Designs/ folder) so the developer can see the change in `git diff` and commit it, with no Save button.
//
//  - Debounced: a burst of edits (dragging a colour wheel) is written once, shortly after the last one.
//  - Atomic: written to a temporary file in the same folder, then moved over the target, so a crash cannot
//    leave a half-written file.
//  - Quiet: if the text is identical to what is already on disk nothing is written, so unchanged files never
//    show up as modified.
//  - Flushed when the object is destroyed, so closing the plugin right after an edit loses nothing.
namespace goodlookinui::juce_adapter {
class DesignAutosave : private juce::Timer {
public:
    struct Event { juce::String name; juce::File file; bool wrote; bool ok; juce::Time time; juce::String error; };
    std::function<void(const Event&)> onSaved;          // called after each file is handled (for a status line)

    DesignAutosave() = default;
    explicit DesignAutosave(juce::File folder, int delayMs = 350) { setFolder(std::move(folder), delayMs); }
    ~DesignAutosave() override { flush(); }

    void setFolder(juce::File folder, int delayMs = 350) { dir = std::move(folder); delay = delayMs; }
    bool hasFolder() const { return dir != juce::File{}; }
    juce::File getFolder() const { return dir; }
    juce::File fileFor(const juce::String& name) const { return dir.getChildFile(name); }

    // Queue `text` to be saved as `name` inside the folder. Later requests for the same name replace earlier ones.
    void request(const juce::String& name, const std::string& text) {
        if (!hasFolder()) return;
        pending[name] = text; startTimer(delay);
    }
    // Write everything queued right now.
    void flush() {
        stopTimer();
        auto work = std::move(pending); pending.clear();
        for (auto& [name, text] : work) writeNow(name, text);
    }
    // Reads a file from the folder (empty if it does not exist). Lets a developer build start from the saved design.
    std::string read(const juce::String& name) const {
        auto f = fileFor(name); return hasFolder() && f.existsAsFile() ? f.loadFileAsString().toStdString() : std::string();
    }
    // Immediate, synchronous write used by flush() and by tests. Returns true when the file now holds `text`.
    bool writeNow(const juce::String& name, const std::string& rawText) {
        Event e{name, fileFor(name), false, true, juce::Time::getCurrentTime(), {}};
        // Keep whatever line endings the file already has (older tools wrote CRLF), so the first autosave does not
        // turn every line into a diff. New files use LF.
        const bool crlf = e.file.existsAsFile() && e.file.loadFileAsString().contains("\r\n");
        std::string text = rawText;
        if (crlf) { text.clear(); for (char c : rawText) { if (c == '\n') text += '\r'; text += c; } }
        if (!dir.createDirectory().wasOk()) { e.ok = false; e.error = "cannot create " + dir.getFullPathName(); }
        else if (e.file.existsAsFile() && e.file.loadFileAsString().toStdString() == text) { /* unchanged: leave the file alone */ }
        else {
            juce::TemporaryFile temp(e.file);                                   // same folder, so the move is atomic
            if (!temp.getFile().replaceWithText(juce::String(text), false, false, crlf ? "\r\n" : "\n") || !temp.overwriteTargetFileWithTemporary()) { e.ok = false; e.error = "cannot write " + e.file.getFullPathName(); }
            else e.wrote = true;
        }
        if (onSaved) onSaved(e);
        return e.ok;
    }
private:
    void timerCallback() override { flush(); }
    juce::File dir; int delay = 350; std::map<juce::String, std::string> pending;
};
} // namespace goodlookinui::juce_adapter
#endif
