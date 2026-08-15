#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Properties Panel
// Edits properties of the currently selected timeline segment.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QWidget>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QGroupBox>
#include <QUndoStack>
#include "core/types.h"

class SegmentsModel;
class PointsModel;

class PropertiesPanel : public QWidget
{
    Q_OBJECT
public:
    explicit PropertiesPanel(SegmentsModel* model, QWidget* parent = nullptr);

    void setUndoStack(QUndoStack* stack) { m_undoStack = stack; }
    void setPointsModel(PointsModel* model);

public slots:
    /// Load properties for selected segment
    void loadSegment(const QString& segId);

    /// Clear (no selection)
    void clearSelection();

signals:
    void segmentModified(const QString& segId);
    void deleteRequested(const QString& segId);
    // Fired when a manually-entered trigger time was below the gantry's
    // physically-minimum feasible time and got clamped up.
    void triggerTimeClamped(double clampedTime, double minTime);

private slots:
    void applyChanges();

private:
    void setupUI();
    void setFieldsEnabled(bool enabled);

    SegmentsModel*   m_model      = nullptr;
    PointsModel*     m_ptModel    = nullptr;
    QUndoStack*      m_undoStack  = nullptr;
    QString          m_currentId;

    QLabel*          m_nameLabel      = nullptr;
    QComboBox*       m_typeCombo      = nullptr;
    QSpinBox*        m_speedSpin      = nullptr;
    QSpinBox*        m_accSpin        = nullptr;
    QCheckBox*       m_enabledCheck   = nullptr;
    QCheckBox*       m_stopAfterCheck = nullptr;
    QCheckBox*       m_pauseAfterCheck= nullptr;
    QDoubleSpinBox*  m_cpSpin         = nullptr;
    QDoubleSpinBox*  m_triggerTimeSpin= nullptr;
    QLabel*          m_triggerTimeMinLabel = nullptr;
    QDoubleSpinBox*  m_preWaitSpin    = nullptr;
    QDoubleSpinBox*  m_postWaitSpin   = nullptr;
    QComboBox*       m_camTriggerCombo= nullptr;
    QComboBox*       m_triggerAtCombo = nullptr;
    QComboBox*       m_viaPointCombo  = nullptr;
    QLabel*          m_viaPointLabel  = nullptr;
    QPushButton*     m_deleteBtn      = nullptr;

    void refreshViaPointChoices();
    void updateViaPointVisibility();

    // Remembered CP blend value (mm) so unchecking "Stop After Move"
    // restores something sensible instead of snapping back to 0.
    double           m_lastBlendMm    = 10.0;

    // Physically-minimum trigger time for the current segment relative to
    // its chronological predecessor, computed in loadSegment() and enforced
    // as a hard floor in applyChanges().
    double           m_minTriggerTime = 0.0;
};
