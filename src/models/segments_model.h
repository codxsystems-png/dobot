#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Segments Model
// QAbstractListModel for timeline segments with ordering by triggerTime.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QAbstractListModel>
#include "core/types.h"

class PointsModel;

class SegmentsModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles {
        IdRole         = Qt::UserRole + 1,
        PointIdRole,
        TriggerTimeRole,
        TypeRole,
        SpeedRole,
        AccRole,
        CpRole,
        PreWaitRole,
        PostWaitRole,
        CamTriggerRole
    };

    explicit SegmentsModel(PointsModel* pointsModel, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    // Drop support (from Points Library)
    bool canDropMimeData(const QMimeData* data, Qt::DropAction action,
                         int row, int column, const QModelIndex& parent) const override;
    bool dropMimeData(const QMimeData* data, Qt::DropAction action,
                      int row, int column, const QModelIndex& parent) override;
    QStringList mimeTypes() const override;
    Qt::DropActions supportedDropActions() const override;

    // Segment management
    void addSegment(const TimelineSegment& seg);
    void removeSegment(int row);
    void updateSegment(int row, const TimelineSegment& seg);
    TimelineSegment segmentAt(int row) const;
    int indexOf(const QString& id) const;

    /// Re-sort segments by triggerTime
    void sortByTime();

    /// Get total timeline duration (max triggerTime + postWait)
    double totalDuration() const;

    /// Get all segments (for project save)
    QList<TimelineSegment> allSegments() const { return m_segments; }

    /// Set all segments (for project load)
    void setSegments(const QList<TimelineSegment>& segs);

    void clear();

    /// Gantry motor spec used to auto-compute feasible trigger times on
    /// new-segment creation (see dropMimeData). Pushed in by MainWindow on
    /// project load / Gantry Motor Setup changes.
    void setGantryMotorSpec(const GantryMotorSpec& spec) { m_gantryMotorSpec = spec; }
    GantryMotorSpec gantryMotorSpec() const { return m_gantryMotorSpec; }

signals:
    void segmentAdded(int row);
    void segmentRemoved(int row);
    void segmentUpdated(int row);

private:
    QList<TimelineSegment> m_segments;
    PointsModel*           m_pointsModel = nullptr;
    GantryMotorSpec        m_gantryMotorSpec;
};
