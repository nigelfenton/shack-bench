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

const Guide& velocityFactorGuide()
{
    static Guide g = [] {
        Guide guide;
        guide.name = "Measure a cable's velocity factor";
        guide.intro =
            "<p>Velocity factor is how fast a signal travels down the cable, as "
            "a fraction of the speed of light. Solid polyethylene is about "
            "<b>0.66</b>; foam dielectrics are around <b>0.85</b>.</p>"
            "<p><b>Why bother?</b> Because an analyser measures a cable's "
            "<i>electrical</i> length, and converting that into feet needs the "
            "VF. Get it wrong and every derived number is wrong with it.</p>"
            "<p>This shack has been caught by exactly that. A perfectly healthy "
            "DXE-400 run was judged <i>“25–40 % lossier than spec”</i> because "
            "the analysis assumed 0.66. The same measured line is "
            "<b>190.7 ft at VF 0.66</b> but <b>245.6 ft at 0.85</b> — and since "
            "loss is judged <i>per foot</i>, the wrong VF condemned a good "
            "cable. Measuring VF once, from an offcut, removes that whole class "
            "of error.</p>";

        GuideStep s;

        s = {};
        s.title = "You need a known length of the same cable";
        s.body =
            "<p>Use an offcut or a jumper made from the <b>same reel</b> as the "
            "run you care about. A metre or two is ideal.</p>"
            "<p><b>Measure it carefully, and be consistent about where from.</b> "
            "Connector shoulder to connector shoulder is the usual convention "
            "and is what this guide assumes.</p>"
            "<p>⚠ The result is only as good as this measurement. A 10 % error "
            "in the length becomes a <b>10 % error in the velocity factor</b> "
            "— the test cannot do better than your tape measure.</p>";
        guide.steps << s;

        s = {};
        s.title = "SHORT the far end";
        s.body =
            "<p>Connect the cable to <b>CH0</b> and put a <b>short</b> on the "
            "far end — a shorting plug, or braid soldered to centre.</p>"
            "<p><i>Why:</i> a shorted line's reactance passes through zero at "
            "regular intervals, and the spacing of those crossings is set "
            "purely by the electrical length. Nothing else about the cable "
            "matters, which is what makes this measurement robust.</p>"
            "<p>⭐ Those crossings occur every <b>quarter</b> wavelength, not "
            "every half — they alternate between series and parallel resonance. "
            "Assuming half would put the length out by a factor of two.</p>";
        s.needsFitting = true;
        s.fitting = "the cable on CH0 with its far end SHORTED";
        guide.steps << s;

        s = {};
        s.title = "Sweep WIDE — this is the important bit";
        s.body =
            "<p>A short cable resonates high. A 97-inch jumper has its first "
            "crossing around <b>20–26 MHz</b>, so a 3–30 MHz sweep catches only "
            "one — and one crossing cannot be averaged.</p>"
            "<p>Sweep <b>1–300 MHz</b> instead. That same jumper then shows "
            "about <b>14 crossings</b>, and averaging them makes the answer far "
            "less sensitive to any single interpolated point.</p>"
            "<p>⚠ <b>That span is outside your 3–30 MHz calibration.</b> For "
            "this particular measurement that is acceptable — the crossings are "
            "<i>frequencies</i>, and frequency is not affected by calibration "
            "the way magnitude is. But do not read anything into the impedance "
            "values from this sweep.</p>";
        s.action = "Set 1–300 MHz, 201 points";
        s.command = "sweep 1000000 300000000 201";
        s.verify = "sweep";
        s.expect = "1000000 300000000";
        guide.steps << s;

        s = {};
        s.title = "Enter the length, then measure";
        s.body =
            "<p>Type the physical length into the box on the left, then press "
            "<b>Measure VF</b>.</p>"
            "<p>The panel finds every reactance zero-crossing, averages their "
            "spacing, converts that to an electrical length, and divides your "
            "measured length by it.</p>"
            "<p><b>Sanity check the answer.</b> Real coax is 0.66–0.88. "
            "Anything outside that means the length is wrong, the far end is "
            "not shorted, or it is not a plain piece of cable. The panel "
            "refuses such results rather than reporting them.</p>";
        guide.steps << s;

        s = {};
        s.title = "Afterwards — put the range back";
        s.body =
            "<p>The sweep is now 1–300 MHz, which is outside your calibration. "
            "Set it back to the calibrated span before doing any antenna or "
            "feedline work, or those measurements will be interpolated and "
            "unreliable.</p>";
        s.action = "Restore 3–30 MHz, 201 points";
        s.command = "sweep 3000000 30000000 201";
        s.verify = "sweep";
        s.expect = "3000000 30000000";
        guide.steps << s;

        return guide;
    }();
    return g;
}

