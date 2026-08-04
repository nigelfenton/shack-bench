#include "InstrumentPanel.h"

#include "TciClient.h"
#include "TracePlot.h"

#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTextStream>
#include <QThread>
#include <QVBoxLayout>
#include <cmath>

namespace TciMon {

namespace {

struct BandPreset { const char* name; double fromMhz; double toMhz; };

// HF ham bands, plus a wide "all HF" scan. The trapped-vertical case the
// design sheet is built around wants one dip per band.
const BandPreset kBands[] = {
    {"All HF 2-30",  2.000,  30.000},
    {"160m",         1.800,   2.000},
    {"80m",          3.500,   4.000},
    {"60m",          5.330,   5.410},
    {"40m",          7.000,   7.300},
    {"30m",         10.100,  10.150},
    {"20m",         14.000,  14.350},
    {"17m",         18.068,  18.168},
    {"15m",         21.000,  21.450},
    {"12m",         24.890,  24.990},
    {"10m",         28.000,  29.700},
    {"6m",          50.000,  54.000},
};

struct SpanPreset { const char* name; double fromMhz; double toMhz; };
// The unit here is the ORIGINAL tinySA: usable 0.1-350 MHz. Anything above is
// clamped silently by the instrument, so do not offer spans it cannot honour.
const SpanPreset kSpans[] = {
    {"HF 0.1-30",      0.100,   30.000},
    {"Full 0.1-350",   0.100,  350.000},
    {"6m 50-54",      50.000,   54.000},
    {"FM 88-108",     88.000,  108.000},
    {"2m 144-148",   144.000,  148.000},
    {"70cm 430-440", 430.000,  440.000},
};

const QColor kLive   ("#4fc3f7");
const QColor kRefer  ("#ffb454");
const QColor kR      ("#7ee787");
const QColor kX      ("#ff7b72");
const QColor kAe     ("#d2a8ff");

QVector<TracePlot::Span> hamBandSpans()
{
    QVector<TracePlot::Span> out;
    for (const auto& b : kBands) {
        if (QString(b.name).startsWith("All")) continue;
        TracePlot::Span s;
        s.fromHz = qint64(b.fromMhz * 1e6);
        s.toHz   = qint64(b.toMhz   * 1e6);
        s.color  = QColor(80, 140, 255, 22);
        s.label  = b.name;
        out << s;
    }
    return out;
}

} // namespace

InstrumentPanel::InstrumentPanel(TciClient* tci, QWidget* parent)
    : QWidget(parent), m_tci(tci)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    // Top strip: instrument discovery, shared by both tabs.
    auto* top = new QHBoxLayout();
    auto* probe = new QPushButton("Probe instruments");
    connect(probe, &QPushButton::clicked, this, &InstrumentPanel::onProbeClicked);
    top->addWidget(probe);
    m_cursorLabel = new QLabel("—");
    m_cursorLabel->setStyleSheet("color:#8fb8ff; font-family:Consolas;");
    top->addWidget(m_cursorLabel, 1);
    root->addLayout(top);

    m_tabs = new QTabWidget();
    buildAntennaTab();
    buildFeedlineTab();
    buildSpectrumTab();
    buildScopeTab();
    root->addWidget(m_tabs, 1);

    m_log = new QPlainTextEdit();
    m_log->setReadOnly(true);
    m_log->setMaximumHeight(110);
    m_log->setStyleSheet("background:#050a14; color:#9fb4cc; font-family:Consolas;");
    root->addWidget(m_log);

    m_thread = new QThread(this);
    m_worker = new SweepWorker();
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &SweepWorker::progress, this, &InstrumentPanel::onSweepProgress);
    connect(m_worker, &SweepWorker::finished, this, &InstrumentPanel::onSweepFinished);
    m_thread->start();

    log("Bench instruments. Press \"Probe instruments\" to see what is attached.");
    log("Sweeps are refused while AetherSDR reports TX — an analyser on a live "
        "feedline is a destroyed analyser.");
}

InstrumentPanel::~InstrumentPanel()
{
    if (m_thread) {
        m_thread->quit();
        m_thread->wait(3000);
    }
    if (m_scopeThread) {
        m_scopeThread->quit();
        m_scopeThread->wait(3000);
    }
}

