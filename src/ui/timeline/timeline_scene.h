#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Timeline Scene (QGraphicsScene)
// Owns SegmentItems and PlayheadItem. Syncs with SegmentsModel.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QGraphicsScene>
#include <QUndoStack>
#include "core/types.h"

class SegmentsModel;
class PointsModel;
class SegmentItem;
class PlayheadItem;

class TimelineScene : public QGraphicsScene
{
    Q_OBJECT
public:
    explicit TimelineScene(SegmentsModel* segModel,
                           PointsModel* ptModel,
                           QObject* parent = nullptr);

    void setPixelsPerSecond(double pps);
    double pixelsPerSecond() const { return m_pps; }

    void setPlayheadTime(double seconds);
    double playheadTime() const;

    void setSegmentsModel(SegmentsModel* model);
    void setPointsModel(PointsModel* model) { m_ptModel = model; }

    void setUndoStack(QUndoStack* stack) { m_undoStack = stack; }

    /// Rebuild all items from model
    void rebuildItems();

    /// Get selected segment ID (empty if none)
    QString selectedSegmentId() const;

    /// Get every selected segment ID (empty list if none) — for multi-select
    /// delete/nudge, which single selectedSegmentId() can't express.
    QStringList selectedSegmentIds() const;

    /// Called by SegmentItem when a drag finishes — persists the new time
    /// to the model (previously the drag only ever moved the item visually
    /// and was silently discarded on the next rebuild).
    void commitSegmentMove(const QString& segId, double newTime);

    /// Returns another segment's triggerTime if one is within thresholdSec
    /// of targetTime (excluding excludeId), else -1. Used for drag-to-align.
    double nearestOtherSegmentTime(const QString& excludeId, double targetTime, double thresholdSec) const;

    /// Deletes every currently selected segment as one undo step.
    void deleteSelectedSegments();

    /// Duplicates every currently selected segment, offset later in time.
    void duplicateSelectedSegments();

    /// In-app clipboard: copies/pastes the currently selected segment(s).
    void copySelectedSegments();
    void pasteSegments();
    bool hasClipboardContent() const { return !m_clipboard.isEmpty(); }

    /// Nudges every selected segment's trigger time by deltaSeconds (can be
    /// negative), clamped at 0, as a single undo step.
    void nudgeSelectedSegments(double deltaSeconds);

signals:
    void segmentSelected(const QString& segId);
    void segmentDeselected();
    void segmentMoved(const QString& segId, double newTime);
    // Fired when a drag ended below the gantry's physically-minimum feasible
    // time for that move and got clamped up.
    void segmentMoveClamped(const QString& segId, double clampedTime, double requestedTime);
    void playheadMoved(double seconds);
    void pointDroppedOnTimeline(const QString& pointId, double timeSec, const QString& segId);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void dragEnterEvent(QGraphicsSceneDragDropEvent* event) override;
    void dragMoveEvent(QGraphicsSceneDragDropEvent* event) override;
    void dropEvent(QGraphicsSceneDragDropEvent* event) override;
    void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override;

private slots:
    void onModelReset();
    void onSegmentAdded(int row);
    void onSegmentRemoved(int row);
    void onSegmentUpdated(int row);

private:
    SegmentItem* findItem(const QString& segId) const;
    double estimateDurationSec(int row) const;

    SegmentsModel* m_segModel  = nullptr;
    PointsModel*   m_ptModel   = nullptr;
    PlayheadItem*  m_playhead   = nullptr;
    double         m_pps        = 100.0;
    QUndoStack*    m_undoStack  = nullptr;
    QList<TimelineSegment> m_clipboard;
};
