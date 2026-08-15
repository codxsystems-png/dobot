#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Gantry PID Tuning Dialog
//
// Live PID gain editing with a captured step response to judge it against.
// This dialog deliberately drives real hardware, so every run is gated on
// preconditions (connected, homed, calibrated, playback idle), bounded about
// the middle of travel, and stoppable at any instant by an always-enabled
// Abort. Closing, cancelling, or quitting all abort too.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QDialog>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QVector>
#include "core/types.h"
#include "core/step_response_metrics.h"

class ProjectService;
class PlaybackService;
class GantryAxisController;
class StepResponsePlot;

class GantryTuningDialog : public QDialog
{
    Q_OBJECT
public:
    GantryTuningDialog(ProjectService* projectService,
                       GantryAxisController* controller,
                       PlaybackService* playbackService,
                       QWidget* parent = nullptr);
    ~GantryTuningDialog() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onGainsEdited();          // debounced; pushes to the controller thread
    void pushGainsNow();
    void onRunStepTest();
    void onAbort();
    void onStepTelemetry(double tSec, double setpoint, double measured, int pwm, bool stale);
    void onStepTestFinished();
    void onTuningAborted(const QString& reason);
    void onAccepted();
    void onRejected();

private:
    void setupUI();
    /// Enables/disables the run controls and explains why, via tooltip.
    void refreshPreconditions();
    void updateMetrics();
    /// Abort + restore the gains the dialog opened with. Every exit path.
    void abortAndRestore();

    ProjectService*       m_projectService = nullptr;
    GantryAxisController* m_controller     = nullptr;
    PlaybackService*      m_playbackService = nullptr;

    QDoubleSpinBox* m_kpSpin = nullptr;
    QDoubleSpinBox* m_kiSpin = nullptr;
    QDoubleSpinBox* m_kdSpin = nullptr;
    QDoubleSpinBox* m_stepSizeSpin = nullptr;
    QCheckBox*      m_showPwmCheck = nullptr;
    QPushButton*    m_runBtn   = nullptr;
    QPushButton*    m_clearBtn = nullptr;
    QPushButton*    m_abortBtn = nullptr;
    QLabel*         m_statusLabel = nullptr;

    StepResponsePlot* m_plot = nullptr;
    QLabel* m_overshootLabel = nullptr;
    QLabel* m_settlingLabel  = nullptr;
    QLabel* m_riseLabel      = nullptr;
    QLabel* m_ssErrorLabel   = nullptr;
    QLabel* m_noteLabel      = nullptr;

    /// Coalesces spinbox churn so dragging a gain doesn't flood the control
    /// thread with queued invocations.
    QTimer* m_pushDebounce = nullptr;

    QVector<tuning::StepSample> m_capture;
    bool m_running = false;

    // Gains as they were when the dialog opened, so Cancel can put the
    // hardware back rather than leaving experimental values applied.
    double m_originalKp = 0.0, m_originalKi = 0.0, m_originalKd = 0.0;
};
