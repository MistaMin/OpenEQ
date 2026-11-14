// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marcos Deida
// Dependency-free checks for the key/value settings file.
#include <goodlookinui/Settings.h>
#include <cassert>
#include <iostream>
using namespace goodlookinui;
int main() {
    SettingsTable t;
    t.set("meterFace", "neonbars"); t.set("spectrum", "1,4,multi,1,3,24,0.85,1,ff8fe0c0,ffff3b4a,3,1"); t.set("note", "has \"quotes\", commas");
    t.set("", "x"); t.set("bad\nkey", "x"); t.set("k", "bad\nvalue");                       // rejected
    assert(t.all().size() == 3 && !t.has("") && !t.has("k"));
    SettingsTable u; assert(u.fromCsv(t.toCsv())); assert(u == t);
    assert(u.get("note") == "has \"quotes\", commas" && u.get("missing", "dflt") == "dflt");
    // saving an unchanged table is byte-identical, and independent of insertion order
    SettingsTable a, b; a.set("z", "1"); a.set("a", "2"); b.set("a", "2"); b.set("z", "1");
    assert(a.toCsv() == b.toCsv() && a.toCsv() == SettingsTable(a).toCsv());
    assert(a.toCsv().find("\"a\",\"2\"") < a.toCsv().find("\"z\",\"1\""));                 // sorted
    // malformed input is rejected and leaves the table untouched
    SettingsTable v = t; const auto before = v.toCsv();
    for (const char* bad : {"", "junk", "GoodLookinUI settings version 1\nwrong\n", "GoodLookinUI settings version 1\nkey,value\nonlyone\n",
                            "GoodLookinUI settings version 1\nkey,value\n\"a\",\"1\"\n\"a\",\"2\"\n", "GoodLookinUI settings version 1\nkey,value\n\"\",\"1\"\n"})
        assert(!v.fromCsv(bad) && v.toCsv() == before);
    assert(v.fromCsv(std::string("GoodLookinUI settings version 1\nkey,value\r\n\"a\",\"1\"\r\n")) && v.get("a") == "1" && v.all().size() == 1);   // CRLF ok, replaces
    std::cout << "settings: round trip, order-independent output, rejects malformed input OK\n";
}
