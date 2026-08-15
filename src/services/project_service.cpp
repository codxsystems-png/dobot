// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Project Service
// ═══════════════════════════════════════════════════════════════════════════════

#include "services/project_service.h"
#include "models/points_model.h"
#include "models/segments_model.h"
#include "application/fizservice.h"
#include "application/gantryservice.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QBuffer>
#include <QDebug>

ProjectService::ProjectService(PointsModel* pointsModel, QObject* parent)
    : QObject(parent)
    , m_pointsModel(pointsModel)
{
    newProject();
}

// ─── New / Save / Load ──────────────────────────────────────────────────────────

void ProjectService::setModels(PointsModel* pointsModel, SegmentsModel* segmentsModel, FizService* fizService, GantryService* gantryService)
{
    m_pointsModel = pointsModel;
    m_segmentsModel = segmentsModel;
    m_fizService = fizService;
    m_gantryService = gantryService;
}

void ProjectService::newProject()
{
    m_project = Project();
    m_project.name    = "Untitled";
    m_project.version = "1.2.0";
    m_project.created = QDateTime::currentDateTime();
    m_filePath.clear();
    m_dirty = false;

    if (m_pointsModel)
        m_pointsModel->clear();
    if (m_segmentsModel)
        m_segmentsModel->clear();
    if (m_fizService)
        m_fizService->clearKeyframes();
    if (m_gantryService)
        m_gantryService->clearKeyframes();

    emit projectNew();
    emit dirtyChanged(false);
}

bool ProjectService::saveProject()
{
    if (m_filePath.isEmpty())
        return false; // Caller should use saveProjectAs
    return saveProjectAs(m_filePath);
}

bool ProjectService::saveProjectAs(const QString& filePath)
{
    // Sync live models/services into m_project before serializing
    if (m_pointsModel)
        m_project.points = m_pointsModel->allPoints();
    if (m_segmentsModel)
        m_project.segments = m_segmentsModel->allSegments();
    if (m_fizService)
        m_project.fizKeyframes = m_fizService->keyframes();
    if (m_gantryService)
        m_project.gantryKeyframes = m_gantryService->keyframes();

    QJsonObject json = projectToJson();
    QJsonDocument doc(json);

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        emit errorOccurred("Cannot write: " + file.errorString());
        return false;
    }

    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    m_filePath = filePath;
    m_dirty = false;
    emit projectSaved(filePath);
    emit dirtyChanged(false);

    qDebug() << "ProjectService: Saved" << filePath;
    return true;
}

bool ProjectService::loadProject(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        emit errorOccurred("Cannot read: " + file.errorString());
        return false;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();

    if (parseError.error != QJsonParseError::NoError) {
        emit errorOccurred("JSON parse error: " + parseError.errorString());
        return false;
    }

    if (!projectFromJson(doc.object())) {
        emit errorOccurred("Invalid project format");
        return false;
    }

    m_filePath = filePath;
    m_dirty = false;

    // Load points/segments/FIZ keyframes into their live models
    if (m_pointsModel)
        m_pointsModel->setPoints(m_project.points);
    if (m_segmentsModel)
        m_segmentsModel->setSegments(m_project.segments);
    if (m_fizService)
        m_fizService->setKeyframes(m_project.fizKeyframes);
    if (m_gantryService)
        m_gantryService->setKeyframes(m_project.gantryKeyframes);

    emit projectLoaded(m_project.name);
    emit dirtyChanged(false);

    qDebug() << "ProjectService: Loaded" << filePath
             << "Points:" << m_project.points.size();
    return true;
}

void ProjectService::setDirty(bool dirty)
{
    if (m_dirty != dirty) {
        m_dirty = dirty;
        emit dirtyChanged(dirty);
    }
}

void ProjectService::setGantryMotorSpec(const GantryMotorSpec& spec)
{
    m_project.gantryMotorSpec = spec;
    setDirty(true);
    emit gantryMotorSpecChanged(spec);
}

// ─── JSON Serialization ─────────────────────────────────────────────────────────

