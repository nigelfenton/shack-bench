#include "SwrPlot.h"

#include <QPainter>
#include <QPaintEvent>
#include <algorithm>
#include <cmath>
#include <limits>

namespace TciMon {

SwrPlot::SwrPlot(QWidget* parent) : QWidget(parent)
{
    setMinimumHeight(220);
    setAutoFillBackground(true);
}

void SwrPlot::setCurves(const QVector<Curve>& curves)
{
    m_curves = curves;
    update();
}

void SwrPlot::clear()
{
    m_curves.clear();
    update();
}

void SwrPlot::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRect full = rect();
    p.fillRect(full, QColor("#050a14"));

    QVector<Curve> curves;
    for (const auto& c : m_curves)
        if (!c.points.isEmpty()) curves.push_back(c);

    if (curves.isEmpty()) {
        p.setPen(QColor("#6b8099"));
        p.drawText(full, Qt::AlignCenter,
                   "No SWR sweep captured yet.\n"
                   "Run an antenna SWR sweep in AetherSDR.");
        return;
    }

    // Shared SWR (Y) scale across every panel so curves are comparable.
    double sMax = 2.0;
    for (const auto& c : curves)
        for (auto it = c.points.begin(); it != c.points.end(); ++it)
            sMax = std::max(sMax, it.value());
    const double yTop = std::min(10.0, std::ceil(sMax * 2.0) / 2.0);
    const double yBot = 1.0;

    const int mL = 44, mR = 12, mT = 14, mB = 30;
    const QRect area(full.left() + mL, full.top() + mT,
                     full.width() - mL - mR, full.height() - mT - mB);
    if (area.width() < 40 || area.height() < 40) return;

    auto yOf = [&](double swr) {
        double t = (swr - yBot) / (yTop - yBot);
        return area.bottom() - t * area.height();
    };

    // Shared SWR gridlines + Y labels (drawn once, full width).
    p.setFont(QFont("Consolas", 8));
    const double ticks[] = {1.0, 1.5, 2.0, 2.5, 3.0, 4.0, 5.0, 7.0, 10.0};
    for (double t : ticks) {
        if (t < yBot - 1e-6 || t > yTop + 1e-6) continue;
        double y = yOf(t);
        p.setPen(t == 2.0 ? QColor("#33405a") : QColor("#16202e"));
        p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
        p.setPen(QColor("#6b8099"));
        p.drawText(QRectF(full.left(), y - 7, mL - 6, 14),
                   Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(t, 'f', t < 3.0 ? 1 : 0));
    }

    // One side-by-side panel per band, each with its own frequency axis.
    const int n   = curves.size();
    const int gap = (n > 1) ? 10 : 0;
    const double panelW = double(area.width() - gap * (n - 1)) / n;

    for (int i = 0; i < n; ++i) {
        const Curve& c = curves[i];
        const double px = area.left() + i * (panelW + gap);
        const QRectF panel(px, area.top(), panelW, area.height());

        qint64 fMin = c.points.firstKey();
        qint64 fMax = c.points.lastKey();
        if (fMax <= fMin) fMax = fMin + 1;
        auto xOf = [&](qint64 hz) {
            double t = double(hz - fMin) / double(fMax - fMin);
            return panel.left() + t * panel.width();
        };

        // Panel separator / border.
        p.setPen(QColor("#22304a"));
        p.drawRect(panel);

        // Per-panel frequency ticks (start / mid / end).
        const qint64 fMid = (fMin + fMax) / 2;
        const qint64 fticks[] = {fMin, fMid, fMax};
        const int aligns[] = {Qt::AlignLeft, Qt::AlignHCenter, Qt::AlignRight};
        p.setPen(QColor("#16202e"));
        for (int k = 0; k < 3; ++k) {
            double x = xOf(fticks[k]);
            p.drawLine(QPointF(x, panel.top()), QPointF(x, panel.bottom()));
        }
        p.setPen(QColor("#6b8099"));
        for (int k = 0; k < 3; ++k) {
            double x = xOf(fticks[k]);
            p.drawText(QRectF(x - 42, panel.bottom() + 4, 84, 16),
                       Qt::AlignHCenter | Qt::AlignTop,
                       QString::number(fticks[k] / 1e6, 'f', 3));
        }

        // Curve + min marker.
        QPolygonF poly;
        qint64 minF = 0; double minS = std::numeric_limits<double>::max();
        for (auto it = c.points.begin(); it != c.points.end(); ++it) {
            poly << QPointF(xOf(it.key()), yOf(it.value()));
            if (it.value() < minS) { minS = it.value(); minF = it.key(); }
        }
        QPen pen(c.color); pen.setWidthF(1.8);
        p.setPen(pen);
        p.drawPolyline(poly);
        p.setBrush(c.color);
        p.drawEllipse(QPointF(xOf(minF), yOf(minS)), 3.0, 3.0);

        // Panel title: band + min summary (clipped to the panel).
        p.setPen(c.color);
        p.setClipRect(panel);
        p.drawText(QPointF(panel.left() + 6, panel.top() + 13),
                   QString("%1  min %2 @ %3 MHz")
                       .arg(c.band)
                       .arg(minS, 0, 'f', 2)
                       .arg(minF / 1e6, 0, 'f', 3));
        p.setClipping(false);
    }
}

} // namespace TciMon
