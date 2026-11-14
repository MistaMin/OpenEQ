#pragma once
#include <algorithm>
#include <cmath>
#include "AnalogModel.h"
#include "FilterDesign.h"

/** Vintage types with closed-form analogue models, represented by compact analogue sections. Each design is a few s-domain sections; FilterDesign::digitise turns them into
    digital biquads with the same Nyquist-matched transform as the generic types. Slope and Q are ignored unless stated.

    - Blue (boost shelf): boost-only bands that add a band-pass to the dry signal. Blue Low follows the frequency up to
      1978 Hz; Blue High is the Air band, a first-order shelf at 0.798 x max(frequency, 1 kHz).
    - G HP, E LP, G LP, VT LP: second-order filters with a fixed frequency ratio and Q.
    - BritN HP (British vintage): a second-order high pass times a first-order shelf, so the far slope is 12 dB/oct and the knee
      is steeper.
    - VT LS (P-type low section): the passive boost/attenuate network, a second-order circuit. Gain sets the boost
      resistor (the boost at DC equals the gain, up to 24 dB; cuts do nothing), slope sets the attenuation resistor,
      and the frequency scales both capacitors. Fitted across 20 Hz to 1 kHz to 0.03 dB RMS.
    - Tone Low / Tone High (Baxandall): the passive Baxandall bass and treble network, fourth order. Gain moves the
      bass or treble pot on a fitted taper (up to 18 dB either way) and the frequency scales the capacitors. Fitted to
      about 0.04 dB RMS below 5 kHz. */
namespace VintageFilters
{
    enum class Type { BlueLow, BlueHigh, GHighPass, ELowPass, GLowPass, VtLowPass, BritNHighPass, VtLowShelf, ToneLow, ToneHigh };

    namespace detail
    {
        constexpr double pi = 3.14159265358979323846;

        /** Linear interpolation in a table of (x, y) rows, clamped at the ends. */
        inline double interpolate (const double (*table)[2], int rows, double x)
        {
            if (x <= table[0][0]) return table[0][1];
            for (int i = 1; i < rows; ++i)
                if (x <= table[i][0])
                {
                    const double t = (x - table[i - 1][0]) / (table[i][0] - table[i - 1][0]);
                    return table[i - 1][1] + (table[i][1] - table[i - 1][1]) * t;
                }
            return table[rows - 1][1];
        }

        inline AnalogModel::AnalogSection secondOrderHighPass (double omega, double q)
        {
            AnalogModel::AnalogSection s;
            s.b2 = 1.0; s.b1 = 0.0; s.b0 = 0.0;
            s.a2 = 1.0; s.a1 = omega / q; s.a0 = omega * omega;
            return s;
        }

        inline AnalogModel::AnalogSection secondOrderLowPass (double omega, double q)
        {
            AnalogModel::AnalogSection s;
            s.b2 = 0.0; s.b1 = 0.0; s.b0 = omega * omega;
            s.a2 = 1.0; s.a1 = omega / q; s.a0 = omega * omega;
            return s;
        }

        /** Ascending polynomials up to degree 4. */
        struct Poly
        {
            double c[5] = {};
            int degree = 0;
        };

        inline Poly poly (double c0, double c1 = 0.0)
        {
            Poly p;
            p.c[0] = c0; p.c[1] = c1; p.degree = 1;
            return p;
        }

        inline Poly add (const Poly& a, const Poly& b, double kb = 1.0)
        {
            Poly r;
            r.degree = std::max (a.degree, b.degree);
            for (int i = 0; i <= r.degree; ++i) r.c[i] = a.c[i] + kb * b.c[i];
            return r;
        }

        inline Poly scale (const Poly& a, double k)
        {
            Poly r = a;
            for (int i = 0; i <= r.degree; ++i) r.c[i] *= k;
            return r;
        }

        inline Poly mul (const Poly& a, const Poly& b)
        {
            Poly r;
            r.degree = std::min (4, a.degree + b.degree);
            for (int i = 0; i <= a.degree; ++i)
                for (int j = 0; j <= b.degree && i + j <= 4; ++j)
                    r.c[i + j] += a.c[i] * b.c[j];
            return r;
        }