QJsonObject ProjectService::projectToJson() const
{
    QJsonObject obj;
    obj["version"]          = m_project.version;
    obj["robot_type"]       = "dobot_nova5";
    obj["created"]          = m_project.created.toString(Qt::ISODate);
    obj["timelineDuration"] = m_project.timelineDuration;

    // Camera
    QJsonObject cam;
    cam["sourceType"]  = m_project.cameraSourceType;
    cam["sourceUrl"]   = m_project.cameraSourceUrl;
    cam["resolution"]  = m_project.cameraResolution;
    cam["framerate"]   = m_project.cameraFramerate;
    obj["camera"]      = cam;

    // Points
    QJsonArray ptsArray;
    for (const auto& pt : m_project.points)
        ptsArray.append(pointToJson(pt));
    obj["points"] = ptsArray;

    // Segments
    QJsonArray segArray;
    for (const auto& seg : m_project.segments) {
        QJsonObject segObj;
        segObj["id"]            = seg.id;
        segObj["pointId"]       = seg.pointId;
        segObj["triggerTime"]   = seg.triggerTime;
        segObj["type"]          = static_cast<int>(seg.type);
        segObj["speedPct"]      = seg.speedPct;
        segObj["accPct"]        = seg.accPct;
        segObj["cpValue"]       = seg.cpValue;
        segObj["blendRadius"]   = seg.blendRadius;
        segObj["preWait"]       = seg.preWait;
        segObj["postWait"]      = seg.postWait;
        segObj["arcViaPointId"] = seg.arcViaPointId;
        segObj["camTrigger"]    = static_cast<int>(seg.camTrigger);
        segObj["triggerAt"]     = static_cast<int>(seg.triggerAt);
        segObj["enabled"]       = seg.enabled;
        segObj["pauseAfter"]    = seg.pauseAfter;
        segArray.append(segObj);
    }
    obj["segments"] = segArray;

    // FIZ lens mapping — Phase 7
    QJsonObject lens;
    lens["focusNearMm"] = m_project.lensMapping.focusNearMm;
    lens["focusFarMm"]  = m_project.lensMapping.focusFarMm;
    lens["zoomWideMm"]  = m_project.lensMapping.zoomWideMm;
    lens["zoomTeleMm"]  = m_project.lensMapping.zoomTeleMm;
    lens["lensName"]    = m_project.lensMapping.lensName;
    obj["lensMapping"]  = lens;

    // FIZ keyframes — Phase 7
    QJsonArray fizArray;
    for (const auto& kf : m_project.fizKeyframes) {
        QJsonObject kfObj;
        kfObj["id"]   = kf.id;
        kfObj["time"] = kf.time;
        QJsonObject state;
        state["focus"]  = kf.state.focus;
        state["iris"]   = kf.state.iris;
        state["zoom"]   = kf.state.zoom;
        kfObj["state"]  = state;
        kfObj["easing"] = static_cast<int>(kf.easing);
        fizArray.append(kfObj);
    }
    obj["fizKeyframes"] = fizArray;

    // Gantry
    obj["gantryEncoderCountsPerMm"] = m_project.gantryEncoderCountsPerMm;
    QJsonObject motorSpec;
    motorSpec["motorRpm"]          = m_project.gantryMotorSpec.motorRpm;
    motorSpec["gearRatio"]         = m_project.gantryMotorSpec.gearRatio;
    motorSpec["mmPerRev"]          = m_project.gantryMotorSpec.mmPerRev;
    motorSpec["maxAccelMmPerSec2"] = m_project.gantryMotorSpec.maxAccelMmPerSec2;
    motorSpec["configured"]        = m_project.gantryMotorSpec.configured;
    obj["gantryMotorSpec"] = motorSpec;
    QJsonArray gantryArray;
    for (const auto& kf : m_project.gantryKeyframes) {
        QJsonObject kfObj;
        kfObj["id"] = kf.id;
        kfObj["time"] = kf.time;
        kfObj["positionMm"] = kf.positionMm;
        kfObj["easing"] = static_cast<int>(kf.easing);
        gantryArray.append(kfObj);
    }
    obj["gantryKeyframes"] = gantryArray;

    return obj;
}

