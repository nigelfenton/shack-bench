#pragma once

// InspectorPanel — structured per-command view of the TCI stream.
//
// Where the raw log is a flat scroll of every line, the inspector folds
// the stream by command name: one row per command, with live count,
// event rate, the most-recent raw value, the reference syntax, and an
// advisory spec-compliance flag.  It is a pure consumer — feed it every
// line via ingest(); it never sends anything.

#include <QWidget>
#include <QHash>
#include <QString>

class QTableWidget;
class QLineEdit;
class QLabel;
class QTimer;

namespace TciMon {

class InspectorPanel : public QWidget {
    Q_OBJECT
public:
    explicit InspectorPanel(QWidget* parent = nullptr);

    // Feed one raw TCI line (same lines the raw log sees).
    void ingest(const QString& line);

public slots:
    void clearAll();

private slots:
    void refresh();
    void onFilterChanged(const QString& text);

private:
    struct Stat {
        quint64 count{0};
        quint64 countAtLastTick{0};
        double  ratePerMin{0.0};   // smoothed
        QString lastValue;
        QString lastSeen;          // HH:mm:ss
        QString compliance;        // advisory note
    };

    void rebuildRow(const QString& cmd, const Stat& s, int row);
    static QString complianceFor(const QString& cmd, int actualArgs);

    QTableWidget* m_table{};
    QLineEdit*    m_filter{};
    QLabel*       m_summary{};
    QTimer*       m_tick{};

    QHash<QString, Stat> m_stats;
    QString m_filterText;
    bool    m_dirty{false};
};

} // namespace TciMon
