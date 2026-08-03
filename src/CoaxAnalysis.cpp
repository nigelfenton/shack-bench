#include "CoaxAnalysis.h"

#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <complex>

namespace TciMon {

namespace {

using Cplx = std::complex<double>;

// Published matched-line loss, dB per 100 ft. Values are the manufacturers'
// own figures; they are a yardstick, not a measurement.
const QVector<CableType>& catalogue()
{
    static const QVector<CableType> kTypes = {
        {"RG-213 / RG-8 (solid PE)", 0.66,
         {{1.0, 0.16}, {3.0, 0.30}, {10.0, 0.57}, {30.0, 1.05}, {50.0, 1.40}}},
        {"RG-8X (foam)", 0.82,
         {{1.0, 0.25}, {3.0, 0.45}, {10.0, 0.85}, {30.0, 1.55}, {50.0, 2.05}}},
        {"RG-58 (solid PE)", 0.66,
         {{1.0, 0.40}, {3.0, 0.70}, {10.0, 1.30}, {30.0, 2.40}, {50.0, 3.15}}},
        {"LMR-400 / DXE-400 (foam)", 0.85,
         {{1.0, 0.12}, {3.0, 0.22}, {10.0, 0.41}, {30.0, 0.74}, {50.0, 0.96}}},
        {"LMR-600 (foam)", 0.87,
         {{1.0, 0.08}, {3.0, 0.14}, {10.0, 0.27}, {30.0, 0.48}, {50.0, 0.63}}},
        {"Hardline 1/2\" (foam)", 0.88,
         {{1.0, 0.06}, {3.0, 0.11}, {10.0, 0.21}, {30.0, 0.38}, {50.0, 0.50}}},
        {"Generic 50 Ω (VF 0.66)", 0.66, {}},
        {"Generic 50 Ω (VF 0.85)", 0.85, {}},
    };
    return kTypes;
}

// Interpolate R and X onto a common frequency, so two sweeps taken with
// different point counts can still be paired.
bool sampleAt(const SweepResult& s, double mhz, double* r, double* x)
{
    if (s.points.size() < 2) return false;
    const double hz = mhz * 1e6;
    if (hz < s.points.first().hz || hz > s.points.last().hz) return false;
    for (int i = 1; i < s.points.size(); ++i) {
        if (s.points[i].hz >= hz) {
            const auto& a = s.points[i - 1];
            const auto& b = s.points[i];
            const double span = double(b.hz - a.hz);
            const double t = (span > 0) ? (hz - double(a.hz)) / span : 0.0;
            *r = a.r + t * (b.r - a.r);
            *x = a.x + t * (b.x - a.x);
            return true;
        }
    }
    return false;
}

} // namespace

double CableType::specLossDb(double mhz, double feet) const
{
    if (lossPer100ft.isEmpty()) return 0.0;
    // Loss goes roughly as sqrt(f); interpolate on that basis rather than
    // linearly, which would under-read between widely spaced anchors.
    const double sq = std::sqrt(mhz);
    if (mhz <= lossPer100ft.first().first)
        return lossPer100ft.first().second * feet / 100.0;
    if (mhz >= lossPer100ft.last().first)
        return lossPer100ft.last().second * feet / 100.0;
    for (int i = 1; i < lossPer100ft.size(); ++i) {
        if (lossPer100ft[i].first >= mhz) {
            const double f0 = std::sqrt(lossPer100ft[i - 1].first);
            const double f1 = std::sqrt(lossPer100ft[i].first);
            const double l0 = lossPer100ft[i - 1].second;
            const double l1 = lossPer100ft[i].second;
            const double t = (f1 > f0) ? (sq - f0) / (f1 - f0) : 0.0;
            return (l0 + t * (l1 - l0)) * feet / 100.0;
        }
    }
    return lossPer100ft.last().second * feet / 100.0;
}

const QVector<CableType>& cableTypes() { return catalogue(); }

const CableType* cableTypeByName(const QString& name)
{
    for (const auto& c : catalogue())
        if (c.name == name) return &c;
    return nullptr;
}

bool checkInversion(const SweepResult& openEnd, const SweepResult& shortEnd,
                    int* matchingPoints, int* testedPoints)
{
    int inverted = 0, tested = 0;
    if (openEnd.points.isEmpty() || shortEnd.points.isEmpty()) {
        if (matchingPoints) *matchingPoints = 0;
        if (testedPoints) *testedPoints = 0;
        return false;
    }
    const double lo = std::max(openEnd.points.first().hz,
                               shortEnd.points.first().hz) / 1e6;
    const double hi = std::min(openEnd.points.last().hz,
                               shortEnd.points.last().hz) / 1e6;
    // Sample a modest grid; we want the quarter-wave points, not every bin.
    const int kSamples = 25;
    for (int i = 0; i < kSamples; ++i) {
        const double f = lo + (hi - lo) * i / double(kSamples - 1);
        double ro, xo, rs, xs;
        if (!sampleAt(openEnd, f, &ro, &xo)) continue;
        if (!sampleAt(shortEnd, f, &rs, &xs)) continue;
        ++tested;
        // "High" means well above the 50 Ω line, "low" well below it. Points
        // where both are mid-scale are the crossover frequencies and carry no
        // information either way.
        const bool hiO = ro > 100.0, loO = ro < 25.0;
        const bool hiS = rs > 100.0, loS = rs < 25.0;
        if ((hiO && loS) || (loO && hiS)) ++inverted;
    }
    if (matchingPoints) *matchingPoints = inverted;
    if (testedPoints) *testedPoints = tested;
    // A real pair inverts at every quarter-wave point in the span. Over an HF
    // sweep of a long line that is several points; require at least three so
    // one coincidence cannot pass.
    return inverted >= 3;
}

CoaxResult analyseOpenShort(const SweepResult& openEnd,
                            const SweepResult& shortEnd,
                            const CableType& cable)
{
    CoaxResult out;
    out.cableName = cable.name;
    out.assumedVf = cable.vf;

    if (openEnd.points.size() < 8 || shortEnd.points.size() < 8) {
        out.error = "both an open and a short sweep are needed (at least 8 "
                    "points each)";
        return out;
    }

    for (const auto& p : openEnd.points)
        if (!std::isfinite(p.swr)) ++out.impossibleOpen;
    for (const auto& p : shortEnd.points)
        if (!std::isfinite(p.swr)) ++out.impossibleShort;

    out.inversionConfirmed = checkInversion(openEnd, shortEnd,
                                            &out.inversionPoints, nullptr);

    // ---- length, from the SHORT sweep's X zero-crossings -----------------
    // ⭐ A shorted line's reactance crosses zero every QUARTER wavelength, not
    // every half: the crossings alternate between half-wave points (series
    // resonance, R low) and quarter-wave points (parallel resonance, R high).
    // Measured on the real line: 3.398 (R 5.2) / 5.051 (R 493) / 6.800 (R 6.8)
    // / 8.444 (R 383) ... so consecutive crossings are a QUARTER wavelength
    // apart and the mean spacing is c/(4*L_elec), not c/(2*L_elec).
    // Getting this wrong yields exactly a factor of two in the length.
    const auto& sp = shortEnd.points;
    for (int i = 1; i < sp.size(); ++i) {
        const double x0 = sp[i - 1].x, x1 = sp[i].x;
        if ((x0 < 0) == (x1 < 0)) continue;
        const double f0 = sp[i - 1].hz / 1e6, f1 = sp[i].hz / 1e6;
        const double denom = std::abs(x0) + std::abs(x1);
        out.crossingsMhz << (denom > 0 ? f0 + (f1 - f0) * std::abs(x0) / denom
                                       : f0);
    }

    if (out.crossingsMhz.size() >= 2) {
        QVector<double> spacing;
        for (int i = 1; i < out.crossingsMhz.size(); ++i)
            spacing << out.crossingsMhz[i] - out.crossingsMhz[i - 1];
        double sum = 0;
        for (double v : spacing) sum += v;
        out.meanSpacingMhz = sum / spacing.size();
        const auto mm = std::minmax_element(spacing.begin(), spacing.end());
        if (out.meanSpacingMhz > 0)
            out.spacingSpreadPct =
                100.0 * (*mm.second - *mm.first) / out.meanSpacingMhz;
        if (out.meanSpacingMhz > 0) {
            // Consecutive crossings are a QUARTER wavelength apart, so the
            // electrical length is c/(4*spacing).
            out.electricalHalfWaveM = 299.792458 / (4.0 * out.meanSpacingMhz);
            out.physicalLengthM = out.electricalHalfWaveM * cable.vf;
            out.physicalLengthFt = out.physicalLengthM * 3.280839895;
        }
    }

    // ---- Z0 and loss, from the pair --------------------------------------
    const double lo = std::max(openEnd.points.first().hz,
                               shortEnd.points.first().hz) / 1e6;
    const double hi = std::min(openEnd.points.last().hz,
                               shortEnd.points.last().hz) / 1e6;
    const int kSteps = 60;
    double z0sum = 0;
    int z0n = 0;
    for (int i = 0; i <= kSteps; ++i) {
        const double f = lo + (hi - lo) * i / double(kSteps);
        double ro, xo, rs, xs;
        if (!sampleAt(openEnd, f, &ro, &xo)) continue;
        if (!sampleAt(shortEnd, f, &rs, &xs)) continue;
        const Cplx zoc(ro, xo), zsc(rs, xs);
        if (std::abs(zoc) < 1e-9) continue;
        const Cplx z0 = std::sqrt(zoc * zsc);
        const Cplx gl = std::atanh(std::sqrt(zsc / zoc));
        LinePoint lp;
        lp.mhz = f;
        lp.z0Mag = std::abs(z0);
        lp.lossDb = std::abs(8.685889638 * gl.real());
        if (!std::isfinite(lp.z0Mag) || !std::isfinite(lp.lossDb)) continue;
        out.points << lp;
        z0sum += lp.z0Mag;
        ++z0n;
    }
    if (z0n) out.meanZ0 = z0sum / z0n;
    out.pointsUsed = out.points.size();

    if (out.points.isEmpty()) {
        out.error = "could not derive Z0 — do the two sweeps overlap in "
                    "frequency?";
        return out;
    }

    // ---- verdict against the chosen cable --------------------------------
    const LinePoint& top = out.points.last();
    out.topMhz = top.mhz;
    out.measuredLossAtTopDb = top.lossDb;
    out.specLossAtTopDb = cable.specLossDb(top.mhz, out.physicalLengthFt);

    if (out.specLossAtTopDb > 0.01) {
        const double ratio = out.measuredLossAtTopDb / out.specLossAtTopDb;
        if (ratio < 1.15)
            out.verdict = QString("at or better than spec (%1×)")
                              .arg(ratio, 0, 'f', 2);
        else if (ratio < 1.5)
            out.verdict = QString("%1× published loss — mildly lossy; check "
                                  "connectors")
                              .arg(ratio, 0, 'f', 2);
        else
            out.verdict = QString("%1× published loss — investigate")
                              .arg(ratio, 0, 'f', 2);
    } else {
        out.verdict = "no published loss figures for this cable — length and "
                      "Z0 only";
    }

    out.ok = true;
    return out;
}

} // namespace TciMon
