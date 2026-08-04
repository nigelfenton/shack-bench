#include "TrapAnalysis.h"

#include <algorithm>
#include <cmath>

namespace TciMon {

namespace {
constexpr double kPi = 3.14159265358979323846;

// Interpolate the frequency at which the trace crosses `level`, walking
// outward from the notch. Returns < 0 if the sweep never reaches it, which is
// the honest answer when the span is too narrow to contain the skirt.
double crossingFrom(const QVector<TrapPoint>& p, int fromIdx, int step,
                    double level)
{
    for (int i = fromIdx; i >= 0 && i < p.size(); i += step) {
        if (p[i].s21Db >= level) {
            const int prev = i - step;
            if (prev < 0 || prev >= p.size()) return p[i].mhz;
            const double d0 = p[prev].s21Db, d1 = p[i].s21Db;
            if (std::abs(d1 - d0) < 1e-12) return p[i].mhz;
            const double t = (level - d0) / (d1 - d0);
            return p[prev].mhz + t * (p[i].mhz - p[prev].mhz);
        }
    }
    return -1.0;
}
} // namespace

TrapResult analyseTrap(const QVector<TrapPoint>& sweep, double fixtureOhms,
                       bool thruCalibrated)
{
    TrapResult r;
    r.points = sweep;
    r.thruCalibrated = thruCalibrated;

    if (sweep.size() < 12) {
        r.error = "need at least a dozen points to find a notch";
        return r;
    }

    // ---- the notch -------------------------------------------------------
    int minIdx = 0;
    for (int i = 1; i < sweep.size(); ++i)
        if (sweep[i].s21Db < sweep[minIdx].s21Db) minIdx = i;
    r.f0Mhz = sweep[minIdx].mhz;

    // ⚠ A minimum at the very edge of the sweep is almost certainly NOT the
    // resonance — the real notch is outside the span. Say so rather than
    // reporting the edge as f0.
    if (minIdx <= 1 || minIdx >= sweep.size() - 2) {
        r.error = QString("the lowest point is at the edge of the sweep "
                          "(%1 MHz). The resonance is probably outside "
                          "%2–%3 MHz — widen the span.")
                      .arg(r.f0Mhz, 0, 'f', 3)
                      .arg(sweep.first().mhz, 0, 'f', 3)
                      .arg(sweep.last().mhz, 0, 'f', 3);
        return r;
    }

    // Passband reference: the median of the outer thirds, which is robust
    // against a tilted baseline and against a second resonance in view.
    QVector<double> edges;
    const int third = sweep.size() / 3;
    for (int i = 0; i < third; ++i) edges << sweep[i].s21Db;
    for (int i = sweep.size() - third; i < sweep.size(); ++i)
        edges << sweep[i].s21Db;
    std::sort(edges.begin(), edges.end());
    r.passbandDb = edges[edges.size() / 2];
    r.depthDb = r.passbandDb - sweep[minIdx].s21Db;

    if (r.depthDb < 3.0) {
        r.error = QString("no usable notch — the deepest point is only %1 dB "
                          "below the passband. Is the trap actually in the "
                          "through path?")
                      .arg(r.depthDb, 0, 'f', 1);
        return r;
    }

    // ---- loaded Q from the 3 dB bandwidth --------------------------------
    const double level = sweep[minIdx].s21Db + 3.0;
    const double lo = crossingFrom(sweep, minIdx, -1, level);
    const double hi = crossingFrom(sweep, minIdx, +1, level);
    if (lo > 0 && hi > 0 && hi > lo) {
        r.bandwidthMhz = hi - lo;
        if (r.bandwidthMhz > 0) r.loadedQ = r.f0Mhz / r.bandwidthMhz;
    } else {
        r.caution = "the 3 dB points fall outside the sweep, so Q could not be "
                    "measured — widen the span.";
    }

    // ---- L and C ---------------------------------------------------------
    // At resonance the parallel trap's dynamic resistance is what blocks the
    // path; combined with Q it gives the reactance, hence L and C.
    // For a SERIES-through fixture, S21 = Z0/(Z0+Rp) at resonance, so
    //   Rp = Z0 * (10^(depth/20) - 1)
    //   Xl = Rp / Q ;  L = Xl / (2*pi*f0) ;  C = 1 / ((2*pi*f0)^2 * L)
    if (r.loadedQ > 0 && fixtureOhms > 0) {
        const double rp =
            fixtureOhms * (std::pow(10.0, r.depthDb / 20.0) - 1.0);
        const double xl = rp / r.loadedQ;
        const double w = 2.0 * kPi * r.f0Mhz * 1e6;
        if (w > 0 && xl > 0) {
            const double lH = xl / w;
            r.inductanceUh = lH * 1e6;
            r.capacitancePf = 1.0 / (w * w * lH) * 1e12;
        }
    }

    if (!thruCalibrated) {
        const QString note =
            "uncalibrated through path — f0 is reliable, but depth includes "
            "fixture and cable loss and Q is approximate.";
        r.caution = r.caution.isEmpty() ? note : r.caution + "  " + note;
    }

    r.ok = true;
    return r;
}

} // namespace TciMon
