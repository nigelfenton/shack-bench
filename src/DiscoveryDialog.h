#pragma once

// DiscoveryDialog — modal browser for TCI peripherals advertising under
// _tci._tcp.local on the LAN.  Owns an MdnsBrowser, populates a table
// from its signals, and (on accept) hands back a chosen host + port for
// MainWindow's connect bar.

#include <QDialog>
#include <QHash>
#include <QSet>
#include <QString>

class QComboBox;
class QTableWidget;
class QLabel;
class QPushButton;
class QTimer;

namespace TciMon {

class MdnsBrowser;
struct TciService;

class DiscoveryDialog : public QDialog {
    Q_OBJECT
public:
    explicit DiscoveryDialog(QWidget* parent = nullptr);
    ~DiscoveryDialog() override;

    // Populated when accept() is taken via Use Selected.  Caller should
    // read these only if exec() returned QDialog::Accepted.
    QString chosenHost() const { return m_chosenHost; }
    quint16 chosenPort() const { return m_chosenPort; }

private slots:
    void onServiceFound(const TciService& s);
    void onServiceUpdated(const TciService& s);
    void onInfo(const QString& m);
    void onError(const QString& m);
    void onRefreshClicked();
    void onUseSelectedClicked();
    void onCopyIpClicked();
    void onRowDoubleClicked(int row, int col);
    void onAgeTick();
    void onServiceTypeChanged(const QString& text);
    void onTableContextMenu(const QPoint& pos);
    void onShowHiddenToggled(bool checked);
    void onForgetHiddenClicked();

private:
    void buildUI();
    void upsertRow(const TciService& s);
    int  findRow(const QString& instance) const;
    void updateButtons();

    // Hide-list persistence.  Key = "<serviceType>|<instance>".
    void   loadHidden();
    void   saveHidden();
    QString hideKey(const QString& instance) const;
    bool   isHidden(const QString& instance) const;
    void   applyHiddenVisibility();
    void   updateHiddenStatus();

    MdnsBrowser* m_browser = nullptr;
    QComboBox*   m_typeCombo = nullptr;
    QTableWidget* m_table  = nullptr;
    QLabel*       m_status = nullptr;
    QPushButton*  m_refreshBtn = nullptr;
    QPushButton*  m_useBtn     = nullptr;
    QPushButton*  m_copyIpBtn  = nullptr;
    QPushButton*  m_closeBtn   = nullptr;

    QLabel*       m_hiddenStatus  = nullptr;
    QPushButton*  m_showHiddenBtn = nullptr;     // checkable toggle
    QPushButton*  m_forgetHiddenBtn = nullptr;
    bool          m_showHidden = false;
    QSet<QString> m_hidden;                      // hideKey() values

    QTimer*       m_ageTimer = nullptr;

    // Row index → instance name, so age-tick + accept know which service.
    QHash<int, QString> m_rowToInstance;
    QHash<QString, qint64> m_lastSeen;

    QString m_chosenHost;
    quint16 m_chosenPort = 0;
};

} // namespace TciMon
