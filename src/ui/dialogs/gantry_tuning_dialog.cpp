// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Gantry PID Tuning Dialog
// ═══════════════════════════════════════════════════════════════════════════════

#include "ui/dialogs/gantry_tuning_dialog.h"
#include "ui/widgets/step_response_plot.h"
#include "services/project_service.h"
#include "services/playback_service.h"
#include "infrastructure/gantry/gantryaxiscontroller.h"
#include "core/motion_estimator.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QCloseEvent>
#include <QApplication>
#include <QFont>

namespace {
constexpr int kPushDebounceMs = 200;

// Metric thresholds for the colour coding. Overshoot matters most on a
// camera axis — it lands directly in the shot.
constexpr double kOvershootGoodPct = 5.0;
constexpr double kOvershootWarnPct = 15.0;
} // namespace

GantryTuningDialog::GantryTuningDialog(ProjectService* projectService,
                                       GantryAxisController* controller,
                                       PlaybackService* playbackService,
                                       QWidget* parent)
    : QDialog(parent)
    , m_projectService(projectService)
    , m_controller(controller)
    , m_playbackService(playbackService)
{
    setWindowTitle("Gantry PID Tuning");
    setMinimumSize(880, 560);

    if (m_projectService) {
        const GantryTuning& t = m_projectService->project().gantryTuning;
        m_originalKp = t.pidKp;
        m_originalKi = t.pidKi;
        m_originalKd = t.pidKd;
    }

    setupUI();

    if (m_controller) {
        connect(m_controller, &GantryAxisController::stepTelemetry,
                this, &GantryTuningDialog::onStepTelemetry);
        connect(m_controller, &GantryAxisController::stepTestFinished,
                this, &GantryTuningDialog::onStepTestFinished);
        connect(m_controller, &GantryAxisController::tuningAborted,
                this, &GantryTuningDialog::onTuningAborted);
        // Losing the axis mid-run must stop everything, not just log.
        connect(m_controller, &GantryAxisController::disconnected,
                this, [this]() { onTuningAborted("axis disconnected"); });
    }

    // Belt and braces: if the app is quitting, don't leave a motor driving.
    connect(qApp, &QApplication::aboutToQuit, this, [this]() { abortAndRestore(); });

    refreshPreconditions();
}

GantryTuningDialog::~GantryTuningDialog()
{
    // Last line of defence — a destructor reached by any path still stops
    // the axis. Cheap, and the alternative is a motor left running.
    if (m_controller) {
        QMetaObject::invokeMethod(m_controller, [c = m_controller]() {
            c->abortTuning("tuning dialog closed");
        }, Qt::QueuedConnection);
    }
}