void InstrumentPanel::buildAntennaTab()
{
    auto* page = new QWidget();
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(6, 6, 6, 6);

    auto* ctl = new QHBoxLayout();
    ctl->addWidget(new QLabel("Instrument:"));
    m_antInstrument = new QComboBox();
    m_antInstrument->addItem("(probe first)");
    ctl->addWidget(m_antInstrument);

    ctl->addWidget(new QLabel("Band:"));
    m_antBandPreset = new QComboBox();
    for (const auto& b : kBands) m_antBandPreset->addItem(b.name);
    m_antBandPreset->setCurrentIndex(4);      // 40m
    connect(m_antBandPreset, &QComboBox::currentIndexChanged,
            this, &InstrumentPanel::onBandPresetChanged);
    ctl->addWidget(m_antBandPreset);

    m_antFrom = new QDoubleSpinBox();
    m_antFrom->setRange(0.1, 170.0); m_antFrom->setDecimals(3);
    m_antFrom->setSuffix(" MHz"); m_antFrom->setValue(7.000);
    m_antTo = new QDoubleSpinBox();
    m_antTo->setRange(0.1, 170.0); m_antTo->setDecimals(3);
    m_antTo->setSuffix(" MHz"); m_antTo->setValue(7.300);
    ctl->addWidget(m_antFrom);
    ctl->addWidget(new QLabel("to"));
    ctl->addWidget(m_antTo);

    ctl->addWidget(new QLabel("Points:"));
    m_antPoints = new QSpinBox();
    m_antPoints->setRange(5, 500); m_antPoints->setValue(100);
    ctl->addWidget(m_antPoints);

    m_antSweep = new QPushButton("Sweep");
    connect(m_antSweep, &QPushButton::clicked, this, &InstrumentPanel::onSweepClicked);
    ctl->addWidget(m_antSweep);
    m_antStop = new QPushButton("Stop");
    m_antStop->setEnabled(false);
    connect(m_antStop, &QPushButton::clicked, this, &InstrumentPanel::onStopClicked);
    ctl->addWidget(m_antStop);
    ctl->addStretch();
    v->addLayout(ctl);

    // Provenance row — guardrail 2. Nothing is stored without it.
    auto* prov = new QHBoxLayout();
    prov->addWidget(new QLabel("What is connected?"));
    m_antNote = new QLineEdit();
    m_antNote->setPlaceholderText(
        "e.g. Hustler 5-BTV at the feedpoint, 30 m RG-213, ATU bypassed");
    prov->addWidget(m_antNote, 1);
    m_antCalBadge = new QLabel("cal: unknown");
    m_antCalBadge->setStyleSheet("color:#ffb454; font-family:Consolas;");
    prov->addWidget(m_antCalBadge);
    v->addLayout(prov);

    m_plotSwr = new TracePlot();
    m_plotSwr->setUnit(TracePlot::Unit::Swr);
    m_plotSwr->setTitle("SWR");
    m_plotSwr->setPlaceholder("No sweep yet — pick a band and press Sweep.");
    m_plotSwr->setSpans(hamBandSpans());
    connect(m_plotSwr, &TracePlot::cursorMoved, this, &InstrumentPanel::onCursorMoved);

    m_plotRx = new TracePlot();
    m_plotRx->setUnit(TracePlot::Unit::Ohms);
    m_plotRx->setTitle("R and X (ohms) — the diagnostic pair");
    m_plotRx->setPlaceholder("R/X appear here. A disconnected feed is visible "
                             "ONLY in X (a constant series capacitance).");
    connect(m_plotRx, &TracePlot::cursorMoved, this, &InstrumentPanel::onCursorMoved);

    m_plotRl = new TracePlot();
    m_plotRl->setUnit(TracePlot::Unit::Db);
    m_plotRl->setTitle("Return loss (dB)");
    m_plotRl->setPlaceholder("Return loss appears here.");
    connect(m_plotRl, &TracePlot::cursorMoved, this, &InstrumentPanel::onCursorMoved);

    auto* split = new QSplitter(Qt::Vertical);
    split->addWidget(m_plotSwr);
    split->addWidget(m_plotRx);
    split->addWidget(m_plotRl);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    split->setStretchFactor(2, 2);
    v->addWidget(split, 1);

    auto* bottom = new QHBoxLayout();
    m_antStatus = new QLabel("idle");
    m_antStatus->setStyleSheet("color:#9fb4cc; font-family:Consolas;");
    bottom->addWidget(m_antStatus, 1);
    auto* save = new QPushButton("Save to library");
    connect(save, &QPushButton::clicked, this, &InstrumentPanel::onSaveClicked);
    bottom->addWidget(save);
    auto* recall = new QPushButton("Recall / overlay");
    connect(recall, &QPushButton::clicked, this, &InstrumentPanel::onRecallClicked);
    bottom->addWidget(recall);
    auto* csv = new QPushButton("Export CSV");
    connect(csv, &QPushButton::clicked, this, &InstrumentPanel::onExportCsvClicked);
    bottom->addWidget(csv);
    v->addLayout(bottom);

    m_library = new QListWidget();
    m_library->setMaximumHeight(78);
    m_library->setStyleSheet("background:#050a14; color:#9fb4cc; font-family:Consolas;");
    v->addWidget(m_library);

    m_tabs->addTab(page, "Antenna");
}

void InstrumentPanel::buildFeedlineTab()
{
    auto* page = new QWidget();
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(6, 6, 6, 6);

    auto* ctl = new QHBoxLayout();
    ctl->addWidget(new QLabel("Cable:"));
    m_cableType = new QComboBox();
    for (const auto& c : cableTypes()) m_cableType->addItem(c.name);
    connect(m_cableType, &QComboBox::currentIndexChanged,
            this, &InstrumentPanel::onCableTypeChanged);
    ctl->addWidget(m_cableType);

    // ⭐ VF is editable, not locked to the catalogue. It is the single
    // assumption that converts a MEASURED electrical length into a physical
    // one, and picking the wrong cable is how a healthy line comes to look
    // lossy. Let the operator override it, and always show what was used.
    ctl->addWidget(new QLabel("VF:"));
    m_vf = new QDoubleSpinBox();
    m_vf->setRange(0.40, 1.00);
    m_vf->setDecimals(3);
    m_vf->setSingleStep(0.01);
    m_vf->setValue(0.66);
    m_vf->setToolTip("Velocity factor. This is an ASSUMPTION, not a "
                     "measurement — it scales the physical length and "
                     "therefore the loss-per-foot verdict.");
    connect(m_vf, &QDoubleSpinBox::valueChanged, this,
            [this](double) { onAnalyseCoax(); });
    ctl->addWidget(m_vf);
    ctl->addStretch();
    v->addLayout(ctl);

    auto* cap = new QHBoxLayout();
    m_takeOpen = new QPushButton("Take OPEN sweep");
    connect(m_takeOpen, &QPushButton::clicked, this,
            &InstrumentPanel::onTakeOpenClicked);
    cap->addWidget(m_takeOpen);
    m_openState = new QLabel("— not captured —");
    m_openState->setStyleSheet("color:#9fb4cc; font-family:Consolas;");
    cap->addWidget(m_openState, 1);

    m_takeShort = new QPushButton("Take SHORT sweep");
    connect(m_takeShort, &QPushButton::clicked, this,
            &InstrumentPanel::onTakeShortClicked);
    cap->addWidget(m_takeShort);
    m_shortState = new QLabel("— not captured —");
    m_shortState->setStyleSheet("color:#9fb4cc; font-family:Consolas;");
    cap->addWidget(m_shortState, 1);
    v->addLayout(cap);

    m_plotCoax = new TracePlot();
    m_plotCoax->setUnit(TracePlot::Unit::Ohms);
    m_plotCoax->setTitle("R for both far-end conditions — they must INVERT");
    m_plotCoax->setPlaceholder(
        "Capture an OPEN and a SHORT at the same far end.\n"
        "A quarter-wave open looks like a short and vice versa; that swap is\n"
        "what proves the far-end condition was what you thought it was.");
    v->addWidget(m_plotCoax, 1);

    m_coaxReport = new QPlainTextEdit();
    m_coaxReport->setReadOnly(true);
    m_coaxReport->setMinimumHeight(190);
    m_coaxReport->setStyleSheet(
        "background:#050a14; color:#cfe3ff; font-family:Consolas;");
    m_coaxReport->setPlainText(
        "Feedline analysis — open/short pair.\n\n"
        "  Z0     = sqrt(Zoc * Zsc)\n"
        "  loss   = 8.686 * Re(atanh(sqrt(Zsc / Zoc)))\n"
        "  length : X crosses zero every QUARTER wavelength on a shorted line\n\n"
        "Capture both ends to begin.");
    v->addWidget(m_coaxReport);

    m_tabs->addTab(page, "Feedline");
}

