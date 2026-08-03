#include "TracePlot.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <algorithm>
#include <cmath>
#include <limits>

namespace TciMon {

namespace {
const QColor kBg      ("#050a14");
const QColor kGrid    ("#16202e");
const QColor kGridKey ("#33405a");
const QColor kLabel   ("#6b8099");
const QColor kFrame   ("#22304a");
const QColor kTitle   ("#cfe3ff");
const QColor kWarn    ("#ffb454");
}

TracePlot::TracePlot(QWidget* parent) : QWidget(parent)
{
    setMinimumHeight(200);
    setAutoFillBackground(true);
    setMouseTracking(true);
}

void TracePlot::setUnit(Unit u)                     { m_unit = u; update(); }
void TracePlot::setXAxis(XAxis a)                   { m_xAxis = a; update(); }
void TracePlot::setTitle(const QString& t)          { m_title = t; update(); }
void TracePlot::setProvenance(const QString& t)     { m_provenance = t; update(); }
void TracePlot::setPlaceholder(const QString& t)    { m_placeholder = t; update(); }
void TracePlot::setTraces(const QVector<Trace>& t)  { m_traces = t; update(); }
void TracePlot::setSpans(const QVector<Span>& s)    { m_spans = s; update(); }
void TracePlot::setMarkers(const QVector<Marker>& m){ m_markers = m; update(); }

void TracePlot::setFrequencyRange(qint64 a, qint64 b)
{
    m_haveRange = true; m_fromHz = a; m_toHz = b; update();
}
void TracePlot::clearFrequencyRange() { m_haveRange = false; update(); }

void TracePlot::setCalibratedRange(qint64 a, qint64 b)
{
    m_haveCal = true; m_calFrom = a; m_calTo = b; update();
}
void TracePlot::clearCalibratedRange() { m_haveCal = false; update(); }

void TracePlot::clear()
{
    m_traces.clear();
    m_markers.clear();
    m_cursorHz = -1;
    update();
}

// Choose a Y range and tick set appropriate to the unit.
TracePlot::Axis TracePlot::yAxis() const
{
    double lo = std::numeric_limits<double>::max();
    double hi = -std::numeric_limits<double>::max();
    for (const auto& t : m_traces) {
        if (!t.visible) continue;
        for (auto it = t.points.begin(); it != t.points.end(); ++it) {
            lo = std::min(lo, it.value());
            hi = std::max(hi, it.value());
        }
    }
    if (lo > hi) { lo = 0; hi = 1; }

    switch (m_unit) {
    case Unit::Swr: {
        // 1:1 at the BOTTOM, always. A dip in the trace must read as a dip in
        // the match; flipping this axis inverts the meaning of every plot.
        double top = std::min(10.0, std::max(2.0, std::ceil(hi * 2.0) / 2.0));
        return {1.0, top, {1.0, 1.5, 2.0, 2.5, 3.0, 4.0, 5.0, 7.0, 10.0}};
    }
    case Unit::Ohms: {
        // Symmetric about zero so reactance sign is readable at a glance.
        double m = std::max(std::abs(lo), std::abs(hi));
        m = std::max(50.0, std::ceil(m / 50.0) * 50.0);
        QVector<double> ticks;
        for (double v = -m; v <= m + 1e-9; v += m / 4.0) ticks << v;
        return {-m, m, ticks};
    }
    case Unit::Db: {
        double top = std::ceil(std::max(hi, 0.0) / 5.0) * 5.0;
        double bot = std::floor(std::min(lo, 0.0) / 5.0) * 5.0;
        if (top - bot < 10) top = bot + 10;
        QVector<double> ticks;
        for (double v = bot; v <= top + 1e-9; v += (top - bot) / 5.0) ticks << v;
        return {bot, top, ticks};
    }
    case Unit::Volts: {
        double m = std::max(std::abs(lo), std::abs(hi));
        if (m < 1e-6) m = 1.0;
        // Round up to a sensible 1/2/5 step so the grid reads cleanly.
        const double mag = std::pow(10.0, std::floor(std::log10(m)));
        const double norm = m / mag;
        const double nice = (norm <= 1.0 ? 1.0 : norm <= 2.0 ? 2.0
                                                             : norm <= 5.0 ? 5.0 : 10.0);
        m = nice * mag;
        QVector<double> ticks;
        for (int k = -4; k <= 4; ++k) ticks << m * k / 4.0;
        return {-m, m, ticks};
    }
    case Unit::Dbm:
    default: {
        double top = std::ceil((hi + 5) / 10.0) * 10.0;
        double bot = std::floor((lo - 5) / 10.0) * 10.0;
        if (top - bot < 30) bot = top - 30;
        QVector<double> ticks;
        for (double v = bot; v <= top + 1e-9; v += (top - bot) / 6.0) ticks << v;
        return {bot, top, ticks};
    }
    }
}

QString TracePlot::formatValue(double v) const
{
    switch (m_unit) {
    case Unit::Swr:  return QString::number(v, 'f', 2);
    case Unit::Ohms: return QString::number(v, 'f', 1);
    case Unit::Db:   return QString::number(v, 'f', 1);
    case Unit::Dbm:  return QString::number(v, 'f', 1);
    case Unit::Volts:
        if (std::abs(v) >= 1.0)  return QString::number(v, 'f', 2);
        if (std::abs(v) >= 1e-3) return QString::number(v * 1e3, 'f', 0) + "m";
        return QString::number(v * 1e6, 'f', 0) + "u";
    }
    return QString::number(v);
}

void TracePlot::mouseMoveEvent(QMouseEvent* e)
{
    const int mL = 52, mR = 14, mT = m_title.isEmpty() ? 14 : 30;
    const int mB = m_provenance.isEmpty() ? 34 : 48;
    const QRect area(rect().left() + mL, rect().top() + mT,
                     rect().width() - mL - mR, rect().height() - mT - mB);

    qint64 fMin = 0, fMax = 0;
    if (m_haveRange) { fMin = m_fromHz; fMax = m_toHz; }
    else {
        fMin = std::numeric_limits<qint64>::max(); fMax = 0;
        for (const auto& t : m_traces) {
            if (!t.visible || t.points.isEmpty()) continue;
            fMin = std::min(fMin, t.points.firstKey());
            fMax = std::max(fMax, t.points.lastKey());
        }
    }

    if (!area.contains(e->pos()) || fMax <= fMin) {
        if (m_cursorHz >= 0) { m_cursorHz = -1; emit cursorMoved(-1); update(); }
        return;
    }
    const double t = double(e->pos().x() - area.left()) / double(area.width());
    m_cursorHz = fMin + qint64(t * double(fMax - fMin));
    emit cursorMoved(m_cursorHz);
    update();
}

void TracePlot::leaveEvent(QEvent*)
{
    if (m_cursorHz >= 0) { m_cursorHz = -1; emit cursorMoved(-1); update(); }
}

void TracePlot::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRect full = rect();
    p.fillRect(full, kBg);

