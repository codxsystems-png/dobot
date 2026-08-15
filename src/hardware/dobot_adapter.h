#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Dobot Device Adapter
// ═══════════════════════════════════════════════════════════════════════════════

#include "hardware/device_adapter.h"
#include "services/connection_service.h"
#include "core/command_builder.h"
#include "core/types.h"

namespace hardware {

struct DobotMoveTarget {
    TimelineSegment::Type moveType;
    CartesianPose targetPose;
    CartesianPose viaPose; // For Arc
    int speedPct;
    int accPct;
    double cpValue;
    bool pauseAfter = false; // PlaybackEngine pauses once this move completes
};

} // namespace hardware

Q_DECLARE_METATYPE(hardware::DobotMoveTarget)

namespace hardware {

class DobotAdapter : public IDeviceAdapter {
    Q_OBJECT
public:
    explicit DobotAdapter(ConnectionService* service, QObject* parent = nullptr)
        : IDeviceAdapter(parent), m_service(service)
    {
        if (m_service) {
            // ConnectionService::commandCompleted is relayed (queued, cross-thread)
            // from CommandQueueManager's GetCurrentCommandID() polling — this is
            // the real confirmation that the in-flight move actually finished,
            // not a guess based on wall-clock timing.
            connect(m_service, &ConnectionService::commandCompleted, this, [this](int resultId) {
                if (resultId == m_pendingResultId) {
                    m_pendingResultId = -1;
                    emit commandCompleted(resultId);
                }
            });
            // §9.6: a mid-playback connection loss must stop the timeline,
            // not leave it firing moves at a dead socket. PlaybackEngine
            // already E-STOPs on any adapter errorOccurred() (see
            // PlaybackEngine::onAdapterError) — this is what actually wires
            // a real disconnect into that existing safety path.
            connect(m_service, &ConnectionService::disconnected, this, [this]() {
                m_pendingResultId = -1;
                emit errorOccurred("DobotAdapter: connection to robot lost");
            });
        }
    }

    QString deviceName() const override { return "Dobot Nova 5"; }

    bool isReady() const override {
        return m_service && m_service->isConnected()
            && m_pendingResultId < 0
            && m_service->currentMode() == RobotMode::Idle;
    }

    bool isConnected() const override {
        return m_service && m_service->isConnected();
    }

    void enqueueMoveCommand(const QVariant& target, double /*expectedDurationSec*/) override {
        if (!m_service || !m_service->isConnected()) return;

        if (m_pendingResultId >= 0) {
            // Caller should have checked isReady() first — this guards
            // against a second move overwriting/losing track of the
            // ResultID we're still waiting to hear back on.
            emit errorOccurred("DobotAdapter: previous move still in flight — ignoring new command");
            return;
        }

        if (target.canConvert<DobotMoveTarget>()) {
            DobotMoveTarget tgt = target.value<DobotMoveTarget>();

            QString cmd;
            switch (tgt.moveType) {
            case TimelineSegment::MovJ:
                cmd = CommandBuilder::movJ(tgt.targetPose, tgt.speedPct, tgt.accPct, tgt.cpValue);
                break;
            case TimelineSegment::MovL:
                cmd = CommandBuilder::movL(tgt.targetPose, tgt.speedPct, tgt.accPct, tgt.cpValue);
                break;
            case TimelineSegment::Arc:
                cmd = CommandBuilder::arc(tgt.viaPose, tgt.targetPose, tgt.speedPct, tgt.accPct, tgt.cpValue);
                break;
            }

            int resultId = m_service->enqueueMotionCommand(cmd);
            if (resultId < 0) {
                emit errorOccurred("DobotAdapter: failed to enqueue motion command");
                return;
            }
            m_pendingResultId = resultId;
        } else {
            emit errorOccurred("Invalid target format for Dobot");
        }
    }

    void sendStreamedSetpoint(const QVariant& setpoint) override {
        if (!m_service || !m_service->isConnected()) return;

        if (!setpoint.canConvert<DobotMoveTarget>()) {
            emit errorOccurred("DobotAdapter: invalid streamed setpoint payload");
            return;
        }

        // ServoP is fire-and-forget at ~8ms cadence — no ResultID tracking
        // (CommandQueueManager's completion model doesn't apply to a
        // continuous setpoint stream), so this bypasses enqueueMotionCommand
        // entirely. See CommandBuilder::servoP's ASSUMPTION note: whether
        // this needs a dedicated realtime connection instead of the
        // dashboard socket is unconfirmed against real hardware.
        DobotMoveTarget tgt = setpoint.value<DobotMoveTarget>();
        m_service->sendStreamedCommand(CommandBuilder::servoP(tgt.targetPose));
    }

    void stopMotion() override {
        if (m_service) {
            m_service->jogStop(); // Dobot uses jogStop ("MoveJog("")") to halt motion gracefully
        }
    }

    void emergencyStop() override {
        if (m_service) {
            m_service->emergencyStop();
        }
    }

private:
    ConnectionService* m_service;
    int m_pendingResultId = -1;
};

} // namespace hardware
