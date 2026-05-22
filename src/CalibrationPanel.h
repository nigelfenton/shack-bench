#pragma once

// CalibrationPanel — closed-loop WSJT-X TX-drive calibration.
//
// A Qt port of the tx_drive_cal.py harness. For each operating point it
// injects a steady test tone as TCI TX audio (emulating WSJT-X at full
// Pwr), keys the radio, reads tx_sensors telemetry (forward power, SWR,
// SW-ALC), and unkeys. It sweeps tx_gain coarsely, finds the knee — the
// highest drive where ALC stays at/under the target — then re-sweeps
// finely around it to pin the recommended setting.
//
// Goal: maximum forward power with ALC held near zero.
//
// Like ConsolePanel this can key the radio, so it carries the same
// defensive model: dry-run by default, arm-to-send (never persisted),
// per-keydown watchdog, SWR abort, and an always-live STOP/UNKEY button.
//
// Requires an AetherSDR carrying the tx_gain command + tx_sensors `alc`
// field (PR #2950). Against an older build the panel detects the missing
// field on the first keyed point and refuses the run.

#include <QWidget>
#include <QByteArray>
#include <QString>
#include <QVector>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTimer;

namespace TciMon {

class TciClient;
class CalPlot;

class CalibrationPanel : public QWidget {
    Q_OBJECT
public:
    explicit CalibrationPanel(TciClient* tci, QWidget* parent = nullptr);

    // Fed every incoming line so tx_sensors telemetry can be captured and
    // the SWR watchdog can fault.
    void noteIncoming(const QString& line);

private slots:
    void onArmToggled(bool checked);
    void onStartClicked();
    void onStopClicked();
    void onStep();          // sweep state-machine tick
    void onAudioTick();     // tone streamer (~50 ms)
    void onWatchdog();      // hard keydown ceiling

private:
    enum class Phase { Idle, Settle, Keyed, Cooldown };

    struct Row {
        int    rf{0};
        int    gain{0};
        int    n{0};
        double fwdAvg{0.0}, fwdMax{0.0}, swrAvg{0.0};
        double alcAvg{0.0}, alcMax{0.0};
        bool   hasAlc{false};
    };

    void buildUI();
    void refreshBanner();
    void log(const QString& text, const QString& colorHex);
    void setRunning(bool running);

    void startPass(const QVector<int>& gains, bool finePass);
    void nextPoint();
    void beginSettle();
    void beginKeydown();
    void endKeydown();
    void finishPass();
    void concludeRun(int recGain);
    void abort(const QString& reason);
    void unkey();
    Row  recordPoint();
    int  kneeGain(const QVector<Row>& rows) const;   // -1 if none qualifies
    void redrawPlot();
    void persist();
    void restore();

    TciClient* m_tci{};

    // Controls
    QLabel*         m_banner{};
    QCheckBox*      m_arm{};
    QSpinBox*       m_rf{};
    QDoubleSpinBox* m_alcTarget{};
    QDoubleSpinBox* m_dwell{};
    QDoubleSpinBox* m_cooldown{};
    QDoubleSpinBox* m_swrLimit{};
    QLineEdit*      m_coarseEdit{};
    QPushButton*    m_startBtn{};
    QPushButton*    m_stopBtn{};
    QPushButton*    m_saveBtn{};
    QLabel*         m_progress{};
    QLabel*         m_result{};
    QPlainTextEdit* m_transcript{};
    CalPlot*        m_plot{};

    // Timers
    QTimer* m_step{};
    QTimer* m_audio{};
    QTimer* m_watchdog{};

    // Run state
    bool   m_armed{false};
    bool   m_running{false};
    bool   m_dry{false};
    Phase  m_phase{Phase::Idle};
    bool   m_streaming{false};
    bool   m_finePass{false};
    int    m_rfPower{100};
    int    m_curGain{0};
    int    m_coarseStep{10};
    double m_alcTargetVal{-10.0};
    double m_swrLimitVal{3.0};
    QVector<int> m_queue;        // remaining gains in the running pass
    int    m_kneeGain{-1};
    int    m_recGain{-1};

    QVector<Row> m_passRows;     // rows from the pass in progress
    QVector<Row> m_coarseRows;   // coarse pass result (for fine fallback)
    QVector<Row> m_allRows;      // every measured point — CSV + plot

    // Per-point telemetry accumulator
    QVector<double> m_sFwd, m_sSwr, m_sAlc;
    bool   m_haveAlc{false};

    // Test tone — one second of mono samples, looped seamlessly
    QVector<float> m_tone;
    int    m_tonePos{0};
};

} // namespace TciMon
