// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Points Model
// ═══════════════════════════════════════════════════════════════════════════════

#include "models/points_model.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QIcon>
#include <QPixmap>

PointsModel::PointsModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int PointsModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_points.size();
}

QVariant PointsModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_points.size())
        return {};

    const CameraPoint& pt = m_points[index.row()];

    switch (role) {
    case Qt::DisplayRole:
    case NameRole:       return pt.name;
    case IdRole:         return pt.id;
    case Qt::DecorationRole:
    case ThumbnailRole:  return QPixmap::fromImage(pt.thumbnail);
    case RecordedRole:   return pt.recorded;
    default:             return {};
    }
}

bool PointsModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (!index.isValid() || index.row() >= m_points.size())
        return false;

    if (role == Qt::EditRole || role == NameRole) {
        m_points[index.row()].name = value.toString();
        emit dataChanged(index, index, {role});
        return true;
    }
    return false;
}

Qt::ItemFlags PointsModel::flags(const QModelIndex& index) const
{
    Qt::ItemFlags f = QAbstractListModel::flags(index);
    if (index.isValid())
        f |= Qt::ItemIsDragEnabled | Qt::ItemIsEditable;
    return f;
}

QHash<int, QByteArray> PointsModel::roleNames() const
{
    return {
        {IdRole,        "pointId"},
        {NameRole,      "pointName"},
        {ThumbnailRole, "thumbnail"},
        {RecordedRole,  "recorded"}
    };
}

// ─── Drag & Drop ────────────────────────────────────────────────────────────────

Qt::DropActions PointsModel::supportedDragActions() const
{
    return Qt::CopyAction;
}

QStringList PointsModel::mimeTypes() const
{
    return {"application/x-cambot-point"};
}

QMimeData* PointsModel::mimeData(const QModelIndexList& indexes) const
{
    if (indexes.isEmpty()) return nullptr;

    const CameraPoint& pt = m_points[indexes.first().row()];
    QMimeData* mime = new QMimeData();
    mime->setData("application/x-cambot-point", pt.id.toUtf8());
    return mime;
}

// ─── Point Management ───────────────────────────────────────────────────────────

void PointsModel::addPoint(const CameraPoint& point)
{
    beginInsertRows(QModelIndex(), m_points.size(), m_points.size());
    m_points.append(point);
    endInsertRows();
}

void PointsModel::removePoint(int row)
{
    if (row < 0 || row >= m_points.size()) return;
    beginRemoveRows(QModelIndex(), row, row);
    m_points.removeAt(row);
    endRemoveRows();
}

void PointsModel::updatePoint(int row, const CameraPoint& point)
{
    if (row < 0 || row >= m_points.size()) return;
    m_points[row] = point;
    emit dataChanged(index(row), index(row));
}

CameraPoint PointsModel::pointAt(int row) const
{
    if (row < 0 || row >= m_points.size()) return {};
    return m_points[row];
}

CameraPoint PointsModel::pointById(const QString& id) const
{
    for (const auto& pt : m_points)
        if (pt.id == id) return pt;
    return {};
}

int PointsModel::indexOf(const QString& id) const
{
    for (int i = 0; i < m_points.size(); ++i)
        if (m_points[i].id == id) return i;
    return -1;
}

void PointsModel::setPoints(const QList<CameraPoint>& points)
{
    beginResetModel();
    m_points = points;
    endResetModel();
}

void PointsModel::clear()
{
    beginResetModel();
    m_points.clear();
    endResetModel();
}
