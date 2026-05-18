#pragma once

// ReplayPanel — record the live TCI stream to a file, and replay any
// capture back as a local TCI WebSocket *server* so another client
// (WSJT-X, ShackLog, a second TCI Monitor) can connect to it offline.
//
// Replay never touches a real radio — it stands up its own
// QWebSocketServer on localhost and feeds recorded lines with their
// original inter-message timing (optionally time-scaled / looped).

#include <QWidget>
#include <QString>
#include <QVector>
#include <QPair>
#include <QList>

class QLineEdit;
class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;
class QPushButton;
class QLabel;
class QPlainTextEdit;
class QTimer;
class QFile;
class QTextStream;
class QElapsedTimer;
class QWebSocket;
class QWebSocketServer;

namespace TciMon {

class ReplayPanel : public QWidget {
    Q_OBJECT
public:
    explicit ReplayPanel(QWidget* parent = nullptr);
    ~ReplayPanel() override;

    // Fed every incoming line so it can be recorded when armed.
    void noteIncoming(const QString& line);

private slots:
    void onToggleRecord();
    void onBrowseReplay();
    void onStartServer();
    void onStopServer();
    void onNewConnection();
    void sendNext();

private:
    bool loadScript(const QString& path, QString* err);
    void stopRecording();
    void status(const QString& text, const QString& colorHex);

    // Record
    QPushButton*   m_recBtn{};
    QLabel*        m_recStatus{};
    QFile*         m_recFile{};
    QTextStream*   m_recStream{};
    QElapsedTimer* m_recClock{};
    qint64         m_recLastMs{0};
    quint64        m_recCount{0};
    bool           m_recording{false};

    // Replay
    QLineEdit*       m_pathEdit{};
    QSpinBox*        m_portSpin{};
    QDoubleSpinBox*  m_speedSpin{};
    QCheckBox*       m_loopCheck{};
    QPushButton*     m_startBtn{};
    QPushButton*     m_stopBtn{};
    QLabel*          m_srvStatus{};
    QPlainTextEdit*  m_log{};

    QWebSocketServer*           m_server{};
    QList<QWebSocket*>          m_clients;
    QVector<QPair<int,QString>> m_script;   // <delayMs, message>
    int                         m_idx{0};
    QTimer*                     m_playTimer{};
};

} // namespace TciMon
