#pragma once

// Instrument — serial-attached bench instruments (antenna analysers and a
// spectrum analyser), driven from their PUBLISHED ASCII command sets.
//
// PROVENANCE: every driver here is written from the vendors' PUBLISHED ASCII
// command sets and from live probing of the hardware in the shack -- not
// copied from NanoVNA-Saver, NanoVNA-QT or libxavna. A wire protocol is not
// copyrightable expression, the same footing as the TCI and Metis work already
// in this app.
//
// (Shack-Bench was MIT when this was written, which made vendoring those GPL
// projects a licence problem. It is now GPL-3.0, so reuse would be permitted --
// but the clean-room provenance is worth keeping on the record regardless.)
//
// Threading: a sweep can take minutes (an AA-170 FRX120 across 2-30 MHz took
// ~2 minutes), so drivers run on their own QThread and report progress. The
// GUI thread never blocks on serial.

#include <QObject>
#include <QString>
#include <QVector>
#include <QMap>

namespace TciMon {

// One measured point. Antenna analysers give R and X natively; SWR and return
// loss are derived. Keep R/X primary -- a disconnected feedline is diagnosable
// ONLY from reactance (a constant series capacitance), never from SWR alone.
struct SweepPoint {
    qint64 hz = 0;
    double r  = 0.0;      // ohms, resistance          (antenna instruments)
    double x  = 0.0;      // ohms, reactance
    double swr = 1.0;     // derived, >= 1
    double returnLossDb = 0.0;
    double dbm = 0.0;     // spectrum instruments
};

struct SweepResult {
    QVector<SweepPoint> points;
    QString instrument;       // "RigExpert AA-170"
    QString firmware;
    QString port;
    QString calState;         // free text as the instrument reports it
    qint64  calFromHz = 0;    // 0/0 when unknown -- the panel must say so
    qint64  calToHz   = 0;
    QString takenAtIso;
    QString antennaNote;      // "what was connected" -- filled by the panel
    bool    ok = false;
    QString error;
};

// Identification of a probed port. The NanoVNA-F and the tinySA both enumerate
// as STM32 CDC VID_0483/PID_5740, so VID/PID CANNOT discriminate them -- we ask
// the instrument and classify its banner.
struct InstrumentId {
    enum class Kind { Unknown, RigExpert, NanoVna, TinySa };
    Kind    kind = Kind::Unknown;
    QString port;
    QString model;
    QString firmware;
    QString banner;
    qint64  minHz = 0;
    qint64  maxHz = 0;
    QString describe() const;
};

// Probe every serial port and report what is actually attached. Read-only:
// no sweep is started and no instrument state is changed.
QVector<InstrumentId> probeInstruments(QStringList* log = nullptr);

// ---------------------------------------------------------------------------

class SweepWorker : public QObject {
    Q_OBJECT
public:
    explicit SweepWorker(QObject* parent = nullptr) : QObject(parent) {}

public slots:
    // RigExpert AA-170: ON / FQ / SW / FRX<n> -> "MHz,R,X" per line, then OFF.
    // Native R/X, 0.1-170 MHz, 38400 8N1, no prompt.
    void runRigExpertSweep(const QString& port, qint64 fromHz, qint64 toHz,
                           int points);

    // NanoVNA-F V2: sweep/frequencies/data 0 -> S11 "re im"; SWR derived.
    // Reads `cal` first so the panel can render cal state honestly.
    void runNanoVnaSweep(const QString& port, qint64 fromHz, qint64 toHz,
                         int points);

    // tinySA: scanraw/scan -> amplitude in dBm across the span.
    // NOTE the unit in this shack is the ORIGINAL tinySA (Cortex-M0), which
    // CLAMPS SILENTLY at 350 MHz rather than erroring -- the driver reads the
    // sweep back and reports what the instrument actually accepted.
    void runTinySaSweep(const QString& port, qint64 fromHz, qint64 toHz,
                        int points);

    void cancel() { m_cancel = true; }

signals:
    void progress(int done, int total, const QString& note);
    void finished(const TciMon::SweepResult& result);

private:
    bool m_cancel = false;
};

} // namespace TciMon
