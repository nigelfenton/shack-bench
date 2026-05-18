#include "ComparePanel.h"

#include "TciClient.h"

#include <QCheckBox>
#include <QDateTime>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSet>
#include <QSpinBox>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>

namespace TciMon {

namespace {

constexpr const char* kCaptionStyle =
    "QLabel { color: #6b8099; font-size: 9px; font-weight: bold; "
    "letter-spacing: 0.08em; }";

constexpr const char* kDotOn  = "QLabel { color: #4cff7c; font-size: 14px; }";
constexpr const char* kDotOff = "QLabel { color: #6b8099; font-size: 14px; }";

constexpr int kMaxObservers = 4;

} // namespace

QString ComparePanel::colorFor(int idx)
{
    static const char* pal[] = { "#00d8ef", "#ffaa00", "#4cff7c", "#ff5cff" };
    return pal[idx % 4];
}

ComparePanel::ComparePanel(QWidget* parent)
    : QWidget(parent)
{
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(4, 4, 4, 4);
    v->setSpacing(6);

    auto* cap = new QLabel("OBSERVERS  (independent read-only TCI clients)");
    cap->setStyleSheet(kCaptionStyle);
    v->addWidget(cap);

    m_obsBox = new QWidget;
    auto* obsLay = new QVBoxLayout(m_obsBox);
    obsLay->setContentsMargins(0, 0, 0, 0);
    obsLay->setSpacing(3);
    v->addWidget(m_obsBox);

    m_addBtn = new QPushButton("+ Add observer");
    connect(m_addBtn, &QPushButton::clicked,
            this, &ComparePanel::onAddObserver);
    {
        auto* r = new QHBoxLayout;
        r->addWidget(m_addBtn);
        r->addStretch();
        m_onlyMismatch = new QCheckBox("Show only mismatching commands");
        connect(m_onlyMismatch, &QCheckBox::toggled, this, [this](bool) {
            m_dirty = true; refreshDiff();
        });
        r->addWidget(m_onlyMismatch);
        v->addLayout(r);
    }

    auto* dcap = new QLabel("VALUE MATRIX  (red = observers disagree)");
    dcap->setStyleSheet(kCaptionStyle);
    v->addWidget(dcap);

    m_diff = new QTableWidget;
    m_diff->verticalHeader()->setVisible(false);
    m_diff->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_diff->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_diff->setAlternatingRowColors(true);
    m_diff->setShowGrid(false);
    m_diff->setStyleSheet(
        "QTableWidget { background-color: #050a14; "
        "alternate-background-color: #0a1320; color: #dde6f0; "
        "border: 1px solid #1c2a40; "
        "font-family: Consolas, 'Cascadia Mono', monospace; "
        "font-size: 11px; gridline-color: transparent; }");
    v->addWidget(m_diff, 2);

    auto* scap = new QLabel("MERGED STREAM");
    scap->setStyleSheet(kCaptionStyle);
    v->addWidget(scap);

    m_stream = new QPlainTextEdit;
    m_stream->setReadOnly(true);
    m_stream->setMaximumBlockCount(4000);
    m_stream->setStyleSheet(
        "QPlainTextEdit { background-color: #050a14; color: #dde6f0; "
        "border: 1px solid #1c2a40; "
        "font-family: Consolas, 'Cascadia Mono', monospace; "
        "font-size: 11px; }");
    v->addWidget(m_stream, 1);

    m_tick = new QTimer(this);
    m_tick->setInterval(1000);
    connect(m_tick, &QTimer::timeout, this, &ComparePanel::refreshDiff);
    m_tick->start();

    // Start with two observers — the common "same server, two clients" case.
    addObserverRow("127.0.0.1", 40001);
    addObserverRow("127.0.0.1", 40001);
}

void ComparePanel::onAddObserver()
{
    if (m_obs.size() >= kMaxObservers) {
        m_addBtn->setEnabled(false);
        return;
    }
    addObserverRow("127.0.0.1", 40001);
}

void ComparePanel::addObserverRow(const QString& host, quint16 port)
{
    const int idx = m_obs.size();
    Observer o;
    o.name = QString(QChar('A' + idx));

    auto* row = new QHBoxLayout;
    auto* tag = new QLabel(QString("OBS %1").arg(o.name));
    tag->setStyleSheet(QString("QLabel { color:%1; font-weight:bold; "
                               "font-size:11px; }").arg(colorFor(idx)));
    tag->setMinimumWidth(48);

    o.host = new QLineEdit(host);
    o.host->setMaximumWidth(150);
    o.port = new QSpinBox;
    o.port->setRange(1, 65535);
    o.port->setValue(port);
    o.btn  = new QPushButton("Connect");
    o.dot  = new QLabel("●");
    o.dot->setStyleSheet(kDotOff);

    row->addWidget(tag);
    row->addWidget(o.host);
    row->addWidget(o.port);
    row->addWidget(o.btn);
    row->addWidget(o.dot);
    row->addStretch();
    qobject_cast<QVBoxLayout*>(m_obsBox->layout())->addLayout(row);

    o.tci = new TciClient(this);
    m_obs.append(o);

    connect(o.btn, &QPushButton::clicked, this,
            [this, idx]() { toggleObserver(idx); });
    connect(o.tci, &TciClient::rawMessageReceived, this,
            [this, idx](const QString& l) { onObserverLine(idx, l); });
    connect(o.tci, &TciClient::connectionChanged, this,
            [this, idx](bool up) { onObserverConn(idx, up); });

    if (m_obs.size() >= kMaxObservers) m_addBtn->setEnabled(false);
}

void ComparePanel::toggleObserver(int idx)
{
    if (idx < 0 || idx >= m_obs.size()) return;
    Observer& o = m_obs[idx];
    if (o.connected) {
        o.tci->disconnectFromServer();
    } else {
        o.tci->connectToServer(o.host->text().trimmed(),
                               static_cast<quint16>(o.port->value()));
        o.btn->setText("Connecting…");
    }
}

void ComparePanel::onObserverConn(int idx, bool up)
{
    if (idx < 0 || idx >= m_obs.size()) return;
    Observer& o = m_obs[idx];
    o.connected = up;
    o.dot->setStyleSheet(up ? kDotOn : kDotOff);
    o.btn->setText(up ? "Disconnect" : "Connect");
    o.host->setEnabled(!up);
    o.port->setEnabled(!up);

    const QString ts = QDateTime::currentDateTime().toString("HH:mm:ss");
    m_stream->appendHtml(
        QString("<span style='color:#6b8099'>%1</span> "
                "<span style='color:%2'>[OBS %3] %4</span>")
            .arg(ts, colorFor(idx), o.name,
                 up ? QStringLiteral("connected")
                    : QStringLiteral("disconnected")));
}

void ComparePanel::onObserverLine(int idx, const QString& line)
{
    if (idx < 0 || idx >= m_obs.size()) return;
    Observer& o = m_obs[idx];

    const int colon = line.indexOf(':');
    const QString cmd =
        (colon < 0 ? line : line.left(colon)).trimmed().toLower();
    const QString args = colon < 0 ? QString() : line.mid(colon + 1).trimmed();
    if (cmd.isEmpty()) return;

    o.latest.insert(cmd, args);
    m_dirty = true;

    const QString ts = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    m_stream->appendHtml(
        QString("<span style='color:#6b8099'>%1</span> "
                "<span style='color:%2'>[%3]</span> "
                "<span style='color:#dde6f0'>%4</span>")
            .arg(ts, colorFor(idx), o.name, line.toHtmlEscaped()));
}

void ComparePanel::refreshDiff()
{
    if (!m_dirty) return;
    m_dirty = false;

    const int n = m_obs.size();
    QSet<QString> cmdSet;
    for (const Observer& o : m_obs)
        for (auto it = o.latest.constBegin(); it != o.latest.constEnd(); ++it)
            cmdSet.insert(it.key());
    QStringList cmds(cmdSet.begin(), cmdSet.end());
    cmds.sort();

    m_diff->setColumnCount(1 + n);
    QStringList headers{ "Command" };
    for (const Observer& o : m_obs) headers << QString("OBS %1").arg(o.name);
    m_diff->setHorizontalHeaderLabels(headers);
    m_diff->horizontalHeader()->setStretchLastSection(true);
    m_diff->setColumnWidth(0, 130);

    m_diff->setRowCount(0);
    const bool onlyMis = m_onlyMismatch->isChecked();

    for (const QString& cmd : cmds) {
        // Distinct non-empty values across observers that have seen it.
        QSet<QString> distinct;
        int seenBy = 0;
        for (const Observer& o : m_obs) {
            const auto it = o.latest.constFind(cmd);
            if (it != o.latest.constEnd()) {
                ++seenBy;
                distinct.insert(it.value());
            }
        }
        const bool mismatch =
            (distinct.size() > 1) ||
            (seenBy > 0 && seenBy < n && distinct.size() >= 1 && n > 1
             && seenBy != n);
        // A command only one observer ever saw is also a divergence worth
        // showing, but value disagreement is the strong signal.
        const bool valueDisagree = distinct.size() > 1;
        if (onlyMis && !mismatch) continue;

        const int row = m_diff->rowCount();
        m_diff->insertRow(row);
        auto* c0 = new QTableWidgetItem(cmd);
        c0->setForeground(QColor("#00d8ef"));
        m_diff->setItem(row, 0, c0);

        for (int i = 0; i < n; ++i) {
            const auto it = m_obs[i].latest.constFind(cmd);
            const QString val = (it == m_obs[i].latest.constEnd())
                ? QStringLiteral("—") : it.value();
            auto* cell = new QTableWidgetItem(val);
            if (it == m_obs[i].latest.constEnd())
                cell->setForeground(QColor("#3a4a5a"));
            else if (valueDisagree)
                cell->setForeground(QColor("#ff8080"));
            else
                cell->setForeground(QColor("#dde6f0"));
            cell->setToolTip(val);
            m_diff->setItem(row, 1 + i, cell);
        }
        if (valueDisagree) {
            for (int col = 0; col <= n; ++col)
                if (auto* it = m_diff->item(row, col))
                    it->setBackground(QColor(60, 0, 0));
        }
    }
}

} // namespace TciMon
