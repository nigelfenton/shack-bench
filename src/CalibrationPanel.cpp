#include "CalibrationPanel.h"

#include "BuildInfo.h"
#include "CalPlot.h"
#include "TciClient.h"

#include <QCheckBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStringList>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace TciMon {

namespace {

constexpr const char* kCaptionStyle =
    "QLabel { color: #6b8099; font-size: 9px; font-weight: bold; "
    "letter-spacing: 0.08em; }";

// Tone / TCI audio constants — mirror tx_drive_cal.py exactly.
constexpr int    kToneHz    = 1500;     // audio offset; 48000/1500 = 32, loops clean
constexpr double kTonePeak  = 0.999;    // K1 reference — WSJT-X at full Pwr
constexpr int    kAudioRate = 48000;    // TCI TX audio rate
constexpr int    kFrameMs   = 50;       // audio frame streamed per tick
constexpr int    kSettleMs  = 500;      // settle after setting gain/power
constexpr double kPi        = 3.14159265358979323846;

// Append a little-endian uint32 / float32 — TCI audio framing is LE.
void putU32(QByteArray& b, quint32 v)
{
    char d[4] = { char(v & 0xff), char((v >> 8) & 0xff),
                  char((v >> 16) & 0xff), char((v >> 24) & 0xff) };
    b.append(d, 4);
}
void putF32(QByteArray& b, float f)
{
    quint32 u;
    std::memcpy(&u, &f, 4);
    putU32(b, u);
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────

CalibrationPanel::CalibrationPanel(TciClient* tci, QWidget* parent)
    : QWidget(parent),
      m_tci(tci),
      m_step(new QTimer(this)),
      m_audio(new QTimer(this)),
      m_watchdog(new QTimer(this))
{
    m_step->setSingleShot(true);
    m_watchdog->setSingleShot(true);
    m_audio->setInterval(kFrameMs);
    connect(m_step,     &QTimer::timeout, this, &CalibrationPanel::onStep);
    connect(m_audio,    &QTimer::timeout, this, &CalibrationPanel::onAudioTick);
    connect(m_watchdog, &QTimer::timeout, this, &CalibrationPanel::onWatchdog);

    // One second of mono test tone, generated once and looped.
    m_tone.resize(kAudioRate);
    for (int n = 0; n < kAudioRate; ++n)
        m_tone[n] = float(kTonePeak *
            std::sin(2.0 * kPi * kToneHz * n / kAudioRate));

    buildUI();
    refreshBanner();
    restore();
}

// ── UI ────────────────────────────────────────────────────────────────────

void CalibrationPanel::buildUI()
{
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(4, 4, 4, 4);
    v->setSpacing(6);

    // ── Safety banner ──────────────────────────────────────────────────
    m_banner = new QLabel;
    m_banner->setWordWrap(true);
    v->addWidget(m_banner);

    // ── Parameters grid ────────────────────────────────────────────────
    {
        auto* grid = new QGridLayout;
        grid->setHorizontalSpacing(10);
        grid->setVerticalSpacing(2);

        auto cap = [&](const QString& t) {
            auto* l = new QLabel(t);
            l->setStyleSheet(kCaptionStyle);
            return l;
        };

        m_rf = new QSpinBox;
        m_rf->setRange(1, 100);
        m_rf->setValue(100);
        m_rf->setToolTip("SDR RF power level (drive: command).");

        m_alcTarget = new QDoubleSpinBox;
        m_alcTarget->setRange(-25.0, 0.0);
        m_alcTarget->setDecimals(1);
        m_alcTarget->setSingleStep(1.0);
        m_alcTarget->setValue(-10.0);
        m_alcTarget->setSuffix(" dBFS");
        m_alcTarget->setToolTip(
            "Knee = highest tx_gain whose ALC peak stays at/under this.");

        m_dwell = new QDoubleSpinBox;
        m_dwell->setRange(0.5, 5.0);
        m_dwell->setDecimals(1);
        m_dwell->setSingleStep(0.5);
        m_dwell->setValue(2.0);
        m_dwell->setSuffix(" s");
        m_dwell->setToolTip("Telemetry-collection keydown per point.");

        m_cooldown = new QDoubleSpinBox;
        m_cooldown->setRange(0.5, 10.0);
        m_cooldown->setDecimals(1);
        m_cooldown->setSingleStep(0.5);
        m_cooldown->setValue(2.0);
        m_cooldown->setSuffix(" s");
        m_cooldown->setToolTip("RX rest between points (thermal + safety).");

        m_swrLimit = new QDoubleSpinBox;
        m_swrLimit->setRange(1.5, 10.0);
        m_swrLimit->setDecimals(1);
        m_swrLimit->setSingleStep(0.1);
        m_swrLimit->setValue(3.0);
        m_swrLimit->setToolTip("Abort the whole run if SWR exceeds this.");

        m_coarseEdit = new QLineEdit("10,20,30,40,50,60,70,80,90,100");
        m_coarseEdit->setToolTip("Comma list of tx_gain values for the "
                                 "coarse pass. A fine pass auto-runs "
                                 "around the knee.");

        grid->addWidget(cap("RF POWER"),   0, 0);
        grid->addWidget(m_rf,              1, 0);
        grid->addWidget(cap("ALC TARGET"), 0, 1);
        grid->addWidget(m_alcTarget,       1, 1);
        grid->addWidget(cap("DWELL"),      0, 2);
        grid->addWidget(m_dwell,           1, 2);
        grid->addWidget(cap("COOLDOWN"),   0, 3);
        grid->addWidget(m_cooldown,        1, 3);
        grid->addWidget(cap("SWR ABORT >"),0, 4);
        grid->addWidget(m_swrLimit,        1, 4);
        grid->addWidget(cap("COARSE tx_gain SWEEP"), 2, 0, 1, 5);
        grid->addWidget(m_coarseEdit,      3, 0, 1, 5);
        v->addLayout(grid);
    }

    // ── Arm + action row ───────────────────────────────────────────────
    {
        auto* row = new QHBoxLayout;
        m_arm = new QCheckBox("Arm sending");
        m_arm->setToolTip(
            "Unchecked = dry-run (rehearse the sweep, nothing is transmitted).\n"
            "Resets to OFF every launch — never remembered.");
        connect(m_arm, &QCheckBox::toggled,
                this, &CalibrationPanel::onArmToggled);

        m_startBtn = new QPushButton("Start calibration");
        connect(m_startBtn, &QPushButton::clicked,
                this, &CalibrationPanel::onStartClicked);

        m_stopBtn = new QPushButton("STOP / UNKEY NOW");
        m_stopBtn->setEnabled(false);
        m_stopBtn->setStyleSheet(
            "QPushButton { background: #400000; color: #ff8080; "
            "font-weight: bold; border: 1px solid #ff5050; padding: 4px 10px; }"
            "QPushButton:disabled { background: #1a1414; color: #6b5050; "
            "border: 1px solid #3a2a2a; }");
        connect(m_stopBtn, &QPushButton::clicked,
                this, &CalibrationPanel::onStopClicked);

        m_saveBtn = new QPushButton("Save CSV…");
        m_saveBtn->setEnabled(false);
        connect(m_saveBtn, &QPushButton::clicked, this, [this]() {
            if (m_allRows.isEmpty()) return;
            const QString stamp =
                QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
            QSettings s;
            const QString fallback = QStandardPaths::writableLocation(
                QStandardPaths::DocumentsLocation);
            const QString dir =
                s.value("log/lastSaveDir", fallback).toString();
            const QString path = QFileDialog::getSaveFileName(this,
                "Save calibration CSV",
                dir + "/tx-drive-cal-" + stamp + ".csv",
                "CSV files (*.csv);;All files (*)");
            if (path.isEmpty()) return;
            QFile f(path);
            if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate
                        | QIODevice::Text)) {
                QMessageBox::critical(this, "Save failed", f.errorString());
                return;
            }
            QTextStream out(&f);
            // Self-identifying header: when, what built, what server, what
            // came out. Catches the "wrong build" / "AE not transmitting"
            // confusion at a glance months later.
            double maxFwdEver = 0.0;
            for (const Row& r : m_allRows)
                maxFwdEver = std::max(maxFwdEver, r.fwdMax);
            const bool noCarrier = !m_allRows.isEmpty() && maxFwdEver <= 0.0;
            auto orUnknown = [](const QString& s) {
                return s.isEmpty() ? QStringLiteral("(unknown)") : s;
            };
            out << "# TCI Monitor TX-drive calibration\n";
            out << "# run started:      " << m_runStarted << "\n";
            out << "# TCI Monitor:      commit " << TciMon::kBuildGitHash
                << " on " << TciMon::kBuildGitBranch
                << " (dirty=" << TciMon::kBuildGitDirty << ")"
                << ", committed " << TciMon::kBuildGitDate
                << ", compiled " << TciMon::kBuildDate
                << " on " << TciMon::kBuildHostOs << "\n";
            out << "# AetherSDR server: device="   << orUnknown(m_serverDevice)
                << "  protocol="                 << orUnknown(m_serverProtocol)
                << "  software="                 << orUnknown(m_serverSoftware)
                << "\n";
            if (noCarrier) {
                out << "# WARNING: forward power = 0 W across the whole sweep.\n"
                       "#          Radio was keyed but produced no carrier --\n"
                       "#          TX audio is likely not routed. In AetherSDR\n"
                       "#          set the slice's TX source to PC / enable\n"
                       "#          DAX TX. The recommendation below is NOT\n"
                       "#          meaningful.\n";
            }
            if (m_recGain >= 0)
                out << "# recommended tx_gain=" << m_recGain
                    << "  ALC target " << m_alcTargetVal << " dBFS\n";
            else
                out << "# recommended tx_gain=(none)  ALC target "
                    << m_alcTargetVal << " dBFS\n";
            out << "rf_power,tx_gain,n_samples,fwd_avg_w,fwd_max_w,"
                   "swr_avg,alc_avg_dbfs,alc_max_dbfs\n";
            for (const Row& r : m_allRows) {
                out << r.rf << ',' << r.gain << ',' << r.n << ','
                    << QString::number(r.fwdAvg, 'f', 1) << ','
                    << QString::number(r.fwdMax, 'f', 1) << ','
                    << QString::number(r.swrAvg, 'f', 2) << ',';
                if (r.hasAlc)
                    out << QString::number(r.alcAvg, 'f', 1) << ','
                        << QString::number(r.alcMax, 'f', 1) << '\n';
                else
                    out << ",\n";
            }
            f.close();
            s.setValue("log/lastSaveDir", QFileInfo(path).absolutePath());
        });

        row->addWidget(m_arm);
        row->addSpacing(12);
        row->addWidget(m_startBtn);
        row->addWidget(m_stopBtn);
        row->addStretch();
        row->addWidget(m_saveBtn);
        v->addLayout(row);
    }

