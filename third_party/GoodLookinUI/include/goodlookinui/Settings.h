// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marcos Deida
#pragma once
#include "Design.h"
#include <map>
#include <sstream>
#include <string>

// A flat key/value table for a project's design choices that are not per-knob (colour trigger mode, meter face,
// spectrum settings, ...). It is saved as a small CSV that is easy to read in a diff and to commit, and the same
// text can be baked into a build. Keys are written in sorted order and nothing else (no timestamps), so saving an
// unchanged table gives byte-identical text.
namespace goodlookinui {
class SettingsTable {
public:
    static constexpr const char* title = "GoodLookinUI settings version 1";
    static constexpr const char* header = "key,value";

    void set(const std::string& key, const std::string& value) { if (valid(key, value)) values[key] = value; }
    bool has(const std::string& key) const { return values.count(key) != 0; }
    std::string get(const std::string& key, const std::string& fallback = {}) const { auto i = values.find(key); return i == values.end() ? fallback : i->second; }
    void erase(const std::string& key) { values.erase(key); }
    const std::map<std::string, std::string>& all() const { return values; }
    bool operator==(const SettingsTable& o) const { return values == o.values; }

    std::string toCsv() const {
        std::ostringstream out; out << title << '\n' << header << '\n';
        for (const auto& [k, v] : values) out << quote(k) << ',' << quote(v) << '\n';
        return out.str();
    }
    // Replaces the table with the file's rows. Returns false (and leaves the table alone) if the text is malformed.
    bool fromCsv(const std::string& text) {
        try {
            std::istringstream in(text); std::string line;
            auto next = [&] { std::getline(in, line); if (!line.empty() && line.back() == '\r') line.pop_back(); };
            next(); if (line != title) return false;
            next(); if (line != header) return false;
            std::map<std::string, std::string> parsed;
            while (std::getline(in, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty()) continue;
                auto f = fields(line);
                if (f.size() != 2 || !valid(f[0], f[1]) || !parsed.emplace(f[0], f[1]).second) return false;
            }
            values = std::move(parsed); return true;
        } catch (...) { return false; }
    }
private:
    static bool valid(const std::string& k, const std::string& v) {
        return !k.empty() && k.find_first_of("\r\n") == std::string::npos && v.find_first_of("\r\n") == std::string::npos;
    }
    std::map<std::string, std::string> values;
};
} // namespace goodlookinui
