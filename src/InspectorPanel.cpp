#include "InspectorPanel.h"

#include "TciCommands.h"

#include <QDateTime>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace TciMon {

namespace {

constexpr const char* kCaptionStyle =
    "QLabel { color: #6b8099; font-size: 9px; font-weight: bold; "
    "letter-spacing: 0.08em; }";

// Number of comma-separated arguments a reference syntax template
// declares, e.g. "vfo:<rx>,<vfo>,<hz>;" → 3, "ready;" → 0.
int expectedArgs(const QString& syntax)
{
    const int colon = syntax.indexOf(':');
    if (colon < 0) return 0;                       // e.g. "ready;"
    QString body = syntax.mid(colon + 1);
    const int semi = body.indexOf(';');
    if (semi >= 0) body = body.left(semi);
    body = body.trimmed();
    if (body.isEmpty()) return 0;
    return static_cast<int>(body.split(',').size());
}

} // namespace

InspectorPanel::InspectorPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(4, 4, 4, 4);
    v->setSpacing(4);

    auto* top = new QHBoxLayout;
    auto* cap = new QLabel("PROTOCOL INSPECTOR");
    cap->setStyleSheet(kCaptionStyle);
    m_filter = new QLineEdit;
    m_filter->setPlaceholderText("Filter command…");
    m_filter->setMaximumWidth(200);
    connect(m_filter, &QLineEdit::textChanged,
            this, &InspectorPanel::onFilterChanged);
    auto* clearBtn = new QPushButton("Reset stats");
    connect(clearBtn, &QPushButton::clicked, this, &InspectorPanel::clearAll);
    top->addWidget(cap);
    top->addStretch();
    top->addWidget(m_filter);
    top->addWidget(clearBtn);
    v->addLayout(top);

    m_table = new QTableWidget;
    m_table->setColumnCount(6);
    m_table->setHorizontalHeaderLabels(
        {"Command", "Count", "Rate /min", "Last value", "Syntax", "Compliance"});
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setShowGrid(false);
    m_table->setStyleSheet(
        "QTableWidget { background-color: #050a14; "
        "alternate-background-color: #0a1320; color: #dde6f0; "
        "border: 1px solid #1c2a40; "
        "font-family: Consolas, 'Cascadia Mono', monospace; "
        "font-size: 11px; gridline-color: transparent; }");
    m_table->setColumnWidth(0, 130);
    m_table->setColumnWidth(1, 70);
    m_table->setColumnWidth(2, 80);
    m_table->setColumnWidth(3, 240);
    m_table->setColumnWidth(4, 220);
    v->addWidget(m_table, 1);

    m_summary = new QLabel("0 command types");
    m_summary->setStyleSheet(kCaptionStyle);
    v->addWidget(m_summary);

    m_tick = new QTimer(this);
    m_tick->setInterval(1000);
    connect(m_tick, &QTimer::timeout, this, &InspectorPanel::refresh);
    m_tick->start();
}

void InspectorPanel::ingest(const QString& line)
{
    const int colon = line.indexOf(':');
    const QString cmd =
        (colon < 0 ? line : line.left(colon)).trimmed().toLower();
    if (cmd.isEmpty()) return;

    const int actualArgs = colon < 0
        ? 0
        : static_cast<int>(line.mid(colon + 1).split(',').size());

    Stat& s = m_stats[cmd];
    ++s.count;
    s.lastValue  = colon < 0 ? QString() : line.mid(colon + 1).trimmed();
    s.lastSeen   = QDateTime::currentDateTime().toString("HH:mm:ss");
    s.compliance = complianceFor(cmd, actualArgs);
    m_dirty = true;
}

QString InspectorPanel::complianceFor(const QString& cmd, int actualArgs)
{
    const TciCommand* ref = findTciCommand(cmd);
    if (!ref) return QStringLiteral("unknown cmd");
    const int want = expectedArgs(ref->syntax);
    // Servers legitimately append extra trailing fields (e.g. spot
    // descriptions with commas), so only *too few* args is suspect.
    if (actualArgs < want)
        return QString("few args (%1<%2)").arg(actualArgs).arg(want);
    return QStringLiteral("ok");
}

void InspectorPanel::onFilterChanged(const QString& text)
{
    m_filterText = text.trimmed().toLower();
    m_dirty = true;
    refresh();
}

void InspectorPanel::refresh()
{
    // Update smoothed rate for every command on every tick (1 s).
    for (auto it = m_stats.begin(); it != m_stats.end(); ++it) {
        Stat& s = it.value();
        const double inst = static_cast<double>(s.count - s.countAtLastTick);
        s.countAtLastTick = s.count;
        // EMA so a bursty stream doesn't make the figure jitter wildly.
        s.ratePerMin = 0.4 * (inst * 60.0) + 0.6 * s.ratePerMin;
    }

    if (!m_dirty) {
        // Still repaint rates even when no new command types arrived.
        for (int r = 0; r < m_table->rowCount(); ++r) {
            auto* nameItem = m_table->item(r, 0);
            if (!nameItem) continue;
            const auto sit = m_stats.constFind(nameItem->text());
            if (sit == m_stats.constEnd()) continue;
            m_table->item(r, 2)->setText(
                QString::number(sit.value().ratePerMin, 'f', 0));
        }
        return;
    }
    m_dirty = false;

    QStringList names = m_stats.keys();
    names.sort();

    m_table->setRowCount(0);
    int shown = 0;
    for (const QString& cmd : names) {
        if (!m_filterText.isEmpty() && !cmd.contains(m_filterText))
            continue;
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        rebuildRow(cmd, m_stats.value(cmd), row);
        ++shown;
    }
    m_summary->setText(
        QString("%1 command types  •  %2 shown")
            .arg(m_stats.size()).arg(shown));
}

void InspectorPanel::rebuildRow(const QString& cmd, const Stat& s, int row)
{
    const TciCommand* ref = findTciCommand(cmd);

    auto* nameItem = new QTableWidgetItem(cmd);
    nameItem->setForeground(QColor("#00d8ef"));
    m_table->setItem(row, 0, nameItem);
    m_table->setItem(row, 1,
        new QTableWidgetItem(QString::number(s.count)));
    m_table->setItem(row, 2,
        new QTableWidgetItem(QString::number(s.ratePerMin, 'f', 0)));

    auto* valItem = new QTableWidgetItem(s.lastValue);
    valItem->setForeground(QColor("#dde6f0"));
    valItem->setToolTip(s.lastValue);
    m_table->setItem(row, 3, valItem);

    auto* synItem = new QTableWidgetItem(ref ? ref->syntax : QStringLiteral("—"));
    synItem->setForeground(QColor("#6b8099"));
    if (ref) synItem->setToolTip(ref->summary);
    m_table->setItem(row, 4, synItem);

    auto* cmpItem = new QTableWidgetItem(s.compliance);
    if (s.compliance == "ok")
        cmpItem->setForeground(QColor("#4cff7c"));
    else if (s.compliance.startsWith("unknown"))
        cmpItem->setForeground(QColor("#6b8099"));
    else
        cmpItem->setForeground(QColor("#ffaa00"));
    m_table->setItem(row, 5, cmpItem);
}

void InspectorPanel::clearAll()
{
    m_stats.clear();
    m_table->setRowCount(0);
    m_summary->setText("0 command types");
}

} // namespace TciMon
