// ═══════════════════════════════════════════════════════════════════════════════
// Test: .crp schema migration — a v1 project must load bit-identically, and a
// v2 project must still be readable by everything that only knows v1 keys.
//
// This is the test that matters most in the multi-axis work, because its
// failure mode is silent: a migration that drops a field does not crash, it
// quietly resets someone's calibrated axis to defaults and only shows up as
// "playback is inaccurate" on a shoot day. So the assertions here are on
// exact values, not on "loaded without error".
// ═══════════════════════════════════════════════════════════════════════════════

#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include "services/project_service.h"

namespace {

/// A real pre-multi-axis project file: flat gantry keys, no "axes", no
/// "schemaVersion". Values are deliberately non-default throughout — a
/// migration bug that resets a field to its default would pass against
/// defaults and fail here.
const char* kV1Project = R"JSON({
  "name": "Old Shoot",
  "version": "1.2.0",
  "timelineDuration": 42.5,
  "points": [],
  "segments": [],
  "gantryEncoderCountsPerMm": 2709.2,
  "gantryMotorSpec": {
    "motorRpm": 300.0,
    "gearRatio": 5.5,
    "mmPerRev": 12.25,
    "maxAccelMmPerSec2": 275.0,
    "configured": true,
    "axisType": 1
  },
  "gantryTuning": {
    "countsPerUnit": 7.526,
    "travelMin": -15.5,
    "travelMax": 359.5,
    "pwmRampPerTick": 23,
    "pidKp": 2.0983,
    "pidKi": 4.8310,
    "pidKd": 0.0658,
    "configured": true
  },
  "gantryKeyframes": [
    { "id": "k1", "time": 1.25, "positionMm": 90.5, "easing": 1 },
    { "id": "k2", "time": 3.75, "positionMm": 270.25, "easing": 0 }
  ]
})JSON";

QString writeTemp(QTemporaryDir& dir, const QString& name, const QByteArray& content)
{
    const QString path = dir.filePath(name);
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(content);
    f.close();
    return path;
}

} // namespace

class TestProjectMigration : public QObject
{
    Q_OBJECT
private slots:

    // ─── v1 → in-memory ───────────────────────────────────────────────────

    void testV1FlatKeysSurviveExactly()
    {
        QTemporaryDir dir;
        const QString path = writeTemp(dir, "v1.crp", kV1Project);

        ProjectService svc(nullptr);
        QVERIFY(svc.loadProject(path));
        const Project& p = svc.project();

        // Motor spec, value for value.
        QCOMPARE(p.gantryMotorSpec.motorRpm, 300.0);
        QCOMPARE(p.gantryMotorSpec.gearRatio, 5.5);
        QCOMPARE(p.gantryMotorSpec.mmPerRev, 12.25);
        QCOMPARE(p.gantryMotorSpec.maxAccelMmPerSec2, 275.0);
        QVERIFY(p.gantryMotorSpec.configured);
        QCOMPARE(p.gantryMotorSpec.axisType, GantryAxisType::Rotary);

        // Tuning, including the hard-won commissioning numbers.
        QCOMPARE(p.gantryTuning.countsPerUnit, 7.526);
        QCOMPARE(p.gantryTuning.travelLimits.minMm, -15.5);
        QCOMPARE(p.gantryTuning.travelLimits.maxMm, 359.5);
        QCOMPARE(p.gantryTuning.pwmRampPerTick, 23);
        QCOMPARE(p.gantryTuning.pidKp, 2.0983);
        QCOMPARE(p.gantryTuning.pidKi, 4.8310);
        QCOMPARE(p.gantryTuning.pidKd, 0.0658);
        QVERIFY(p.gantryTuning.configured);

        QCOMPARE(p.gantryKeyframes.size(), 2);
        QCOMPARE(p.gantryKeyframes.at(0).positionMm, 90.5);
        QCOMPARE(p.gantryKeyframes.at(1).time, 3.75);
    }

