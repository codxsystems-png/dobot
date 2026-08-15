// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Timeline Scene
// ═══════════════════════════════════════════════════════════════════════════════

#include "ui/timeline/timeline_scene.h"
#include "ui/timeline/segment_item.h"
#include "ui/timeline/playhead_item.h"
#include "ui/timeline/timeline_undo_commands.h"
#include "models/segments_model.h"
#include "models/points_model.h"
#include "math/motion_profile.h"
#include "core/motion_estimator.h"
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneDragDropEvent>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsView>
#include <QKeyEvent>
#include <QMimeData>
#include <QMenu>
#include <QUuid>
#include <algorithm>
#include <cmath>

static constexpr double SCENE_HEIGHT = 80.0;

TimelineScene::TimelineScene(SegmentsModel* segModel,
                             PointsModel* ptModel,
                             QObject* parent)
    : QGraphicsScene(parent)
    , m_segModel(segModel)
    , m_ptModel(ptModel)
{
    setBackgroundBrush(QColor(25, 25, 25));

    m_playhead = new PlayheadItem(SCENE_HEIGHT);
    addItem(m_playhead);
    connect(m_playhead, &PlayheadItem::timeDragged,
            this, &TimelineScene::playheadMoved);

    if (m_segModel) {
        connect(m_segModel, &QAbstractItemModel::modelReset,
                this, &TimelineScene::onModelReset);
        connect(m_segModel, &SegmentsModel::segmentAdded,
                this, &TimelineScene::onSegmentAdded);
        connect(m_segModel, &SegmentsModel::segmentRemoved,
                this, &TimelineScene::onSegmentRemoved);
        // Previously missing entirely: updateSegment() (Properties Panel
        // edits, undo/redo, nudge, ...) only ever emitted this signal —
        // nothing rebuilt the visual item, so the change saved to the model
        // but the timeline kept showing the segment's old time/appearance.
        connect(m_segModel, &SegmentsModel::segmentUpdated,
                this, &TimelineScene::onSegmentUpdated);
        rebuildItems();
    }
}

void TimelineScene::setSegmentsModel(SegmentsModel* model)
{
    m_segModel = model;
    if (m_segModel) {
        connect(m_segModel, &QAbstractItemModel::modelReset,
                this, &TimelineScene::onModelReset);
        connect(m_segModel, &SegmentsModel::segmentAdded,
                this, &TimelineScene::onSegmentAdded);
        connect(m_segModel, &SegmentsModel::segmentRemoved,
                this, &TimelineScene::onSegmentRemoved);
        connect(m_segModel, &SegmentsModel::segmentUpdated,
                this, &TimelineScene::onSegmentUpdated);
        rebuildItems();
    }
}


void TimelineScene::setPixelsPerSecond(double pps)
{
    m_pps = qMax(10.0, pps);
    rebuildItems();
}

void TimelineScene::setPlayheadTime(double seconds)
{
    if (m_playhead)
        m_playhead->setTime(seconds, m_pps);
}

double TimelineScene::playheadTime() const
{
    return m_playhead ? m_playhead->time() : 0.0;
}

double TimelineScene::estimateDurationSec(int row) const
{
    // Estimated move duration (distance from the nearest preceding row's
    // resolved point to this row's, at this segment's speed/accel %) —
    // rough Nova 5 Cartesian speed/accel placeholders, same values used for
    // MockDobotAdapter's simulated timing. Not calibrated against hardware;
    // this is a visual estimate only, not a safety calculation.
    static constexpr double kBaseVelocityMmPerSec = 400.0;
    static constexpr double kBaseAccelMmPerSec2   = 800.0;
    static constexpr double kMinDurationSec       = 0.3;

    if (!m_segModel || !m_ptModel || row < 0 || row >= m_segModel->rowCount()) return -1.0;

    TimelineSegment seg = m_segModel->segmentAt(row);
    CameraPoint pt = m_ptModel->pointById(seg.pointId);
    if (pt.id.isEmpty()) return -1.0;

    CameraPoint fromPt;
    bool haveFrom = false;
    for (int i = row - 1; i >= 0; --i) {
        CameraPoint candidate = m_ptModel->pointById(m_segModel->segmentAt(i).pointId);
        if (!candidate.id.isEmpty()) {
            fromPt = candidate;
            haveFrom = true;
            break;
        }
    }
    if (!haveFrom) return -1.0;

    double dx = pt.pose.x - fromPt.pose.x;
    double dy = pt.pose.y - fromPt.pose.y;
    double dz = pt.pose.z - fromPt.pose.z;
    double distanceMm = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (distanceMm < 1e-6) return kMinDurationSec;

    double speedFrac = qBound(1, seg.speedPct, 100) / 100.0;
    double accFrac   = qBound(1, seg.accPct, 100) / 100.0;
    double vMax = kBaseVelocityMmPerSec * speedFrac;
    double aMax = kBaseAccelMmPerSec2 * accFrac;

    math::TrapezoidalProfile profile(0.0, distanceMm, vMax, aMax);
    return qMax(kMinDurationSec, profile.duration());
}

