#include "ReplayPanel.h"

#include <QCheckBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <QWebSocket>
#include <QWebSocketServer>

namespace TciMon {

namespace {

constexpr const char* kCaptionStyle =
    "QLabel { color: #6b8099; font-size: 9px; font-weight: bold; "
    "letter-spacing: 0.08em; }";

constexpr int kDefaultGapMs = 50;   // used when a capture has no timing

} // namespace

ReplayPanel::ReplayPanel(QWidget* parent)
    : QWidget(parent),
      m_recClock(new QElapsedTimer),
      m_playTimer(new QTimer(this))
{
    m_playTimer->setSingleShot(true);
    connect(m_playTimer, &QTimer::timeout, this, &ReplayPanel::sendNext);

    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(4, 4, 4, 4);
    v->setSpacing(6);

    // ── Record ─────────────────────────────────────────────────────────
    {
        auto* cap = new QLabel("RECORD LIVE STREAM");
        cap->setStyleSheet(kCaptionStyle);
        v->addWidget(cap);

        auto* row = new QHBoxLayout;
        m_recBtn = new QPushButton("Start recording…");
        connect(m_recBtn, &QPushButton::clicked,
                this, &ReplayPanel::onToggleRecord);
        m_recStatus = new QLabel("not recording");
        m_recStatus->setStyleSheet(kCaptionStyle);
        row->addWidget(m_recBtn);
        row->addSpacing(10);
        row->addWidget(m_recStatus);
        row->addStretch();
        v->addLayout(row);
    }

    // ── Replay server ──────────────────────────────────────────────────
    {
        auto* cap = new QLabel("REPLAY AS LOCAL TCI SERVER");
        cap->setStyleSheet(kCaptionStyle);
        v->addWidget(cap);

        auto* fr = new QHBoxLayout;
        m_pathEdit = new QLineEdit;
        m_pathEdit->setPlaceholderText("Capture or saved-log file…");
        auto* browse = new QPushButton("Browse…");
        connect(browse, &QPushButton::clicked,
                this, &ReplayPanel::onBrowseReplay);
        fr->addWidget(m_pathEdit, 1);
        fr->addWidget(browse);
        v->addLayout(fr);

        auto* row = new QHBoxLayout;
        auto* pl = new QLabel("PORT"); pl->setStyleSheet(kCaptionStyle);
        m_portSpin = new QSpinBox;
        m_portSpin->setRange(1, 65535);
        m_portSpin->setValue(40001);

        auto* sl = new QLabel("SPEED ×"); sl->setStyleSheet(kCaptionStyle);
        m_speedSpin = new QDoubleSpinBox;
        m_speedSpin->setRange(0.1, 20.0);
        m_speedSpin->setSingleStep(0.5);
        m_speedSpin->setValue(1.0);
        m_speedSpin->setDecimals(1);

        m_loopCheck = new QCheckBox("Loop");

        m_startBtn = new QPushButton("Start server");
        m_stopBtn  = new QPushButton("Stop");
        m_stopBtn->setEnabled(false);
        connect(m_startBtn, &QPushButton::clicked,
                this, &ReplayPanel::onStartServer);
        connect(m_stopBtn, &QPushButton::clicked,
                this, &ReplayPanel::onStopServer);

        row->addWidget(pl); row->addWidget(m_portSpin);
        row->addSpacing(8);
        row->addWidget(sl); row->addWidget(m_speedSpin);
        row->addSpacing(8);
        row->addWidget(m_loopCheck);
        row->addStretch();
        row->addWidget(m_startBtn);
        row->addWidget(m_stopBtn);
        v->addLayout(row);

        m_srvStatus = new QLabel("server stopped");
        m_srvStatus->setStyleSheet(kCaptionStyle);
        v->addWidget(m_srvStatus);
    }

    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(1000);
    m_log->setStyleSheet(
        "QPlainTextEdit { background-color: #050a14; color: #dde6f0; "
        "border: 1px solid #1c2a40; "
        "font-family: Consolas, 'Cascadia Mono', monospace; "
        "font-size: 11px; }");
    v->addWidget(m_log, 1);
}

ReplayPanel::~ReplayPanel()
{
    onStopServer();
    stopRecording();
    delete m_recClock;
}

void ReplayPanel::status(const QString& text, const QString& colorHex)
{
    const QString ts = QDateTime::currentDateTime().toString("HH:mm:ss");
    m_log->appendHtml(
        QString("<span style='color:#6b8099'>%1</span> "
                "<span style='color:%2'>%3</span>")
            .arg(ts, colorHex, text.toHtmlEscaped()));
}

// ── Recording ──────────────────────────────────────────────────────────

void ReplayPanel::onToggleRecord()
{
    if (m_recording) { stopRecording(); return; }

    const QString stamp =
        QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    const QString path = QFileDialog::getSaveFileName(this,
        "Record TCI capture",
        QString("tci-capture-%1.tcicap").arg(stamp),
        "TCI capture (*.tcicap);;All files (*)");
    if (path.isEmpty()) return;

    m_recFile = new QFile(path);
    if (!m_recFile->open(QIODevice::WriteOnly | QIODevice::Truncate
                         | QIODevice::Text)) {
        status(QString("record failed: %1").arg(m_recFile->errorString()),
               "#ff5050");
        delete m_recFile;
        m_recFile = nullptr;
        return;
    }
    m_recStream = new QTextStream(m_recFile);
    *m_recStream << "# tcimon-capture v1  "
                 << QDateTime::currentDateTime().toString(Qt::ISODate)
                 << "\n";
    m_recClock->restart();
    m_recLastMs = 0;
    m_recCount  = 0;
    m_recording = true;
    m_recBtn->setText("Stop recording");
    m_recStatus->setText("recording → 0 lines");
    status(QString("recording to %1").arg(path), "#4cff7c");
}

void ReplayPanel::stopRecording()
{
    if (!m_recording) return;
    m_recording = false;
    if (m_recStream) { m_recStream->flush(); delete m_recStream; m_recStream = nullptr; }
    if (m_recFile)   { m_recFile->close(); delete m_recFile; m_recFile = nullptr; }
    m_recBtn->setText("Start recording…");
    m_recStatus->setText(QString("saved %1 lines").arg(m_recCount));
    status(QString("recording stopped — %1 lines").arg(m_recCount), "#6b8099");
}

void ReplayPanel::noteIncoming(const QString& line)
{
    if (!m_recording || !m_recStream) return;
    const qint64 now = m_recClock->elapsed();
    const qint64 delta = m_recCount == 0 ? 0 : now - m_recLastMs;
    m_recLastMs = now;
    *m_recStream << delta << '\t' << line << '\n';
    ++m_recCount;
    if ((m_recCount % 25) == 0)
        m_recStatus->setText(QString("recording → %1 lines").arg(m_recCount));
}

// ── Replay ──────────────────────────────────────────────────────────────

void ReplayPanel::onBrowseReplay()
{
    const QString path = QFileDialog::getOpenFileName(this,
        "Open capture / log",
        QString(),
        "Captures & logs (*.tcicap *.log *.txt);;All files (*)");
    if (!path.isEmpty()) m_pathEdit->setText(path);
}

bool ReplayPanel::loadScript(const QString& path, QString* err)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        *err = f.errorString();
        return false;
    }
    m_script.clear();
    QTextStream in(&f);
    // Matches a leading "HH:mm:ss(.zzz)" timestamp from a saved raw log.
    static const QRegularExpression kLogTs(
        QStringLiteral("^\\d{2}:\\d{2}:\\d{2}(\\.\\d+)?\\s+"));
    while (!in.atEnd()) {
        const QString raw = in.readLine();
        if (raw.isEmpty() || raw.startsWith('#')) continue;

        const int tab = raw.indexOf('\t');
        if (tab > 0) {
            bool ok = false;
            const int d = raw.left(tab).toInt(&ok);
            if (ok) {
                m_script.append({ qMax(0, d), raw.mid(tab + 1) });
                continue;
            }
        }
        // Fall back: strip a saved-log timestamp prefix if present, then
        // play with a fixed gap.
        QString msg = raw;
        const auto m = kLogTs.match(msg);
        if (m.hasMatch()) msg = msg.mid(m.capturedLength());
        m_script.append({ kDefaultGapMs, msg.trimmed() });
    }
    f.close();
    return !m_script.isEmpty();
}

