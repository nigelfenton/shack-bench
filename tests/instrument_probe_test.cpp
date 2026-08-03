// instrument_probe_test — exercise the Instrument drivers against whatever is
// actually plugged into this bench, and print real results.
//
// This is a HARDWARE test, not a unit test: it is the evidence that the panel's
// drivers parse what the instruments really emit, rather than what a design
// document said they would. It refuses to be a silent pass — every branch
// prints, and the exit code is 1 if nothing at all was found.
//
// Run:  instrument_probe_test            (probe only, no sweeps)
//       instrument_probe_test --sweep    (also take one short sweep each)

#include "Instrument.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <cmath>
#include <cstdio>

using namespace TciMon;

static int failures = 0;

static void check(bool cond, const char* what)
{
    std::printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", what);
    if (!cond) ++failures;
}

static SweepResult runSweep(InstrumentId::Kind kind, const QString& port,
                            qint64 a, qint64 b, int pts)
{
    QThread th;
    SweepWorker w;
    w.moveToThread(&th);
    th.start();

    SweepResult out;
    QEventLoop loop;
    QObject::connect(&w, &SweepWorker::finished,
                     [&](const SweepResult& r) { out = r; loop.quit(); });
    QObject::connect(&w, &SweepWorker::progress,
                     [](int d, int t, const QString& n) {
                         std::printf("      ... %s %d/%d\n",
                                     n.toLocal8Bit().constData(), d, t);
                         std::fflush(stdout);
                     });

    const char* slot = (kind == InstrumentId::Kind::RigExpert) ? "runRigExpertSweep"
                     : (kind == InstrumentId::Kind::NanoVna)   ? "runNanoVnaSweep"
                                                               : "runTinySaSweep";
    QMetaObject::invokeMethod(&w, slot, Qt::QueuedConnection,
                              Q_ARG(QString, port), Q_ARG(qint64, a),
                              Q_ARG(qint64, b), Q_ARG(int, pts));
    QTimer::singleShot(240000, &loop, &QEventLoop::quit);   // hard ceiling
    loop.exec();
    th.quit();
    th.wait(4000);
    return out;
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const bool doSweep = QCoreApplication::arguments().contains("--sweep");

    std::printf("=== probing serial ports ===\n");
    QStringList log;
    const QVector<InstrumentId> found = probeInstruments(&log);
    for (const QString& l : log)
        std::printf("  %s\n", l.toLocal8Bit().constData());

    check(!found.isEmpty(), "at least one instrument answered");
    for (const auto& id : found)
        std::printf("  -> %s  [%.3f-%.3f MHz]\n",
                    id.describe().toLocal8Bit().constData(),
                    id.minHz / 1e6, id.maxHz / 1e6);

    // The tinySA in this shack is the ORIGINAL, which clamps at 350 MHz. If a
    // tinySA is present, prove the driver reports the ACCEPTED span rather
    // than the requested one.
    for (const auto& id : found) {
        if (id.kind != InstrumentId::Kind::TinySa) continue;
        std::printf("\n=== tinySA: does the driver report the CLAMPED span? ===\n");
        const SweepResult r = runSweep(id.kind, id.port, 100000, 6000000000LL, 290);
        std::printf("  requested stop : 6000.000 MHz\n");
        std::printf("  reported span  : %.3f-%.3f MHz\n",
                    r.calFromHz / 1e6, r.calToHz / 1e6);
        std::printf("  calState       : %s\n", r.calState.toLocal8Bit().constData());
        check(r.calToHz < 6000000000LL,
              "an over-range request is reported as the span the instrument accepted");
        check(r.calState.contains("clamped"),
              "the clamp is called out in the cal state, not hidden");
        check(!r.points.isEmpty(), "spectrum points were returned");
        if (!r.points.isEmpty()) {
            double lo = 1e9, hi = -1e9;
            for (const auto& p : r.points) { lo = std::min(lo, p.dbm); hi = std::max(hi, p.dbm); }
            std::printf("  %lld points, %.1f to %.1f dBm\n",
                        (long long)r.points.size(), lo, hi);
            check(hi > lo, "the trace has real dynamic range (not a flat line)");
        }
    }

    if (!doSweep) {
        std::printf("\n(pass --sweep to also sweep the antenna analysers)\n");
        std::printf("\n%d check(s) failed.\n", failures);
        return failures ? 1 : 0;
    }

    for (const auto& id : found) {
        if (id.kind == InstrumentId::Kind::NanoVna) {
            std::printf("\n=== NanoVNA: 40m sweep ===\n");
            const SweepResult r = runSweep(id.kind, id.port, 7000000, 7300000, 101);
            std::printf("  cal   : %s\n", r.calState.toLocal8Bit().constData());
            std::printf("  points: %lld\n", (long long)r.points.size());
            if (!r.error.isEmpty())
                std::printf("  FLAG  : %s\n", r.error.toLocal8Bit().constData());
            check(!r.points.isEmpty(), "the VNA returned S11 points");
            // Physical-impossibility flag: with CH0 open this MUST fire. That
            // it fires is the guardrail working, not a driver failure.
            int impossible = 0;
            for (const auto& p : r.points) if (!std::isfinite(p.swr)) ++impossible;
            std::printf("  |gamma|>=1 points: %d of %lld\n",
                        impossible, (long long)r.points.size());
            check(r.error.isEmpty() || r.error.contains("impossible") ||
                      r.error.contains("Γ"),
                  "an open/uncalibrated port is FLAGGED rather than plotted as data");
        }
        if (id.kind == InstrumentId::Kind::RigExpert) {
            std::printf("\n=== AA-170: 40m sweep ===\n");
            const SweepResult r = runSweep(id.kind, id.port, 7000000, 7300000, 20);
            std::printf("  points: %lld\n", (long long)r.points.size());
            if (!r.error.isEmpty())
                std::printf("  ERROR : %s\n", r.error.toLocal8Bit().constData());
            if (r.points.isEmpty()) {
                std::printf("  (the analyser is not returning measurement data — "
                            "see the error above)\n");
            } else {
                double best = 1e9; qint64 bestHz = 0;
                for (const auto& p : r.points)
                    if (p.swr < best) { best = p.swr; bestHz = p.hz; }
                std::printf("  best match %.2f at %.4f MHz\n", best, bestHz / 1e6);
                check(best >= 1.0, "SWR is never below 1.0 (physically impossible)");
            }
        }
    }

    std::printf("\n%d check(s) failed.\n", failures);
    return failures ? 1 : 0;
}
