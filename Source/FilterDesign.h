#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include "AnalogModel.h"

/** Generic bell, shelf, notch, band-pass, and cut designs.

    Slope controls filter order, including fractional values. Bells and notches span order 2..16,
    shelves 1..16, and cuts and band-pass 0..16. Bells use Orfanidis Butterworth parametric designs;
    shelves use Holters-Zoelzer Butterworth designs. Notch and band-pass prototypes use W = Q(s/w0 + w0/s).
    Fractional orders interpolate adjacent section coefficients. Cut orders between 1 and 2 use
    fitted tables; below order 1, first-order sections blend towards unity.

    Digitization uses impulse-invariant poles and a numerator matched at DC, the section frequency,
    and Nyquist. All processing here is allocation-free. */
namespace FilterDesign
{
    enum class Shape { Bell, LowShelf, HighShelf, Notch, BandPass, LowPass, HighPass };

    struct Biquad { double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0; };

    namespace detail
    {
        using C = std::complex<double>;
        constexpr double pi = 3.14159265358979323846;

        /** Cut and band-pass order: 0 at slope 0, 2 at 0.5, 8 at 0.75, 16 at 1. */
        inline double cutOrder (double s)
        {
            s = std::clamp (s, 0.0, 1.0);
            if (s <= 0.5)  return 4.0 * s;
            if (s <= 0.75) return 2.0 + 24.0 * (s - 0.5);
            return 8.0 + 32.0 * (s - 0.75);
        }

        /** Bell and notch prototype order (half the total order): 1 at slope 0, 2 at 0.5, 4 at 0.75, 8 at 1. */
        inline double bellOrder (double s)
        {
            s = std::clamp (s, 0.0, 1.0);
            double label = 0.0;
            if (s <= 0.5)       label = 12.0 + 24.0 * s;
            else if (s <= 0.75) label = 24.0 + 96.0 * (s - 0.5);
            else                label = 48.0 + 192.0 * (s - 0.75);
            return label / 12.0;
        }

        /** Shelf order: 1 at slope 0, 4 at 0.5, 8 at 0.75, 16 at 1. */
        inline double shelfOrder (double s)
        {
            s = std::clamp (s, 0.0, 1.0);
            if (s <= 0.5)  return 1.0 + 6.0 * s;
            if (s <= 0.75) return 4.0 + 16.0 * (s - 0.5);
            return 8.0 + 32.0 * (s - 0.75);
        }

        /** A prototype section in the prototype variable W: n(W) / d(W), coefficients ascending (c0 + c1 W + c2 W^2). */
        struct ProtoSection
        {
            double n[3] = { 1.0, 0.0, 0.0 };
            double d[3] = { 1.0, 0.0, 0.0 };
        };

        enum class Variable { BandPass, LowPass, HighPass }; // W = Q(s/w0 + w0/s), W = s/w0, W = w0/s

        struct Proto
        {
            std::array<ProtoSection, 10> sections {};
            int count = 0;
            double gain = 1.0;
            Variable variable = Variable::LowPass;
        };

        /** Butterworth sections of order n in W, high Q first, first-order last: quads 1 + 2 sin(theta) W + W^2. */
        inline int butterworthSections (int n, ProtoSection* out, bool asDenominator)
        {
            int count = 0;
            double c1s[8] = {};
            for (int k = 1; k <= n / 2; ++k)
                c1s[k - 1] = 2.0 * std::sin (pi * (2.0 * k - 1.0) / (2.0 * n));
            std::sort (c1s, c1s + n / 2); // small c1 = high Q first
            for (int k = 0; k < n / 2; ++k)
            {
                ProtoSection s;
                double* c = asDenominator ? s.d : s.n;
                c[0] = 1.0; c[1] = c1s[k]; c[2] = 1.0;
                out[count++] = s;
            }
            if (n % 2 == 1)
            {
                ProtoSection s;
                double* c = asDenominator ? s.d : s.n;
                c[0] = 1.0; c[1] = 1.0; c[2] = 0.0;
                out[count++] = s;
            }
            return count;
        }

        /** Integer-order bell or shelf in W: G * prod B(W/r) / B(rW), r = G^(1/(2n)). */
        inline int shelfLikeSections (int n, double g, ProtoSection* out)
        {
            ProtoSection base[10];
            const int count = butterworthSections (n, base, true);
            const double r = std::pow (g, 1.0 / (2.0 * n));
            for (int i = 0; i < count; ++i)
            {
                const auto& d = base[i].d;
                ProtoSection s;
                for (int k = 0; k < 3; ++k)
                {
                    s.n[k] = d[k] * std::pow (1.0 / r, k);
                    s.d[k] = d[k] * std::pow (r, k);
                }
                out[i] = s;
            }
            return count;
        }

