#include "CalPlot.h"

#include <QPainter>
#include <QPaintEvent>
#include <algorithm>
#include <cmath>

namespace TciMon {

namespace {
constexpr const char* kBg      = "#050a14";
constexpr const char* kGrid    = "#16202e";
constexpr const char* kGridKey = "#33405a";
constexpr const char* kAxisTxt = "#6b8099";
constexpr const char* kAlc     = "#ffaa00";   // amber — ALC, left axis
constexpr const char* kFwd     = "#00d8ef";   // cyan  — forward power, right axis
constexpr const char* kKnee    = "#4cff7c";   // green — knee marker
constexpr const char* kRec     = "#ff5cff";   // magenta — recommended setting
} // namespace

CalPlot::CalPlot(QWidget* parent) : QWidget(parent)
{
    setMinimumHeight(260);
    setAutoFillBackground(true);
}

void CalPlot::setData(const QVector<Point>& pts,
                      int kneeGain, int recGain, double alcTarget)
{
    m_pts       = pts;
    m_kneeGain  = kneeGain;
    m_recGain   = recGain;
    m_alcTarget = alcTarget;
    std::sort(m_pts.begin(), m_pts.end(),
              [](const Point& a, const Point& b) { return a.gain < b.gain; });
    update();
}

void CalPlot::clear()
{
    m_pts.clear();
    m_kneeGain = m_recGain = -1;
    update();
}

void CalPlot::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRect full = rect();
    p.fillRect(full, QColor(kBg));

    if (m_pts.isEmpty()) {
        p.setPen(QColor(kAxisTxt));
        p.drawText(full, Qt::AlignCenter,
                   "No calibration sweep yet.\n"
                   "Arm, then Start calibration to sweep tx_gain.");
        return;
    }

    // ── Axis ranges ────────────────────────────────────────────────────
    double alcMin = m_alcTarget, alcMax = m_alcTarget;
    double fwdMax = 1.0;
    bool   anyAlc = false;
    for (const Point& pt : m_pts) {
        fwdMax = std::max(fwdMax, pt.fwd);
        if (pt.hasAlc) {
            anyAlc = true;
            alcMin = std::min(alcMin, pt.alc);
            alcMax = std::max(alcMax, pt.alc);
        }
    }
    double aLo = std::floor((alcMin - 1.0) / 5.0) * 5.0;
    double aHi = std::ceil((alcMax + 1.0) / 5.0) * 5.0;
    aLo = std::max(aLo, -40.0);
    aHi = std::max(aHi, aLo + 5.0);
    const double fHi = std::max(25.0, std::ceil(fwdMax / 25.0) * 25.0);

    const int mL = 46, mR = 52, mT = 30, mB = 34;
    const QRect area(full.left() + mL, full.top() + mT,
                     full.width() - mL - mR, full.height() - mT - mB);
    if (area.width() < 60 || area.height() < 60) return;

    auto xOf    = [&](double g) {
        return area.left() + (g / 100.0) * area.width();
    };
    auto yOfAlc = [&](double v) {
        return area.bottom() - (v - aLo) / (aHi - aLo) * area.height();
    };
    auto yOfFwd = [&](double v) {
        return area.bottom() - (v / fHi) * area.height();
    };

    p.setFont(QFont("Consolas", 8));

    // ── ALC (left-axis) gridlines + labels ─────────────────────────────
    for (double v = aLo; v <= aHi + 1e-6; v += 5.0) {
        const double y = yOfAlc(v);
        const bool zero = std::fabs(v) < 1e-6;
        p.setPen(QColor(zero ? kGridKey : kGrid));
        p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
        p.setPen(QColor(kAlc));
        p.drawText(QRectF(full.left(), y - 7, mL - 6, 14),
                   Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(v, 'f', 0));
    }

    // ── Forward-power (right-axis) labels ──────────────────────────────
    for (int k = 0; k <= 4; ++k) {
        const double v = fHi * k / 4.0;
        const double y = yOfFwd(v);
        p.setPen(QColor(kFwd));
        p.drawText(QRectF(area.right() + 5, y - 7, mR - 8, 14),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   QString::number(v, 'f', 0));
    }

