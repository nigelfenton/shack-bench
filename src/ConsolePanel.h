#pragma once

// ConsolePanel — guarded free-text TCI command sender.
//
// This is the only part of Shack-Bench that can key the radio, so it is
// deliberately defensive, mirroring the tci_sweep.py philosophy:
//
//   • Dry-run by default.  The arm state is NOT persisted and resets to
//     OFF every launch — you must consciously arm sending each session.
//   • TX-class commands (trx, tune) require a second explicit confirm.
//   • Keying a carrier starts a watchdog: if SWR exceeds a limit, or TX
//     persists past a timeout, it auto-sends trx:0,false; tune:0,false;.
//   • An always-available "UNKEY NOW" button does the same on demand.

#include <QWidget>
#include <QString>

class QLineEdit;
class QPlainTextEdit;
class QCheckBox;
class QPushButton;
class QDoubleSpinBox;
class QSpinBox;
class QLabel;
class QTimer;
class QElapsedTimer;

namespace TciMon {

class TciClient;

class ConsolePanel : public QWidget {
    Q_OBJECT
public:
    explicit ConsolePanel(TciClient* tci, QWidget* parent = nullptr);
    ~ConsolePanel() override;

    // Fed every incoming line so the watchdog can watch SWR and the
    // transcript can correlate replies to a recent send.
    void noteIncoming(const QString& line);

private slots:
    void onSend();
    void onArmToggled(bool checked);
    void onUnkeyNow();
    void onWatchdogTimeout();

private:
    static bool isTxClass(const QString& cmd);
    static bool isKeyDown(const QString& cmd, const QString& args);
    void appendLine(const QString& text, const QString& colorHex);
    void doAbort(const QString& reason);
    void refreshBanner();

    TciClient* m_tci{};

    QLineEdit*      m_cmdEdit{};
    QPlainTextEdit* m_transcript{};
    QCheckBox*      m_armCheck{};
    QPushButton*    m_sendBtn{};
    QPushButton*    m_unkeyBtn{};
    QDoubleSpinBox* m_swrLimit{};
    QSpinBox*       m_txTimeout{};
    QLabel*         m_banner{};

    QTimer*         m_watchdog{};
    QElapsedTimer*  m_sinceSend{};
    bool            m_armed{false};
    bool            m_keyed{false};   // a carrier-up command is outstanding
};

} // namespace TciMon
