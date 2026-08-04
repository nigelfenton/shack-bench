// scope_decode_test — the DSO2D15 sample decode and computed measurements.
//
// No scope needed: the interleave and the volts conversion are pure functions,
// and the measurements are checked against synthetic signals whose answers are
// known exactly. Firmware 3.0.1 reports no measurement values at all, so this
// maths IS the measurement — worth pinning.

#include "Scope.h"

#include <QtGlobal>
#include <cmath>
#include <cstdio>

using namespace TciMon;

static constexpr double kPi = 3.14159265358979323846;
static int failures = 0;

static void check(bool cond, const char* what)
{
    std::printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", what);
    if (!cond) ++failures;
}

static void near(double got, double want, double tol, const char* what)
{
    const bool ok = std::abs(got - want) <= tol;
    std::printf("%s %s (got %.4f, want %.4f +/- %.4f)\n",
                ok ? "[ OK ]" : "[FAIL]", what, got, want, tol);
    if (!ok) ++failures;
}

int main()
{
    std::printf("=== channel de-interleave ===\n");
    // Two channels, 2000-byte blocks round-robin: ch1 blocks hold 10,
    // ch2 blocks hold 20. Anything that mixes them up shows immediately.
    QByteArray payload;
    for (int block = 0; block < 4; ++block) {
        const char v = (block % 2 == 0) ? char(10) : char(20);
        payload.append(QByteArray(2000, v));
    }
    // scale 1 V/div, offset 0 -> volts = raw/25
    const QVector<double> ch1 = decodeChannel(payload, 1, 2, 1.0, 0.0);
    const QVector<double> ch2 = decodeChannel(payload, 2, 2, 1.0, 0.0);
    check(ch1.size() == 4000, "ch1 gets both of its blocks");
    check(ch2.size() == 4000, "ch2 gets both of its blocks");
    near(ch1.first(), 10.0 / 25.0, 1e-9, "ch1 decodes its own value, not ch2's");
    near(ch2.first(), 20.0 / 25.0, 1e-9, "ch2 decodes its own value, not ch1's");

    std::printf("\n=== volts conversion (25 counts per division) ===\n");
    QByteArray one; one.append(char(25));
    const QVector<double> v1 = decodeChannel(one, 1, 1, 2.0, 0.0, 1);
    near(v1.value(0), 2.0, 1e-9, "raw 25 at 2 V/div is exactly one division");
    QByteArray neg; neg.append(char(-25));
    const QVector<double> v2 = decodeChannel(neg, 1, 1, 2.0, 0.0, 1);
    near(v2.value(0), -2.0, 1e-9, "samples are SIGNED (raw -25 -> -2 V)");
    QByteArray off; off.append(char(25));
    const QVector<double> v3 = decodeChannel(off, 1, 1, 2.0, 0.5, 1);
    near(v3.value(0), 1.5, 1e-9, "the channel offset is subtracted");

    std::printf("\n=== single-channel capture is not de-interleaved ===\n");
    QByteArray solo(1000, char(25));
    const QVector<double> s1 = decodeChannel(solo, 1, 1, 1.0, 0.0);
    check(s1.size() == 1000, "with one channel enabled every byte belongs to it");

    std::printf("\n=== computed measurements: 1 kHz sine, 2 Vpp ===\n");
    ScopeChannel ch;
    ch.index = 1;
    // voltsPerDiv matters now: the "is there a signal" gate is in DIVISIONS,
    // so a channel with no sensitivity set would look like a flat trace.
    ch.voltsPerDiv = 0.5;                // 2 Vpp = 4 divisions, plainly a signal
    const double fs = 125000.0;          // the scope's actual sample rate
    const double f  = 1000.0;
    for (int i = 0; i < 2000; ++i)
        ch.volts << std::sin(2.0 * kPi * f * i / fs);
    computeMeasurements(&ch, 1.0 / fs);
    near(ch.vpp, 2.0, 0.02, "Vpp of a unit sine is 2.0");
    near(ch.vrms, 0.7071, 0.01, "Vrms is 1/sqrt(2)");
    near(ch.vmean, 0.0, 0.02, "mean of a full-cycle sine is zero");
    near(ch.freqHz, 1000.0, 15.0, "frequency is recovered from the crossings");

    std::printf("\n=== a DC offset must not break the frequency ===\n");
    ScopeChannel dc;
    dc.voltsPerDiv = 0.5;
    for (int i = 0; i < 2000; ++i)
        dc.volts << 3.0 + std::sin(2.0 * kPi * 500.0 * i / fs);
    computeMeasurements(&dc, 1.0 / fs);
    near(dc.vmean, 3.0, 0.05, "the offset shows in the mean");
    near(dc.freqHz, 500.0, 10.0,
         "crossings are counted about the MEAN, not about zero");

    std::printf("\n=== a flat trace must NOT report a frequency ===\n");
    // The unconnected-probe case. A naive zero-crossing count on noise
    // invents a confident frequency; this must refuse instead.
    // ⭐ This is the REAL open-probe capture from the DSO2D15: the whole trace
    // occupied two ADC counts (0.08 of a division) at 2 V/div, and a naive
    // crossing count reported a confident 259 Hz.
    ScopeChannel flat;
    flat.voltsPerDiv = 2.0;
    for (int i = 0; i < 2000; ++i) {
        const int raw = (i % 7 == 0) ? 3 : 4;        // the measured histogram
        flat.volts << double(raw) / 25.0 * 2.0 - 0.24;
    }
    computeMeasurements(&flat, 1.0 / fs);
    check(flat.freqHz == 0.0,
          "two counts of dither at 2 V/div reports NO frequency (was 259 Hz)");
    near(flat.vpp, 0.08, 0.01, "its Vpp is 2 counts = 0.08 V");

    std::printf("\n=== the SAME swing IS a signal at high sensitivity ===\n");
    // 0.08 V is nothing at 2 V/div but four divisions at 20 mV/div. The gate
    // must scale with the instrument, not with an absolute voltage.
    ScopeChannel sensitive;
    sensitive.voltsPerDiv = 0.02;
    for (int i = 0; i < 2000; ++i)
        sensitive.volts << 0.04 * std::sin(2.0 * kPi * 250.0 * i / fs);
    computeMeasurements(&sensitive, 1.0 / fs);
    near(sensitive.freqHz, 250.0, 8.0,
         "an 80 mVpp sine at 20 mV/div IS measured (4 divisions)");

    std::printf("\n=== just under the gate is refused ===\n");
    ScopeChannel marginal;
    marginal.voltsPerDiv = 1.0;
    for (int i = 0; i < 2000; ++i)
        marginal.volts << 0.2 * std::sin(2.0 * kPi * 250.0 * i / fs);  // 0.4 div
    computeMeasurements(&marginal, 1.0 / fs);
    check(marginal.freqHz == 0.0,
          "0.4 division of swing is below the half-division gate");

    std::printf("\n=== degenerate input is handled, not crashed on ===\n");
    const QVector<double> none = decodeChannel(QByteArray(), 1, 2, 1.0, 0.0);
    check(none.isEmpty(), "an empty payload decodes to nothing");
    ScopeChannel empty;
    computeMeasurements(&empty, 1.0 / fs);
    check(empty.vpp == 0.0, "measuring an empty channel is a no-op");

    std::printf("\n%d check(s) failed.\n", failures);
    return failures ? 1 : 0;
}
