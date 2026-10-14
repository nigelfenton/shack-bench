// cable_tests_test — velocity factor and common-mode choke analysis.
//
// Both are checked against synthetic data built from KNOWN values, so the
// recovered numbers are compared with closed-form truth rather than with the
// code's own output. The VF case uses Nigel's real jumper: 97 inches of
// RG-8/U, PL-259 to PL-259, shoulder to shoulder.

#include "CableTests.h"

#include <QtGlobal>
#include <cmath>
#include <complex>
#include <cstdio>

static constexpr double kPiLocal = 3.14159265358979323846;

using namespace TciMon;

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

// A shorted line of known length and VF: X = Z0 * tan(beta * l), which crosses
// zero every quarter wavelength (alternating series and parallel resonance).
static QVector<QPair<double, double>> shortedLine(double metres, double vf,
                                                  double f1, double f2, int n)
{
    QVector<QPair<double, double>> out;
    const double c = 299.792458;    // metres * MHz
    for (int i = 0; i < n; ++i) {
        const double f = f1 + (f2 - f1) * i / double(n - 1);
        const double beta = 2.0 * kPiLocal * f / (c * vf);
        out << qMakePair(f, 50.0 * std::tan(beta * metres));
    }
    return out;
}

int main()
{
    std::printf("=== velocity factor: Nigel's 97in RG-8/U jumper ===\n");
    const double inches = 97.0;
    const double metres = inches * 0.0254;
    std::printf("       %.0f in = %.4f m, synthesised at VF 0.66\n",
                inches, metres);
    VfResult v = measureVelocityFactor(
        shortedLine(metres, 0.66, 1.0, 300.0, 2001), metres);
    check(v.ok, "the sweep analyses");
    near(v.velocityFactor, 0.66, 0.02, "recovers VF 0.66 for solid PE");
    std::printf("       %lld crossings, mean spacing %.3f MHz, electrical %.3f m\n",
                (long long)v.crossingsMhz.size(), v.meanSpacingMhz,
                v.electricalMetres);
    std::printf("       nearest catalogue match: %s (VF %.2f)\n",
                v.nearestCable.toLocal8Bit().constData(), v.nearestVf);

    std::printf("\n=== the same cable in foam reads a DIFFERENT VF ===\n");
    VfResult f = measureVelocityFactor(
        shortedLine(metres, 0.85, 1.0, 300.0, 2001), metres);
    check(f.ok, "the foam case analyses");
    near(f.velocityFactor, 0.85, 0.02, "recovers VF 0.85 for foam");
    check(f.electricalMetres < v.electricalMetres,
          "a faster cable is electrically SHORTER for the same physical length");

    std::printf("\n=== a wrong physical length gives a wrong VF, proportionally ===\n");
    // This is the whole point of the test: VF is only as good as the length.
    VfResult w = measureVelocityFactor(
        shortedLine(metres, 0.66, 1.0, 300.0, 2001), metres * 1.10);
    check(w.ok, "it still analyses");
    near(w.velocityFactor, 0.66 * 1.10, 0.02,
         "a 10% length error becomes a 10% VF error");

    std::printf("\n=== refusals ===\n");
    VfResult noLen = measureVelocityFactor(
        shortedLine(metres, 0.66, 1.0, 300.0, 501), 0.0);
    check(!noLen.ok && noLen.error.contains("length"),
          "no length entered is refused, saying why");

    // A narrow sweep on a short cable sees at most one crossing.
    VfResult narrow = measureVelocityFactor(
        shortedLine(metres, 0.66, 3.0, 30.0, 201), metres);
    check(!narrow.ok, "a too-narrow sweep is refused");
    check(narrow.error.contains("WIDE") || narrow.error.contains("resonance"),
          "and says to widen the span");
    std::printf("       %s\n", narrow.error.toLocal8Bit().constData());

    VfResult daft = measureVelocityFactor(
        shortedLine(metres, 0.66, 1.0, 300.0, 2001), metres * 0.3);
    check(!daft.ok && daft.error.contains("sensible"),
          "a physically impossible VF is refused, not reported");

    std::printf("\n=== the dead-data trap: exact zeros are NOT measurements ===\n");
    // ⭐ Measured on the real NanoVNA on 2026-08-03: outside its stored
    // calibration it returns R 50.000, X 0.000, |gamma| 0.0000 to four
    // decimals for EVERY point, and the last real sample was 29.9 MHz however
    // wide the sweep was requested. Those exact zeros fabricate crossings, so
    // a 1-300 MHz sweep of a real cable is live to 30 MHz and dead after.
    QVector<QPair<double, double>> dead =
        shortedLine(metres, 0.66, 1.0, 30.0, 40);
    for (int i = 0; i < 161; ++i)
        dead << qMakePair(30.0 + 270.0 * i / 160.0, 0.0);
    VfResult dz = measureVelocityFactor(dead, metres);
    check(!dz.ok, "a sweep that is mostly exact zeros is REFUSED");
    check(dz.error.contains("NO DATA") || dz.error.contains("calibrat"),
          "and the error names the cause: outside the calibrated range");
    std::printf("       %s\n", dz.error.toLocal8Bit().constData());

    std::printf("\n=== a short cable in a narrow window fails helpfully ===\n");
    VfResult few = measureVelocityFactor(
        shortedLine(metres, 0.66, 3.0, 30.0, 201), metres);
    check(!few.ok, "97 in over 3-30 MHz gives too few crossings");
    check(few.error.contains("LONGER") || few.error.contains("calibrate"),
          "and names BOTH remedies: calibrate higher, or use a longer offcut");
    std::printf("       %s\n", few.error.toLocal8Bit().constData());

    std::printf("\n=== a longer offcut works in the SAME calibrated window ===\n");
    const double longM = 400.0 * 0.0254;
    VfResult lng = measureVelocityFactor(
        shortedLine(longM, 0.66, 3.0, 30.0, 401), longM);
    check(lng.ok, "400 in analyses inside 3-30 MHz");
    near(lng.velocityFactor, 0.66, 0.02, "and recovers VF 0.66");
    std::printf("       %lld crossings from the longer piece\n",
                (long long)lng.crossingsMhz.size());

    std::printf("\n=== choke: a good HF choke (5 kohm across HF) ===\n");
    QVector<QPair<double, double>> good;
    for (int i = 0; i < 200; ++i) {
        const double mhz = 1.0 + 49.0 * i / 199.0;
        const double z = 5000.0;          // flat, idealised
        const double s21 = 2.0 * 50.0 / (2.0 * 50.0 + z);
        good << qMakePair(mhz, 20.0 * std::log10(s21));
    }
    ChokeResult cg = analyseChoke(good, 50.0, 1000.0);
    check(cg.ok, "the good choke analyses");
    near(cg.peakOhms, 5000.0, 60.0, "recovers ~5 kohm from S21");
    check(cg.bandsAboveThreshold == cg.bandsTested && cg.bandsTested > 5,
          "passes on every band in the sweep");
    std::printf("       %s\n", cg.verdict.toLocal8Bit().constData());

    std::printf("\n=== choke: the 6in coil that was removed (a few hundred ohms) ===\n");
    // 4-5 turns at 6in is roughly 2.5 uH: Z = 2*pi*f*L, which is small on HF.
    QVector<QPair<double, double>> weak;
    for (int i = 0; i < 200; ++i) {
        const double mhz = 1.0 + 49.0 * i / 199.0;
        const double z = 2.0 * kPiLocal * mhz * 1e6 * 2.5e-6;
        const double s21 = 2.0 * 50.0 / (2.0 * 50.0 + z);
        weak << qMakePair(mhz, 20.0 * std::log10(s21));
    }
    ChokeResult cw = analyseChoke(weak, 50.0, 1000.0);
    check(cw.ok, "the weak choke analyses");
    check(cw.bandsAboveThreshold < cw.bandsTested,
          "it FAILS on the lower bands, as a small air coil should");
    std::printf("       %s\n", cw.verdict.toLocal8Bit().constData());
    for (const auto& b : cw.bands)
        std::printf("         %-5s %8.0f ohm  %s\n",
                    b.band.toLocal8Bit().constData(), b.ohms,
                    b.pass ? "pass" : "FAIL");
    check(!cw.bands.isEmpty() && !cw.bands.first().pass,
          "80m/160m specifically fail — the inductance is too small there");

    std::printf("\n=== choke: impossible data is refused ===\n");
    QVector<QPair<double, double>> hot;
    for (int i = 0; i < 50; ++i) hot << qMakePair(1.0 + i * 0.5, +3.0);
    ChokeResult ch = analyseChoke(hot, 50.0, 1000.0);
    check(!ch.ok && ch.error.contains("passive"),
          "|S21| > 1 on a passive fixture is refused, not turned into a number");

    std::printf("\n=== choke: a REACTIVE 1050 ohm choke needs the phase ===\n");
    // Away from self-resonance a choke is mostly reactance. Build the complex
    // S21 of a pure j1050 ohm series element from the closed form.
    {
        const std::complex<double> z(0.0, 1050.0);
        const std::complex<double> s21 = 100.0 / (100.0 + z);
        QVector<S21Sample> cx;
        QVector<QPair<double, double>> mag;
        for (int i = 0; i < 50; ++i) {
            S21Sample s;
            s.mhz = 3.0 + 27.0 * i / 49.0;
            s.s21 = s21;
            cx << s;
            mag << qMakePair(s.mhz, 20.0 * std::log10(std::abs(s21)));
        }
        const ChokeResult rc = analyseChoke(cx, 50.0, 1000.0);
        near(rc.peakOhms, 1050.0, 1.0, "complex S21 recovers |Z| = 1050 ohm");
        check(rc.bandsAboveThreshold == rc.bandsTested && rc.bandsTested > 0,
              "and it PASSES the 1 kohm threshold, as it should");
        const ChokeResult rm = analyseChoke(mag, 50.0, 1000.0);
        check(rm.peakOhms < 1000.0,
              "the magnitude-only form under-reads it (the old bug, kept "
              "as the fallback's documented limit)");
    }

    std::printf("\n=== choke: dead (exactly zero) data is not a false PASS ===\n");
    {
        // 5 kohm resistive choke, but the outer points read 0 — what the VNA
        // returns outside its calibrated span.
        auto build = [](int deadEachEnd) {
            QVector<S21Sample> v;
            for (int i = 0; i < 100; ++i) {
                S21Sample s;
                s.mhz = 1.0 + 49.0 * i / 99.0;
                const bool dead = i < deadEachEnd || i >= 100 - deadEachEnd;
                s.s21 = dead ? 0.0 : 100.0 / (100.0 + 5000.0);
                v << s;
            }
            return v;
        };
        const ChokeResult few = analyseChoke(build(5), 50.0, 1000.0);
        check(few.ok, "a few dead points are skipped, the rest analysed");
        near(few.peakOhms, 5000.0, 60.0,
             "the peak is the real 5 kohm, not ~1e8 from a zero");
        check(few.caution.contains("exactly zero"), "and the skip is reported");
        const ChokeResult lots = analyseChoke(build(20), 50.0, 1000.0);
        check(!lots.ok && lots.error.contains("calibrated"),
              "a sweep that is 40% dead is refused");
        QVector<QPair<double, double>> sentinel;
        for (int i = 0; i < 40; ++i)
            sentinel << qMakePair(1.0 + i, i < 20 ? -120.0 : -34.2);
        check(!analyseChoke(sentinel, 50.0, 1000.0).ok,
              "the driver's -120 dB sentinel counts as dead in the dB form");
    }

    std::printf("\n%d check(s) failed.\n", failures);
    return failures ? 1 : 0;
}
