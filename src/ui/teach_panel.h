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

    /// Populates the axis selector. The board (port + Connect) is shared by
    /// all of them, which matches the hardware: one Arduino, several axes.
    void setAxes(const QStringList& axisIds, const QStringList& displayNames);
    /// Which axis the jog/home block is currently driving.
    QString selectedAxisId() const;

public slots:
    void updateGantryPosition(double posMm);
    /// Position for a specific axis; ignored unless that axis is selected, so
    /// a background axis's polling cannot overwrite the visible readout.
    void updateAxisPosition(const QString& axisId, double pos);

    /// Unit label for the external axis readout ("mm" or "deg"). Pushed by
    /// MainWindow when the axis type changes — TeachPanel has no
    /// ProjectService of its own.
    void setAxisUnitLabel(const QString& label);

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

    /// Axis-qualified equivalents, carrying which axis the operator selected.
    /// The unqualified ones above still fire for the primary axis so existing
    /// wiring keeps working.
    void axisJogRequested(const QString& axisId, int speed);
    void axisJogStopRequested(const QString& axisId);
    void axisHomeRequested(const QString& axisId);
    void axisSelectionChanged(const QString& axisId);

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
    QComboBox*    m_axisSelectCombo  = nullptr;
    QStringList   m_axisIds;
    QPushButton*  m_gantryHomeBtn   = nullptr;
    QLabel*       m_gantryPosLabel  = nullptr;
    QString       m_axisUnitLabel   = "mm";
    double        m_lastGantryPos   = 0.0;
    QPushButton*  m_gantryJogNeg    = nullptr;
    QPushButton*  m_gantryJogPos    = nullptr;
    QComboBox*    m_gantrySpeedCombo = nullptr;
    QPushButton*  m_gantryRecordBtn = nullptr;
    int           m_gantryPwm       = 200;
    bool          m_gantryConnected = false;

    bool          m_isDragMode      = false;
};