    QVector<Trace> traces;
    for (const auto& t : m_traces)
        if (t.visible && !t.points.isEmpty()) traces.push_back(t);

    const int mL = 52, mR = 14;
    const int mT = m_title.isEmpty() ? 14 : 30;
    const int mB = m_provenance.isEmpty() ? 34 : 48;
    const QRect area(full.left() + mL, full.top() + mT,
                     full.width() - mL - mR, full.height() - mT - mB);

    if (!m_title.isEmpty()) {
        QFont tf("Segoe UI", 9, QFont::DemiBold);
        p.setFont(tf);
        p.setPen(kTitle);
        p.drawText(QRectF(full.left() + 8, full.top() + 6, full.width() - 16, 18),
                   Qt::AlignLeft | Qt::AlignVCenter, m_title);
    }

    if (traces.isEmpty()) {
        p.setFont(QFont("Segoe UI", 9));
        p.setPen(kLabel);
        p.drawText(area, Qt::AlignCenter, m_placeholder);
        return;
    }
    if (area.width() < 60 || area.height() < 50) return;

    // ---- axes -----------------------------------------------------------
    const Axis ax = yAxis();
    qint64 fMin, fMax;
    if (m_haveRange) { fMin = m_fromHz; fMax = m_toHz; }
    else {
        fMin = std::numeric_limits<qint64>::max(); fMax = 0;
        for (const auto& t : traces) {
            fMin = std::min(fMin, t.points.firstKey());
            fMax = std::max(fMax, t.points.lastKey());
        }
    }
    if (fMax <= fMin) fMax = fMin + 1;

    auto xOf = [&](qint64 hz) {
        double t = double(hz - fMin) / double(fMax - fMin);
        return area.left() + t * area.width();
    };
    auto yOf = [&](double v) {
        double t = (v - ax.lo) / (ax.hi - ax.lo);
        return area.bottom() - t * area.height();
    };