        /** P-type low section. Values fitted to the plug-in, with capacitors normalised to x = s / (2 pi f). */
        inline bool passiveTubeLowShelf (double fc, double gainDb, double slopeNorm, AnalogModel::AnalogCascade& out)
        {
            constexpr double k = 2.868180903e-05, rbh = 89759.11354, e = 10809.97829, r2 = 20311.64032, ratio = 37.13187915;
            constexpr double r3 = 1.0e4;
            const double c2 = 2.0 * pi * k, c1 = c2 / ratio;

            // Reference level (no boost or attenuation) and the boost resistor that gives the commanded DC boost.
            const double nB0 = r2 + r3, nP0 = e * nB0, dP0 = e + nB0;
            const double reference = (nP0 / dP0) / (rbh + nP0 / dP0) * r3 / nB0;
            const double target = std::pow (10.0, std::clamp (gainDb, 0.0, 24.0) / 20.0) * reference;
            const double rb = std::max (0.0, (target * (rbh * dP0 + nP0) - e * r3) / (dP0 * (1.0 - target)));

            // Attenuation resistor from the slope control, interpolated in a fitted table (log-spaced above 0.1).
            static const double slopes[] = { 0.0, 0.05, 0.1, 0.15, 0.2, 0.25, 0.3, 0.35, 0.4, 0.45, 0.5, 0.55, 0.6, 0.65,
                                             0.7, 0.75, 0.8, 0.85, 0.9, 0.95, 0.98, 1.0 };
            static const double rcs[] = { 0.0, 0.002754199767, 199.6430356, 2240.68939, 4434.362754, 6862.870713,
                                          9625.484724, 12781.52008, 16438.37839, 20729.14377, 25848.70503, 32020.00134,
                                          39660.59441, 49343.22214, 62013.69234, 79361.85404, 104323.0239, 143714.9279,
                                          214878.0619, 382224.0393, 665214.3958, 1249680.321 };
            const double sn = std::clamp (slopeNorm, 0.0, 1.0);
            double rc = 0.0;
            for (int i = 1; i < (int) std::size (slopes); ++i)
                if (sn <= slopes[i])
                {
                    const double t = (sn - slopes[i - 1]) / (slopes[i] - slopes[i - 1]);
                    rc = slopes[i - 1] >= 0.1 ? rcs[i - 1] * std::pow (rcs[i] / rcs[i - 1], t)
                                              : rcs[i - 1] + (rcs[i] - rcs[i - 1]) * t;
                    break;
                }

            const Poly nH = poly (rc, 0.0), dH = poly (1.0, rc * c1);
            const Poly nJ = poly (rb, 0.0), dJ = poly (1.0, rb * c2);
            const Poly nB = add (nH, dH, r2 + r3);
            const Poly nP = scale (nB, e);
            const Poly dP = add (scale (dH, e), nB);
            const Poly num = scale (add (mul (nJ, dP), mul (dJ, dH), e * r3), 1.0 / reference);
            const Poly den = add (add (scale (mul (dP, dJ), rbh), mul (nP, dJ)), mul (nJ, dP));
            return FilterDesign::cascadeFromRational (num.c, num.degree, den.c, den.degree, 2.0 * pi * fc, out);
        }

        /** Passive Baxandall bass and treble network. Values fitted to the plug-in, capacitors normalised to x = s / (2 pi f). */
        inline bool baxandall (bool bass, double fc, double gainDb, AnalogModel::AnalogCascade& out)
        {
            struct Network { double r2, r3, pb, c1, c2, c3, c4, pt, r4, fixed, offsetDb; double taper[6]; };
            static const Network low { 0.06363503222, 3.049295449e-06, 5.851861887, 0.0001278414474, 0.001289134074,
                                       3.010115087e-05, 0.0003084422416, 1.841473552, 6.298187018, 0.077251663, 21.56891902,
                                       { 6.08447711e-07, 5.35626624e-06, 4.30954335e-06, 2.36631296e-04, 1.57704898e-01, -2.23716026e+00 } };
            static const Network high { 0.8004385911, 214.7829779, 0.02520901593, 0.0028198178, 0.002304485375,
                                        1.123506018e-05, 0.0001100221695, 117.3882375, 46.45535317, 0.4304696906, 22.05814639,
                                        { -1.16778970e-07, -1.33443465e-06, 1.52537899e-04, 2.82818553e-03, 1.52027060e-01, -2.45461350e+00 } };
            const Network& n = bass ? low : high;
            const double g = std::clamp (gainDb, -18.0, 18.0);
            double logit = 0.0;
            for (double t : n.taper) logit = logit * g + t;
            const double pot = 1.0 / (1.0 + std::exp (-logit));
            const double x = bass ? pot : n.fixed, y = bass ? n.fixed : pot;

            const double w = 2.0 * pi * 100.0; // the fitted capacitor values are for 100 Hz
            const double c1 = n.c1 * w, c2 = n.c2 * w, c3 = n.c3 * w, c4 = n.c4 * w, r1 = 1.0;
            const double ra = std::max (n.pb * (1.0 - x), 1.0e-9), rb = std::max (n.pb * x, 1.0e-9);
            const double rc = std::max (n.pt * (1.0 - y), 1.0e-9), rd = std::max (n.pt * y, 1.0e-9);
            // Branch admittances: bass input arm, bass ground arm, treble input arm, treble ground arm.
            const Poly n1 = poly (1.0, c1 * ra), d1 = poly (r1 + ra, r1 * c1 * ra);
            const Poly n2 = poly (1.0, c2 * rb), d2 = poly (n.r2 + rb, n.r2 * c2 * rb);
            const Poly n3 = poly (0.0, c3), d3 = poly (1.0, c3 * rc);
            const Poly n4 = poly (0.0, c4), d4 = poly (1.0, c4 * rd);
            const double g3 = 1.0 / n.r3, g4 = 1.0 / n.r4;
            const Poly p = add (add (mul (n1, d2), mul (n2, d1)), mul (d1, d2), g3);
            const Poly q = add (add (scale (mul (d3, d4), g3 + g4), mul (n3, d4)), mul (n4, d3));
            const Poly d1234 = mul (mul (d1, d2), mul (d3, d4));
            const Poly num = scale (add (mul (mul (p, n3), d4), mul (mul (n1, d2), mul (d3, d4)), g3),
                                    std::pow (10.0, n.offsetDb / 20.0));
            const Poly den = add (mul (p, q), d1234, -g3 * g3);
            return FilterDesign::cascadeFromRational (num.c, num.degree, den.c, den.degree, 2.0 * pi * fc, out);
        }
    }

