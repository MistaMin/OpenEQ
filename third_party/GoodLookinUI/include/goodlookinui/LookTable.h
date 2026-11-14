// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marcos Deida
#pragma once
#include "Design.h"
#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

// The editable colour-trigger table: one Look per (parameter, choice). It starts from the
// defaults the application supplies, is overridden by a baked looks CSV and then by whatever
// the developer edits at run time (saved in the application's state). Empty strings mean "not set".
namespace goodlookinui {
struct Look {
    std::string parameter, choice;
    std::string colour;     // #RRGGBB or empty
    std::string faceplate;  // whole-UI faceplate code or empty
    std::string style;      // knob class code or empty
    std::string plate;      // section plate code or empty
    bool operator==(const Look&) const = default;
};

inline bool validLook(const Look& l) {
    if (l.parameter.empty() || l.choice.empty()) return false;
    if (!l.colour.empty() && (l.colour.size() != 7 || l.colour[0] != '#' ||
        l.colour.find_first_not_of("0123456789abcdefABCDEF", 1) != std::string::npos)) return false;
    if (!l.style.empty() && !findStyle(l.style)) return false;
    if (!l.plate.empty() && !findFaceplate(l.plate)) return false;
    if (!l.faceplate.empty() && !findFaceplate(l.faceplate)) return false;
    return true;
}

class LookTable {
public:
    static constexpr const char* header = "parameter,choice,colour,faceplate,style,plate";
    static constexpr const char* title = "GoodLookinUI looks version 1";

    LookTable() = default;
    explicit LookTable(std::vector<Look> defaultLooks) : defaults(std::move(defaultLooks)) { resetToDefaults(); }

    void resetToDefaults() { rows = defaults; }
    const Look* find(const std::string& parameter, const std::string& choice) const {
        for (const auto& r : rows) if (r.parameter == parameter && r.choice == choice) return &r;
        return nullptr;
    }
    const Look* find(const char* parameter, const char* choice) const { return find(std::string(parameter), std::string(choice)); }
    // Insert or replace. Returns false (and changes nothing) if the look is invalid.
    bool set(const Look& l) {
        if (!validLook(l)) return false;
        for (auto& r : rows) if (r.parameter == l.parameter && r.choice == l.choice) { r = l; return true; }
        rows.push_back(l); return true;
    }
    // Restore one entry to its built-in default (removes it if it has none).
    void resetEntry(const std::string& parameter, const std::string& choice) {
        const Look* d = nullptr;
        for (const auto& r : defaults) if (r.parameter == parameter && r.choice == choice) d = &r;
        if (d) set(*d);
        else rows.erase(std::remove_if(rows.begin(), rows.end(), [&](const Look& r) { return r.parameter == parameter && r.choice == choice; }), rows.end());
    }
    const std::vector<Look>& all() const { return rows; }

    std::string toCsv() const {
        std::ostringstream out;
        out << title << '\n' << header << '\n';
        for (const auto& r : rows)
            out << quote(r.parameter) << ',' << quote(r.choice) << ',' << quote(r.colour) << ','
                << quote(r.faceplate) << ',' << quote(r.style) << ',' << quote(r.plate) << '\n';
        return out.str();
    }
    // Merges the rows of a CSV into the table. Returns false if the text is malformed;
    // in that case the table is left unchanged.
    bool fromCsv(const std::string& text) {
        try {
            std::istringstream in(text); std::string line;
            auto next = [&] { std::getline(in, line); if (!line.empty() && line.back() == '\r') line.pop_back(); };
            next(); if (line != title) return false;
            next(); if (line != header) return false;
            std::vector<Look> parsed;
            while (std::getline(in, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty()) continue;
                auto f = fields(line);
                if (f.size() != 6) return false;
                Look l{f[0], f[1], f[2], f[3], f[4], f[5]};
                if (!validLook(l)) return false;
                parsed.push_back(l);
            }
            for (const auto& l : parsed) set(l);
            return true;
        } catch (...) { return false; }
    }
private:
    std::vector<Look> defaults, rows;
};
} // namespace goodlookinui
