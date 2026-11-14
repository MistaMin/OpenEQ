// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marcos Deida
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

// JUCE-free spectrum analysis.
//   SampleRing     audio thread -> UI thread mono capture (wait-free push).
//   RealFft        real-input FFT using one half-size complex FFT (about half the work).
//   Analyser       Hann window, dBFS calibration, tilt, fall-off and peak hold.
//   toColumns      reduces bins to one value per pixel column on a log-frequency axis.
//   ranges/colourAt  colour for each frequency range.
// All allocation happens in configure(); analyse() and decay() allocate nothing.
namespace goodlookinui {
namespace spectrum {

// ---- capture ---------------------------------------------------------------
class SampleRing {
public:
    static constexpr std::size_t capacity = 1u << 15;   // 32768 samples; at least twice the largest FFT

    // Audio thread. Averages the channels to mono and stores them. Wait-free.
    void push(const float* const* channels, int numChannels, int numSamples) noexcept {
        if (numChannels <= 0 || numSamples <= 0) return;
        std::uint64_t w = writePos.load(std::memory_order_relaxed);
        const float inv = 1.0f / float(numChannels);
        for (int i = 0; i < numSamples; ++i) {
            float s = 0.0f;
            for (int c = 0; c < numChannels; ++c) s += channels[c][i];
            data[std::size_t((w + std::uint64_t(i)) & (capacity - 1))].store(s * inv, std::memory_order_relaxed);
        }
        writePos.store(w + std::uint64_t(numSamples), std::memory_order_release);
    }
    std::uint64_t position() const noexcept { return writePos.load(std::memory_order_acquire); }

    // UI thread. Copies the latest `count` samples (oldest first). Returns false if fewer
    // than `count` samples exist yet, or the writer lapped the copy (practically never).
    bool copyLatest(float* dst, std::size_t count) const noexcept {
        if (count == 0 || count > capacity / 2) return false;
        const std::uint64_t end = writePos.load(std::memory_order_acquire);
        if (end < count) return false;
        const std::uint64_t start = end - count;
        for (std::size_t i = 0; i < count; ++i) dst[i] = data[std::size_t((start + i) & (capacity - 1))].load(std::memory_order_relaxed);
        return writePos.load(std::memory_order_acquire) - start <= capacity - 64;
    }

private:
    std::array<std::atomic<float>, capacity> data{};
    std::atomic<std::uint64_t> writePos{0};
};

// ---- FFT --------------------------------------------------------------------
class RealFft {
public:
    RealFft() = default;
    explicit RealFft(int n) { configure(n); }

    void configure(int n) {
        size = n; half = n / 2;
        rev.assign(std::size_t(half), 0);
        int bits = 0; while ((1 << bits) < half) ++bits;
        for (int i = 0; i < half; ++i) { int r = 0; for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b); rev[std::size_t(i)] = r; }
        cosT.resize(std::size_t(half / 2 > 0 ? half / 2 : 1)); sinT.resize(cosT.size());
        for (int k = 0; k < half / 2; ++k) { const double a = -2.0 * M_PI * double(k) / double(half); cosT[std::size_t(k)] = float(std::cos(a)); sinT[std::size_t(k)] = float(std::sin(a)); }
        wr.resize(std::size_t(half + 1)); wi.resize(wr.size());
        for (int k = 0; k <= half; ++k) { const double a = -2.0 * M_PI * double(k) / double(n); wr[std::size_t(k)] = float(std::cos(a)); wi[std::size_t(k)] = float(std::sin(a)); }
        zr.assign(std::size_t(half), 0.0f); zi.assign(std::size_t(half), 0.0f);
    }
    int getSize() const { return size; }

