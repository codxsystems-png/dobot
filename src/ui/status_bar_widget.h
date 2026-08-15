#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Status Bar Widget
// Monospace 11pt display: connection/mode/speed, joints, cartesian, queue/cam.
// Updates at 60Hz from feedback signals.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QWidget>
#include <QLabel>
#include <QTimer>
#include "core/types.h"
#include "core/feedback_parser.h"

class StatusBarWidget : public QWidget
{
    Q_OBJECT
public:
    explicit StatusBarWidget(QWidget* parent = nullptr);

public slots:
    /// Update from RealtimeFeedbackWorker (called via ConnectionService)
    void updateFeedback(const JointAngles& joints,
                        const CartesianPose& pose,
                        RobotMode mode,
                        int speedPct,
                        int queuePending);

    /// Update connection state display
    void updateConnectionState(bool connected);

    /// Flash red for E-STOP
    void flashEmergency();

    /// Update FIZ state (Phase 7)
    // void updateFizState(const FizState& fiz, bool fizConnected);

private slots:
    void clearFlash();

private:
    void setupUI();
    QString robotModeString(RobotMode mode) const;
    QString modeColor(RobotMode mode) const;

    QLabel* m_lineConnection = nullptr;  // Row 1: Connected | Mode: IDLE | Speed: 80%
    QLabel* m_lineJoints     = nullptr;  // Row 2: J: 000.0 045.0 -030.0 ...
    QLabel* m_lineCartesian  = nullptr;  // Row 3: C: X:500.0 Y:0.0 Z:300.0 ...
    QLabel* m_lineQueue      = nullptr;  // Row 4: Queue: ID=0 Pending:0 | CAM: ...
    // QLabel* m_lineFiz      = nullptr;  // Row 5: FIZ (Phase 7)

    QTimer* m_flashTimer     = nullptr;
    bool    m_isConnected    = false;
};