void InstrumentPanel::onCableTypeChanged(int index)
{
    const auto& types = cableTypes();
    if (index < 0 || index >= types.size()) return;
    m_vf->setValue(types[index].vf);   // triggers a re-analysis
}

void InstrumentPanel::onTakeOpenClicked()
{
    m_capturingOpen = true;
    m_capturingShort = false;
    log("capturing the OPEN sweep — leave the far end DISCONNECTED and clear "
        "of metal.");
    onSweepClicked();
}

void InstrumentPanel::onTakeShortClicked()
{
    m_capturingShort = true;
    m_capturingOpen = false;
    log("capturing the SHORT sweep — short the SAME far end.");
    onSweepClicked();
}

void InstrumentPanel::onAnalyseCoax()
{
    if (m_openSweep.points.isEmpty() || m_shortSweep.points.isEmpty()) return;

    CableType cable;
    const auto& types = cableTypes();
    const int idx = m_cableType ? m_cableType->currentIndex() : -1;
    if (idx >= 0 && idx < types.size()) cable = types[idx];
    cable.vf = m_vf->value();          // the override wins

    const CoaxResult r = analyseOpenShort(m_openSweep, m_shortSweep, cable);

    // Plot R for both, so the inversion is visible rather than merely asserted.
    TracePlot::Trace o, s;
    o.label = "open";  o.color = QColor("#ffb454");
    s.label = "short"; s.color = QColor("#4fc3f7");
    for (const auto& p : m_openSweep.points)  o.points.insert(p.hz, p.r);
    for (const auto& p : m_shortSweep.points) s.points.insert(p.hz, p.r);
    m_plotCoax->setTraces({o, s});

    QStringList t;
    if (!r.ok) {
        t << "ANALYSIS FAILED" << ("  " + r.error);
        m_coaxReport->setPlainText(t.join('\n'));
        return;
    }

    t << "FEEDLINE — open/short pair";
    t << "";
    t << "  integrity";
    t << QString("    impossible points   open %1, short %2  (want 0)")
             .arg(r.impossibleOpen).arg(r.impossibleShort);
    t << QString("    R inversion         %1  (%2 quarter-wave points)")
             .arg(r.inversionConfirmed ? "CONFIRMED" : "*** NOT SEEN ***")
             .arg(r.inversionPoints);
    if (!r.inversionConfirmed)
        t << "    ^^ the two sweeps do not invert. One far-end condition was"
             "\n       probably not what you thought — do not trust the numbers.";
    t << "";
    t << "  length";
    t << QString("    X zero-crossings    %1, mean spacing %2 MHz (spread %3%)")
             .arg(r.crossingsMhz.size())
             .arg(r.meanSpacingMhz, 0, 'f', 4)
             .arg(r.spacingSpreadPct, 0, 'f', 1);
    t << QString("    electrical length   %1 m   [MEASURED]")
             .arg(r.electricalHalfWaveM, 0, 'f', 2);
    t << QString("    physical length     %1 m = %2 ft   [assumes VF %3]")
             .arg(r.physicalLengthM, 0, 'f', 2)
             .arg(r.physicalLengthFt, 0, 'f', 1)
             .arg(r.assumedVf, 0, 'f', 3);
    t << "";
    t << "  impedance and loss";
    t << QString("    mean |Z0|           %1 ohm").arg(r.meanZ0, 0, 'f', 1);
    t << QString("    loss at %1 MHz     %2 dB measured")
             .arg(r.topMhz, 0, 'f', 1).arg(r.measuredLossAtTopDb, 0, 'f', 2);
    if (r.specLossAtTopDb > 0.01)
        t << QString("    %1 spec            %2 dB over %3 ft")
                 .arg(r.cableName).arg(r.specLossAtTopDb, 0, 'f', 2)
                 .arg(r.physicalLengthFt, 0, 'f', 0);
    t << QString("    verdict             %1").arg(r.verdict);
    t << "";
    t << "  ⚠ The physical length and therefore the loss verdict depend on the";
    t << "    velocity factor above. Electrical length is measured; VF is not.";

    m_coaxReport->setPlainText(t.join('\n'));
    log(QString("feedline: %1 ft at VF %2, |Z0| %3 ohm, %4")
            .arg(r.physicalLengthFt, 0, 'f', 1)
            .arg(r.assumedVf, 0, 'f', 3)
            .arg(r.meanZ0, 0, 'f', 1)
            .arg(r.verdict));
}