    // in: n real samples. re/im: n/2+1 values each (DC .. Nyquist). Unnormalised.
    void forward(const float* in, float* re, float* im) {
        for (int i = 0; i < half; ++i) { const int r = rev[std::size_t(i)]; zr[std::size_t(r)] = in[2 * i]; zi[std::size_t(r)] = in[2 * i + 1]; }
        for (int len = 2; len <= half; len <<= 1) {
            const int h = len >> 1, step = half / len;
            for (int i = 0; i < half; i += len)
                for (int j = 0; j < h; ++j) {
                    const float c = cosT[std::size_t(j * step)], s = sinT[std::size_t(j * step)];
                    float* ar = &zr[std::size_t(i + j)]; float* ai = &zi[std::size_t(i + j)];
                    float* br = &zr[std::size_t(i + j + h)]; float* bi = &zi[std::size_t(i + j + h)];
                    const float tr = *br * c - *bi * s, ti = *br * s + *bi * c;
                    *br = *ar - tr; *bi = *ai - ti; *ar += tr; *ai += ti;
                }
        }
        for (int k = 0; k <= half; ++k) {
            const int a = k % half, b = (half - k) % half;
            const float zkr = zr[std::size_t(a)], zki = zi[std::size_t(a)], zcr = zr[std::size_t(b)], zci = -zi[std::size_t(b)];
            const float er = 0.5f * (zkr + zcr), ei = 0.5f * (zki + zci);                 // even part
            const float dr = 0.5f * (zkr - zcr), di = 0.5f * (zki - zci);
            const float orr = di, oi = -dr;                                               // odd part = -i * d
            const float c = wr[std::size_t(k)], s = wi[std::size_t(k)];
            re[k] = er + orr * c - oi * s;
            im[k] = ei + orr * s + oi * c;
        }
    }

private:
    int size = 0, half = 0;
    std::vector<int> rev;
    std::vector<float> cosT, sinT, wr, wi, zr, zi;
};

inline constexpr int fftSizes[] = {512, 1024, 2048, 4096, 8192, 16384};
inline constexpr float floorDb = -120.0f;

// ---- analyser ---------------------------------------------------------------
class Analyser {
public:
    void configure(int fftSize, double sampleRate) {
        n = fftSize; sr = sampleRate; fft.configure(n);
        window.resize(std::size_t(n)); double sum = 0.0;
        for (int i = 0; i < n; ++i) { const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * double(i) / double(n)); window[std::size_t(i)] = float(w); sum += w; }   // periodic Hann
        norm = float(2.0 / sum);                                        // amplitude-A sine on a bin centre reads exactly A
        const std::size_t bins = std::size_t(n / 2 + 1);
        frame.assign(std::size_t(n), 0.0f); re.assign(bins, 0.0f); im.assign(bins, 0.0f);
        display.assign(bins, floorDb); hold.assign(bins, floorDb); tiltDb.assign(bins, 0.0f);
        lastPos = 0; haveFrame = false; setTilt(tilt);
    }
    void setSampleRate(double s) { if (s != sr && n > 0) configure(n, s); }
    // dB per octave around 1 kHz (4.5 makes pink noise look flat; 0 is a true response).
    void setTilt(float dbPerOctave) {
        tilt = dbPerOctave;
        for (std::size_t k = 0; k < tiltDb.size(); ++k) tiltDb[k] = k == 0 ? 0.0f : tilt * std::log2(float(double(k) * sr / double(n)) / 1000.0f);
    }
    void setFall(float dbPerSecond) { fall = dbPerSecond; }
    int fftSize() const { return n; }
    int numBins() const { return n / 2 + 1; }
    double sampleRate() const { return sr; }
    const std::vector<float>& displayDb() const { return display; }
    const std::vector<float>& holdDb() const { return hold; }
    std::uint64_t framesAnalysed() const { return frames; }

    // Pulls the latest block from the ring and analyses it. Returns true if it did the FFT
    // (skipped when fewer than minNew new samples arrived, so silence costs nothing).
    bool analyse(const SampleRing& ring, std::uint64_t minNew = 128) {
        const std::uint64_t pos = ring.position();
        if (haveFrame && pos - lastPos < minNew) return false;
        if (!ring.copyLatest(frame.data(), std::size_t(n))) return false;
        lastPos = pos; haveFrame = true;
        process(frame.data());
        return true;
    }
    // Direct analysis of n samples (used by tests and offline rendering).
    void process(const float* samples) {
        for (int i = 0; i < n; ++i) frame[std::size_t(i)] = samples[i] * window[std::size_t(i)];
        fft.forward(frame.data(), re.data(), im.data());
        for (std::size_t k = 0; k < re.size(); ++k) {
            const float mag = std::sqrt(re[k] * re[k] + im[k] * im[k]) * norm * (k == 0 || int(k) == n / 2 ? 0.5f : 1.0f);
            const float db = mag > 1.0e-7f ? 20.0f * std::log10(mag) : floorDb;
            const float v = std::max(floorDb, db + tiltDb[k]);
            display[k] = std::max(v, display[k]);        // instant attack; decay() lowers it
            hold[k] = std::max(v, hold[k]);
        }
        ++frames;
    }
    // Lowers the display by the fall rate (call once per UI tick with the elapsed time).
    void decay(float seconds) {
        const float drop = fall * seconds, holdDrop = 6.0f * seconds;
        for (std::size_t k = 0; k < display.size(); ++k) {
            display[k] = std::max(floorDb, display[k] - drop);
            hold[k] = std::max(display[k], hold[k] - holdDrop);
        }
    }
    void reset() { std::fill(display.begin(), display.end(), floorDb); std::fill(hold.begin(), hold.end(), floorDb); haveFrame = false; }

private:
    int n = 0; double sr = 48000.0; float norm = 1.0f, tilt = 0.0f, fall = 24.0f;
    RealFft fft; std::vector<float> window, frame, re, im, display, hold, tiltDb;
    std::uint64_t lastPos = 0, frames = 0; bool haveFrame = false;
};

