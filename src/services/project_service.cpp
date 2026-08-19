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

namespace {

/// Serialises one axis. Same shape as the legacy flat keys, just nested, so
/// the two representations can be compared by eye when something looks wrong.
QJsonObject axisToJson(const AxisConfig& a)
{
    QJsonObject o;
    o["id"]                = a.id;
    o["displayName"]       = a.displayName;
    o["portName"]          = a.portName;
    o["firmwareAxisIndex"] = a.firmwareAxisIndex;

    QJsonObject spec;
    spec["motorRpm"]          = a.motorSpec.motorRpm;
    spec["gearRatio"]         = a.motorSpec.gearRatio;
    spec["mmPerRev"]          = a.motorSpec.mmPerRev;
    spec["maxAccelMmPerSec2"] = a.motorSpec.maxAccelMmPerSec2;
    spec["configured"]        = a.motorSpec.configured;
    spec["axisType"]          = static_cast<int>(a.motorSpec.axisType);
    spec["driveKind"]         = static_cast<int>(a.motorSpec.driveKind);
    spec["pulsesPerRev"]      = a.motorSpec.pulsesPerRev;
    spec["stepRateCeilingHz"] = a.motorSpec.stepRateCeilingHz;
    o["motorSpec"] = spec;

    QJsonObject t;
    t["countsPerUnit"]         = a.tuning.countsPerUnit;
    t["travelMin"]             = a.tuning.travelLimits.minMm;
    t["travelMax"]             = a.tuning.travelLimits.maxMm;
    t["pwmRampPerTick"]        = a.tuning.pwmRampPerTick;
    t["pidKp"]                 = a.tuning.pidKp;
    t["pidKi"]                 = a.tuning.pidKi;
    t["pidKd"]                 = a.tuning.pidKd;
    t["configured"]            = a.tuning.configured;
    t["stepAccelStepsPerSec2"] = a.tuning.stepAccelStepsPerSec2;
    t["idleDisable"]           = a.tuning.idleDisable;
    o["tuning"] = t;
    return o;
}

AxisConfig axisFromJson(const QJsonObject& o)
{
    AxisConfig a;
    a.id                = o["id"].toString("gantry");
    a.displayName       = o["displayName"].toString("Gantry");
    a.portName          = o["portName"].toString();
    a.firmwareAxisIndex = o["firmwareAxisIndex"].toInt(0);

    const QJsonObject spec = o["motorSpec"].toObject();
    a.motorSpec.motorRpm          = spec["motorRpm"].toDouble(3000.0);
    a.motorSpec.gearRatio         = spec["gearRatio"].toDouble(1.0);
    a.motorSpec.mmPerRev          = spec["mmPerRev"].toDouble(4.0);
    a.motorSpec.maxAccelMmPerSec2 = spec["maxAccelMmPerSec2"].toDouble(400.0);
    a.motorSpec.configured        = spec["configured"].toBool(false);
    a.motorSpec.axisType          = static_cast<GantryAxisType>(spec["axisType"].toInt(0));
    a.motorSpec.driveKind         = static_cast<AxisDriveKind>(spec["driveKind"].toInt(0));
    a.motorSpec.pulsesPerRev      = spec["pulsesPerRev"].toDouble(1600.0);
    a.motorSpec.stepRateCeilingHz = spec["stepRateCeilingHz"].toDouble(3500.0);

    const QJsonObject t = o["tuning"].toObject();
    a.tuning.countsPerUnit         = t["countsPerUnit"].toDouble(100.0);
    a.tuning.travelLimits.minMm    = t["travelMin"].toDouble(0.0);
    a.tuning.travelLimits.maxMm    = t["travelMax"].toDouble(1000.0);
    a.tuning.pwmRampPerTick        = t["pwmRampPerTick"].toInt(15);
    a.tuning.pidKp                 = t["pidKp"].toDouble(0.8);
    a.tuning.pidKi                 = t["pidKi"].toDouble(0.1);
    a.tuning.pidKd                 = t["pidKd"].toDouble(0.05);
    a.tuning.configured            = t["configured"].toBool(false);
    a.tuning.stepAccelStepsPerSec2 = t["stepAccelStepsPerSec2"].toDouble(40000.0);
    a.tuning.idleDisable           = t["idleDisable"].toBool(false);
    return a;
}

} // namespace

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

void ProjectService::setAxes(const QList<AxisConfig>& axes)
{
    m_project.axes = axes;
    // Keep the legacy members in step immediately, not only at save time —
    // consumers read them between now and the next write.
    if (!m_project.axes.isEmpty()) {
        const AxisConfig& primary = m_project.axes.first();
        m_project.gantryMotorSpec = primary.motorSpec;
        m_project.gantryTuning    = primary.tuning;
        m_project.gantryEncoderCountsPerMm = primary.tuning.countsPerUnit;
    }
    setDirty(true);
    emit axesChanged(m_project.axes);
}

