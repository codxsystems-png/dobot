#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Teach Service
// Record point workflow: wait for Idle(5) → grab camera frame → create point.
// GoToPoint: send MovJ to saved position.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QObject>
#include "core/types.h"

class ConnectionService;
class CameraPreviewWidget;
class PointsModel;
class FizService;
class GantryService;
class AxisManager;

class TeachService : public QObject
{
    Q_OBJECT
public:
    /// Lets a taught point record EVERY axis, not only the primary. Optional:
    /// without it points still capture the primary, which is what a
    /// single-axis rig needs.
    void setAxisManager(AxisManager* mgr) { m_axisManager = mgr; }

    explicit TeachService(ConnectionService* connService,
                          CameraPreviewWidget* camera,
                          PointsModel* model,
                          FizService* fizService = nullptr,
                          GantryService* gantryService = nullptr,
                          QObject* parent = nullptr);

public slots:
    /// Record current position as a new point (§8.6: only when Idle)
    void recordPoint(const QString& name = "");

    /// Move robot to a saved point
    void goToPoint(const QString& pointId);

    /// Delete a point by ID
    void deletePoint(const QString& pointId);

    /// Late-bind the model/camera (they're created after TeachService)
    void setModel(PointsModel* model)  { m_model = model; }
    void setCamera(CameraPreviewWidget* cam) { m_camera = cam; }

signals:
    void pointRecorded(const CameraPoint& point);
    void goToStarted(const QString& pointId);
    void goToCompleted(const QString& pointId);
    void errorOccurred(const QString& error);

private:
    ConnectionService*    m_connService = nullptr;
    CameraPreviewWidget*  m_camera      = nullptr;
    PointsModel*          m_model       = nullptr;
    FizService*           m_fizService  = nullptr;
    GantryService*        m_gantryService = nullptr;
    /// Optional. Present, a taught point records every axis; absent, only the
    /// primary — which is all a single-axis rig has.
    AxisManager*          m_axisManager = nullptr;
    int                   m_pointCounter = 0;
};
