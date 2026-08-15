// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Export Service
// ═══════════════════════════════════════════════════════════════════════════════

#include "services/export_service.h"
#include "models/segments_model.h"
#include "models/points_model.h"
#include "core/command_builder.h"
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QDebug>

ExportService::ExportService(SegmentsModel* segModel,
                             PointsModel* ptModel,
                             QObject* parent)
    : QObject(parent)
    , m_segModel(segModel)
    , m_ptModel(ptModel)
{
}

bool ExportService::exportTo(const QString& filePath, Format format)
{
    QString content;
    switch (format) {
    case CSV:         content = generateCSV();         break;
    case Python:      content = generatePython();      break;
    case CommandList: content = generateCommandList(); break;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit errorOccurred("Cannot write: " + file.errorString());
        return false;
    }

    QTextStream out(&file);
    out << content;
    file.close();

    qDebug() << "ExportService: Exported" << filePath;
    emit exportCompleted(filePath);
    return true;
}

// ─── CSV ──────────────────────────────────────────────────────────────────────

QString ExportService::generateCSV() const
{
    QStringList lines;
    lines << "# CamBot Timeline Export — " + QDateTime::currentDateTime().toString(Qt::ISODate);
    lines << "time_s,point_name,move_type,x_mm,y_mm,z_mm,rx_deg,ry_deg,rz_deg,"
             "j1_deg,j2_deg,j3_deg,j4_deg,j5_deg,j6_deg,speed_pct,acc_pct,cp_mm,"
             "pre_wait_s,post_wait_s,cam_trigger";

    for (int i = 0; i < m_segModel->rowCount(); ++i) {
        TimelineSegment seg = m_segModel->segmentAt(i);
        CameraPoint pt = m_ptModel->pointById(seg.pointId);

        QString typeStr = seg.type == TimelineSegment::MovJ ? "MovJ" :
                          seg.type == TimelineSegment::MovL ? "MovL" : "Arc";
        QString camStr  = seg.camTrigger == TimelineSegment::None        ? "None" :
                          seg.camTrigger == TimelineSegment::StartRecord  ? "StartRecord" :
                          seg.camTrigger == TimelineSegment::StopRecord   ? "StopRecord" : "TakePhoto";

        lines << QString("%1,%2,%3,%4,%5,%6,%7,%8,%9,%10,%11,%12,%13,%14,%15,%16,%17,%18,%19,%20,%21")
                     .arg(seg.triggerTime, 0, 'f', 3)
                     .arg(pt.name)
                     .arg(typeStr)
                     .arg(pt.pose.x,  0, 'f', 3).arg(pt.pose.y,  0, 'f', 3).arg(pt.pose.z,  0, 'f', 3)
                     .arg(pt.pose.rx, 0, 'f', 3).arg(pt.pose.ry, 0, 'f', 3).arg(pt.pose.rz, 0, 'f', 3)
                     .arg(pt.joints.j[0], 0, 'f', 3).arg(pt.joints.j[1], 0, 'f', 3)
                     .arg(pt.joints.j[2], 0, 'f', 3).arg(pt.joints.j[3], 0, 'f', 3)
                     .arg(pt.joints.j[4], 0, 'f', 3).arg(pt.joints.j[5], 0, 'f', 3)
                     .arg(seg.speedPct).arg(seg.accPct).arg(seg.cpValue, 0, 'f', 1)
                     .arg(seg.preWait, 0, 'f', 1).arg(seg.postWait, 0, 'f', 1)
                     .arg(camStr);
    }

    return lines.join('\n') + '\n';
}

// ─── Python ───────────────────────────────────────────────────────────────────