const Guide& chokeGuide()
{
    static Guide g = [] {
        Guide guide;
        guide.name = "Measure a common-mode choke";
        guide.intro =
            "<p>A common-mode choke's job is to stop RF flowing on the "
            "<b>outside</b> of the coax braid. That current is what turns a "
            "feedline into part of the antenna: RF in the shack, noise pickup, "
            "and a pattern that is not the one you designed.</p>"
            "<p>The wanted signal travels <i>inside</i> the coax as a "
            "differential pair, and a good choke ignores it entirely. So we "
            "must measure the <b>outside</b> of the shield specifically — which "
            "is what the G3TXQ fixture does.</p>"
            "<p>A useful HF choke presents more than about <b>1000 Ω</b> across "
            "the bands you use. Below a few hundred ohms it is not doing much.</p>";

        GuideStep s;

        s = {};
        s.title = "The G3TXQ fixture";
        s.body =
            "<p>Named for Steve Hunt G3TXQ, whose measurements are the standard "
            "amateur reference for this.</p>"
            "<p><img src=\":/img/g3txq-fixture.svg\" width=\"560\"></p>"
            "<p>Three things make it work:</p>"
            "<p><b>1. Each centre pin is shorted to its own pad.</b> This is the "
            "counter-intuitive step — you are deliberately destroying the normal "
            "signal path. Coax has three conductors, not two: the centre, the "
            "<i>inside</i> of the braid, and the <i>outside</i> of the braid. At "
            "RF the skin effect keeps current on the surface, so inside and "
            "outside behave as separate conductors. Shorting centre to shell "
            "removes the inside pair from the measurement, leaving only the "
            "outside — the path a choke exists to block.</p>"
            "<p><b>2. A slot separates the two pads.</b> Without it the board "
            "itself is a short circuit and you measure nothing but copper, "
            "whatever is clipped on. The slot forces every bit of current to "
            "detour through the choke.</p>"
            "<p><b>3. The choke bridges the gap on two short tails.</b> It is "
            "now simply a series element between two 50 Ω ports, which is why "
            "Zcm = 2·Z0·(1/S21 − 1) applies — ordinary two-port maths.</p>"
            "<p>⚠ Keep the tails short. They are in series with the thing you "
            "are measuring, so their own inductance adds to the reading — "
            "flattering the choke, and worst at 10 m where a few inches "
            "matters most.</p>";
        guide.steps << s;

        s = {};
        s.title = "Bound the fixture first — floor and ceiling";
        s.body =
            "<p>Before trusting any choke reading, measure the jig on its own "
            "twice. These two sweeps say what a later number is actually "
            "worth.</p>"
            "<p><b>Strap across the tails — the FLOOR.</b> Shorting the tails "
            "together should read a few ohms. If the empty fixture already "
            "shows several hundred, it cannot honestly report a kilohm with a "
            "choke fitted, and every result will be inflated by that error.</p>"
            "<p><b>Tails open — the CEILING.</b> Stray capacitance across the "
            "slot sets the highest impedance the jig can resolve. Above that "
            "figure you are measuring the fixture, not the choke. A choke "
            "reading close to the ceiling is not a good choke; it is an "
            "exhausted fixture.</p>"
            "<p>Same discipline as the calibration verify step: measure the "
            "known thing first, so you know what the unknown reading means.</p>";
        s.needsFitting = true;
        s.fitting = "a strap across the tails (floor), then nothing (ceiling)";
        guide.steps << s;

        s = {};
        s.title = "Fit the choke";
        s.body =
            "<p>Now put the choke under test across the tails, fixture still "
            "between <b>CH0 and CH1</b>.</p>"
            "<p>If you are comparing several chokes, keep everything else "
            "identical — same tails, same clip positions, same sweep. Anything "
            "that changes between measurements must be the choke, or you are "
            "comparing fixtures.</p>";
        s.needsFitting = true;
        s.fitting = "the choke under test across the two tails";
        guide.steps << s;

        s = {};
        s.title = "Sweep the bands you actually use";
        s.body =
            "<p><b>1–50 MHz</b> covers 160 m through 6 m and shows the whole "
            "shape of the choke's behaviour.</p>"
            "<p>A choke is not flat. It rises with frequency, <b>peaks at its "
            "self-resonance</b>, then falls away above it. Where that peak sits "
            "decides which bands it is good on — which is why the panel reports "
            "<i>per band</i> rather than giving one number.</p>";
        s.action = "Set 1–50 MHz, 201 points";
        s.command = "sweep 1000000 50000000 201";
        s.verify = "sweep";
        s.expect = "1000000 50000000";
        guide.steps << s;

        s = {};
        s.title = "Reading the result";
        s.body =
            "<p><b>Above 1 kΩ</b> on a band — the choke is working there.<br>"
            "<b>A few hundred ohms</b> — marginal.<br>"
            "<b>Tens of ohms</b> — it is not choking anything.</p>"
            "<p>A worked example from this shack: the homebrew coil removed from "
            "the 5-BTV feed was 4–5 turns of coax at 6 inches diameter, roughly "
            "2.5 µH. That computes to about <b>31 Ω on 160 m rising to 450 Ω on "
            "10 m</b> — failing every band. Its replacement, large-diameter "
            "loops of around 20 µH, is 5–10× better and comfortably over the "
            "threshold.</p>"
            "<p>⚠ If the impedance peak sits at the edge of the sweep, the real "
            "self-resonance is outside it. Widen the span to find where the "
            "choke actually works best — the panel says so when it detects it.</p>";
        guide.steps << s;

        s = {};
        s.title = "Afterwards";
        s.body =
            "<p>Put the sweep back to your calibrated 3–30 MHz span before "
            "returning to antenna or feedline work.</p>";
        s.action = "Restore 3–30 MHz, 201 points";
        s.command = "sweep 3000000 30000000 201";
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
