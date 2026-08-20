// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — External Axis Setup Dialog
// ═══════════════════════════════════════════════════════════════════════════════

#include "ui/dialogs/gantry_setup_dialog.h"
#include "services/project_service.h"
#include "core/motion_estimator.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QScreen>
#include <QGuiApplication>
#include <QFont>

GantrySetupDialog::GantrySetupDialog(ProjectService* projectService, QWidget* parent)
    : QDialog(parent)
    , m_projectService(projectService)
{
    setWindowTitle("External Axis Setup");
    setMinimumWidth(560);

    if (m_projectService) m_axes = m_projectService->project().axes;
    if (m_axes.isEmpty()) {
        // Synthesise the primary axis from the legacy members, so a project
        // that predates the axis list still opens with its real settings
        // rather than defaults.
        AxisConfig primary;
        if (m_projectService) {
            primary.motorSpec = m_projectService->project().gantryMotorSpec;
            primary.tuning    = m_projectService->project().gantryTuning;
        }
        m_axes.append(primary);
    }

    setupUI();
    refreshAxisCombo();
    loadAxisIntoWidgets(m_axes.at(m_currentAxis));
    updateUnitLabels();
}

void GantrySetupDialog::setupUI()
{
    QVBoxLayout* outer = new QVBoxLayout(this);

    // The form outgrows a laptop screen, and a dialog that runs past the
    // screen edge puts OK out of reach. Groups scroll; buttons stay pinned.
    QScrollArea* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    QWidget* content = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);

    // ─── Axis selection ───────────────────────────────────────────────────
    QGroupBox* axisGroup = new QGroupBox("Axis");
    QVBoxLayout* axisBox = new QVBoxLayout(axisGroup);

    QHBoxLayout* pickRow = new QHBoxLayout();
    m_axisCombo    = new QComboBox();
    m_addButton    = new QPushButton("Add Axis");
    m_removeButton = new QPushButton("Remove");
    pickRow->addWidget(m_axisCombo, 1);
    pickRow->addWidget(m_addButton);
    pickRow->addWidget(m_removeButton);
    axisBox->addLayout(pickRow);

    m_axisCountHint = new QLabel();
    m_axisCountHint->setStyleSheet("color: #999; font-size: 10px;");
    m_axisCountHint->setWordWrap(true);
    axisBox->addWidget(m_axisCountHint);

    QFormLayout* idForm = new QFormLayout();
    m_axisTypeCombo = new QComboBox();
    m_axisTypeCombo->addItem("Linear (slider / gantry) — mm",
                             static_cast<int>(GantryAxisType::Linear));
    m_axisTypeCombo->addItem("Rotary (pan / tilt) — degrees",
                             static_cast<int>(GantryAxisType::Rotary));
    idForm->addRow("Motion:", m_axisTypeCombo);

    m_boardIndexSpin = new QDoubleSpinBox();
    m_boardIndexSpin->setRange(0, kMaxAxes - 1);
    m_boardIndexSpin->setDecimals(0);
    idForm->addRow("Board Address:", m_boardIndexSpin);

    QLabel* addrHint = new QLabel(
        "Which STEP/DIR channel on the board this axis is wired to. Two axes "
        "must never share an address — the board routes replies by it, so a "
        "duplicate silently steals the other axis's position and faults.");
    addrHint->setStyleSheet("color: #999; font-size: 10px;");
    addrHint->setWordWrap(true);
    idForm->addRow("", addrHint);
    axisBox->addLayout(idForm);
    layout->addWidget(axisGroup);

    // ─── Motor spec ───────────────────────────────────────────────────────
    QGroupBox* motorGroup = new QGroupBox("Motor");
    m_motorForm = new QFormLayout(motorGroup);

    m_motorRpmSpin = new QDoubleSpinBox();
    m_motorRpmSpin->setRange(1.0, 100000.0);
    m_motorRpmSpin->setDecimals(1);
    m_motorRpmSpin->setSuffix(" RPM");
    m_motorForm->addRow("Motor RPM:", m_motorRpmSpin);

    m_gearRatioSpin = new QDoubleSpinBox();
    m_gearRatioSpin->setRange(0.01, 1000.0);
    m_gearRatioSpin->setDecimals(3);
    m_motorForm->addRow("Gear Ratio (motor : output):", m_gearRatioSpin);

    m_mmPerRevSpin = new QDoubleSpinBox();
    m_mmPerRevSpin->setRange(0.01, 10000.0);
    m_mmPerRevSpin->setDecimals(3);
    m_mmPerRevSpin->setSuffix(" mm");
    m_motorForm->addRow("Travel per Output Rev:", m_mmPerRevSpin);

    m_maxAccelSpin = new QDoubleSpinBox();
    m_maxAccelSpin->setRange(1.0, 100000.0);
    m_maxAccelSpin->setDecimals(1);
    m_motorForm->addRow("Max Acceleration:", m_maxAccelSpin);

    m_derivedVelocityLabel = new QLabel();
    QFont bold = m_derivedVelocityLabel->font();
    bold.setBold(true);
    m_derivedVelocityLabel->setFont(bold);
    m_motorForm->addRow("", m_derivedVelocityLabel);
    layout->addWidget(motorGroup);

    // ─── Driver ───────────────────────────────────────────────────────────
    QGroupBox* driverGroup = new QGroupBox("Stepper Driver");
    QFormLayout* driverForm = new QFormLayout(driverGroup);

    m_pulsesPerRevSpin = new QDoubleSpinBox();
    m_pulsesPerRevSpin->setRange(1.0, 100000.0);
    m_pulsesPerRevSpin->setDecimals(0);
    m_pulsesPerRevSpin->setSuffix(" pulses/rev");
    driverForm->addRow("Microstep Setting:", m_pulsesPerRevSpin);

    m_stepCeilingSpin = new QDoubleSpinBox();
    m_stepCeilingSpin->setRange(100.0, 20000.0);
    m_stepCeilingSpin->setDecimals(0);
    m_stepCeilingSpin->setSuffix(" Hz");
    driverForm->addRow("Max Step Rate:", m_stepCeilingSpin);

    m_stepAccelSpin = new QDoubleSpinBox();
    m_stepAccelSpin->setRange(100.0, 1000000.0);
    m_stepAccelSpin->setDecimals(0);
    m_stepAccelSpin->setSuffix(" steps/s2");
    driverForm->addRow("Step Acceleration:", m_stepAccelSpin);

    m_idleDisableCheck = new QCheckBox("Release motor torque when idle");
    driverForm->addRow("", m_idleDisableCheck);

    QLabel* driverHint = new QLabel(
        "Set the max step rate from a bench sweep watching the SHAFT, not from "
        "the motor's rating — the board counts the pulses it emits, so it "
        "reports every rate as fine even while the motor is losing steps. "
        "Leave torque release OFF on anything gravity-loaded.");
    driverHint->setStyleSheet("color: #999; font-size: 10px;");
    driverHint->setWordWrap(true);
    driverForm->addRow("", driverHint);
    layout->addWidget(driverGroup);

    // ─── Calibration ──────────────────────────────────────────────────────
    QGroupBox* calGroup = new QGroupBox("Step Calibration");
    QFormLayout* calForm = new QFormLayout(calGroup);

    m_countsPerUnitSpin = new QDoubleSpinBox();
    m_countsPerUnitSpin->setRange(0.001, 1000000.0);
    m_countsPerUnitSpin->setDecimals(3);

    m_computeStepsButton = new QPushButton("Compute from driver settings");
    QHBoxLayout* calRow = new QHBoxLayout();
    calRow->addWidget(m_countsPerUnitSpin);
    calRow->addWidget(m_computeStepsButton);
    calForm->addRow("Steps per Unit:", calRow);

    m_calibrationHint = new QLabel(
        "Steps per unit of axis travel. Derivable from the driver settings, but "
        "verify it against a measured move: a mismatch over 2% means a wrong "
        "microstep DIP or a slipping belt, not a number to calibrate around.");
    m_calibrationHint->setStyleSheet("color: #999; font-size: 10px;");
    m_calibrationHint->setWordWrap(true);
    calForm->addRow("", m_calibrationHint);
    layout->addWidget(calGroup);

    // ─── Travel limits ────────────────────────────────────────────────────
    QGroupBox* limitsGroup = new QGroupBox("Travel Limits");
    QFormLayout* limitsForm = new QFormLayout(limitsGroup);

    m_travelMinSpin = new QDoubleSpinBox();
    m_travelMinSpin->setRange(-100000.0, 100000.0);
    m_travelMinSpin->setDecimals(2);
    limitsForm->addRow("Minimum:", m_travelMinSpin);

    m_travelMaxSpin = new QDoubleSpinBox();
    m_travelMaxSpin->setRange(-100000.0, 100000.0);
    m_travelMaxSpin->setDecimals(2);
    limitsForm->addRow("Maximum:", m_travelMaxSpin);
    layout->addWidget(limitsGroup);

    layout->addStretch();
    scroll->setWidget(content);
    outer->addWidget(scroll);

    QDialogButtonBox* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &GantrySetupDialog::onAccepted);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    outer->addWidget(buttons);

    // ─── Live updates ─────────────────────────────────────────────────────
    connect(m_axisCombo, &QComboBox::currentIndexChanged,
            this, &GantrySetupDialog::onAxisSelected);
    connect(m_addButton,    &QPushButton::clicked, this, &GantrySetupDialog::onAddAxis);
    connect(m_removeButton, &QPushButton::clicked, this, &GantrySetupDialog::onRemoveAxis);
    connect(m_computeStepsButton, &QPushButton::clicked,
            this, &GantrySetupDialog::computeStepsPerUnit);
    connect(m_axisTypeCombo, &QComboBox::currentIndexChanged,
            this, &GantrySetupDialog::updateUnitLabels);
    for (auto* sp : { m_motorRpmSpin, m_gearRatioSpin, m_mmPerRevSpin,
                      m_pulsesPerRevSpin, m_stepCeilingSpin }) {
        connect(sp, &QDoubleSpinBox::valueChanged,
                this, &GantrySetupDialog::updateDerivedVelocity);
    }

    int wanted = 720;
    if (QScreen* screen = QGuiApplication::primaryScreen()) {
        wanted = qMin(wanted, static_cast<int>(screen->availableGeometry().height() * 0.85));
    }
    resize(600, wanted);
}

