#pragma once

// Scope — Hantek DSO2D15 over USBTMC.
//
// Unlike the three serial instruments this one is NOT a COM port: it
// enumerates as a USB Test & Measurement device (VID_049F/PID_505E) and is
// driven through a VISA library. Shack-Bench loads visa32/visa64 at runtime
// rather than linking it, so the app still builds and runs on a machine with
// no VISA installed — the Scope tab simply reports that it is unavailable.
//
// ⭐ Three firmware quirks, each of which produced a wrong conclusion during
// bring-up on 2026-08-03 and all of which this driver works around:
//
//  1. LONG-FORM KEYWORDS ONLY. ":CHANnel1:SCALe?" answers; ":CHAN1:SCAL?"
//     mostly does not. Abbreviating made 45 commands look unimplemented.
//
//  2. AN UNKNOWN COMMAND REPLIES "Undefined header" AND THAT REPLY QUEUES.
//     The scope does not stay silent, so treating a timeout as "not
//     implemented" and moving on makes the stale reply the answer to the NEXT
//     query — every result after it is off by one. A known-good query came
//     back as ";" that way. Always drain after a failure.
//
//  3. THE FIRST QUERY AFTER CONNECT USUALLY ERRORS. Send a throwaway *IDN?
//     and ignore the result before doing anything real.
//
// ⛔ Firmware 3.0.1 implements NO ":MEASure:" value queries (VPP, FREQuency,
// VRMS, PERiod...) — verified with parameters, after setting MEASure:SOURce,
// and in clean per-query sessions. Measurements are therefore COMPUTED from
// the captured waveform, which is what the reference toolkit does too.

#include <QObject>
#include <QString>
#include <QVector>

namespace TciMon {

struct ScopeChannel {
    int             index = 1;
    bool            enabled = false;
    double          voltsPerDiv = 0;
    double          offsetV = 0;
    double          probe = 1;
    QString         coupling;
    QVector<double> volts;      // converted samples

    // Computed here because the instrument cannot report them.
    double vpp = 0;
    double vmin = 0;
    double vmax = 0;
    double vrms = 0;
    double vmean = 0;
    double freqHz = 0;          // 0 when no clean crossings were found
};

struct ScopeCapture {
    bool                  ok = false;
    QString               error;
    QString               idn;
    double                secondsPerDiv = 0;
    double                sampleRateHz = 0;
    int                   pointsRequested = 0;
    QString               triggerMode;
    QString               triggerSweep;
    QVector<ScopeChannel> channels;
    QString               takenAtIso;

    double secondsPerSample() const
    {
        return sampleRateHz > 0 ? 1.0 / sampleRateHz : 0.0;
    }
};

// Is a VISA runtime present on this machine?
bool scopeVisaAvailable(QString* detail = nullptr);

// Find a DSO2D15-class scope. Empty if none.
QString findScopeResource(QString* log = nullptr);

class ScopeWorker : public QObject {
    Q_OBJECT
public:
    explicit ScopeWorker(QObject* parent = nullptr) : QObject(parent) {}

public slots:
    // Read settings and one capture from both channels.
    void capture(const QString& resource);

signals:
    void progress(const QString& note);
    void finished(const TciMon::ScopeCapture& result);
};

// Exposed for testing: decode the interleaved payload into per-channel samples.
// Channels are stored as 2000-byte blocks, round-robin:
//   |ch1 block|ch2 block|ch1 block|ch2 block|...
// Samples are SIGNED bytes at 25 counts per division, so
//   volts = raw / 25 * voltsPerDiv - offset
QVector<double> decodeChannel(const QByteArray& payload, int channelIndex,
                              int enabledChannels, double voltsPerDiv,
                              double offsetV, int blockLen = 2000);

// Compute what the firmware will not report.
void computeMeasurements(ScopeChannel* ch, double secondsPerSample);

} // namespace TciMon
