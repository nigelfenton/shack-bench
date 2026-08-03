#pragma once

// InstrumentPanel — bench instruments as a tabbed workspace.
//
//   Antenna   RigExpert AA-170 / NanoVNA-F V2 — SWR, R/X, return loss
//   Spectrum  tinySA — amplitude vs frequency
//
// Follows the CalibrationPanel precedent: a controls strip, a plot stack, a
// log, and a defensive posture around anything that touches RF.
//
// Guardrails (each one paid for by a real misdiagnosis, see the design sheet):
//  1. Cal-range badge — a sweep outside the instrument's calibrated span is
//     hatched and labelled, never rendered as if it were trustworthy.
//  2. "What was connected?" provenance — required before a sweep can be
//     saved. A beautiful graph of the wrong antenna is worse than no graph,
//     because it gets believed.
//  3. TX interlock — refuse to sweep while AE reports transmit. An analyser
//     on a live feedline is a destroyed analyser.
//  4. Auto-flag impossible physics — |Γ| ≥ 1 or R < 0 on a passive load means
//     a bad cal or an open port, not a bad antenna.
//  5. Restore the instrument's previous state and leave RF OFF on finish.

#include <QWidget>
#include <QVector>
#include <QString>

#include "CoaxAnalysis.h"
#include "Scope.h"
#include "Instrument.h"

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QThread;

namespace TciMon {

class TciClient;
class TracePlot;

class InstrumentPanel : public QWidget {
    Q_OBJECT
public:
    explicit InstrumentPanel(TciClient* tci, QWidget* parent = nullptr);
    ~InstrumentPanel() override;

    // Fed every incoming TCI line so the panel can (a) hold the TX interlock
    // and (b) overlay AE's own live SWR on the analyser's axes -- the
    // cross-instrument check nothing else in the shack can do.
    void noteIncoming(const QString& line);

private slots:
    void onProbeClicked();
    void onSweepClicked();
    void onStopClicked();
    void onSweepProgress(int done, int total, const QString& note);
    void onSweepFinished(const TciMon::SweepResult& result);
    void onSaveClicked();
    void onRecallClicked();
    void onExportCsvClicked();
    void onBandPresetChanged(int index);
    void onCursorMoved(qint64 hz);
    void onTakeOpenClicked();
    void onTakeShortClicked();
    void onCableTypeChanged(int index);
    void onAnalyseCoax();
    void onScopeCapture();
    void onScopeFinished(const TciMon::ScopeCapture& cap);
    void onScopeProgress(const QString& note);

private:
    void buildAntennaTab();
    void buildSpectrumTab();
    void buildFeedlineTab();
    void buildScopeTab();
    void log(const QString& line);
    void refreshPlots();
    void setBusy(bool busy);
    bool txInterlockBlocks(QString* why) const;
    QString provenanceLine(const SweepResult& r) const;

    TciClient*      m_tci = nullptr;

    QTabWidget*     m_tabs = nullptr;

    // --- antenna tab ---
    QComboBox*      m_antInstrument = nullptr;
    QComboBox*      m_antBandPreset = nullptr;
    QDoubleSpinBox* m_antFrom = nullptr;
    QDoubleSpinBox* m_antTo   = nullptr;
    QSpinBox*       m_antPoints = nullptr;
    QLineEdit*      m_antNote = nullptr;
    QPushButton*    m_antSweep = nullptr;
    QPushButton*    m_antStop  = nullptr;
    QLabel*         m_antStatus = nullptr;
    QLabel*         m_antCalBadge = nullptr;
    TracePlot*      m_plotSwr = nullptr;
    TracePlot*      m_plotRx  = nullptr;
    TracePlot*      m_plotRl  = nullptr;

    // --- spectrum tab ---
    QComboBox*      m_spanPreset = nullptr;
    QDoubleSpinBox* m_saFrom = nullptr;
    QDoubleSpinBox* m_saTo   = nullptr;
    QPushButton*    m_saSweep = nullptr;
    QLabel*         m_saStatus = nullptr;
    TracePlot*      m_plotSa = nullptr;

    // --- feedline tab (open/short pair) ---
    QComboBox*      m_cableType = nullptr;
    QDoubleSpinBox* m_vf = nullptr;          // editable; the catalogue seeds it
    QPushButton*    m_takeOpen = nullptr;
    QPushButton*    m_takeShort = nullptr;
    QLabel*         m_openState = nullptr;
    QLabel*         m_shortState = nullptr;
    QPlainTextEdit* m_coaxReport = nullptr;
    TracePlot*      m_plotCoax = nullptr;
    SweepResult     m_openSweep;
    SweepResult     m_shortSweep;
    bool            m_capturingOpen = false;   // which button armed the sweep
    bool            m_capturingShort = false;

    // --- scope tab (Hantek DSO2D15 over USBTMC) ---
    QPushButton*    m_scopeCapture = nullptr;
    QLabel*         m_scopeStatus = nullptr;
    QPlainTextEdit* m_scopeReport = nullptr;
    TracePlot*      m_plotScope = nullptr;
    QThread*        m_scopeThread = nullptr;
    ScopeWorker*    m_scopeWorker = nullptr;
    QString         m_scopeResource;

    // --- shared ---
    QListWidget*    m_library = nullptr;
    QPlainTextEdit* m_log = nullptr;
    QLabel*         m_cursorLabel = nullptr;

    QThread*        m_thread = nullptr;
    SweepWorker*    m_worker = nullptr;

    QVector<InstrumentId> m_found;
    SweepResult           m_live;        // most recent sweep
    SweepResult           m_reference;   // recalled baseline, overlaid
    QVector<SweepResult>  m_saved;

    bool    m_txActive = false;
    bool    m_busy = false;
    double  m_aeSwr = 0.0;      // AE's own reported SWR, for the overlay
    qint64  m_aeSwrHz = 0;
};

} // namespace TciMon
