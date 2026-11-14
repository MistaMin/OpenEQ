// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marcos Deida
// JUCE-free checks for the spectrum analysis core: accuracy, speed, capture and mapping.
#include <goodlookinui/Spectrum.h>
#include <cassert>
#include <chrono>
#include <complex>
#include <iostream>
#include <random>
#include <thread>
using namespace goodlookinui::spectrum;
static bool near(double a, double b, double t) { return std::fabs(a - b) <= t; }
int main() {
    // 1) FFT equals a direct DFT (random input), for several sizes
    for (int n : {16, 64, 512, 4096}) {
        std::mt19937 rng(7); std::uniform_real_distribution<float> d(-1, 1);
        std::vector<float> x(std::size_t(n), 0.0f), re(std::size_t(n / 2 + 1), 0.0f), im(std::size_t(n / 2 + 1), 0.0f); for (auto& v : x) v = d(rng);
        RealFft f(n); f.forward(x.data(), re.data(), im.data());
        double worst = 0;
        for (int k = 0; k <= n / 2; k += (n > 512 ? 17 : 1)) {
            std::complex<double> s = 0; for (int t = 0; t < n; ++t) s += double(x[size_t(t)]) * std::polar(1.0, -2.0 * M_PI * k * t / n);
            worst = std::max(worst, std::abs(s - std::complex<double>(re[size_t(k)], im[size_t(k)])));
        }
        assert(worst < 2e-3 * std::sqrt(double(n)));
    }
    // 2) calibration: a sine of amplitude A on a bin centre reads 20log10(A) dBFS at every size
    for (int n : fftSizes) {
        Analyser a; a.configure(n, 48000.0);
        const int bin = n / 8; std::vector<float> s(std::size_t(n), 0.0f);
        for (double A : {1.0, 0.5, 0.1}) {
            a.reset(); for (int i = 0; i < n; ++i) s[size_t(i)] = float(A * std::sin(2.0 * M_PI * bin * i / n));
            a.process(s.data());
            const auto& db = a.displayDb();
            assert(near(db[size_t(bin)], 20.0 * std::log10(A), 0.05));
            // 3) leakage: Hann sidelobes stay far below the peak
            for (int k = 0; k < n / 2; ++k) if (std::abs(k - bin) > 6) assert(db[size_t(k)] < 20.0 * std::log10(A) - 70.0);
        }
    }
    // 4) peak bin is where the tone is, for several frequencies
    {
        Analyser a; a.configure(8192, 44100.0); std::vector<float> s(8192);
        for (double f : {100.0, 1000.0, 5000.0, 16000.0}) {
            a.reset(); for (int i = 0; i < 8192; ++i) s[size_t(i)] = float(0.3 * std::sin(2.0 * M_PI * f * i / 44100.0));
            a.process(s.data());
            const auto& db = a.displayDb(); const int pk = int(std::max_element(db.begin(), db.end()) - db.begin());
            assert(std::fabs(pk * 44100.0 / 8192.0 - f) < 44100.0 / 8192.0);
            // off-bin scalloping loss of a Hann window is at most 1.42 dB
            assert(db[size_t(pk)] > 20 * std::log10(0.3) - 1.5 && db[size_t(pk)] <= 20 * std::log10(0.3) + 0.1);
        }
    }
    // 5) white noise: flat on average; level matches the theory (power per bin)
    {
        Analyser a; a.configure(4096, 48000.0); std::mt19937 rng(1); std::normal_distribution<float> g(0.0f, 0.1f);
        std::vector<float> s(4096); double sum = 0; int frames = 40;
        for (int fr = 0; fr < frames; ++fr) { a.reset(); for (auto& v : s) v = g(rng); a.process(s.data());
            for (size_t k = 100; k < 1900; ++k) sum += std::pow(10.0, a.displayDb()[k] / 10.0); }
        const double meanPow = sum / (frames * 1800.0);          // mean |X|^2 in the display scale
        const double expected = 2.0 * 0.01 * (2.0 / 4096.0) * 1.5 / 1.0 * 1.0;   // see below
        (void)expected;
        // |X|^2*(2/sum w)^2 averages to sigma^2 * 4 * sum(w^2) / (sum w)^2 = 0.01*4*1.5/ (0.5*4096)
        const double theory = 0.01 * 4.0 * (0.375 * 4096.0) / std::pow(0.5 * 4096.0, 2.0);
        assert(near(10 * std::log10(meanPow), 10 * std::log10(theory), 0.25));
    }
    // 6) tilt moves levels by tilt dB per octave around 1 kHz
    {
        Analyser a; a.configure(4096, 48000.0); a.setTilt(4.5f); std::vector<float> s(4096);
        const int b1 = 85, b2 = 171;                              // ~1 kHz and ~2 kHz bins (11.72 Hz each)
        for (int i = 0; i < 4096; ++i) s[size_t(i)] = float(0.2 * std::sin(2 * M_PI * b2 * i / 4096.0));
        a.process(s.data()); (void)b1;
        const double f2 = b2 * 48000.0 / 4096.0;
        assert(near(a.displayDb()[size_t(b2)], 20 * std::log10(0.2) + 4.5 * std::log2(f2 / 1000.0), 0.1));
    }
    // 7) ballistics: instant attack, steady fall, floor, hold
    {
        Analyser a; a.configure(1024, 48000.0); a.setFall(30.0f); std::vector<float> s(1024);
        for (int i = 0; i < 1024; ++i) s[size_t(i)] = float(std::sin(2 * M_PI * 64 * i / 1024.0));
        a.process(s.data()); const float top = a.displayDb()[64]; assert(near(top, 0.0, 0.05));
        a.decay(0.5f); assert(near(a.displayDb()[64], top - 15.0f, 0.01));
        assert(a.holdDb()[64] > a.displayDb()[64]);
        for (int i = 0; i < 100; ++i) a.decay(1.0f); assert(a.displayDb()[64] == floorDb);
    }
    // 8) ring buffer: order, wrap-around, not enough data, concurrent writer
    {
        SampleRing r; std::vector<float> a(100), l(100), out(64);
        for (int i = 0; i < 100; ++i) { a[size_t(i)] = float(i); l[size_t(i)] = float(i) + 2.0f; }
        const float* ch[2] = {a.data(), l.data()}; assert(!r.copyLatest(out.data(), 64));
        r.push(ch, 2, 100); assert(r.position() == 100 && r.copyLatest(out.data(), 64));
        for (int i = 0; i < 64; ++i) assert(out[size_t(i)] == float(36 + i) + 1.0f);          // mono average, oldest first
        for (int k = 0; k < 700; ++k) r.push(ch, 2, 100);                                      // wraps the 32768 buffer
        assert(r.copyLatest(out.data(), 64) && out[63] == 99.0f + 1.0f && out[0] == 36.0f + 1.0f);
        std::atomic<bool> go{true}; long ok = 0;
        std::thread w([&] { std::vector<float> blk(512, 0.5f); const float* c[1] = {blk.data()}; for (int k = 0; k < 20000; ++k) r.push(c, 1, 512); go = false; });
        std::vector<float> big(4096); while (go) { if (r.copyLatest(big.data(), 4096)) ++ok; }
        w.join(); assert(ok > 0);
    }
    // 9) columns: a lone peak lands in the right column; no peak is lost on dense columns
    {
        Analyser a; a.configure(16384, 48000.0); std::vector<float> s(16384);
        for (double f : {60.0, 440.0, 1000.0, 9000.0}) {
            a.reset(); const int bin = int(std::lround(f * 16384 / 48000.0)); for (int i = 0; i < 16384; ++i) s[size_t(i)] = float(0.5 * std::sin(2 * M_PI * bin * i / 16384.0));
            a.process(s.data()); std::vector<float> col(1000); toColumns(a.displayDb().data(), a.numBins(), 48000.0, 1000, 20.0f, 30000.0f, col.data());
            const int pk = int(std::max_element(col.begin(), col.end()) - col.begin());
            const double fPk = 20.0 * std::pow(1500.0, (pk + 0.5) / 1000.0), fTrue = bin * 48000.0 / 16384.0;
            assert(std::fabs(std::log2(fPk / fTrue)) < 0.02);                          // within 0.02 octave
            assert(col[size_t(pk)] > 20 * std::log10(0.5) - 3.1);
        }
        std::vector<float> col(1000); toColumns(a.displayDb().data(), a.numBins(), 48000.0, 1000, 20.0f, 30000.0f, col.data());
        assert(col[999] == floorDb);                                                    // above Nyquist (30 kHz > 24 kHz) is empty
    }
    // 10) colours: range colours are reached at range centres, blended between, monotone hue steps
    {
        std::uint32_t c[7]; for (int i = 0; i < 7; ++i) c[i] = defaultColours[i];
        for (int i = 0; i < 7; ++i) assert(colourAt(std::sqrt(ranges[i].lo * ranges[i].hi), c) == c[i]);
        assert(colourAt(20, c) == c[0] && colourAt(30000, c) == c[6]);
        const auto mid = colourAt(std::sqrt(std::sqrt(ranges[0].lo * ranges[0].hi) * std::sqrt(ranges[1].lo * ranges[1].hi)), c);
        assert(mid != c[0] && mid != c[1]);
        assert(rangeIndexFor(30) == 0 && rangeIndexFor(1000) == 3 && rangeIndexFor(20000) == 6);
    }
    // 11) speed: one 16384-point analysis (window + FFT + dB) well under a millisecond or two
    {
        Analyser a; a.configure(16384, 48000.0); std::vector<float> s(16384); std::mt19937 rng(3); std::uniform_real_distribution<float> d(-1, 1); for (auto& v : s) v = d(rng);
        a.process(s.data()); auto t0 = std::chrono::steady_clock::now(); const int runs = 200;
        for (int i = 0; i < runs; ++i) a.process(s.data());
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / runs;
        std::cout << "16384-point analysis: " << ms << " ms per frame (" << ms * 30 / 10.0 << " % of one core at 30 fps)\n";
        assert(ms < 2.0);
    }
    // 12) fractional-octave smoothing on columns: flat stays flat, ripple shrinks, a tone stays centred, power is kept
    {
        const int cols = 1000; const float octaves = float(std::log2(1500.0));        // 20 Hz .. 30 kHz
        std::vector<float> in(std::size_t(cols), -40.0f), out(std::size_t(cols), 0.0f), scratch(std::size_t(cols), 0.0f); std::vector<double> prefix(std::size_t(cols) + 1, 0.0);
        smoothColumns(in.data(), out.data(), cols, octaves, 3, scratch.data(), prefix.data());
        for (int i = 0; i < cols; ++i) assert(near(out[std::size_t(i)], -40.0, 0.01));
        std::mt19937 rng(11); std::normal_distribution<float> g(0.0f, 4.0f); std::vector<float> noisy(std::size_t(cols), 0.0f); for (auto& v : noisy) v = -50.0f + g(rng);
        auto spread = [&](const std::vector<float>& v) { double m = 0, q = 0; int c = 0; for (int k = 100; k < 900; ++k) { m += v[std::size_t(k)]; ++c; } m /= c; for (int k = 100; k < 900; ++k) q += (v[std::size_t(k)] - m) * (v[std::size_t(k)] - m); return std::sqrt(q / c); };
        std::vector<float> o1(std::size_t(cols), 0.0f), o3(std::size_t(cols), 0.0f), o24(std::size_t(cols), 0.0f);
        smoothColumns(noisy.data(), o1.data(), cols, octaves, 1, scratch.data(), prefix.data()); smoothColumns(noisy.data(), o3.data(), cols, octaves, 3, scratch.data(), prefix.data()); smoothColumns(noisy.data(), o24.data(), cols, octaves, 24, scratch.data(), prefix.data());
        assert(spread(noisy) > 3.0 && spread(o24) < spread(noisy) && spread(o3) < spread(o24) && spread(o1) < spread(o3));    // wider window -> smoother
        std::vector<float> tone(std::size_t(cols), -100.0f); tone[600] = -6.0f;
        smoothColumns(tone.data(), out.data(), cols, octaves, 3, scratch.data(), prefix.data());
        const int pk = int(std::max_element(out.begin(), out.end()) - out.begin()); assert(std::abs(pk - 600) <= 1);          // stays centred
        assert(near(out[std::size_t(600 - 40)], out[std::size_t(600 + 40)], 0.5));                                           // and symmetric in log frequency
        double pIn = 0, pOut = 0; for (int i = 0; i < cols; ++i) { pIn += std::pow(10.0, tone[std::size_t(i)] / 10.0); pOut += std::pow(10.0, out[std::size_t(i)] / 10.0); }
        assert(std::fabs(10 * std::log10(pOut / pIn)) < 0.2);                                                                // power conserved
        smoothColumns(tone.data(), out.data(), cols, octaves, 1000, scratch.data(), prefix.data()); assert(out[600] == tone[600]);   // too narrow: pass-through
    }
    // 13) spectrogram history + colour maps
    {
        SpectrogramHistory h; h.configure(4, 3); float r1[4] = {-90, -60, -30, 0}, r2[4] = {0, 0, 0, 0}, r3[4] = {-120, -120, -120, -120};
        h.push(r1, -90.0f, 0.0f); assert(h.filled() == 1 && h.row(0)[0] == 0 && h.row(0)[3] == 255 && std::abs(int(h.row(0)[1]) - 85) <= 1);
        h.push(r2, -90.0f, 0.0f); assert(h.row(0)[0] == 255 && h.row(1)[3] == 255 && h.row(1)[0] == 0);    // newest first
        h.push(r3, -90.0f, 0.0f); h.push(r2, -90.0f, 0.0f); assert(h.filled() == 3 && h.row(0)[0] == 255 && h.row(1)[0] == 0 && h.row(2)[0] == 255);   // wraps, oldest dropped
        assert(heatColour(0) == 0xff000000u && heatColour(255) != 0xff000000u && (heatColour(255) & 0xffu) > 200);
        int prev = -1; for (int v = 0; v <= 255; v += 5) { const auto c = heatColour(std::uint8_t(v)); const int lum = int((c >> 16) & 255) + int((c >> 8) & 255) + int(c & 255); assert(lum >= prev - 40); prev = lum; }   // brightens with level
        assert(shadeColour(0xffff0000u, 0) == 0xff000000u && (shadeColour(0xffff0000u, 178) >> 16 & 255) > 240 && shadeColour(0xff3040ffu, 255) != 0xff3040ffu);
    }
    std::cout << "spectrum: FFT vs DFT, calibration, leakage, bin location, noise level, tilt, ballistics, ring, columns, colours, speed OK\n";
}