void TimelineScene::rebuildItems()
{
    if (!m_segModel) return;

    // Remove existing segment items (keep playhead)
    QList<QGraphicsItem*> toRemove;
    for (auto* item : items()) {
        if (item->type() == SegmentItem::Type)
            toRemove.append(item);
    }
    for (auto* item : toRemove) {
        removeItem(item);
        delete item;
    }

    for (int i = 0; i < m_segModel->rowCount(); ++i) {
        TimelineSegment seg = m_segModel->segmentAt(i);
        QString name = m_segModel->data(m_segModel->index(i), Qt::DisplayRole).toString();
        auto* item = new SegmentItem(seg, name, m_pps, estimateDurationSec(i));
        addItem(item);
    }

    // Update scene rect
    double duration = m_segModel->totalDuration();
    setSceneRect(0, 0, qMax(duration * m_pps + 200, 1000.0), SCENE_HEIGHT);

    if (m_playhead)
        m_playhead->setSceneHeight(SCENE_HEIGHT);
}

QString TimelineScene::selectedSegmentId() const
{
    auto sel = selectedItems();
    for (auto* item : sel) {
        if (item->type() == SegmentItem::Type)
            return static_cast<SegmentItem*>(item)->segmentId();
    }
    return {};
}

void TimelineScene::commitSegmentMove(const QString& segId, double newTime)
{
    if (!m_segModel) return;

    int row = m_segModel->indexOf(segId);
    if (row < 0) return;

    TimelineSegment oldSeg = m_segModel->segmentAt(row);

    // Hard floor: dragging earlier than the gantry can physically get there
    // clamps back up, so the move (once released) can never land on an
    // infeasible gap. Predecessor = the other segment with the greatest
    // triggerTime that's still <= the requested newTime.
    if (m_ptModel) {
        TimelineSegment predecessor;
        bool havePredecessor = false;
        for (int i = 0; i < m_segModel->rowCount(); ++i) {
            if (i == row) continue;
            TimelineSegment candidate = m_segModel->segmentAt(i);
            if (candidate.triggerTime <= newTime &&
                (!havePredecessor || candidate.triggerTime > predecessor.triggerTime)) {
                predecessor = candidate;
                havePredecessor = true;
            }
        }
        if (havePredecessor) {
            CameraPoint fromPt = m_ptModel->pointById(predecessor.pointId);
            CameraPoint toPt   = m_ptModel->pointById(oldSeg.pointId);
            if (!fromPt.id.isEmpty() && !toPt.id.isEmpty()) {
                double minGap = motion::minGantryDurationSec(fromPt, toPt, m_segModel->gantryMotorSpec(), 0.0);
                double minTime = predecessor.triggerTime + minGap;
                if (newTime < minTime) {
                    emit segmentMoveClamped(segId, minTime, newTime);
                    newTime = minTime;
                }
            }
        }
    }

    if (oldSeg.triggerTime == newTime) return; // no actual change (e.g. click without drag)

    TimelineSegment newSeg = oldSeg;
    newSeg.triggerTime = newTime;

    if (m_undoStack) {
        m_undoStack->push(new UpdateSegmentCommand(m_segModel, segId, oldSeg, newSeg, "Move Segment"));
    } else {
        m_segModel->updateSegment(row, newSeg);
    }
    emit segmentMoved(segId, newTime);
}

double TimelineScene::nearestOtherSegmentTime(const QString& excludeId, double targetTime, double thresholdSec) const
{
    if (!m_segModel) return -1.0;

    double best = -1.0;
    double bestDist = thresholdSec;
    for (int i = 0; i < m_segModel->rowCount(); ++i) {
        TimelineSegment seg = m_segModel->segmentAt(i);
        if (seg.id == excludeId) continue;
        double dist = std::abs(seg.triggerTime - targetTime);
        if (dist <= bestDist) {
            bestDist = dist;
            best = seg.triggerTime;
        }
    }
    return best;
}

