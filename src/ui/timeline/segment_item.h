#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Segment Item (QGraphicsItem)
// Visual block on the timeline representing one TimelineSegment.
// Color-coded by move type. Draggable along X (time axis).
// ═══════════════════════════════════════════════════════════════════════════════

#include <QGraphicsRectItem>
#include <QGraphicsSceneMouseEvent>
#include "core/types.h"

class SegmentItem : public QGraphicsRectItem
{
public:
    explicit SegmentItem(const TimelineSegment& seg,
                         const QString& pointName,
                         double pixelsPerSecond,
                         double durationSec = -1.0,
                         QGraphicsItem* parent = nullptr);

    /// Update from model data. durationSec < 0 falls back to the fixed
    /// cosmetic default (used when no estimate could be computed, e.g. the
    /// referenced point is missing).
    void updateFromSegment(const TimelineSegment& seg,
                           const QString& pointName,
                           double pixelsPerSecond,
                           double durationSec = -1.0);

    QString segmentId() const { return m_segId; }
    double  triggerTime() const { return m_triggerTime; }

    enum { Type = QGraphicsItem::UserType + 1 };
    int type() const override { return Type; }

protected:
    void paint(QPainter* painter,
               const QStyleOptionGraphicsItem* option,
               QWidget* widget) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;

private:
    QColor typeColor(TimelineSegment::Type t) const;

    QString                m_segId;
    QString                m_pointName;
    double                 m_triggerTime = 0.0;
    double                 m_pps        = 100.0;
    TimelineSegment::Type  m_moveType   = TimelineSegment::MovJ;
    int                    m_speedPct   = 80;
    bool                   m_enabled    = true;
    bool                   m_pauseAfter = false;
    bool                   m_hovered    = false;
    QPointF                m_dragStart;
};
