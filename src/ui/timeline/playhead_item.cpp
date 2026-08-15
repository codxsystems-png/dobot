// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Playhead Item
// ═══════════════════════════════════════════════════════════════════════════════

#include "ui/timeline/playhead_item.h"
#include <QGraphicsSceneMouseEvent>
#include <QCursor>
#include <QPen>

PlayheadItem::PlayheadItem(double sceneHeight, QGraphicsItem* parent)
    : QGraphicsLineItem(parent)
{
    setPen(QPen(QColor(220, 50, 50), 2));
    setLine(0, 0, 0, sceneHeight);
    setFlags(ItemIsSelectable);
    setCursor(Qt::SizeHorCursor);
    setZValue(1000); // always on top
}

void PlayheadItem::setTime(double seconds, double pixelsPerSecond)
{
    m_time = qMax(0.0, seconds);
    m_pps  = pixelsPerSecond;
    setPos(m_time * m_pps, 0);
}

void PlayheadItem::setSceneHeight(double h)
{
    setLine(0, 0, 0, h);
}

void PlayheadItem::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        setCursor(Qt::ClosedHandCursor);
    QGraphicsLineItem::mousePressEvent(event);
}

void PlayheadItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->buttons() & Qt::LeftButton) {
        double newX = qMax(0.0, event->scenePos().x());
        double newTime = newX / m_pps;
        // Snap to 0.05s
        newTime = qRound(newTime * 20.0) / 20.0;
        m_time = newTime;
        setPos(m_time * m_pps, 0);
        emit timeDragged(m_time);
    }
}

void PlayheadItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    setCursor(Qt::SizeHorCursor);
    QGraphicsLineItem::mouseReleaseEvent(event);
}
