#include "Scope.h"

#include <QDateTime>
#include <QLibrary>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace TciMon {

namespace {

// ---- Minimal VISA binding, resolved at runtime -------------------------
// Linking visa would make Shack-Bench refuse to start on a machine without a
// VISA runtime. Resolve the half-dozen entry points we need instead, and
// degrade to "Scope unavailable" when they are missing.

using ViSession = quint32;
using ViStatus  = qint32;

typedef ViStatus (*Fn_viOpenDefaultRM)(ViSession*);
typedef ViStatus (*Fn_viOpen)(ViSession, const char*, quint32, quint32, ViSession*);
typedef ViStatus (*Fn_viClose)(ViSession);
typedef ViStatus (*Fn_viWrite)(ViSession, const uchar*, quint32, quint32*);
typedef ViStatus (*Fn_viRead)(ViSession, uchar*, quint32, quint32*);
// ⚠ ViAttrState is ViUInt64 on a 64-bit VISA build and ViUInt32 on 32-bit
// (visa.h keys off _VISA_ENV_IS_64_BIT). We build 64-bit, so it is 64.
typedef ViStatus (*Fn_viSetAttribute)(ViSession, quint32, quint64);
typedef ViStatus (*Fn_viFindRsrc)(ViSession, const char*, ViSession*, quint32*, char*);
typedef ViStatus (*Fn_viFindNext)(ViSession, char*);
typedef ViStatus (*Fn_viClear)(ViSession);

struct Visa {
    QLibrary lib;
    bool loaded = false;
    Fn_viOpenDefaultRM openRM = nullptr;
    Fn_viOpen open = nullptr;
    Fn_viClose close = nullptr;
    Fn_viWrite write = nullptr;
    Fn_viRead read = nullptr;
    Fn_viSetAttribute setAttr = nullptr;
    Fn_viFindRsrc findRsrc = nullptr;
    Fn_viFindNext findNext = nullptr;
    Fn_viClear clear = nullptr;
    QString detail;
};

constexpr quint32 kAttrTmoValue = 0x3FFF001A;

Visa& visa()
{
    static Visa v;
    static bool tried = false;
    if (tried) return v;
    tried = true;

    const QStringList candidates = {"visa64", "visa32", "librsvisa", "libvisa"};
    for (const QString& name : candidates) {
        v.lib.setFileName(name);
        if (!v.lib.load()) continue;
        v.openRM   = (Fn_viOpenDefaultRM) v.lib.resolve("viOpenDefaultRM");
        v.open     = (Fn_viOpen)          v.lib.resolve("viOpen");
        v.close    = (Fn_viClose)         v.lib.resolve("viClose");
        v.write    = (Fn_viWrite)         v.lib.resolve("viWrite");
        v.read     = (Fn_viRead)          v.lib.resolve("viRead");
        v.setAttr  = (Fn_viSetAttribute)  v.lib.resolve("viSetAttribute");
        v.findRsrc = (Fn_viFindRsrc)      v.lib.resolve("viFindRsrc");
        v.findNext = (Fn_viFindNext)      v.lib.resolve("viFindNext");
        v.clear    = (Fn_viClear)         v.lib.resolve("viClear");
        if (v.openRM && v.open && v.close && v.write && v.read) {
            v.loaded = true;
            v.detail = QString("%1 loaded").arg(v.lib.fileName());
            return v;
        }
        v.lib.unload();
    }
    v.detail = "no VISA runtime found (looked for visa64 / visa32). Install "
               "NI-VISA or Keysight IO Libraries to use the scope.";
    return v;
}

// One session's worth of talking to the instrument.
class Session {
public:
    explicit Session(const QString& resource) { openIt(resource); }
    ~Session()
    {
        if (m_inst && visa().close) visa().close(m_inst);
        if (m_rm && visa().close) visa().close(m_rm);
    }

    bool ok() const { return m_inst != 0; }
    QString error() const { return m_err; }

    void setTimeout(quint32 ms)
    {
        if (m_inst && visa().setAttr) visa().setAttr(m_inst, kAttrTmoValue, ms);
    }

    bool writeLine(const QString& cmd)
    {
        if (!m_inst) return false;
        const QByteArray b = (cmd + "\n").toLatin1();
        quint32 n = 0;
        return visa().write(m_inst, (const uchar*)b.constData(),
                            quint32(b.size()), &n) >= 0;
    }

