#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Gantry Axis Controller
// Infrastructure-layer wrapper for Arduino gantry serial protocol.
// Runs on its own thread, driven by external ticks for closed-loop control.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QObject>
#include <QStringList>
#include <QByteArray>
#include <QTimer>
#include <QElapsedTimer>
#include "core/types.h"
#include "infrastructure/gantry/pid_controller.h"
#include "infrastructure/gantry/serial_transport.h"
#include "services/connection_state_machine.h"

class GantryAxisController : public QObject
{
    Q_OBJECT
public:
    // transport == nullptr uses the real QSerialPort-backed transport (or,
    // if Qt6::SerialPort isn't available, a no-op transport that always
    // fails to open). Tests pass a FakeSerialTransport instead — see
    // src/infrastructure/gantry/fake_serial_transport.h.
    explicit GantryAxisController(ISerialTransport* transport = nullptr, QObject* parent = nullptr);
    ~GantryAxisController() override;

    QStringList availablePorts() const;
    bool isConnected() const { return m_connected; }
    bool isHomed() const { return m_isHomed; }
    double currentPositionMm() const { return m_currentPositionMm; }

    void setEncoderCountsPerMm(double countsPerMm);
    double encoderCountsPerMm() const { return m_countsPerMm; }

    // Runtime safety net: tick() clamps any requested target to this range
    // before it ever reaches the closed loop, independent of whatever
    // preflight validation the caller did (or didn't) do upstream.
    void setTravelLimits(const GantryLimits& limits) { m_travelLimits = limits; }
    GantryLimits travelLimits() const { return m_travelLimits; }

    // PID gains for the position closed loop (PWM output, in encoder-mm error
    // units). Defaults below are a conservative starting point carried over
    // from the old P-only controller's KP; KI/KD are new and need tuning
    // against real hardware settling behavior.
    // ASSUMPTION: verify/tune against real gantry hardware.
    void setPidGains(double kp, double ki, double kd) { m_pid.setGains(kp, ki, kd); }

    ConnectionStateMachine::State connectionState() const { return m_stateMachine->state(); }

public slots:
    bool connectPort(const QString& portName);
    void disconnectPort();

    // Commands
    void homeGantry();
    void jogGantry(int speedPwm);
    void stopJog();
    void resetEncoder();

    // Called periodically by GantryService during playback / holding
    void tick(double targetMm);

    // Fallback heartbeat for when playback tick isn't running (e.g. idle)
    void heartbeat();

signals:
    void connected(const QString& portName);
    void disconnected();
    void errorOccurred(const QString& message);
    void positionChanged(double positionMm);
    void homed();

private slots:
    void onReadyRead();
    void onTransportError(const QString& message, bool isFatal);
    void onReconnectRequested();

private:
    void sendCommand(const QString& cmd);
    void handleResponse(const QString& response);
    void processClosedLoop();
    void setMotorPwm(int pwm);

    // Mechanical teardown only (close port, stop timer, reset flags) — used
    // by disconnectPort() (user-initiated), connectPort()'s own pre-cleanup,
    // and the fault path, each of which then decides separately what (if
    // anything) to tell m_stateMachine.
    void teardownConnection();

    ConnectionStateMachine* m_stateMachine = nullptr;
    QString m_lastPortName;

    ISerialTransport* m_transport = nullptr;
    QTimer* m_controlTimer = nullptr;

    bool m_connected = false;
    bool m_isHomed = false;
    double m_countsPerMm = 100.0;

    enum class State {
        Idle,
        Homing,
        Jogging,
        Tracking
    };
    State m_state = State::Idle;

    // Tracking
    double m_currentPositionMm = 0.0;
    double m_targetPositionMm = 0.0;
    long m_lastEncoderCount = 0;
    GantryLimits m_travelLimits;

    // Homing
    int m_homePwm = -100;
    QElapsedTimer m_homingElapsed;
    static constexpr int HOMING_TIMEOUT_MS = 15000;

    // Jogging
    int m_jogPwm = 0;

    // Closed-loop
    int m_currentPwm = 0;
    static constexpr int MAX_PWM = 255;
    static constexpr int MAX_PWM_CHANGE_PER_TICK = 15; // Slower ramp to prevent overshoot, kept as an outer limiter on top of the PID
    gantry::PIDController m_pid{0.8, 0.1, 0.05, -MAX_PWM, MAX_PWM};
    QElapsedTimer m_pidClock;
    bool m_pidClockValid = false;

    // Diagnostic: how many consecutive Tracking ticks the PID has demanded
    // full-effort PWM while still not closing the error — a sign the
    // commanded trajectory (timeline speed/accel) is faster than the gantry
    // can actually track, as opposed to a calibration or serial issue.
    int m_saturatedTicks = 0;
    bool m_saturationLogged = false;
    static constexpr double SATURATION_ERROR_THRESHOLD_MM = 5.0;
    static constexpr int SATURATION_LOG_TICKS = 15; // ~300ms at 20ms/tick

    // Half-duplex request/response tracking for 'e' / 'h' / 'p' queries
    enum class PendingQuery { None, Encoder, Home, Ping };
    PendingQuery m_pendingQuery = PendingQuery::None;
    int m_pendingTicks = 0;
    static constexpr int PENDING_TIMEOUT_TICKS = 10; // 200ms at 20ms tick rate

    QByteArray m_readBuffer;
};
