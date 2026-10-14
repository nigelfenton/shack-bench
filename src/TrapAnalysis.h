#pragma once

// TrapAnalysis — characterise a parallel LC trap on the bench.
//
// For testing home-made or manufactured coil traps as COMPONENTS, before they
// go on an antenna. That is a different job from dip-hunting a trap in situ:
// here the trap is in a fixture and we want real numbers — resonant frequency,
// the trap's Q, and the depth of the notch.
//
// Method: SERIES-THROUGH S21. The trap sits IN LINE between CH0 and CH1, so it
// is a series impedance between two Z0 ports:
//     S21 = 2*Z0 / (2*Z0 + Ztrap)
// ⚠ 2*Z0, not Z0: the trap sees the source AND the load in series with it.
// The choke analysis (CableTests.cpp) uses the same form; the two must agree.
// A parallel LC is high impedance at resonance, which blocks the through path
// and produces a deep notch. ⚠ This is the SERIES topology — a trap shunted
// across the line to ground gives an almost flat trace instead. Reading the
// notch gives:
//
//   f0  — the minimum of |S21|
//   Rp  — the dynamic resistance at resonance, from |S21| at the minimum:
//           A = 1/|S21min| = 10^(depth/20) ;  Rp = 2*Z0*(A - 1)
//   Q   — f0 / bandwidth at 3 dB above the notch minimum (see below)
//   L,C — Xl = Rp/Q ;  L = Xl/(2*pi*f0) ;  C = 1/((2*pi*f0)^2 * L)
//
// ⚠ Depth is measured from 0 dB, NOT from a "passband" picked off the sweep.
// The fixture loads the trap with 2*Z0 = 100 Ω against an Rp of tens of kΩ,
// so the fixture's loaded Q is far below 1 and the notch skirt is enormous:
// for a 2.5 uH / 51 pF trap, S21 is still -17 dB two MHz away from 14.1 MHz.
// Any reference taken from the sweep itself sits down that skirt and
// under-reads the depth by 20 dB or more. With a THRU cal, 0 dB IS the
// through-path reference by definition, so that is what we use.
//
// ⚠ Q here is the TRAP's own Q, not a loaded Q. 3 dB above the minimum is
// where |Ztrap| has fallen to about Rp/sqrt(2) — the trap's own half-power
// points — because the trap dominates the 100 Ω fixture there. Exactly:
//     Qmeasured = Qtrap * sqrt(1 - 2/A^2)
// which we invert. For any usefully deep notch the correction is tiny, and
// the figure is comparable with a coil's quoted unloaded Q. That is also the
// Q that L = Rp/(2*pi*f0*Q) needs, so the L/C estimate is self-consistent.
//
// ⚠ An undersampled notch (fewer than ~3 points across the 3 dB bandwidth)
// misreads f0, depth and Q together. The result says so, and in that case
// L comes instead from the notch SKIRT, which a wide sweep samples well:
// far from resonance the trap is almost pure reactance X, so
//     |X| = 2*Z0*sqrt(1/|S21|^2 - 1) ;  L = |X| * |1 - (f/f0)^2| / (2*pi*f)
// That needs only f0 and the skirt, not the depth or the bandwidth.
//
// ⚠ Without a THRU calibration there is no trustworthy 0 dB. We still use 0,
// so fixture and cable loss count as trap loss: depth, Rp and L read high and
// C low — roughly 1 % per 0.1 dB of through loss. f0 is barely affected (it is
// a frequency, not a level), nor is Q (a constant offset does not move the
// 3 dB points), so an uncalibrated sweep still ranks traps and tracks a tuning
// change. The result carries a flag saying which it was.

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
    double  depthDb = 0;         // how far the minimum is below referenceDb
    double  referenceDb = 0;     // always 0 dB — the THRU reference (see top)
    double  rpOhms = 0;          // dynamic resistance at resonance
    double  bandwidthMhz = 0;    // at 3 dB above the minimum
    double  trapQ = 0;           // the trap's own Q, NOT loaded Q (see top)
    double  inductanceUh = 0;
    double  capacitancePf = 0;
    bool    lcFromSkirt = false; // L/C from the skirt, not from Rp and Q
    bool    undersampled = false;// < ~3 points across the 3 dB bandwidth
    bool    thruCalibrated = false;

    // Populated when the sweep cannot support a verdict.
    QString caution;
};

// Analyse an S21 sweep for a parallel-LC notch.
//   fixtureOhms — the port impedance Z0 either side of the trap (50 for a
//                 normal VNA fixture). Used only for the Rp/L/C estimate.
TrapResult analyseTrap(const QVector<TrapPoint>& sweep, double fixtureOhms,
                       bool thruCalibrated);

} // namespace TciMon