QString ExportService::generatePython() const
{
    QStringList lines;
    lines << "#!/usr/bin/env python3";
    lines << "# CamBot Timeline Export — " + QDateTime::currentDateTime().toString(Qt::ISODate);
    lines << "# Requires: dobot_api (DobotApiDashboard + DobotApiMove)";
    lines << "";
    lines << "from dobot_api import DobotApiDashboard, DobotApiMove";
    lines << "import time";
    lines << "";
    lines << "dashboard = DobotApiDashboard('192.168.1.6', 29999)";
    lines << "move      = DobotApiMove('192.168.1.6', 30003)";
    lines << "";
    lines << "dashboard.EnableRobot()";
    lines << "time.sleep(1.0)";
    lines << "";

    for (int i = 0; i < m_segModel->rowCount(); ++i) {
        TimelineSegment seg = m_segModel->segmentAt(i);
        CameraPoint pt = m_ptModel->pointById(seg.pointId);

        lines << QString("# Segment %1 — %2  @ %3s").arg(i + 1).arg(pt.name)
                        .arg(seg.triggerTime, 0, 'f', 2);

        if (seg.preWait > 0.0)
            lines << QString("time.sleep(%1)").arg(seg.preWait, 0, 'f', 1);

        // Camera trigger at start
        if (seg.camTrigger != TimelineSegment::None &&
            (seg.triggerAt == TimelineSegment::AtStart ||
             seg.triggerAt == TimelineSegment::AtBoth)) {
            if (seg.camTrigger == TimelineSegment::StartRecord)
                lines << "# camera.start_recording()";
            else if (seg.camTrigger == TimelineSegment::TakePhoto)
                lines << "# camera.take_photo()";
        }

        // Motion
        QString cmd;
        if (seg.type == TimelineSegment::MovJ)
            cmd = QString("move.MovJ({%1,%2,%3,%4,%5,%6}, speed=%7, acc=%8)")
                .arg(pt.pose.x,0,'f',3).arg(pt.pose.y,0,'f',3).arg(pt.pose.z,0,'f',3)
                .arg(pt.pose.rx,0,'f',3).arg(pt.pose.ry,0,'f',3).arg(pt.pose.rz,0,'f',3)
                .arg(seg.speedPct).arg(seg.accPct);
        else
            cmd = QString("move.MovL({%1,%2,%3,%4,%5,%6}, speed=%7, acc=%8)")
                .arg(pt.pose.x,0,'f',3).arg(pt.pose.y,0,'f',3).arg(pt.pose.z,0,'f',3)
                .arg(pt.pose.rx,0,'f',3).arg(pt.pose.ry,0,'f',3).arg(pt.pose.rz,0,'f',3)
                .arg(seg.speedPct).arg(seg.accPct);
        lines << cmd;
        lines << "# sync() — wait for motion complete";

        if (seg.postWait > 0.0)
            lines << QString("time.sleep(%1)").arg(seg.postWait, 0, 'f', 1);

        lines << "";
    }

    lines << "dashboard.DisableRobot()";
    lines << "print('Done.')";

    return lines.join('\n') + '\n';
}

// ─── Command List ─────────────────────────────────────────────────────────────

QString ExportService::generateCommandList() const
{
    QStringList lines;
    lines << "# CamBot — Raw Dobot Command List";
    lines << "# " + QDateTime::currentDateTime().toString(Qt::ISODate);
    lines << "# Send each line to port 29999 in order";
    lines << "";

    lines << "EnableRobot();";

    for (int i = 0; i < m_segModel->rowCount(); ++i) {
        TimelineSegment seg = m_segModel->segmentAt(i);
        CameraPoint pt = m_ptModel->pointById(seg.pointId);

        lines << QString("\n# Seg %1: %2 at t=%3s")
                    .arg(i+1).arg(pt.name).arg(seg.triggerTime, 0, 'f', 2);

        // Use CommandBuilder for correct format
        QString cmd;
        if (seg.type == TimelineSegment::MovJ)
            cmd = CommandBuilder::movJ(pt.pose, seg.speedPct, seg.accPct, seg.cpValue);
        else if (seg.type == TimelineSegment::MovL)
            cmd = CommandBuilder::movL(pt.pose, seg.speedPct, seg.accPct, seg.cpValue);
        else
            cmd = "# Arc export not supported";

        lines << cmd;
        lines << "Sync();";
    }

    lines << "\nDisableRobot();";

    return lines.join('\n') + '\n';
}