void GantrySetupDialog::refreshAxisCombo()
{
    const QSignalBlocker block(m_axisCombo);
    m_axisCombo->clear();
    for (const AxisConfig& a : m_axes) {
        m_axisCombo->addItem(a.displayName.isEmpty() ? a.id : a.displayName);
    }
    m_axisCombo->setCurrentIndex(m_currentAxis);

    m_addButton->setEnabled(m_axes.size() < kMaxAxes);
    // The first axis is "gantry" and everything not yet migrated to per-axis
    // lookups still refers to it by that id, so it cannot be removed.
    m_removeButton->setEnabled(m_axes.size() > 1 && m_currentAxis > 0);

    m_axisCountHint->setText(
        QString("%1 of %2 axes configured. The first axis is the primary and "
                "cannot be removed.").arg(m_axes.size()).arg(kMaxAxes));
}

void GantrySetupDialog::loadAxisIntoWidgets(const AxisConfig& a)
{
    m_axisTypeCombo->setCurrentIndex(a.motorSpec.axisType == GantryAxisType::Rotary ? 1 : 0);
    m_boardIndexSpin->setValue(a.firmwareAxisIndex);

    m_motorRpmSpin->setValue(a.motorSpec.motorRpm);
    m_gearRatioSpin->setValue(a.motorSpec.gearRatio);
    m_mmPerRevSpin->setValue(a.motorSpec.mmPerRev);
    m_maxAccelSpin->setValue(a.motorSpec.maxAccelMmPerSec2);

    m_pulsesPerRevSpin->setValue(a.motorSpec.pulsesPerRev);
    m_stepCeilingSpin->setValue(a.motorSpec.stepRateCeilingHz);
    m_stepAccelSpin->setValue(a.tuning.stepAccelStepsPerSec2);
    m_idleDisableCheck->setChecked(a.tuning.idleDisable);

    m_countsPerUnitSpin->setValue(a.tuning.countsPerUnit);
    m_travelMinSpin->setValue(a.tuning.travelLimits.minMm);
    m_travelMaxSpin->setValue(a.tuning.travelLimits.maxMm);

    updateUnitLabels();
}

