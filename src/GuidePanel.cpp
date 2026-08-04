#include "GuidePanel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace TciMon {

const Guide& calibrationGuide()
{
    static Guide g = [] {
        Guide guide;
        guide.name = "Calibrate the NanoVNA (OSL + THROUGH)";
        guide.intro =
            "<p>A calibration tells the analyser what <i>perfect</i> looks like, "
            "so it can subtract its own cables, connectors and internal errors "
            "from every later measurement.</p>"
            "<p>You fit three known standards on port 1 — <b>Open</b>, "
            "<b>Short</b>, <b>Load</b> — and then join the two ports together "
            "for <b>Through</b>. The first three fix reflection measurements "
            "(SWR, R, X). The fourth fixes transmission measurements (S21), "
            "which is what the Trap bench needs.</p>"
            "<p><b>A calibration belongs to the frequency range it was taken "
            "over.</b> Sweep outside that range later and the analyser "
            "interpolates, quietly, and can produce confident nonsense — this "
            "shack has seen SWR 105 and |Γ| > 1 from exactly that mistake. So "
            "the range is set first, before any standard is fitted.</p>";

        GuideStep s;

        s = {};
        s.title = "Set the frequency range FIRST";
        s.body =
            "<p>The cal is only valid across the span you set now. Pick the "
            "widest range you will actually use — for HF trap and antenna work "
            "<b>3–30 MHz</b> is a sensible default.</p>"
            "<p>⚠ Wider is not automatically better: the analyser has a fixed "
            "201 points, so 3–30 MHz gives 135 kHz steps. Narrow bands like "
            "30 m (50 kHz wide) fall <i>between</i> samples. For fine work on "
            "one band, cal a narrow span instead.</p>";
        s.action = "Set 3–30 MHz, 201 points";
        s.command = "sweep 3000000 30000000 201";
        s.verify = "sweep";
        s.expect = "3000000 30000000";
        guide.steps << s;

        s = {};
        s.title = "Clear the old calibration";
        s.body =
            "<p>Starting from a clean slate avoids mixing standards captured "
            "over different ranges — a stale term is worse than none, because "
            "nothing warns you it is there.</p>";
        s.action = "Reset the calibration";
        s.command = "cal reset";
        guide.steps << s;

        s = {};
        s.title = "Fit the OPEN standard";
        s.body =
            "<p>Screw the <b>Open</b> standard onto <b>port 1 (CH0)</b> — or "
            "onto the end of the test lead, if you want the cal to include "
            "that lead, which you usually do.</p>"
            "<p><i>Why:</i> an open circuit reflects everything back with no "
            "phase inversion. Measuring a known-perfect reflection lets the "
            "analyser work out what its own hardware adds.</p>"
            "<p>⚠ Whatever you cal <i>through</i> becomes invisible in later "
            "measurements. Cal at the end of the lead you will actually use.</p>";
        s.needsFitting = true;
        s.fitting = "the OPEN standard on port 1";
        s.action = "Capture OPEN";
        s.command = "cal open";
        guide.steps << s;

        s = {};
        s.title = "Fit the SHORT standard";
        s.body =
            "<p>Swap the Open for the <b>Short</b> standard, same place.</p>"
            "<p><i>Why:</i> a short also reflects everything, but inverted. "
            "Open and Short together pin down the phase behaviour across the "
            "whole span.</p>";
        s.needsFitting = true;
        s.fitting = "the SHORT standard on port 1";
        s.action = "Capture SHORT";
        s.command = "cal short";
        guide.steps << s;

        s = {};
        s.title = "Fit the 50 Ω LOAD";
        s.body =
            "<p>Swap in the <b>Load</b> — a precision 50 Ω termination.</p>"
            "<p><i>Why:</i> it reflects (almost) nothing, which establishes the "
            "analyser's noise floor and its directivity — how well it can tell "
            "an outgoing wave from a returning one.</p>"
            "<p>⚠ <b>Wait a moment after fitting before capturing.</b> This "
            "step has failed silently here before: it reported OK while the "
            "sweep had not yet settled on the newly-fitted standard, and the "
            "result read tens of kΩ on a 50 Ω load. The guide pauses for you.</p>";
        s.needsFitting = true;
        s.fitting = "the 50 Ω LOAD on port 1";
        s.action = "Capture LOAD";
        s.command = "cal load";
        guide.steps << s;

        s = {};
        s.title = "Join port 1 to port 2 — the THROUGH";
        s.body =
            "<p>Connect <b>CH0 to CH1</b> directly, using the same leads and "
            "adaptors you will use for the measurement. A barrel connector or "
            "a short known-good cable is ideal.</p>"
            "<p><i>Why:</i> this is the step that makes <b>S21</b> meaningful. "
            "It tells the analyser what \"nothing in the way\" looks like, so "
            "everything it measures afterwards is the <i>device</i> and not the "
            "leads. Without it a trap's notch depth includes your cable loss "
            "and the Q figure is only approximate.</p>"
            "<p>⚠ Include any adaptors that will be part of the trap fixture. "
            "What you cal out is what stops being measured.</p>";
        s.needsFitting = true;
        s.fitting = "CH0 joined to CH1 (through)";
        s.action = "Capture THROUGH";
        s.command = "cal thru";
        guide.steps << s;

        s = {};
        s.title = "Apply and save";
        s.body =
            "<p>Finish the calibration and store it in slot 0 so it survives a "
            "power cycle.</p>";
        s.action = "Apply and save to slot 0";
        s.command = "cal done";
        s.verify = "cal";
        s.expect = "thru";
        guide.steps << s;

        s = {};
        s.title = "VERIFY — the step most people skip";
        s.body =
            "<p><b>Leave the 50 Ω load fitted on port 1</b> and measure it.</p>"
            "<p>⚠ The instrument reporting <tt>cal'ed</tt> only means "
            "<i>a calibration exists</i>. It does <b>not</b> mean the "
            "calibration is correct. The only way to know is to measure a "
            "known thing and see whether you get the known answer.</p>"
            "<p>A good calibration reads:</p>"
            "<ul>"
            "<li><b>R</b> 49.9 – 50.2 Ω</li>"
            "<li><b>X</b> within ±0.5 Ω</li>"
            "<li><b>SWR</b> 1.000 – 1.010</li>"
            "<li><b>zero</b> points with negative R or |Γ| ≥ 1</li>"
            "</ul>"
            "<p>Anything else — negative resistance, |Γ| above 1, a suspiciously "
            "perfect 0.0000 across a wide span — means redo it. Those are "
            "physically impossible on a passive load, so they are proof of a "
            "bad cal rather than a bad measurement.</p>";
        s.needsFitting = true;
        s.fitting = "the 50 Ω LOAD back on port 1";
        s.action = "Run the verification sweep";
        s.command = "__verify_load";     // handled specially by the tab
        guide.steps << s;

        return guide;
    }();
    return g;
}