    /** Builds the analogue cascade for a vintage type. Returns false when the band does nothing. */
    inline bool designAnalog (Type type, double fc, double gainDb, double q, AnalogModel::AnalogCascade& out,
                              double slopeNorm = 0.0)
    {
        using namespace detail;
        out.count = 0;
        out.gain = 1.0;
        const double wc = 2.0 * pi * fc;
        switch (type)
        {
            case Type::BlueLow:
            {
                // H = 1 + k (s/Qe) / (s^2 + s/Qe + 1), at f0 = min(fc, 1978 Hz). Boost only: g' in 0.43..11.16 dB.
                static const double qTable[][2] = { { std::log (0.3), 0.411 }, { std::log (0.5), 0.437 }, { std::log (0.7), 0.456 },
                                                    { std::log (1.0), 0.478 }, { std::log (1.4), 0.512 }, { std::log (2.0), 0.555 },
                                                    { std::log (3.0), 0.614 }, { std::log (5.0), 0.708 }, { std::log (10.0), 0.894 },
                                                    { std::log (20.0), 1.215 } };
                const double qe = interpolate (qTable, 10, std::log (std::max (0.025, q)));
                const double gain = std::clamp (gainDb, 0.43, 11.16);
                const double k = std::pow (10.0, gain / 20.0) - 1.0;
                const double w0 = 2.0 * pi * std::min (fc, 1978.0);
                AnalogModel::AnalogSection s;
                s.b2 = 1.0; s.b1 = (1.0 + k) * w0 / qe; s.b0 = w0 * w0;
                s.a2 = 1.0; s.a1 = w0 / qe;             s.a0 = w0 * w0;
                out.sections[0] = s;
                out.count = 1;
                return true;
            }
            case Type::BlueHigh:
            {
                // Air band: H = 1 + k s / (s + w0), w0 = 2 pi 0.798 max(fc, 1 kHz). k from gain (0..16 dB) by table.
                static const double kTable[][2] = { { 0.0, 0.0756 }, { 1.0, 0.1562 }, { 2.0, 0.328 }, { 3.0, 0.5087 }, { 4.0, 0.6972 },
                                                    { 5.0, 0.8982 }, { 6.0, 1.1103 }, { 8.0, 1.577 }, { 10.0, 2.083 }, { 12.0, 2.643 },
                                                    { 14.0, 3.241 }, { 16.0, 3.86 } };
                const double k = interpolate (kTable, 12, std::clamp (gainDb, 0.0, 16.0));
                const double w0 = 2.0 * pi * 0.798 * std::max (fc, 1000.0);
                AnalogModel::AnalogSection s;
                s.b2 = 0.0; s.b1 = 1.0 + k; s.b0 = w0;
                s.a2 = 0.0; s.a1 = 1.0;     s.a0 = w0;
                out.sections[0] = s;
                out.count = 1;
                return true;
            }
            case Type::GHighPass:
                out.sections[0] = secondOrderHighPass (1.16371 * wc, 0.77346);
                out.count = 1;
                return true;
            case Type::ELowPass:
            case Type::GLowPass:
            case Type::VtLowPass:
            {
                const double w = type == Type::ELowPass ? 1.06966 : (type == Type::GLowPass ? 1.04863 : 1.40237);
                const double qq = type == Type::ELowPass ? 0.71775 : (type == Type::GLowPass ? 0.71774 : 0.53776);
                out.sections[0] = secondOrderLowPass (w * wc, qq);
                out.count = 1;
                return true;
            }
            case Type::BritNHighPass:
            {
                out.sections[0] = secondOrderHighPass (1.01993 * wc, 0.93633);
                AnalogModel::AnalogSection shelf;
                shelf.b2 = 0.0; shelf.b1 = 1.0; shelf.b0 = 0.39788 * wc;
                shelf.a2 = 0.0; shelf.a1 = 1.0; shelf.a0 = 0.96022 * wc;
                out.sections[1] = shelf;
                out.count = 2;
                return true;
            }
            case Type::VtLowShelf:
                return passiveTubeLowShelf (fc, gainDb, slopeNorm, out);
            case Type::ToneLow:
                return baxandall (true, fc, gainDb, out);
            case Type::ToneHigh:
                return baxandall (false, fc, gainDb, out);
        }
        return false;
    }
}
