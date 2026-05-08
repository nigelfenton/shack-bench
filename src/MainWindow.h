#pragma once

// MainWindow — TCI Monitor's only window.
//
// Layout:
//   • Top connect bar  — host + port + [Connect]/[Disconnect] + status
//   • Splitter:
//       Left  — parsed view: current VFO/mode + spot table
//       Right — raw message log (color-coded by message type)
//   • Bottom action bar — [Save log…] [Clear log] [Filter: ...]
//
// Parsing happens in this class (not in TciClient) — the client just
// emits raw lines and we slice them into structured forms here.

#include <QMainWindow>

class QLineEdit;
class QSpinBox;
class QPushButton;
class QLabel;
class QPlainTextEdit;
class QTableWidget;
class QCheckBox;

namespace TciMon {

class TciClient;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void onConnectClicked();
    void onDisconnectClicked();
    void onConnectionChanged(bool connected);
    void onRawMessage(const QString& line);
    void onErrorText(const QString& text);
    void onSaveLog();
    void onClearLog();
    void onFilterChanged(const QString& text);

private:
    void buildUI();
    void appendLog(const QString& line, const QString& colorHex);
    void parseLine(const QString& line);
    void handleVfo(const QStringList& args);
    void handleMode(const QStringList& args);
    void handleSpot(const QStringList& args);
    void handleSpotDelete(const QStringList& args);
    void handleSpotClear();
    void refreshStatus();

    TciClient* m_tci{nullptr};

    // Connect bar
    QLineEdit*   m_hostEdit{};
    QSpinBox*    m_portSpin{};
    QPushButton* m_connectBtn{};
    QPushButton* m_disconnectBtn{};
    QLabel*      m_statusDot{};
    QLabel*      m_statusText{};

    // Parsed display
    QLabel*       m_curVfo{};
    QLabel*       m_curMode{};
    QTableWidget* m_spotTable{};

    // Raw log
    QPlainTextEdit* m_logView{};
    QLineEdit*      m_filterEdit{};
    QCheckBox*      m_autoscrollCheck{};

    // Action row
    QPushButton* m_saveBtn{};
    QPushButton* m_clearBtn{};

    // Stats / counters in status bar
    QLabel* m_sbMsgCount{};
    QLabel* m_sbSpotCount{};
    int     m_msgCount{0};
    int     m_spotCount{0};
};

} // namespace TciMon
