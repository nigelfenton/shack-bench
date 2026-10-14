#include "TrapAnalysis.h"

#include <QStringList>

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

    // ---- depth, against 0 dB ---------------------------------------------
    // ⚠ NOT a passband picked off the sweep. This fixture loads the trap with
    // only 2*Z0, so its loaded Q is far below 1 and the skirt is so broad
    // that the "outer thirds" of even a 3–30 MHz sweep are still well down
    // it. A median of them under-read a 54 dB notch as 32 dB and put L and C
    // out by 20x. With a THRU cal, 0 dB is the through reference by
    // definition; without one, 0 dB is still the least-wrong assumption and
    // the caution below says what that costs.
    const double minDb = sweep[minIdx].s21Db;
    double maxDb = sweep[0].s21Db;
    for (const auto& p : sweep) maxDb = std::max(maxDb, p.s21Db);
    r.referenceDb = 0.0;
    r.depthDb = r.referenceDb - minDb;

    // Both tests: below 0 dB AND below the rest of the trace. The second
    // stops a flat but lossy (uncalibrated) through path passing as a notch.
    if (r.depthDb < 3.0 || maxDb - minDb < 3.0) {
        r.error = QString("no usable notch — the deepest point is only %1 dB "
                          "below 0 dB and %2 dB below the rest of the trace. "
                          "Is the trap actually in the through path?")
                      .arg(r.depthDb, 0, 'f', 1)
                      .arg(maxDb - minDb, 0, 'f', 1);
        return r;
    }

    // Rp straight from |S21| at the minimum: S21 = 2*Z0/(2*Z0 + Rp).
    const double a = std::pow(10.0, r.depthDb / 20.0);   // 1/|S21min|
    if (fixtureOhms > 0) r.rpOhms = 2.0 * fixtureOhms * (a - 1.0);

    // ---- the trap's Q from the 3 dB bandwidth ----------------------------
    // 3 dB above the minimum is the trap's own half-power points, not a
    // loaded-Q measurement (see the header). Measured bandwidth is wider by
    // 1/sqrt(1 - 2/A^2); undo that. It only matters for a shallow notch, and
    // near 3 dB it blows up, so leave very shallow notches uncorrected.
    const double step =
        (sweep.last().mhz - sweep.first().mhz) / double(sweep.size() - 1);
    const double level = minDb + 3.0;
    const double lo = crossingFrom(sweep, minIdx, -1, level);
    const double hi = crossingFrom(sweep, minIdx, +1, level);
    QStringList notes;
    if (lo > 0 && hi > 0 && hi > lo) {
        r.bandwidthMhz = hi - lo;
        const double k = 1.0 - 2.0 / (a * a);
        r.trapQ = r.f0Mhz / r.bandwidthMhz;
        if (k > 0.25) r.trapQ /= std::sqrt(k);

        // ⚠ A notch narrower than ~3 sweep steps sits between samples: the
        // minimum misses the true bottom (depth and Rp read low, f0 is off by
        // up to half a step) and the interpolated 3 dB points are guesses.
        if (step > 0 && r.bandwidthMhz < 3.0 * step) {
            r.undersampled = true;
            notes << QString("notch undersampled — the 3 dB bandwidth (%1 kHz) "
                             "spans only %2 sweep steps, so f0, depth and Q are "
                             "approximate. Narrow the sweep around %3 MHz.")
                         .arg(r.bandwidthMhz * 1e3, 0, 'f', 1)
                         .arg(r.bandwidthMhz / step, 0, 'f', 1)
                         .arg(r.f0Mhz, 0, 'f', 3);
        }
    } else {
        notes << "the 3 dB points fall outside the sweep, so Q could not be "
                 "measured — widen the span.";
    }

    // ---- L and C ---------------------------------------------------------
    // Well-sampled notch: Xl = Rp/Q, L = Xl/w0, C = 1/(w0^2 L). Rp and Q
    // both describe the trap itself, so the pair is consistent.
    const double w0 = 2.0 * kPi * r.f0Mhz * 1e6;
    double lH = 0;
    if (r.trapQ > 0 && r.rpOhms > 0 && w0 > 0)
        lH = r.rpOhms / r.trapQ / w0;

    // Undersampled notch: Rp and Q are both wrong (together they put L out
    // by ~25% on a 3–30 MHz, 401-point sweep), so use the skirt instead. Far
    // from f0 the trap is nearly pure reactance, and |S21| gives |X|:
    //     |X| = 2*Z0*sqrt(1/|S21|^2 - 1) ;  L = |X|*|1 - (f/f0)^2| / w
    // Only points >5 bandwidths out (resistive part < 10% of X) and at least
    // 6 dB down (so passband ripple and cal error do not dominate) are used,
    // and the median taken, which shrugs off a stray second resonance.
    if (r.undersampled && fixtureOhms > 0 && r.bandwidthMhz > 0) {
        QVector<double> ls;
        for (const auto& p : sweep) {
            if (std::abs(p.mhz - r.f0Mhz) < 5.0 * r.bandwidthMhz) continue;
            if (p.s21Db > -6.0 || p.mhz <= 0) continue;
            const double s = std::pow(10.0, p.s21Db / 20.0);
            const double x = 2.0 * fixtureOhms * std::sqrt(1.0 / (s * s) - 1.0);
            const double fr = p.mhz / r.f0Mhz;
            ls << x * std::abs(1.0 - fr * fr) / (2.0 * kPi * p.mhz * 1e6);
        }
        if (ls.size() >= 8) {
            std::sort(ls.begin(), ls.end());
            lH = ls[ls.size() / 2];
            r.lcFromSkirt = true;
        }
    }
    if (lH > 0 && w0 > 0) {
        r.inductanceUh = lH * 1e6;
        r.capacitancePf = 1.0 / (w0 * w0 * lH) * 1e12;
    }

    if (!thruCalibrated)
        notes << "uncalibrated through path — f0 and Q are barely affected, but "
                 "depth is measured from an assumed 0 dB, so fixture and cable "
                 "loss count as trap loss: Rp and L read high and C low, "
                 "roughly 1 % per 0.1 dB of through loss.";
    r.caution = notes.join("  ");

    r.ok = true;
    return r;
}

} // namespace TciMon
