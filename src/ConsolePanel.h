#pragma once

// ConsolePanel — guarded free-text TCI command sender.
//
// This is the only part of Shack-Bench that can key the radio, so it is
// deliberately defensive, mirroring the tci_sweep.py philosophy:
//
//   • Dry-run by default.  The arm state is NOT persisted and resets to
//     OFF every launch — you must consciously arm sending each session.
//   • TX-class commands (trx, tune, cw_msg, cw_macros) require a second
//     explicit confirm. EVERY command on a line is checked — the line is
//     sent verbatim, so `vfo:…;trx:0,true;` must not pass as a `vfo`.
//   • Keying a carrier starts a watchdog: if SWR exceeds a limit, or TX
//     persists past a timeout, it auto-unkeys every receiver it keyed.
//   • An always-available "UNKEY NOW" button does the same on demand —
//     enabled even when disarmed, because unkeying is always safe.
//   • Disarming, disconnecting, losing the link or closing the app while
//     keyed unkeys too. If the link is down the unkey is queued in TciClient
//     and sent first on reconnect, and the log says so rather than "sent".

#include <QSet>
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

    // The app is about to stop being in control (close, Disconnect): unkey
    // if this panel keyed anything. No-op otherwise.
    void releaseTx(const QString& reason);

private slots:
    void onSend();
    void onArmToggled(bool checked);
    void onUnkeyNow();
    void onWatchdogTimeout();

private:
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
    QSet<int>       m_keyedRx;        // receivers with a carrier-up outstanding
    bool            m_cwSent{false};  // a CW send may still be keying
};

} // namespace TciMon