        /** Integer-order Butterworth low-pass (1/B) or high-pass (W^n / B) prototype in W. */
        inline int cutSections (int n, bool highPass, ProtoSection* out)
        {
            ProtoSection base[10];
            const int count = butterworthSections (n, base, true);
            for (int i = 0; i < count; ++i)
            {
                ProtoSection s;
                std::copy (base[i].d, base[i].d + 3, s.d);
                const bool quad = base[i].d[2] != 0.0;
                s.n[0] = highPass ? 0.0 : 1.0; s.n[1] = 0.0; s.n[2] = 0.0;
                if (highPass)
                {
                    if (quad) s.n[2] = 1.0;
                    else      s.n[1] = 1.0;
                }
                out[i] = s;
            }
            return count;
        }

        inline void convolveFirstOrder (double* c, double a0, double a1)
        {
            // c (degree <= 1 here) times (a0 + a1 W), result written back into c[0..2].
            const double c0 = c[0], c1 = c[1];
            c[0] = c0 * a0;
            c[1] = c0 * a1 + c1 * a0;
            c[2] = c1 * a1;
        }

        /** Interpolates two section lists of orders n and n+1 (the order-n list is padded with (1+W)/(1+W)). */
        template <typename MakeSections>
        inline int fractionalSections (double order, MakeSections make, ProtoSection* out)
        {
            const int n = (int) std::floor (order + 1.0e-9);
            const double x = order - n;
            ProtoSection lo[10], hi[10];
            int loCount = make (n, lo);
            if (x < 1.0e-9)
            {
                std::copy (lo, lo + loCount, out);
                return loCount;
            }
            const int hiCount = make (n + 1, hi);
            if (n % 2 == 0)
            {
                ProtoSection pad;
                pad.n[0] = 1.0; pad.n[1] = 1.0; pad.n[2] = 0.0;
                pad.d[0] = 1.0; pad.d[1] = 1.0; pad.d[2] = 0.0;
                lo[loCount++] = pad;
            }
            else
            {
                // The first-order section of the order-n list becomes a quad: (c0 + c1 W)(1 + W).
                auto& last = lo[loCount - 1];
                convolveFirstOrder (last.n, 1.0, 1.0);
                convolveFirstOrder (last.d, 1.0, 1.0);
            }
            const int count = std::min (loCount, hiCount);
            auto blend = [x] (double a, double b)
            {
                if (a > 0.0 && b > 0.0)
                    return std::pow (a, 1.0 - x) * std::pow (b, x); // geometric (exact for bells and shelves)
                return a * (1.0 - x) + b * x;                         // linear where a coefficient is zero
            };
            for (int i = 0; i < count; ++i)
                for (int k = 0; k < 3; ++k)
                {
                    out[i].n[k] = blend (lo[i].n[k], hi[i].n[k]);
                    out[i].d[k] = blend (lo[i].d[k], hi[i].d[k]);
                }
            return count;
        }

        /** Cut between orders 1 and 2: HP(V) = V (V + z) / (V^2 + a V + b), fitted to the plug-in (LP mirrors V -> 1/V). */
        inline void cutOneToTwo (double order, bool highPass, ProtoSection& out)
        {
            static const double table[][4] = {
                { 1.00, 0.618, 1.618, 0.618 }, { 1.05, 0.583, 1.596, 0.637 }, { 1.20, 0.487, 1.543, 0.695 },
                { 1.40, 0.378, 1.495, 0.775 }, { 1.50, 0.327, 1.476, 0.814 }, { 1.60, 0.278, 1.460, 0.852 },
                { 1.80, 0.177, 1.434, 0.927 }, { 1.95, 0.082, 1.419, 0.982 }, { 2.00, 0.000, 1.41421356, 1.000 }
            };
            const int rows = (int) (sizeof (table) / sizeof (table[0]));
            int i = 0;
            while (i < rows - 2 && order > table[i + 1][0])
                ++i;
            const double t = std::clamp ((order - table[i][0]) / (table[i + 1][0] - table[i][0]), 0.0, 1.0);
            const double z = table[i][1] + (table[i + 1][1] - table[i][1]) * t;
            const double a = table[i][2] + (table[i + 1][2] - table[i][2]) * t;
            const double b = table[i][3] + (table[i + 1][3] - table[i][3]) * t;
            if (highPass)
            {
                out.n[0] = 0.0; out.n[1] = z; out.n[2] = 1.0;
                out.d[0] = b;   out.d[1] = a; out.d[2] = 1.0;
            }
            else
            {
                // V -> 1/V and multiply through by V^2: (1 + z V) / (1 + a V + b V^2).
                out.n[0] = 1.0; out.n[1] = z; out.n[2] = 0.0;
                out.d[0] = 1.0; out.d[1] = a; out.d[2] = b;
            }
        }

