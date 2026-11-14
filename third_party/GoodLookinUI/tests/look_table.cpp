// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marcos Deida
// Dependency-free checks for the editable, persistent look table.
#include <goodlookinui/LookTable.h>
#include <cassert>
#include <iostream>
using namespace goodlookinui;
int main() {
    const std::vector<Look> defaults = {
        {"mid1Type", "N-EQ", "#5793B8", "", "N", "Marine"}, {"mid1Type", "Brit", "#3F6FD0", "", "Brit", "Granite"},
        {"mid2Type", "N-EQ", "#6EA68B", "", "N", "Marine"}, {"preampType", "Brit", "#C96F4A", "Silver", "Brit", "Granite"}};
    LookTable t(defaults);
    assert(t.all() == defaults);
    const Look* a = t.find("mid1Type", "N-EQ"); assert(a && a->style == "N" && a->plate == "Marine");

    // edits are kept per (parameter, choice); siblings and other parameters stay untouched
    Look e = *a; e.style = "Pie"; e.colour = "#12AB34"; e.plate = "Walnut"; assert(t.set(e));
    assert(t.find("mid1Type", "N-EQ")->style == "Pie" && t.find("mid1Type", "N-EQ")->colour == "#12AB34");
    assert(t.find("mid1Type", "Brit")->style == "Brit" && t.find("mid2Type", "N-EQ")->style == "N");

    // invalid looks are refused and change nothing
    Look bad = e; bad.style = "Nope"; assert(!t.set(bad));
    bad = e; bad.colour = "red"; assert(!t.set(bad));
    bad = e; bad.plate = "Nope"; assert(!t.set(bad));
    bad = e; bad.parameter = ""; assert(!t.set(bad));
    assert(t.find("mid1Type", "N-EQ")->style == "Pie");

    Look added{"mid1Type", "Extra", "#FFFFFF", "", "Wd", ""}; assert(t.set(added) && t.find("mid1Type", "Extra"));

    // CSV round trip keeps every edit
    LookTable u(defaults); assert(u.fromCsv(t.toCsv())); assert(u.all() == t.all());
    assert(u.find("mid1Type", "N-EQ")->colour == "#12AB34");

    // malformed text is rejected without touching the table
    LookTable v(defaults); const auto before = v.all();
    assert(!v.fromCsv("junk")); assert(!v.fromCsv("GoodLookinUI looks version 1\nwrong,header\n"));
    assert(!v.fromCsv(std::string("GoodLookinUI looks version 1\n") + LookTable::header + "\n\"a\",\"b\",\"#GGGGGG\",\"\",\"\",\"\"\n"));
    assert(v.all() == before);

    // reset restores a default, and removes an added row
    t.resetEntry("mid1Type", "N-EQ"); assert(t.find("mid1Type", "N-EQ")->style == "N");
    t.resetEntry("mid1Type", "Extra"); assert(!t.find("mid1Type", "Extra"));
    t.set(e); t.resetToDefaults(); assert(t.all() == defaults);

    // a table without defaults starts empty and still works
    LookTable empty; assert(empty.all().empty()); assert(empty.set(added) && empty.all().size() == 1);
    std::cout << "look table: edits per choice, CSV round trip, validation, reset OK\n";
}
