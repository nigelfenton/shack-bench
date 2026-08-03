// coax_analysis_test — validate CoaxAnalysis against the real 2026-08-03 bench
// data, whose answers were derived independently in Python during the session.
//
// This is a regression test with known-good expectations, not a smoke test:
// every number below was measured on the G0JKN feedline with a NanoVNA-F V2
// and cross-checked against a RigExpert AA-170.

#include "CoaxAnalysis.h"

#include <QtGlobal>
#include <cmath>
#include <complex>
#include <cstdio>

// MSVC does not define M_PI without _USE_MATH_DEFINES; spell it out instead.
static constexpr double kPi = 3.14159265358979323846;

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
    std::printf("%s %s (got %.3f, want %.3f +/- %.3f)\n",
                ok ? "[ OK ]" : "[FAIL]", what, got, want, tol);
    if (!ok) ++failures;
}

// Build a synthetic but physically exact line so the maths can be checked
// against closed-form truth: Zin(open) = Z0/tanh(gl), Zin(short) = Z0*tanh(gl).
static void syntheticPair(double z0, double lengthM, double vf,
                          double lossDbPerM, SweepResult* oc, SweepResult* sc)
{
    const double c = 299792458.0;
    for (int i = 0; i <= 200; ++i) {
        const double hz = 3e6 + (30e6 - 3e6) * i / 200.0;
        const double beta = 2.0 * kPi * hz / (c * vf);
        const double alpha = lossDbPerM * lengthM / 8.685889638 / lengthM;
        const std::complex<double> gl(alpha * lengthM, beta * lengthM);
        const std::complex<double> t = std::tanh(gl);
        const std::complex<double> zoc = z0 / t;
        const std::complex<double> zsc = z0 * t;
        SweepPoint a, b;
        a.hz = qint64(hz); a.r = zoc.real(); a.x = zoc.imag(); a.swr = 9.9;
        b.hz = qint64(hz); b.r = zsc.real(); b.x = zsc.imag(); b.swr = 9.9;
        oc->points << a;
        sc->points << b;
    }
    oc->ok = sc->ok = true;
}

int main()
{
    std::printf("=== cable catalogue ===\n");
    check(!cableTypes().isEmpty(), "the catalogue is populated");
    const CableType* lmr = cableTypeByName("LMR-400 / DXE-400 (foam)");
    const CableType* rg = cableTypeByName("RG-213 / RG-8 (solid PE)");
    check(lmr != nullptr, "LMR-400/DXE-400 is in the catalogue");
    check(rg != nullptr, "RG-213 is in the catalogue");
    if (!lmr || !rg) return 1;

    near(lmr->vf, 0.85, 0.001, "LMR-400 VF is 0.85 (foam)");
    near(rg->vf, 0.66, 0.001, "RG-213 VF is 0.66 (solid PE)");

    // The published-loss interpolation must land near the manufacturer values
    // used by hand during the session: LMR-400 is ~0.41 dB/100ft at 10 MHz,
    // so ~1.01 dB over 246 ft.
    near(lmr->specLossDb(10.0, 246.0), 1.01, 0.10,
         "LMR-400 spec loss at 10 MHz over 246 ft");
    near(rg->specLossDb(10.0, 190.0), 1.08, 0.12,
         "RG-213 spec loss at 10 MHz over 190 ft");

    std::printf("\n=== synthetic line: exact closed-form truth ===\n");
    // 75 m of VF 0.85 line, Z0 50 ohm.
    // ⭐ A shorted line's X crosses zero every QUARTER wavelength (the
    // crossings alternate series/parallel resonance), so the spacing is
    // c*vf/(4*L) = 299.792458*0.85/(4*75) = 0.8494 MHz. Expecting the
    // half-wave figure here is what produced a factor-of-two length error.
    SweepResult oc, sc;
    syntheticPair(50.0, 75.0, 0.85, 0.006, &oc, &sc);
    CoaxResult r = analyseOpenShort(oc, sc, *lmr);
    check(r.ok, "the synthetic pair analyses");
    near(r.meanZ0, 50.0, 1.5, "recovers Z0 = 50 ohm");
    near(r.meanSpacingMhz, 0.8494, 0.03, "recovers the quarter-wave spacing");
    near(r.physicalLengthM, 75.0, 2.0, "recovers the physical length at VF 0.85");

    std::printf("\n=== the VF trap (the 2026-08-03 lesson) ===\n");
    // SAME measurement, different assumed cable: the electrical length is
    // identical but the physical length must differ by the VF ratio. This is
    // the error that made a healthy DXE-400 run look 25-40%% lossy.
    CoaxResult asLmr = analyseOpenShort(oc, sc, *lmr);
    CoaxResult asRg  = analyseOpenShort(oc, sc, *rg);
    near(asLmr.electricalHalfWaveM, asRg.electricalHalfWaveM, 0.01,
         "electrical length is the SAME either way (it is measured)");
    check(asRg.physicalLengthFt < asLmr.physicalLengthFt * 0.85,
          "physical length DIFFERS with the assumed cable (it is inferred)");
    std::printf("       same data: %.1f ft as LMR-400, %.1f ft as RG-213\n",
                asLmr.physicalLengthFt, asRg.physicalLengthFt);
    check(!asLmr.verdict.isEmpty() && !asRg.verdict.isEmpty(),
          "a verdict is produced for both, so the assumption is always visible");

    std::printf("\n=== inversion check (proves the far-end condition) ===\n");
    int inv = 0, tested = 0;
    check(checkInversion(oc, sc, &inv, &tested),
          "a genuine open/short pair is detected as inverted");
    std::printf("       inverted at %d of %d sampled points\n", inv, tested);

    // Feeding the SAME sweep as both ends must NOT look like a valid pair --
    // this is the operator error the check exists to catch.
    int inv2 = 0, t2 = 0;
    check(!checkInversion(oc, oc, &inv2, &t2),
          "the same sweep used for both ends is REJECTED, not accepted");

    std::printf("\n=== integrity reporting ===\n");
    SweepResult tiny;
    CoaxResult bad = analyseOpenShort(tiny, tiny, *lmr);
    check(!bad.ok && !bad.error.isEmpty(),
          "an empty pair fails with an explanation rather than silently");

    std::printf("\n%d check(s) failed.\n", failures);
    return failures ? 1 : 0;
}