        /** Below order 1 the plug-in follows (first-order section)^n. Four interleaved first-order pole/zero pairs,
            log-spaced over three decades below the corner (an Oustaloup approximation of V^n), follow that slope.
            highPass gives (V + z)/(V + p) factors; otherwise the mirror (1 + zV)/(1 + pV). */
        inline int fractionalFirstOrder (double n, bool highPass, ProtoSection* out)
        {
            constexpr int pairs = 4;
            const double low = 1.0e-3, high = 1.0;
            for (int k = 1; k <= pairs; ++k)
            {
                const double zero = low * std::pow (high / low, (k - 0.5 - 0.5 * n) / pairs);
                const double pole = low * std::pow (high / low, (k - 0.5 + 0.5 * n) / pairs);
                ProtoSection s;
                if (highPass) { s.n[0] = zero; s.n[1] = 1.0; s.d[0] = pole; s.d[1] = 1.0; }
                else          { s.n[0] = 1.0; s.n[1] = zero; s.d[0] = 1.0; s.d[1] = pole; }
                out[k - 1] = s;
            }
            return pairs;
        }

        /** Builds the prototype for a shape. Returns false when the band does nothing (order 0). */
        inline bool buildProto (Shape shape, double gainDb, double q, double slope, Proto& p)
        {
            const double g = std::pow (10.0, gainDb / 20.0);
            p.gain = 1.0;
            p.count = 0;
            switch (shape)
            {
                case Shape::Bell:
                {
                    p.variable = Variable::BandPass;
                    if (std::abs (gainDb) < 1.0e-9) return false;
                    p.count = fractionalSections (bellOrder (slope), [g] (int n, ProtoSection* o) { return shelfLikeSections (n, g, o); }, p.sections.data());
                    p.gain = g;
                    return true;
                }
                case Shape::LowShelf:
                case Shape::HighShelf:
                {
                    p.variable = shape == Shape::LowShelf ? Variable::LowPass : Variable::HighPass;
                    if (std::abs (gainDb) < 1.0e-9) return false;
                    p.count = fractionalSections (shelfOrder (slope), [g] (int n, ProtoSection* o) { return shelfLikeSections (n, g, o); }, p.sections.data());
                    p.gain = g;
                    return true;
                }
                case Shape::Notch:
                {
                    p.variable = Variable::BandPass;
                    p.count = fractionalSections (bellOrder (slope), [] (int n, ProtoSection* o) { return cutSections (n, true, o); }, p.sections.data());
                    return true;
                }
                case Shape::BandPass:
                {
                    p.variable = Variable::BandPass;
                    const double m = cutOrder (slope) / 2.0;
                    if (m < 1.0e-6) return false;
                    if (m < 1.0)
                    {
                        p.count = fractionalFirstOrder (m, false, p.sections.data());
                        return true;
                    }
                    p.count = fractionalSections (m, [] (int n, ProtoSection* o) { return cutSections (n, false, o); }, p.sections.data());
                    return true;
                }
                case Shape::LowPass:
                case Shape::HighPass:
                {
                    const bool highPass = shape == Shape::HighPass;
                    p.variable = Variable::LowPass; // V = s / w0; a high pass is written directly in V
                    const double n = cutOrder (slope);
                    if (n < 1.0e-6) return false;
                    ProtoSection s;
                    if (n < 1.0)
                    {
                        p.count = fractionalFirstOrder (n, highPass, p.sections.data());
                        return true;
                    }
                    if (n < 2.0 - 1.0e-9)
                    {
                        cutOneToTwo (n, highPass, s);
                        p.sections[0] = s;
                        p.count = 1;
                        return true;
                    }
                    if (std::abs (n - 2.0) < 1.0e-9)
                    {
                        // Order 2: the section Q is the band Q / sqrt(2), so Q = 1 is Butterworth.
                        const double qs = std::max (0.025, q * 0.70710678118654752);
                        s.d[0] = 1.0; s.d[1] = 1.0 / qs; s.d[2] = 1.0;
                        if (highPass) { s.n[0] = 0.0; s.n[1] = 0.0; s.n[2] = 1.0; }
                        else          { s.n[0] = 1.0; }
                        p.sections[0] = s;
                        p.count = 1;
                        return true;
                    }
                    if (highPass)
                    {
                        p.variable = Variable::HighPass; // W = w0/s turns the low-pass prototype into a high pass
                    }
                    p.count = fractionalSections (n, [] (int k, ProtoSection* o) { return cutSections (k, false, o); }, p.sections.data());
                    return true;
                }
            }
            return false;
        }