void ReplayPanel::onStartServer()
{
    const QString path = m_pathEdit->text().trimmed();
    if (path.isEmpty()) {
        status("pick a capture/log file first", "#ffaa00");
        return;
    }
    QString err;
    if (!loadScript(path, &err)) {
        status(QString("load failed: %1").arg(err.isEmpty()
                   ? QStringLiteral("no replayable lines") : err),
               "#ff5050");
        return;
    }

    m_server = new QWebSocketServer(QStringLiteral("TCI Monitor Replay"),
                                    QWebSocketServer::NonSecureMode, this);
    const quint16 port = static_cast<quint16>(m_portSpin->value());
    if (!m_server->listen(QHostAddress::Any, port)) {
        status(QString("listen on :%1 failed: %2")
                   .arg(port).arg(m_server->errorString()), "#ff5050");
        delete m_server;
        m_server = nullptr;
        return;
    }
    connect(m_server, &QWebSocketServer::newConnection,
            this, &ReplayPanel::onNewConnection);

    m_idx = 0;
    m_startBtn->setEnabled(false);
    m_stopBtn->setEnabled(true);
    m_pathEdit->setEnabled(false);
    m_portSpin->setEnabled(false);
    m_srvStatus->setText(
        QString("listening ws://localhost:%1  •  %2 lines loaded  •  "
                "waiting for a client").arg(port).arg(m_script.size()));
    status(QString("server up on :%1 — %2 lines, connect a client now")
               .arg(port).arg(m_script.size()), "#4cff7c");
}

