// trap_analysis_test — parallel-LC trap characterisation from an S21 sweep.
//
// No instrument needed. Traps are synthesised from KNOWN L, C and coil
// resistance with an independent complex-impedance model, and EVERY recovered
// quantity — f0, Q, L and C — is asserted against closed-form truth:
//     f0 = 1/(2*pi*sqrt(LC))      Q = w0*L/R      L, C as built
// ⚠ An earlier version synthesised with the same (wrong) Z0/(Z0+Z) formula as
// the analyser and only printed L and C, so a 20x error in both passed. Do not
// let the synthesis borrow anything from TrapAnalysis.cpp.

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
// resonance blocks the path. A series element between two Z0 ports sees the
// source AND load impedance, hence
//     S21 = 2*Z0 / (2*Z0 + Ztrap)
// `offsetDb` adds a flat through loss, standing in for an uncalibrated fixture.
// ⚠ NOT the shunt form 2*Zsh/(2*Zsh+Z0) — that is a trap across the line to
// ground, which produces a flat -0.01 dB trace with no notch at all. Getting
// this backwards synthesised a trap with no resonance in it.
static QVector<TrapPoint> synth(double lUh, double cPf, double coilOhms,
                                double f1, double f2, int n,
                                double offsetDb = 0.0, double z0 = 50.0)
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
        const std::complex<double> s21 = 2.0 * z0 / (2.0 * z0 + zsh);
        TrapPoint p;
        p.mhz = f;
        p.s21Db = 20.0 * std::log10(std::abs(s21)) + offsetDb;
        out << p;
    }
    return out;
}

// Relative-tolerance check: |got - want| <= frac * want.
static void within(double got, double want, double frac, const char* what)
{
    near(got, want, std::abs(want) * frac, what);
}