void InstrumentPanel::buildScopeTab()
{
    auto* page = new QWidget();
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(6, 6, 6, 6);

    auto* ctl = new QHBoxLayout();
    m_scopeCapture = new QPushButton("Capture");
    connect(m_scopeCapture, &QPushButton::clicked,
            this, &InstrumentPanel::onScopeCapture);
    ctl->addWidget(m_scopeCapture);
    m_scopeStatus = new QLabel("checking for a scope…");
    m_scopeStatus->setWordWrap(true);
    m_scopeStatus->setStyleSheet("color:#9fb4cc; font-family:Consolas;");
    ctl->addWidget(m_scopeStatus, 1);

    // Export stays disabled until there is something real to write, so the
    // buttons can never produce an empty or stale file.
    m_scopeCsv = new QPushButton("Export CSV");
    m_scopeCsv->setEnabled(false);
    connect(m_scopeCsv, &QPushButton::clicked,
            this, &InstrumentPanel::onScopeExportCsv);
    ctl->addWidget(m_scopeCsv);

    m_scopePng = new QPushButton("Save PNG");
    m_scopePng->setEnabled(false);
    connect(m_scopePng, &QPushButton::clicked,
            this, &InstrumentPanel::onScopeExportPng);
    ctl->addWidget(m_scopePng);
    v->addLayout(ctl);

    m_plotScope = new TracePlot();
    m_plotScope->setUnit(TracePlot::Unit::Volts);
    m_plotScope->setXAxis(TracePlot::XAxis::TimeMicros);
    m_plotScope->setTitle("Waveform");
    m_plotScope->setPlaceholder("No capture yet — press Capture.");
    connect(m_plotScope, &TracePlot::cursorMoved,
            this, &InstrumentPanel::onCursorMoved);
    v->addWidget(m_plotScope, 1);

    m_scopeReport = new QPlainTextEdit();
    m_scopeReport->setReadOnly(true);
    m_scopeReport->setMaximumHeight(150);
    m_scopeReport->setStyleSheet(
        "background:#050a14; color:#cfe3ff; font-family:Consolas;");
    v->addWidget(m_scopeReport);

    m_tabs->addTab(page, "Scope");

    // Availability is a startup fact, so resolve it once here rather than
    // failing at the first Capture.
    QString detail;
    if (!scopeVisaAvailable(&detail)) {
        m_scopeCapture->setEnabled(false);
        m_scopeStatus->setText("no VISA runtime — " + detail);
        m_scopeReport->setPlainText(
            "The scope is driven over USBTMC through a VISA library, which is\n"
            "not present on this machine. Install NI-VISA or Keysight IO\n"
            "Libraries and restart. Every other tab works without it.");
        return;
    }
    QString findLog;
    m_scopeResource = findScopeResource(&findLog);
    if (m_scopeResource.isEmpty()) {
        m_scopeCapture->setEnabled(false);
        m_scopeStatus->setText("no scope found — " + findLog);
    } else {
        m_scopeStatus->setText(m_scopeResource);
    }

    m_scopeThread = new QThread(this);
    m_scopeWorker = new ScopeWorker();
    m_scopeWorker->moveToThread(m_scopeThread);
    connect(m_scopeThread, &QThread::finished, m_scopeWorker,
            &QObject::deleteLater);
    connect(m_scopeWorker, &ScopeWorker::finished,
            this, &InstrumentPanel::onScopeFinished);
    connect(m_scopeWorker, &ScopeWorker::progress,
            this, &InstrumentPanel::onScopeProgress);
    m_scopeThread->start();
}

void InstrumentPanel::onScopeProgress(const QString& note)
{
    m_scopeStatus->setText(note);
}

void InstrumentPanel::onScopeCapture()
{
    if (m_scopeResource.isEmpty() || !m_scopeWorker) return;
    m_scopeCapture->setEnabled(false);
    log("scope: capturing…");
    QMetaObject::invokeMethod(m_scopeWorker, "capture", Qt::QueuedConnection,
                              Q_ARG(QString, m_scopeResource));
}

void InstrumentPanel::onScopeFinished(const TciMon::ScopeCapture& cap)
{
    m_scopeCapture->setEnabled(true);

    if (!cap.ok) {
        m_scopeStatus->setText("capture failed");
        m_scopeReport->setPlainText("CAPTURE FAILED\n  " + cap.error);
        log("scope: " + cap.error);
        return;
    }

    QVector<TracePlot::Trace> traces;
    const QColor colours[2] = {QColor("#ffd866"), QColor("#4fc3f7")};
    const double sps = cap.secondsPerSample();
    for (const auto& ch : cap.channels) {
        if (!ch.enabled || ch.volts.isEmpty()) continue;
        TracePlot::Trace t;
        t.label = QString("CH%1").arg(ch.index);
        t.color = colours[(ch.index - 1) % 2];
        for (int i = 0; i < ch.volts.size(); ++i)
            t.points.insert(qint64(i * sps * 1e6), ch.volts[i]);  // microseconds
        traces << t;
    }
    m_plotScope->setTraces(traces);
    m_plotScope->setTitle(QString("Waveform — %1 s/div, %2 Sa/s")
                              .arg(cap.secondsPerDiv, 0, 'g', 3)
                              .arg(cap.sampleRateHz, 0, 'f', 0));
    m_plotScope->setProvenance(
        QString("%1 · trigger %2/%3 · %4")
            .arg(cap.idn.section(',', 1, 1).trimmed(), cap.triggerMode,
                 cap.triggerSweep, cap.takenAtIso));

    QStringList t;
    t << QString("%1   %2 s/div   %3 Sa/s   %4 points")
             .arg(cap.idn.section(',', 1, 1).trimmed())
             .arg(cap.secondsPerDiv, 0, 'g', 3)
             .arg(cap.sampleRateHz, 0, 'f', 0)
             .arg(cap.pointsRequested);
    t << "";
    t << "  ch  V/div   coupling   Vpp      Vrms     Vmean    freq";
    for (const auto& ch : cap.channels) {
        if (!ch.enabled) { t << QString("  %1  (off)").arg(ch.index); continue; }
        t << QString("  %1   %2   %3   %4  %5  %6  %7")
                 .arg(ch.index)
                 .arg(ch.voltsPerDiv, 6, 'g', 3)
                 .arg(ch.coupling, -8)
                 .arg(ch.vpp, 7, 'f', 3)
                 .arg(ch.vrms, 8, 'f', 3)
                 .arg(ch.vmean, 8, 'f', 3)
                 .arg(ch.freqHz > 0
                          ? QString("%1 Hz").arg(ch.freqHz, 0, 'f', 1)
                          : QString("—"));
    }
    t << "";
    t << "  ⚠ Vpp/Vrms/frequency are COMPUTED from the captured samples.";
    t << "    Firmware 3.0.1 implements no :MEASure: value queries at all, so";
    t << "    these are our numbers, not the instrument's readout.";
    t << "    A flat trace reports no frequency rather than inventing one.";
    m_scopeReport->setPlainText(t.join('\n'));

    m_scopeLast = cap;
    m_scopeCsv->setEnabled(true);
    m_scopePng->setEnabled(true);

    m_scopeStatus->setText(QString("captured %1").arg(cap.takenAtIso));
    log(QString("scope: captured %1 channel(s)").arg(traces.size()));
}