void ProjectService::setAxisKeyframes(const QString& axisId, const QList<GantryKeyframe>& kfs)
{
    if (axisId == "gantry") {
        m_project.gantryKeyframes = kfs;
    } else {
        m_project.axisKeyframes.insert(axisId, kfs);
    }
    setDirty(true);
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

void ProjectService::setGantryTuning(const GantryTuning& tuning)
{
    m_project.gantryTuning = tuning;
    // Keep the legacy mirror in step so a save right now stays consistent.
    m_project.gantryEncoderCountsPerMm = tuning.countsPerUnit;
    setDirty(true);
    emit gantryTuningChanged(tuning);
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
    // Legacy key is mirrored from the live tuning value so an older build
    // reading this file still sees a sane counts/mm rather than a stale one.
    obj["gantryEncoderCountsPerMm"] = m_project.gantryTuning.countsPerUnit;
    QJsonObject motorSpec;
    motorSpec["motorRpm"]          = m_project.gantryMotorSpec.motorRpm;
    motorSpec["gearRatio"]         = m_project.gantryMotorSpec.gearRatio;
    motorSpec["mmPerRev"]          = m_project.gantryMotorSpec.mmPerRev;
    motorSpec["maxAccelMmPerSec2"] = m_project.gantryMotorSpec.maxAccelMmPerSec2;
    motorSpec["configured"]        = m_project.gantryMotorSpec.configured;
    motorSpec["axisType"]          = static_cast<int>(m_project.gantryMotorSpec.axisType);
    motorSpec["driveKind"]         = static_cast<int>(m_project.gantryMotorSpec.driveKind);
    motorSpec["pulsesPerRev"]      = m_project.gantryMotorSpec.pulsesPerRev;
    motorSpec["stepRateCeilingHz"] = m_project.gantryMotorSpec.stepRateCeilingHz;
    obj["gantryMotorSpec"] = motorSpec;

    QJsonObject tuning;
    tuning["countsPerUnit"]  = m_project.gantryTuning.countsPerUnit;
    tuning["travelMin"]      = m_project.gantryTuning.travelLimits.minMm;
    tuning["travelMax"]      = m_project.gantryTuning.travelLimits.maxMm;
    tuning["pwmRampPerTick"] = m_project.gantryTuning.pwmRampPerTick;
    tuning["pidKp"]          = m_project.gantryTuning.pidKp;
    tuning["pidKi"]          = m_project.gantryTuning.pidKi;
    tuning["pidKd"]          = m_project.gantryTuning.pidKd;
    tuning["configured"]     = m_project.gantryTuning.configured;
    tuning["stepAccelStepsPerSec2"] = m_project.gantryTuning.stepAccelStepsPerSec2;
    tuning["idleDisable"]           = m_project.gantryTuning.idleDisable;
    obj["gantryTuning"] = tuning;

    // ─── Multi-axis (schema 2) ────────────────────────────────────────────
    // Written IN ADDITION TO every legacy key above, never instead of them.
    // An older build opening this file still finds the flat gantry keys and
    // behaves exactly as it always did; a newer build prefers "axes".
    obj["schemaVersion"] = 2;

    QJsonArray axesArray;
    if (m_project.axes.isEmpty()) {
        // Nothing has populated the list yet — synthesise the first axis from
        // the live singular members so a file saved now already carries the
        // new shape rather than waiting for an unrelated edit.
        AxisConfig primary;
        primary.motorSpec = m_project.gantryMotorSpec;
        primary.tuning    = m_project.gantryTuning;
        axesArray.append(axisToJson(primary));
    } else {
        for (const auto& a : m_project.axes) axesArray.append(axisToJson(a));
    }
    obj["axes"] = axesArray;

    QJsonObject extraKfs;
    for (auto it = m_project.axisKeyframes.constBegin();
         it != m_project.axisKeyframes.constEnd(); ++it) {
        if (it.key() == "gantry") continue;   // that one lives in gantryKeyframes
        QJsonArray arr;
        for (const auto& kf : it.value()) {
            QJsonObject k;
            k["id"]         = kf.id;
            k["time"]       = kf.time;
            k["positionMm"] = kf.positionMm;
            k["easing"]     = static_cast<int>(kf.easing);
            arr.append(k);
        }
        extraKfs[it.key()] = arr;
    }
    obj["axisKeyframes"] = extraKfs;
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
    double legacyCounts = obj["gantryEncoderCountsPerMm"].toDouble(100.0);
    m_project.gantryEncoderCountsPerMm = legacyCounts; // keep the frozen field populated
    if (obj.contains("gantryMotorSpec")) {
        QJsonObject motorSpec = obj["gantryMotorSpec"].toObject();
        m_project.gantryMotorSpec.motorRpm          = motorSpec["motorRpm"].toDouble(3000.0);
        m_project.gantryMotorSpec.gearRatio         = motorSpec["gearRatio"].toDouble(1.0);
        m_project.gantryMotorSpec.mmPerRev          = motorSpec["mmPerRev"].toDouble(4.0);
        m_project.gantryMotorSpec.maxAccelMmPerSec2 = motorSpec["maxAccelMmPerSec2"].toDouble(400.0);
        m_project.gantryMotorSpec.configured        = motorSpec["configured"].toBool(false);
        m_project.gantryMotorSpec.axisType =
            static_cast<GantryAxisType>(motorSpec["axisType"].toInt(0)); // 0 == Linear
        // Absent in files written before steppers existed, and 0 is DcServoPwm,
        // so every one of those loads as exactly the axis it always was.
        m_project.gantryMotorSpec.driveKind =
            static_cast<AxisDriveKind>(motorSpec["driveKind"].toInt(0));
        m_project.gantryMotorSpec.pulsesPerRev = motorSpec["pulsesPerRev"].toDouble(1600.0);
        m_project.gantryMotorSpec.stepRateCeilingHz =
            motorSpec["stepRateCeilingHz"].toDouble(3500.0);
    } else {
        m_project.gantryMotorSpec = GantryMotorSpec(); // old project file — behave exactly as before
    }

    if (obj.contains("gantryTuning")) {
        QJsonObject tuning = obj["gantryTuning"].toObject();
        m_project.gantryTuning.countsPerUnit      = tuning["countsPerUnit"].toDouble(legacyCounts);
        m_project.gantryTuning.travelLimits.minMm = tuning["travelMin"].toDouble(0.0);
        m_project.gantryTuning.travelLimits.maxMm = tuning["travelMax"].toDouble(1000.0);
        m_project.gantryTuning.pwmRampPerTick     = tuning["pwmRampPerTick"].toInt(15);
        m_project.gantryTuning.pidKp              = tuning["pidKp"].toDouble(0.8);
        m_project.gantryTuning.pidKi              = tuning["pidKi"].toDouble(0.1);
        m_project.gantryTuning.pidKd              = tuning["pidKd"].toDouble(0.05);
        m_project.gantryTuning.configured         = tuning["configured"].toBool(false);
        m_project.gantryTuning.stepAccelStepsPerSec2 = tuning["stepAccelStepsPerSec2"].toDouble(40000.0);
        m_project.gantryTuning.idleDisable           = tuning["idleDisable"].toBool(false);
    } else {
        // Project predates the tuning block — take defaults, but migrate the
        // legacy counts/mm so an existing calibration isn't silently lost.
        m_project.gantryTuning = GantryTuning();
        m_project.gantryTuning.countsPerUnit = legacyCounts;
    }
    // ─── Multi-axis (schema 2) ────────────────────────────────────────────
    // The flat gantry keys have already been read above, so the singular
    // members hold the v1 answer. If this file also carries "axes", that is
    // authoritative and the singular members are re-mirrored FROM it; if it
    // does not, we synthesise the list FROM them. Either way both
    // representations agree when this returns, which is what lets consumers
    // migrate one at a time.
    m_project.axes.clear();
    m_project.axisKeyframes.clear();

    if (obj.contains("axes") && obj["axes"].isArray() && !obj["axes"].toArray().isEmpty()) {
        for (const auto& v : obj["axes"].toArray()) {
            m_project.axes.append(axisFromJson(v.toObject()));
        }
        // Mirror the primary axis back onto the legacy members. Every
        // not-yet-migrated consumer still reads those, so skipping this would
        // silently revert the first axis to defaults on any schema-2 file.
        const AxisConfig& primary = m_project.axes.first();
        m_project.gantryMotorSpec = primary.motorSpec;
        m_project.gantryTuning    = primary.tuning;
        m_project.gantryEncoderCountsPerMm = primary.tuning.countsPerUnit;
    } else {
        // Pre-schema-2 file. Synthesise exactly one axis from what the flat
        // keys gave us, so it loads bit-identically to how it always did.
        AxisConfig primary;
        primary.motorSpec = m_project.gantryMotorSpec;
        primary.tuning    = m_project.gantryTuning;
        m_project.axes.append(primary);
    }

    if (obj.contains("axisKeyframes")) {
        const QJsonObject extra = obj["axisKeyframes"].toObject();
        for (auto it = extra.constBegin(); it != extra.constEnd(); ++it) {
            QList<GantryKeyframe> kfs;
            for (const auto& v : it.value().toArray()) {
                const QJsonObject k = v.toObject();
                GantryKeyframe kf;
                kf.id         = k["id"].toString();
                kf.time       = k["time"].toDouble();
                kf.positionMm = k["positionMm"].toDouble();
                kf.easing     = static_cast<GantryKeyframe::Easing>(k["easing"].toInt());
                kfs.append(kf);
            }
            m_project.axisKeyframes.insert(it.key(), kfs);
        }
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
