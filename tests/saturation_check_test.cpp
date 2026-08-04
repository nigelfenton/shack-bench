// saturation_check_test — tell a clipped input apart from a missed knee.
//
// The positive case uses the REAL Hermes-Lite 2 telemetry from shack-bench#1.
// The negative cases matter just as much: a detector that cries clipping on a
// healthy sweep would send the operator chasing a tone level that was never
// the problem.

#include "SaturationCheck.h"

#include <QtGlobal>
#include <cmath>
#include <cstdio>

using namespace TciMon;

static int failures = 0;

static void check(bool cond, const char* what)
{
    std::printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", what);
    if (!cond) ++failures;
}

static SatPoint pt(int gain, double fwd, double alc, bool hasAlc = true)
{
    SatPoint p;
    p.gain = gain; p.fwdAvg = fwd; p.alcMax = alc; p.hasAlc = hasAlc;
    return p;
}

int main()
{
    std::printf("=== the real HL2 run from shack-bench#1 ===\n");
    // rf=100 tx_gain=80  fwd=2.2 W  alc_peak=-1.6
    // rf=100 tx_gain=90  fwd=2.9 W  alc_peak=-1.4
    // rf=100 tx_gain=100 fwd=1.9 W  alc_peak=-1.4
    QVector<SatPoint> hl2{pt(80, 2.2, -1.6), pt(90, 2.9, -1.4),
                          pt(100, 1.9, -1.4)};
    SatVerdict v = checkSaturation(hl2, -10.0);
    check(v.saturated, "the HL2 clipping case IS detected");
    check(v.powerNonMonotonic, "and the power fall is noticed (2.9 W -> 1.9 W)");
    check(v.alcSpanDb < 1.0, "ALC barely moved across 20 gain points");
    std::printf("       %s\n", v.reason.toLocal8Bit().constData());

    std::printf("\n=== a healthy FLEX-style sweep must NOT trip it ===\n");
    // ALC climbs steadily toward the target and power rises monotonically —
    // this is what a knee looks like, and it must be left alone.
    QVector<SatPoint> flex;
    for (int g = 10; g <= 60; g += 10)
        flex << pt(g, 5.0 + g * 0.6, -40.0 + g * 0.55);
    SatVerdict f = checkSaturation(flex, -10.0);
    check(!f.saturated, "a rising ALC curve is not called saturation");
    check(!f.powerNonMonotonic, "and its power is monotonic");

    std::printf("\n=== pinned ALC ALONE is not enough ===\n");
    // Flat ALC but power still climbing: could be a genuinely flat region.
    // Reporting clipping here would be a false accusation.
    QVector<SatPoint> flatButRising{pt(80, 2.0, -1.5), pt(90, 2.6, -1.4),
                                    pt(100, 3.1, -1.4)};
    check(!checkSaturation(flatButRising, -10.0).saturated,
          "flat ALC with RISING power is not reported as clipping");

    std::printf("\n=== falling power ALONE is not enough ===\n");
    // Power sagging while ALC still moves freely — a supply problem, not
    // clipping, and a different fix entirely.
    QVector<SatPoint> saggy{pt(80, 2.2, -30.0), pt(90, 2.9, -20.0),
                            pt(100, 1.9, -12.0)};
    check(!checkSaturation(saggy, -10.0).saturated,
          "falling power with MOVING ALC is not reported as clipping");

    std::printf("\n=== ALC pinned near the TARGET is not clipping ===\n");
    // Sitting at -10 dBFS is the sweep succeeding, not saturating.
    QVector<SatPoint> onTarget{pt(80, 2.2, -10.2), pt(90, 2.9, -10.1),
                               pt(100, 1.9, -10.1)};
    check(!checkSaturation(onTarget, -10.0).saturated,
          "ALC pinned AT the target is not called clipping");

    std::printf("\n=== degenerate input is handled ===\n");
    check(!checkSaturation({}, -10.0).saturated, "an empty sweep is not saturated");
    QVector<SatPoint> tiny{pt(80, 2.2, -1.6), pt(90, 2.9, -1.4)};
    check(!checkSaturation(tiny, -10.0).saturated,
          "two points are too few to judge");
    QVector<SatPoint> noAlc{pt(80, 2.2, 0, false), pt(90, 2.9, 0, false),
                            pt(100, 1.9, 0, false)};
    check(!checkSaturation(noAlc, -10.0).saturated,
          "points without ALC telemetry cannot show saturation");

    std::printf("\n=== the advice names the right remedy ===\n");
    check(v.advice.contains("tone", Qt::CaseInsensitive),
          "it says to lower the tone");
    check(v.advice.contains("widen", Qt::CaseInsensitive),
          "and explicitly says widening the sweep will not help");

    std::printf("\n%d check(s) failed.\n", failures);
    return failures ? 1 : 0;
}
