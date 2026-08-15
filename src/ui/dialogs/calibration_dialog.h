#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Calibration Dialog (Phase 6f)
// SetTool / SetUser coordinate system configuration.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QDialog>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QLabel>
#include <QFormLayout>
#include "core/types.h"

class ConnectionService;

class CalibrationDialog : public QDialog
{
    Q_OBJECT
public:
    explicit CalibrationDialog(ConnectionService* connService,
                               QWidget* parent = nullptr);

private slots:
    void applyTool();
    void applyUser();
    void resetTool();
    void resetUser();

private:
    void setupUI();
    void addAxisRow(QFormLayout* form, const QString& label,
                    QDoubleSpinBox*& spinX, QDoubleSpinBox*& spinY,
                    QDoubleSpinBox*& spinZ, QDoubleSpinBox*& spinRX,
                    QDoubleSpinBox*& spinRY, QDoubleSpinBox*& spinRZ);

    ConnectionService* m_connService = nullptr;

    // Tool coordinate spinboxes
    QDoubleSpinBox* m_toolX  = nullptr;
    QDoubleSpinBox* m_toolY  = nullptr;
    QDoubleSpinBox* m_toolZ  = nullptr;
    QDoubleSpinBox* m_toolRX = nullptr;
    QDoubleSpinBox* m_toolRY = nullptr;
    QDoubleSpinBox* m_toolRZ = nullptr;

    // User coordinate spinboxes
    QDoubleSpinBox* m_userX  = nullptr;
    QDoubleSpinBox* m_userY  = nullptr;
    QDoubleSpinBox* m_userZ  = nullptr;
    QDoubleSpinBox* m_userRX = nullptr;
    QDoubleSpinBox* m_userRY = nullptr;
    QDoubleSpinBox* m_userRZ = nullptr;

    QComboBox* m_toolIndex = nullptr;
    QComboBox* m_userIndex = nullptr;
    QLabel*    m_statusLabel = nullptr;
};