void InstrumentPanel::onScopeExportCsv()
{
    if (!m_scopeLast.ok) { log("scope: nothing to export."); return; }

    const QString stamp =
        QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    const QString path = QFileDialog::getSaveFileName(
        this, "Export scope capture",
        QString("scope-%1.csv").arg(stamp), "CSV (*.csv)");
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        log("scope: cannot write " + path);
        return;
    }
    QTextStream ts(&f);

    // Provenance travels WITH the data, as it does for the sweep exports. A
    // column of volts with no record of the instrument, the timebase or the
    // per-channel sensitivity cannot be checked later, and an unverifiable
    // capture is one that gets believed anyway.
    const ScopeCapture& c = m_scopeLast;
    ts << "# " << c.idn << "\n";
    ts << "# taken " << c.takenAtIso << "\n";
    ts << "# " << c.secondsPerDiv << " s/div, " << c.sampleRateHz
       << " Sa/s, " << c.pointsRequested << " points requested\n";
    ts << "# trigger " << c.triggerMode << " / " << c.triggerSweep << "\n";
    for (const auto& ch : c.channels) {
        if (!ch.enabled) continue;
        ts << "# CH" << ch.index << ": " << ch.voltsPerDiv << " V/div, offset "
           << ch.offsetV << " V, probe x" << ch.probe << ", " << ch.coupling
           << "\n";
        ts << "#   Vpp " << QString::number(ch.vpp, 'f', 4)
           << "  Vrms " << QString::number(ch.vrms, 'f', 4)
           << "  Vmean " << QString::number(ch.vmean, 'f', 4)
           << "  Vmin " << QString::number(ch.vmin, 'f', 4)
           << "  Vmax " << QString::number(ch.vmax, 'f', 4)
           << "  freq "
           << (ch.freqHz > 0 ? QString::number(ch.freqHz, 'f', 2) + " Hz"
                             : QString("(none — under half a division)"))
           << "\n";
    }
    ts << "# NOTE: Vpp/Vrms/Vmean/freq are COMPUTED from these samples.\n";
    ts << "#       Firmware 3.0.1 implements no :MEASure: value queries, so\n";
    ts << "#       they are this program's numbers, not the scope's readout.\n";

    // One row per sample instant, one column per enabled channel.
    QVector<const ScopeChannel*> live;
    for (const auto& ch : c.channels)
        if (ch.enabled && !ch.volts.isEmpty()) live << &ch;
    if (live.isEmpty()) {
        ts << "# no channel produced samples\n";
        f.close();
        log("scope: exported header only — no samples");
        return;
    }

    ts << "time_s";
    for (const auto* ch : live) ts << ",ch" << ch->index << "_volts";
    ts << "\n";

    int n = 0;
    for (const auto* ch : live) n = std::max(n, int(ch->volts.size()));
    const double dt = c.secondsPerSample();
    for (int i = 0; i < n; ++i) {
        ts << QString::number(i * dt, 'e', 9);
        for (const auto* ch : live) {
            ts << ',';
            if (i < ch->volts.size())
                ts << QString::number(ch->volts[i], 'f', 6);
        }
        ts << "\n";
    }
    f.close();
    log(QString("scope: exported %1 samples to %2").arg(n).arg(path));
}

void InstrumentPanel::onScopeExportPng()
{
    if (!m_scopeLast.ok || !m_plotScope) { log("scope: nothing to save."); return; }
    const QString stamp =
        QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    const QString path = QFileDialog::getSaveFileName(
        this, "Save waveform image",
        QString("scope-%1.png").arg(stamp), "PNG (*.png)");
    if (path.isEmpty()) return;

    // Render at 2x so the image is legible when it ends up in a report or a
    // forum post rather than back on this screen.
    const qreal scale = 2.0;
    QPixmap pm(m_plotScope->size() * scale);
    pm.setDevicePixelRatio(scale);
    pm.fill(QColor("#050a14"));
    m_plotScope->render(&pm);
    if (pm.save(path, "PNG"))
        log("scope: saved " + path);
    else
        log("scope: could not write " + path);
}

void InstrumentPanel::buildSpectrumTab()
{
    auto* page = new QWidget();
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(6, 6, 6, 6);

    auto* ctl = new QHBoxLayout();
    ctl->addWidget(new QLabel("Span:"));
    m_spanPreset = new QComboBox();
    for (const auto& s : kSpans) m_spanPreset->addItem(s.name);
    connect(m_spanPreset, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (i >= 0 && i < int(sizeof(kSpans) / sizeof(kSpans[0]))) {
            m_saFrom->setValue(kSpans[i].fromMhz);
            m_saTo->setValue(kSpans[i].toMhz);
        }
    });
    ctl->addWidget(m_spanPreset);

    m_saFrom = new QDoubleSpinBox();
    m_saFrom->setRange(0.1, 350.0); m_saFrom->setDecimals(3);
    m_saFrom->setSuffix(" MHz"); m_saFrom->setValue(0.100);
    m_saTo = new QDoubleSpinBox();
    m_saTo->setRange(0.1, 350.0); m_saTo->setDecimals(3);
    m_saTo->setSuffix(" MHz"); m_saTo->setValue(30.000);
    ctl->addWidget(m_saFrom);
    ctl->addWidget(new QLabel("to"));
    ctl->addWidget(m_saTo);

    m_saSweep = new QPushButton("Scan");
    connect(m_saSweep, &QPushButton::clicked, this, &InstrumentPanel::onSweepClicked);
    ctl->addWidget(m_saSweep);
    ctl->addStretch();
    v->addLayout(ctl);

    m_plotSa = new TracePlot();
    m_plotSa->setUnit(TracePlot::Unit::Dbm);
    m_plotSa->setTitle("Spectrum (dBm)");
    m_plotSa->setPlaceholder("No scan yet — pick a span and press Scan.");
    m_plotSa->setSpans(hamBandSpans());
    connect(m_plotSa, &TracePlot::cursorMoved, this, &InstrumentPanel::onCursorMoved);
    v->addWidget(m_plotSa, 1);

    m_saStatus = new QLabel(
        "tinySA — this unit is the ORIGINAL (0.1–350 MHz). It CLAMPS a wider "
        "request silently, so the plot always shows the span it actually accepted.");
    m_saStatus->setWordWrap(true);
    m_saStatus->setStyleSheet("color:#9fb4cc; font-family:Consolas;");
    v->addWidget(m_saStatus);

    m_tabs->addTab(page, "Spectrum");
}