AxisConfig GantrySetupDialog::axisFromWidgets() const
{
    AxisConfig a = m_axes.at(m_currentAxis);   // keeps id, displayName, port

    a.firmwareAxisIndex = static_cast<int>(m_boardIndexSpin->value());

    a.motorSpec.axisType = static_cast<GantryAxisType>(m_axisTypeCombo->currentData().toInt());
    a.motorSpec.motorRpm          = m_motorRpmSpin->value();
    a.motorSpec.gearRatio         = m_gearRatioSpin->value();
    a.motorSpec.mmPerRev          = m_mmPerRevSpin->value();
    a.motorSpec.maxAccelMmPerSec2 = m_maxAccelSpin->value();
    a.motorSpec.pulsesPerRev      = m_pulsesPerRevSpin->value();
    a.motorSpec.stepRateCeilingHz = m_stepCeilingSpin->value();
    a.motorSpec.configured        = true;

    a.tuning.countsPerUnit         = m_countsPerUnitSpin->value();
    a.tuning.travelLimits.minMm    = m_travelMinSpin->value();
    a.tuning.travelLimits.maxMm    = m_travelMaxSpin->value();
    a.tuning.stepAccelStepsPerSec2 = m_stepAccelSpin->value();
    a.tuning.idleDisable           = m_idleDisableCheck->isChecked();
    a.tuning.configured            = true;
    return a;
}

