// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Gantry / External Axis Setup Dialog
// ═══════════════════════════════════════════════════════════════════════════════

#include "ui/dialogs/gantry_setup_dialog.h"
#include "services/project_service.h"
#include "core/motion_estimator.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QFont>

// The control loop ticks at 50Hz; used to express the PWM ramp limit as a
// wall-clock "time to full power", which is far more legible than PWM/tick.
static constexpr double CONTROL_TICK_MS = 20.0;
static constexpr int    MAX_PWM         = 255;

GantrySetupDialog::GantrySetupDialog(ProjectService* projectService, QWidget* parent)
    : QDialog(parent)
    , m_projectService(projectService)
{
    setWindowTitle("Gantry / External Axis Setup");
    setMinimumWidth(500);
    setupUI();
    updateUnitLabels();
    updateDerivedVelocity();
    updateRampReadout();
}

GantryMotorSpec GantrySetupDialog::specFromWidgets() const
{
    GantryMotorSpec spec;
    spec.motorRpm          = m_motorRpmSpin->value();
    spec.gearRatio         = m_gearRatioSpin->value();
    spec.mmPerRev          = m_mmPerRevSpin->value();
    spec.maxAccelMmPerSec2 = m_maxAccelSpin->value();
    spec.axisType          = static_cast<GantryAxisType>(m_axisTypeCombo->currentData().toInt());
    return spec;
}

void GantrySetupDialog::setupUI()
{
    QVBoxLayout* layout = new QVBoxLayout(this);

    GantryMotorSpec spec;
    GantryTuning    tuning;
    if (m_projectService) {
        spec   = m_projectService->project().gantryMotorSpec;
        tuning = m_projectService->project().gantryTuning;
    }
    m_pidKp = tuning.pidKp;
    m_pidKi = tuning.pidKi;
    m_pidKd = tuning.pidKd;

    // ─── Axis Type ────────────────────────────────────────────────────────
    QGroupBox* axisGroup = new QGroupBox("Axis Type");
    QFormLayout* axisForm = new QFormLayout(axisGroup);
    m_axisTypeCombo = new QComboBox();
    m_axisTypeCombo->addItem("Linear (gantry / slider) — mm",
                             static_cast<int>(GantryAxisType::Linear));
    m_axisTypeCombo->addItem("Rotary (motor only) — degrees",
                             static_cast<int>(GantryAxisType::Rotary));
    m_axisTypeCombo->setCurrentIndex(spec.axisType == GantryAxisType::Rotary ? 1 : 0);
    axisForm->addRow("Axis:", m_axisTypeCombo);
    layout->addWidget(axisGroup);

    // ─── Motor Spec ───────────────────────────────────────────────────────
    QGroupBox* group = new QGroupBox("Motor Spec");
    m_motorForm = new QFormLayout(group);

    m_motorRpmSpin = new QDoubleSpinBox();
    m_motorRpmSpin->setRange(1.0, 100000.0);
    m_motorRpmSpin->setDecimals(1);
    m_motorRpmSpin->setSuffix(" RPM");
    m_motorRpmSpin->setValue(spec.motorRpm);
    m_motorForm->addRow("Motor RPM:", m_motorRpmSpin);

    m_gearRatioSpin = new QDoubleSpinBox();
    m_gearRatioSpin->setRange(0.01, 1000.0);
    m_gearRatioSpin->setDecimals(3);
    m_gearRatioSpin->setValue(spec.gearRatio);
    m_motorForm->addRow("Gear Ratio (motor : output, 10 = 10:1 reducer):", m_gearRatioSpin);

    m_mmPerRevSpin = new QDoubleSpinBox();
    m_mmPerRevSpin->setRange(0.01, 10000.0);
    m_mmPerRevSpin->setDecimals(3);
    m_mmPerRevSpin->setSuffix(" mm");
    m_mmPerRevSpin->setValue(spec.mmPerRev);
    m_motorForm->addRow("Travel per Output Rev (leadscrew pitch / pulley circumference):",
                        m_mmPerRevSpin);

    m_maxAccelSpin = new QDoubleSpinBox();
    m_maxAccelSpin->setRange(1.0, 100000.0);
    m_maxAccelSpin->setDecimals(1);
    m_maxAccelSpin->setValue(spec.maxAccelMmPerSec2);
    m_motorForm->addRow("Max Acceleration:", m_maxAccelSpin);

    m_derivedVelocityLabel = new QLabel();
    QFont bold = m_derivedVelocityLabel->font();
    bold.setBold(true);
    m_derivedVelocityLabel->setFont(bold);
    m_motorForm->addRow("", m_derivedVelocityLabel);
    layout->addWidget(group);

    // ─── Calibration ──────────────────────────────────────────────────────
    QGroupBox* calGroup = new QGroupBox("Encoder Calibration");
    QFormLayout* calForm = new QFormLayout(calGroup);

    m_countsPerUnitSpin = new QDoubleSpinBox();
    m_countsPerUnitSpin->setRange(0.1, 1000000.0);
    m_countsPerUnitSpin->setDecimals(3);
    m_countsPerUnitSpin->setValue(tuning.countsPerUnit);
    calForm->addRow("Encoder Counts per Unit:", m_countsPerUnitSpin);

    m_calibrationHint = new QLabel(
        "Encoder counts per unit of axis travel. If this is wrong, playback\n"
        "overshoots and the motor stalls at full power even though manual\n"
        "jogging still looks accurate — jogging is open-loop and never uses it.");
    m_calibrationHint->setStyleSheet("color: #999; font-size: 10px;");
    m_calibrationHint->setWordWrap(true);
    calForm->addRow("", m_calibrationHint);
    layout->addWidget(calGroup);

    // ─── Travel Limits ────────────────────────────────────────────────────
    QGroupBox* limitsGroup = new QGroupBox("Travel Limits");
    QFormLayout* limitsForm = new QFormLayout(limitsGroup);

    m_travelMinSpin = new QDoubleSpinBox();
    m_travelMinSpin->setRange(-100000.0, 100000.0);
    m_travelMinSpin->setDecimals(2);
    m_travelMinSpin->setValue(tuning.travelLimits.minMm);
    limitsForm->addRow("Minimum:", m_travelMinSpin);

    m_travelMaxSpin = new QDoubleSpinBox();
    m_travelMaxSpin->setRange(-100000.0, 100000.0);
    m_travelMaxSpin->setDecimals(2);
    m_travelMaxSpin->setValue(tuning.travelLimits.maxMm);
    limitsForm->addRow("Maximum:", m_travelMaxSpin);
    layout->addWidget(limitsGroup);

    // ─── Motion Tuning ────────────────────────────────────────────────────
    QGroupBox* tuneGroup = new QGroupBox("Motion Tuning");
    QFormLayout* tuneForm = new QFormLayout(tuneGroup);

    m_pwmRampSpin = new QSpinBox();
    m_pwmRampSpin->setRange(1, MAX_PWM);
    m_pwmRampSpin->setSuffix(" PWM/tick");
    m_pwmRampSpin->setValue(tuning.pwmRampPerTick);
    tuneForm->addRow("PWM Ramp Limit:", m_pwmRampSpin);

    m_rampReadout = new QLabel();
    m_rampReadout->setStyleSheet("color: #999; font-size: 10px;");
    tuneForm->addRow("", m_rampReadout);
    layout->addWidget(tuneGroup);

    // ─── Live updates ─────────────────────────────────────────────────────
    connect(m_axisTypeCombo, &QComboBox::currentIndexChanged,
            this, &GantrySetupDialog::updateUnitLabels);
    for (auto* s : { m_motorRpmSpin, m_gearRatioSpin, m_mmPerRevSpin }) {
        connect(s, &QDoubleSpinBox::valueChanged,
                this, &GantrySetupDialog::updateDerivedVelocity);
    }
    connect(m_pwmRampSpin, &QSpinBox::valueChanged,
            this, &GantrySetupDialog::updateRampReadout);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &GantrySetupDialog::onAccepted);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void GantrySetupDialog::updateUnitLabels()
{
    GantryMotorSpec spec = specFromWidgets();
    bool rotary = spec.axisType == GantryAxisType::Rotary;

    // A rotary axis has no leadscrew or pulley — one output revolution is
    // 360 degrees by definition, so the field is meaningless there. Hidden
    // rather than cleared, so switching back to Linear restores the value.
    m_motorForm->setRowVisible(m_mmPerRevSpin, !rotary);

    m_maxAccelSpin->setSuffix(" " + motion::accelLabel(spec));
    m_countsPerUnitSpin->setSuffix(rotary ? " counts/°" : " counts/mm");
    m_travelMinSpin->setSuffix(motion::unitSuffix(spec));
    m_travelMaxSpin->setSuffix(motion::unitSuffix(spec));

    updateDerivedVelocity();
}