QStringList TimelineScene::selectedSegmentIds() const
{
    QStringList ids;
    for (auto* item : selectedItems()) {
        if (item->type() == SegmentItem::Type)
            ids << static_cast<SegmentItem*>(item)->segmentId();
    }
    return ids;
}

void TimelineScene::deleteSelectedSegments()
{
    if (!m_segModel) return;
    QStringList ids = selectedSegmentIds();
    if (ids.isEmpty()) return;

    if (m_undoStack) m_undoStack->beginMacro(
        ids.size() > 1 ? QString("Delete %1 Segments").arg(ids.size()) : "Delete Segment");

    for (const QString& id : ids) {
        int row = m_segModel->indexOf(id);
        if (row < 0) continue;
        TimelineSegment seg = m_segModel->segmentAt(row);
        if (m_undoStack) {
            m_undoStack->push(new RemoveSegmentCommand(m_segModel, seg));
        } else {
            m_segModel->removeSegment(row);
        }
    }

    if (m_undoStack) m_undoStack->endMacro();
}

void TimelineScene::duplicateSelectedSegments()
{
    if (!m_segModel) return;
    QStringList ids = selectedSegmentIds();
    if (ids.isEmpty()) return;

    static constexpr double kDuplicateOffsetSec = 1.5;

    if (m_undoStack) m_undoStack->beginMacro(
        ids.size() > 1 ? QString("Duplicate %1 Segments").arg(ids.size()) : "Duplicate Segment");

    for (const QString& id : ids) {
        int row = m_segModel->indexOf(id);
        if (row < 0) continue;
        TimelineSegment clone = m_segModel->segmentAt(row);
        clone.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        clone.triggerTime = qMax(0.0, clone.triggerTime + kDuplicateOffsetSec);

        if (m_undoStack) {
            m_undoStack->push(new AddSegmentCommand(m_segModel, clone));
        } else {
            m_segModel->addSegment(clone);
        }
    }

    if (m_undoStack) m_undoStack->endMacro();
}

void TimelineScene::copySelectedSegments()
{
    if (!m_segModel) return;
    m_clipboard.clear();
    for (const QString& id : selectedSegmentIds()) {
        int row = m_segModel->indexOf(id);
        if (row >= 0) m_clipboard.append(m_segModel->segmentAt(row));
    }
}

void TimelineScene::pasteSegments()
{
    if (!m_segModel || m_clipboard.isEmpty()) return;

    // Anchor the earliest copied segment at the current playhead time and
    // preserve every other copied segment's original spacing relative to it.
    double earliest = m_clipboard.first().triggerTime;
    for (const auto& seg : m_clipboard) earliest = qMin(earliest, seg.triggerTime);
    double anchor = m_playhead ? m_playhead->time() : 0.0;

    if (m_undoStack) m_undoStack->beginMacro(
        m_clipboard.size() > 1 ? QString("Paste %1 Segments").arg(m_clipboard.size()) : "Paste Segment");

    for (const auto& original : m_clipboard) {
        TimelineSegment clone = original;
        clone.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        clone.triggerTime = qMax(0.0, anchor + (original.triggerTime - earliest));

        if (m_undoStack) {
            m_undoStack->push(new AddSegmentCommand(m_segModel, clone));
        } else {
            m_segModel->addSegment(clone);
        }
    }

    if (m_undoStack) m_undoStack->endMacro();
}

void TimelineScene::nudgeSelectedSegments(double deltaSeconds)
{
    if (!m_segModel) return;
    QStringList ids = selectedSegmentIds();
    if (ids.isEmpty()) return;

    if (m_undoStack) m_undoStack->beginMacro("Nudge Segment");

    for (const QString& id : ids) {
        int row = m_segModel->indexOf(id);
        if (row < 0) continue;
        TimelineSegment oldSeg = m_segModel->segmentAt(row);
        TimelineSegment newSeg = oldSeg;
        newSeg.triggerTime = qMax(0.0, oldSeg.triggerTime + deltaSeconds);
        if (newSeg.triggerTime == oldSeg.triggerTime) continue;

        if (m_undoStack) {
            m_undoStack->push(new UpdateSegmentCommand(m_segModel, id, oldSeg, newSeg, "Nudge Segment"));
        } else {
            m_segModel->updateSegment(row, newSeg);
        }
    }

    if (m_undoStack) m_undoStack->endMacro();
}

