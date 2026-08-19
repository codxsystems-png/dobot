#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Gantry / External Axis Setup Dialog
// Motor spec (RPM, gear ratio, travel per rev), encoder calibration, travel
// limits and PWM ramp for the external axis. The axis can be a linear gantry
// (mm) or a bare rotary motor (degrees); every unit label follows that choice.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QDialog>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QPushButton>
#include <QSpinBox>
#include <QFormLayout>
#include <QLabel>
#include "core/types.h"

class ProjectService;

class GantrySetupDialog : public QDialog
{
    Q_OBJECT
public:
    explicit GantrySetupDialog(ProjectService* projectService, QWidget* parent = nullptr);

private slots:
    void updateDerivedVelocity();
    void updateUnitLabels();
    void updateRampReadout();
    /// Shows/hides whole groups so the dialog only ever offers controls that
    /// mean something for the selected drive kind.
    void updateForDriveKind();
    /// Fills the calibration field from pulses/rev, gear ratio and pitch.
    void computeStepsPerUnit();
    void onAccepted();

private:
    void setupUI();
    /// Builds a spec from the current widget values (for live derivations).
    GantryMotorSpec specFromWidgets() const;

    ProjectService* m_projectService = nullptr;

    // Axis type and drive kind
    QComboBox*      m_axisTypeCombo  = nullptr;
    QComboBox*      m_driveKindCombo = nullptr;

    // Motor spec
    QDoubleSpinBox* m_motorRpmSpin  = nullptr;
    QDoubleSpinBox* m_gearRatioSpin = nullptr;
    QDoubleSpinBox* m_mmPerRevSpin  = nullptr;
    QDoubleSpinBox* m_maxAccelSpin  = nullptr;
    QLabel*         m_derivedVelocityLabel = nullptr;
    QFormLayout*    m_motorForm     = nullptr;   // for hiding the per-rev row in Rotary

    // Stepper-only spec
    QGroupBox*      m_stepGroup      = nullptr;
    QDoubleSpinBox* m_pulsesPerRevSpin = nullptr;
    QDoubleSpinBox* m_stepCeilingSpin  = nullptr;
    QDoubleSpinBox* m_stepAccelSpin    = nullptr;
    QCheckBox*      m_idleDisableCheck = nullptr;
    QLabel*         m_stepHint         = nullptr;

    // Calibration
    QGroupBox*      m_calGroup          = nullptr;
    QDoubleSpinBox* m_countsPerUnitSpin = nullptr;
    QLabel*         m_calibrationHint   = nullptr;
    QPushButton*    m_computeStepsButton = nullptr;

    // Travel limits
    QDoubleSpinBox* m_travelMinSpin = nullptr;
    QDoubleSpinBox* m_travelMaxSpin = nullptr;

    // Motion tuning (DC only — a stepper has no PWM to ramp)
    QGroupBox*      m_tuneGroup     = nullptr;
    QSpinBox*       m_pwmRampSpin   = nullptr;
    QLabel*         m_rampReadout   = nullptr;

    // PID gains are edited in GantryTuningDialog, not here — carried through
    // untouched so saving this dialog never clobbers them.
    double m_pidKp = 0.8, m_pidKi = 0.1, m_pidKd = 0.05;
};
