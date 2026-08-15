#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Timeline Ruler
// Draws time ticks, labels, and playhead position marker above the scene.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QWidget>

class TimelineRuler : public QWidget
{
    Q_OBJECT
public:
    explicit TimelineRuler(QWidget* parent = nullptr);

    void setPixelsPerSecond(double pps);
    double pixelsPerSecond() const { return m_pps; }

    void setDuration(double seconds);
    double duration() const { return m_duration; }

    void setPlayheadTime(double seconds);
    double playheadTime() const { return m_playheadTime; }

    void setScrollOffset(int px);

signals:
    void timeClicked(double seconds);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    double timeAtX(int x) const;

    double m_pps          = 100.0;   // pixels per second
    double m_duration     = 30.0;
    double m_playheadTime = 0.0;
    int    m_scrollOffset = 0;
};