void TimelineScene::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    QGraphicsScene::mousePressEvent(event);

    // A StrongFocus policy on the QGraphicsView isn't enough by itself —
    // Qt doesn't reliably hand the view keyboard focus just because a click
    // landed inside its viewport, so keyboard shortcuts (Delete, Ctrl+D/C/V,
    // arrow-key nudge) never fired. Force it explicitly on every click.
    if (!views().isEmpty()) {
        views().first()->setFocus(Qt::MouseFocusReason);
    }

    QString selId = selectedSegmentId();
    if (selId.isEmpty())
        emit segmentDeselected();
    else
        emit segmentSelected(selId);
}

void TimelineScene::dragEnterEvent(QGraphicsSceneDragDropEvent* event)
{
    if (event->mimeData() && event->mimeData()->hasFormat("application/x-cambot-point")) {
        event->acceptProposedAction();
    } else {
        QGraphicsScene::dragEnterEvent(event);
    }
}

void TimelineScene::dragMoveEvent(QGraphicsSceneDragDropEvent* event)
{
    if (event->mimeData() && event->mimeData()->hasFormat("application/x-cambot-point")) {
        event->acceptProposedAction();
    } else {
        QGraphicsScene::dragMoveEvent(event);
    }
}

void TimelineScene::dropEvent(QGraphicsSceneDragDropEvent* event)
{
    if (event->mimeData() && event->mimeData()->hasFormat("application/x-cambot-point")) {
        QString pointId = QString::fromUtf8(event->mimeData()->data("application/x-cambot-point"));
        double dropX = event->scenePos().x();
        double timeSec = qMax(0.0, dropX / m_pps);

        // Floor against the physically-minimum gantry move time from the
        // nearest segment preceding the drop point, if the motor spec is
        // configured. If nothing precedes it, the pixel time stands as-is.
        if (m_segModel && m_ptModel) {
            TimelineSegment predecessor;
            bool havePredecessor = false;
            for (int i = 0; i < m_segModel->rowCount(); ++i) {
                TimelineSegment candidate = m_segModel->segmentAt(i);
                if (candidate.triggerTime <= timeSec &&
                    (!havePredecessor || candidate.triggerTime > predecessor.triggerTime)) {
                    predecessor = candidate;
                    havePredecessor = true;
                }
            }
            if (havePredecessor) {
                CameraPoint fromPt = m_ptModel->pointById(predecessor.pointId);
                CameraPoint toPt   = m_ptModel->pointById(pointId);
                if (!fromPt.id.isEmpty() && !toPt.id.isEmpty()) {
                    double minGap = motion::minGantryDurationSec(fromPt, toPt, m_segModel->gantryMotorSpec(), 0.0);
                    timeSec = qMax(timeSec, predecessor.triggerTime + minGap);
                }
            }
        }

        if (m_segModel) {
            TimelineSegment seg;
            seg.id          = QUuid::createUuid().toString(QUuid::WithoutBraces);
            seg.pointId     = pointId;
            seg.type        = TimelineSegment::MovJ;
            seg.speedPct    = 80;
            seg.accPct      = 50;
            seg.triggerTime = timeSec;

            if (m_undoStack) {
                m_undoStack->push(new AddSegmentCommand(m_segModel, seg, "Add Segment"));
            } else {
                m_segModel->addSegment(seg);
            }
            emit pointDroppedOnTimeline(pointId, timeSec);
        }
        event->acceptProposedAction();
        QGraphicsScene::dropEvent(event);
    }
}