const Guide& trapGuide()
{
    static Guide g = [] {
        Guide guide;
        guide.name = "Measure a coil trap";
        guide.intro =
            "<p>A trap is a coil and capacitor in parallel. At one particular "
            "frequency — its <b>resonance</b> — that combination becomes a very "
            "high impedance and effectively an open circuit. On a trapped "
            "antenna this is what makes the section beyond it disappear on one "
            "band while staying connected on the lower ones.</p>"
            "<p>To measure one on the bench we exploit exactly that: put the "
            "trap <b>in series</b> between the analyser's two ports and sweep. "
            "Away from resonance the trap passes signal, so S21 is near 0 dB. "
            "At resonance it blocks, and S21 falls into a deep <b>notch</b>. "
            "The bottom of that notch is the resonant frequency.</p>";

        GuideStep s;

        s = {};
        s.title = "What the numbers mean";
        s.body =
            "<p><b>f0 — resonant frequency.</b> The bottom of the notch. This "
            "is the number you are usually chasing.</p>"
            "<p><b>Depth</b> — how far the notch falls below the passband. A "
            "deep notch means a low-loss trap; a shallow one means resistance "
            "in the coil is spoiling it.</p>"
            "<p><b>Q — sharpness.</b> f0 divided by the 3 dB bandwidth. High Q "
            "is a narrow, deep notch; low Q is broad and shallow. A lossy or "
            "damp coil measures as lower Q.</p>"
            "<p>⚠ What you get here is <b>loaded Q</b> — the analyser's 50 Ω "
            "source and load damp the resonance, so it reads lower than the "
            "trap's unloaded Q. It is the honest number for antenna work, "
            "because the trap is loaded in service too, but it is not the "
            "figure a coil datasheet quotes.</p>";
        guide.steps << s;

        s = {};
        s.title = "Fit the trap in SERIES";
        s.body =
            "<p>Connect <b>CH0 → trap → CH1</b>. The trap is in the signal "
            "path, not shunted across it to ground.</p>"
            "<p>⚠ This matters. A trap connected across the line to ground "
            "instead gives an almost flat trace with no notch at all — you "
            "would conclude the trap was faulty when the <i>fixture</i> was "
            "wrong. Series through, every time.</p>"
            "<p>Keep the leads short and consistent between traps. If you are "
            "comparing five traps, anything that changes between them must be "
            "the trap.</p>";
        s.needsFitting = true;
        s.fitting = "the trap IN SERIES between CH0 and CH1";
        guide.steps << s;

        s = {};
        s.title = "Sweep wide first";
        s.body =
            "<p>Start with a wide span — <b>3–30 MHz</b> — so the notch is "
            "somewhere in view. You cannot measure what is off-screen, and a "
            "narrow sweep that misses the resonance reports the lowest point it "
            "<i>can</i> see, which is meaningless.</p>"
            "<p>Shack-Bench refuses that case rather than reporting it: if the "
            "minimum sits at the edge of the sweep it tells you to widen the "
            "span instead of naming a false f0.</p>";
        guide.steps << s;

        s = {};
        s.title = "Then narrow the span for Q";
        s.body =
            "<p>Once you know roughly where f0 is, sweep a narrow span around "
            "it — say ±10 %. The 3 dB points are then measured with far more "
            "resolution, so the Q figure becomes meaningful rather than "
            "approximate.</p>"
            "<p>With only 201 points, a 3–30 MHz sweep puts 135 kHz between "
            "samples. A trap with a 3 dB bandwidth of 40 kHz is <i>entirely "
            "between two points</i> at that resolution.</p>";
        guide.steps << s;

        s = {};
        s.title = "Reading the result on an antenna";
        s.body =
            "<p>⚠ A trap measured on the bench and the same trap measured on "
            "the antenna will <b>not</b> agree. In service it sees the "
            "surrounding tubing's stray capacitance, which pulls its resonance "
            "<i>down</i> — often by several percent.</p>"
            "<p>So the bench figure is best used for <b>comparison</b>: between "
            "traps, or before and after an adjustment. Absolute agreement with "
            "the band edge is not expected.</p>"
            "<p>For a trapped vertical: if a band resonates low, the bench "
            "measurement tells you whether the <i>trap</i> is off-frequency or "
            "whether the <i>section length</i> is wrong. Those need different "
            "fixes, and without this measurement you are guessing.</p>";
        guide.steps << s;

        return guide;
    }();
    return g;
}

