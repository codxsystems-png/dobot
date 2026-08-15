#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Gantry Motor Setup Dialog
// Lets the operator enter the gantry's real motor spec (RPM, gear ratio,
// leadscrew pitch / pulley circumference, max acceleration) so segment
// trigger times can be auto-computed from actual physics instead of guessed.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QDialog>
#include <QDoubleSpinBox>
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
    void onAccepted();

private:
    void setupUI();

    ProjectService* m_projectService = nullptr;

    QDoubleSpinBox* m_motorRpmSpin  = nullptr;
    QDoubleSpinBox* m_gearRatioSpin = nullptr;
    QDoubleSpinBox* m_mmPerRevSpin  = nullptr;
    QDoubleSpinBox* m_maxAccelSpin  = nullptr;
    QLabel*         m_derivedVelocityLabel = nullptr;
};
