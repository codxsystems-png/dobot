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
#include <QHBoxLayout>

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
    updateForDriveKind();
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
    spec.driveKind         = static_cast<AxisDriveKind>(m_driveKindCombo->currentData().toInt());
    spec.pulsesPerRev      = m_pulsesPerRevSpin->value();
    spec.stepRateCeilingHz = m_stepCeilingSpin->value();
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

    // Orthogonal to axis type: either drive kind can be linear or rotary.
    m_driveKindCombo = new QComboBox();
    m_driveKindCombo->addItem("DC servo — PWM + encoder, tuned PID",
                              static_cast<int>(AxisDriveKind::DcServoPwm));
    m_driveKindCombo->addItem("Stepper — STEP/DIR into a closed-loop driver",
                              static_cast<int>(AxisDriveKind::StepDirClosedLoop));
    m_driveKindCombo->setCurrentIndex(
        spec.driveKind == AxisDriveKind::StepDirClosedLoop ? 1 : 0);
    axisForm->addRow("Drive:", m_driveKindCombo);
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

    // ─── Stepper Driver (hidden for a DC axis) ────────────────────────────
    m_stepGroup = new QGroupBox("Stepper Driver");
    QFormLayout* stepForm = new QFormLayout(m_stepGroup);

    m_pulsesPerRevSpin = new QDoubleSpinBox();
    m_pulsesPerRevSpin->setRange(1.0, 100000.0);
    m_pulsesPerRevSpin->setDecimals(0);
    m_pulsesPerRevSpin->setSuffix(" pulses/rev");
    m_pulsesPerRevSpin->setValue(spec.pulsesPerRev);
    stepForm->addRow("Driver Microstep Setting:", m_pulsesPerRevSpin);

    m_stepCeilingSpin = new QDoubleSpinBox();
    m_stepCeilingSpin->setRange(100.0, 100000.0);
    m_stepCeilingSpin->setDecimals(0);
    m_stepCeilingSpin->setSuffix(" Hz");
    m_stepCeilingSpin->setValue(spec.stepRateCeilingHz);
    stepForm->addRow("Max Step Rate:", m_stepCeilingSpin);

    m_stepAccelSpin = new QDoubleSpinBox();
    m_stepAccelSpin->setRange(100.0, 1000000.0);
    m_stepAccelSpin->setDecimals(0);
    m_stepAccelSpin->setSuffix(" steps/s2");
    m_stepAccelSpin->setValue(tuning.stepAccelStepsPerSec2);
    stepForm->addRow("Step Acceleration:", m_stepAccelSpin);

    m_idleDisableCheck = new QCheckBox("Release motor torque when idle");
    m_idleDisableCheck->setChecked(tuning.idleDisable);
    stepForm->addRow("", m_idleDisableCheck);

    m_stepHint = new QLabel(
        "Set the max step rate from the bench sweep, not from the motor rating - "
        "it is usually what limits the axis. Leave torque release OFF on anything "
        "gravity-loaded: releasing it lets the axis fall.");
    m_stepHint->setStyleSheet("color: #999; font-size: 10px;");
    m_stepHint->setWordWrap(true);
    stepForm->addRow("", m_stepHint);
    layout->addWidget(m_stepGroup);

    // ─── Calibration ──────────────────────────────────────────────────────
    m_calGroup = new QGroupBox("Encoder Calibration");
    QGroupBox* calGroup = m_calGroup;
    QFormLayout* calForm = new QFormLayout(calGroup);

    m_countsPerUnitSpin = new QDoubleSpinBox();
    m_countsPerUnitSpin->setRange(0.1, 1000000.0);
    m_countsPerUnitSpin->setDecimals(3);
    m_countsPerUnitSpin->setValue(tuning.countsPerUnit);

    // A stepper's scale factor is derivable from the driver settings, unlike
    // an encoder's, which has to be measured.
    m_computeStepsButton = new QPushButton("Compute from driver settings");
    QHBoxLayout* calRow = new QHBoxLayout();
    calRow->addWidget(m_countsPerUnitSpin);
    calRow->addWidget(m_computeStepsButton);
    calForm->addRow("Encoder Counts per Unit:", calRow);

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

    // ─── Motion Tuning (DC only) ──────────────────────────────────────────
    m_tuneGroup = new QGroupBox("Motion Tuning");
    QGroupBox* tuneGroup = m_tuneGroup;
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
    connect(m_driveKindCombo, &QComboBox::currentIndexChanged,
            this, &GantrySetupDialog::updateForDriveKind);
    connect(m_computeStepsButton, &QPushButton::clicked,
            this, &GantrySetupDialog::computeStepsPerUnit);
    for (auto* s : { m_motorRpmSpin, m_gearRatioSpin, m_mmPerRevSpin,
                     m_pulsesPerRevSpin, m_stepCeilingSpin }) {
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

void GantrySetupDialog::updateForDriveKind()
{
    const GantryMotorSpec spec = specFromWidgets();
    const bool stepper = spec.driveKind == AxisDriveKind::StepDirClosedLoop;

    // Whole groups appear and disappear rather than being greyed out. A
    // stepper has no PWM to ramp and no PID to tune, so offering the controls
    // at all — even disabled — implies they mean something here.
    m_stepGroup->setVisible(stepper);
    m_tuneGroup->setVisible(!stepper);
    m_computeStepsButton->setVisible(stepper);

    m_calGroup->setTitle(stepper ? "Step Calibration" : "Encoder Calibration");
    m_calibrationHint->setText(stepper
        ? "Steps per unit of axis travel. Derivable from the driver settings, "
        "but verify it against a measured move: a mismatch of more than 2% "
        "means a wrong microstep DIP or a slipping belt, not a number to "
        "calibrate around."
        : "Encoder counts per unit of axis travel. If this is wrong, playback "
        "overshoots and the motor stalls at full power even though manual "
        "jogging still looks accurate — jogging is open-loop and never uses it.");

    updateUnitLabels();
}

void GantrySetupDialog::computeStepsPerUnit()
{
    const GantryMotorSpec spec = specFromWidgets();
    const double stepsPerUnit = motion::deriveStepsPerUnit(spec);
    if (stepsPerUnit <= 0.0) {
        QMessageBox::warning(this, "Step Calibration",
            "Cannot derive steps per unit from the current spec — check the gear "
        "ratio and travel per output rev are both above zero.");
        return;
    }
    m_countsPerUnitSpin->setValue(stepsPerUnit);
}

void GantrySetupDialog::updateUnitLabels()
{
    GantryMotorSpec spec = specFromWidgets();
    bool rotary  = spec.axisType == GantryAxisType::Rotary;
    bool stepper = spec.driveKind == AxisDriveKind::StepDirClosedLoop;

    // A rotary axis has no leadscrew or pulley — one output revolution is
    // 360 degrees by definition, so the field is meaningless there. Hidden
    // rather than cleared, so switching back to Linear restores the value.
    m_motorForm->setRowVisible(m_mmPerRevSpin, !rotary);

    m_maxAccelSpin->setSuffix(" " + motion::accelLabel(spec));
    if (stepper) m_countsPerUnitSpin->setSuffix(rotary ? " steps/°" : " steps/mm");
    else         m_countsPerUnitSpin->setSuffix(rotary ? " counts/°" : " counts/mm");
    m_travelMinSpin->setSuffix(motion::unitSuffix(spec));
    m_travelMaxSpin->setSuffix(motion::unitSuffix(spec));

    updateDerivedVelocity();
}

void GantrySetupDialog::updateDerivedVelocity()
{
    GantryMotorSpec spec = specFromWidgets();
    double vMax = motion::deriveMaxGantryVelocityUnitsPerSec(spec);

    QString text = QString("→ Max speed: %1 %2")
                       .arg(vMax, 0, 'f', 1).arg(motion::velocityLabel(spec));

    // Say WHICH limit binds. "Why is my axis slow" has two entirely different
    // answers — a smaller motor or a faster board — and the fix differs.
    if (spec.driveKind == AxisDriveKind::StepDirClosedLoop) {
        text += motion::stepCeilingIsBinding(spec)
                    ? "  (limited by step rate, not motor RPM)"
                    : "  (limited by motor RPM)";
    }
    m_derivedVelocityLabel->setText(text);
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
        tuning.stepAccelStepsPerSec2 = m_stepAccelSpin->value();
        tuning.idleDisable           = m_idleDisableCheck->isChecked();
        // Carried through from load — PID gains belong to the tuning dialog.
        tuning.pidKp = m_pidKp;
        tuning.pidKi = m_pidKi;
        tuning.pidKd = m_pidKd;
        tuning.configured = true;
        m_projectService->setGantryTuning(tuning);
    }
    accept();
}
