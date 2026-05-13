#include "DiscoveryDialog.h"
#include "MdnsBrowser.h"

#include <QApplication>
#include <QBrush>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>

namespace TciMon {

namespace {
enum Col {
    ColInstance = 0,
    ColClass,
    ColModel,
    ColHostPort,
    ColIp,
    ColTciVer,
    ColAge,
    ColExtras,
    ColCount
};

QString ageLabel(qint64 lastSeenMs) {
    if (lastSeenMs == 0) return QStringLiteral("—");
    qint64 ageMs = QDateTime::currentMSecsSinceEpoch() - lastSeenMs;
    if (ageMs < 0) ageMs = 0;
    if (ageMs <  2000) return QStringLiteral("fresh");
    if (ageMs < 15000) return QStringLiteral("%1 s").arg(ageMs / 1000);
    if (ageMs < 60000) return QStringLiteral("%1 s (stale)").arg(ageMs / 1000);
    return QStringLiteral("%1 m (stale)").arg(ageMs / 60000);
}

QString extrasLabel(const QHash<QString, QString>& txt) {
    QStringList extras;
    for (auto it = txt.constBegin(); it != txt.constEnd(); ++it) {
        if (it.key().startsWith(QLatin1String("x-")))
            extras << it.key() + QChar('=') + it.value();
    }
    extras.sort();
    return extras.join(QStringLiteral(", "));
}
} // namespace

// ---------------------------------------------------------------------------

DiscoveryDialog::DiscoveryDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Discover network peripherals"));
    resize(880, 460);
    loadHidden();
    buildUI();

    m_browser = new MdnsBrowser(this);
    connect(m_browser, &MdnsBrowser::serviceFound,   this, &DiscoveryDialog::onServiceFound);
    connect(m_browser, &MdnsBrowser::serviceUpdated, this, &DiscoveryDialog::onServiceUpdated);
    connect(m_browser, &MdnsBrowser::info,           this, &DiscoveryDialog::onInfo);
    connect(m_browser, &MdnsBrowser::error,          this, &DiscoveryDialog::onError);

    if (!m_browser->start()) {
        m_status->setText(tr("Could not open mDNS socket — see log for details."));
    }

    m_ageTimer = new QTimer(this);
    m_ageTimer->setInterval(1000);
    connect(m_ageTimer, &QTimer::timeout, this, &DiscoveryDialog::onAgeTick);
    m_ageTimer->start();
}

DiscoveryDialog::~DiscoveryDialog() = default;