        inline C evalPoly (const double* c, C w) { return c[0] + w * (c[1] + w * c[2]); }

        /** Prototype variable at the analogue frequency omega (rad/s). */
        inline C protoVariable (Variable v, double w0, double q, double omega)
        {
            const C s (0.0, omega);
            switch (v)
            {
                case Variable::BandPass: return q * (s / w0 + w0 / s);
                case Variable::LowPass:  return s / w0;
                case Variable::HighPass: return w0 / s;
            }
            return s / w0;
        }

        inline double protoMagnitude (const Proto& p, double w0, double q, double omega)
        {
            const C w = protoVariable (p.variable, w0, q, omega);
            C h = p.gain;
            for (int i = 0; i < p.count; ++i)
                h *= evalPoly (p.sections[(size_t) i].n, w) / evalPoly (p.sections[(size_t) i].d, w);
            return std::abs (h);
        }

        /** Roots of c0 + c1 W + c2 W^2 (degree 0..2). Returns the root count. */
        inline int rootsOf (const double* c, C* out)
        {
            if (std::abs (c[2]) > 1.0e-300)
            {
                const C disc = std::sqrt (C (c[1] * c[1] - 4.0 * c[2] * c[0], 0.0));
                out[0] = (-c[1] + disc) / (2.0 * c[2]);
                out[1] = (-c[1] - disc) / (2.0 * c[2]);
                return 2;
            }
            if (std::abs (c[1]) > 1.0e-300)
            {
                out[0] = -c[0] / c[1];
                return 1;
            }
            return 0;
        }

        /** Maps prototype roots to s-plane roots. Fills `out`, returns the count. */
        inline int mapRoot (Variable v, double w0, double q, C w, C* out)
        {
            switch (v)
            {
                case Variable::BandPass:
                {
                    const C half = w / (2.0 * q);
                    const C rad = std::sqrt (half * half - 1.0);
                    out[0] = w0 * (half + rad);
                    out[1] = w0 * (half - rad);
                    return 2;
                }
                case Variable::LowPass:
                    out[0] = w0 * w;
                    return 1;
                case Variable::HighPass:
                    if (std::abs (w) < 1.0e-300) return 0; // W = 0 maps to s at infinity
                    out[0] = w0 / w;
                    return 1;
            }
            return 0;
        }

        /** Groups roots into conjugate pairs and lone reals. Each group is stored as a monic polynomial (c0, c1, c2). */
        inline int groupRoots (const C* roots, int count, double (*groups)[3], double* freq)
        {
            bool used[48] = {};
            int g = 0;
            for (int i = 0; i < count; ++i)
            {
                if (used[i]) continue;
                used[i] = true;
                const C r = roots[i];
                const double scale = std::max (1.0, std::abs (r));
                if (std::abs (r.imag()) > 1.0e-9 * scale)
                {
                    // Find its conjugate.
                    int best = -1;
                    double bestDist = 1.0e300;
                    for (int j = i + 1; j < count; ++j)
                        if (! used[j])
                        {
                            const double d = std::abs (roots[j] - std::conj (r));
                            if (d < bestDist) { bestDist = d; best = j; }
                        }
                    if (best >= 0) used[best] = true;
                    groups[g][0] = std::norm (r);
                    groups[g][1] = -2.0 * r.real();
                    groups[g][2] = 1.0;
                    freq[g] = std::abs (r);
                    ++g;
                }
                else
                {
                    // Pair two real roots where possible.
                    int other = -1;
                    for (int j = i + 1; j < count; ++j)
                        if (! used[j] && std::abs (roots[j].imag()) <= 1.0e-9 * std::max (1.0, std::abs (roots[j])))
                        { other = j; break; }
                    if (other >= 0)
                    {
                        used[other] = true;
                        const double a = r.real(), b = roots[other].real();
                        groups[g][0] = a * b; groups[g][1] = -(a + b); groups[g][2] = 1.0;
                        freq[g] = std::sqrt (std::abs (a * b));
                    }
                    else
                    {
                        groups[g][0] = -r.real(); groups[g][1] = 1.0; groups[g][2] = 0.0;
                        freq[g] = std::abs (r.real());
                    }
                    ++g;
                }
            }
            return g;
        }
    }

