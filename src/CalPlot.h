#pragma once

// CalPlot — a custom-painted TX-drive calibration graph.
//
// X axis is tx_gain (0–100 %).  Two Y series share the plot:
//   • ALC peak (dBFS) — left axis, amber
//   • forward power (W) — right axis, cyan
// The knee (highest clean tx_gain) and the recommended setting are marked.
//
// No Qt Charts dependency — Shack-Bench stays lean, same as SwrPlot.

#include <QWidget>
#include <QVector>
#include <QString>

namespace TciMon {

class CalPlot : public QWidget {
    Q_OBJECT
public:
    struct Point {
        int    gain{0};        // tx_gain %
        double fwd{0.0};       // forward power, W (avg over the dwell window)
        double alc{0.0};       // ALC peak, dBFS
        bool   hasAlc{false};  // false if the build predates PR #2950
    };

    explicit CalPlot(QWidget* parent = nullptr);

    // Replace the displayed curve. kneeGain / recGain are tx_gain values to
    // mark, or -1 for "none". target is the ALC ceiling line (dBFS).
    void setData(const QVector<Point>& pts,
                 int kneeGain, int recGain, double alcTarget);
    void clear();

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QVector<Point> m_pts;
    int    m_kneeGain{-1};
    int    m_recGain{-1};
    double m_alcTarget{-10.0};
};

} // namespace TciMon