    // ── tx_gain (X-axis) gridlines + labels ────────────────────────────
    for (int g = 0; g <= 100; g += 20) {
        const double x = xOf(g);
        p.setPen(QColor(kGrid));
        p.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
        p.setPen(QColor(kAxisTxt));
        p.drawText(QRectF(x - 24, area.bottom() + 4, 48, 16),
                   Qt::AlignHCenter | Qt::AlignTop, QString::number(g));
    }
    p.setPen(QColor(kAxisTxt));
    p.drawText(QRectF(area.left(), full.bottom() - 15, area.width(), 14),
               Qt::AlignHCenter | Qt::AlignTop, "tx_gain  (%)");

    // ── ALC target ceiling line ────────────────────────────────────────
    if (anyAlc && m_alcTarget >= aLo && m_alcTarget <= aHi) {
        const double y = yOfAlc(m_alcTarget);
        QPen tp{QColor(kAlc)};
        tp.setStyle(Qt::DashLine);
        p.setPen(tp);
        p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
        p.drawText(QPointF(area.left() + 4, y - 4),
                   QString("ALC target %1 dBFS").arg(m_alcTarget, 0, 'f', 0));
    }

    // ── Knee marker (vertical line) ────────────────────────────────────
    if (m_kneeGain >= 0) {
        const double x = xOf(m_kneeGain);
        QPen kp{QColor(kKnee)};
        kp.setStyle(Qt::DashLine);
        p.setPen(kp);
        p.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
    }

    // ── Forward-power curve (right axis) ───────────────────────────────
    {
        QPolygonF poly;
        for (const Point& pt : m_pts)
            poly << QPointF(xOf(pt.gain), yOfFwd(pt.fwd));
        QPen pen{QColor(kFwd)};
        pen.setWidthF(2.0);
        p.setPen(pen);
        p.drawPolyline(poly);
        p.setBrush(QColor(kFwd));
        for (const QPointF& pt : poly)
            p.drawEllipse(pt, 2.6, 2.6);
    }

    // ── ALC curve (left axis) ──────────────────────────────────────────
    if (anyAlc) {
        QPolygonF poly;
        for (const Point& pt : m_pts)
            if (pt.hasAlc) poly << QPointF(xOf(pt.gain), yOfAlc(pt.alc));
        QPen pen{QColor(kAlc)};
        pen.setWidthF(2.0);
        p.setPen(pen);
        p.drawPolyline(poly);
        p.setBrush(QColor(kAlc));
        for (const QPointF& pt : poly)
            p.drawEllipse(pt, 2.6, 2.6);
    }

    // ── Recommended-setting marker ─────────────────────────────────────
    if (m_recGain >= 0) {
        for (const Point& pt : m_pts) {
            if (pt.gain != m_recGain) continue;
            const double x = xOf(pt.gain);
            const double y = pt.hasAlc ? yOfAlc(pt.alc) : yOfFwd(pt.fwd);
            p.setPen(QPen(QColor(kRec), 2.0));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(QPointF(x, y), 6.0, 6.0);
            break;
        }
    }

    // ── Legend ─────────────────────────────────────────────────────────
    p.setFont(QFont("Consolas", 8, QFont::Bold));
    int lx = area.left() + 6;
    const int ly = full.top() + 8;
    auto chip = [&](const char* col, const QString& text) {
        p.fillRect(QRect(lx, ly + 2, 10, 10), QColor(col));
        p.setPen(QColor(col));
        p.drawText(QPointF(lx + 14, ly + 11), text);
        lx += 14 + p.fontMetrics().horizontalAdvance(text) + 18;
    };
    chip(kFwd, "forward power (W)");
    chip(kAlc, "ALC peak (dBFS)");
    if (m_recGain >= 0)
        chip(kRec, QString("recommended tx_gain = %1").arg(m_recGain));
}

} // namespace TciMon