    /** Prototype order for a shape and normalised slope (bells and notches: the half order). */
    inline double prototypeOrder (Shape shape, double slopeNorm)
    {
        switch (shape)
        {
            case Shape::Bell: case Shape::Notch:        return detail::bellOrder (slopeNorm);
            case Shape::LowShelf: case Shape::HighShelf: return detail::shelfOrder (slopeNorm);
            case Shape::BandPass:                        return detail::cutOrder (slopeNorm) / 2.0;
            case Shape::LowPass: case Shape::HighPass:   return detail::cutOrder (slopeNorm);
        }
        return 1.0;
    }

    /** Analogue design: sections of monic s-domain polynomials plus an overall gain matched to the prototype.
        Returns false when the band does nothing. */
    inline bool designAnalog (Shape shape, double f0, double gainDb, double q, double slopeNorm, AnalogModel::AnalogCascade& out)
    {
        using namespace detail;
        out.count = 0;
        out.gain = 1.0;
        Proto p;
        if (! buildProto (shape, gainDb, q, slopeNorm, p) || p.count == 0)
            return false;

        const double w0 = 2.0 * pi * f0;
        q = std::max (0.025, q);
        C zeros[48], poles[48];
        int nz = 0, np = 0, protoZeros = 0, protoPoles = 0;
        for (int i = 0; i < p.count; ++i)
        {
            C r[2];
            const int zc = rootsOf (p.sections[(size_t) i].n, r);
            for (int k = 0; k < zc; ++k) { protoZeros++; C m[2]; const int mc = mapRoot (p.variable, w0, q, r[k], m); for (int j = 0; j < mc && nz < 48; ++j) zeros[nz++] = m[j]; }
            const int pc = rootsOf (p.sections[(size_t) i].d, r);
            for (int k = 0; k < pc; ++k) { protoPoles++; C m[2]; const int mc = mapRoot (p.variable, w0, q, r[k], m); for (int j = 0; j < mc && np < 48; ++j) poles[np++] = m[j]; }
        }
        // Band-pass and high-pass variables add zeros at s = 0 for each prototype pole without a matching zero.
        if (p.variable != Variable::LowPass)
            for (int k = protoZeros; k < protoPoles && nz < 48; ++k)
                zeros[nz++] = C (0.0, 0.0);

        // Zeros at the origin are handed out last, spread over the sections (one each for a band pass, two for a high pass).
        int originZeros = 0;
        {
            int kept = 0;
            for (int i = 0; i < nz; ++i)
            {
                if (std::abs (zeros[i]) < 1.0e-9 * std::max (1.0, w0)) ++originZeros;
                else zeros[kept++] = zeros[i];
            }
            nz = kept;
        }
        double zg[48][3], pg[48][3], zf[48], pf[48];
        const int zgCount = groupRoots (zeros, nz, zg, zf);
        const int pgCount = groupRoots (poles, np, pg, pf);

        // Pair each pole group with the zero group nearest in frequency, preferring the same degree.
        bool zUsed[48] = {};
        int count = 0;
        for (int i = 0; i < pgCount && count < AnalogModel::AnalogCascade::maxSections; ++i)
        {
            AnalogModel::AnalogSection s;
            s.a0 = pg[i][0]; s.a1 = pg[i][1]; s.a2 = pg[i][2];
            int best = -1;
            double bestScore = 1.0e300;
            for (int j = 0; j < zgCount; ++j)
            {
                if (zUsed[j]) continue;
                const bool degreeOk = (zg[j][2] != 0.0) == (pg[i][2] != 0.0) || pg[i][2] != 0.0;
                if (! degreeOk) continue;
                const double score = std::abs (std::log ((zf[j] + 1.0e-9) / (pf[i] + 1.0e-9)))
                                   + ((zg[j][2] != 0.0) == (pg[i][2] != 0.0) ? 0.0 : 100.0);
                if (score < bestScore) { bestScore = score; best = j; }
            }
            if (best >= 0)
            {
                zUsed[best] = true;
                s.b0 = zg[best][0]; s.b1 = zg[best][1]; s.b2 = zg[best][2];
            }
            else
            {
                s.b0 = 1.0; s.b1 = 0.0; s.b2 = 0.0;
            }
            out.sections[(size_t) count++] = s;
        }
        out.count = count;

        // Spread the origin zeros: first one per section that still has room, then a second.
        for (int pass = 0; pass < 2 && originZeros > 0; ++pass)
            for (int i = 0; i < count && originZeros > 0; ++i)
            {
                auto& sec = out.sections[(size_t) i];
                const int numDegree = sec.b2 != 0.0 ? 2 : (sec.b1 != 0.0 ? 1 : 0);
                const int denDegree = sec.a2 != 0.0 ? 2 : 1;
                if (numDegree >= denDegree)
                    continue;
                // Multiply the numerator by s.
                sec.b2 = sec.b1; sec.b1 = sec.b0; sec.b0 = 0.0;
                --originZeros;
            }

        // Overall gain: match the prototype magnitude at the best-conditioned of a few reference frequencies.
        double bestRef = 0.0, bestMag = -1.0;
        for (double factor : { 1.0, 0.5, 2.0, 0.25, 4.0, 0.1, 10.0 })
        {
            const double omega = w0 * factor;
            const double mag = protoMagnitude (p, w0, q, omega);
            if (std::isfinite (mag) && mag > bestMag) { bestMag = mag; bestRef = omega; }
        }
        C h = 1.0;
        const C s (0.0, bestRef);
        for (int i = 0; i < count; ++i)
        {
            const auto& sec = out.sections[(size_t) i];
            h *= (sec.b0 + s * (sec.b1 + s * sec.b2)) / (sec.a0 + s * (sec.a1 + s * sec.a2));
        }
        out.gain = std::abs (h) > 0.0 ? bestMag / std::abs (h) : 1.0;
        return true;
    }

