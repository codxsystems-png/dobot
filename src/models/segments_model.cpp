// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Segments Model
// ═══════════════════════════════════════════════════════════════════════════════

#include "models/segments_model.h"
#include "models/points_model.h"
#include "core/motion_estimator.h"
#include <QMimeData>
#include <QUuid>
#include <algorithm>

SegmentsModel::SegmentsModel(PointsModel* pointsModel, QObject* parent)
    : QAbstractListModel(parent)
    , m_pointsModel(pointsModel)
{
}

int SegmentsModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_segments.size();
}

QVariant SegmentsModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_segments.size())
        return {};

    const auto& seg = m_segments[index.row()];

    switch (role) {
    case Qt::DisplayRole: {
        // Show point name if available
        if (m_pointsModel) {
            CameraPoint pt = m_pointsModel->pointById(seg.pointId);
            if (!pt.id.isEmpty())
                return pt.name;
        }
        return seg.pointId.left(8);
    }
    case IdRole:          return seg.id;
    case PointIdRole:     return seg.pointId;
    case TriggerTimeRole: return seg.triggerTime;
    case TypeRole:        return static_cast<int>(seg.type);
    case SpeedRole:       return seg.speedPct;
    case AccRole:         return seg.accPct;
    case CpRole:          return seg.cpValue;
    case PreWaitRole:     return seg.preWait;
    case PostWaitRole:    return seg.postWait;
    case CamTriggerRole:  return static_cast<int>(seg.camTrigger);
    default:              return {};
    }
}

bool SegmentsModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (!index.isValid() || index.row() >= m_segments.size())
        return false;

    auto& seg = m_segments[index.row()];

    switch (role) {
    case TriggerTimeRole: seg.triggerTime = value.toDouble(); break;
    case TypeRole:        seg.type = static_cast<TimelineSegment::Type>(value.toInt()); break;
    case SpeedRole:       seg.speedPct = qBound(1, value.toInt(), 100); break;
    case AccRole:         seg.accPct = qBound(1, value.toInt(), 100); break;
    case CpRole:          seg.cpValue = qMax(0.0, value.toDouble()); break;
    case PreWaitRole:     seg.preWait = qMax(0.0, value.toDouble()); break;
    case PostWaitRole:    seg.postWait = qMax(0.0, value.toDouble()); break;
    case CamTriggerRole:  seg.camTrigger = static_cast<TimelineSegment::CamTrigger>(value.toInt()); break;
    default: return false;
    }

    emit dataChanged(index, index, {role});
    emit segmentUpdated(index.row());
    return true;
}

Qt::ItemFlags SegmentsModel::flags(const QModelIndex& index) const
{
    Qt::ItemFlags f = QAbstractListModel::flags(index);
    if (index.isValid())
        f |= Qt::ItemIsEditable;
    f |= Qt::ItemIsDropEnabled;
    return f;
}

// ─── Drop from Points Library ───────────────────────────────────────────────────

QStringList SegmentsModel::mimeTypes() const
{
    return {"application/x-cambot-point"};
}

Qt::DropActions SegmentsModel::supportedDropActions() const
{
    return Qt::CopyAction;
}

bool SegmentsModel::canDropMimeData(const QMimeData* data, Qt::DropAction action,
                                     int row, int column, const QModelIndex& parent) const
{
    Q_UNUSED(action) Q_UNUSED(row) Q_UNUSED(column) Q_UNUSED(parent)
    return data && data->hasFormat("application/x-cambot-point");
}

bool SegmentsModel::dropMimeData(const QMimeData* data, Qt::DropAction action,
                                  int row, int column, const QModelIndex& parent)
{
    Q_UNUSED(action) Q_UNUSED(column) Q_UNUSED(parent)

    if (!data || !data->hasFormat("application/x-cambot-point"))
        return false;

    QString pointId = QString::fromUtf8(data->data("application/x-cambot-point"));

    TimelineSegment seg;
    seg.id        = QUuid::createUuid().toString(QUuid::WithoutBraces);
    seg.pointId   = pointId;
    seg.type      = TimelineSegment::MovJ;
    seg.speedPct  = 80;
    seg.accPct    = 50;

    // Auto-position: append at end, floored by the gantry's physically
    // minimum feasible move time (falls back to the naive 2s gap when the
    // motor spec isn't configured or either point can't be resolved).
    if (!m_segments.isEmpty()) {
        constexpr double kNaiveGapSec = 2.0;
        double minGap = kNaiveGapSec;
        if (m_pointsModel) {
            CameraPoint fromPt = m_pointsModel->pointById(m_segments.last().pointId);
            CameraPoint toPt   = m_pointsModel->pointById(pointId);
            if (!fromPt.id.isEmpty() && !toPt.id.isEmpty()) {
                minGap = qMax(kNaiveGapSec, motion::minGantryDurationSec(fromPt, toPt, m_gantryMotorSpec, kNaiveGapSec));
            }
        }
        seg.triggerTime = m_segments.last().triggerTime + minGap;
    } else {
        seg.triggerTime = 0.0;
    }

    if (row >= 0 && row < m_segments.size())
        seg.triggerTime = m_segments[row].triggerTime;

    addSegment(seg);
    return true;
}

// ─── Segment Management ────────────────────────────────────────────────────────

void SegmentsModel::addSegment(const TimelineSegment& seg)
{
    beginInsertRows(QModelIndex(), m_segments.size(), m_segments.size());
    m_segments.append(seg);
    endInsertRows();
    sortByTime();
    emit segmentAdded(m_segments.size() - 1);
}

void SegmentsModel::removeSegment(int row)
{
    if (row < 0 || row >= m_segments.size()) return;
    beginRemoveRows(QModelIndex(), row, row);
    m_segments.removeAt(row);
    endRemoveRows();
    emit segmentRemoved(row);
}

void SegmentsModel::updateSegment(int row, const TimelineSegment& seg)
{
    if (row < 0 || row >= m_segments.size()) return;
    m_segments[row] = seg;
    emit dataChanged(index(row), index(row));
    emit segmentUpdated(row);
}

TimelineSegment SegmentsModel::segmentAt(int row) const
{
    if (row < 0 || row >= m_segments.size()) return {};
    return m_segments[row];
}

int SegmentsModel::indexOf(const QString& id) const
{
    for (int i = 0; i < m_segments.size(); ++i)
        if (m_segments[i].id == id) return i;
    return -1;
}

void SegmentsModel::sortByTime()
{
    emit layoutAboutToBeChanged();
    std::sort(m_segments.begin(), m_segments.end(),
              [](const TimelineSegment& a, const TimelineSegment& b) {
                  return a.triggerTime < b.triggerTime;
              });
    emit layoutChanged();
}

double SegmentsModel::totalDuration() const
{
    double maxTime = 0.0;
    for (const auto& seg : m_segments)
        maxTime = qMax(maxTime, seg.triggerTime + seg.postWait + 2.0); // +2s padding
    return maxTime;
}

void SegmentsModel::setSegments(const QList<TimelineSegment>& segs)
{
    beginResetModel();
    m_segments = segs;
    endResetModel();
}

void SegmentsModel::clear()
{
    beginResetModel();
    m_segments.clear();
    endResetModel();
}