void TimelineScene::keyPressEvent(QKeyEvent* event)
{
    // Delete/Backspace — acts on every selected segment (multi-select aware).
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        if (!selectedSegmentIds().isEmpty()) {
            deleteSelectedSegments();
            event->accept();
            return;
        }
    }
    // Ctrl+D — duplicate selection.
    if (event->key() == Qt::Key_D && (event->modifiers() & Qt::ControlModifier)) {
        duplicateSelectedSegments();
        event->accept();
        return;
    }
    // Ctrl+C / Ctrl+V — in-app clipboard.
    if (event->key() == Qt::Key_C && (event->modifiers() & Qt::ControlModifier)) {
        copySelectedSegments();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_V && (event->modifiers() & Qt::ControlModifier)) {
        pasteSegments();
        event->accept();
        return;
    }
    // Arrow keys — nudge selection in time. Shift = coarse step (1s vs 0.1s).
    if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right) {
        if (!selectedSegmentIds().isEmpty()) {
            double step = (event->modifiers() & Qt::ShiftModifier) ? 1.0 : 0.1;
            nudgeSelectedSegments(event->key() == Qt::Key_Left ? -step : step);
            event->accept();
            return;
        }
    }
    QGraphicsScene::keyPressEvent(event);
}

void TimelineScene::contextMenuEvent(QGraphicsSceneContextMenuEvent* event)
{
    QGraphicsItem* item = itemAt(event->scenePos(), QTransform());
    if (!item || item->type() != SegmentItem::Type) {
        QGraphicsScene::contextMenuEvent(event);
        return;
    }

    auto* segItem = static_cast<SegmentItem*>(item);
    QString segId = segItem->segmentId();

    // Right-click a segment that's part of the current multi-selection acts
    // on the whole selection; right-clicking an unselected one selects just
    // that one first, so it's always unambiguous what the menu acts on.
    if (!segItem->isSelected()) {
        clearSelection();
        segItem->setSelected(true);
        emit segmentSelected(segId);
    }
    int selCount = selectedSegmentIds().size();
    QString suffix = selCount > 1 ? QString(" (%1)").arg(selCount) : QString();

    QMenu menu;
    QAction* deleteAction = menu.addAction("Delete" + suffix);
    QAction* duplicateAction = menu.addAction("Duplicate" + suffix);
    QAction* copyAction = menu.addAction("Copy" + suffix);
    menu.addSeparator();
    QAction* pasteAction = menu.addAction("Paste");
    pasteAction->setEnabled(hasClipboardContent());

    QAction* chosen = menu.exec(event->screenPos());
    if (chosen == deleteAction) deleteSelectedSegments();
    else if (chosen == duplicateAction) duplicateSelectedSegments();
    else if (chosen == copyAction) copySelectedSegments();
    else if (chosen == pasteAction) pasteSegments();

    event->accept();
}

SegmentItem* TimelineScene::findItem(const QString& segId) const
{
    for (auto* item : items()) {
        if (item->type() == SegmentItem::Type) {
            auto* si = static_cast<SegmentItem*>(item);
            if (si->segmentId() == segId)
                return si;
        }
    }
    return nullptr;
}

void TimelineScene::onModelReset()
{
    rebuildItems();
}

void TimelineScene::onSegmentAdded(int row)
{
    Q_UNUSED(row)
    rebuildItems();
}

void TimelineScene::onSegmentRemoved(int row)
{
    Q_UNUSED(row)
    rebuildItems();
}

void TimelineScene::onSegmentUpdated(int row)
{
    if (!m_segModel || row < 0 || row >= m_segModel->rowCount()) return;

    TimelineSegment seg = m_segModel->segmentAt(row);
    SegmentItem* item = findItem(seg.id);
    if (!item) {
        rebuildItems(); // shouldn't happen, but don't silently show nothing
        return;
    }

    // Update the EXISTING item in place (not a full rebuildItems()) so the
    // segment's selection state survives the update — otherwise holding an
    // arrow key to nudge repeatedly would deselect after the first press.
    QString name = m_segModel->data(m_segModel->index(row), Qt::DisplayRole).toString();
    item->updateFromSegment(seg, name, m_pps, estimateDurationSec(row));

    // This segment's own point is also the "from" pose for whichever
    // segment comes after it, so that neighbor's width estimate may have
    // changed too.
    if (row + 1 < m_segModel->rowCount()) {
        TimelineSegment nextSeg = m_segModel->segmentAt(row + 1);
        if (SegmentItem* nextItem = findItem(nextSeg.id)) {
            QString nextName = m_segModel->data(m_segModel->index(row + 1), Qt::DisplayRole).toString();
            nextItem->updateFromSegment(nextSeg, nextName, m_pps, estimateDurationSec(row + 1));
        }
    }

    double duration = m_segModel->totalDuration();
    setSceneRect(0, 0, qMax(duration * m_pps + 200, 1000.0), SCENE_HEIGHT);
}