void InstrumentPanel::onBandPresetChanged(int index)
{
    if (index < 0 || index >= int(sizeof(kBands) / sizeof(kBands[0]))) return;
    m_antFrom->setValue(kBands[index].fromMhz);
    m_antTo->setValue(kBands[index].toMhz);
}

void InstrumentPanel::log(const QString& line)
{
    m_log->appendPlainText(
        QDateTime::currentDateTime().toString("HH:mm:ss ") + line);
}

void InstrumentPanel::onProbeClicked()
{
    // The probe opens serial ports and waits on replies, so it must not run on
    // the GUI thread — doing so froze the whole window for the duration and
    // made a 3 s probe feel broken rather than slow.
    log("probing serial ports (read-only; no sweep is started)…");
    QApplication::setOverrideCursor(Qt::BusyCursor);

    QStringList detail;
    QVector<InstrumentId> found;
    {
        // QtConcurrent would drag in another module for one call; a scoped
        // thread is enough and keeps the dependency list short.
        QThread worker;
        QObject ctx;
        ctx.moveToThread(&worker);
        QObject::connect(&worker, &QThread::started, &ctx, [&] {
            found = probeInstruments(&detail);
            worker.quit();
        });
        worker.start();
        while (worker.isRunning()) {
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50);
            QThread::msleep(10);
        }
        worker.wait(2000);
    }
    QApplication::restoreOverrideCursor();

    m_found = found;
    for (const QString& d : detail) log("  " + d);

    // ⚠ Only the antenna analysers belong in this dropdown — the tinySA is a
    // spectrum analyser and lives on its own tab. Previously "found 2" could
    // still leave the box empty with no explanation, which reads as a bug.
    m_antInstrument->clear();
    int analysers = 0;
    for (const auto& id : m_found) {
        if (id.kind == InstrumentId::Kind::RigExpert ||
            id.kind == InstrumentId::Kind::NanoVna) {
            m_antInstrument->addItem(id.describe(), id.port);
            ++analysers;
        }
    }
    if (analysers == 0)
        m_antInstrument->addItem("(no antenna analyser found)");

    // Say what was found AND where it went, so an empty dropdown is explained
    // rather than merely observed.
    QStringList kinds;
    for (const auto& id : m_found) {
        switch (id.kind) {
        case InstrumentId::Kind::RigExpert:
        case InstrumentId::Kind::NanoVna:
            kinds << QString("%1 → Antenna tab").arg(id.describe()); break;
        case InstrumentId::Kind::TinySa:
            kinds << QString("%1 → Spectrum tab").arg(id.describe()); break;
        default: break;
        }
    }
    for (const QString& k : kinds) log("  " + k);
    log(QString("found %1 instrument(s): %2 antenna analyser(s)")
            .arg(m_found.size()).arg(analysers));
    if (analysers == 0)
        log("  no antenna analyser attached — the RigExpert AA-170 and the "
            "NanoVNA are the two this tab can use.");

    // The scope is on a different transport, so re-check it here too.
    if (m_scopeResource.isEmpty() && scopeVisaAvailable(nullptr)) {
        QString findLog;
        m_scopeResource = findScopeResource(&findLog);
        if (!m_scopeResource.isEmpty()) {
            m_scopeCapture->setEnabled(true);
            m_scopeStatus->setText(m_scopeResource);
            log("  scope: " + findLog);
        }
    }
}

bool InstrumentPanel::txInterlockBlocks(QString* why) const
{
    if (m_txActive) {
        if (why) *why = "AetherSDR reports TX is active. Sweeping now would put "
                        "transmitter power into the analyser's input.";
        return true;
    }
    return false;
}

void InstrumentPanel::onSweepClicked()
{
    QString why;
    if (txInterlockBlocks(&why)) {
        QMessageBox::warning(this, "Refused — TX is active", why);
        log("REFUSED: " + why);
        return;
    }
    if (m_busy) return;

    const bool spectrum = (m_tabs->currentIndex() == 1);

    if (spectrum) {
        const qint64 a = qint64(m_saFrom->value() * 1e6);
        const qint64 b = qint64(m_saTo->value() * 1e6);
        QString port;
        for (const auto& id : m_found)
            if (id.kind == InstrumentId::Kind::TinySa) port = id.port;
        if (port.isEmpty()) {
            log("no tinySA found — press \"Probe instruments\" first.");
            return;
        }
        setBusy(true);
        log(QString("tinySA scan %1–%2 MHz on %3")
                .arg(a / 1e6, 0, 'f', 3).arg(b / 1e6, 0, 'f', 3).arg(port));
        QMetaObject::invokeMethod(m_worker, "runTinySaSweep", Qt::QueuedConnection,
                                  Q_ARG(QString, port), Q_ARG(qint64, a),
                                  Q_ARG(qint64, b), Q_ARG(int, 290));
        return;
    }

    const QString port = m_antInstrument->currentData().toString();
    if (port.isEmpty()) {
        log("no antenna analyser selected — press \"Probe instruments\" first.");
        return;
    }
    InstrumentId::Kind kind = InstrumentId::Kind::Unknown;
    for (const auto& id : m_found) if (id.port == port) kind = id.kind;

    const qint64 a = qint64(m_antFrom->value() * 1e6);
    const qint64 b = qint64(m_antTo->value() * 1e6);
    if (b <= a) { log("stop frequency must be above start."); return; }

    setBusy(true);
    if (kind == InstrumentId::Kind::RigExpert) {
        log(QString("AA-170 sweep %1–%2 MHz, %3 points (this can take minutes)")
                .arg(a / 1e6, 0, 'f', 3).arg(b / 1e6, 0, 'f', 3)
                .arg(m_antPoints->value()));
        QMetaObject::invokeMethod(m_worker, "runRigExpertSweep", Qt::QueuedConnection,
                                  Q_ARG(QString, port), Q_ARG(qint64, a),
                                  Q_ARG(qint64, b), Q_ARG(int, m_antPoints->value()));
    } else {
        log(QString("NanoVNA sweep %1–%2 MHz, %3 points")
                .arg(a / 1e6, 0, 'f', 3).arg(b / 1e6, 0, 'f', 3)
                .arg(m_antPoints->value()));
        QMetaObject::invokeMethod(m_worker, "runNanoVnaSweep", Qt::QueuedConnection,
                                  Q_ARG(QString, port), Q_ARG(qint64, a),
                                  Q_ARG(qint64, b), Q_ARG(int, m_antPoints->value()));
    }
}