    // ---- shaded spans (ham bands) ---------------------------------------
    for (const auto& s : m_spans) {
        if (s.toHz < fMin || s.fromHz > fMax) continue;
        const double x1 = xOf(std::max(s.fromHz, fMin));
        const double x2 = xOf(std::min(s.toHz, fMax));
        if (x2 - x1 < 0.5) continue;
        p.fillRect(QRectF(x1, area.top(), x2 - x1, area.height()), s.color);
        if (!s.label.isEmpty() && x2 - x1 > 26) {
            p.setFont(QFont("Consolas", 7));
            p.setPen(kLabel);
            p.drawText(QRectF(x1, area.top() + 2, x2 - x1, 12),
                       Qt::AlignHCenter | Qt::AlignTop, s.label);
        }
    }

    // ---- uncalibrated hatching ------------------------------------------
    // Anything outside the instrument's calibrated span is drawn hatched, so
    // a reading taken there can never masquerade as a trustworthy one.
    if (m_haveCal) {
        QBrush hatch(QColor(255, 180, 84, 26), Qt::BDiagPattern);
        if (m_calFrom > fMin) {
            const double x2 = xOf(std::min(m_calFrom, fMax));
            p.fillRect(QRectF(area.left(), area.top(),
                              x2 - area.left(), area.height()), hatch);
        }
        if (m_calTo < fMax) {
            const double x1 = xOf(std::max(m_calTo, fMin));
            p.fillRect(QRectF(x1, area.top(),
                              area.right() - x1, area.height()), hatch);
        }
    }

    // ---- grid ------------------------------------------------------------
    p.setFont(QFont("Consolas", 8));
    for (double t : ax.ticks) {
        if (t < ax.lo - 1e-6 || t > ax.hi + 1e-6) continue;
        const double y = yOf(t);
        const bool key = (m_unit == Unit::Swr && std::abs(t - 2.0) < 1e-6)
                      || (m_unit == Unit::Ohms && std::abs(t) < 1e-6)
                      || (m_unit == Unit::Db && std::abs(t) < 1e-6);
        p.setPen(key ? kGridKey : kGrid);
        p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
        p.setPen(kLabel);
        p.drawText(QRectF(full.left(), y - 7, mL - 8, 14),
                   Qt::AlignRight | Qt::AlignVCenter, formatValue(t));
    }

    const int nf = 5;
    for (int i = 0; i <= nf; ++i) {
        const qint64 hz = fMin + (fMax - fMin) * i / nf;
        const double x = xOf(hz);
        p.setPen(kGrid);
        p.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
        p.setPen(kLabel);
        QString tick;
        if (m_xAxis == XAxis::TimeMicros) {
            // Keys are microseconds. Pick a unit that keeps the label short.
            const double us = double(hz);
            if (std::abs(us) >= 1000.0)   tick = QString::number(us / 1000.0, 'f', 2) + " ms";
            else if (std::abs(us) >= 1.0) tick = QString::number(us, 'f', 1) + " us";
            else                          tick = QString::number(us * 1000.0, 'f', 0) + " ns";
        } else {
            const double mhz = hz / 1e6;
            tick = QString::number(mhz, 'f', mhz < 100 ? 3 : 1);
        }
        p.drawText(QRectF(x - 48, area.bottom() + 4, 96, 14),
                   Qt::AlignHCenter | Qt::AlignTop, tick);
    }

    p.setPen(kFrame);
    p.drawRect(area);

    // ---- markers ---------------------------------------------------------
    for (const auto& mk : m_markers) {
        if (mk.hz < fMin || mk.hz > fMax) continue;
        const double x = xOf(mk.hz);
        QPen pen(mk.color); pen.setStyle(Qt::DotLine);
        p.setPen(pen);
        p.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
        if (!mk.label.isEmpty()) {
            p.setFont(QFont("Consolas", 7));
            p.setPen(mk.color);
            p.drawText(QPointF(x + 3, area.top() + 22), mk.label);
        }
    }

