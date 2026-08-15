// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Timeline Ruler
// ═══════════════════════════════════════════════════════════════════════════════

#include "ui/timeline/timeline_ruler.h"
#include <QPainter>
#include <QMouseEvent>
#include <cmath>

TimelineRuler::TimelineRuler(QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(28);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
}

void TimelineRuler::setPixelsPerSecond(double pps)
{
    m_pps = qMax(10.0, pps);
    update();
}

void TimelineRuler::setDuration(double seconds)
{
    m_duration = qMax(1.0, seconds);
    setMinimumWidth(static_cast<int>(m_duration * m_pps) + 100);
    update();
}

void TimelineRuler::setPlayheadTime(double seconds)
{
    m_playheadTime = qBound(0.0, seconds, m_duration);
    update();
}

void TimelineRuler::setScrollOffset(int px)
{
    m_scrollOffset = px;
    update();
}

double TimelineRuler::timeAtX(int x) const
{
    return qBound(0.0, (x + m_scrollOffset) / m_pps, m_duration);
}

void TimelineRuler::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    int w = width();
    int h = height();

    // Background
    p.fillRect(rect(), QColor(30, 30, 30));

    // Bottom line
    p.setPen(QColor(80, 80, 80));
    p.drawLine(0, h - 1, w, h - 1);

    // Determine tick interval based on zoom
    double tickInterval = 1.0; // seconds
    if (m_pps < 30)       tickInterval = 5.0;
    else if (m_pps < 60)  tickInterval = 2.0;
    else if (m_pps > 200) tickInterval = 0.5;

    double subTick = tickInterval / 5.0;

    // Draw ticks
    QFont font("Consolas", 8);
    p.setFont(font);

    double startTime = m_scrollOffset / m_pps;
    double endTime   = (m_scrollOffset + w) / m_pps;

    // Sub-ticks
    p.setPen(QColor(60, 60, 60));
    for (double t = std::floor(startTime / subTick) * subTick; t <= endTime; t += subTick) {
        int x = static_cast<int>(t * m_pps) - m_scrollOffset;
        p.drawLine(x, h - 5, x, h - 1);
    }

    // Major ticks + labels
    p.setPen(QColor(180, 180, 180));
    for (double t = std::floor(startTime / tickInterval) * tickInterval; t <= endTime; t += tickInterval) {
        if (t < 0) continue;
        int x = static_cast<int>(t * m_pps) - m_scrollOffset;

        // Tick
        p.drawLine(x, h - 12, x, h - 1);

        // Label
        int mins = static_cast<int>(t) / 60;
        int secs = static_cast<int>(t) % 60;
        int frac = static_cast<int>((t - std::floor(t)) * 10);
        QString label;
        if (mins > 0)
            label = QString("%1:%2").arg(mins).arg(secs, 2, 10, QChar('0'));
        else if (frac > 0)
            label = QString("%1.%2s").arg(secs).arg(frac);
        else
            label = QString("%1s").arg(secs);

        p.drawText(x + 3, h - 14, label);
    }

    // Playhead marker (red triangle)
    int phx = static_cast<int>(m_playheadTime * m_pps) - m_scrollOffset;
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(220, 50, 50));
    QPolygon tri;
    tri << QPoint(phx - 5, 0) << QPoint(phx + 5, 0) << QPoint(phx, 8);
    p.drawPolygon(tri);

    // Playhead line
    p.setPen(QPen(QColor(220, 50, 50), 1));
    p.drawLine(phx, 8, phx, h);
}

void TimelineRuler::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        double t = timeAtX(event->pos().x());
        setPlayheadTime(t);
        emit timeClicked(t);
    }
}

void TimelineRuler::mouseMoveEvent(QMouseEvent* event)
{
    if (event->buttons() & Qt::LeftButton) {
        double t = timeAtX(event->pos().x());
        setPlayheadTime(t);
        emit timeClicked(t);
    }
}
