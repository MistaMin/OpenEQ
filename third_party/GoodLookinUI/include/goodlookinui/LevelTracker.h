// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marcos Deida
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

// Lock-free level tap between the audio thread and the UI. The audio thread calls
// push() for each block; the UI calls read() a few dozen times a second and gets the
// peak and mean-square power accumulated since its last read (no block is missed).
namespace goodlookinui {
class LevelTracker {
public:
    struct Reading {
        float peak[2] = {0, 0};
        float meanSquare[2] = {0, 0};
    };

    void push(const float* const* channels, int numChannels, int numSamples) noexcept {
        for (int c = 0; c < std::min(numChannels, 2); ++c) {
            float pk = 0.0f; double ss = 0.0;
            for (int i = 0; i < numSamples; ++i) {
                const float v = channels[c][i];
                pk = std::max(pk, std::fabs(v));
                ss += double(v) * double(v);
            }
            float cur = peak[c].load(std::memory_order_relaxed);
            while (pk > cur && !peak[c].compare_exchange_weak(cur, pk, std::memory_order_relaxed)) {}
            double s = sumSq[c].load(std::memory_order_relaxed);
            while (!sumSq[c].compare_exchange_weak(s, s + ss, std::memory_order_relaxed)) {}
            count[c].fetch_add(std::uint64_t(std::max(numSamples, 0)), std::memory_order_relaxed);
        }
    }

    Reading read() noexcept {
        Reading r;
        for (int c = 0; c < 2; ++c) {
            r.peak[c] = peak[c].exchange(0.0f, std::memory_order_relaxed);
            const double s = sumSq[c].exchange(0.0, std::memory_order_relaxed);
            const std::uint64_t n = count[c].exchange(0, std::memory_order_relaxed);
            r.meanSquare[c] = n ? float(s / double(n)) : 0.0f;
        }
        return r;
    }

private:
    std::atomic<float> peak[2] = {{0.0f}, {0.0f}};
    std::atomic<double> sumSq[2] = {{0.0}, {0.0}};
    std::atomic<std::uint64_t> count[2] = {{0}, {0}};
};
} // namespace goodlookinui