    // ---- traces ----------------------------------------------------------
    for (const auto& t : traces) {
        QPolygonF poly;
        poly.reserve(t.points.size());
        for (auto it = t.points.begin(); it != t.points.end(); ++it)
            poly << QPointF(xOf(it.key()),
                            yOf(std::clamp(it.value(), ax.lo, ax.hi)));
        QPen pen(t.color);
        pen.setWidthF(t.dashed ? 1.2 : 1.8);
        if (t.dashed) pen.setStyle(Qt::DashLine);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(poly);

        // Best-match marker, but only where it is meaningful.
        if (m_unit == Unit::Swr) {
            qint64 bf = 0; double bs = std::numeric_limits<double>::max();
            for (auto it = t.points.begin(); it != t.points.end(); ++it)
                if (it.value() < bs) { bs = it.value(); bf = it.key(); }
            if (bf) {
                p.setBrush(t.color);
                p.setPen(Qt::NoPen);
                p.drawEllipse(QPointF(xOf(bf), yOf(std::clamp(bs, ax.lo, ax.hi))),
                              3.2, 3.2);
            }
        }
    }

    // ---- cursor + readout -------------------------------------------------
    if (m_cursorHz >= fMin && m_cursorHz <= fMax) {
        const double x = xOf(m_cursorHz);
        p.setPen(QPen(QColor("#8fb8ff"), 1.0, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));

        QStringList rows;
        if (m_xAxis == XAxis::TimeMicros) {
            const double us = double(m_cursorHz);
            rows << (std::abs(us) >= 1000.0
                         ? QString("%1 ms").arg(us / 1000.0, 0, 'f', 3)
                         : QString("%1 us").arg(us, 0, 'f', 2));
        } else {
            rows << QString("%1 MHz").arg(m_cursorHz / 1e6, 0, 'f', 4);
        }
        for (const auto& t : traces) {
            auto it = t.points.lowerBound(m_cursorHz);
            if (it == t.points.end() && !t.points.isEmpty()) --it;
            if (it == t.points.end()) continue;
            rows << QString("%1  %2").arg(t.label, formatValue(it.value()));
        }
        p.setFont(QFont("Consolas", 8));
        const QFontMetrics fm(p.font());
        int w = 0;
        for (const auto& r : rows) w = std::max(w, fm.horizontalAdvance(r));
        const int h = rows.size() * (fm.height() + 1) + 8;
        double bx = x + 8;
        if (bx + w + 12 > area.right()) bx = x - w - 20;
        const QRectF box(bx, area.top() + 6, w + 12, h);
        p.fillRect(box, QColor(5, 10, 20, 225));
        p.setPen(kFrame);
        p.drawRect(box);
        p.setPen(kTitle);
        for (int i = 0; i < rows.size(); ++i)
            p.drawText(QPointF(box.left() + 6,
                               box.top() + 6 + fm.ascent() + i * (fm.height() + 1)),
                       rows[i]);
    }

    // ---- legend -----------------------------------------------------------
    {
        p.setFont(QFont("Consolas", 8));
        const QFontMetrics fm(p.font());
        int lx = area.left() + 6;
        const int ly = area.bottom() - 14;
        for (const auto& t : traces) {
            QPen pen(t.color); pen.setWidthF(2.0);
            if (t.dashed) pen.setStyle(Qt::DashLine);
            p.setPen(pen);
            p.drawLine(QPointF(lx, ly + 5), QPointF(lx + 16, ly + 5));
            p.setPen(kLabel);
            p.drawText(QPointF(lx + 21, ly + 9), t.label);
            lx += 27 + fm.horizontalAdvance(t.label);
            if (lx > area.right() - 60) break;
        }
    }

    // ---- provenance strip --------------------------------------------------
    if (!m_provenance.isEmpty()) {
        p.setFont(QFont("Consolas", 7));
        p.setPen(kLabel);
        p.drawText(QRectF(full.left() + 6, full.bottom() - 15,
                          full.width() - 12, 14),
                   Qt::AlignLeft | Qt::AlignVCenter, m_provenance);
    }

    // Out-of-cal warning rides on top of everything.
    if (m_haveCal && (fMin < m_calFrom || fMax > m_calTo)) {
        p.setFont(QFont("Segoe UI", 8, QFont::DemiBold));
        p.setPen(kWarn);
        p.drawText(QRectF(area.left(), area.top() + 2, area.width() - 6, 14),
                   Qt::AlignRight | Qt::AlignTop,
                   QString("outside calibration %1-%2 MHz")
                       .arg(m_calFrom / 1e6, 0, 'f', 3)
                       .arg(m_calTo / 1e6, 0, 'f', 3));
    }
}

} // namespace TciMon
