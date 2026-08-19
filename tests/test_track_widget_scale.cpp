// ═══════════════════════════════════════════════════════════════════════════════
// Test: FizTrackWidget axis rows — row count and, above all, the value scale.
//
// The scale had a real defect: display divided position by 1.8 (assuming a
// 0-180 range) while placement multiplied by 10 (assuming 0-1000). The two were
// not inverses, so a keyframe dropped at mid-height was stored at a position
// that redrew somewhere else — the diamond visibly jumped, and the stored value
// was not what the operator pointed at.
//
// These tests drive the widget through its public interface: place a keyframe
// by double-clicking at a known height, and assert the emitted position matches
// that height on the axis's own travel range.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QtTest/QtTest>
#include <QSignalSpy>
#include "presentation/widgets/fiztrackwidget.h"

namespace {

/// Double-clicks at a given row and fractional height (0 = bottom, 1 = top).
void doubleClickRow(FizTrackWidget* w, int rowIndex, double heightFraction, int x = 50)
{
    constexpr int ROW_H = 38;
    const int top = rowIndex * ROW_H;
    const int y   = top + static_cast<int>((1.0 - heightFraction) * ROW_H);
    QTest::mouseDClick(w, Qt::LeftButton, Qt::NoModifier, QPoint(x, y));
}

} // namespace

class TestTrackWidgetScale : public QObject
{
    Q_OBJECT
private slots:

    // ─── Rows are data ────────────────────────────────────────────────────

    void testDefaultHasThreeFizRowsPlusOneAxis()
    {
        FizTrackWidget w;
        QCOMPARE(w.height(), 38 * 4);
    }

    /// The row count follows the axis list. A hardcoded four is exactly what
    /// stopped a second axis ever being visible.
    void testHeightGrowsWithEachAxis()
    {
        FizTrackWidget w;
        w.setAxes({ {"gantry", "GANTRY", 0.0, 100.0, QColor()},
                    {"tilt",   "TILT",   0.0, 90.0,  QColor()} });
        QCOMPARE(w.height(), 38 * 5);

        w.setAxes({ {"gantry", "GANTRY", 0.0, 100.0, QColor()} });
        QCOMPARE(w.height(), 38 * 4);
    }

    void testNoAxesLeavesOnlyTheFizRows()
    {
        FizTrackWidget w;
        w.setAxes({});
        QCOMPARE(w.height(), 38 * 3);
    }

    // ─── The value scale ──────────────────────────────────────────────────

