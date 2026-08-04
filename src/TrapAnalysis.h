#pragma once

// TrapAnalysis — characterise a parallel LC trap on the bench.
//
// For testing home-made or manufactured coil traps as COMPONENTS, before they
// go on an antenna. That is a different job from dip-hunting a trap in situ:
// here the trap is in a fixture and we want real numbers — resonant frequency,
// loaded Q, and the depth of the notch.
//
// Method: SERIES-THROUGH S21. The trap sits IN LINE between CH0 and CH1, so
//     S21 = Z0 / (Z0 + Ztrap)
// A parallel LC is high impedance at resonance, which blocks the through path
// and produces a deep notch. ⚠ This is the SERIES topology — a trap shunted
// across the line to ground gives an almost flat trace instead. Reading the
// notch gives:
//
//   f0  — the minimum of |S21|
//   Q   — f0 / bandwidth at 3 dB above the notch minimum
//   L,C — from f0 and Q, given the fixture impedance
//
// ⚠ Q from a series-through measurement is LOADED Q: the 50 Ω source and load
// damp the resonance, so it reads lower than the trap's unloaded Q. That is the
// honest number for antenna work (the trap is loaded in service too), but it is
// NOT the figure a coil datasheet quotes. The panel says so rather than letting
// the number be mistaken for unloaded Q.
//
// ⚠ Without a THRU calibration the absolute depth is uncalibrated — fixture and
// cable loss are included. f0 is barely affected (it is a frequency, not a
// level), and Q only mildly, so an uncalibrated sweep still ranks traps and
// tracks a tuning change. The result carries a flag saying which it was.

#include <QString>
#include <QVector>

namespace TciMon {

struct TrapPoint {
    double mhz = 0;
    double s21Db = 0;        // 20*log10(|S21|)
};

struct TrapResult {
    bool    ok = false;
    QString error;

    QVector<TrapPoint> points;

    double  f0Mhz = 0;           // notch centre
    double  depthDb = 0;         // how far below the passband floor
    double  passbandDb = 0;      // reference level away from resonance
    double  bandwidthMhz = 0;    // at 3 dB above the minimum
    double  loadedQ = 0;
    double  inductanceUh = 0;
    double  capacitancePf = 0;
    bool    thruCalibrated = false;

    // Populated when the sweep cannot support a verdict.
    QString caution;
};

// Analyse an S21 sweep for a parallel-LC notch.
//   fixtureOhms — the through impedance the trap is measured in (50 for a
//                 normal VNA fixture). Used only for the L/C estimate.
TrapResult analyseTrap(const QVector<TrapPoint>& sweep, double fixtureOhms,
                       bool thruCalibrated);

} // namespace TciMon
