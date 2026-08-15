#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Points Model
// QAbstractListModel with thumbnail, name, joint/cartesian data.
// Supports drag-and-drop mime data for timeline.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QAbstractListModel>
#include <QMimeData>
#include "core/types.h"

class PointsModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles {
        IdRole        = Qt::UserRole + 1,
        NameRole,
        ThumbnailRole,
        JointsRole,
        PoseRole,
        FizStateRole,
        RecordedRole
    };

    explicit PointsModel(QObject* parent = nullptr);

    // QAbstractListModel overrides
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Drag support
    Qt::DropActions supportedDragActions() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    QStringList mimeTypes() const override;

    // Point management
    void addPoint(const CameraPoint& point);
    void removePoint(int row);
    void updatePoint(int row, const CameraPoint& point);
    CameraPoint pointAt(int row) const;
    CameraPoint pointById(const QString& id) const;
    int indexOf(const QString& id) const;

    /// Get all points (for project save)
    QList<CameraPoint> allPoints() const { return m_points; }

    /// Set all points (for project load)
    void setPoints(const QList<CameraPoint>& points);

    /// Clear all
    void clear();

private:
    QList<CameraPoint> m_points;
};