void InstrumentPanel::onStopClicked()
{
    if (m_worker) m_worker->cancel();
    log("cancel requested — the instrument is left with RF off.");
}

void InstrumentPanel::setBusy(bool busy)
{
    m_busy = busy;
    m_antSweep->setEnabled(!busy);
    m_saSweep->setEnabled(!busy);
    m_antStop->setEnabled(busy);
}

void InstrumentPanel::onSweepProgress(int done, int total, const QString& note)
{
    const QString s = QString("%1  %2/%3").arg(note).arg(done).arg(total);
    m_antStatus->setText(s);
}

QString InstrumentPanel::provenanceLine(const SweepResult& r) const
{
    QString cal = r.calState.isEmpty() ? "cal unknown" : r.calState;
    if (r.calFromHz && r.calToHz)
        cal += QString(" [%1–%2 MHz]")
                   .arg(r.calFromHz / 1e6, 0, 'f', 3)
                   .arg(r.calToHz / 1e6, 0, 'f', 3);
    return QString("%1 fw %2 · %3 · %4 · %5")
        .arg(r.instrument, r.firmware.isEmpty() ? "?" : r.firmware,
             r.port, r.takenAtIso, cal)
        + (r.antennaNote.isEmpty() ? QString("  ⚠ no provenance note")
                                   : QString("  · %1").arg(r.antennaNote));
}

void InstrumentPanel::onSweepFinished(const TciMon::SweepResult& result)
{
    setBusy(false);

    if (!result.ok && result.points.isEmpty()) {
        m_antStatus->setText("failed");
        log("SWEEP FAILED: " + result.error);
        QMessageBox::warning(this, "Sweep failed", result.error);
        return;
    }

    m_live = result;
    m_live.antennaNote = m_antNote->text().trimmed();

    // If this sweep was armed from the Feedline tab, file it as the open or
    // short half and re-analyse as soon as both are present.
    if (m_capturingOpen || m_capturingShort) {
        const QString when =
            QDateTime::currentDateTime().toString("HH:mm:ss");
        if (m_capturingOpen) {
            m_openSweep = result;
            m_openState->setText(QString("captured %1  (%2 pts)")
                                     .arg(when).arg(result.points.size()));
        } else {
            m_shortSweep = result;
            m_shortState->setText(QString("captured %1  (%2 pts)")
                                      .arg(when).arg(result.points.size()));
        }
        m_capturingOpen = m_capturingShort = false;
        onAnalyseCoax();
    }

    // Guardrail 4: report impossible physics loudly rather than plotting it as
    // though it were a measurement.
    if (!result.error.isEmpty()) {
        log("⚠ " + result.error);
        m_antStatus->setText("SUSPECT — see log");
    } else {
        m_antStatus->setText(QString("%1 points").arg(result.points.size()));
        log(QString("sweep complete: %1 points").arg(result.points.size()));
    }

    if (result.calFromHz && result.calToHz)
        m_antCalBadge->setText(QString("cal: %1–%2 MHz")
                                   .arg(result.calFromHz / 1e6, 0, 'f', 3)
                                   .arg(result.calToHz / 1e6, 0, 'f', 3));
    else
        m_antCalBadge->setText("cal: unknown");

    refreshPlots();
}

void InstrumentPanel::refreshPlots()
{
    const bool spectrum = !m_live.points.isEmpty() && m_live.instrument.contains("tinySA");

    if (spectrum) {
        TracePlot::Trace t;
        t.label = "tinySA";
        t.color = kLive;
        for (const auto& p : m_live.points) t.points.insert(p.hz, p.dbm);
        m_plotSa->setTraces({t});
        m_plotSa->setFrequencyRange(m_live.calFromHz, m_live.calToHz);
        m_plotSa->setTitle(QString("Spectrum %1–%2 MHz")
                               .arg(m_live.calFromHz / 1e6, 0, 'f', 3)
                               .arg(m_live.calToHz / 1e6, 0, 'f', 3));
        m_plotSa->setProvenance(provenanceLine(m_live));
        return;
    }

    TracePlot::Trace swr, r, x, rl;
    swr.label = "SWR"; swr.color = kLive;
    r.label = "R"; r.color = kR;
    x.label = "X"; x.color = kX;
    rl.label = "return loss"; rl.color = kLive;

    for (const auto& p : m_live.points) {
        if (std::isfinite(p.swr)) swr.points.insert(p.hz, p.swr);
        r.points.insert(p.hz, p.r);
        x.points.insert(p.hz, p.x);
        rl.points.insert(p.hz, p.returnLossDb);
    }

    QVector<TracePlot::Trace> swrTraces{swr};
    if (!m_reference.points.isEmpty()) {
        TracePlot::Trace ref;
        ref.label = "baseline";
        ref.color = kRefer;
        ref.dashed = true;
        for (const auto& p : m_reference.points)
            if (std::isfinite(p.swr)) ref.points.insert(p.hz, p.swr);
        swrTraces << ref;
    }
    // AE's own live SWR on the same axes -- the cross-instrument check that
    // nothing else in the shack can make.
    if (m_aeSwr > 0.0 && m_aeSwrHz > 0) {
        TracePlot::Trace ae;
        ae.label = "AE reported";
        ae.color = kAe;
        ae.points.insert(m_aeSwrHz, m_aeSwr);
        swrTraces << ae;
    }

    m_plotSwr->setTraces(swrTraces);
    m_plotRx->setTraces({r, x});
    m_plotRl->setTraces({rl});

    const QString prov = provenanceLine(m_live);
    m_plotSwr->setProvenance(prov);
    m_plotRx->setProvenance(prov);
    m_plotRl->setProvenance(prov);

    m_plotSwr->setTitle(QString("SWR — %1").arg(
        m_live.antennaNote.isEmpty() ? QString("(no provenance note)")
                                     : m_live.antennaNote));

    if (m_live.calFromHz && m_live.calToHz) {
        m_plotSwr->setCalibratedRange(m_live.calFromHz, m_live.calToHz);
        m_plotRx->setCalibratedRange(m_live.calFromHz, m_live.calToHz);
        m_plotRl->setCalibratedRange(m_live.calFromHz, m_live.calToHz);
    }
}

