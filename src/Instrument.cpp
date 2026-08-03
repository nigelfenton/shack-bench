#include "Instrument.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QRegularExpression>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QStringList>
#include <QThread>
#include <cmath>
#include <limits>

namespace TciMon {

namespace {

// Read until the port stays quiet for `quietMs`, or `capMs` elapses.
//
// Every instrument here streams its results over seconds with pauses in the
// middle, so a naive "wait 1 s then read" returns an empty buffer and looks
// exactly like "the instrument sent nothing". Both the NanoVNA's `data 0` and
// the AA-170's FRX stream were observed doing this during bring-up.
QString readUntilQuiet(QSerialPort& sp, int capMs, int quietMs)
{
    QByteArray out;
    QElapsedTimer total, since;
    total.start();
    since.start();
    while (total.elapsed() < capMs) {
        if (sp.waitForReadyRead(120)) {
            out += sp.readAll();
            since.restart();
        } else if (!out.isEmpty() && since.elapsed() > quietMs) {
            break;
        }
    }
    return QString::fromLatin1(out);
}

QStringList cleanLines(const QString& blob, const QString& echoed)
{
    QStringList out;
    for (const QString& raw : blob.split(QRegularExpression("[\r\n]+"))) {
        const QString l = raw.trimmed();
        if (l.isEmpty() || l == "ch>" || l == echoed) continue;
        out << l;
    }
    return out;
}

// ⭐ Line terminator is CR ONLY, not CRLF — and this is load-bearing.
//
// The RigExpert tolerates a trailing LF on short commands (VER, ON, FQ, SW all
// answer "OK" either way), but on FRX the stray LF is taken as a second, empty
// command and the measurement stream never starts: FRX answers a bare "OK" and
// emits no data. That failure is indistinguishable from a dead instrument, and
// it cost a full session to find. Send "\r".
//
// The NanoVNA and tinySA shells accept CR alone as well, so one terminator
// serves all three.
QStringList command(QSerialPort& sp, const QString& cmd,
                    int capMs = 4000, int quietMs = 600)
{
    sp.clear();
    sp.write((cmd + "\r").toLatin1());
    sp.flush();
    return cleanLines(readUntilQuiet(sp, capMs, quietMs), cmd);
}

bool openPort(QSerialPort& sp, const QString& port, int baud, QString* err)
{
    sp.setPortName(port);
    sp.setBaudRate(baud);
    sp.setDataBits(QSerialPort::Data8);
    sp.setParity(QSerialPort::NoParity);
    sp.setStopBits(QSerialPort::OneStop);
    sp.setFlowControl(QSerialPort::NoFlowControl);
    if (!sp.open(QIODevice::ReadWrite)) {
        if (err) *err = QString("cannot open %1: %2").arg(port, sp.errorString());
        return false;
    }
    QThread::msleep(300);
    sp.clear();
    return true;
}

// Γ -> SWR, guarding the |Γ| >= 1 case. A passive antenna cannot reflect more
// power than it receives, so |Γ| >= 1 is proof of a bad calibration or an open
// port -- NOT a very bad antenna. Report it as infinite rather than letting a
// negative denominator produce a plausible-looking small number.
double swrFromGamma(double g)
{
    if (g >= 0.999999) return std::numeric_limits<double>::infinity();
    return (1.0 + g) / (1.0 - g);
}

} // namespace

QString InstrumentId::describe() const
{
    switch (kind) {
    case Kind::RigExpert: return QString("RigExpert %1 fw %2 (%3)").arg(model, firmware, port);
    case Kind::NanoVna:   return QString("%1 fw %2 (%3)").arg(model, firmware, port);
    case Kind::TinySa:    return QString("%1 fw %2 (%3)").arg(model, firmware, port);
    default:              return QString("unknown device (%1)").arg(port);
    }
}

QVector<InstrumentId> probeInstruments(QStringList* log)
{
    QVector<InstrumentId> found;

    for (const QSerialPortInfo& info : QSerialPortInfo::availablePorts()) {
        // FlexRadio's virtual ports are numerous and are not instruments;
        // probing them is slow and pointless.
        if (info.description().contains("FlexRadio", Qt::CaseInsensitive))
            continue;

        InstrumentId id;
        id.port = info.portName();

        // The RigExpert is FTDI and speaks a bare ASCII protocol at 38400 with
        // no prompt; the VNA/SA pair are STM32 CDC shells at 115200. Try the
        // shell first, then the RigExpert dialect.
        {
            QSerialPort sp;
            QString err;
            if (openPort(sp, id.port, 115200, &err)) {
                const QStringList banner = command(sp, "info", 2500, 500);
                const QString joined = banner.join(" | ");
                id.banner = joined;
                if (joined.contains("tinySA", Qt::CaseInsensitive)) {
                    id.kind = InstrumentId::Kind::TinySa;
                    id.model = joined.contains("tinySA4") ? "tinySA Ultra" : "tinySA";
                    const QStringList v = command(sp, "version", 2000, 400);
                    if (!v.isEmpty()) id.firmware = v.first();
                    // Read the accepted sweep back -- this instrument CLAMPS
                    // silently rather than refusing an out-of-range request.
                    const QStringList sw = command(sp, "sweep", 2000, 400);
                    if (!sw.isEmpty()) {
                        const QStringList p = sw.first().split(' ', Qt::SkipEmptyParts);
                        if (p.size() >= 2) { id.minHz = p[0].toLongLong(); id.maxHz = p[1].toLongLong(); }
                    }
                } else if (joined.contains("NanoVNA", Qt::CaseInsensitive)) {
                    id.kind = InstrumentId::Kind::NanoVna;
                    id.model = "NanoVNA-F V2";
                    for (const QString& l : banner)
                        if (l.startsWith("Model:")) id.model = l.mid(6).trimmed();
                    const QStringList v = command(sp, "version", 2000, 400);
                    if (!v.isEmpty()) id.firmware = v.first();
                }
                sp.close();
            }
        }

        if (id.kind == InstrumentId::Kind::Unknown) {
            QSerialPort sp;
            QString err;
            if (openPort(sp, id.port, 38400, &err)) {
                const QStringList ver = command(sp, "VER", 2500, 500);
                const QString joined = ver.join(" | ");
                if (joined.contains("AA-", Qt::CaseInsensitive)) {
                    id.kind = InstrumentId::Kind::RigExpert;
                    id.banner = joined;
                    const QStringList p = ver.first().split(' ', Qt::SkipEmptyParts);
                    if (!p.isEmpty()) id.model = p.first();
                    if (p.size() > 1) id.firmware = p.at(1);
                    id.minHz = 100000;
                    id.maxHz = 170000000;
                }
                sp.close();
            }
        }

        if (log)
            *log << QString("%1: %2").arg(id.port,
                    id.kind == InstrumentId::Kind::Unknown ? "not an instrument"
                                                           : id.describe());
        if (id.kind != InstrumentId::Kind::Unknown)
            found.push_back(id);
    }
    return found;
}

// ---------------------------------------------------------------------------

void SweepWorker::runRigExpertSweep(const QString& port, qint64 fromHz,
                                    qint64 toHz, int points)
{
    m_cancel = false;
    SweepResult res;
    res.instrument = "RigExpert AA-170";
    res.port = port;
    res.takenAtIso = QDateTime::currentDateTime().toString(Qt::ISODate);

    QSerialPort sp;
    QString err;
    if (!openPort(sp, port, 38400, &err)) {
        res.error = err;
        emit finished(res);
        return;
    }

    const QStringList ver = command(sp, "VER", 2500, 500);
    if (!ver.isEmpty()) {
        const QStringList p = ver.first().split(' ', Qt::SkipEmptyParts);
        if (p.size() > 1) res.firmware = p.at(1);
    }

    const qint64 centre = (fromHz + toHz) / 2;
    const qint64 span   = std::llabs(toHz - fromHz);

    command(sp, "ON", 2000, 400);
    command(sp, QString("FQ%1").arg(centre), 2000, 400);
    command(sp, QString("SW%1").arg(span), 2000, 400);

    emit progress(0, points, "sweeping");

    // FRX<n> streams n+1 lines of "MHz,R,X". It can take minutes over a wide
    // span, and it pauses mid-stream, so allow a long cap and a generous quiet
    // window before concluding the instrument has finished.
    sp.clear();
    sp.write(QString("FRX%1\r").arg(points).toLatin1());   // CR only — see command()
    sp.flush();
    const QString blob = readUntilQuiet(sp, 180000, 5000);

    int parsed = 0;
    for (const QString& line : cleanLines(blob, QString())) {
        if (m_cancel) break;
        const QStringList f = line.split(',', Qt::SkipEmptyParts);
        if (f.size() < 3) continue;
        bool okF = false, okR = false, okX = false;
        const double mhz = f[0].trimmed().toDouble(&okF);
        const double r   = f[1].trimmed().toDouble(&okR);
        const double x   = f[2].trimmed().toDouble(&okX);
        if (!okF || !okR || !okX) continue;

        SweepPoint pt;
        pt.hz = qint64(mhz * 1e6);
        pt.r = r;
        pt.x = x;
        // Γ from the measured impedance against a 50 Ω reference.
        const double zr = r - 50.0, zi = x;
        const double dr = r + 50.0;
        const double den = dr * dr + zi * zi;
        const double g = (den > 0) ? std::sqrt((zr * zr + zi * zi) / den) : 1.0;
        pt.swr = swrFromGamma(g);
        pt.returnLossDb = (g > 0) ? -20.0 * std::log10(g) : 99.0;
        res.points.push_back(pt);
        if (++parsed % 5 == 0) emit progress(parsed, points, "sweeping");
    }

    command(sp, "OFF", 2000, 400);   // leave RF off, always
    sp.close();

    if (res.points.isEmpty()) {
        res.error = "the analyser accepted the commands but returned no "
                    "measurement data (FRX answered OK with no MHz,R,X lines). "
                    "Check the batteries and that the unit's own screen is live "
                    "— the AA-170 runs its measurement side from internal "
                    "cells, not from USB.";
    } else {
        res.ok = true;
    }
    emit finished(res);
}

void SweepWorker::runNanoVnaSweep(const QString& port, qint64 fromHz,
                                  qint64 toHz, int points)
{
    m_cancel = false;
    SweepResult res;
    res.instrument = "NanoVNA-F V2";
    res.port = port;
    res.takenAtIso = QDateTime::currentDateTime().toString(Qt::ISODate);

    QSerialPort sp;
    QString err;
    if (!openPort(sp, port, 115200, &err)) {
        res.error = err;
        emit finished(res);
        return;
    }

    const QStringList v = command(sp, "version", 2000, 400);
    if (!v.isEmpty()) res.firmware = v.first();

    // Record the calibration span BEFORE changing the sweep -- the stored cal
    // belongs to whatever range was set when it was taken, and re-ranging is
    // exactly how a cal silently starts interpolating.
    const QStringList before = command(sp, "sweep", 2000, 400);
    if (!before.isEmpty()) {
        const QStringList p = before.first().split(' ', Qt::SkipEmptyParts);
        if (p.size() >= 2) {
            res.calFromHz = p[0].toLongLong();
            res.calToHz   = p[1].toLongLong();
        }
    }
    const QStringList cal = command(sp, "cal", 2000, 400);
    res.calState = cal.isEmpty() ? QString("unknown") : cal.first();

    command(sp, QString("sweep %1 %2 %3").arg(fromHz).arg(toHz).arg(points),
            4000, 800);
    QThread::msleep(600);

    emit progress(0, points, "reading frequencies");
    const QStringList freqs = command(sp, "frequencies", 30000, 2500);
    emit progress(points / 3, points, "reading S11");
    const QStringList data = command(sp, "data 0", 30000, 2500);

    const int n = std::min(freqs.size(), data.size());
    int impossible = 0;
    for (int i = 0; i < n && !m_cancel; ++i) {
        bool okHz = false;
        const qint64 hz = freqs[i].toLongLong(&okHz);
        const QStringList ri = data[i].split(' ', Qt::SkipEmptyParts);
        if (!okHz || ri.size() < 2) continue;
        const double re = ri[0].toDouble();
        const double im = ri[1].toDouble();
        const double g  = std::hypot(re, im);
        if (g >= 1.0) ++impossible;

        SweepPoint pt;
        pt.hz = hz;
        pt.swr = swrFromGamma(g);
        pt.returnLossDb = (g > 0) ? -20.0 * std::log10(g) : 99.0;
        // Z = 50 (1+Γ)/(1-Γ)
        const double dr = 1.0 - re, di = -im;
        const double den = dr * dr + di * di;
        if (den > 1e-12) {
            const double nr = 1.0 + re, ni = im;
            pt.r = 50.0 * (nr * dr + ni * di) / den;
            pt.x = 50.0 * (ni * dr - nr * di) / den;
        }
        res.points.push_back(pt);
    }
    sp.close();

    if (res.points.isEmpty()) {
        res.error = "no S11 data returned";
    } else {
        res.ok = true;
        // Physical-impossibility check (design sheet 3.4.4). |Γ| >= 1 on a
        // passive load cannot happen: it means a bad/absent calibration, or
        // nothing connected to CH0. Say so rather than plotting it.
        if (impossible > res.points.size() / 10) {
            res.error = QString("%1 of %2 points have |Γ| ≥ 1, which is "
                                "impossible for a passive load — CH0 is "
                                "probably open, or the calibration does not "
                                "cover this span. Treat this sweep as invalid.")
                            .arg(impossible).arg(res.points.size());
        }
    }
    emit finished(res);
}

void SweepWorker::runTinySaSweep(const QString& port, qint64 fromHz,
                                 qint64 toHz, int points)
{
    m_cancel = false;
    SweepResult res;
    res.instrument = "tinySA";
    res.port = port;
    res.takenAtIso = QDateTime::currentDateTime().toString(Qt::ISODate);

    QSerialPort sp;
    QString err;
    if (!openPort(sp, port, 115200, &err)) {
        res.error = err;
        emit finished(res);
        return;
    }

    const QStringList v = command(sp, "version", 2000, 400);
    if (!v.isEmpty()) res.firmware = v.first();

    command(sp, QString("sweep start %1").arg(fromHz), 3000, 500);
    command(sp, QString("sweep stop %1").arg(toHz), 3000, 500);

    // ⭐ Read the sweep BACK. This instrument clamps an out-of-range request
    // silently and reports success, so what we asked for is not evidence of
    // what it is doing. The accepted span is the truth we plot against.
    qint64 gotFrom = fromHz, gotTo = toHz;
    int gotPoints = points;
    const QStringList sw = command(sp, "sweep", 3000, 500);
    if (!sw.isEmpty()) {
        const QStringList p = sw.first().split(' ', Qt::SkipEmptyParts);
        if (p.size() >= 3) {
            gotFrom = p[0].toLongLong();
            gotTo   = p[1].toLongLong();
            gotPoints = p[2].toInt();
        }
    }
    res.calFromHz = gotFrom;
    res.calToHz   = gotTo;
    if (gotTo < toHz - 1000) {
        res.calState = QString("clamped to %1–%2 MHz by the instrument")
                           .arg(gotFrom / 1e6, 0, 'f', 3)
                           .arg(gotTo / 1e6, 0, 'f', 3);
    } else {
        res.calState = "uncalibrated (spectrum analyser — amplitude is indicative)";
    }

    emit progress(0, gotPoints, "scanning");
    QThread::msleep(400);

    // `data 0` returns one ASCII dBm value per point. (`scanraw` returns a
    // BINARY blob -- do not parse it as text.)
    const QStringList data = command(sp, "data 0", 40000, 2500);
    const int n = data.size();
    for (int i = 0; i < n && !m_cancel; ++i) {
        bool ok = false;
        const double dbm = data[i].toDouble(&ok);
        if (!ok) continue;
        SweepPoint pt;
        pt.hz = (n > 1) ? gotFrom + (gotTo - gotFrom) * qint64(i) / (n - 1) : gotFrom;
        pt.dbm = dbm;
        res.points.push_back(pt);
    }
    sp.close();

    if (res.points.isEmpty()) res.error = "no spectrum data returned";
    else res.ok = true;
    emit finished(res);
}

} // namespace TciMon