    QByteArray readRaw(int cap = 8 * 1024 * 1024)
    {
        QByteArray out;
        if (!m_inst) return out;
        QByteArray chunk(65536, Qt::Uninitialized);
        // ⚠ Status semantics matter here and getting them backwards hangs or
        // faults the process:
        //   VI_SUCCESS (0)              — the transfer ENDED (terminator/END).
        //   VI_SUCCESS_MAX_CNT (0x3FFF0006) — the buffer filled, MORE is waiting.
        //   negative                    — an error; stop.
        // So keep reading ONLY on MAX_CNT. An earlier version looped while
        // st == 0, which re-read after a completed transfer.
        constexpr ViStatus kMaxCnt = 0x3FFF0006;
        for (int guard = 0; guard < 512 && out.size() < cap; ++guard) {
            quint32 n = 0;
            const ViStatus st = visa().read(m_inst, (uchar*)chunk.data(),
                                            quint32(chunk.size()), &n);
            if (n > 0) out.append(chunk.constData(), int(n));
            if (st != kMaxCnt) break;      // ended, or errored
        }
        return out;
    }

    // Query, returning an empty string on failure. ⭐ Drains afterwards on
    // failure so a queued "Undefined header" cannot become the next answer.
    QString query(const QString& cmd)
    {
        if (!writeLine(cmd)) { drain(); return QString(); }
        const QByteArray r = readRaw(65536);
        const QString s = QString::fromLatin1(r).trimmed();
        if (s.isEmpty() ||
            (s.contains("undefined", Qt::CaseInsensitive) &&
             s.contains("header", Qt::CaseInsensitive))) {
            drain();
            return QString();
        }
        return s;
    }

    void drain()
    {
        if (!m_inst) return;
        for (int i = 0; i < 4; ++i) {
            QByteArray chunk(4096, Qt::Uninitialized);
            quint32 n = 0;
            const ViStatus st = visa().read(m_inst, (uchar*)chunk.data(), 4096, &n);
            if (st < 0 || n == 0) break;
        }
        if (visa().clear) visa().clear(m_inst);
        QThread::msleep(120);
    }

private:
    void openIt(const QString& resource)
    {
        Visa& v = visa();
        if (!v.loaded) { m_err = v.detail; return; }
        if (v.openRM(&m_rm) < 0) { m_err = "viOpenDefaultRM failed"; return; }
        const QByteArray r = resource.toLatin1();
        if (v.open(m_rm, r.constData(), 0, 5000, &m_inst) < 0) {
            m_err = QString("cannot open %1").arg(resource);
            m_inst = 0;
            return;
        }
        setTimeout(6000);
        // ⭐ The first query after connect usually errors. Burn one.
        writeLine("*IDN?");
        readRaw(4096);
        QThread::msleep(300);
    }

    ViSession m_rm = 0;
    ViSession m_inst = 0;
    QString m_err;
};

} // namespace

bool scopeVisaAvailable(QString* detail)
{
    Visa& v = visa();
    if (detail) *detail = v.detail;
    return v.loaded;
}

QString findScopeResource(QString* log)
{
    Visa& v = visa();
    if (!v.loaded) {
        if (log) *log = v.detail;
        return QString();
    }
    if (!v.findRsrc || !v.findNext) {
        // Fall back to the known VID/PID if enumeration is unavailable.
        if (log) *log = "VISA has no viFindRsrc; trying the known USB id";
        return "USB0::0x049F::0x505E::?*::INSTR";
    }
    ViSession rm = 0;
    if (v.openRM(&rm) < 0) return QString();
    ViSession find = 0;
    quint32 count = 0;
    // ⚠ VISA writes the resource string into this buffer and the spec requires
    // it to hold VI_FIND_BUFLEN (256) bytes. Give it more than that and zero
    // it, rather than trusting the library to bound its own write.
    char desc[1024];
    std::memset(desc, 0, sizeof(desc));
    QString hit;
    if (v.findRsrc(rm, "USB?*INSTR", &find, &count, desc) >= 0 && count > 0) {
        for (quint32 i = 0; i < count; ++i) {
            const QString r = QString::fromLatin1(desc);
            // 0x049F is Hantek's USBTMC vendor id for this family.
            if (r.contains("049F", Qt::CaseInsensitive)) {
                hit = r;
                break;
            }
            if (i + 1 >= count || !v.findNext) break;
            std::memset(desc, 0, sizeof(desc));
            if (v.findNext(find, desc) < 0) break;
        }
        if (find && v.close) v.close(find);
    }
    if (rm && v.close) v.close(rm);
    if (log) *log = hit.isEmpty() ? QString("no Hantek scope among %1 USB "
                                            "instrument(s)").arg(count)
                                  : QString("found %1").arg(hit);
    return hit;
}

QVector<double> decodeChannel(const QByteArray& payload, int channelIndex,
                              int enabledChannels, double voltsPerDiv,
                              double offsetV, int blockLen)
{
    QVector<double> out;
    if (payload.isEmpty() || enabledChannels < 1 || channelIndex < 1) return out;

    // Blocks round-robin between the enabled channels.
    const int stride = blockLen * enabledChannels;
    const int size = int(payload.size());
    for (int start = (channelIndex - 1) * blockLen; start < size;
         start += stride) {
        const int n = std::min(blockLen, size - start);
        for (int k = 0; k < n; ++k) {
            const qint8 raw = qint8(payload.at(start + k));
            // 25 counts per division, then subtract the channel offset.
            out << (double(raw) / 25.0 * voltsPerDiv - offsetV);
        }
    }
    return out;
}

