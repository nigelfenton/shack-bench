#pragma once

// ComparePanel — connect several independent read-only observers and
// diff what each TCI server sends them.
//
// Each observer row has its own host/port, so this covers both:
//   • same server, N observers  — "do all clients see identical frames?"
//   • cross-server compare      — "how do two radios' dialects differ?"
//
// The headline view is a per-command value matrix: one row per command,
// one column per observer, rows where observers disagree are flagged.
// A merged, colour-coded stream log sits underneath for the timeline.

#include <QWidget>
#include <QHash>
#include <QString>
#include <QVector>

class QLineEdit;
class QSpinBox;
class QPushButton;
class QLabel;
class QTableWidget;
class QPlainTextEdit;
class QCheckBox;
class QTimer;

namespace TciMon {

class TciClient;

class ComparePanel : public QWidget {
    Q_OBJECT
public:
    explicit ComparePanel(QWidget* parent = nullptr);

private slots:
    void onAddObserver();
    void refreshDiff();

private:
    struct Observer {
        QString      name;
        QLineEdit*   host{};
        QSpinBox*    port{};
        QPushButton* btn{};
        QLabel*      dot{};
        TciClient*   tci{};
        bool         connected{false};
        QHash<QString, QString> latest;   // cmd → most-recent args
    };

    void addObserverRow(const QString& host, quint16 port);
    void toggleObserver(int idx);
    void onObserverLine(int idx, const QString& line);
    void onObserverConn(int idx, bool up);
    static QString colorFor(int idx);

    QVector<Observer> m_obs;

    QWidget*        m_obsBox{};
    QPushButton*    m_addBtn{};
    QTableWidget*   m_diff{};
    QCheckBox*      m_onlyMismatch{};
    QPlainTextEdit* m_stream{};
    QTimer*         m_tick{};
    bool            m_dirty{false};
};

} // namespace TciMon