    /// A v1 file predates drive kinds entirely, so it must come back as the
    /// DC servo it always was. If this ever loaded as a stepper, the tuning
    /// dialog would refuse to open and playback would stream step targets at
    /// an H-bridge.
    void testV1DefaultsToDcServo()
    {
        QTemporaryDir dir;
        ProjectService svc(nullptr);
        QVERIFY(svc.loadProject(writeTemp(dir, "v1.crp", kV1Project)));

        QCOMPARE(svc.project().gantryMotorSpec.driveKind, AxisDriveKind::DcServoPwm);
    }

    /// Loading a v1 file must still produce exactly one axis in the new list,
    /// carrying the same values — that synthesis is what lets the rest of the
    /// codebase migrate to `axes` without breaking old projects.
    void testV1SynthesisesOneAxis()
    {
        QTemporaryDir dir;
        ProjectService svc(nullptr);
        QVERIFY(svc.loadProject(writeTemp(dir, "v1.crp", kV1Project)));
        const Project& p = svc.project();

        QCOMPARE(p.axes.size(), 1);
        QCOMPARE(p.axes.first().id, QString("gantry"));
        QCOMPARE(p.axes.first().tuning.countsPerUnit, 7.526);
        QCOMPARE(p.axes.first().motorSpec.gearRatio, 5.5);
        QCOMPARE(p.axes.first().motorSpec.axisType, GantryAxisType::Rotary);
    }

    // ─── Round trip ───────────────────────────────────────────────────────

    /// Load v1, save, load again: every value must be unchanged. A migration
    /// that silently drops a field usually survives one load — this is what
    /// catches it.
    void testV1RoundTripsWithoutLoss()
    {
        QTemporaryDir dir;
        const QString v1 = writeTemp(dir, "v1.crp", kV1Project);
        const QString v2 = dir.filePath("resaved.crp");

        ProjectService a(nullptr);
        QVERIFY(a.loadProject(v1));
        QVERIFY(a.saveProjectAs(v2));

        ProjectService b(nullptr);
        QVERIFY(b.loadProject(v2));
        const Project& p = b.project();

        QCOMPARE(p.gantryTuning.countsPerUnit, 7.526);
        QCOMPARE(p.gantryTuning.pidKp, 2.0983);
        QCOMPARE(p.gantryTuning.pidKi, 4.8310);
        QCOMPARE(p.gantryTuning.pidKd, 0.0658);
        QCOMPARE(p.gantryTuning.pwmRampPerTick, 23);
        QCOMPARE(p.gantryTuning.travelLimits.minMm, -15.5);
        QCOMPARE(p.gantryTuning.travelLimits.maxMm, 359.5);
        QCOMPARE(p.gantryMotorSpec.motorRpm, 300.0);
        QCOMPARE(p.gantryMotorSpec.gearRatio, 5.5);
        QCOMPARE(p.gantryMotorSpec.mmPerRev, 12.25);
        QCOMPARE(p.gantryMotorSpec.axisType, GantryAxisType::Rotary);
        QCOMPARE(p.gantryKeyframes.size(), 2);
        QCOMPARE(p.gantryKeyframes.at(1).positionMm, 270.25);
    }

    /// The legacy keys must STILL be written, so a build that predates all of
    /// this can open a file we saved. Dropping them would strand anyone who
    /// rolls back mid-shoot.
    void testSavedFileStillCarriesLegacyKeys()
    {
        QTemporaryDir dir;
        ProjectService a(nullptr);
        QVERIFY(a.loadProject(writeTemp(dir, "v1.crp", kV1Project)));
        const QString out = dir.filePath("out.crp");
        QVERIFY(a.saveProjectAs(out));

        QFile f(out);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();

        QVERIFY2(o.contains("gantryMotorSpec"), "legacy flat spec must still be written");
        QVERIFY2(o.contains("gantryTuning"),    "legacy flat tuning must still be written");
        QVERIFY2(o.contains("gantryEncoderCountsPerMm"), "legacy counts key must still be written");
        QVERIFY2(o.contains("axes"), "new axes array must be written alongside");
        QCOMPARE(o["schemaVersion"].toInt(), 2);

        // And the legacy copy must agree with the axis, not lag behind it.
        QCOMPARE(o["gantryTuning"].toObject()["countsPerUnit"].toDouble(), 7.526);
        QCOMPARE(o["gantryEncoderCountsPerMm"].toDouble(), 7.526);
    }