    // ── Progress + result ──────────────────────────────────────────────
    m_progress = new QLabel("idle");
    m_progress->setStyleSheet(kCaptionStyle);
    v->addWidget(m_progress);

    m_plot = new CalPlot;
    v->addWidget(m_plot, 1);

    m_result = new QLabel("—");
    m_result->setStyleSheet(
        "QLabel { color: #ffd400; font-size: 13px; font-weight: bold; "
        "font-family: Consolas, 'Cascadia Mono', monospace; }");
    v->addWidget(m_result);

    auto* tcap = new QLabel("RUN LOG");
    tcap->setStyleSheet(kCaptionStyle);
    v->addWidget(tcap);

    m_transcript = new QPlainTextEdit;
    m_transcript->setReadOnly(true);
    m_transcript->setMaximumBlockCount(2000);
    m_transcript->setMaximumHeight(150);
    m_transcript->setStyleSheet(
        "QPlainTextEdit { background-color: #050a14; color: #dde6f0; "
        "border: 1px solid #1c2a40; "
        "font-family: Consolas, 'Cascadia Mono', monospace; "
        "font-size: 11px; }");
    v->addWidget(m_transcript);
}

void CalibrationPanel::refreshBanner()
{
    if (m_armed) {
        m_banner->setText("● ARMED — calibration will key the radio and "
                          "stream a test tone. Rig into a DUMMY LOAD, "
                          "amplifier bypassed/off.");
        m_banner->setStyleSheet(
            "QLabel { background:#400000; color:#ff8080; "
            "border:1px solid #ff5050; padding:5px; font-weight:bold; }");
    } else {
        m_banner->setText("○ DRY-RUN — Start rehearses the sweep; nothing is "
                          "transmitted. Tick \"Arm sending\" for a live run.");
        m_banner->setStyleSheet(
            "QLabel { background:#0a1320; color:#6b8099; "
            "border:1px solid #1c2a40; padding:5px; }");
    }
}