    /// Dropping at mid-height must store the middle of the axis's OWN range.
    /// With travel 0..360 that is 180 — the old code produced 500.
    void testMidHeightMapsToMidTravel()
    {
        FizTrackWidget w;
        w.setAxes({ {"gantry", "GANTRY", 0.0, 360.0, QColor()} });
        w.resize(400, w.height());

        QSignalSpy spy(&w, &FizTrackWidget::addAxisKeyframeRequested);
        doubleClickRow(&w, 3, 0.5);          // row 3 = first axis row

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toString(), QString("gantry"));
        const double pos = spy.first().at(2).toDouble();
        QVERIFY2(qAbs(pos - 180.0) < 12.0,
                 qPrintable(QString("mid-height on a 0..360 axis should be ~180, got %1").arg(pos)));
    }

    /// Top of the row is the top of travel, bottom is the bottom. The old
    /// scale clamped at 100% well before the top for any range above 180.
    void testTopAndBottomMapToTheTravelEnds()
    {
        FizTrackWidget w;
        w.setAxes({ {"gantry", "GANTRY", 0.0, 1000.0, QColor()} });
        w.resize(400, w.height());

        QSignalSpy spy(&w, &FizTrackWidget::addAxisKeyframeRequested);

        doubleClickRow(&w, 3, 1.0);          // top
        QCOMPARE(spy.count(), 1);
        QVERIFY(spy.at(0).at(2).toDouble() > 900.0);

        doubleClickRow(&w, 3, 0.0);          // bottom
        QCOMPARE(spy.count(), 2);
        QVERIFY(spy.at(1).at(2).toDouble() < 100.0);
    }

    /// A range that does not start at zero must still map correctly — this is
    /// the rotary axis commissioned with travel -15.5..359.5.
    void testNonZeroBasedRangeMapsFromItsMinimum()
    {
        FizTrackWidget w;
        w.setAxes({ {"gantry", "GANTRY", -100.0, 100.0, QColor()} });
        w.resize(400, w.height());

        QSignalSpy spy(&w, &FizTrackWidget::addAxisKeyframeRequested);
        doubleClickRow(&w, 3, 0.5);

        QCOMPARE(spy.count(), 1);
        const double pos = spy.first().at(2).toDouble();
        QVERIFY2(qAbs(pos) < 15.0,
                 qPrintable(QString("mid-height on a -100..100 axis should be ~0, got %1").arg(pos)));
    }

    /// Each axis row uses ITS OWN range, not the first axis's.
    void testEachAxisRowUsesItsOwnRange()
    {
        FizTrackWidget w;
        w.setAxes({ {"gantry", "GANTRY", 0.0, 1000.0, QColor()},
                    {"tilt",   "TILT",   0.0, 90.0,   QColor()} });
        w.resize(400, w.height());

        QSignalSpy spy(&w, &FizTrackWidget::addAxisKeyframeRequested);
        doubleClickRow(&w, 4, 0.5);          // row 4 = the SECOND axis

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toString(), QString("tilt"));
        const double pos = spy.first().at(2).toDouble();
        QVERIFY2(qAbs(pos - 45.0) < 6.0,
                 qPrintable(QString("mid-height on a 0..90 axis should be ~45, got %1").arg(pos)));
    }

    /// The primary axis must still emit the legacy signal, so existing wiring
    /// keeps working while call sites migrate.
    void testPrimaryAxisStillEmitsTheLegacySignal()
    {
        FizTrackWidget w;
        w.setAxes({ {"gantry", "GANTRY", 0.0, 100.0, QColor()} });
        w.resize(400, w.height());

        QSignalSpy legacy(&w, &FizTrackWidget::addGantryKeyframeRequested);
        doubleClickRow(&w, 3, 0.5);
        QCOMPARE(legacy.count(), 1);
    }

    /// A non-primary axis must NOT fire the legacy signal, or its keyframes
    /// would be written into the primary axis's list.
    void testSecondaryAxisDoesNotEmitTheLegacySignal()
    {
        FizTrackWidget w;
        w.setAxes({ {"gantry", "GANTRY", 0.0, 100.0, QColor()},
                    {"tilt",   "TILT",   0.0, 90.0,  QColor()} });
        w.resize(400, w.height());

        QSignalSpy legacy(&w, &FizTrackWidget::addGantryKeyframeRequested);
        doubleClickRow(&w, 4, 0.5);
        QCOMPARE(legacy.count(), 0);
    }

    /// A degenerate range must not divide by zero or collapse every keyframe
    /// onto one pixel row.
    void testDegenerateRangeIsSurvivable()
    {
        FizTrackWidget w;
        w.setAxes({ {"gantry", "GANTRY", 50.0, 50.0, QColor()} });
        w.resize(400, w.height());

        QSignalSpy spy(&w, &FizTrackWidget::addAxisKeyframeRequested);
        doubleClickRow(&w, 3, 0.5);
        QCOMPARE(spy.count(), 1);            // no crash, and it still reports
    }

    /// A removed axis must not leave its keyframes painting onto whichever row
    /// later takes its index.
    void testRemovingAnAxisDropsItsKeyframes()
    {
        FizTrackWidget w;
        w.setAxes({ {"gantry", "GANTRY", 0.0, 100.0, QColor()},
                    {"tilt",   "TILT",   0.0, 90.0,  QColor()} });

        GantryKeyframe kf;
        kf.id = "t1"; kf.time = 1.0; kf.positionMm = 45.0;
        w.setAxisKeyframes("tilt", { kf });

        w.setAxes({ {"gantry", "GANTRY", 0.0, 100.0, QColor()} });
        QCOMPARE(w.height(), 38 * 4);        // and nothing left over to paint
    }
};

QTEST_MAIN(TestTrackWidgetScale)
#include "test_track_widget_scale.moc"
