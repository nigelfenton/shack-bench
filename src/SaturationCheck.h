#pragma once

// SaturationCheck — tell "the sweep missed the knee" apart from "the input was
// clipped before the sweep began".
//
// Both end with no knee found, but they need opposite responses, and the
// current message ("widen the sweep or raise the ALC target") points at the
// sweep range when the real cause is the tone level. On a Hermes-Lite 2 that
// advice is actively misleading: widening the sweep cannot help, because the
// independent variable has stopped mattering.
//
// The signature of a clipped input, from the HL2 run in shack-bench#1:
//
//   rf=100 tx_gain=80   fwd=2.2 W  alc_peak=-1.6
//   rf=100 tx_gain=90   fwd=2.9 W  alc_peak=-1.4
//   rf=100 tx_gain=100  fwd=1.9 W  alc_peak=-1.4
//
//   1. ALC is PINNED — it barely moves while tx_gain changes by 20 points,
//      and it sits just below 0 dBFS rather than near the target.
//   2. Power is NON-MONOTONIC — more drive produced LESS power (2.9 -> 1.9 W).
//      That is compression, not a drive curve.
//
// Either alone is weak evidence; together they are conclusive. The check
// deliberately requires both, so a genuinely flat-but-unsaturated region does
// not get misreported as clipping.

#include <QString>
#include <QVector>

namespace TciMon {

struct SatPoint {
    int    gain = 0;
    double fwdAvg = 0;
    double alcMax = 0;
    bool   hasAlc = false;
};

struct SatVerdict {
    bool    saturated = false;
    QString reason;        // empty unless saturated
    QString advice;        // what to actually do about it

    // Evidence, so the log can show the working rather than assert.
    double  alcSpanDb = 0;     // how much ALC moved across the sweep
    int     gainSpan = 0;      // how much tx_gain moved
    double  alcCeilingDb = 0;  // the highest ALC seen
    bool    powerNonMonotonic = false;
    double  fwdPeak = 0;
    double  fwdAtMaxGain = 0;
};

// `alcTargetDbfs` is the sweep's target, e.g. -10.0.
SatVerdict checkSaturation(const QVector<SatPoint>& points,
                           double alcTargetDbfs);

} // namespace TciMon