    /** Roots of c[0] + c[1] x + ... + c[degree] x^degree (real coefficients, degree up to 8), by Durand-Kerner with
        Newton polishing. Returns the root count (the degree after leading zeros are trimmed). */
    inline int polynomialRoots (const double* c, int degree, std::complex<double>* roots)
    {
        using C = std::complex<double>;
        double scaleMax = 0.0;
        for (int i = 0; i <= degree; ++i) scaleMax = std::max (scaleMax, std::abs (c[i]));
        while (degree > 0 && std::abs (c[degree]) <= 1.0e-14 * scaleMax) --degree;
        if (degree <= 0 || degree > 8) return 0;
        double monic[9];
        for (int i = 0; i <= degree; ++i) monic[i] = c[i] / c[degree];
        auto eval = [&] (C x) { C v = 1.0; for (int i = degree - 1; i >= 0; --i) v = v * x + monic[i]; return v; };
        auto evalDerivative = [&] (C x) { C v = (double) degree; for (int i = degree - 1; i >= 1; --i) v = v * x + (double) i * monic[i]; return v; };
        // Cauchy bound for the starting circle.
        double radius = 0.0;
        for (int i = 0; i < degree; ++i) radius = std::max (radius, std::abs (monic[i]));
        radius = std::min (1.0 + radius, 1.0e12);
        const double start = std::max (1.0e-12, std::pow (std::abs (monic[0]), 1.0 / degree));
        for (int i = 0; i < degree; ++i)
            roots[i] = std::polar (std::min (start, radius), 0.4 + 2.0 * 3.14159265358979323846 * i / degree);
        for (int iteration = 0; iteration < 500; ++iteration)
        {
            double change = 0.0;
            for (int i = 0; i < degree; ++i)
            {
                C denominator = 1.0;
                for (int j = 0; j < degree; ++j)
                    if (j != i) denominator *= roots[i] - roots[j];
                if (std::abs (denominator) < 1.0e-300) denominator = 1.0e-300;
                const C step = eval (roots[i]) / denominator;
                roots[i] -= step;
                change = std::max (change, std::abs (step) / std::max (1.0e-300, std::abs (roots[i])));
            }
            if (change < 1.0e-15) break;
        }
        for (int i = 0; i < degree; ++i)
            for (int k = 0; k < 4; ++k)
            {
                const C d = evalDerivative (roots[i]);
                if (std::abs (d) < 1.0e-300) break;
                roots[i] -= eval (roots[i]) / d;
            }
        // Clean near-real roots, so that conjugate grouping sees them as real.
        for (int i = 0; i < degree; ++i)
            if (std::abs (roots[i].imag()) < 1.0e-10 * std::max (1.0, std::abs (roots[i])))
                roots[i] = C (roots[i].real(), 0.0);
        return degree;
    }