void GantrySetupDialog::storeWidgetsIntoCurrentAxis()
{
    if (m_currentAxis < 0 || m_currentAxis >= m_axes.size()) return;
    m_axes[m_currentAxis] = axisFromWidgets();
}

void GantrySetupDialog::onAxisSelected(int index)
{
    if (index < 0 || index >= m_axes.size()) return;
    storeWidgetsIntoCurrentAxis();      // do not lose the edit being left
    m_currentAxis = index;
    loadAxisIntoWidgets(m_axes.at(index));
    refreshAxisCombo();
}

void GantrySetupDialog::onAddAxis()
{
    if (m_axes.size() >= kMaxAxes) return;
    storeWidgetsIntoCurrentAxis();

    AxisConfig a;
    const int n = m_axes.size();
    a.id          = QString("axis%1").arg(n);
    a.displayName = QString("Axis %1").arg(n + 1);
    a.portName    = m_axes.first().portName;   // same board by default
    // Next free board address, so a new axis never lands on one already taken.
    int idx = 0;
    bool used = true;
    while (used && idx < kMaxAxes) {
        used = false;
        for (const AxisConfig& e : m_axes) if (e.firmwareAxisIndex == idx) used = true;
        if (used) idx++;
    }
    a.firmwareAxisIndex = idx;

    m_axes.append(a);
    m_currentAxis = m_axes.size() - 1;
    refreshAxisCombo();
    loadAxisIntoWidgets(a);
}

