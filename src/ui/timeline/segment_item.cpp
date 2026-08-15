// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Segment Item
// ═══════════════════════════════════════════════════════════════════════════════

#include "ui/timeline/segment_item.h"
#include "ui/timeline/timeline_scene.h"
#include <QPainter>
#include <QCursor>
#include <QGraphicsScene>

static constexpr double SEGMENT_HEIGHT  = 48.0;
static constexpr double MIN_WIDTH       = 30.0;
static constexpr double DEFAULT_WIDTH_S = 1.5; // default visual width in seconds

SegmentItem::SegmentItem(const TimelineSegment& seg,
                         const QString& pointName,
                         double pixelsPerSecond,
                         double durationSec,
                         QGraphicsItem* parent)
    : QGraphicsRectItem(parent)
{
    setAcceptHoverEvents(true);
    setFlags(ItemIsSelectable | ItemSendsGeometryChanges);
    setCursor(Qt::OpenHandCursor);
    updateFromSegment(seg, pointName, pixelsPerSecond, durationSec);
}

void SegmentItem::updateFromSegment(const TimelineSegment& seg,
                                     const QString& pointName,
                                     double pixelsPerSecond,
                                     double durationSec)
{
    m_segId       = seg.id;
    m_pointName   = pointName;
    m_triggerTime = seg.triggerTime;
    m_pps         = pixelsPerSecond;
    m_moveType    = seg.type;
    m_speedPct    = seg.speedPct;
    m_enabled     = seg.enabled;
    m_pauseAfter  = seg.pauseAfter;

    double widthSec = durationSec > 0.0 ? durationSec : DEFAULT_WIDTH_S;
    double w = qMax(MIN_WIDTH, widthSec * m_pps);
    double x = m_triggerTime * m_pps;

    setRect(0, 0, w, SEGMENT_HEIGHT);
    setPos(x, 10); // 10px top margin in scene
    setToolTip(QString("%1\n%2 | Speed: %3%\nTime: %4s%5%6")
        .arg(m_pointName)
        .arg(m_moveType == TimelineSegment::MovJ ? "MovJ" :
             m_moveType == TimelineSegment::MovL ? "MovL" : "Arc")
        .arg(m_speedPct)
        .arg(m_triggerTime, 0, 'f', 2)
        .arg(m_enabled ? "" : "\n(Disabled — skipped during playback)")
        .arg(m_pauseAfter ? "\n(Pauses playback after this move)" : ""));

    update();
}

QColor SegmentItem::typeColor(TimelineSegment::Type t) const
{
    switch (t) {
    case TimelineSegment::MovJ: return QColor(0, 120, 212);    // blue
    case TimelineSegment::MovL: return QColor(0, 180, 80);     // green
    case TimelineSegment::Arc:  return QColor(200, 120, 0);    // orange
    default:                    return QColor(100, 100, 100);
    }
}

void SegmentItem::paint(QPainter* painter,
                         const QStyleOptionGraphicsItem* option,
                         QWidget* widget)
{
    Q_UNUSED(option) Q_UNUSED(widget)

    QRectF r = rect();
    QColor base = typeColor(m_moveType);

    // Hover / selection highlight
    if (isSelected())
        base = base.lighter(140);
    else if (m_hovered)
        base = base.lighter(120);

    if (!m_enabled) {
        base = base.darker(200); // muted step — visibly desaturated/dimmed
    }

    // Body gradient
    QLinearGradient grad(0, 0, 0, r.height());
    grad.setColorAt(0, base.lighter(115));
    grad.setColorAt(1, base.darker(110));

    painter->setRenderHint(QPainter::Antialiasing);
    QPen outlinePen(base.darker(130), 1.5);
    if (!m_enabled) outlinePen.setStyle(Qt::DashLine);
    painter->setPen(outlinePen);
    painter->setBrush(grad);
    painter->drawRoundedRect(r.adjusted(1, 1, -1, -1), 4, 4);

    // Point name
    painter->setPen(m_enabled ? Qt::white : QColor(200, 200, 200, 140));
    QFont f("Segoe UI", 9, QFont::Bold);
    painter->setFont(f);
    painter->drawText(r.adjusted(6, 4, -4, -SEGMENT_HEIGHT / 2),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      m_pointName);

    // Move type label
    QFont f2("Consolas", 7);
    painter->setFont(f2);
    painter->setPen(QColor(220, 220, 220, m_enabled ? 180 : 100));
    QString typeStr = m_moveType == TimelineSegment::MovJ ? "MovJ" :
                      m_moveType == TimelineSegment::MovL ? "MovL" : "Arc";
    painter->drawText(r.adjusted(6, SEGMENT_HEIGHT / 2, -4, -4),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      QString("%1 %2%").arg(typeStr).arg(m_speedPct));

    // Pause-after-move marker — a solid bar on the trailing edge.
    if (m_pauseAfter) {
        QRectF pauseBar(r.right() - 5, r.top() + 1, 4, r.height() - 2);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(255, 200, 0));
        painter->drawRect(pauseBar);
    }
}

void SegmentItem::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        setCursor(Qt::ClosedHandCursor);
        m_dragStart = event->scenePos();
    }
    QGraphicsRectItem::mousePressEvent(event);
}

void SegmentItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->buttons() & Qt::LeftButton) {
        double dx = event->scenePos().x() - m_dragStart.x();
        double newTime = qMax(0.0, m_triggerTime + dx / m_pps);

        // Snap to 0.1s grid
        newTime = qRound(newTime * 10.0) / 10.0;

        // Snap to a nearby OTHER segment's time, if closer than the grid
        // would already place it — makes lining two points up trivial
        // instead of fiddly pixel-hunting.
        if (auto* ts = qobject_cast<TimelineScene*>(scene())) {
            static constexpr double kSnapThresholdSec = 0.15;
            double snapped = ts->nearestOtherSegmentTime(m_segId, newTime, kSnapThresholdSec);
            if (snapped >= 0.0) newTime = snapped;
        }

        setPos(newTime * m_pps, pos().y());
    }
}

void SegmentItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    setCursor(Qt::OpenHandCursor);

    if (event->button() == Qt::LeftButton) {
        // Commit new time — mouseMoveEvent already applied grid/segment
        // snapping continuously, so pos() already reflects the final,
        // fully-snapped location. Re-snapping to the grid here would undo
        // a segment-snap that landed off-grid.
        double newTime = qMax(0.0, pos().x() / m_pps);
        m_triggerTime = newTime;

        // Persist to the model — without this the drag only ever moved the
        // item visually and got silently discarded on the next rebuild.
        if (auto* ts = qobject_cast<TimelineScene*>(scene())) {
            ts->commitSegmentMove(m_segId, newTime);
        }
    }

    QGraphicsRectItem::mouseReleaseEvent(event);
}

void SegmentItem::hoverEnterEvent(QGraphicsSceneHoverEvent* event)
{
    Q_UNUSED(event)
    m_hovered = true;
    update();
}

void SegmentItem::hoverLeaveEvent(QGraphicsSceneHoverEvent* event)
{
    Q_UNUSED(event)
    m_hovered = false;
    update();
}
