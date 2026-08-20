#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — External Axis Setup Dialog
//
// Motor spec, step calibration and travel limits for one external axis. Every
// axis is a step/dir channel into a closed-loop drive, so there are no gains
// here and no tuning workflow — the drive closes its own loop.
//
// An axis can be linear (mm) or rotary (degrees); every unit label follows
// that choice.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QDialog>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QPushButton>
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
    void onAxisSelected(int index);
    void updateDerivedVelocity();
    void updateUnitLabels();
    void computeStepsPerUnit();
    void onAddAxis();
    void onRemoveAxis();
    void onAccepted();

private:
    void setupUI();
    void loadAxisIntoWidgets(const AxisConfig& axis);
    /// Reads the widgets back into the axis currently selected, so switching
    /// axes or pressing OK never loses an edit that was only on screen.
    void storeWidgetsIntoCurrentAxis();
    AxisConfig axisFromWidgets() const;
    void refreshAxisCombo();

    ProjectService* m_projectService = nullptr;

    /// Working copy. Edits land here and are only written back on OK, so
    /// Cancel genuinely cancels.
    QList<AxisConfig> m_axes;
    int               m_currentAxis = 0;

    // Axis selection
    QComboBox*   m_axisCombo   = nullptr;
    QPushButton* m_addButton   = nullptr;
    QPushButton* m_removeButton = nullptr;
    QLabel*      m_axisCountHint = nullptr;

    // Identity and wiring
    QComboBox*      m_axisTypeCombo = nullptr;
    QDoubleSpinBox* m_boardIndexSpin = nullptr;

    // Motor spec
    QDoubleSpinBox* m_motorRpmSpin  = nullptr;
    QDoubleSpinBox* m_gearRatioSpin = nullptr;
    QDoubleSpinBox* m_mmPerRevSpin  = nullptr;
    QDoubleSpinBox* m_maxAccelSpin  = nullptr;
    QLabel*         m_derivedVelocityLabel = nullptr;
    QFormLayout*    m_motorForm     = nullptr;

    // Driver
    QDoubleSpinBox* m_pulsesPerRevSpin = nullptr;
    QDoubleSpinBox* m_stepCeilingSpin  = nullptr;
    QDoubleSpinBox* m_stepAccelSpin    = nullptr;
    QCheckBox*      m_idleDisableCheck = nullptr;

    // Calibration
    QDoubleSpinBox* m_countsPerUnitSpin  = nullptr;
    QLabel*         m_calibrationHint    = nullptr;
    QPushButton*    m_computeStepsButton = nullptr;

    // Travel limits
    QDoubleSpinBox* m_travelMinSpin = nullptr;
    QDoubleSpinBox* m_travelMaxSpin = nullptr;
};