void GantrySetupDialog::updateDerivedVelocity()
{
    GantryMotorSpec spec = specFromWidgets();
    double vMax = motion::deriveMaxGantryVelocityUnitsPerSec(spec);
    m_derivedVelocityLabel->setText(
        QString("→ Max speed: %1 %2").arg(vMax, 0, 'f', 1).arg(motion::velocityLabel(spec)));
}

void GantrySetupDialog::updateRampReadout()
{
    double msToFull = static_cast<double>(MAX_PWM) / m_pwmRampSpin->value() * CONTROL_TICK_MS;
    m_rampReadout->setText(
        QString("→ 0 to full power in %1 ms. Too low and the loop can't keep up "
                "with a fast move; too high and the axis jerks.")
            .arg(msToFull, 0, 'f', 0));
}

void GantrySetupDialog::onAccepted()
{
    if (m_travelMaxSpin->value() - m_travelMinSpin->value() < 1.0) {
        QMessageBox::warning(this, "Travel Limits",
            "Maximum travel must be at least 1 unit above minimum.");
        return;
    }

    if (m_projectService) {
        GantryMotorSpec spec = specFromWidgets();
        spec.configured = true;
        m_projectService->setGantryMotorSpec(spec);

        GantryTuning tuning;
        tuning.countsPerUnit      = m_countsPerUnitSpin->value();
        tuning.travelLimits.minMm = m_travelMinSpin->value();
        tuning.travelLimits.maxMm = m_travelMaxSpin->value();
        tuning.pwmRampPerTick     = m_pwmRampSpin->value();
        // Carried through from load — PID gains belong to the tuning dialog.
        tuning.pidKp = m_pidKp;
        tuning.pidKi = m_pidKi;
        tuning.pidKd = m_pidKd;
        tuning.configured = true;
        m_projectService->setGantryTuning(tuning);
    }
    accept();
}