bool ProjectService::projectFromJson(const QJsonObject& obj)
{
    m_project = Project();

    m_project.version          = obj["version"].toString("1.1.0");
    m_project.created          = QDateTime::fromString(obj["created"].toString(), Qt::ISODate);
    m_project.timelineDuration = obj["timelineDuration"].toDouble(30.0);

    // Camera
    QJsonObject cam = obj["camera"].toObject();
    m_project.cameraSourceType = cam["sourceType"].toString("usb");
    m_project.cameraSourceUrl  = cam["sourceUrl"].toString();
    m_project.cameraResolution = cam["resolution"].toString("1920x1080");
    m_project.cameraFramerate  = cam["framerate"].toInt(30);

    // Points
    QJsonArray ptsArray = obj["points"].toArray();
    for (const auto& v : ptsArray)
        m_project.points.append(pointFromJson(v.toObject()));

    // Segments
    if (obj.contains("segments")) {
        QJsonArray segArray = obj["segments"].toArray();
        for (const auto& v : segArray) {
            QJsonObject segObj = v.toObject();
            TimelineSegment seg;
            seg.id            = segObj["id"].toString();
            seg.pointId       = segObj["pointId"].toString();
            seg.triggerTime   = segObj["triggerTime"].toDouble();
            seg.type          = static_cast<TimelineSegment::Type>(segObj["type"].toInt(TimelineSegment::MovJ));
            seg.speedPct      = segObj["speedPct"].toInt(80);
            seg.accPct        = segObj["accPct"].toInt(50);
            seg.cpValue       = segObj["cpValue"].toDouble();
            seg.blendRadius   = segObj["blendRadius"].toDouble();
            seg.preWait       = segObj["preWait"].toDouble();
            seg.postWait      = segObj["postWait"].toDouble();
            seg.arcViaPointId = segObj["arcViaPointId"].toString();
            seg.camTrigger    = static_cast<TimelineSegment::CamTrigger>(segObj["camTrigger"].toInt(TimelineSegment::None));
            seg.triggerAt     = static_cast<TimelineSegment::TriggerAt>(segObj["triggerAt"].toInt(TimelineSegment::AtStart));
            seg.enabled       = segObj["enabled"].toBool(true);
            seg.pauseAfter    = segObj["pauseAfter"].toBool(false);
            m_project.segments.append(seg);
        }
    }

    // FIZ keyframes
    if (obj.contains("fizKeyframes")) {
        QJsonArray fizArray = obj["fizKeyframes"].toArray();
        for (const auto& v : fizArray) {
            QJsonObject kfObj = v.toObject();
            FizKeyframe kf;
            kf.id   = kfObj["id"].toString();
            kf.time = kfObj["time"].toDouble();
            QJsonObject state = kfObj["state"].toObject();
            kf.state.focus = state["focus"].toDouble(0.0);
            kf.state.iris  = state["iris"].toDouble(0.0);
            kf.state.zoom  = state["zoom"].toDouble(0.0);
            kf.easing = static_cast<FizKeyframe::Easing>(kfObj["easing"].toInt());
            m_project.fizKeyframes.append(kf);
        }
    }

    // Lens mapping (backward-compatible)
    if (obj.contains("lensMapping")) {
        QJsonObject lens = obj["lensMapping"].toObject();
        m_project.lensMapping.focusNearMm = lens["focusNearMm"].toDouble(300.0f);
        m_project.lensMapping.focusFarMm  = lens["focusFarMm"].toDouble(5000.0f);
        m_project.lensMapping.zoomWideMm  = lens["zoomWideMm"].toDouble(24.0f);
        m_project.lensMapping.zoomTeleMm  = lens["zoomTeleMm"].toDouble(85.0f);
        m_project.lensMapping.lensName    = lens["lensName"].toString();
    }

    // Gantry
    m_project.gantryEncoderCountsPerMm = obj["gantryEncoderCountsPerMm"].toDouble(100.0);
    if (obj.contains("gantryMotorSpec")) {
        QJsonObject motorSpec = obj["gantryMotorSpec"].toObject();
        m_project.gantryMotorSpec.motorRpm          = motorSpec["motorRpm"].toDouble(3000.0);
        m_project.gantryMotorSpec.gearRatio         = motorSpec["gearRatio"].toDouble(1.0);
        m_project.gantryMotorSpec.mmPerRev          = motorSpec["mmPerRev"].toDouble(4.0);
        m_project.gantryMotorSpec.maxAccelMmPerSec2 = motorSpec["maxAccelMmPerSec2"].toDouble(400.0);
        m_project.gantryMotorSpec.configured        = motorSpec["configured"].toBool(false);
    } else {
        m_project.gantryMotorSpec = GantryMotorSpec(); // old project file — behave exactly as before
    }
    if (obj.contains("gantryKeyframes")) {
        QJsonArray gantryArray = obj["gantryKeyframes"].toArray();
        for (const auto& v : gantryArray) {
            QJsonObject kfObj = v.toObject();
            GantryKeyframe kf;
            kf.id = kfObj["id"].toString();
            kf.time = kfObj["time"].toDouble();
            kf.positionMm = kfObj["positionMm"].toDouble();
            kf.easing = static_cast<GantryKeyframe::Easing>(kfObj["easing"].toInt());
            m_project.gantryKeyframes.append(kf);
        }
    }

    return true;
}

