// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marcos Deida
// JUCE-free checks for the audio-to-UI level tap.
#include <goodlookinui/LevelTracker.h>
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>
static bool near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }
int main() {
    goodlookinui::LevelTracker t;
    const int n = 4800;
    std::vector<float> l(n), r(n), z(n, 0.0f);
    for (int i = 0; i < n; ++i) { l[i] = 0.5f * std::sin(2.0f * 3.14159265f * 1000.0f * float(i) / 48000.0f); r[i] = 0.25f * (i % 2 ? 1.0f : -1.0f); }
    const float* ch[2] = {l.data(), r.data()};
    t.push(ch, 2, n);
    auto a = t.read();
    assert(near(a.peak[0], 0.5f, 0.001f) && near(a.peak[1], 0.25f, 1e-6f));            // peaks
    assert(near(a.meanSquare[0], 0.125f, 0.002f) && near(a.meanSquare[1], 0.0625f, 1e-5f));   // RMS 0.3536 / 0.25
    assert(near(20.0f * std::log10(a.peak[0]), -6.02f, 0.02f));
    auto b = t.read();                                                                  // reading resets the accumulators
    assert(b.peak[0] == 0.0f && b.meanSquare[0] == 0.0f);
    // several blocks between two reads: the loudest peak wins, power is averaged
    const float* quiet[2] = {z.data(), z.data()};
    t.push(ch, 2, n); t.push(quiet, 2, n);
    auto c = t.read();
    assert(near(c.peak[0], 0.5f, 0.001f) && near(c.meanSquare[0], 0.0625f, 0.002f));
    // mono and silence
    t.push(ch, 1, n); auto d = t.read(); assert(d.peak[1] == 0.0f && d.peak[0] > 0.4f);
    t.push(quiet, 2, n); auto e = t.read(); assert(e.peak[0] == 0.0f && e.meanSquare[1] == 0.0f);
    t.push(ch, 2, 0); assert(t.read().peak[0] == 0.0f);                                 // zero-length block
    // concurrent push / read never loses the global maximum
    float maxSeen = 0.0f; std::atomic<bool> go{true};
    std::thread audio([&] { for (int k = 0; k < 2000; ++k) t.push(ch, 2, 256); go = false; });
    while (go) maxSeen = std::max(maxSeen, t.read().peak[0]);
    audio.join(); maxSeen = std::max(maxSeen, t.read().peak[0]);
    assert(maxSeen > 0.45f);
    std::cout << "level tracker: peak, RMS, reset, accumulation, mono, silence, concurrent use OK\n";
}