void GantrySetupDialog::onRemoveAxis()
{
    if (m_currentAxis <= 0 || m_currentAxis >= m_axes.size()) return;

    const QString name = m_axes.at(m_currentAxis).displayName;
    if (QMessageBox::question(this, "Remove Axis",
            QString("Remove %1?\n\nIts keyframes stay in the project file but "
                    "nothing will drive them.").arg(name))
        != QMessageBox::Yes) {
        return;
    }

    m_axes.removeAt(m_currentAxis);
    m_currentAxis = qMax(0, m_currentAxis - 1);
    refreshAxisCombo();
    loadAxisIntoWidgets(m_axes.at(m_currentAxis));
}

void GantrySetupDialog::updateUnitLabels()
{
    const AxisConfig a = axisFromWidgets();
    const bool rotary = a.motorSpec.axisType == GantryAxisType::Rotary;

    // A rotary axis has no leadscrew or pulley — one output revolution is 360
    // degrees by definition, so the field is meaningless there. Hidden rather
    // than cleared, so switching back restores the value.
    m_motorForm->setRowVisible(m_mmPerRevSpin, !rotary);

    m_maxAccelSpin->setSuffix(" " + motion::accelLabel(a.motorSpec));
    m_countsPerUnitSpin->setSuffix(rotary ? " steps/°" : " steps/mm");
    m_travelMinSpin->setSuffix(motion::unitSuffix(a.motorSpec));
    m_travelMaxSpin->setSuffix(motion::unitSuffix(a.motorSpec));

    updateDerivedVelocity();
}

void GantrySetupDialog::updateDerivedVelocity()
{
    const AxisConfig a = axisFromWidgets();
    const double vMax = motion::deriveMaxGantryVelocityUnitsPerSec(a.motorSpec);

    QString text = QString("→ Max speed: %1 %2")
                       .arg(vMax, 0, 'f', 1).arg(motion::velocityLabel(a.motorSpec));
    // Say WHICH limit binds: "why is my axis slow" has two different answers
    // and two different fixes.
    text += motion::stepCeilingIsBinding(a.motorSpec)
                ? "  (limited by step rate, not motor RPM)"
                : "  (limited by motor RPM)";
    m_derivedVelocityLabel->setText(text);
}

void GantrySetupDialog::computeStepsPerUnit()
{
    const AxisConfig a = axisFromWidgets();
    const double stepsPerUnit = motion::deriveStepsPerUnit(a.motorSpec);
    if (stepsPerUnit <= 0.0) {
        QMessageBox::warning(this, "Step Calibration",
            "Cannot derive steps per unit — check the gear ratio and travel per "
            "output rev are both above zero.");
        return;
    }
    m_countsPerUnitSpin->setValue(stepsPerUnit);
}

void GantrySetupDialog::onAccepted()
{
    storeWidgetsIntoCurrentAxis();

    for (const AxisConfig& a : m_axes) {
        if (a.tuning.travelLimits.maxMm - a.tuning.travelLimits.minMm < 1.0) {
            QMessageBox::warning(this, "Travel Limits",
                QString("%1: maximum travel must be at least 1 unit above minimum.")
                    .arg(a.displayName));
            return;
        }
    }

    // Two axes on one board address would leave the second silently taking
    // over the first's replies, so refuse it here rather than let AxisManager
    // drop an axis the operator thought they had configured.
    for (int i = 0; i < m_axes.size(); ++i) {
        for (int j = i + 1; j < m_axes.size(); ++j) {
            if (m_axes[i].portName == m_axes[j].portName &&
                m_axes[i].firmwareAxisIndex == m_axes[j].firmwareAxisIndex) {
                QMessageBox::warning(this, "Board Address",
                    QString("%1 and %2 are both set to board address %3. Each axis "
                            "on a board needs its own address, or they will steal "
                            "each other's position and faults.")
                        .arg(m_axes[i].displayName, m_axes[j].displayName)
                        .arg(m_axes[i].firmwareAxisIndex));
                return;
            }
        }
    }

    if (m_projectService) m_projectService->setAxes(m_axes);
    accept();
}
