#pragma once

// TracePlot — a general custom-painted "value vs frequency" graph.
//
// SwrPlot draws one small-multiples panel per band for SWR specifically.
// TracePlot is its generalisation: ONE shared frequency axis with any number
// of overlaid traces, an explicit Y unit, band shading, markers, a hover
// cursor with live readout, and a provenance strip.
//
// It is used by both instrument tabs:
//   Antenna  — SWR (1.0 at the BOTTOM), R and X in ohms, return loss in dB
//   Spectrum — amplitude in dBm
//
// No Qt Charts dependency; Shack-Bench stays lean. Same palette as SwrPlot.

#include <QWidget>
#include <QMap>
#include <QVector>
#include <QString>
#include <QColor>

namespace TciMon {

class TracePlot : public QWidget {
    Q_OBJECT
public:
    // What the Y axis means. Governs scaling, tick choice and formatting --
    // and, for Swr, the rule that 1:1 sits at the BOTTOM so a dip reads as a
    // dip. Inverting an SWR axis reverses its meaning; do not.
    enum class Unit { Swr, Ohms, Db, Dbm, Volts };

    // The X axis is frequency for every instrument except the scope, which is
    // time. This only changes tick formatting and the axis caption; the data
    // is still keyed by a qint64 (microseconds instead of Hz).
    enum class XAxis { FrequencyHz, TimeMicros };

    struct Trace {
        QString             label;       // "40m live" / "baseline 2026-08-02"
        QColor              color;
        QMap<qint64,double> points;      // freqHz -> value (sorted by key)
        bool                dashed = false;   // recalled/reference traces
        bool                visible = true;
    };

    // A shaded frequency span with a caption -- ham bands, or the calibrated
    // range of the instrument.
    struct Span {
        qint64  fromHz = 0;
        qint64  toHz   = 0;
        QColor  color;
        QString label;
    };

    // A labelled vertical line -- an operating frequency, a marker peak.
    struct Marker {
        qint64  hz = 0;
        QString label;
        QColor  color;
    };

    explicit TracePlot(QWidget* parent = nullptr);

    void setUnit(Unit u);
    void setXAxis(XAxis a);
    void setTitle(const QString& title);        // headline, drawn top-left
    void setProvenance(const QString& text);    // small strip along the bottom
    void setPlaceholder(const QString& text);   // shown when there is no data

    void setTraces(const QVector<Trace>& traces);
    void setSpans(const QVector<Span>& spans);
    void setMarkers(const QVector<Marker>& markers);

    // Pin the frequency axis. Without this the axis fits the data, which makes
    // two sweeps of different spans silently non-comparable when overlaid.
    void setFrequencyRange(qint64 fromHz, qint64 toHz);
    void clearFrequencyRange();

    // Draw the region OUTSIDE this span as hatched "uncalibrated" territory.
    // A sweep that leaves its calibration must never look as trustworthy as
    // one inside it.
    void setCalibratedRange(qint64 fromHz, qint64 toHz);
    void clearCalibratedRange();

    void clear();

signals:
    // Emitted as the pointer moves across the plot so a panel can mirror the
    // reading in its own status line. hz < 0 means the cursor left the area.
    void cursorMoved(qint64 hz);

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    struct Axis { double lo, hi; QVector<double> ticks; };
    Axis yAxis() const;
    QString formatValue(double v) const;

    Unit            m_unit = Unit::Swr;
    XAxis           m_xAxis = XAxis::FrequencyHz;
    QString         m_title;
    QString         m_provenance;
    QString         m_placeholder = "No sweep captured yet.";
    QVector<Trace>  m_traces;
    QVector<Span>   m_spans;
    QVector<Marker> m_markers;

    bool    m_haveRange = false;
    qint64  m_fromHz = 0, m_toHz = 0;
    bool    m_haveCal = false;
    qint64  m_calFrom = 0, m_calTo = 0;

    qint64  m_cursorHz = -1;
};

} // namespace TciMon