// ---------------------------------------------------------------------------

GuidePanel::GuidePanel(QWidget* parent) : QWidget(parent)
{
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(8, 8, 8, 8);
    v->setSpacing(6);

    m_heading = new QLabel();
    m_heading->setWordWrap(true);
    m_heading->setStyleSheet(
        "color:#cfe3ff; font-size:14px; font-weight:600;");
    v->addWidget(m_heading);

    m_progress = new QProgressBar();
    m_progress->setTextVisible(true);
    m_progress->setMaximumHeight(16);
    v->addWidget(m_progress);

    m_body = new QTextBrowser();
    m_body->setOpenExternalLinks(false);
    m_body->setStyleSheet(
        "background:#050a14; color:#dde6f0; border:1px solid #22304a;");
    v->addWidget(m_body, 1);

    m_status = new QLabel();
    m_status->setWordWrap(true);
    m_status->setStyleSheet("color:#9fb4cc; font-family:Consolas;");
    v->addWidget(m_status);

    auto* row = new QHBoxLayout();
    m_back = new QPushButton("← Back");
    connect(m_back, &QPushButton::clicked, this, &GuidePanel::onBack);
    row->addWidget(m_back);

    m_action = new QPushButton("Do it");
    connect(m_action, &QPushButton::clicked, this, &GuidePanel::onAction);
    row->addWidget(m_action, 1);

    m_next = new QPushButton("Next →");
    connect(m_next, &QPushButton::clicked, this, &GuidePanel::onNext);
    row->addWidget(m_next);
    v->addLayout(row);
}