void GantryTuningDialog::setupUI()
{
    auto* outer = new QVBoxLayout(this);
    auto* body  = new QHBoxLayout();

    // ─── Left column: controls ────────────────────────────────────────────
    auto* left = new QVBoxLayout();

    auto* gainsGroup = new QGroupBox("PID Gains (applied live)");
    auto* gainsForm  = new QFormLayout(gainsGroup);

    auto makeGainSpin = [](double value) {
        auto* s = new QDoubleSpinBox();
        s->setRange(0.0, 100.0);
        s->setDecimals(4);
        s->setSingleStep(0.01);
        s->setValue(value);
        return s;
    };
    m_kpSpin = makeGainSpin(m_originalKp);
    m_kiSpin = makeGainSpin(m_originalKi);
    m_kdSpin = makeGainSpin(m_originalKd);
    gainsForm->addRow("Kp:", m_kpSpin);
    gainsForm->addRow("Ki:", m_kiSpin);
    gainsForm->addRow("Kd:", m_kdSpin);
    left->addWidget(gainsGroup);

    auto* testGroup = new QGroupBox("Step Test");
    auto* testForm  = new QFormLayout(testGroup);

    QString unitSuffix = " mm";
    if (m_projectService) {
        unitSuffix = motion::unitSuffix(m_projectService->project().gantryMotorSpec);
    }
    m_stepSizeSpin = new QDoubleSpinBox();
    m_stepSizeSpin->setRange(0.1, 200.0);
    m_stepSizeSpin->setDecimals(1);
    m_stepSizeSpin->setValue(10.0);
    m_stepSizeSpin->setSuffix(unitSuffix);
    testForm->addRow("Step size:", m_stepSizeSpin);

    m_showPwmCheck = new QCheckBox("Show PWM trace");
    testForm->addRow("", m_showPwmCheck);

    m_runBtn   = new QPushButton("Run Step Test");
    m_clearBtn = new QPushButton("Clear");
    testForm->addRow(m_runBtn);
    testForm->addRow(m_clearBtn);
    left->addWidget(testGroup);

    m_statusLabel = new QLabel();
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setStyleSheet("color: #999; font-size: 10px;");
    left->addWidget(m_statusLabel);

    left->addStretch();
    body->addLayout(left, 0);

    // ─── Right: plot + metrics ────────────────────────────────────────────
    auto* right = new QVBoxLayout();
    m_plot = new StepResponsePlot();
    if (m_projectService) {
        m_plot->setUnitLabel(motion::unitLabel(m_projectService->project().gantryMotorSpec));
    }
    right->addWidget(m_plot, 1);

    auto* metricsGroup = new QGroupBox("Response");
    auto* metricsGrid  = new QGridLayout(metricsGroup);
    QFont mono("Consolas", 9);

    auto addMetric = [&](int col, const QString& caption, QLabel*& out) {
        auto* cap = new QLabel(caption);
        cap->setStyleSheet("color: #999; font-size: 10px;");
        out = new QLabel("—");
        out->setFont(mono);
        metricsGrid->addWidget(cap, 0, col);
        metricsGrid->addWidget(out, 1, col);
    };
    addMetric(0, "Overshoot",   m_overshootLabel);
    addMetric(1, "Settling",    m_settlingLabel);
    addMetric(2, "Rise",        m_riseLabel);
    addMetric(3, "Steady-state error", m_ssErrorLabel);

    m_noteLabel = new QLabel();
    m_noteLabel->setWordWrap(true);
    m_noteLabel->setStyleSheet("color: #cc8844; font-size: 10px;");
    metricsGrid->addWidget(m_noteLabel, 2, 0, 1, 4);
    right->addWidget(metricsGroup, 0);

    body->addLayout(right, 1);
    outer->addLayout(body, 1);

    // ─── Abort: full width, always enabled, never behind the debounce ─────
    m_abortBtn = new QPushButton("ABORT — STOP AXIS");
    m_abortBtn->setMinimumHeight(38);
    m_abortBtn->setStyleSheet(
        "QPushButton { background: #6a1414; color: #ffdddd; font-weight: bold;"
        " border: 1px solid #aa3333; border-radius: 4px; }"
        "QPushButton:hover { background: #8a1c1c; }");
    outer->addWidget(m_abortBtn);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText("Save Gains");
    outer->addWidget(buttons);

    // ─── Wiring ───────────────────────────────────────────────────────────
    m_pushDebounce = new QTimer(this);
    m_pushDebounce->setSingleShot(true);
    m_pushDebounce->setInterval(kPushDebounceMs);
    connect(m_pushDebounce, &QTimer::timeout, this, &GantryTuningDialog::pushGainsNow);

    for (auto* s : { m_kpSpin, m_kiSpin, m_kdSpin }) {
        connect(s, &QDoubleSpinBox::valueChanged, this, &GantryTuningDialog::onGainsEdited);
    }
    connect(m_showPwmCheck, &QCheckBox::toggled, this, [this](bool on) {
        m_plot->setShowPwm(on);
    });
    connect(m_runBtn,   &QPushButton::clicked, this, &GantryTuningDialog::onRunStepTest);
    connect(m_abortBtn, &QPushButton::clicked, this, &GantryTuningDialog::onAbort);
    connect(m_clearBtn, &QPushButton::clicked, this, [this]() {
        m_capture.clear();
        m_plot->clear();
        updateMetrics();
    });
    connect(buttons, &QDialogButtonBox::accepted, this, &GantryTuningDialog::onAccepted);
    connect(buttons, &QDialogButtonBox::rejected, this, &GantryTuningDialog::onRejected);
}

void GantryTuningDialog::refreshPreconditions()
{
    QString blocker;

    if (!m_controller) {
        blocker = "No gantry controller.";
    } else if (!m_controller->isConnected()) {
        blocker = "Axis is not connected.";
    } else if (!m_controller->isHomed()) {
        blocker = "Axis is not homed. Without a known origin, position and "
                  "travel limits are meaningless.";
    } else if (m_playbackService && m_playbackService->state() != PlaybackService::Stopped) {
        blocker = "Playback is running — it would fight the tuning loop for "
                  "control of the axis.";
    } else if (m_projectService && !m_projectService->project().gantryTuning.configured) {
        blocker = "Set encoder calibration and travel limits first "
                  "(Robot → Gantry / External Axis Setup). Tuning against a "
                  "wrong scale factor is worse than not tuning.";
    }

    const bool ready = blocker.isEmpty() && !m_running;
    m_runBtn->setEnabled(ready);
    m_runBtn->setToolTip(blocker.isEmpty() ? "" : blocker);

    if (m_running) {
        m_statusLabel->setText("Running — the axis is moving. Abort stops it immediately.");
        m_statusLabel->setStyleSheet("color: #ff8844; font-size: 10px;");
    } else if (!blocker.isEmpty()) {
        m_statusLabel->setText(blocker);
        m_statusLabel->setStyleSheet("color: #cc8844; font-size: 10px;");
    } else {
        m_statusLabel->setText("Ready. The test settles at mid-travel, steps, then returns.");
        m_statusLabel->setStyleSheet("color: #999; font-size: 10px;");
    }
}

void GantryTuningDialog::onGainsEdited()
{
    m_pushDebounce->start(); // restarts on each keystroke/drag
}

void GantryTuningDialog::pushGainsNow()
{
    if (!m_controller || !m_projectService) return;

    GantryTuning t = m_projectService->project().gantryTuning;
    t.pidKp = m_kpSpin->value();
    t.pidKi = m_kiSpin->value();
    t.pidKd = m_kdSpin->value();

    QMetaObject::invokeMethod(m_controller, [c = m_controller, t]() {
        c->applyTuning(t);
    }, Qt::QueuedConnection);
}