void DiscoveryDialog::buildUI() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    auto* hint = new QLabel(
        tr("Pick a service type or type a custom one. <code>_tci._tcp.local.</code> "
           "lists TCI peripherals under the AetherSDR schema; the rest reveal "
           "whatever else is advertising on the LAN. Double-click a row to use it.")
    );
    hint->setTextFormat(Qt::RichText);
    hint->setWordWrap(true);
    root->addWidget(hint);

    auto* typeRow = new QHBoxLayout;
    typeRow->setSpacing(6);
    auto* typeLbl = new QLabel(tr("Service:"));
    m_typeCombo = new QComboBox;
    m_typeCombo->setEditable(true);
    m_typeCombo->setMinimumWidth(280);
    struct Preset { const char* type; const char* label; };
    const Preset presets[] = {
        { "_tci._tcp.local.",             "TCI peripherals (AetherSDR)" },
        { "_services._dns-sd._udp.local.","All service types — meta-query (drill in)" },
        { "_http._tcp.local.",            "HTTP servers" },
        { "_workstation._tcp.local.",     "Workstations" },
        { "_smb._tcp.local.",             "SMB / file shares" },
        { "_ipp._tcp.local.",             "Printers (IPP)" },
        { "_airplay._tcp.local.",         "AirPlay video" },
        { "_raop._tcp.local.",            "AirPlay audio (RAOP)" },
        { "_googlecast._tcp.local.",      "Chromecast / Google Cast" },
        { "_hue._tcp.local.",             "Philips Hue bridges" },
        { "_spotify-connect._tcp.local.", "Spotify Connect" },
        { "_ssh._tcp.local.",             "SSH servers" },
        { "_companion-link._tcp.local.",  "Apple Companion-Link" },
    };
    for (const auto& p : presets) {
        m_typeCombo->addItem(QStringLiteral("%1   —   %2").arg(p.label, p.type),
                             QString::fromLatin1(p.type));
    }
    // Dropdown pick → use the data role (bare service type) directly, never
    // the display label.  activated(int) only fires on explicit user action,
    // unlike currentIndexChanged or editingFinished which can also fire on
    // focus changes / programmatic edits.
    connect(m_typeCombo, QOverload<int>::of(&QComboBox::activated), this,
            [this](int idx) {
                if (idx < 0) return;
                const QString t = m_typeCombo->itemData(idx).toString();
                if (t.isEmpty()) return;
                m_typeCombo->blockSignals(true);
                m_typeCombo->setEditText(t);
                m_typeCombo->blockSignals(false);
                onServiceTypeChanged(t);
            });
    // Custom-typed service type — only commit on Enter, never on focus loss.
    connect(m_typeCombo->lineEdit(), &QLineEdit::returnPressed, this, [this]() {
        onServiceTypeChanged(m_typeCombo->currentText());
    });
    typeRow->addWidget(typeLbl);
    typeRow->addWidget(m_typeCombo, 1);
    root->addLayout(typeRow);

    m_table = new QTableWidget(0, ColCount, this);
    QStringList headers;
    headers << tr("Instance") << tr("Class") << tr("Model")
            << tr("Host:Port") << tr("IP") << tr("TCI") << tr("Age")
            << tr("Extras (x-*)");
    m_table->setHorizontalHeaderLabels(headers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setAlternatingRowColors(true);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QTableWidget::cellDoubleClicked,
            this,    &DiscoveryDialog::onRowDoubleClicked);
    connect(m_table, &QTableWidget::itemSelectionChanged,
            this, [this]() { updateButtons(); });
    connect(m_table, &QTableWidget::customContextMenuRequested,
            this,    &DiscoveryDialog::onTableContextMenu);
    root->addWidget(m_table, 1);

    m_status = new QLabel(tr("Starting browse…"));
    m_status->setStyleSheet(QStringLiteral("color:#888;"));
    root->addWidget(m_status);

    // Hidden-items row — directly under the status line so the rest of
    // the button bar isn't crowded.
    auto* hiddenRow = new QHBoxLayout;
    hiddenRow->setSpacing(6);
    m_hiddenStatus    = new QLabel;
    m_hiddenStatus->setStyleSheet(QStringLiteral("color:#888;"));
    m_showHiddenBtn   = new QPushButton(tr("Show hidden"));
    m_showHiddenBtn->setCheckable(true);
    m_forgetHiddenBtn = new QPushButton(tr("Forget all hidden"));
    connect(m_showHiddenBtn,   &QPushButton::toggled, this, &DiscoveryDialog::onShowHiddenToggled);
    connect(m_forgetHiddenBtn, &QPushButton::clicked, this, &DiscoveryDialog::onForgetHiddenClicked);
    hiddenRow->addWidget(m_hiddenStatus);
    hiddenRow->addStretch();
    hiddenRow->addWidget(m_showHiddenBtn);
    hiddenRow->addWidget(m_forgetHiddenBtn);
    root->addLayout(hiddenRow);

    auto* btnRow = new QHBoxLayout;
    btnRow->setSpacing(6);
    m_refreshBtn = new QPushButton(tr("Refresh"));
    m_copyIpBtn  = new QPushButton(tr("Copy IP"));
    m_useBtn     = new QPushButton(tr("Use Selected"));
    m_useBtn->setDefault(true);
    m_closeBtn   = new QPushButton(tr("Close"));
    connect(m_refreshBtn, &QPushButton::clicked, this, &DiscoveryDialog::onRefreshClicked);
    connect(m_copyIpBtn,  &QPushButton::clicked, this, &DiscoveryDialog::onCopyIpClicked);
    connect(m_useBtn,     &QPushButton::clicked, this, &DiscoveryDialog::onUseSelectedClicked);
    connect(m_closeBtn,   &QPushButton::clicked, this, &DiscoveryDialog::reject);
    btnRow->addWidget(m_refreshBtn);
    btnRow->addWidget(m_copyIpBtn);
    btnRow->addStretch();
    btnRow->addWidget(m_useBtn);
    btnRow->addWidget(m_closeBtn);
    root->addLayout(btnRow);

    updateButtons();
    updateHiddenStatus();
}

void DiscoveryDialog::onServiceFound(const TciService& s)   { upsertRow(s); }
void DiscoveryDialog::onServiceUpdated(const TciService& s) { upsertRow(s); }

void DiscoveryDialog::onInfo(const QString& m)  { m_status->setText(m); }
void DiscoveryDialog::onError(const QString& m) {
    m_status->setText(m);
    m_status->setStyleSheet(QStringLiteral("color:#cc4444;"));
}

