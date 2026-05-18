#include "ConsolePanel.h"

#include "TciClient.h"
#include "TciCommands.h"

#include <QCheckBox>
#include <QCompleter>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>

namespace TciMon {

namespace {

constexpr const char* kCaptionStyle =
    "QLabel { color: #6b8099; font-size: 9px; font-weight: bold; "
    "letter-spacing: 0.08em; }";

// Correlation window: incoming lines within this long after a send are
// echoed into the transcript so a reply lines up with its command.
constexpr qint64 kReplyWindowMs = 3000;

} // namespace

ConsolePanel::ConsolePanel(TciClient* tci, QWidget* parent)
    : QWidget(parent),
      m_tci(tci),
      m_watchdog(new QTimer(this)),
      m_sinceSend(new QElapsedTimer)
{
    m_watchdog->setSingleShot(true);
    connect(m_watchdog, &QTimer::timeout,
            this, &ConsolePanel::onWatchdogTimeout);

    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(4, 4, 4, 4);
    v->setSpacing(4);

    // ── Safety banner ──────────────────────────────────────────────────
    m_banner = new QLabel;
    m_banner->setWordWrap(true);
    v->addWidget(m_banner);

    // ── Arm + watchdog controls ────────────────────────────────────────
    {
        auto* row = new QHBoxLayout;
        m_armCheck = new QCheckBox("Arm sending");
        m_armCheck->setToolTip(
            "Unchecked = dry-run (nothing leaves the app).\n"
            "Resets to OFF every launch — never remembered.");
        connect(m_armCheck, &QCheckBox::toggled,
                this, &ConsolePanel::onArmToggled);

        auto* sl = new QLabel("SWR abort >");
        sl->setStyleSheet(kCaptionStyle);
        m_swrLimit = new QDoubleSpinBox;
        m_swrLimit->setRange(1.5, 10.0);
        m_swrLimit->setSingleStep(0.1);
        m_swrLimit->setValue(3.0);
        m_swrLimit->setDecimals(1);

        auto* tl = new QLabel("TX timeout (s)");
        tl->setStyleSheet(kCaptionStyle);
        m_txTimeout = new QSpinBox;
        m_txTimeout->setRange(2, 120);
        m_txTimeout->setValue(10);

        m_unkeyBtn = new QPushButton("UNKEY NOW");
        m_unkeyBtn->setEnabled(false);
        m_unkeyBtn->setStyleSheet(
            "QPushButton { background: #400000; color: #ff8080; "
            "font-weight: bold; border: 1px solid #ff5050; padding: 4px 10px; }"
            "QPushButton:disabled { background: #1a1414; color: #6b5050; "
            "border: 1px solid #3a2a2a; }");
        connect(m_unkeyBtn, &QPushButton::clicked,
                this, &ConsolePanel::onUnkeyNow);

        row->addWidget(m_armCheck);
        row->addSpacing(12);
        row->addWidget(sl);  row->addWidget(m_swrLimit);
        row->addSpacing(8);
        row->addWidget(tl);  row->addWidget(m_txTimeout);
        row->addStretch();
        row->addWidget(m_unkeyBtn);
        v->addLayout(row);
    }

    // ── Command entry ──────────────────────────────────────────────────
    {
        auto* row = new QHBoxLayout;
        m_cmdEdit = new QLineEdit;
        m_cmdEdit->setPlaceholderText("TCI command, e.g. vfo:0,0,14074000");
        m_cmdEdit->setStyleSheet(
            "QLineEdit { font-family: Consolas, 'Cascadia Mono', monospace; }");

        QStringList names;
        for (const TciCommand& c : allTciCommands())
            names << c.name;
        names.sort();
        auto* comp = new QCompleter(names, this);
        comp->setCaseSensitivity(Qt::CaseInsensitive);
        comp->setCompletionMode(QCompleter::PopupCompletion);
        m_cmdEdit->setCompleter(comp);
        connect(m_cmdEdit, &QLineEdit::returnPressed,
                this, &ConsolePanel::onSend);

        m_sendBtn = new QPushButton("Send");
        connect(m_sendBtn, &QPushButton::clicked,
                this, &ConsolePanel::onSend);

        row->addWidget(m_cmdEdit, 1);
        row->addWidget(m_sendBtn);
        v->addLayout(row);
    }

    auto* tcap = new QLabel("TRANSCRIPT");
    tcap->setStyleSheet(kCaptionStyle);
    v->addWidget(tcap);

    m_transcript = new QPlainTextEdit;
    m_transcript->setReadOnly(true);
    m_transcript->setMaximumBlockCount(2000);
    m_transcript->setStyleSheet(
        "QPlainTextEdit { background-color: #050a14; color: #dde6f0; "
        "border: 1px solid #1c2a40; "
        "font-family: Consolas, 'Cascadia Mono', monospace; "
        "font-size: 11px; }");
    v->addWidget(m_transcript, 1);

    refreshBanner();
}

ConsolePanel::~ConsolePanel()
{
    delete m_sinceSend;
}

bool ConsolePanel::isTxClass(const QString& cmd)
{
    // Commands that can put RF on the antenna.
    return cmd == "trx" || cmd == "tune";
}

bool ConsolePanel::isKeyDown(const QString& cmd, const QString& args)
{
    if (cmd != "trx" && cmd != "tune") return false;
    // trx:0,true; / tune:0,true; — a "true" argument keys the carrier.
    const QStringList parts = args.split(',', Qt::SkipEmptyParts);
    for (const QString& p : parts)
        if (p.trimmed().compare("true", Qt::CaseInsensitive) == 0)
            return true;
    return false;
}

void ConsolePanel::appendLine(const QString& text, const QString& colorHex)
{
    const QString ts = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    m_transcript->appendHtml(
        QString("<span style='color:#6b8099'>%1</span> "
                "<span style='color:%2'>%3</span>")
            .arg(ts, colorHex, text.toHtmlEscaped()));
}

void ConsolePanel::onArmToggled(bool checked)
{
    if (checked) {
        const auto r = QMessageBox::warning(this, "Arm transmit-capable sending",
            "You are enabling live command sending.\n\n"
            "Commands will be transmitted to the connected TCI server. "
            "trx/tune can key your radio onto the antenna.\n\n"
            "A watchdog will auto-unkey on SWR limit or TX timeout, but "
            "you are responsible for what you send.\n\nArm sending now?",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (r != QMessageBox::Yes) {
            m_armCheck->setChecked(false);   // recurses with checked=false
            return;
        }
    }
    m_armed = checked;
    m_unkeyBtn->setEnabled(checked);
    if (!checked && m_watchdog->isActive()) m_watchdog->stop();
    refreshBanner();
    appendLine(checked ? "[armed] live sending enabled"
                       : "[safe] dry-run — nothing is transmitted",
               checked ? "#ff8080" : "#6b8099");
}

void ConsolePanel::refreshBanner()
{
    if (m_armed) {
        m_banner->setText("● ARMED — commands are transmitted to the radio. "
                          "trx/tune can key the antenna.");
        m_banner->setStyleSheet(
            "QLabel { background:#400000; color:#ff8080; "
            "border:1px solid #ff5050; padding:5px; font-weight:bold; }");
    } else {
        m_banner->setText("○ DRY-RUN — nothing is transmitted. "
                          "Tick \"Arm sending\" to send for real.");
        m_banner->setStyleSheet(
            "QLabel { background:#0a1320; color:#6b8099; "
            "border:1px solid #1c2a40; padding:5px; }");
    }
}

void ConsolePanel::onSend()
{
    QString text = m_cmdEdit->text().trimmed();
    if (text.isEmpty()) return;
    if (!text.endsWith(';')) text += ';';

    const int colon = text.indexOf(':');
    const int semi  = text.indexOf(';');
    const QString cmd =
        (colon < 0 ? text.left(semi) : text.left(colon)).trimmed().toLower();
    const QString args = (colon < 0 || semi < 0)
        ? QString()
        : text.mid(colon + 1, semi - colon - 1);

    if (!m_armed) {
        appendLine(QString("DRY-RUN  would send: %1").arg(text), "#ffaa00");
        m_cmdEdit->clear();
        return;
    }

    if (isTxClass(cmd)) {
        const auto r = QMessageBox::warning(this, "Confirm TX-class command",
            QString("This is a transmit-capable command:\n\n    %1\n\n"
                    "It can key your radio. Send it?").arg(text),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (r != QMessageBox::Yes) {
            appendLine(QString("cancelled: %1").arg(text), "#6b8099");
            return;
        }
    }

    m_tci->send(text);
    m_sinceSend->restart();
    appendLine(QString("SENT  %1").arg(text), "#4cff7c");
    m_cmdEdit->clear();

    if (isKeyDown(cmd, args)) {
        m_keyed = true;
        m_watchdog->start(m_txTimeout->value() * 1000);
        appendLine(QString("watchdog armed — auto-unkey on SWR>%1 or %2 s")
                       .arg(m_swrLimit->value(), 0, 'f', 1)
                       .arg(m_txTimeout->value()),
                   "#ffaa00");
    } else if ((cmd == "trx" || cmd == "tune") && m_watchdog->isActive()) {
        // An explicit unkey clears a running watchdog.
        m_watchdog->stop();
        m_keyed = false;
    }
}

void ConsolePanel::onWatchdogTimeout()
{
    doAbort(QString("TX timeout (%1 s)").arg(m_txTimeout->value()));
}

void ConsolePanel::onUnkeyNow()
{
    doAbort("manual unkey");
}

void ConsolePanel::doAbort(const QString& reason)
{
    m_watchdog->stop();
    m_keyed = false;
    if (m_tci) {
        m_tci->send("trx:0,false;");
        m_tci->send("tune:0,false;");
    }
    appendLine(QString("ABORT (%1) — sent trx:0,false; tune:0,false;")
                   .arg(reason),
               "#ff5050");
}

void ConsolePanel::noteIncoming(const QString& line)
{
    const int colon = line.indexOf(':');
    const QString cmd =
        (colon < 0 ? line : line.left(colon)).trimmed().toLower();

    // Watchdog: tx_sensors:0,mic,fwd,peak,swr — abort over the SWR limit.
    if (m_keyed && cmd == "tx_sensors" && colon >= 0) {
        const QStringList a = line.mid(colon + 1).split(',');
        if (a.size() >= 5) {
            bool ok = false;
            const double swr = a[4].trimmed().toDouble(&ok);
            if (ok && swr > m_swrLimit->value()) {
                doAbort(QString("SWR %1 > %2")
                            .arg(swr, 0, 'f', 2)
                            .arg(m_swrLimit->value(), 0, 'f', 1));
                return;
            }
        }
    }

    // Correlate replies: echo lines that arrive shortly after a send.
    if (m_sinceSend->isValid() && m_sinceSend->elapsed() < kReplyWindowMs)
        appendLine(QString("< %1").arg(line), "#7c9cff");
}

} // namespace TciMon