void GantryTuningDialog::onRunStepTest()
{
    if (!m_controller) return;

    m_capture.clear();
    m_plot->clear();
    m_plot->setSetpoint(0.0);
    updateMetrics();

    m_running = true;
    refreshPreconditions();

    const double step = m_stepSizeSpin->value();
    QMetaObject::invokeMethod(m_controller, [c = m_controller, step]() {
        c->startStepTest(step);
    }, Qt::QueuedConnection);
}

void GantryTuningDialog::onAbort()
{
    if (!m_controller) return;
    // Deliberately not debounced and never disabled — this is the panic path.
    QMetaObject::invokeMethod(m_controller, [c = m_controller]() {
        c->abortTuning("aborted by operator");
    }, Qt::QueuedConnection);
}

void GantryTuningDialog::onStepTelemetry(double tSec, double setpoint,
                                         double measured, int pwm, bool stale)
{
    tuning::StepSample s;
    s.t = tSec;
    s.setpoint = setpoint;
    s.measured = measured;
    s.pwm = pwm;
    s.stale = stale;
    m_capture.append(s);

    m_plot->setSetpoint(setpoint);
    m_plot->setCapturing(true);
    m_plot->setSamples(m_capture);
}

void GantryTuningDialog::onStepTestFinished()
{
    m_running = false;
    m_plot->setCapturing(false);
    refreshPreconditions();
    updateMetrics();
}

void GantryTuningDialog::onTuningAborted(const QString& reason)
{
    m_running = false;
    m_plot->setCapturing(false);
    refreshPreconditions();
    updateMetrics();

    m_statusLabel->setText("Aborted: " + reason);
    m_statusLabel->setStyleSheet("color: #ff6666; font-size: 10px;");
}

void GantryTuningDialog::updateMetrics()
{
    if (m_capture.isEmpty()) {
        for (auto* l : { m_overshootLabel, m_settlingLabel, m_riseLabel, m_ssErrorLabel }) {
            l->setText("—");
            l->setStyleSheet("");
        }
        m_noteLabel->clear();
        return;
    }

    tuning::StepMetrics m = tuning::computeStepMetrics(m_capture);
    if (!m.valid) {
        for (auto* l : { m_overshootLabel, m_settlingLabel, m_riseLabel, m_ssErrorLabel }) {
            l->setText("—");
            l->setStyleSheet("");
        }
        m_noteLabel->setText(m.note);
        return;
    }

    QString unit = m_projectService
        ? motion::unitLabel(m_projectService->project().gantryMotorSpec) : "mm";

    m_overshootLabel->setText(QString("%1 %").arg(m.overshootPercent, 0, 'f', 1));
    QString colour = m.overshootPercent < kOvershootGoodPct ? "#55cc55"
                   : m.overshootPercent < kOvershootWarnPct ? "#ccaa44" : "#ff6666";
    m_overshootLabel->setStyleSheet("color: " + colour + ";");

    m_settlingLabel->setText(QString("%1 s").arg(m.settlingTimeSec, 0, 'f', 2));
    m_riseLabel->setText(QString("%1 s").arg(m.riseTimeSec, 0, 'f', 2));
    m_ssErrorLabel->setText(QString("%1 %2").arg(m.steadyStateError, 0, 'f', 2).arg(unit));

    QString note = m.note;
    if (m.saturatedSamples > 0) {
        QString sat = QString("%1 ticks at full PWM — the axis was at maximum effort")
                          .arg(m.saturatedSamples);
        note = note.isEmpty() ? sat : note + "; " + sat;
    }
    m_noteLabel->setText(note);
}

void GantryTuningDialog::abortAndRestore()
{
    if (!m_controller) return;

    GantryTuning t;
    if (m_projectService) t = m_projectService->project().gantryTuning;
    t.pidKp = m_originalKp;
    t.pidKi = m_originalKi;
    t.pidKd = m_originalKd;

    QMetaObject::invokeMethod(m_controller, [c = m_controller, t]() {
        c->abortTuning("tuning cancelled");
        c->applyTuning(t);   // put the hardware back as we found it
    }, Qt::QueuedConnection);
}

void GantryTuningDialog::onAccepted()
{
    if (m_controller) {
        QMetaObject::invokeMethod(m_controller, [c = m_controller]() {
            c->abortTuning("tuning finished");
        }, Qt::QueuedConnection);
    }
    if (m_projectService) {
        GantryTuning t = m_projectService->project().gantryTuning;
        t.pidKp = m_kpSpin->value();
        t.pidKi = m_kiSpin->value();
        t.pidKd = m_kdSpin->value();
        t.configured = true;
        m_projectService->setGantryTuning(t);   // re-pushes to the controller
    }
    accept();
}

void GantryTuningDialog::onRejected()
{
    abortAndRestore();
    reject();
}

void GantryTuningDialog::closeEvent(QCloseEvent* event)
{
    abortAndRestore();
    QDialog::closeEvent(event);
}
