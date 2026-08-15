#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Teach / Jog Panel
// Connect/Power/Enable buttons, J1-J6 jog, XYZ jog, step size, drag toggle.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QVBoxLayout>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QGroupBox>

class TeachPanel : public QWidget
{
    Q_OBJECT
public:
    explicit TeachPanel(QWidget* parent = nullptr);

    /// Enable/disable jog controls based on connection
    void setRobotConnected(bool connected);
    void setGantryConnected(bool connected);

public slots:
    void updateGantryPosition(double posMm);

signals:
    /// Connect button clicked — wire to ConnectionService::connectToRobot
    void connectRequested();

    /// Power button clicked
    void powerRequested(bool enable);

    /// Enable robot button clicked
    void enableRequested(bool enable);

    /// Jog axis request (e.g. "J1+", "X-", etc.)
    void jogRequested(const QString& axis);

    /// Stop all jogging
    void jogStopRequested();

    /// Drag mode toggle
    void dragModeRequested(bool enable);

    /// Speed factor changed (1-100)
    void speedChanged(int speedPct);

    // Gantry specific signals
    void gantryConnectRequested(const QString& port);
    void gantryDisconnectRequested();
    void gantryJogRequested(int pwm);
    void gantryJogStopRequested();
    void gantryHomeRequested();

    /// "Record Path" toggled — checked=true starts a continuous capture of
    /// hand-jogged gantry motion (see PathRecorderService), false stops it
    /// and commits the simplified result as timeline keyframes.
    void gantryRecordToggled(bool recording);

private:
    void setupUI();
    void createConnectionGroup();
    void createJogGroup();
    void createCartesianJogGroup();
    void createGantryJogGroup();
    void createSettingsGroup(QVBoxLayout* targetLayout);

    QPushButton* createJogButton(const QString& text, const QString& axis);

    // Connection controls
    QGroupBox*    m_connectionGroup = nullptr;
    QPushButton*  m_connectBtn      = nullptr;
    QPushButton*  m_powerBtn        = nullptr;
    QPushButton*  m_enableBtn       = nullptr;
    QPushButton*  m_dragBtn         = nullptr;

    // Jog controls
    QGroupBox*    m_jogGroup        = nullptr;
    QGroupBox*    m_cartesianGroup  = nullptr;

    // Settings
    QComboBox*    m_stepSizeCombo   = nullptr;
    QComboBox*    m_speedCombo      = nullptr;

    // Gantry controls
    QGroupBox*    m_gantryGroup     = nullptr;
    QPushButton*  m_gantryConnectBtn = nullptr;
    QComboBox*    m_gantryPortCombo  = nullptr;
    QPushButton*  m_gantryHomeBtn   = nullptr;
    QLabel*       m_gantryPosLabel  = nullptr;
    QPushButton*  m_gantryJogNeg    = nullptr;
    QPushButton*  m_gantryJogPos    = nullptr;
    QComboBox*    m_gantrySpeedCombo = nullptr;
    QPushButton*  m_gantryRecordBtn = nullptr;
    int           m_gantryPwm       = 200;
    bool          m_gantryConnected = false;

    bool          m_isDragMode      = false;
};
