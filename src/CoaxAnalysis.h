#pragma once

// CoaxAnalysis — derive a feedline's real properties from an open/short pair.
//
// This is the analysis that answers "how long is my coax, is it really 50 Ω,
// and is it losing more than it should". Kept out of the GUI so it can be
// tested on its own against known data.
//
// The measurements are:
//   Z0    = sqrt(Zoc * Zsc)                      (textbook, needs BOTH ends)
//   gamma*l = atanh(sqrt(Zsc / Zoc))             loss = 8.686 * Re(gamma*l)
//   length: X crosses zero every half wavelength, so the mean spacing of the
//           crossings gives the ELECTRICAL length; the physical length needs
//           the velocity factor.
//
// ⭐ The VF is the part that bit us on 2026-08-03. Electrical length is
// measured; physical length is an ASSUMPTION layered on top. The same
// 88.07 m electrical half-wave is 190.7 ft at RG-213's VF 0.66 and 245.6 ft
// at LMR-400's 0.85 — and because loss-per-foot is judged against the
// physical length, choosing the wrong cable made a healthy line look 25-40%
// lossy. Never report a loss verdict without stating the assumed VF.

#include <QString>
#include <QVector>

#include "Instrument.h"

namespace TciMon {

// A cable type: velocity factor plus published matched-line loss so a measured
// line can be judged against what it is supposed to do.
struct CableType {
    QString name;
    double  vf = 0.66;
    // Published loss in dB per 100 ft at these frequencies (MHz).
    QVector<QPair<double, double>> lossPer100ft;

    double specLossDb(double mhz, double feet) const;
};

// The catalogue. Ordered so the common HF cables come first.
const QVector<CableType>& cableTypes();
const CableType* cableTypeByName(const QString& name);

// One frequency's worth of derived line properties.
struct LinePoint {
    double mhz = 0;
    double z0Mag = 0;        // |Z0|, ohms
    double lossDb = 0;       // total one-way matched loss at this frequency
};

struct CoaxResult {
    bool    ok = false;
    QString error;

    // Integrity — reported, never silently assumed.
    int     pointsUsed = 0;
    int     impossibleOpen = 0;   // |gamma| >= 1 in the open sweep
    int     impossibleShort = 0;
    bool    inversionConfirmed = false;  // R swaps high/low between the two
    int     inversionPoints = 0;

    // Length
    QVector<double> crossingsMhz;
    double  meanSpacingMhz = 0;
    double  spacingSpreadPct = 0;      // (max-min)/mean, a regularity check
    double  electricalHalfWaveM = 0;
    double  assumedVf = 0;
    double  physicalLengthM = 0;
    double  physicalLengthFt = 0;

    // Impedance and loss
    QVector<LinePoint> points;
    double  meanZ0 = 0;

    // Verdict against the chosen cable's published figures.
    QString cableName;
    double  specLossAtTopDb = 0;
    double  measuredLossAtTopDb = 0;
    double  topMhz = 0;
    QString verdict;
};

// Analyse an open/short pair. Both sweeps must cover the same frequencies.
CoaxResult analyseOpenShort(const SweepResult& openEnd,
                            const SweepResult& shortEnd,
                            const CableType& cable);

// Does the R signature actually invert between the two sweeps? A quarter-wave
// open looks like a short and vice versa, so R must swap high/low. If it does
// not, one of the far-end conditions was not what the operator thought — the
// single most common way this measurement lies.
bool checkInversion(const SweepResult& openEnd, const SweepResult& shortEnd,
                    int* matchingPoints, int* testedPoints);

} // namespace TciMon