void CalibrationPanel::log(const QString& text, const QString& colorHex)
{
    const QString ts = QDateTime::currentDateTime().toString("HH:mm:ss");
    m_transcript->appendHtml(
        QString("<span style='color:#6b8099'>%1</span> "
                "<span style='color:%2'>%3</span>")
            .arg(ts, colorHex, text.toHtmlEscaped()));
}

// ── Arm ─────────────────────────────────────────────────────────────────

void CalibrationPanel::onArmToggled(bool checked)
{
    if (checked) {
        const auto r = QMessageBox::warning(this, "Arm calibration",
            "You are enabling a LIVE calibration run.\n\n"
            "It will key your transmitter repeatedly and stream a "
            "full-scale test tone while sweeping tx_gain.\n\n"
            "Confirm before arming:\n"
            "  • rig into a DUMMY LOAD\n"
            "  • RF2K-S / amplifier bypassed or off\n"
            "  • you are present at the radio\n\n"
            "A watchdog auto-unkeys on SWR fault or keydown overrun, but "
            "you are responsible for the run. Arm now?",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (r != QMessageBox::Yes) {
            m_arm->setChecked(false);   // recurses with checked=false
            return;
        }
    }
    m_armed = checked;
    m_stopBtn->setEnabled(m_armed || m_running);
    refreshBanner();
    log(checked ? "[armed] live calibration enabled"
                : "[safe] dry-run — nothing is transmitted",
        checked ? "#ff8080" : "#6b8099");
}

// ── Start / stop ─────────────────────────────────────────────────────────

void CalibrationPanel::onStartClicked()
{
    if (m_running) return;

    // Parse the coarse tx_gain list.
    QVector<int> coarse;
    for (const QString& tok :
             m_coarseEdit->text().split(',', Qt::SkipEmptyParts)) {
        bool ok = false;
        const int g = tok.trimmed().toInt(&ok);
        if (ok && g >= 0 && g <= 100) coarse.push_back(g);
    }
    if (coarse.size() < 2) {
        QMessageBox::warning(this, "Calibration",
            "Enter at least two coarse tx_gain values (0–100), "
            "comma-separated.");
        return;
    }
    std::sort(coarse.begin(), coarse.end());
    m_coarseStep = coarse.size() >= 2 ? (coarse[1] - coarse[0]) : 10;
    if (m_coarseStep < 2) m_coarseStep = 2;

    m_dry = !m_armed;

    if (!m_dry) {
        if (!m_tci || !m_tci->connected()) {
            QMessageBox::warning(this, "Not connected",
                "Connect to the TCI server before an armed run. "
                "(The radio must be connected in AetherSDR too.)");
            return;
        }
        const auto r = QMessageBox::warning(this, "Start live calibration",
            "This will begin keying the transmitter.\n\n"
            "Rig into a DUMMY LOAD, amplifier bypassed/off, and stay "
            "present. Begin the run?",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (r != QMessageBox::Yes) return;
    }

    m_rfPower      = m_rf->value();
    m_alcTargetVal = m_alcTarget->value();
    m_swrLimitVal  = m_swrLimit->value();
    m_allRows.clear();
    m_coarseRows.clear();
    m_kneeGain = m_recGain = -1;
    m_haveAlc  = false;
    m_runStarted = QDateTime::currentDateTime().toString(Qt::ISODate);
    m_transcript->clear();
    m_result->setText("—");

    log(m_dry ? "DRY RUN — rehearsing sweep, no TX"
              : "*** LIVE — keying TX ***",
        m_dry ? "#6b8099" : "#ff8080");
    log(QString("rf_power=%1  ALC target=%2 dBFS  dwell=%3 s  cooldown=%4 s")
            .arg(m_rfPower).arg(m_alcTargetVal, 0, 'f', 1)
            .arg(m_dwell->value(), 0, 'f', 1)
            .arg(m_cooldown->value(), 0, 'f', 1),
        "#6b8099");

    if (!m_dry && m_tci)
        m_tci->send("tx_sensors_enable:true;");

    setRunning(true);
    startPass(coarse, /*finePass=*/false);
}

void CalibrationPanel::onStopClicked()
{
    if (m_running) {
        abort("manual STOP");
    } else {
        // Emergency unkey even outside a run.
        if (m_tci) {
            m_tci->send("trx:0,false;");
            m_tci->send("tune:0,false;");
        }
        log("manual unkey — sent trx:0,false; tune:0,false;", "#ff5050");
    }
}

// ── Sweep state machine ──────────────────────────────────────────────────

void CalibrationPanel::startPass(const QVector<int>& gains, bool finePass)
{
    m_finePass = finePass;
    m_queue    = gains;
    m_passRows.clear();
    QStringList gs;
    for (int g : gains) gs << QString::number(g);
    log(QString("=== %1 pass — tx_gain %2 ===")
            .arg(finePass ? "fine" : "coarse", gs.join(',')),
        "#00d8ef");
    nextPoint();
}

void CalibrationPanel::nextPoint()
{
    if (m_queue.isEmpty()) { finishPass(); return; }
    m_curGain = m_queue.takeFirst();
    beginSettle();
}

void CalibrationPanel::beginSettle()
{
    m_phase = Phase::Settle;
    m_progress->setText(QString("%1 pass — point tx_gain=%2  (rf %3)")
                            .arg(m_finePass ? "fine" : "coarse")
                            .arg(m_curGain).arg(m_rfPower));
    if (m_dry) {
        log(QString("  [DRY] would send drive:%1; tx_gain:%2;")
                .arg(m_rfPower).arg(m_curGain), "#ffaa00");
    } else if (m_tci) {
        m_tci->send(QString("drive:%1;").arg(m_rfPower));
        m_tci->send(QString("tx_gain:%1;").arg(m_curGain));
    }
    m_step->start(kSettleMs);
}

void CalibrationPanel::onStep()
{
    switch (m_phase) {
    case Phase::Settle:   beginKeydown(); break;
    case Phase::Keyed:    endKeydown();   break;
    case Phase::Cooldown: nextPoint();    break;
    case Phase::Idle:     break;
    }
}

void CalibrationPanel::beginKeydown()
{
    m_sFwd.clear();
    m_sSwr.clear();
    m_sAlc.clear();
    m_phase = Phase::Keyed;

    const int dwellMs = int(m_dwell->value() * 1000.0);
    if (m_dry) {
        log(QString("  [DRY] tx_gain=%1 — would key + stream %2 Hz tone")
                .arg(m_curGain).arg(kToneHz), "#ffaa00");
        m_step->start(std::min(dwellMs, 400));
        return;
    }
    if (m_tci) m_tci->send("trx:0,true;");
    m_streaming = true;                       // tone streamer begins
    // Hard ceiling: dwell + 2 s margin. Normal path stops it in endKeydown.
    m_watchdog->start(dwellMs + 2000);
    m_step->start(dwellMs);
}

void CalibrationPanel::endKeydown()
{
    unkey();                                  // clears streaming, stops watchdog

    const Row r = recordPoint();
    m_passRows.push_back(r);
    m_allRows.push_back(r);
    redrawPlot();

    // #2950 guard — checked on the first measured live point.
    if (!m_dry && m_allRows.size() == 1) {
        if (r.n == 0) {
            abort("no tx_sensors telemetry — is the radio connected "
                  "and keying?");
            return;
        }
        if (!m_haveAlc) {
            abort("no `alc` field in tx_sensors — this AetherSDR build "
                  "predates PR #2950; knee detection needs it");
            return;
        }
    }

    m_phase = Phase::Cooldown;
    m_step->start(int(m_cooldown->value() * 1000.0));
}

CalibrationPanel::Row CalibrationPanel::recordPoint()
{
    Row r;
    r.rf   = m_rfPower;
    r.gain = m_curGain;
    r.n    = m_sFwd.size();
    if (r.n == 0) {
        log(QString("  rf=%1 tx_gain=%2: no telemetry")
                .arg(m_rfPower).arg(m_curGain),
            m_dry ? "#6b8099" : "#ff5050");
        return r;
    }
    double fSum = 0.0, sSum = 0.0;
    for (double x : m_sFwd) { fSum += x; r.fwdMax = std::max(r.fwdMax, x); }
    for (double x : m_sSwr) sSum += x;
    r.fwdAvg = fSum / m_sFwd.size();
    r.swrAvg = sSum / double(m_sSwr.isEmpty() ? 1 : m_sSwr.size());
    if (!m_sAlc.isEmpty()) {
        double aSum = 0.0;
        r.alcMax = -1e9;
        for (double x : m_sAlc) { aSum += x; r.alcMax = std::max(r.alcMax, x); }
        r.alcAvg = aSum / m_sAlc.size();
        r.hasAlc = true;
    }
    log(QString("  rf=%1 tx_gain=%2  fwd=%3 W  swr=%4  alc_peak=%5  n=%6")
            .arg(m_rfPower).arg(m_curGain)
            .arg(r.fwdAvg, 0, 'f', 1).arg(r.swrAvg, 0, 'f', 2)
            .arg(r.hasAlc ? QString::number(r.alcMax, 'f', 1)
                          : QStringLiteral("—"))
            .arg(r.n),
        "#dde6f0");
    return r;
}

void CalibrationPanel::finishPass()
{
    if (!m_finePass) {
        m_coarseRows = m_passRows;
        const int knee = kneeGain(m_coarseRows);
        if (knee < 0) {
            log(QString("no point at/under ALC target %1 dBFS — knee not "
                        "found in the coarse range").arg(m_alcTargetVal, 0, 'f', 1),
                "#ff5050");
            concludeRun(-1);
            return;
        }
        m_kneeGain = knee;
        log(QString("coarse knee at tx_gain=%1").arg(knee), "#4cff7c");
        redrawPlot();

        // Fine re-sweep: ± one coarse step around the knee, step 2.
        const int lo = std::max(0,   knee - m_coarseStep);
        const int hi = std::min(100, knee + m_coarseStep);
        QVector<int> fine;
        for (int g = lo; g <= hi; g += 2) fine.push_back(g);
        startPass(fine, /*finePass=*/true);
        return;
    }

    // Fine pass done — refine the knee, fall back to the coarse one.
    int knee = kneeGain(m_passRows);
    if (knee < 0) knee = kneeGain(m_coarseRows);
    m_kneeGain = knee;
    concludeRun(knee);
}

void CalibrationPanel::concludeRun(int recGain)
{
    // Sanity check: every point with fwd=0 means the radio was keyed but no
    // audio reached the modulator (the ALC "knee" is then just the noise
    // floor and the recommendation would be meaningless). Refuse it.
    double maxFwdEver = 0.0;
    for (const Row& r : m_allRows)
        maxFwdEver = std::max(maxFwdEver, r.fwdMax);
    const bool noCarrier = !m_allRows.isEmpty() && maxFwdEver <= 0.0;
    if (noCarrier) recGain = -1;

    m_recGain = recGain;
    setRunning(false);
    m_progress->setText("done");
    redrawPlot();

    if (noCarrier) {
        m_result->setText("No carrier produced (fwd = 0 W across the sweep) "
                          "— check TX audio routing in AetherSDR "
                          "(slice TX source / DAX TX).");
        log("WARNING — forward power = 0 W across the whole sweep; "
            "no recommendation made", "#ff5050");
    } else if (recGain >= 0) {
        // Pull the recommended point's telemetry for the summary.
        QString detail;
        for (const Row& r : m_allRows) {
            if (r.gain == recGain) {
                detail = QString("  →  fwd ≈ %1 W, ALC peak ≈ %2 dBFS")
                             .arg(r.fwdAvg, 0, 'f', 1)
                             .arg(r.hasAlc ? QString::number(r.alcMax, 'f', 1)
                                           : QStringLiteral("—"));
            }
        }
        m_result->setText(
            QString("Recommended tx_gain = %1%2").arg(recGain).arg(detail));
        log(QString("RESULT — recommended tx_gain=%1").arg(recGain), "#ffd400");
    } else {
        m_result->setText("No recommendation — widen the sweep or raise the "
                           "ALC target.");
        log("run finished — no knee found", "#ffaa00");
    }
    if (!m_allRows.isEmpty()) m_saveBtn->setEnabled(true);
    persist();
}

void CalibrationPanel::abort(const QString& reason)
{
    m_step->stop();
    m_watchdog->stop();
    unkey();
    log(QString("ABORT (%1) — sent trx:0,false; tune:0,false;").arg(reason),
        "#ff5050");
    m_phase = Phase::Idle;
    setRunning(false);
    m_progress->setText(QString("aborted — %1").arg(reason));
    if (!m_allRows.isEmpty()) {
        m_saveBtn->setEnabled(true);
        redrawPlot();
    }
}

void CalibrationPanel::unkey()
{
    m_streaming = false;
    m_watchdog->stop();
    if (m_tci && !m_dry) {
        m_tci->send("trx:0,false;");
        m_tci->send("tune:0,false;");
    }
}

void CalibrationPanel::onWatchdog()
{
    abort("watchdog — keydown exceeded its ceiling");
}

// ── Tone streamer ────────────────────────────────────────────────────────

void CalibrationPanel::onAudioTick()
{
    if (!m_streaming || m_dry || !m_tci) return;

    const int frameMono = kAudioRate * kFrameMs / 1000;   // 2400
    QByteArray f;
    f.reserve(64 + frameMono * 2 * 4);
    // 64-byte TciAudioHeader: receiver, sampleRate, format, codec, crc,
    // length, type, channels, reserved[8].
    putU32(f, 0);                        // receiver
    putU32(f, kAudioRate);               // sampleRate
    putU32(f, 3);                        // format = float32
    putU32(f, 0);                        // codec = uncompressed
    putU32(f, 0);                        // crc
    putU32(f, quint32(frameMono * 2));   // length = total floats
    putU32(f, 2);                        // type = 2 (TX_AUDIO)
    putU32(f, 2);                        // channels = stereo
    for (int i = 0; i < 8; ++i) putU32(f, 0);   // reserved[8]
    for (int i = 0; i < frameMono; ++i) {
        const float s = m_tone[(m_tonePos + i) % kAudioRate];
        putF32(f, s);   // L
        putF32(f, s);   // R (duplicated stereo, as WSJT-X sends)
    }
    m_tonePos = (m_tonePos + frameMono) % kAudioRate;
    m_tci->sendBinary(f);
}

// ── Telemetry intake ─────────────────────────────────────────────────────

void CalibrationPanel::noteIncoming(const QString& line)
{
    const int colon = line.indexOf(':');
    if (colon < 0) return;
    const QString cmd = line.left(colon).trimmed().toLower();

    // Capture the server-identification greeting (sent once at connect).
    // We remember the latest value of each so the saved CSV can say exactly
    // which AetherSDR build produced the data.
    if (cmd == "software" || cmd == "protocol" || cmd == "device") {
        const QString val = line.mid(colon + 1).trimmed();
        if      (cmd == "software") m_serverSoftware = val;
        else if (cmd == "protocol") m_serverProtocol = val;
        else if (cmd == "device")   m_serverDevice   = val;
        return;
    }

    if (cmd != "tx_sensors") return;

    // tx_sensors:trx,mic_dbm,fwd_watts,peak_watts,swr[,alc_dbfs]
    const QStringList a = line.mid(colon + 1).split(',');
    if (a.size() < 5 || a[0].trimmed() != "0") return;

    bool okF = false, okS = false;
    const double fwd = a[2].trimmed().toDouble(&okF);
    const double swr = a[4].trimmed().toDouble(&okS);
    if (!okF || !okS) return;

    bool   hasAlc = false;
    double alc    = 0.0;
    if (a.size() > 5) {
        bool okA = false;
        alc = a[5].trimmed().toDouble(&okA);
        if (okA) { hasAlc = true; m_haveAlc = true; }
    }

    if (m_phase != Phase::Keyed) return;

    // SWR fault aborts the whole run.
    if (swr > m_swrLimitVal && m_sSwr.size() >= 1) {
        abort(QString("SWR %1 > %2")
                  .arg(swr, 0, 'f', 2).arg(m_swrLimitVal, 0, 'f', 1));
        return;
    }
    m_sFwd.push_back(fwd);
    m_sSwr.push_back(swr);
    if (hasAlc) m_sAlc.push_back(alc);
}

// ── Knee + plot ──────────────────────────────────────────────────────────

int CalibrationPanel::kneeGain(const QVector<Row>& rows) const
{
    int best = -1;
    for (const Row& r : rows)
        if (r.hasAlc && r.alcMax <= m_alcTargetVal && r.gain > best)
            best = r.gain;
    return best;
}

void CalibrationPanel::redrawPlot()
{
    // Dedupe by tx_gain — a fine-pass point supersedes the coarse one.
    QMap<int, Row> byGain;
    for (const Row& r : m_allRows)
        if (r.n > 0) byGain.insert(r.gain, r);

    QVector<CalPlot::Point> pts;
    for (auto it = byGain.begin(); it != byGain.end(); ++it) {
        CalPlot::Point p;
        p.gain   = it.value().gain;
        p.fwd    = it.value().fwdAvg;
        p.alc    = it.value().alcMax;
        p.hasAlc = it.value().hasAlc;
        pts.push_back(p);
    }
    m_plot->setData(pts, m_kneeGain, m_recGain, m_alcTargetVal);
}

void CalibrationPanel::setRunning(bool running)
{
    m_running = running;
    m_startBtn->setEnabled(!running);
    m_stopBtn->setEnabled(running || m_armed);
    m_arm->setEnabled(!running);
    m_rf->setEnabled(!running);
    m_alcTarget->setEnabled(!running);
    m_dwell->setEnabled(!running);
    m_cooldown->setEnabled(!running);
    m_swrLimit->setEnabled(!running);
    m_coarseEdit->setEnabled(!running);

    if (running) {
        if (!m_dry) { m_tonePos = 0; m_audio->start(); }
    } else {
        m_audio->stop();
        m_streaming = false;
        m_step->stop();
        m_watchdog->stop();
        m_phase = Phase::Idle;
    }
}

// ── Persistence ──────────────────────────────────────────────────────────

void CalibrationPanel::persist()
{
    QSettings s;
    s.beginGroup("txcal");
    s.remove("");
    QStringList rows;
    for (const Row& r : m_allRows) {
        rows << QString("%1;%2;%3;%4;%5;%6;%7;%8;%9")
                    .arg(r.rf).arg(r.gain).arg(r.n)
                    .arg(r.fwdAvg, 0, 'f', 2).arg(r.fwdMax, 0, 'f', 2)
                    .arg(r.swrAvg, 0, 'f', 3)
                    .arg(r.alcAvg, 0, 'f', 2).arg(r.alcMax, 0, 'f', 2)
                    .arg(r.hasAlc ? 1 : 0);
    }
    s.setValue("rows", rows);
    s.setValue("knee", m_kneeGain);
    s.setValue("rec",  m_recGain);
    s.setValue("alcTarget", m_alcTargetVal);
    s.endGroup();
}

void CalibrationPanel::restore()
{
    QSettings s;
    s.beginGroup("txcal");
    const QStringList rows = s.value("rows").toStringList();
    m_kneeGain     = s.value("knee", -1).toInt();
    m_recGain      = s.value("rec",  -1).toInt();
    m_alcTargetVal = s.value("alcTarget", -10.0).toDouble();
    s.endGroup();

    for (const QString& line : rows) {
        const QStringList f = line.split(';');
        if (f.size() < 9) continue;
        Row r;
        r.rf     = f[0].toInt();
        r.gain   = f[1].toInt();
        r.n      = f[2].toInt();
        r.fwdAvg = f[3].toDouble();
        r.fwdMax = f[4].toDouble();
        r.swrAvg = f[5].toDouble();
        r.alcAvg = f[6].toDouble();
        r.alcMax = f[7].toDouble();
        r.hasAlc = f[8].toInt() != 0;
        m_allRows.push_back(r);
    }
    if (!m_allRows.isEmpty()) {
        m_saveBtn->setEnabled(true);
        if (m_recGain >= 0)
            m_result->setText(
                QString("Last run — recommended tx_gain = %1").arg(m_recGain));
        redrawPlot();
        log(QString("restored last calibration — %1 points")
                .arg(m_allRows.size()), "#6b8099");
    }
}

} // namespace TciMon