QJsonObject ProjectService::pointToJson(const CameraPoint& pt) const
{
    QJsonObject obj;
    obj["id"]       = pt.id;
    obj["name"]     = pt.name;
    obj["recorded"] = pt.recorded.toString(Qt::ISODate);

    // Joints
    QJsonArray joints;
    for (int i = 0; i < 6; ++i)
        joints.append(pt.joints.j[i]);
    obj["joints"] = joints;

    // Cartesian
    QJsonArray cart;
    cart.append(pt.pose.x);  cart.append(pt.pose.y);  cart.append(pt.pose.z);
    cart.append(pt.pose.rx); cart.append(pt.pose.ry); cart.append(pt.pose.rz);
    obj["cartesian"] = cart;

    // Thumbnail (base64 JPEG)
    if (!pt.thumbnail.isNull()) {
        QByteArray ba;
        QBuffer buf(&ba);
        buf.open(QIODevice::WriteOnly);
        pt.thumbnail.save(&buf, "JPEG", 85);
        obj["thumbnail"] = QString::fromLatin1(ba.toBase64());
    }

    // FIZ state
    QJsonObject fiz;
    fiz["focus"] = pt.fizState.focus;
    fiz["iris"]  = pt.fizState.iris;
    fiz["zoom"]  = pt.fizState.zoom;
    obj["fizState"] = fiz;

    // Gantry
    obj["gantryPositionMm"] = pt.gantryPositionMm;

    return obj;
}

CameraPoint ProjectService::pointFromJson(const QJsonObject& obj) const
{
    CameraPoint pt;
    pt.id       = obj["id"].toString();
    pt.name     = obj["name"].toString();
    pt.recorded = QDateTime::fromString(obj["recorded"].toString(), Qt::ISODate);

    // Joints
    QJsonArray joints = obj["joints"].toArray();
    for (int i = 0; i < qMin(6, (int)joints.size()); ++i)
        pt.joints.j[i] = joints[i].toDouble();

    // Cartesian
    QJsonArray cart = obj["cartesian"].toArray();
    if (cart.size() >= 6) {
        pt.pose.x  = cart[0].toDouble();
        pt.pose.y  = cart[1].toDouble();
        pt.pose.z  = cart[2].toDouble();
        pt.pose.rx = cart[3].toDouble();
        pt.pose.ry = cart[4].toDouble();
        pt.pose.rz = cart[5].toDouble();
    }

    // Thumbnail
    if (obj.contains("thumbnail")) {
        QByteArray ba = QByteArray::fromBase64(obj["thumbnail"].toString().toLatin1());
        pt.thumbnail.loadFromData(ba, "JPEG");
    }

    // FIZ state (backward-compatible)
    if (obj.contains("fizState")) {
        QJsonObject fiz = obj["fizState"].toObject();
        pt.fizState.focus = fiz["focus"].toDouble(0.0f);
        pt.fizState.iris  = fiz["iris"].toDouble(0.0f);
        pt.fizState.zoom  = fiz["zoom"].toDouble(0.0f);
    }

    // Gantry state
    pt.gantryPositionMm = obj["gantryPositionMm"].toDouble(0.0);

    return pt;
}
