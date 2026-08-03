#pragma once

// SwrPlot — a tiny custom-painted SWR-vs-frequency graph.
//
// No Qt Charts dependency (Shack-Bench stays lean). Draws one or more
// captured sweeps as overlaid polylines with an SWR grid, frequency
// axis, per-curve min-SWR marker, and a legend.

#include <QWidget>
#include <QMap>
#include <QVector>
#include <QString>
#include <QColor>

namespace TciMon {

class SwrPlot : public QWidget {
    Q_OBJECT
public:
    struct Curve {
        QString             band;            // "20m"
        QColor              color;
        QMap<qint64,double> points;          // freqHz → swr (sorted by key)
    };

    explicit SwrPlot(QWidget* parent = nullptr);

    void setCurves(const QVector<Curve>& curves);
    void clear();

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QVector<Curve> m_curves;
};

} // namespace TciMon
