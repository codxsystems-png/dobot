#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Export Service (Phase 6e)
// Export project to: CSV waypoints, Python script, plain command list.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QObject>
#include <QString>
#include "core/types.h"

class SegmentsModel;
class PointsModel;

class ExportService : public QObject
{
    Q_OBJECT
public:
    enum Format {
        CSV,        // time, point_name, x, y, z, rx, ry, rz, speed, acc
        Python,     // dobot_api compatible script
        CommandList // raw ASCII commands
    };

    explicit ExportService(SegmentsModel* segModel,
                           PointsModel* ptModel,
                           QObject* parent = nullptr);

    /// Export to file in specified format
    bool exportTo(const QString& filePath, Format format);

signals:
    void exportCompleted(const QString& filePath);
    void errorOccurred(const QString& error);

private:
    QString generateCSV() const;
    QString generatePython() const;
    QString generateCommandList() const;

    SegmentsModel* m_segModel = nullptr;
    PointsModel*   m_ptModel  = nullptr;
};
