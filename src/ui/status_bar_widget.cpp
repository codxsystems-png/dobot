// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Status Bar Widget
// ═══════════════════════════════════════════════════════════════════════════════

#include "ui/status_bar_widget.h"
#include <QVBoxLayout>
#include <QFont>

StatusBarWidget::StatusBarWidget(QWidget* parent)
    : QWidget(parent)
{
    setupUI();

    m_flashTimer = new QTimer(this);
    m_flashTimer->setSingleShot(true);
    connect(m_flashTimer, &QTimer::timeout, this, &StatusBarWidget::clearFlash);
}

void StatusBarWidget::setupUI()
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 1, 4, 1);
    layout->setSpacing(0);

    QFont mono("Consolas", 11);
    mono.setStyleHint(QFont::Monospace);

    auto makeLabel = [&](const QString& text) -> QLabel* {
        QLabel* lbl = new QLabel(text);
        lbl->setFont(mono);
        lbl->setStyleSheet("color: #e0e0e0; padding: 1px 0;");
        layout->addWidget(lbl);
        return lbl;
    };

    m_lineConnection = makeLabel("Disconnected | Mode: --- | Speed: ---%");
    m_lineJoints     = makeLabel("J: ---.- ---.- ---.- ---.- ---.- ---.-");
    m_lineCartesian  = makeLabel("C: X:---.- Y:---.- Z:---.- RX:---.- RY:---.- RZ:---.-");
    m_lineQueue      = makeLabel("Queue: ID=- Pending:0 | CAM: Off");
}

void StatusBarWidget::updateFeedback(const JointAngles& joints,
                                      const CartesianPose& pose,
                                      RobotMode mode,
                                      int speedPct,
                                      int queuePending)
{
    // Row 1: Connection + Mode + Speed
    QString connStr = m_isConnected ? "Connected" : "Disconnected";
    QString modeStr = robotModeString(mode);
    QString color   = modeColor(mode);
    m_lineConnection->setText(
        QString("%1 | Mode: %2 | Speed: %3%")
            .arg(connStr, -13).arg(modeStr, -10).arg(speedPct));
    m_lineConnection->setStyleSheet(QString("color: %1; padding: 1px 0;").arg(color));

    // Row 2: Joint angles
    m_lineJoints->setText(
        QString("J: %1 %2 %3 %4 %5 %6")
            .arg(joints.j[0], 7, 'f', 1)
            .arg(joints.j[1], 7, 'f', 1)
            .arg(joints.j[2], 7, 'f', 1)
            .arg(joints.j[3], 7, 'f', 1)
            .arg(joints.j[4], 7, 'f', 1)
            .arg(joints.j[5], 7, 'f', 1));

    // Row 3: Cartesian pose
    m_lineCartesian->setText(
        QString("C: X:%1 Y:%2 Z:%3 RX:%4 RY:%5 RZ:%6")
            .arg(pose.x, 7, 'f', 1)
            .arg(pose.y, 7, 'f', 1)
            .arg(pose.z, 7, 'f', 1)
            .arg(pose.rx, 7, 'f', 1)
            .arg(pose.ry, 7, 'f', 1)
            .arg(pose.rz, 7, 'f', 1));

    // Row 4: Queue
    m_lineQueue->setText(
        QString("Queue: Pending:%1 | CAM: Off")
            .arg(queuePending));
}

void StatusBarWidget::updateConnectionState(bool connected)
{
    m_isConnected = connected;
    if (!connected) {
        m_lineConnection->setText("Disconnected | Mode: --- | Speed: ---%");
        m_lineConnection->setStyleSheet("color: #cc0000; padding: 1px 0;");
    }
}

void StatusBarWidget::flashEmergency()
{
    setStyleSheet("background-color: #cc0000;");
    m_flashTimer->start(2000);
}

void StatusBarWidget::clearFlash()
{
    setStyleSheet("");
}

QString StatusBarWidget::robotModeString(RobotMode mode) const
{
    switch (mode) {
        case RobotMode::Init:       return "INIT";
        case RobotMode::BrakeOpen:  return "BRAKE";
        case RobotMode::PowerOff:   return "POWER OFF";
        case RobotMode::Disabled:   return "DISABLED";
        case RobotMode::Idle:       return "IDLE";
        case RobotMode::Drag:       return "DRAG";
        case RobotMode::Running:    return "RUNNING";
        case RobotMode::SingleMove: return "SINGLE";
        case RobotMode::Error:      return "ERROR";
        case RobotMode::Pause:      return "PAUSE";
        case RobotMode::Collision:  return "COLLISION";
        default:                    return "UNKNOWN";
    }
}

QString StatusBarWidget::modeColor(RobotMode mode) const
{
    switch (mode) {
        case RobotMode::Idle:       return "#00cc44";
        case RobotMode::Running:    return "#0078d4";
        case RobotMode::Drag:       return "#ffaa00";
        case RobotMode::Error:
        case RobotMode::Collision:  return "#cc0000";
        default:                    return "#e0e0e0";
    }
}