// ---- bins -> columns ---------------------------------------------------------
// One value per pixel column on a log axis fMin..fMax. Columns spanning several bins take the
// maximum (so no peak is lost); sparse low-frequency columns interpolate between bins.
inline void toColumns(const float* db, int numBins, double sampleRate, int cols, float fMin, float fMax, float* out) {
    const double binHz = sampleRate / double(2 * (numBins - 1)), nyquist = sampleRate * 0.5;
    const double ratio = std::pow(double(fMax) / double(fMin), 1.0 / double(cols));
    double f0 = fMin;
    for (int c = 0; c < cols; ++c) {
        const double f1 = f0 * ratio;
        if (f0 >= nyquist) { out[c] = floorDb; f0 = f1; continue; }
        const double b0 = f0 / binHz, b1 = std::min(f1, nyquist) / binHz;
        const int i0 = int(std::ceil(b0)), i1 = int(std::floor(b1));
        if (i1 >= i0) {
            float m = floorDb; for (int i = std::max(i0, 1); i <= std::min(i1, numBins - 1); ++i) m = std::max(m, db[i]);
            out[c] = m;
        } else {
            const double b = 0.5 * (b0 + b1); const int lo = std::max(0, int(std::floor(b))), hi = std::min(numBins - 1, lo + 1);
            const float t = float(b - double(lo)); out[c] = db[lo] + (db[hi] - db[lo]) * t;
        }
        f0 = f1;
    }
}

// Fractional-octave smoothing on display columns. Columns are log-spaced, so a fixed-width kernel here is exactly
// symmetric in log frequency (a lone tone stays centred; smoothing in the bin domain would drag it toward the
// window's lower edge). Triangular kernel from two box passes of power, O(cols). denominator: 1, 3 (1/3 octave), 6, 12, 24.
// `scratch` must hold `cols` floats and may not alias in/out.
inline void smoothColumns(const float* in, float* out, int cols, float octavesSpanned, int denominator, float* scratch, double* prefix /* cols + 1 */) {
    const float perOctave = float(cols) / octavesSpanned;
    const int w = std::max(1, int(std::lround(perOctave / float(denominator) * 0.5f)));      // each box pass covers half the total width
    if (w <= 1) { std::copy(in, in + cols, out); return; }
    auto boxPass = [&](const float* src, float* dst) {
        prefix[0] = 0.0; for (int i = 0; i < cols; ++i) prefix[i + 1] = prefix[i] + std::pow(10.0, double(src[i]) / 10.0);
        const int r = w / 2;
        for (int i = 0; i < cols; ++i) {
            const int lo = std::max(0, i - r), hi = std::min(cols - 1, i + r);
            const double mean = (prefix[hi + 1] - prefix[lo]) / double(hi - lo + 1);
            dst[i] = mean > 1.0e-12 ? float(10.0 * std::log10(mean)) : floorDb;
        }
    };
    boxPass(in, scratch); boxPass(scratch, out);
}

