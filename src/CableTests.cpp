#include "CableTests.h"

#include "CoaxAnalysis.h"

#include <algorithm>
#include <cmath>

namespace TciMon {

namespace {
constexpr double kPiLocal = 3.14159265358979323846;
constexpr double kC = 299.792458;   // Mm/s, i.e. metres * MHz

struct BandSpot { const char* name; double mhz; };
// One representative frequency per HF band, for the per-band choke summary.
const BandSpot kBandSpots[] = {
    {"160m",  1.900}, {"80m",  3.750}, {"60m",  5.360}, {"40m",  7.150},
    {"30m",  10.125}, {"20m", 14.175}, {"17m", 18.118}, {"15m", 21.225},
    {"12m",  24.940}, {"10m", 28.850}, {"6m",  50.500},
};
} // namespace

VfResult measureVelocityFactor(
    const QVector<QPair<double, double>>& freqMhzAndX, double physicalMetres)
{
    VfResult r;
    r.physicalMetres = physicalMetres;

    if (physicalMetres <= 0) {
        r.error = "enter the cable's physical length first — the velocity "
                  "factor is derived from it, so without it there is nothing "
                  "to compute.";
        return r;
    }
    if (freqMhzAndX.size() < 8) {
        r.error = "too few points to find the resonances";
        return r;
    }

    // X crosses zero every QUARTER wavelength on a shorted line -- the
    // crossings alternate series (R low) and parallel (R high) resonance.
    // Assuming half-wave here is a factor-of-two error in the length, and
    // therefore in the velocity factor.
    for (int i = 1; i < freqMhzAndX.size(); ++i) {
        const double x0 = freqMhzAndX[i - 1].second;
        const double x1 = freqMhzAndX[i].second;
        if ((x0 < 0) == (x1 < 0)) continue;
        const double f0 = freqMhzAndX[i - 1].first;
        const double f1 = freqMhzAndX[i].first;
        const double den = std::abs(x0) + std::abs(x1);
        r.crossingsMhz << (den > 0 ? f0 + (f1 - f0) * std::abs(x0) / den : f0);
    }

    // ⚠ The NanoVNA returns EXACT zeros outside its stored calibration —
    // R = 50.000, X = 0.000, |gamma| = 0.0000 to four decimals. That is not a
    // measurement, it is the absence of one, and it silently contributes fake
    // "crossings". Detect and reject those points before counting anything.
    int exactZeros = 0;
    for (const auto& p : freqMhzAndX)
        if (p.second == 0.0) ++exactZeros;
    if (exactZeros > freqMhzAndX.size() / 4) {
        r.error = QString("%1 of %2 points read exactly X = 0.000, which is "
                          "the instrument reporting NO DATA rather than a "
                          "measurement. The sweep has gone outside the "
                          "calibrated range — recalibrate over the span you "
                          "want to sweep, or narrow the sweep to the "
                          "calibrated one.")
                      .arg(exactZeros).arg(freqMhzAndX.size());
        return r;
    }

    if (r.crossingsMhz.size() < 2) {
        r.error = QString("only %1 resonance(s) in this sweep. Crossings occur "
                          "every quarter wavelength, so a SHORT cable needs a "
                          "HIGH sweep — but the instrument only returns real "
                          "data inside its calibration. Either calibrate "
                          "higher, or use a LONGER piece of the same cable: "
                          "roughly 400 in gives 5 crossings in 3–30 MHz, "
                          "1200 in gives 16.")
                      .arg(r.crossingsMhz.size());
        return r;
    }

    QVector<double> spacing;
    for (int i = 1; i < r.crossingsMhz.size(); ++i)
        spacing << r.crossingsMhz[i] - r.crossingsMhz[i - 1];
    double sum = 0;
    for (double v : spacing) sum += v;
    r.meanSpacingMhz = sum / spacing.size();
    const auto mm = std::minmax_element(spacing.begin(), spacing.end());
    if (r.meanSpacingMhz > 0)
        r.spacingSpreadPct = 100.0 * (*mm.second - *mm.first) / r.meanSpacingMhz;

    if (r.meanSpacingMhz <= 0) {
        r.error = "the resonances are not evenly spaced — is this really a "
                  "plain length of cable?";
        return r;
    }

    // Quarter-wave spacing -> electrical length -> VF against the measured one.
    r.electricalMetres = kC / (4.0 * r.meanSpacingMhz);
    r.velocityFactor = physicalMetres / r.electricalMetres;

    if (r.velocityFactor <= 0.3 || r.velocityFactor >= 1.0) {
        r.error = QString("the computed VF is %1, which is not physically "
                          "sensible (real cable is 0.66–0.88). Check the "
                          "length entered, and that the far end is SHORTED.")
                      .arg(r.velocityFactor, 0, 'f', 3);
        return r;
    }

    // Name the closest catalogue entry, as a sanity check rather than a claim.
    double best = 1e9;
    for (const auto& c : cableTypes()) {
        const double d = std::abs(c.vf - r.velocityFactor);
        if (d < best) { best = d; r.nearestCable = c.name; r.nearestVf = c.vf; }
    }

    if (r.spacingSpreadPct > 12.0)
        r.caution = QString("the crossing spacing varies by %1%, which is more "
                            "than a plain cable should — a connector or a join "
                            "may be reflecting.")
                        .arg(r.spacingSpreadPct, 0, 'f', 1);
    if (r.crossingsMhz.size() < 4) {
        const QString note =
            QString("only %1 crossings were averaged; a wider sweep gives a "
                    "more reliable figure.").arg(r.crossingsMhz.size());
        r.caution = r.caution.isEmpty() ? note : r.caution + "  " + note;
    }

    r.ok = true;
    return r;
}

ChokeResult analyseChoke(
    const QVector<QPair<double, double>>& freqMhzAndS21Db, double fixtureZ0,
    double thresholdOhms)
{
    ChokeResult r;
    if (freqMhzAndS21Db.size() < 8) {
        r.error = "too few points to judge a choke";
        return r;
    }
    if (fixtureZ0 <= 0) fixtureZ0 = 50.0;

    // In a Z0 system, a series impedance Z across the through path gives
    //     S21 = 2*Z0 / (2*Z0 + Z)    =>    Z = 2*Z0 * (1/S21 - 1)
    // The choke bridges the two shells, so it IS that series element.
    double bestZ = -1, worstZ = 1e18;
    for (const auto& p : freqMhzAndS21Db) {
        const double s21 = std::pow(10.0, p.second / 20.0);
        if (s21 <= 0 || s21 >= 1.0) continue;   // |S21| >= 1 is not passive
        ChokePoint cp;
        cp.mhz = p.first;
        cp.zMagOhms = 2.0 * fixtureZ0 * (1.0 / s21 - 1.0);
        r.points << cp;
        if (cp.zMagOhms > bestZ) { bestZ = cp.zMagOhms; r.peakMhz = cp.mhz; }
        if (cp.zMagOhms < worstZ) { worstZ = cp.zMagOhms; r.worstMhz = cp.mhz; }
    }
    if (r.points.isEmpty()) {
        r.error = "no usable points — every sample had |S21| >= 1, which is "
                  "impossible for a passive fixture. Is the calibration valid "
                  "over this span?";
        return r;
    }
    r.peakOhms = bestZ;
    r.worstOhms = worstZ;

    // Per band, because "is it good?" really means "is it good where I
    // operate?" — a choke can be excellent on 20 m and useless on 80 m.
    for (const auto& b : kBandSpots) {
        if (b.mhz < r.points.first().mhz || b.mhz > r.points.last().mhz)
            continue;
        const ChokePoint* nearest = nullptr;
        double bestD = 1e18;
        for (const auto& p : r.points) {
            const double d = std::abs(p.mhz - b.mhz);
            if (d < bestD) { bestD = d; nearest = &p; }
        }
        if (!nearest) continue;
        ChokeResult::BandZ bz;
        bz.band = b.name;
        bz.mhz = b.mhz;
        bz.ohms = nearest->zMagOhms;
        bz.pass = nearest->zMagOhms >= thresholdOhms;
        r.bands << bz;
        ++r.bandsTested;
        if (bz.pass) ++r.bandsAboveThreshold;
    }

    if (r.bandsTested == 0) {
        r.verdict = "no ham band fell inside this sweep";
    } else if (r.bandsAboveThreshold == r.bandsTested) {
        r.verdict = QString("GOOD — above %1 Ω on all %2 bands in the sweep")
                        .arg(thresholdOhms, 0, 'f', 0).arg(r.bandsTested);
    } else if (r.bandsAboveThreshold == 0) {
        r.verdict = QString("POOR — below %1 Ω on every band in the sweep; "
                            "this is not choking common-mode current")
                        .arg(thresholdOhms, 0, 'f', 0);
    } else {
        r.verdict = QString("PARTIAL — above %1 Ω on %2 of %3 bands; useful "
                            "where it is high, not where it is not")
                        .arg(thresholdOhms, 0, 'f', 0)
                        .arg(r.bandsAboveThreshold).arg(r.bandsTested);
    }

    // A choke peaks at its self-resonance and falls away either side. If the
    // peak sits at the edge of the sweep the real peak is probably outside it.
    if (r.peakMhz <= r.points.first().mhz * 1.02 ||
        r.peakMhz >= r.points.last().mhz * 0.98) {
        r.caution = "the impedance peak is at the edge of the sweep, so the "
                    "choke's self-resonance is probably outside it — widen the "
                    "span to see where it actually works best.";
    }

    r.ok = true;
    return r;
}

} // namespace TciMon