    /** Turns a rational function H(x) = num(x) / den(x), in a normalised variable x = s / omegaScale, into an analogue
        cascade in s (rad/s). Poles and zeros are grouped into sections by nearest frequency, and the overall gain is set
        so the cascade equals H at x = j. Used for the vintage circuit models. */
    inline bool cascadeFromRational (const double* num, int numDegree, const double* den, int denDegree, double omegaScale,
                                     AnalogModel::AnalogCascade& out)
    {
        using C = std::complex<double>;
        out.count = 0;
        out.gain = 1.0;
        C zeros[8], poles[8];
        const int nz = polynomialRoots (num, numDegree, zeros);
        const int np = polynomialRoots (den, denDegree, poles);
        if (np == 0) return false;
        for (int i = 0; i < nz; ++i) zeros[i] *= omegaScale;
        for (int i = 0; i < np; ++i) poles[i] *= omegaScale;
        double zg[8][3], pg[8][3], zf[8], pf[8];
        const int zgCount = detail::groupRoots (zeros, nz, zg, zf);
        const int pgCount = detail::groupRoots (poles, np, pg, pf);
        bool zUsed[8] = {};
        int count = 0;
        for (int i = 0; i < pgCount && count < AnalogModel::AnalogCascade::maxSections; ++i)
        {
            AnalogModel::AnalogSection s;
            s.a0 = pg[i][0]; s.a1 = pg[i][1]; s.a2 = pg[i][2];
            int best = -1;
            double bestScore = 1.0e300;
            for (int j = 0; j < zgCount; ++j)
            {
                if (zUsed[j] || (zg[j][2] != 0.0 && pg[i][2] == 0.0)) continue; // a first-order section takes one zero
                const double score = std::abs (std::log ((zf[j] + 1.0e-9) / (pf[i] + 1.0e-9)))
                                   + ((zg[j][2] != 0.0) == (pg[i][2] != 0.0) ? 0.0 : 100.0);
                if (score < bestScore) { bestScore = score; best = j; }
            }
            if (best >= 0)
            {
                zUsed[best] = true;
                s.b0 = zg[best][0]; s.b1 = zg[best][1]; s.b2 = zg[best][2];
            }
            out.sections[(size_t) count++] = s;
        }
        // Zero groups left over (more zeros than pole sections) go into sections that still have room.
        for (int j = 0; j < zgCount; ++j)
        {
            if (zUsed[j] || zg[j][2] != 0.0) continue;
            for (int i = 0; i < count; ++i)
            {
                auto& sec = out.sections[(size_t) i];
                const int numDegree = sec.b2 != 0.0 ? 2 : (sec.b1 != 0.0 ? 1 : 0);
                const int denDegree = sec.a2 != 0.0 ? 2 : 1;
                if (numDegree < denDegree)
                {
                    // Multiply (b1 s + b0) by (s + z0).
                    const double b0 = sec.b0, b1 = sec.b1;
                    sec.b2 = b1; sec.b1 = b0 + b1 * zg[j][0]; sec.b0 = b0 * zg[j][0];
                    zUsed[j] = true;
                    break;
                }
            }
        }
        out.count = count;

        auto evalAsc = [] (const double* c, int degree, C x) { C v = 0.0; for (int i = degree; i >= 0; --i) v = v * x + c[i]; return v; };
        const C target = evalAsc (num, numDegree, C (0.0, 1.0)) / evalAsc (den, denDegree, C (0.0, 1.0));
        C h = 1.0;
        const C s (0.0, omegaScale);
        for (int i = 0; i < count; ++i)
        {
            const auto& sec = out.sections[(size_t) i];
            h *= (sec.b0 + s * (sec.b1 + s * sec.b2)) / (sec.a0 + s * (sec.a1 + s * sec.a2));
        }
        out.gain = std::abs (h) > 0.0 ? std::abs (target) / std::abs (h) : 1.0;
        return true;
    }

