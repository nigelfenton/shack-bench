#include "CalPlot.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <algorithm>
#include <cmath>

namespace TciMon {

namespace {

// Palette — neon-on-dark, picked to keep the existing TCI Monitor mood
// (deep navy bg, amber+cyan accents) but with the saturation and glow
// turned up so the curves carry across the room.
constexpr const char* kBgTop      = "#02060e";   // very dark navy at top
constexpr const char* kBgBot      = "#0a1322";   // slightly more visible at bottom
constexpr const char* kFrame      = "#1c2a40";
constexpr const char* kGrid       = "#13202e";
constexpr const char* kGridKey    = "#33405a";
constexpr const char* kAxisTxt    = "#7d96b3";
constexpr const char* kAxisHi     = "#aabfd9";
constexpr const char* kAlc        = "#ffb733";   // amber, slightly punchier
constexpr const char* kFwd        = "#33e6ff";   // cyan, slightly punchier
constexpr const char* kKnee       = "#5cff8c";
constexpr const char* kRec        = "#ff66ff";

// Build a smooth Catmull-Rom path through the given points, expressed as
// cubic Béziers so we can both stroke it and use it as a fill region.
// Open-uniform: clamp the tangent at the endpoints by reflecting.
QPainterPath smoothPath(const QVector<QPointF>& pts)
{
    QPainterPath path;
    if (pts.isEmpty()) return path;
    path.moveTo(pts.first());
    if (pts.size() == 1) return path;
    if (pts.size() == 2) { path.lineTo(pts.last()); return path; }

    const qreal t = 1.0 / 6.0;  // standard Catmull-Rom → Bézier scale
    for (int i = 0; i < pts.size() - 1; ++i) {
        const int n  = int(pts.size());
        const QPointF& p0 = pts.at(std::max(0, i - 1));
        const QPointF& p1 = pts.at(i);
        const QPointF& p2 = pts.at(i + 1);
        const QPointF& p3 = pts.at(std::min(n - 1, i + 2));
        const QPointF c1 = p1 + (p2 - p0) * t;
        const QPointF c2 = p2 - (p3 - p1) * t;
        path.cubicTo(c1, c2, p2);
    }
    return path;
}

// Concentric "glow": 3 strokes of increasing radius and decreasing alpha,
// then the final solid marker on top. Cheap and produces a credible bloom.
void drawGlowMarker(QPainter& p, const QPointF& c, double r,
                    const QColor& base, bool filled)
{
    QColor halo = base;
    for (int i = 3; i >= 1; --i) {
        halo.setAlpha(40 + 30 * (3 - i));   // 40 / 70 / 100
        p.setPen(Qt::NoPen);
        p.setBrush(halo);
        p.drawEllipse(c, r + i * 2.0, r + i * 2.0);
    }
    p.setBrush(filled ? base : Qt::NoBrush);
    p.setPen(QPen(base, filled ? 0.0 : 2.0));
    p.drawEllipse(c, r, r);
}

} // namespace

CalPlot::CalPlot(QWidget* parent) : QWidget(parent)
{
    setMinimumHeight(280);
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
    p.setRenderHints(QPainter::Antialiasing |
                     QPainter::SmoothPixmapTransform |
                     QPainter::TextAntialiasing, true);

    const QRectF full = rect();

    // ── Background: subtle vertical gradient (top darker, bottom slightly lit)
    {
        QLinearGradient bg(full.topLeft(), full.bottomLeft());
        bg.setColorAt(0.0, QColor(kBgTop));
        bg.setColorAt(1.0, QColor(kBgBot));
        p.fillRect(full, bg);
    }

    if (m_pts.isEmpty()) {
        p.setPen(QColor(kAxisTxt));
        p.setFont(QFont("Helvetica", 11));
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

    const int mL = 52, mR = 58, mT = 38, mB = 40;
    const QRectF area(full.left() + mL, full.top() + mT,
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

    // ── Plot-area card: thin frame + slight inset to feel like a panel ─
    {
        QPen frame{QColor(kFrame)};
        frame.setWidthF(1.0);
        p.setPen(frame);
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(area, 4.0, 4.0);
    }

    // ── ALC (left-axis) gridlines + labels ─────────────────────────────
    p.setFont(QFont("Consolas", 9));
    for (double v = aLo; v <= aHi + 1e-6; v += 5.0) {
        const double y = yOfAlc(v);
        const bool zero = std::fabs(v) < 1e-6;
        QPen gp{QColor(zero ? kGridKey : kGrid)};
        gp.setWidthF(zero ? 1.0 : 0.6);
        p.setPen(gp);
        p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
        p.setPen(QColor(kAlc));
        p.drawText(QRectF(full.left() + 2, y - 8, mL - 8, 16),
                   Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(v, 'f', 0));
    }

    // ── Forward-power (right-axis) labels ──────────────────────────────
    for (int k = 0; k <= 4; ++k) {
        const double v = fHi * k / 4.0;
        const double y = yOfFwd(v);
        p.setPen(QColor(kFwd));
        p.drawText(QRectF(area.right() + 6, y - 8, mR - 10, 16),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   QString::number(v, 'f', 0));
    }

    // ── tx_gain (X-axis) gridlines + labels ────────────────────────────
    for (int g = 0; g <= 100; g += 20) {
        const double x = xOf(g);
        QPen gp{QColor(kGrid)};
        gp.setWidthF(0.6);
        p.setPen(gp);
        p.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
        p.setPen(QColor(kAxisTxt));
        p.drawText(QRectF(x - 26, area.bottom() + 6, 52, 16),
                   Qt::AlignHCenter | Qt::AlignTop, QString::number(g));
    }
    p.setFont(QFont("Consolas", 9, QFont::Bold));
    p.setPen(QColor(kAxisHi));
    p.drawText(QRectF(area.left(), full.bottom() - 18, area.width(), 16),
               Qt::AlignHCenter | Qt::AlignTop, "tx_gain  (%)");

    // ── Axis-side metric labels (top of each axis edge) ────────────────
    p.setFont(QFont("Consolas", 8, QFont::Bold));
    p.setPen(QColor(kAlc));
    p.drawText(QRectF(full.left() + 2, area.top() - 18, mL + 60, 14),
               Qt::AlignLeft | Qt::AlignVCenter, "ALC  dBFS");
    p.setPen(QColor(kFwd));
    p.drawText(QRectF(area.right() - 60, area.top() - 18, mR + 60, 14),
               Qt::AlignRight | Qt::AlignVCenter, "fwd  W");

    // ── ALC target ceiling line (glowing dashed) ───────────────────────
    if (anyAlc && m_alcTarget >= aLo && m_alcTarget <= aHi) {
        const double y = yOfAlc(m_alcTarget);
        // soft glow
        QColor halo = QColor(kAlc);
        halo.setAlpha(40);
        QPen hpen(halo);
        hpen.setWidthF(5.0);
        hpen.setStyle(Qt::SolidLine);
        p.setPen(hpen);
        p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
        // dashed core
        QPen tp{QColor(kAlc)};
        tp.setStyle(Qt::DashLine);
        tp.setWidthF(1.2);
        p.setPen(tp);
        p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
        p.setFont(QFont("Consolas", 8, QFont::Bold));
        p.drawText(QPointF(area.left() + 6, y - 5),
                   QString("ALC target %1 dBFS").arg(m_alcTarget, 0, 'f', 0));
    }

    // ── Knee marker (vertical glow + dashed core) ──────────────────────
    if (m_kneeGain >= 0) {
        const double x = xOf(m_kneeGain);
        QColor halo = QColor(kKnee);
        halo.setAlpha(45);
        QPen hpen(halo);
        hpen.setWidthF(6.0);
        p.setPen(hpen);
        p.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
        QPen kp{QColor(kKnee)};
        kp.setStyle(Qt::DashLine);
        kp.setWidthF(1.3);
        p.setPen(kp);
        p.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
    }

    p.save();
    p.setClipRect(area);

    // ── Smooth curves with gradient fill ───────────────────────────────
    auto drawCurve = [&](const QVector<QPointF>& pts, const QColor& color) {
        if (pts.size() < 2) return;
        QPainterPath stroke = smoothPath(pts);

        // Fill under the curve (close to baseline) with a vertical gradient.
        QPainterPath fill = stroke;
        fill.lineTo(pts.last().x(), area.bottom());
        fill.lineTo(pts.first().x(), area.bottom());
        fill.closeSubpath();
        QLinearGradient g(0, area.top(), 0, area.bottom());
        QColor c1 = color; c1.setAlpha(110);
        QColor c2 = color; c2.setAlpha(0);
        g.setColorAt(0.0, c1);
        g.setColorAt(1.0, c2);
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawPath(fill);

        // Stroke the curve with a soft outer glow + clean inner line.
        QColor halo = color; halo.setAlpha(70);
        QPen hpen(halo); hpen.setWidthF(5.0); hpen.setCapStyle(Qt::RoundCap);
        p.setPen(hpen);
        p.setBrush(Qt::NoBrush);
        p.drawPath(stroke);

        QPen line(color); line.setWidthF(2.2); line.setCapStyle(Qt::RoundCap);
        p.setPen(line);
        p.drawPath(stroke);

        // Tiny dots at each data point (downplayed — the curve is the star).
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        for (const QPointF& pt : pts) p.drawEllipse(pt, 2.0, 2.0);
    };

    // Forward-power curve (right axis)
    {
        QVector<QPointF> pts;
        pts.reserve(m_pts.size());
        for (const Point& pt : m_pts) pts << QPointF(xOf(pt.gain), yOfFwd(pt.fwd));
        drawCurve(pts, QColor(kFwd));
    }

    // ALC peak curve (left axis)
    if (anyAlc) {
        QVector<QPointF> pts;
        pts.reserve(m_pts.size());
        for (const Point& pt : m_pts)
            if (pt.hasAlc) pts << QPointF(xOf(pt.gain), yOfAlc(pt.alc));
        drawCurve(pts, QColor(kAlc));
    }

    p.restore();

    // ── Recommended-setting marker (big glowing ring) ──────────────────
    if (m_recGain >= 0) {
        for (const Point& pt : m_pts) {
            if (pt.gain != m_recGain) continue;
            const double x = xOf(pt.gain);
            const double y = pt.hasAlc ? yOfAlc(pt.alc) : yOfFwd(pt.fwd);
            drawGlowMarker(p, QPointF(x, y), 6.0, QColor(kRec), /*filled*/ false);
            // Annotate next to the ring; flip side near the right edge so
            // the label never gets clipped against the fwd-axis labels.
            const bool toLeft  = pt.gain > 70;
            QRectF lblArea(toLeft ? x - 200 : x + 14, y - 10, 186, 22);
            p.setFont(QFont("Consolas", 9, QFont::Bold));
            p.setPen(QColor(kRec));
            p.drawText(lblArea,
                       Qt::AlignVCenter | (toLeft ? Qt::AlignRight : Qt::AlignLeft),
                       QString("rec tx_gain = %1").arg(m_recGain));
            break;
        }
    }

    // ── Legend (chips along the top) ───────────────────────────────────
    p.setFont(QFont("Consolas", 9, QFont::Bold));
    int lx = int(area.left()) + 8;
    const int ly = int(full.top()) + 10;
    auto chip = [&](const char* col, const QString& text) {
        QColor c(col);
        // Glow puck
        QColor halo = c; halo.setAlpha(80);
        p.setPen(Qt::NoPen);
        p.setBrush(halo);
        p.drawEllipse(QPointF(lx + 6, ly + 7), 7.0, 7.0);
        p.setBrush(c);
        p.drawEllipse(QPointF(lx + 6, ly + 7), 4.0, 4.0);
        p.setPen(c);
        p.drawText(QPointF(lx + 16, ly + 11), text);
        lx += 16 + p.fontMetrics().horizontalAdvance(text) + 20;
    };
    chip(kFwd, "forward power (W)");
    chip(kAlc, "ALC peak (dBFS)");
    if (m_recGain >= 0)
        chip(kRec, QString("recommended tx_gain = %1").arg(m_recGain));
}

} // namespace TciMon