void GuidePanel::setGuide(const Guide& g)
{
    m_guide = g;
    m_index = 0;
    m_actionDone = false;
    render();
}

void GuidePanel::reset()
{
    m_index = 0;
    m_actionDone = false;
    render();
}

void GuidePanel::onNext()
{
    if (m_index + 1 < m_guide.steps.size()) {
        ++m_index;
        m_actionDone = false;
        m_status->clear();
        render();
    }
}

void GuidePanel::onBack()
{
    if (m_index > 0) {
        --m_index;
        m_actionDone = false;
        m_status->clear();
        render();
    }
}

void GuidePanel::onAction()
{
    if (m_index >= m_guide.steps.size()) return;
    const GuideStep& s = m_guide.steps[m_index];
    if (s.command.isEmpty()) return;
    m_action->setEnabled(false);
    m_status->setText("running…");
    emit runCommand(s.command, s.title);
}

void GuidePanel::commandFinished(bool ok, const QString& reply)
{
    m_lastReply = reply;
    m_action->setEnabled(true);
    m_actionDone = ok;
    if (ok) {
        m_status->setText(reply.isEmpty() ? QString("done") : ("done — " + reply));
        m_status->setStyleSheet("color:#7ee787; font-family:Consolas;");
    } else {
        m_status->setText(reply.isEmpty() ? QString("that step did not succeed")
                                          : reply);
        m_status->setStyleSheet("color:#ff7b72; font-family:Consolas;");
    }
    render();
}

void GuidePanel::render()
{
    if (m_guide.steps.isEmpty()) {
        m_heading->setText(m_guide.name);
        m_body->setHtml(m_guide.intro);
        m_progress->setRange(0, 1);
        m_progress->setValue(0);
        return;
    }
    const GuideStep& s = m_guide.steps[m_index];

    m_heading->setText(QString("%1  —  step %2 of %3: %4")
                           .arg(m_guide.name)
                           .arg(m_index + 1)
                           .arg(m_guide.steps.size())
                           .arg(s.title));
    m_progress->setRange(0, m_guide.steps.size());
    m_progress->setValue(m_index + (m_actionDone ? 1 : 0));

    QString html;
    if (m_index == 0 && !m_guide.intro.isEmpty())
        html += m_guide.intro + "<hr>";
    html += s.body;
    if (s.needsFitting)
        html += QString("<p style='color:#ffd866'><b>Before continuing, fit "
                        "%1.</b></p>").arg(s.fitting);
    m_body->setHtml(html);

    m_action->setVisible(!s.action.isEmpty());
    m_action->setText(s.action.isEmpty() ? QString("Do it") : s.action);
    m_back->setEnabled(m_index > 0);

    // A step with an action must be RUN before Next unlocks — a guide that
    // lets you skip past a failed capture is worse than no guide.
    const bool gate = !s.command.isEmpty();
    m_next->setEnabled(m_index + 1 < m_guide.steps.size() &&
                       (!gate || m_actionDone));
    m_next->setText(m_index + 1 < m_guide.steps.size() ? "Next →" : "Finished");
}

} // namespace TciMon
