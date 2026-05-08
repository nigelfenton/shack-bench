#include "MainWindow.h"

#include "TciClient.h"

#include <QCheckBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QFile>
#include <QTextStream>

namespace TciMon {

namespace {

constexpr const char* kStatusOnline =
    "QLabel { color: #4cff7c; font-size: 14px; }";
constexpr const char* kStatusOffline =
    "QLabel { color: #6b8099; font-size: 14px; }";

constexpr const char* kBigValueStyle =
    "QLabel { color: #ffd400; font-size: 18px; font-weight: bold; "
    "font-family: Consolas, 'Cascadia Mono', monospace; }";

constexpr const char* kCaptionStyle =
    "QLabel { color: #6b8099; font-size: 9px; font-weight: bold; "
    "letter-spacing: 0.08em; }";

// Color used in the raw log for each message family.
constexpr const char* kColorVfo     = "#00d8ef";   // cyan
constexpr const char* kColorMode    = "#00d8ef";
constexpr const char* kColorSpot    = "#4cff7c";   // green
constexpr const char* kColorSpotDel = "#ffaa00";   // amber
constexpr const char* kColorMeta    = "#6b8099";   // muted (start, ready, protocol)
constexpr const char* kColorErr     = "#ff5050";
constexpr const char* kColorOther   = "#dde6f0";   // default

// Format a frequency string sourced from a TCI Hz integer.
QString hzToMhz(const QString& hzStr)
{
    bool ok = false;
    qint64 hz = hzStr.trimmed().toLongLong(&ok);
    if (!ok) return hzStr;
    return QString::number(hz / 1.0e6, 'f', 6);
}

// Decode an RGB color string (hex or decimal) to a CSS hex string for a
// swatch.  Returns empty string if it can't be parsed.
QString tciColorToCss(const QString& raw)
{
    const QString s = raw.trimmed();
    if (s.isEmpty()) return {};
    bool ok = false;
    quint32 v = s.toUInt(&ok, /*base*/ 0);   // auto-detect 0x prefix
    if (!ok) v = s.toUInt(&ok, 10);
    if (!ok) return {};
    return QString("#%1").arg(v & 0xFFFFFF, 6, 16, QChar('0')).toUpper();
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      m_tci(new TciClient(this))
{
    setWindowTitle("TCI Monitor");
    resize(1200, 720);

    buildUI();

    connect(m_tci, &TciClient::connectionChanged, this, &MainWindow::onConnectionChanged);
    connect(m_tci, &TciClient::rawMessageReceived, this, &MainWindow::onRawMessage);
    connect(m_tci, &TciClient::errorTextReceived, this, &MainWindow::onErrorText);

    refreshStatus();
}

MainWindow::~MainWindow() = default;

// ── UI ────────────────────────────────────────────────────────────────────

void MainWindow::buildUI()
{
    auto* central = new QWidget;
    setCentralWidget(central);
    auto* main = new QVBoxLayout(central);
    main->setContentsMargins(8, 6, 8, 6);
    main->setSpacing(6);

    // ── Connect bar ────────────────────────────────────────────────────
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(6);

        auto* lh = new QLabel("HOST"); lh->setStyleSheet(kCaptionStyle);
        m_hostEdit = new QLineEdit("127.0.0.1");
        m_hostEdit->setMinimumWidth(160);

        auto* lp = new QLabel("PORT"); lp->setStyleSheet(kCaptionStyle);
        m_portSpin = new QSpinBox;
        m_portSpin->setRange(1, 65535);
        // AetherSDR's TCI default is 40001.  ExpertSDR2 / SunSDR use 50001.
        // Since AetherSDR is the most common target here, default to 40001
        // and let the user override in the spinbox.
        m_portSpin->setValue(40001);

        m_connectBtn    = new QPushButton("Connect");
        m_disconnectBtn = new QPushButton("Disconnect");
        m_disconnectBtn->setEnabled(false);
        connect(m_connectBtn,    &QPushButton::clicked, this, &MainWindow::onConnectClicked);
        connect(m_disconnectBtn, &QPushButton::clicked, this, &MainWindow::onDisconnectClicked);

        m_statusDot  = new QLabel("●");
        m_statusDot->setStyleSheet(kStatusOffline);
        m_statusText = new QLabel("Offline");
        m_statusText->setStyleSheet(kCaptionStyle);
        m_statusText->setMinimumWidth(80);

        row->addWidget(lh); row->addWidget(m_hostEdit);
        row->addSpacing(8);
        row->addWidget(lp); row->addWidget(m_portSpin);
        row->addSpacing(8);
        row->addWidget(m_connectBtn);
        row->addWidget(m_disconnectBtn);
        row->addStretch();
        row->addWidget(m_statusDot);
        row->addWidget(m_statusText);
        main->addLayout(row);
    }

    // ── Splitter: parsed | raw ─────────────────────────────────────────
    auto* split = new QSplitter(Qt::Horizontal);

    // Left — parsed view
    {
        auto* left = new QWidget;
        auto* lv = new QVBoxLayout(left);
        lv->setContentsMargins(4, 4, 4, 4);
        lv->setSpacing(4);

        // Current VFO + Mode
        auto* hdr = new QHBoxLayout;
        auto* vfoBlock = new QVBoxLayout; vfoBlock->setSpacing(0);
        auto* lvf = new QLabel("VFO 0 / RX 0"); lvf->setStyleSheet(kCaptionStyle);
        m_curVfo = new QLabel("—");
        m_curVfo->setStyleSheet(kBigValueStyle);
        m_curVfo->setMinimumWidth(160);
        vfoBlock->addWidget(lvf); vfoBlock->addWidget(m_curVfo);

        auto* modeBlock = new QVBoxLayout; modeBlock->setSpacing(0);
        auto* lmd = new QLabel("MODE"); lmd->setStyleSheet(kCaptionStyle);
        m_curMode = new QLabel("—");
        m_curMode->setStyleSheet(kBigValueStyle);
        m_curMode->setMinimumWidth(80);
        modeBlock->addWidget(lmd); modeBlock->addWidget(m_curMode);

        hdr->addLayout(vfoBlock);
        hdr->addLayout(modeBlock);
        hdr->addStretch();
        lv->addLayout(hdr);

        auto* lst = new QLabel("SPOTS");
        lst->setStyleSheet(kCaptionStyle);
        lv->addWidget(lst);

        m_spotTable = new QTableWidget;
        m_spotTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_spotTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_spotTable->setSelectionMode(QAbstractItemView::SingleSelection);
        m_spotTable->setAlternatingRowColors(true);
        m_spotTable->verticalHeader()->setVisible(false);
        m_spotTable->setColumnCount(6);
        m_spotTable->setHorizontalHeaderLabels(
            {"Time", "Call", "Mode", "Freq (MHz)", "Color", "Description"});
        m_spotTable->horizontalHeader()->setStretchLastSection(true);
        lv->addWidget(m_spotTable, 1);

        split->addWidget(left);
    }

    // Right — raw log
    {
        auto* right = new QWidget;
        auto* rv = new QVBoxLayout(right);
        rv->setContentsMargins(4, 4, 4, 4);
        rv->setSpacing(4);

        // First control row: title + filter + autoscroll + pause
        auto* topRow = new QHBoxLayout;
        auto* lr = new QLabel("RAW MESSAGES");
        lr->setStyleSheet(kCaptionStyle);
        m_filterEdit = new QLineEdit;
        m_filterEdit->setPlaceholderText("Filter (substring)…");
        m_filterEdit->setMaximumWidth(220);
        connect(m_filterEdit, &QLineEdit::textChanged,
                this, &MainWindow::onFilterChanged);
        m_autoscrollCheck = new QCheckBox("Autoscroll");
        m_autoscrollCheck->setChecked(true);
        m_pauseBtn = new QPushButton("Pause");
        m_pauseBtn->setCheckable(true);
        m_pauseBtn->setStyleSheet(
            "QPushButton:checked { background: #003040; "
            "border: 1px solid #ffaa00; color: #ffaa00; }");
        connect(m_pauseBtn, &QPushButton::toggled,
                this, &MainWindow::onPauseToggled);
        topRow->addWidget(lr);
        topRow->addStretch();
        topRow->addWidget(m_filterEdit);
        topRow->addWidget(m_autoscrollCheck);
        topRow->addWidget(m_pauseBtn);
        rv->addLayout(topRow);

        // Second control row: suppression status
        auto* supRow = new QHBoxLayout;
        m_suppressLabel = new QLabel("0 suppressions");
        m_suppressLabel->setStyleSheet(kCaptionStyle);
        m_clearSuppressBtn = new QPushButton("Show / clear…");
        m_clearSuppressBtn->setEnabled(false);
        connect(m_clearSuppressBtn, &QPushButton::clicked,
                this, &MainWindow::onShowSuppressions);
        supRow->addStretch();
        supRow->addWidget(m_suppressLabel);
        supRow->addWidget(m_clearSuppressBtn);
        rv->addLayout(supRow);

        m_logTable = new QTableWidget;
        m_logTable->setColumnCount(3);
        m_logTable->setHorizontalHeaderLabels({"Time", "Cmd", "Message"});
        m_logTable->verticalHeader()->setVisible(false);
        m_logTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_logTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_logTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
        m_logTable->horizontalHeader()->setStretchLastSection(true);
        m_logTable->horizontalHeader()->setMinimumSectionSize(60);
        m_logTable->setColumnWidth(0, 100);
        m_logTable->setColumnWidth(1, 110);
        m_logTable->setShowGrid(false);
        m_logTable->setAlternatingRowColors(true);
        m_logTable->setStyleSheet(
            "QTableWidget { background-color: #050a14; "
            "alternate-background-color: #0a1320; "
            "color: #dde6f0; border: 1px solid #1c2a40; "
            "font-family: Consolas, 'Cascadia Mono', monospace; "
            "font-size: 11px; gridline-color: transparent; }");
        m_logTable->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(m_logTable, &QTableWidget::customContextMenuRequested,
                this, &MainWindow::onLogContextMenu);
        rv->addWidget(m_logTable, 1);

        split->addWidget(right);
    }

    split->setStretchFactor(0, 2);
    split->setStretchFactor(1, 3);
    main->addWidget(split, 1);

    // ── Bottom action row ──────────────────────────────────────────────
    {
        auto* row = new QHBoxLayout;
        m_saveBtn  = new QPushButton("Save log…");
        m_clearBtn = new QPushButton("Clear log");
        connect(m_saveBtn,  &QPushButton::clicked, this, &MainWindow::onSaveLog);
        connect(m_clearBtn, &QPushButton::clicked, this, &MainWindow::onClearLog);
        row->addStretch();
        row->addWidget(m_saveBtn);
        row->addWidget(m_clearBtn);
        main->addLayout(row);
    }

    // ── Status bar counters ────────────────────────────────────────────
    m_sbMsgCount  = new QLabel("0 msgs");
    m_sbSpotCount = new QLabel("0 spots");
    statusBar()->addPermanentWidget(m_sbMsgCount);
    statusBar()->addPermanentWidget(m_sbSpotCount);
}

// ── Connection ───────────────────────────────────────────────────────────

void MainWindow::onConnectClicked()
{
    const QString host  = m_hostEdit->text().trimmed();
    const quint16 port  = static_cast<quint16>(m_portSpin->value());
    appendLog(QString("[connect] ws://%1:%2").arg(host).arg(port), kColorMeta);
    m_tci->connectToServer(host, port);
}

void MainWindow::onDisconnectClicked()
{
    appendLog("[disconnect] user requested", kColorMeta);
    m_tci->disconnectFromServer();
}

void MainWindow::onConnectionChanged(bool connected)
{
    m_connectBtn->setEnabled(!connected);
    m_disconnectBtn->setEnabled(connected);
    m_statusDot->setStyleSheet(connected ? kStatusOnline : kStatusOffline);
    m_statusText->setText(connected ? "Connected" : "Offline");
    appendLog(connected ? "[connected]" : "[disconnected]", kColorMeta);
}

void MainWindow::onErrorText(const QString& text)
{
    appendLog(QString("[error] %1").arg(text), kColorErr);
}

void MainWindow::refreshStatus()
{
    m_sbMsgCount->setText(QString("%1 msgs").arg(m_msgCount));
    m_sbSpotCount->setText(QString("%1 spots").arg(m_spotCount));
}

// ── Raw message handling + parsing ───────────────────────────────────────

void MainWindow::onRawMessage(const QString& line)
{
    ++m_msgCount;

    // Pick a color based on the leading command name.
    const int colon = line.indexOf(':');
    QString cmd = colon < 0 ? line : line.left(colon);
    cmd = cmd.trimmed().toLower();

    QString color = kColorOther;
    if      (cmd == "vfo")           color = kColorVfo;
    else if (cmd == "mode")          color = kColorMode;
    else if (cmd == "spot")          color = kColorSpot;
    else if (cmd == "spot_delete" || cmd == "spot_clear") color = kColorSpotDel;
    else if (cmd == "protocol" || cmd == "ready" || cmd == "start")
                                     color = kColorMeta;

    appendLog(line, color);
    parseLine(line);
    refreshStatus();
}

void MainWindow::parseLine(const QString& line)
{
    const int colon = line.indexOf(':');
    if (colon < 0) return;
    const QString cmd  = line.left(colon).trimmed().toLower();
    const QString rest = line.mid(colon + 1);
    const QStringList args = rest.split(',', Qt::KeepEmptyParts);

    if      (cmd == "vfo")          handleVfo(args);
    else if (cmd == "mode")         handleMode(args);
    else if (cmd == "spot")         handleSpot(args);
    else if (cmd == "spot_delete")  handleSpotDelete(args);
    else if (cmd == "spot_clear")   handleSpotClear();
}

void MainWindow::handleVfo(const QStringList& args)
{
    // vfo:rx,vfo,freqHz — report only RX 0 / VFO 0 in the parsed pane.
    if (args.size() < 3) return;
    if (args[0].trimmed() != "0" || args[1].trimmed() != "0") return;
    m_curVfo->setText(hzToMhz(args[2]) + " MHz");
}

void MainWindow::handleMode(const QStringList& args)
{
    if (args.size() < 2) return;
    if (args[0].trimmed() != "0") return;
    m_curMode->setText(args[1].trimmed().toUpper());
}

void MainWindow::handleSpot(const QStringList& args)
{
    // spot:call,mode,freqHz,color,description (TCI 1.10 form).  Some
    // servers omit one or more trailing fields — handle gracefully.
    QString call = args.value(0).trimmed();
    QString mode = args.value(1).trimmed();
    QString hz   = args.value(2).trimmed();
    QString col  = args.value(3).trimmed();
    // description may itself contain commas — rejoin everything from
    // index 4 onward.
    QString desc;
    if (args.size() > 4) {
        QStringList tail = args.mid(4);
        desc = tail.join(',').trimmed();
    }
    if (call.isEmpty()) return;

    ++m_spotCount;
    const int row = m_spotTable->rowCount();
    m_spotTable->insertRow(row);
    auto* tItem = new QTableWidgetItem(QDateTime::currentDateTime().toString("HH:mm:ss"));
    m_spotTable->setItem(row, 0, tItem);
    m_spotTable->setItem(row, 1, new QTableWidgetItem(call));
    m_spotTable->setItem(row, 2, new QTableWidgetItem(mode));
    m_spotTable->setItem(row, 3, new QTableWidgetItem(hzToMhz(hz)));

    auto* colItem = new QTableWidgetItem(col);
    const QString css = tciColorToCss(col);
    if (!css.isEmpty()) {
        QColor c{css};
        colItem->setBackground(c);
        // Pick contrasting foreground so the value stays legible.
        const int luma = (299 * c.red() + 587 * c.green() + 114 * c.blue()) / 1000;
        colItem->setForeground(QColor(luma > 128 ? "#000000" : "#FFFFFF"));
    }
    m_spotTable->setItem(row, 4, colItem);
    m_spotTable->setItem(row, 5, new QTableWidgetItem(desc));

    m_spotTable->resizeColumnsToContents();
    m_spotTable->scrollToBottom();
}

void MainWindow::handleSpotDelete(const QStringList& args)
{
    // spot_delete:call — remove rows matching the call.  Keeps the
    // table aligned with what's currently shown on the radio.
    if (args.isEmpty()) return;
    const QString call = args[0].trimmed();
    for (int r = m_spotTable->rowCount() - 1; r >= 0; --r) {
        auto* it = m_spotTable->item(r, 1);
        if (it && it->text() == call) m_spotTable->removeRow(r);
    }
}

void MainWindow::handleSpotClear()
{
    m_spotTable->setRowCount(0);
}

// ── Log view ──────────────────────────────────────────────────────────────

void MainWindow::appendLog(const QString& line, const QString& colorHex)
{
    // Hard drops first — these don't even bump the visible row.
    if (m_paused) return;

    const int colon = line.indexOf(':');
    const QString cmd = (colon < 0 ? line : line.left(colon)).trimmed().toLower();
    if (m_suppressed.contains(cmd)) return;

    const QString filter = m_filterEdit->text().trimmed();
    if (!filter.isEmpty() && !line.contains(filter, Qt::CaseInsensitive)) {
        return;
    }

    // Bound memory for long sessions — keep a rolling window of 50k rows.
    constexpr int kMaxRows = 50000;
    if (m_logTable->rowCount() >= kMaxRows) {
        m_logTable->removeRow(0);
    }

    const int row = m_logTable->rowCount();
    m_logTable->insertRow(row);

    const QString ts = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    auto* tsItem  = new QTableWidgetItem(ts);
    auto* cmdItem = new QTableWidgetItem(cmd);
    auto* msgItem = new QTableWidgetItem(line);
    const QColor color{colorHex};
    const QColor tsColor{"#6b8099"};
    tsItem->setForeground(tsColor);
    cmdItem->setForeground(color);
    msgItem->setForeground(color);
    m_logTable->setItem(row, 0, tsItem);
    m_logTable->setItem(row, 1, cmdItem);
    m_logTable->setItem(row, 2, msgItem);

    if (m_autoscrollCheck->isChecked()) {
        m_logTable->scrollToBottom();
    }
}

void MainWindow::onClearLog()
{
    m_logTable->setRowCount(0);
    m_msgCount  = 0;
    m_spotCount = 0;
    refreshStatus();
}

void MainWindow::onSaveLog()
{
    const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    const QString defaultName = QString("tci-monitor-%1.log").arg(stamp);
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString path = QFileDialog::getSaveFileName(this, "Save log",
                            dir + "/" + defaultName,
                            "Log files (*.log *.txt);;All files (*)");
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QMessageBox::critical(this, "Save failed", f.errorString());
        return;
    }
    QTextStream out(&f);
    for (int r = 0; r < m_logTable->rowCount(); ++r) {
        out << m_logTable->item(r, 0)->text() << "  "
            << m_logTable->item(r, 2)->text() << "\n";
    }
    f.close();
}

void MainWindow::onFilterChanged(const QString&)
{
    // Filter applies to future appends; existing rows stay put.  Tell the
    // user via a brief status-bar nudge.
    statusBar()->showMessage(
        "Filter applies to new rows — use Clear to apply retroactively", 3000);
}

// ── Pause / suppress controls ──────────────────────────────────────────

void MainWindow::onPauseToggled(bool checked)
{
    m_paused = checked;
    m_pauseBtn->setText(checked ? "Paused — click to resume" : "Pause");
    statusBar()->showMessage(checked ? "Paused — log freezes (parsed views still update)"
                                     : "Resumed",
                             2500);
}

void MainWindow::onLogContextMenu(const QPoint& pos)
{
    const auto rows = m_logTable->selectionModel()->selectedRows();
    if (rows.isEmpty()) return;

    // Pull the cmd of the row under the cursor (or the first selected row
    // if the user right-clicked outside their selection).
    const QModelIndex idx = m_logTable->indexAt(pos);
    QString cmdHere;
    if (idx.isValid()) {
        auto* it = m_logTable->item(idx.row(), 1);
        if (it) cmdHere = it->text();
    }
    if (cmdHere.isEmpty()) {
        auto* it = m_logTable->item(rows.first().row(), 1);
        if (it) cmdHere = it->text();
    }

    QMenu menu(this);
    QAction* removeAct = menu.addAction(
        rows.size() == 1 ? QString("Remove this row")
                         : QString("Remove %1 selected rows").arg(rows.size()));
    QAction* suppressAct = nullptr;
    if (!cmdHere.isEmpty()) {
        suppressAct = menu.addAction(
            QString("Suppress all '%1' messages").arg(cmdHere));
    }
    menu.addSeparator();
    QAction* showSupAct = menu.addAction("Show suppressions…");

    QAction* chosen = menu.exec(m_logTable->viewport()->mapToGlobal(pos));
    if (!chosen) return;

    if (chosen == removeAct) {
        // Remove rows in reverse so earlier indices stay valid.
        QList<int> rowNums;
        for (const auto& mi : rows) rowNums << mi.row();
        std::sort(rowNums.begin(), rowNums.end(), std::greater<int>());
        for (int r : rowNums) m_logTable->removeRow(r);
    } else if (suppressAct && chosen == suppressAct) {
        m_suppressed.insert(cmdHere);
        // Also remove existing rows of this type for instant relief.
        for (int r = m_logTable->rowCount() - 1; r >= 0; --r) {
            auto* it = m_logTable->item(r, 1);
            if (it && it->text() == cmdHere) m_logTable->removeRow(r);
        }
        m_suppressLabel->setText(QString("%1 suppression%2")
                                     .arg(m_suppressed.size())
                                     .arg(m_suppressed.size() == 1 ? "" : "s"));
        m_clearSuppressBtn->setEnabled(true);
    } else if (chosen == showSupAct) {
        onShowSuppressions();
    }
}

void MainWindow::onShowSuppressions()
{
    QDialog dlg(this);
    dlg.setWindowTitle("Suppressed message types");
    dlg.resize(360, 320);

    auto* layout = new QVBoxLayout(&dlg);
    auto* hint = new QLabel(
        "These command prefixes are dropped before reaching the log.  "
        "Select one or more and click Remove to start showing them again.");
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto* list = new QListWidget;
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    QStringList items(m_suppressed.begin(), m_suppressed.end());
    items.sort();
    list->addItems(items);
    layout->addWidget(list, 1);

    auto* row = new QHBoxLayout;
    auto* removeBtn = new QPushButton("Remove selected");
    auto* clearBtn  = new QPushButton("Clear all");
    auto* closeBtn  = new QPushButton("Close");
    row->addWidget(removeBtn);
    row->addWidget(clearBtn);
    row->addStretch();
    row->addWidget(closeBtn);
    layout->addLayout(row);

    connect(removeBtn, &QPushButton::clicked, &dlg, [&]() {
        for (auto* it : list->selectedItems()) {
            m_suppressed.remove(it->text());
            delete list->takeItem(list->row(it));
        }
    });
    connect(clearBtn, &QPushButton::clicked, &dlg, [&]() {
        m_suppressed.clear();
        list->clear();
    });
    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);

    dlg.exec();

    m_suppressLabel->setText(QString("%1 suppression%2")
                                 .arg(m_suppressed.size())
                                 .arg(m_suppressed.size() == 1 ? "" : "s"));
    m_clearSuppressBtn->setEnabled(!m_suppressed.isEmpty());
}

void MainWindow::onClearSuppressions()
{
    m_suppressed.clear();
    m_suppressLabel->setText("0 suppressions");
    m_clearSuppressBtn->setEnabled(false);
}

} // namespace TciMon