void ReplayPanel::onStopServer()
{
    m_playTimer->stop();
    for (QWebSocket* c : m_clients) {
        c->close();
        c->deleteLater();
    }
    m_clients.clear();
    if (m_server) {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }
    if (m_startBtn) {
        m_startBtn->setEnabled(true);
        m_stopBtn->setEnabled(false);
        m_pathEdit->setEnabled(true);
        m_portSpin->setEnabled(true);
        m_srvStatus->setText("server stopped");
    }
}

void ReplayPanel::onNewConnection()
{
    while (m_server && m_server->hasPendingConnections()) {
        QWebSocket* sock = m_server->nextPendingConnection();
        m_clients.append(sock);
        connect(sock, &QWebSocket::disconnected, this, [this, sock]() {
            m_clients.removeAll(sock);
            sock->deleteLater();
            status("client disconnected", "#6b8099");
        });
        status(QString("client connected (%1 total) — starting playback")
                   .arg(m_clients.size()), "#00d8ef");
    }
    // Begin (or continue) playback as soon as the first client is here.
    if (!m_playTimer->isActive() && !m_clients.isEmpty())
        sendNext();
}

void ReplayPanel::sendNext()
{
    if (m_clients.isEmpty() || m_script.isEmpty()) return;

    if (m_idx >= m_script.size()) {
        if (m_loopCheck->isChecked()) {
            m_idx = 0;
            status("loop — restarting capture", "#6b8099");
        } else {
            status(QString("playback complete — %1 lines sent")
                       .arg(m_script.size()), "#4cff7c");
            return;
        }
    }

    const auto& step = m_script.at(m_idx);
    QString msg = step.second;
    if (!msg.endsWith(';')) msg += ';';
    for (QWebSocket* c : m_clients)
        c->sendTextMessage(msg);
    ++m_idx;

    if (m_idx < m_script.size() || m_loopCheck->isChecked()) {
        const int nextDelay = (m_idx < m_script.size())
            ? m_script.at(m_idx).first
            : 0;
        const double speed = qMax(0.1, m_speedSpin->value());
        m_playTimer->start(qMax(0, int(nextDelay / speed)));
    } else {
        status(QString("playback complete — %1 lines sent")
                   .arg(m_script.size()), "#4cff7c");
    }
}

} // namespace TciMon