    /** Nyquist-matched digitisation of one analogue section: impulse-invariant poles, numerator matched to the analogue
        magnitude at DC, at the section frequency and at Nyquist. */
    inline Biquad matchSection (const AnalogModel::AnalogSection& a, double sampleRate)
    {
        using namespace detail;
        const double T = 1.0 / sampleRate;
        auto analogMag2 = [&a] (double omega)
        {
            const C s (0.0, omega);
            const C h = (a.b0 + s * (a.b1 + s * a.b2)) / (a.a0 + s * (a.a1 + s * a.a2));
            return std::norm (h);
        };
        const double nyquist = pi * sampleRate;
        Biquad d;
        if (std::abs (a.a2) < 1.0e-300)
        {
            // First order: pole -a0/a1.
            const double pole = -a.a0 / a.a1;
            const double zp = std::exp (pole * T);
            d.a1 = -zp;
            const double h0 = std::sqrt (analogMag2 (0.0));
            const double hn = std::sqrt (analogMag2 (nyquist));
            d.b0 = 0.5 * ((1.0 + d.a1) * h0 + (1.0 - d.a1) * hn);
            d.b1 = 0.5 * ((1.0 + d.a1) * h0 - (1.0 - d.a1) * hn);
            d.b2 = 0.0;
            d.a2 = 0.0;
            return d;
        }
        // Second order: poles of a2 s^2 + a1 s + a0.
        const C disc = std::sqrt (C (a.a1 * a.a1 - 4.0 * a.a2 * a.a0, 0.0));
        const C p1 = (-a.a1 + disc) / (2.0 * a.a2);
        const C p2 = (-a.a1 - disc) / (2.0 * a.a2);
        const C z1 = std::exp (p1 * T), z2 = std::exp (p2 * T);
        d.a1 = -(z1 + z2).real();
        d.a2 = (z1 * z2).real();

        if (a.b0 == 0.0 && a.b1 == 0.0 && a.b2 != 0.0)
        {
            // A pure s^2 numerator is an analogue double zero at DC. Keep it exactly at z = 1, so the far skirt keeps
            // 12 dB/oct, and set the level at the pole frequency.
            double poleFreq = std::sqrt (std::abs (a.a0 / a.a2));
            const double wp = std::clamp (poleFreq * T, 1.0e-4, 0.9 * pi);
            const C zi = std::polar (1.0, -wp);
            const double digitalShape = std::abs ((1.0 - zi) * (1.0 - zi) / (1.0 + d.a1 * zi + d.a2 * zi * zi));
            const double k = digitalShape > 0.0 ? std::sqrt (analogMag2 (wp * sampleRate)) / digitalShape : 0.0;
            d.b0 = k; d.b1 = -2.0 * k; d.b2 = k;
            return d;
        }
        const double A0 = (1.0 + d.a1 + d.a2) * (1.0 + d.a1 + d.a2);
        const double A1 = (1.0 - d.a1 + d.a2) * (1.0 - d.a1 + d.a2);
        const double A2 = -4.0 * d.a2;
        double matchFreq = std::sqrt (std::abs (a.a0 / a.a2)); // pole frequency, rad/s
        const bool axisZeros = a.b2 != 0.0 && a.b0 > 0.0 && std::abs (a.b1) < 1.0e-6 * std::sqrt (a.b0 * a.b2);
        if (axisZeros)
            matchFreq = std::sqrt (a.b0 / a.b2); // keep the notch where the analogue one is
        const double wm = std::clamp (matchFreq * T, 1.0e-4, 0.9 * pi);
        const double phi1 = std::sin (wm / 2.0) * std::sin (wm / 2.0);
        const double phi0 = 1.0 - phi1;
        const double phi2 = 4.0 * phi0 * phi1;
        const double B0 = A0 * analogMag2 (0.0);
        const double B1 = A1 * analogMag2 (nyquist);
        const double B2 = ((A0 * phi0 + A1 * phi1 + A2 * phi2) * analogMag2 (wm * sampleRate) - B0 * phi0 - B1 * phi1) / phi2;
        const double r0 = std::sqrt (std::max (0.0, B0)), r1 = std::sqrt (std::max (0.0, B1));
        const double W = 0.5 * (r0 + r1);
        const double root = std::sqrt (std::max (0.0, W * W + B2));
        d.b0 = 0.5 * (W + root);
        d.b1 = 0.5 * (r0 - r1);
        d.b2 = d.b0 != 0.0 ? -B2 / (4.0 * d.b0) : 0.0;
        return d;
    }

    /** Digitises a cascade. The overall gain goes into the first section. Returns the section count. */
    inline int digitise (const AnalogModel::AnalogCascade& cascade, double sampleRate, Biquad* out, int maxSections)
    {
        const int count = std::min (cascade.count, maxSections);
        for (int i = 0; i < count; ++i)
            out[i] = matchSection (cascade.sections[(size_t) i], sampleRate);
        if (count > 0)
        {
            out[0].b0 *= cascade.gain;
            out[0].b1 *= cascade.gain;
            out[0].b2 *= cascade.gain;
        }
        return count;
    }

    /** Full design: digital second-order sections. Returns 0 when the band does nothing (unity). */
    inline int design (Shape shape, double sampleRate, double f0, double gainDb, double q, double slopeNorm, Biquad* out, int maxSections)
    {
        AnalogModel::AnalogCascade cascade;
        if (! designAnalog (shape, f0, gainDb, q, slopeNorm, cascade))
            return 0;
        return digitise (cascade, sampleRate, out, maxSections);
    }
}