int main()
{
    // L = 2.5 uH, C = 51 pF, 1 ohm of coil resistance:
    //   f0 = 14.095 MHz, Q = w0*L/R = 221, Rp ~ 49 kohm, notch ~53.8 dB.
    const double L = 2.5, C = 51.0;
    const double f0 = 1.0 / (2.0 * kPi * std::sqrt(L * 1e-6 * C * 1e-12)) / 1e6;
    const auto trueQ = [&](double coilOhms) {
        return 2.0 * kPi * f0 * 1e6 * L * 1e-6 / coilOhms;
    };
    const double rpTrue = std::pow(trueQ(1.0), 2) * 1.0;     // Q^2 * R
    const double depthTrue = 20.0 * std::log10((100.0 + rpTrue) / 100.0);

    std::printf("=== a known 14.1 MHz trap, narrow sweep ===\n");
    std::printf("       truth: f0 %.4f MHz, Q %.1f, Rp %.0f ohm, depth %.2f dB\n",
                f0, trueQ(1.0), rpTrue, depthTrue);
    TrapResult r = analyseTrap(synth(L, C, 1.0, 12.0, 16.0, 401), 50.0, true);
    std::printf("       got:   f0 %.4f MHz, Q %.1f, Rp %.0f ohm, depth %.2f dB, "
                "L %.3f uH, C %.2f pF\n", r.f0Mhz, r.trapQ, r.rpOhms,
                r.depthDb, r.inductanceUh, r.capacitancePf);
    check(r.ok, "the sweep analyses");
    within(r.f0Mhz, f0, 0.002, "f0 within 0.2%");
    // The depth is from 0 dB, so it must match the closed form — a reference
    // taken from the skirt read this as ~32 dB.
    near(r.depthDb, depthTrue, 0.5, "notch depth matches 20log((2Z0+Rp)/2Z0)");
    within(r.rpOhms, rpTrue, 0.10, "Rp within 10% (2*Z0 series formula)");
    within(r.trapQ, trueQ(1.0), 0.15, "Q within 15% of the trap's own Q");
    within(r.inductanceUh, L, 0.10, "L within 10%");
    within(r.capacitancePf, C, 0.10, "C within 10%");
    check(!r.undersampled, "6+ points across the notch is NOT undersampled");
    check(!r.lcFromSkirt, "and L/C come from Rp and Q");
    check(r.caution.isEmpty(), "a clean calibrated sweep carries no caution");

    std::printf("\n=== a lossier coil gives a lower Q ===\n");
    TrapResult good = analyseTrap(synth(L, C, 0.5, 12.0, 16.0, 401), 50.0, true);
    TrapResult poor = analyseTrap(synth(L, C, 8.0, 12.0, 16.0, 401), 50.0, true);
    check(good.ok && poor.ok, "both analyse");
    check(poor.trapQ < good.trapQ,
          "more coil resistance measures as LOWER Q");
    check(poor.depthDb < good.depthDb,
          "and as a SHALLOWER notch");
    std::printf("       0.5 ohm: Q %.0f depth %.1f dB | 8 ohm: Q %.1f depth %.1f dB\n",
                good.trapQ, good.depthDb, poor.trapQ, poor.depthDb);
    within(good.trapQ, trueQ(0.5), 0.15, "0.5 ohm coil: Q within 15%");
    within(poor.trapQ, trueQ(8.0), 0.15, "8 ohm coil: Q within 15%");
    within(poor.inductanceUh, L, 0.10, "8 ohm coil: L within 10%");
    within(poor.capacitancePf, C, 0.10, "8 ohm coil: C within 10%");

    std::printf("\n=== the guide's 3-30 MHz first sweep: notch undersampled ===\n");
    // 401 points over 27 MHz is a 67.5 kHz step against a 64 kHz bandwidth.
    // f0 can only be as good as half a step; Q is legitimately wrong and must
    // be FLAGGED; L and C must still come out right, from the skirt.
    TrapResult wide = analyseTrap(synth(L, C, 1.0, 3.0, 30.0, 401), 50.0, true);
    std::printf("       got: f0 %.4f MHz, Q %.1f, L %.3f uH, C %.2f pF\n",
                wide.f0Mhz, wide.trapQ, wide.inductanceUh, wide.capacitancePf);
    std::printf("       %s\n", wide.caution.toLocal8Bit().constData());
    check(wide.ok, "the wide sweep analyses");
    near(wide.f0Mhz, f0, 27.0 / 400.0 / 2.0, "f0 within half a sweep step");
    check(wide.undersampled, "the notch is flagged as undersampled");
    check(wide.caution.contains("undersampled") &&
              wide.caution.contains("Narrow the sweep"),
          "and the caution says to narrow the sweep");
    check(wide.lcFromSkirt, "L/C fall back to the skirt");
    within(wide.inductanceUh, L, 0.10, "L within 10% from a 3-30 MHz sweep");
    within(wide.capacitancePf, C, 0.10, "C within 10% from a 3-30 MHz sweep");

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

    std::printf("\n=== a lossy flat through path is not a notch either ===\n");
    // Uncalibrated, with 5 dB of cable loss and no trap: 5 dB below 0 dB but
    // nothing below the rest of the trace. Must not pass as a notch.
    for (auto& p : flat) p.s21Db -= 5.0;
    TrapResult lossy = analyseTrap(flat, 50.0, false);
    check(!lossy.ok && lossy.error.contains("notch"),
          "a flat -5 dB trace is refused");

    std::printf("\n=== an uncalibrated sweep is FLAGGED, not silently trusted ===\n");
    // 0.3 dB of uncorrected fixture loss: f0 and Q unaffected, L and C off by
    // a few percent — still inside tolerance, and the caution says why.
    TrapResult uncal =
        analyseTrap(synth(L, C, 1.0, 12.0, 16.0, 401, -0.3), 50.0, false);
    std::printf("       got: f0 %.4f MHz, Q %.1f, L %.3f uH, C %.2f pF\n",
                uncal.f0Mhz, uncal.trapQ, uncal.inductanceUh,
                uncal.capacitancePf);
    check(uncal.ok, "it still analyses");
    check(!uncal.thruCalibrated, "the flag is carried");
    check(uncal.caution.contains("uncalibrated"),
          "and the caution explains what is and is not reliable");
    near(uncal.f0Mhz, r.f0Mhz, 1e-9,
         "f0 is identical either way — it is a frequency, not a level");
    within(uncal.trapQ, trueQ(1.0), 0.15, "Q within 15% despite the offset");
    check(uncal.inductanceUh > r.inductanceUh,
          "through loss counted as trap loss reads L HIGH, as documented");
    within(uncal.inductanceUh, L, 0.10, "L still within 10%");
    within(uncal.capacitancePf, C, 0.10, "C still within 10%");

    std::printf("\n=== too few points is refused ===\n");
    TrapResult tiny = analyseTrap(QVector<TrapPoint>(), 50.0, true);
    check(!tiny.ok && !tiny.error.isEmpty(), "an empty sweep fails clearly");

    std::printf("\n%d check(s) failed.\n", failures);
    return failures ? 1 : 0;
}
