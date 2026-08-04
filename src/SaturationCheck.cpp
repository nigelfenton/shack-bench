#include "SaturationCheck.h"

#include <algorithm>
#include <cmath>

namespace TciMon {

namespace {

// How little ALC movement counts as "pinned", across a meaningful gain span.
// The HL2 case moved 0.2 dB over 20 gain points; a healthy FLEX sweep moves
// many dB. 2 dB is comfortably between the two.
constexpr double kPinnedSpanDb   = 2.0;
constexpr int    kMinGainSpan    = 10;

// ALC this close to full scale is the radio saying "no headroom left". The
// HL2 sat at -1.4 dBFS while the target was -10.
constexpr double kCeilingDbfs    = -4.0;

// Ignore trivial power wobble; only a real fall counts as compression.
constexpr double kFwdFallFraction = 0.85;

} // namespace

SatVerdict checkSaturation(const QVector<SatPoint>& points,
                           double alcTargetDbfs)
{
    SatVerdict v;

    // Only points where the radio actually reported ALC can say anything.
    QVector<SatPoint> usable;
    for (const auto& p : points)
        if (p.hasAlc) usable << p;
    if (usable.size() < 3) return v;

    std::sort(usable.begin(), usable.end(),
              [](const SatPoint& a, const SatPoint& b) {
                  return a.gain < b.gain;
              });

    double alcLo = usable.first().alcMax, alcHi = usable.first().alcMax;
    for (const auto& p : usable) {
        alcLo = std::min(alcLo, p.alcMax);
        alcHi = std::max(alcHi, p.alcMax);
    }
    v.alcSpanDb = alcHi - alcLo;
    v.alcCeilingDb = alcHi;
    v.gainSpan = usable.last().gain - usable.first().gain;

    // Power: did more drive ever produce LESS power?
    double peak = 0;
    int peakGain = 0;
    for (const auto& p : usable)
        if (p.fwdAvg > peak) { peak = p.fwdAvg; peakGain = p.gain; }
    v.fwdPeak = peak;
    v.fwdAtMaxGain = usable.last().fwdAvg;
    v.powerNonMonotonic =
        peak > 0 && peakGain < usable.last().gain &&
        v.fwdAtMaxGain < peak * kFwdFallFraction;

    const bool pinned = v.alcSpanDb <= kPinnedSpanDb &&
                        v.gainSpan >= kMinGainSpan;
    const bool atCeiling = v.alcCeilingDb >= kCeilingDbfs;
    const bool aboveTarget = v.alcCeilingDb > alcTargetDbfs;

    // ⚠ Require the ALC evidence AND the power evidence together. Pinned ALC
    // alone could be a genuinely flat region; falling power alone could be a
    // supply sagging. Both at once is compression.
    if (!(pinned && atCeiling && aboveTarget && v.powerNonMonotonic))
        return v;

    v.saturated = true;
    v.reason =
        QString("ALC is pinned at %1 dBFS — it moved only %2 dB while tx_gain "
                "moved %3 points, and forward power FELL from %4 W to %5 W as "
                "drive rose. That is compression, not a drive curve: the TX "
                "audio is already clipping before the sweep starts.")
            .arg(v.alcCeilingDb, 0, 'f', 1)
            .arg(v.alcSpanDb, 0, 'f', 1)
            .arg(v.gainSpan)
            .arg(v.fwdPeak, 0, 'f', 1)
            .arg(v.fwdAtMaxGain, 0, 'f', 1);
    v.advice =
        "Lower the test tone peak rather than widening the sweep — widening "
        "cannot help, because tx_gain has stopped affecting the result. Some "
        "backends (the Hermes-Lite 2 among them) accept TCI audio at a level "
        "where a 0.999 full-scale tone is already saturating the modulator, "
        "so there is no ALC knee to find.";
    return v;
}

} // namespace TciMon
