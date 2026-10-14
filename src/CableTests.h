#pragma once

// CableTests — two bench measurements that answer questions this shack has
// actually been caught by.
//
// 1. VELOCITY FACTOR, measured rather than assumed.
//    On 2026-08-03 a healthy DXE-400 run looked "25-40% lossier than spec"
//    purely because the analysis assumed RG-213's VF 0.66. The same measured
//    electrical length is 190.7 ft at 0.66 and 245.6 ft at 0.85, and loss is
//    judged PER FOOT — so the wrong VF produced a wrong verdict about a good
//    cable. Measuring VF once, from a known-length offcut, removes that whole
//    error class for every later measurement of that reel.
//
// 2. COMMON-MODE CHOKE IMPEDANCE.
//    A choke's job is to present a high impedance to current flowing on the
//    OUTSIDE of the coax shield. The G3TXQ fixture measures exactly that: two
//    SO-239s with their shells bridged by the choke under test, each centre pin
//    shorted to its own shell, so the VNA drives the shield outer surface.
//    From S21 in a Z0 system, the series impedance across the fixture is
//        Zcm = 2*Z0 * (1/S21 - 1)
//    A useful HF choke wants |Zcm| above about 1 kohm across the bands in use.

#include <QString>
#include <QVector>

#include <complex>

namespace TciMon {

// ---- velocity factor ---------------------------------------------------

struct VfResult {
    bool    ok = false;
    QString error;
    QString caution;

    QVector<double> crossingsMhz;
    double  meanSpacingMhz = 0;
    double  spacingSpreadPct = 0;
    double  physicalMetres = 0;     // what the operator measured
    double  electricalMetres = 0;   // what the sweep measured
    double  velocityFactor = 0;
    QString nearestCable;           // best match in the catalogue
    double  nearestVf = 0;
};

// `physicalMetres` is the OPERATOR'S measurement of the cable — connector
// shoulder to connector shoulder. The whole result is only as good as it.
VfResult measureVelocityFactor(const QVector<QPair<double, double>>& freqMhzAndX,
                               double physicalMetres);

// ---- common-mode choke -------------------------------------------------

struct ChokePoint {
    double mhz = 0;
    double zMagOhms = 0;
};

struct ChokeResult {
    bool    ok = false;
    QString error;
    QString caution;

    QVector<ChokePoint> points;
    double  peakOhms = 0;
    double  peakMhz = 0;          // self-resonance: where it chokes best
    double  worstOhms = 0;
    double  worstMhz = 0;
    int     bandsAboveThreshold = 0;
    int     bandsTested = 0;
    QString verdict;

    // Per-band summary, since "is it good?" is really "is it good where I
    // operate?" — a choke can be excellent on 20 m and useless on 80 m.
    struct BandZ { QString band; double mhz; double ohms; bool pass; };
    QVector<BandZ> bands;
};

// One complex S21 sample from the fixture.
struct S21Sample {
    double mhz = 0;
    std::complex<double> s21;
};

// Complex S21 -> common-mode impedance |Zcm|, judged against `thresholdOhms`
// (1000 is the usual figure for HF). Uses the full complex formula.
// ⚠ A choke is mostly REACTIVE away from self-resonance, and the magnitude-only
// form 2*Z0*(1/|S21| - 1) is exact only for a resistive Z: a true 1050 Ω
// reactance read as 955 Ω and FAILED a 1 kΩ threshold.
ChokeResult analyseChoke(const QVector<S21Sample>& samples, double fixtureZ0,
                         double thresholdOhms);

// Magnitude-only fallback (phase unknown): treats S21 as real, so reactive
// impedances UNDER-read. Prefer the complex overload; kept for old data.
ChokeResult analyseChoke(const QVector<QPair<double, double>>& freqMhzAndS21Db,
                         double fixtureZ0, double thresholdOhms);

} // namespace TciMon
