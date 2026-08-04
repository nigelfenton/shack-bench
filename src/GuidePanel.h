#pragma once

// GuidePanel — a step-by-step walkthrough beside the instrument it drives.
//
// Built because handing someone a list of SCPI commands once, verbally, is a
// poor way to teach a bench procedure they will repeat months apart. The panel
// holds the method, the reasoning and the verification in one place, and runs
// the steps itself so there is nothing to mistype.
//
// Two guides so far:
//   Calibration — the OSL + THROUGH sequence, with the verify step that
//                 distinguishes "a cal exists" from "a cal is correct"
//   Trap bench  — how a series-through S21 measurement works and how to read it
//
// Design rule: each step says WHAT to do, WHY it matters, and — where it can —
// checks the result before letting the operator move on. A guide that lets you
// press Next through a failed step is just decoration.

#include <QString>
#include <QVector>
#include <QWidget>

class QLabel;
class QPushButton;
class QTextBrowser;
class QProgressBar;

namespace TciMon {

struct GuideStep {
    QString title;
    QString body;          // rich text; the teaching lives here
    QString action;        // button label; empty = nothing to run, just read
    QString command;       // instrument command the action sends, if any
    bool    needsFitting = false;   // operator must attach something first
    QString fitting;                // what to attach, e.g. "the OPEN standard"
    QString verify;                 // command whose reply is checked, if any
    QString expect;                 // substring the reply must contain
};

struct Guide {
    QString name;
    QString intro;
    QVector<GuideStep> steps;
};

// The two guides. Kept as data so the wording can be reviewed without reading
// UI code, and so a third guide is a table entry rather than a new widget.
const Guide& calibrationGuide();
const Guide& trapGuide();
const Guide& velocityFactorGuide();
const Guide& chokeGuide();

class GuidePanel : public QWidget {
    Q_OBJECT
public:
    explicit GuidePanel(QWidget* parent = nullptr);

    void setGuide(const Guide& g);
    void reset();

signals:
    // The panel does not own the instrument connection — it asks the owning
    // tab to run a command and tell it what came back.
    void runCommand(const QString& command, const QString& stepTitle);

public slots:
    // Result of the command the panel last asked for.
    void commandFinished(bool ok, const QString& reply);

private slots:
    void onNext();
    void onBack();
    void onAction();

private:
    void render();

    Guide          m_guide;
    int            m_index = 0;
    bool           m_actionDone = false;
    QString        m_lastReply;

    QLabel*        m_heading = nullptr;
    QProgressBar*  m_progress = nullptr;
    QTextBrowser*  m_body = nullptr;
    QLabel*        m_status = nullptr;
    QPushButton*   m_back = nullptr;
    QPushButton*   m_action = nullptr;
    QPushButton*   m_next = nullptr;
};

} // namespace TciMon