    // ─── v2 → in-memory ───────────────────────────────────────────────────

    /// When "axes" is present it is authoritative, and the legacy members must
    /// be re-mirrored from it. Skipping that mirror would silently revert the
    /// first axis to defaults for every consumer still reading the flat keys —
    /// which today is most of them.
    void testV2AxesAreMirroredOntoLegacyMembers()
    {
        QTemporaryDir dir;
        const QByteArray v2 = R"JSON({
          "name": "New Shoot",
          "schemaVersion": 2,
          "points": [], "segments": [],
          "gantryEncoderCountsPerMm": 100.0,
          "gantryMotorSpec": { "motorRpm": 3000.0, "gearRatio": 1.0 },
          "gantryTuning": { "countsPerUnit": 100.0 },
          "axes": [{
            "id": "gantry",
            "displayName": "Slider",
            "portName": "COM9",
            "firmwareAxisIndex": 1,
            "motorSpec": { "motorRpm": 600.0, "gearRatio": 3.0, "mmPerRev": 8.0,
                           "driveKind": 1, "pulsesPerRev": 800.0,
                           "stepRateCeilingHz": 4200.0, "axisType": 0,
                           "configured": true },
            "tuning": { "countsPerUnit": 55.5, "travelMin": 2.0, "travelMax": 800.0,
                        "stepAccelStepsPerSec2": 12345.0, "idleDisable": true,
                        "configured": true }
          }]
        })JSON";

        ProjectService svc(nullptr);
        QVERIFY(svc.loadProject(writeTemp(dir, "v2.crp", v2)));
        const Project& p = svc.project();

        QCOMPARE(p.axes.size(), 1);
        QCOMPARE(p.axes.first().displayName, QString("Slider"));
        QCOMPARE(p.axes.first().portName, QString("COM9"));
        QCOMPARE(p.axes.first().firmwareAxisIndex, 1);

        // The axis wins over the stale flat keys, not the other way round.
        QCOMPARE(p.gantryTuning.countsPerUnit, 55.5);
        QCOMPARE(p.gantryMotorSpec.motorRpm, 600.0);
        QCOMPARE(p.gantryMotorSpec.driveKind, AxisDriveKind::StepDirClosedLoop);
        QCOMPARE(p.gantryMotorSpec.stepRateCeilingHz, 4200.0);
        QCOMPARE(p.gantryTuning.stepAccelStepsPerSec2, 12345.0);
        QVERIFY(p.gantryTuning.idleDisable);
    }

    /// Additional axes' keyframes round-trip separately from the primary's.
    void testExtraAxisKeyframesRoundTrip()
    {
        QTemporaryDir dir;
        ProjectService a(nullptr);
        QVERIFY(a.loadProject(writeTemp(dir, "v1.crp", kV1Project)));

        GantryKeyframe kf;
        kf.id = "tilt1"; kf.time = 2.5; kf.positionMm = 44.75;
        a.setAxisKeyframes("tilt_head", { kf });

        const QString out = dir.filePath("multi.crp");
        QVERIFY(a.saveProjectAs(out));

        ProjectService b(nullptr);
        QVERIFY(b.loadProject(out));
        QVERIFY(b.project().axisKeyframes.contains("tilt_head"));
        QCOMPARE(b.project().axisKeyframes.value("tilt_head").size(), 1);
        QCOMPARE(b.project().axisKeyframes.value("tilt_head").first().positionMm, 44.75);
        // And the primary axis's own keyframes are untouched by that.
        QCOMPARE(b.project().gantryKeyframes.size(), 2);
    }

    /// A file with neither representation must not crash or produce an empty
    /// axis list — downstream code will index axes[0].
    void testEmptyProjectStillHasOneAxis()
    {
        QTemporaryDir dir;
        ProjectService svc(nullptr);
        QVERIFY(svc.loadProject(writeTemp(dir, "bare.crp",
                                   R"JSON({"name":"bare","points":[],"segments":[]})JSON")));

        QCOMPARE(svc.project().axes.size(), 1);
        QCOMPARE(svc.project().axes.first().id, QString("gantry"));
    }
};

QTEST_MAIN(TestProjectMigration)
#include "test_project_migration.moc"
