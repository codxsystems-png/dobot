// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Teach Service
// ═══════════════════════════════════════════════════════════════════════════════

#include "services/teach_service.h"
#include "services/connection_service.h"
#include "ui/camera_preview_widget.h"
#include "models/points_model.h"
#include "application/fizservice.h"
#include "application/gantryservice.h"
#include "core/command_builder.h"
#include <QUuid>
#include <QDateTime>
#include <QDebug>
#include <memory>

TeachService::TeachService(ConnectionService* connService,
                           CameraPreviewWidget* camera,
                           PointsModel* model,
                           FizService* fizService,
                           GantryService* gantryService,
                           QObject* parent)
    : QObject(parent)
    , m_connService(connService)
    , m_camera(camera)
    , m_model(model)
    , m_fizService(fizService)
    , m_gantryService(gantryService)
{
}

void TeachService::recordPoint(const QString& name)
{
    // Note: Recording allowed even without robot connection
    // (gantry/FIZ-only recording is valid; robot pose stays at defaults)

    // §8.6: Thumbnail ONLY when robot is Idle
    // The ConnectionService caches the latest feedback — we trust that
    // the caller has verified Idle state before requesting record.

    CameraPoint pt;
    pt.id       = QUuid::createUuid().toString(QUuid::WithoutBraces);
    pt.name     = name.isEmpty()
                    ? QString("Point %1").arg(++m_pointCounter)
                    : name;
    pt.recorded = QDateTime::currentDateTime();

    // Grab thumbnail from camera
    if (m_camera && m_camera->isActive()) {
        pt.thumbnail = m_camera->grabThumbnail();
    }
    // If no camera, thumbnail stays null — that's acceptable

    if (m_connService) {
        pt.joints = m_connService->currentJoints();
        pt.pose   = m_connService->currentPose();
    }


    if (m_fizService) {
        pt.fizState = m_fizService->currentState();
    }
    
    if (m_gantryService) {
        pt.gantryPositionMm = m_gantryService->currentPositionMm();
    }

    // Add to model
    if (!m_model) {
        qWarning() << "TeachService: No PointsModel set — cannot store point!";
        emit errorOccurred("Internal error: PointsModel not initialized");
        return;
    }
    m_model->addPoint(pt);

    qDebug() << "TeachService: Recorded" << pt.name << pt.id;
    emit pointRecorded(pt);
}

void TeachService::goToPoint(const QString& pointId)
{
    CameraPoint pt = m_model->pointById(pointId);
    if (pt.id.isEmpty()) {
        emit errorOccurred("Point not found: " + pointId);
        return;
    }

    if (!m_connService || !m_connService->isConnected()) {
        emit errorOccurred("Robot not connected");
        return;
    }

    QString cmd = CommandBuilder::movJ(pt.pose, 80, 50, 0.0);
    int resultId = m_connService->enqueueMotionCommand(cmd);
    if (resultId < 0) {
        emit errorOccurred("Failed to send GoTo command for: " + pt.name);
        return;
    }

    qDebug() << "TeachService: GoTo" << pt.name << "resultId" << resultId;
    emit goToStarted(pointId);

    // Wait for this exact ResultID to be confirmed complete (§8.5) before
    // reporting done — a shared Connection handle lets the lambda
    // disconnect itself once its own resultId fires.
    auto conn = std::make_shared<QMetaObject::Connection>();
    *conn = connect(m_connService, &ConnectionService::commandCompleted, this,
                     [this, pointId, resultId, conn](int completedId) {
        if (completedId != resultId) return;
        disconnect(*conn);
        emit goToCompleted(pointId);
    });
}

void TeachService::deletePoint(const QString& pointId)
{
    int idx = m_model->indexOf(pointId);
    if (idx >= 0) {
        m_model->removePoint(idx);
        qDebug() << "TeachService: Deleted point" << pointId;
    }
}
