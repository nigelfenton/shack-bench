// trap_analysis_test — parallel-LC trap characterisation from an S21 sweep.
//
// No instrument needed. Traps are synthesised from KNOWN L and C, so the
// recovered f0 and Q can be checked against closed-form truth rather than
// against the code's own output.

#include "TrapAnalysis.h"

#include <QtGlobal>
#include <cmath>
#include <complex>
#include <cstdio>

using namespace TciMon;

static constexpr double kPi = 3.14159265358979323846;
static int failures = 0;

static void check(bool cond, const char* what)
{
    std::printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", what);
    if (!cond) ++failures;
}

static void near(double got, double want, double tol, const char* what)
{
    const bool ok = std::abs(got - want) <= tol;
    std::printf("%s %s (got %.4f, want %.4f +/- %.4f)\n",
                ok ? "[ OK ]" : "[FAIL]", what, got, want, tol);
    if (!ok) ++failures;
}

// A parallel LC with series coil resistance, measured SERIES-THROUGH in a Z0
// fixture: the trap sits IN LINE between the ports, so a high impedance at
// resonance blocks the path.
//     S21 = Z0 / (Z0 + Ztrap)
// ⚠ NOT the shunt form 2*Zsh/(2*Zsh+Z0) — that is a trap across the line to
// ground, which produces a flat -0.01 dB trace with no notch at all. Getting
// this backwards synthesised a trap with no resonance in it.
static QVector<TrapPoint> synth(double lUh, double cPf, double coilOhms,
                                double f1, double f2, int n, double z0 = 50.0)
{
    QVector<TrapPoint> out;
    const double L = lUh * 1e-6, C = cPf * 1e-12;
    for (int i = 0; i < n; ++i) {
        const double f = f1 + (f2 - f1) * i / double(n - 1);
        const double w = 2.0 * kPi * f * 1e6;
        // Parallel combination of (R + jwL) and 1/(jwC).
        const std::complex<double> zl(coilOhms, w * L);
        const std::complex<double> zc(0.0, -1.0 / (w * C));
        const std::complex<double> zsh = (zl * zc) / (zl + zc);
        const std::complex<double> s21 = z0 / (z0 + zsh);
        TrapPoint p;
        p.mhz = f;
        p.s21Db = 20.0 * std::log10(std::abs(s21));
        out << p;
    }
    return out;
}

int main()
{
    std::printf("=== a known 14.1 MHz trap ===\n");
    // L = 2.5 uH, C = 51 pF -> f0 = 1/(2*pi*sqrt(LC)) = 14.09 MHz
    const double L = 2.5, C = 51.0;
    const double f0 = 1.0 / (2.0 * kPi * std::sqrt(L * 1e-6 * C * 1e-12)) / 1e6;
    std::printf("       synthesised f0 = %.3f MHz\n", f0);
    TrapResult r = analyseTrap(synth(L, C, 1.0, 12.0, 16.0, 401), 50.0, true);
    check(r.ok, "the sweep analyses");
    near(r.f0Mhz, f0, 0.05, "recovers the resonant frequency");
    check(r.depthDb > 20.0, "a low-loss trap gives a deep notch");
    check(r.loadedQ > 0, "loaded Q is measured");
    std::printf("       depth %.1f dB, loaded Q %.0f, L %.2f uH, C %.1f pF\n",
                r.depthDb, r.loadedQ, r.inductanceUh, r.capacitancePf);

    std::printf("\n=== a lossier coil gives a lower Q ===\n");
    TrapResult good = analyseTrap(synth(L, C, 0.5, 12.0, 16.0, 401), 50.0, true);
    TrapResult poor = analyseTrap(synth(L, C, 8.0, 12.0, 16.0, 401), 50.0, true);
    check(good.ok && poor.ok, "both analyse");
    check(poor.loadedQ < good.loadedQ,
          "more coil resistance measures as LOWER Q");
    check(poor.depthDb < good.depthDb,
          "and as a SHALLOWER notch");
    std::printf("       0.5 ohm: Q %.0f depth %.1f dB | 8 ohm: Q %.0f depth %.1f dB\n",
                good.loadedQ, good.depthDb, poor.loadedQ, poor.depthDb);

    std::printf("\n=== f0 tracks L and C, as physics requires ===\n");
    // Doubling C must drop f0 by sqrt(2).
    TrapResult dbl = analyseTrap(synth(L, C * 2.0, 1.0, 8.0, 14.0, 401), 50.0, true);
    check(dbl.ok, "the doubled-C trap analyses");
    near(dbl.f0Mhz, f0 / std::sqrt(2.0), 0.08,
         "doubling C drops f0 by sqrt(2)");

    std::printf("\n=== a resonance OUTSIDE the span is refused, not reported ===\n");
    // Sweep 20-30 MHz for a 14 MHz trap: the minimum will sit at the low edge.
    TrapResult off = analyseTrap(synth(L, C, 1.0, 20.0, 30.0, 201), 50.0, true);
    check(!off.ok, "an edge minimum is NOT reported as f0");
    check(off.error.contains("outside") || off.error.contains("edge"),
          "and the error says to widen the span");
    std::printf("       %s\n", off.error.toLocal8Bit().constData());

    std::printf("\n=== an empty through path is refused ===\n");
    // No trap fitted. A perfectly flat line makes index 0 the "minimum", which
    // trips the edge guard first, so dither it very slightly to put the lowest
    // point mid-span — that isolates the no-notch check itself.
    QVector<TrapPoint> flat;
    for (int i = 0; i < 201; ++i) {
        TrapPoint p;
        p.mhz = 12.0 + 4.0 * i / 200.0;
        p.s21Db = -0.1 - (i == 100 ? 0.05 : 0.0);
        flat << p;
    }
    TrapResult none = analyseTrap(flat, 50.0, true);
    check(!none.ok, "a flat trace is not a trap");
    check(none.error.contains("notch"),
          "and says there is no usable notch, naming the depth");
    std::printf("       %s\n", none.error.toLocal8Bit().constData());

    std::printf("\n=== an uncalibrated sweep is FLAGGED, not silently trusted ===\n");
    TrapResult uncal = analyseTrap(synth(L, C, 1.0, 12.0, 16.0, 401), 50.0, false);
    check(uncal.ok, "it still analyses");
    check(!uncal.thruCalibrated, "the flag is carried");
    check(uncal.caution.contains("uncalibrated"),
          "and the caution explains what is and is not reliable");
    near(uncal.f0Mhz, r.f0Mhz, 1e-9,
         "f0 is identical either way — it is a frequency, not a level");

    std::printf("\n=== too few points is refused ===\n");
    TrapResult tiny = analyseTrap(QVector<TrapPoint>(), 50.0, true);
    check(!tiny.ok && !tiny.error.isEmpty(), "an empty sweep fails clearly");

    std::printf("\n%d check(s) failed.\n", failures);
    return failures ? 1 : 0;
}