void computeMeasurements(ScopeChannel* ch, double secondsPerSample)
{
    if (!ch || ch->volts.isEmpty()) return;
    const auto& v = ch->volts;

    auto mm = std::minmax_element(v.begin(), v.end());
    ch->vmin = *mm.first;
    ch->vmax = *mm.second;
    ch->vpp = ch->vmax - ch->vmin;

    double sum = 0, sumSq = 0;
    for (double x : v) { sum += x; sumSq += x * x; }
    ch->vmean = sum / v.size();
    ch->vrms = std::sqrt(sumSq / v.size());

    // Frequency by mid-level crossings. Use the MEAN as the threshold with a
    // hysteresis band, so a noisy flat trace does not produce a confident
    // nonsense frequency — which is exactly what a naive zero-crossing count
    // does on an unconnected probe.
    const double band = std::max(ch->vpp * 0.15, 1e-6);
    if (ch->vpp < 1e-4 || secondsPerSample <= 0) { ch->freqHz = 0; return; }

    const double hi = ch->vmean + band * 0.5;
    const double lo = ch->vmean - band * 0.5;
    int crossings = 0;
    int firstIdx = -1, lastIdx = -1;
    bool above = v.first() > ch->vmean;
    for (int i = 1; i < v.size(); ++i) {
        if (!above && v[i] > hi) {
            above = true;
            ++crossings;
            if (firstIdx < 0) firstIdx = i;
            lastIdx = i;
        } else if (above && v[i] < lo) {
            above = false;
        }
    }
    if (crossings >= 2 && lastIdx > firstIdx) {
        const double periods = crossings - 1;
        const double seconds = (lastIdx - firstIdx) * secondsPerSample;
        ch->freqHz = seconds > 0 ? periods / seconds : 0;
    } else {
        ch->freqHz = 0;
    }
}

void ScopeWorker::capture(const QString& resource)
{
    ScopeCapture out;
    out.takenAtIso = QDateTime::currentDateTime().toString(Qt::ISODate);

    Session s(resource);
    if (!s.ok()) {
        out.error = s.error();
        emit finished(out);
        return;
    }

    emit progress("reading settings");
    out.idn = s.query("*IDN?");
    // ⭐ LONG-FORM keywords. The abbreviated spellings mostly fail.
    out.secondsPerDiv = s.query(":TIMebase:SCALe?").toDouble();
    out.sampleRateHz  = s.query(":ACQuire:SRATe?").toDouble();
    out.pointsRequested = s.query("ACQuire:POINts?").toInt();
    out.triggerMode  = s.query(":TRIGger:MODE?");
    out.triggerSweep = s.query(":TRIGger:SWEep?");

    int enabled = 0;
    for (int c = 1; c <= 2; ++c) {
        ScopeChannel ch;
        ch.index = c;
        ch.enabled = s.query(QString(":CHANnel%1:DISPlay?").arg(c)) == "1";
        ch.voltsPerDiv = s.query(QString(":CHANnel%1:SCALe?").arg(c)).toDouble();
        ch.offsetV = s.query(QString(":CHANnel%1:OFFSet?").arg(c)).toDouble();
        ch.probe = s.query(QString(":CHANnel%1:PROBe?").arg(c)).toDouble();
        ch.coupling = s.query(QString(":CHANnel%1:COUPling?").arg(c));
        if (ch.enabled) ++enabled;
        out.channels << ch;
    }
    if (enabled == 0) {
        out.error = "no channel is enabled on the scope";
        emit finished(out);
        return;
    }

    emit progress("fetching waveform");
    s.setTimeout(20000);
    if (!s.writeLine("PRIVate:WAVeform:DATA:ALL?")) {
        out.error = "the waveform request was refused";
        emit finished(out);
        return;
    }
    const QByteArray buf = s.readRaw();
    if (buf.size() < 129 || buf.at(0) != '#' || buf.at(1) != '9') {
        out.error = QString("unexpected waveform reply (%1 bytes, header %2)")
                        .arg(buf.size())
                        .arg(QString::fromLatin1(buf.left(2)));
        emit finished(out);
        return;
    }

    // #9 + 9-digit block length + 9-digit total + 9-digit cursor, then the
    // metadata runs to byte 128 and the samples follow.
    const QByteArray payload = buf.mid(128);
    for (auto& ch : out.channels) {
        if (!ch.enabled) continue;
        ch.volts = decodeChannel(payload, ch.index, enabled, ch.voltsPerDiv,
                                 ch.offsetV);
        computeMeasurements(&ch, out.secondsPerSample());
    }

    out.ok = true;
    emit finished(out);
}

} // namespace TciMon