void InstrumentPanel::onCursorMoved(qint64 hz)
{
    if (hz < 0) { m_cursorLabel->setText("—"); return; }
    QString s = QString("%1 MHz").arg(hz / 1e6, 0, 'f', 4);
    // Nearest measured point wins; interpolating would invent data.
    const SweepResult& src = m_live;
    double bestD = 1e18; const SweepPoint* best = nullptr;
    for (const auto& p : src.points) {
        const double d = std::abs(double(p.hz - hz));
        if (d < bestD) { bestD = d; best = &p; }
    }
    if (best) {
        if (src.instrument.contains("tinySA"))
            s += QString("   %1 dBm").arg(best->dbm, 0, 'f', 1);
        else
            s += QString("   SWR %1   R %2   X %3")
                     .arg(std::isfinite(best->swr) ? QString::number(best->swr, 'f', 2)
                                                   : QString("∞"))
                     .arg(best->r, 0, 'f', 1)
                     .arg(best->x, 0, 'f', 1);
    }
    m_cursorLabel->setText(s);
}

void InstrumentPanel::onSaveClicked()
{
    if (m_live.points.isEmpty()) { log("nothing to save."); return; }
    // Guardrail 2: provenance is REQUIRED. Every wrong conclusion in the
    // 2026-08-02 session came from not knowing what was on the end of the coax.
    if (m_antNote->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "Provenance required",
            "Describe what was connected before saving.\n\n"
            "A stored sweep with no provenance is how you end up comparing two "
            "different antennas and believing the result.");
        m_antNote->setFocus();
        return;
    }
    bool ok = false;
    const QString name = QInputDialog::getText(this, "Save to library",
        "Name this sweep:", QLineEdit::Normal,
        QString("%1 %2").arg(m_antNote->text().trimmed(),
                             QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm")),
        &ok);
    if (!ok || name.isEmpty()) return;

    m_live.antennaNote = m_antNote->text().trimmed();
    m_saved.push_back(m_live);
    m_library->addItem(QString("%1  [%2 pts, %3]")
                           .arg(name).arg(m_live.points.size(), 0)
                           .arg(m_live.instrument));
    log("saved to library: " + name);
}

void InstrumentPanel::onRecallClicked()
{
    const int row = m_library->currentRow();
    if (row < 0 || row >= m_saved.size()) { log("select a library entry first."); return; }
    m_reference = m_saved[row];
    log("overlaying baseline: " + m_library->item(row)->text());
    refreshPlots();
}

void InstrumentPanel::onExportCsvClicked()
{
    if (m_live.points.isEmpty()) { log("nothing to export."); return; }
    const QString path = QFileDialog::getSaveFileName(this, "Export sweep CSV",
        QString("sweep-%1.csv").arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss")),
        "CSV (*.csv)");
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        log("cannot write " + path);
        return;
    }
    QTextStream ts(&f);
    // The provenance travels WITH the data, not just on the screen.
    ts << "# " << provenanceLine(m_live) << "\n";
    ts << "freq_hz,r_ohm,x_ohm,swr,return_loss_db,dbm\n";
    for (const auto& p : m_live.points)
        ts << p.hz << ',' << p.r << ',' << p.x << ','
           << (std::isfinite(p.swr) ? QString::number(p.swr, 'f', 4) : "inf")
           << ',' << p.returnLossDb << ',' << p.dbm << '\n';
    f.close();
    log("exported " + path);
}

void InstrumentPanel::noteIncoming(const QString& line)
{
    const QString l = line.trimmed().toLower();

    // TX interlock. Err toward "transmitting": a false positive costs a
    // refused sweep, a false negative costs the analyser's front end.
    if (l.startsWith("trx:")) {
        if (l.contains("true")) {
            if (!m_txActive) log("TX detected — sweeps are blocked until it clears.");
            m_txActive = true;
        } else if (l.contains("false")) {
            m_txActive = false;
        }
    }

    // AE's own SWR, for the cross-instrument overlay.
    if (l.startsWith("tx_sensors:")) {
        const QStringList parts = l.mid(11).split(',', Qt::SkipEmptyParts);
        for (const QString& p : parts) {
            bool ok = false;
            const double v = p.trimmed().toDouble(&ok);
            if (ok && v >= 1.0 && v < 100.0) { m_aeSwr = v; break; }
        }
    }
    if (l.startsWith("vfo:")) {
        const QStringList parts = l.mid(4).split(',', Qt::SkipEmptyParts);
        if (parts.size() >= 3) {
            bool ok = false;
            const qint64 hz = parts.last().trimmed().toLongLong(&ok);
            if (ok && hz > 0) m_aeSwrHz = hz;
        }
    }
}

} // namespace TciMon
