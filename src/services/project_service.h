#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Project Service
// Save/load .crp JSON format (version 1.2.0).
// ═══════════════════════════════════════════════════════════════════════════════

#include <QObject>
#include <QString>
#include "core/types.h"

class PointsModel;
class SegmentsModel;
class FizService;
class GantryService;

class ProjectService : public QObject
{
    Q_OBJECT
public:
    explicit ProjectService(PointsModel* pointsModel, QObject* parent = nullptr);

    /// Current project file path (empty if unsaved)
    QString currentFilePath() const { return m_filePath; }
    bool hasUnsavedChanges() const { return m_dirty; }
    QString projectName() const { return m_project.name; }
    const Project& project() const { return m_project; }

public slots:
    /// Late-bind models created after construction (points model didn't exist yet)
    void setModels(PointsModel* pointsModel, SegmentsModel* segmentsModel, FizService* fizService, GantryService* gantryService = nullptr);

    /// New empty project
    void newProject();

    /// Update the gantry's real motor spec (RPM/gear ratio/etc.) used to
    /// auto-compute feasible segment trigger times.
    void setGantryMotorSpec(const GantryMotorSpec& spec);

    /// Update the external axis's closed-loop configuration (encoder
    /// calibration, travel limits, PWM ramp, PID gains).
    void setGantryTuning(const GantryTuning& tuning);

    /// Save to current path (or prompt if new)
    bool saveProject();

    /// Save to specific path
    bool saveProjectAs(const QString& filePath);

    /// Load from file
    bool loadProject(const QString& filePath);

    /// Mark as dirty
    void setDirty(bool dirty = true);

signals:
    void projectLoaded(const QString& name);
    void projectSaved(const QString& path);
    void projectNew();
    void dirtyChanged(bool dirty);
    void errorOccurred(const QString& error);
    void gantryMotorSpecChanged(const GantryMotorSpec& spec);
    void gantryTuningChanged(const GantryTuning& tuning);

private:
    QJsonObject projectToJson() const;
    bool projectFromJson(const QJsonObject& obj);
    QJsonObject pointToJson(const CameraPoint& pt) const;
    CameraPoint pointFromJson(const QJsonObject& obj) const;

    PointsModel*   m_pointsModel = nullptr;
    SegmentsModel* m_segmentsModel = nullptr;
    FizService*    m_fizService = nullptr;
    GantryService* m_gantryService = nullptr;
    Project      m_project;
    QString      m_filePath;
    bool         m_dirty = false;
};
