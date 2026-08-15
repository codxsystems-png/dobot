#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Playhead Item (QGraphicsItem)
// Vertical red line draggable along X axis. Emits time changes.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QGraphicsLineItem>
#include <QObject>

class PlayheadItem : public QObject, public QGraphicsLineItem
{
    Q_OBJECT
public:
    explicit PlayheadItem(double sceneHeight, QGraphicsItem* parent = nullptr);

    void setTime(double seconds, double pixelsPerSecond);
    double time() const { return m_time; }

    void setSceneHeight(double h);

    enum { Type = QGraphicsItem::UserType + 2 };
    int type() const override { return Type; }

signals:
    void timeDragged(double seconds);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

private:
    double m_time = 0.0;
    double m_pps  = 100.0;
};