// ---- spectrogram history -----------------------------------------------------
// Fixed-size rolling history of rows (one row = one analysis frame reduced to `cols` columns), stored as
// 8-bit levels (0 = floor, 255 = ceiling). Newest row is row(0); writes never allocate.
class SpectrogramHistory {
public:
    void configure(int columns, int rows) { cols = columns; nrows = rows; data.assign(std::size_t(columns) * std::size_t(rows), 0); head = 0; count = 0; }
    int columns() const { return cols; }
    int rows() const { return nrows; }
    int filled() const { return count; }
    void push(const float* db, float floorLevelDb, float ceilLevelDb) {
        head = (head + nrows - 1) % nrows;                             // new row goes where the oldest was
        auto* r = &data[std::size_t(head) * std::size_t(cols)];
        const float scale = 255.0f / (ceilLevelDb - floorLevelDb);
        for (int c = 0; c < cols; ++c) r[c] = std::uint8_t(std::lround(std::clamp((db[c] - floorLevelDb) * scale, 0.0f, 255.0f)));
        count = std::min(count + 1, nrows);
    }
    // age 0 = newest
    const std::uint8_t* row(int age) const { return &data[std::size_t((head + age) % nrows) * std::size_t(cols)]; }
private:
    int cols = 0, nrows = 0, head = 0, count = 0; std::vector<std::uint8_t> data;
};

// Colour maps for 0..255 levels. Heat runs black > blue > magenta > orange > yellow > white.
inline std::uint32_t heatColour(std::uint8_t v) {
    struct Stop { float p; int r, g, b; };
    static constexpr Stop stops[] = {{0.0f, 0, 0, 0}, {0.18f, 10, 20, 90}, {0.40f, 140, 20, 150}, {0.62f, 245, 90, 30}, {0.82f, 255, 215, 60}, {1.0f, 255, 255, 235}};
    const float t = float(v) / 255.0f; int i = 0; while (i < 4 && t > stops[i + 1].p) ++i;
    const float u = (t - stops[i].p) / (stops[i + 1].p - stops[i].p);
    auto mix = [&](int a, int b) { return std::uint32_t(std::lround(float(a) + (float(b) - float(a)) * std::clamp(u, 0.0f, 1.0f))); };
    return 0xff000000u | (mix(stops[i].r, stops[i + 1].r) << 16) | (mix(stops[i].g, stops[i + 1].g) << 8) | mix(stops[i].b, stops[i + 1].b);
}
// Shades a base colour by level: black at 0, the colour at about 70%, brightening to white-ish at 255.
inline std::uint32_t shadeColour(std::uint32_t base, std::uint8_t v) {
    const float t = float(v) / 255.0f; auto ch = [&](int sh) { const float c = float((base >> sh) & 255);
        const float lit = t < 0.7f ? c * (t / 0.7f) : c + (255.0f - c) * ((t - 0.7f) / 0.3f) * 0.6f; return std::uint32_t(std::lround(std::clamp(lit, 0.0f, 255.0f))); };
    return 0xff000000u | (ch(16) << 16) | (ch(8) << 8) | ch(0);
}

// ---- frequency ranges and colours ---------------------------------------------
struct Range { const char* name; float lo, hi; };
inline constexpr Range ranges[7] = {{"SUB", 20, 60}, {"BASS", 60, 250}, {"LOW MID", 250, 500}, {"MID", 500, 2000},
                                    {"HIGH MID", 2000, 4000}, {"PRESENCE", 4000, 8000}, {"AIR", 8000, 30000}};
inline constexpr std::uint32_t defaultColours[7] = {0xffff3b4a, 0xffff9a1f, 0xffffe23a, 0xff5be85a, 0xff35e0d0, 0xff3f8ff0, 0xffc05cff};

// Blends between range colours on a log axis (each range's colour is reached at its geometric centre).
inline std::uint32_t colourAt(float freq, const std::uint32_t* cols) {
    float centres[7];
    for (int i = 0; i < 7; ++i) centres[i] = std::sqrt(ranges[i].lo * ranges[i].hi);
    if (freq <= centres[0]) return cols[0];
    if (freq >= centres[6]) return cols[6];
    int i = 0; while (freq > centres[i + 1]) ++i;
    const float t = (std::log(freq) - std::log(centres[i])) / (std::log(centres[i + 1]) - std::log(centres[i]));
    auto mix = [t](std::uint32_t a, std::uint32_t b, int sh) { return std::uint32_t(std::lround(float((a >> sh) & 255) * (1 - t) + float((b >> sh) & 255) * t)) & 255u; };
    return 0xff000000u | (mix(cols[i], cols[i + 1], 16) << 16) | (mix(cols[i], cols[i + 1], 8) << 8) | mix(cols[i], cols[i + 1], 0);
}
inline int rangeIndexFor(float freq) { for (int i = 0; i < 7; ++i) if (freq < ranges[i].hi) return i; return 6; }

} // namespace spectrum
} // namespace goodlookinui