void DiscoveryDialog::onRefreshClicked() {
    if (m_browser) m_browser->refresh();
    m_status->setText(tr("Re-queried _tci._tcp.local at %1")
                          .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"))));
}

void DiscoveryDialog::onUseSelectedClicked() {
    int row = m_table->currentRow();
    if (row < 0) return;

    // In meta-query mode the row IS a service type — drilling in means
    // switching the combobox to that type, not closing the dialog.
    const bool inMeta = m_browser &&
        m_browser->serviceType() == QStringLiteral("_services._dns-sd._udp.local.");
    if (inMeta) {
        const QString instance = m_rowToInstance.value(row);
        if (instance.isEmpty()) return;
        m_typeCombo->setEditText(instance);
        onServiceTypeChanged(instance);
        return;
    }

    auto* ipItem   = m_table->item(row, ColIp);
    auto* portItem = m_table->item(row, ColHostPort);
    if (!ipItem || ipItem->text().isEmpty()) return;
    m_chosenHost = ipItem->text();

    // Host:Port column carries "<host>:<port>" — pull the port off the end.
    quint16 port = 0;
    if (portItem) {
        QString hp = portItem->text();
        int colon = hp.lastIndexOf(QChar(':'));
        if (colon > 0) port = quint16(hp.mid(colon + 1).toUInt());
    }
    m_chosenPort = port;
    accept();
}

void DiscoveryDialog::onCopyIpClicked() {
    int row = m_table->currentRow();
    if (row < 0) return;
    auto* ipItem = m_table->item(row, ColIp);
    if (!ipItem) return;
    QApplication::clipboard()->setText(ipItem->text());
    m_status->setText(tr("Copied %1 to clipboard").arg(ipItem->text()));
}

void DiscoveryDialog::onRowDoubleClicked(int /*row*/, int /*col*/) {
    onUseSelectedClicked();
}

void DiscoveryDialog::onServiceTypeChanged(const QString& text) {
    if (!m_browser) return;
    QString t = text.trimmed();
    if (t.isEmpty()) return;
    if (t == m_browser->serviceType()) return;
    // Clear table + cached state so we start fresh under the new type.
    m_table->setRowCount(0);
    m_rowToInstance.clear();
    m_lastSeen.clear();
    m_browser->setServiceType(t);
    m_status->setText(tr("Browsing %1 — re-querying…").arg(m_browser->serviceType()));

    // In meta-query mode the "Use Selected" button drills into the picked
    // service type instead of closing the dialog with a host/port.
    const bool inMeta =
        m_browser->serviceType() == QStringLiteral("_services._dns-sd._udp.local.");
    m_useBtn->setText(inMeta ? tr("Drill into selected") : tr("Use Selected"));

    updateButtons();
    updateHiddenStatus();
}

// ---------------------------------------------------------------------------
// Hide-list mechanics
// ---------------------------------------------------------------------------
QString DiscoveryDialog::hideKey(const QString& instance) const {
    QString type = m_browser ? m_browser->serviceType()
                             : QStringLiteral("_tci._tcp.local.");
    return type + QChar('|') + instance;
}

bool DiscoveryDialog::isHidden(const QString& instance) const {
    return m_hidden.contains(hideKey(instance));
}

void DiscoveryDialog::loadHidden() {
    QSettings s;
    const QStringList list = s.value(QStringLiteral("discovery/hiddenKeys"))
                                .toStringList();
    m_hidden = QSet<QString>(list.cbegin(), list.cend());
}

void DiscoveryDialog::saveHidden() {
    QSettings s;
    QStringList list(m_hidden.cbegin(), m_hidden.cend());
    list.sort();
    s.setValue(QStringLiteral("discovery/hiddenKeys"), list);
}

void DiscoveryDialog::onTableContextMenu(const QPoint& pos) {
    int row = m_table->rowAt(pos.y());
    if (row < 0) return;
    const QString instance = m_rowToInstance.value(row);
    if (instance.isEmpty()) return;

    QMenu menu(this);
    if (isHidden(instance)) {
        QAction* a = menu.addAction(tr("Unhide '%1'").arg(instance));
        connect(a, &QAction::triggered, this, [this, instance]() {
            m_hidden.remove(hideKey(instance));
            saveHidden();
            applyHiddenVisibility();
            updateHiddenStatus();
        });
    } else {
        QAction* a = menu.addAction(tr("Hide '%1'").arg(instance));
        connect(a, &QAction::triggered, this, [this, instance]() {
            m_hidden.insert(hideKey(instance));
            saveHidden();
            applyHiddenVisibility();
            updateHiddenStatus();
        });
    }
    menu.exec(m_table->viewport()->mapToGlobal(pos));
}

void DiscoveryDialog::onShowHiddenToggled(bool checked) {
    m_showHidden = checked;
    m_showHiddenBtn->setText(checked ? tr("Showing hidden") : tr("Show hidden"));
    applyHiddenVisibility();
}

void DiscoveryDialog::onForgetHiddenClicked() {
    if (m_hidden.isEmpty()) return;
    // Only forget entries for the current service type — leaves the user's
    // hidden choices for other service types intact.
    QString type = m_browser ? m_browser->serviceType() : QString();
    QString prefix = type + QChar('|');
    QSet<QString> keep;
    for (const QString& k : std::as_const(m_hidden))
        if (!k.startsWith(prefix)) keep.insert(k);
    m_hidden = keep;
    saveHidden();
    applyHiddenVisibility();
    updateHiddenStatus();
}

void DiscoveryDialog::applyHiddenVisibility() {
    int hiddenInTable = 0;
    for (auto it = m_rowToInstance.constBegin(); it != m_rowToInstance.constEnd(); ++it) {
        const int row = it.key();
        const QString& instance = it.value();
        const bool hidden = isHidden(instance);
        m_table->setRowHidden(row, hidden && !m_showHidden);

        // When showing hidden rows, dim them so they're visually distinct.
        if (hidden && m_showHidden) {
            for (int c = 0; c < m_table->columnCount(); ++c)
                if (auto* it2 = m_table->item(row, c))
                    it2->setForeground(QBrush(QColor(0x88, 0x88, 0x88)));
        } else if (!hidden) {
            // Restore default foreground (clear our override).
            for (int c = 0; c < m_table->columnCount(); ++c)
                if (auto* it2 = m_table->item(row, c))
                    it2->setForeground(QBrush());
        }
        if (hidden) hiddenInTable++;
    }
    (void)hiddenInTable;
}

void DiscoveryDialog::updateHiddenStatus() {
    QString type = m_browser ? m_browser->serviceType() : QString();
    QString prefix = type + QChar('|');
    int n = 0;
    for (const QString& k : std::as_const(m_hidden))
        if (k.startsWith(prefix)) n++;
    if (n == 0) {
        m_hiddenStatus->setText(tr("No hidden entries for %1").arg(type));
        m_showHiddenBtn->setEnabled(false);
        m_forgetHiddenBtn->setEnabled(false);
    } else {
        m_hiddenStatus->setText(tr("%1 hidden entry(ies) under %2 — right-click "
                                   "to hide/unhide rows.").arg(n).arg(type));
        m_showHiddenBtn->setEnabled(true);
        m_forgetHiddenBtn->setEnabled(true);
    }
}

void DiscoveryDialog::onAgeTick() {
    for (auto it = m_rowToInstance.constBegin(); it != m_rowToInstance.constEnd(); ++it) {
        int row = it.key();
        const QString& inst = it.value();
        auto* cell = m_table->item(row, ColAge);
        if (cell) cell->setText(ageLabel(m_lastSeen.value(inst, 0)));
    }
}

// ---------------------------------------------------------------------------

int DiscoveryDialog::findRow(const QString& instance) const {
    for (auto it = m_rowToInstance.constBegin(); it != m_rowToInstance.constEnd(); ++it)
        if (it.value() == instance) return it.key();
    return -1;
}

void DiscoveryDialog::upsertRow(const TciService& s) {
    int row = findRow(s.instance);
    if (row < 0) {
        row = m_table->rowCount();
        m_table->insertRow(row);
        for (int c = 0; c < ColCount; ++c)
            m_table->setItem(row, c, new QTableWidgetItem);
        m_rowToInstance.insert(row, s.instance);
    }
    m_lastSeen.insert(s.instance, s.lastSeenMs);

    auto set = [&](int col, const QString& text) {
        if (auto* it = m_table->item(row, col)) it->setText(text);
    };

    set(ColInstance, s.instance);
    set(ColClass,    s.txt.value(QStringLiteral("class")));
    set(ColModel,    s.txt.value(QStringLiteral("model")));
    {
        QString host = s.hostname;
        if (host.endsWith(QChar('.'))) host.chop(1);
        set(ColHostPort, QStringLiteral("%1:%2").arg(host).arg(s.port));
    }
    set(ColIp,      s.address.isNull() ? QString() : s.address.toString());
    set(ColTciVer,  s.txt.value(QStringLiteral("tci-version")));
    set(ColAge,     ageLabel(s.lastSeenMs));
    set(ColExtras,  extrasLabel(s.txt));

    m_table->resizeColumnsToContents();

    // Apply hide state for this row (and refresh dimming across the table).
    applyHiddenVisibility();

    // Visible count excludes hidden entries unless show-hidden is on.
    int visible = 0;
    for (auto it = m_rowToInstance.constBegin(); it != m_rowToInstance.constEnd(); ++it)
        if (!m_table->isRowHidden(it.key())) visible++;
    m_status->setText(tr("Discovered %1 visible peripheral(s)").arg(visible));
    updateButtons();
    updateHiddenStatus();
}

void DiscoveryDialog::updateButtons() {
    bool hasRow = m_table && m_table->currentRow() >= 0;
    if (m_useBtn)    m_useBtn->setEnabled(hasRow);
    if (m_copyIpBtn) m_copyIpBtn->setEnabled(hasRow);
}

} // namespace TciMon
