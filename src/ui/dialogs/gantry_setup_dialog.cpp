// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Gantry Motor Setup Dialog
// ═══════════════════════════════════════════════════════════════════════════════

#include "ui/dialogs/gantry_setup_dialog.h"
#include "services/project_service.h"
#include "core/motion_estimator.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QFont>

GantrySetupDialog::GantrySetupDialog(ProjectService* projectService, QWidget* parent)
    : QDialog(parent)
    , m_projectService(projectService)
{
    setWindowTitle("Gantry Motor Setup");
    setMinimumWidth(420);
    setupUI();
    updateDerivedVelocity();
}

void GantrySetupDialog::setupUI()
{
    QVBoxLayout* layout = new QVBoxLayout(this);

    QGroupBox* group = new QGroupBox("Gantry Motor Spec");
    QFormLayout* form = new QFormLayout(group);

    const GantryMotorSpec& spec = m_projectService ? m_projectService->project().gantryMotorSpec
                                                     : GantryMotorSpec();

    m_motorRpmSpin = new QDoubleSpinBox();
    m_motorRpmSpin->setRange(1.0, 100000.0);
    m_motorRpmSpin->setDecimals(1);
    m_motorRpmSpin->setSuffix(" RPM");
    m_motorRpmSpin->setValue(spec.motorRpm);
    form->addRow("Motor RPM:", m_motorRpmSpin);

    m_gearRatioSpin = new QDoubleSpinBox();
    m_gearRatioSpin->setRange(0.01, 1000.0);
    m_gearRatioSpin->setDecimals(3);
    m_gearRatioSpin->setValue(spec.gearRatio);
    form->addRow("Gear Ratio (motor : output, e.g. 10 = 10:1 reducer):", m_gearRatioSpin);

    m_mmPerRevSpin = new QDoubleSpinBox();
    m_mmPerRevSpin->setRange(0.01, 10000.0);
    m_mmPerRevSpin->setDecimals(3);
    m_mmPerRevSpin->setSuffix(" mm");
    m_mmPerRevSpin->setValue(spec.mmPerRev);
    form->addRow("Linear Travel per Output Rev (leadscrew pitch or pulley circumference):", m_mmPerRevSpin);

    m_maxAccelSpin = new QDoubleSpinBox();
    m_maxAccelSpin->setRange(1.0, 100000.0);
    m_maxAccelSpin->setDecimals(1);
    m_maxAccelSpin->setSuffix(" mm/s²");
    m_maxAccelSpin->setValue(spec.maxAccelMmPerSec2);
    form->addRow("Max Acceleration:", m_maxAccelSpin);

    m_derivedVelocityLabel = new QLabel();
    QFont f = m_derivedVelocityLabel->font();
    f.setBold(true);
    m_derivedVelocityLabel->setFont(f);
    form->addRow("", m_derivedVelocityLabel);

    layout->addWidget(group);

    connect(m_motorRpmSpin,  QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &GantrySetupDialog::updateDerivedVelocity);
    connect(m_gearRatioSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &GantrySetupDialog::updateDerivedVelocity);
    connect(m_mmPerRevSpin,  QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &GantrySetupDialog::updateDerivedVelocity);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &GantrySetupDialog::onAccepted);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void GantrySetupDialog::updateDerivedVelocity()
{
    GantryMotorSpec spec;
    spec.motorRpm  = m_motorRpmSpin->value();
    spec.gearRatio = m_gearRatioSpin->value();
    spec.mmPerRev  = m_mmPerRevSpin->value();

    double vMax = motion::deriveMaxGantryVelocityMmPerSec(spec);
    m_derivedVelocityLabel->setText(QString("→ Max linear speed: %1 mm/s").arg(vMax, 0, 'f', 1));
}

void GantrySetupDialog::onAccepted()
{
    if (m_projectService) {
        GantryMotorSpec spec;
        spec.motorRpm          = m_motorRpmSpin->value();
        spec.gearRatio          = m_gearRatioSpin->value();
        spec.mmPerRev           = m_mmPerRevSpin->value();
        spec.maxAccelMmPerSec2  = m_maxAccelSpin->value();
        spec.configured         = true;
        m_projectService->setGantryMotorSpec(spec);
    }
    accept();
}
