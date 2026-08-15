// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — FIZ Track Widget
// ═══════════════════════════════════════════════════════════════════════════════

#include "presentation/widgets/fiztrackwidget.h"
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>

FizTrackWidget::FizTrackWidget(QWidget* parent)
    : QWidget(parent)
{
    m_tracks = {
        {"FOCUS", QColor(0x99, 0x44, 0xFF), 0},
        {"IRIS",  QColor(0x44, 0xAA, 0x44), TRACK_HEIGHT},
        {"ZOOM",  QColor(0x44, 0x88, 0xFF), TRACK_HEIGHT * 2},
        {"GANTRY",QColor(0xFF, 0x66, 0x33), TRACK_HEIGHT * 3}
    };
    setMinimumHeight(TRACK_HEIGHT * 4);
    setFixedHeight(TRACK_HEIGHT * 4);
    setFocusPolicy(Qt::StrongFocus);
}

void FizTrackWidget::setPixelsPerSecond(double pps) { m_pps = qMax(10.0, pps); update(); }
void FizTrackWidget::setScrollOffset(int px) { m_scrollOffset = px; update(); }
void FizTrackWidget::setDuration(double s) { m_duration = qMax(1.0, s); update(); }
void FizTrackWidget::setKeyframes(const QList<FizKeyframe>& kfs) { m_keyframes = kfs; update(); }
void FizTrackWidget::setGantryKeyframes(const QList<GantryKeyframe>& kfs) { m_gantryKeyframes = kfs; update(); }
void FizTrackWidget::setPlayheadTime(double s) { m_playheadTime = s; update(); }

double FizTrackWidget::timeAtX(int x) const { return qBound(0.0, (x + m_scrollOffset) / m_pps, m_duration); }
float  FizTrackWidget::valueAtY(int y, int trackIndex) const {
    int top = m_tracks[trackIndex].yOffset;
    return 100.0f - std::clamp(static_cast<float>(y - top) / TRACK_HEIGHT, 0.0f, 1.0f) * 100.0f;
}
int FizTrackWidget::trackAtY(int y) const { return std::clamp(y / TRACK_HEIGHT, 0, 3); }

void FizTrackWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    int w = width();

    // Draw each track
    for (int t = 0; t < 4; ++t) {
        int yOff = m_tracks[t].yOffset;
        QColor col = m_tracks[t].color;

        // Background
        p.fillRect(0, yOff, w, TRACK_HEIGHT, QColor(20, 20, 20));

        // Track label
        p.setPen(col.darker(120));
        p.setFont(QFont("Consolas", 8, QFont::Bold));
        p.drawText(4, yOff + 14, m_tracks[t].name);

        // Border
        p.setPen(QPen(QColor(45, 45, 45), 1));
        p.drawLine(0, yOff + TRACK_HEIGHT - 1, w, yOff + TRACK_HEIGHT - 1);
    }

    // Draw keyframe diamonds and connection curves
    auto valueForTrack = [](const FizState& s, int t) -> float {
        if (t == 0) return s.focus;
        if (t == 1) return s.iris;
        return s.zoom;
    };

    auto gantryValToPct = [](double pos) {
        // Map 0 - 180 degrees (or 0-100%) to percentage
        return std::clamp(static_cast<float>(pos) / 1.8f, 0.0f, 100.0f);
    };

    for (int t = 0; t < 4; ++t) {
        int yOff = m_tracks[t].yOffset;
        QColor col = m_tracks[t].color;
        p.setPen(QPen(col, 1.5));

        // Draw lines between keyframes
        QPointF prev;
        bool hasPrev = false;
        
        if (t < 3) {
            // FIZ tracks
            for (const auto& kf : m_keyframes) {
                float val = valueForTrack(kf.state, t);
                int x = static_cast<int>(kf.time * m_pps) - m_scrollOffset;
                int y = yOff + static_cast<int>((1.0f - val / 100.0f) * (TRACK_HEIGHT - 8)) + 4;
                QPointF pt(x, y);

                if (hasPrev) p.drawLine(prev, pt);
                prev = pt;
                hasPrev = true;
            }
        } else {
            // Gantry track
            for (const auto& kf : m_gantryKeyframes) {
                float val = gantryValToPct(kf.positionMm);
                int x = static_cast<int>(kf.time * m_pps) - m_scrollOffset;
                int y = yOff + static_cast<int>((1.0f - val / 100.0f) * (TRACK_HEIGHT - 8)) + 4;
                QPointF pt(x, y);

                if (hasPrev) p.drawLine(prev, pt);
                prev = pt;
                hasPrev = true;
            }
        }

        // Draw diamonds — selected one gets a white outline so it's clear
        // what Delete will remove / what's currently being dragged.
        if (t < 3) {
            for (const auto& kf : m_keyframes) {
                float val = valueForTrack(kf.state, t);
                int x = static_cast<int>(kf.time * m_pps) - m_scrollOffset;
                int y = yOff + static_cast<int>((1.0f - val / 100.0f) * (TRACK_HEIGHT - 8)) + 4;

                QPolygon diamond;
                diamond << QPoint(x, y - 4) << QPoint(x + 4, y)
                        << QPoint(x, y + 4) << QPoint(x - 4, y);
                p.setBrush(col);
                p.setPen(kf.id == m_selectedFizId ? QPen(Qt::white, 2) : QPen(col.darker(150), 1));
                p.drawPolygon(diamond);
            }
        } else {
            for (const auto& kf : m_gantryKeyframes) {
                float val = gantryValToPct(kf.positionMm);
                int x = static_cast<int>(kf.time * m_pps) - m_scrollOffset;
                int y = yOff + static_cast<int>((1.0f - val / 100.0f) * (TRACK_HEIGHT - 8)) + 4;

                QPolygon diamond;
                diamond << QPoint(x, y - 4) << QPoint(x + 4, y)
                        << QPoint(x, y + 4) << QPoint(x - 4, y);
                p.setBrush(col);
                p.setPen(kf.id == m_selectedGantryId ? QPen(Qt::white, 2) : QPen(col.darker(150), 1));
                p.drawPolygon(diamond);
            }
        }
    }

    // Playhead
    int phx = static_cast<int>(m_playheadTime * m_pps) - m_scrollOffset;
    p.setPen(QPen(QColor(220, 50, 50), 1));
    p.drawLine(phx, 0, phx, height());
}

int FizTrackWidget::findFizKeyframeAt(int x) const
{
    for (int i = 0; i < m_keyframes.size(); ++i) {
        int kx = static_cast<int>(m_keyframes[i].time * m_pps) - m_scrollOffset;
        if (qAbs(x - kx) < 8) return i;
    }
    return -1;
}

int FizTrackWidget::findGantryKeyframeAt(int x) const
{
    for (int i = 0; i < m_gantryKeyframes.size(); ++i) {
        int kx = static_cast<int>(m_gantryKeyframes[i].time * m_pps) - m_scrollOffset;
        if (qAbs(x - kx) < 8) return i;
    }
    return -1;
}

void FizTrackWidget::mousePressEvent(QMouseEvent* event)
{
    setFocus(Qt::MouseFocusReason);

    if (event->button() == Qt::LeftButton) {
        int track = trackAtY(event->pos().y());

        if (track < 3) {
            int idx = findFizKeyframeAt(event->pos().x());
            if (idx >= 0) {
                m_selectedFizId = m_keyframes[idx].id;
                m_selectedGantryId.clear();
                m_dragging = true;
                emit keyframeSelected(m_keyframes[idx]);
                update();
                return;
            }
        } else {
            int idx = findGantryKeyframeAt(event->pos().x());
            if (idx >= 0) {
                m_selectedGantryId = m_gantryKeyframes[idx].id;
                m_selectedFizId.clear();
                m_dragging = true;
                emit gantryKeyframeSelected(m_gantryKeyframes[idx]);
                update();
                return;
            }
        }

        // Clicked empty space — clear selection.
        m_selectedFizId.clear();
        m_selectedGantryId.clear();
        update();
    }
    QWidget::mousePressEvent(event);
}

void FizTrackWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_dragging || !(event->buttons() & Qt::LeftButton)) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    double newTime = qMax(0.0, timeAtX(event->pos().x()));
    // Snap to 0.1s grid, matching the segment-block drag on the main timeline.
    newTime = qRound(newTime * 10.0) / 10.0;

    if (!m_selectedFizId.isEmpty()) {
        for (auto& kf : m_keyframes) {
            if (kf.id == m_selectedFizId) { kf.time = newTime; break; }
        }
    } else if (!m_selectedGantryId.isEmpty()) {
        for (auto& kf : m_gantryKeyframes) {
            if (kf.id == m_selectedGantryId) { kf.time = newTime; break; }
        }
    }
    update();
}

void FizTrackWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        if (!m_selectedFizId.isEmpty()) {
            for (const auto& kf : m_keyframes) {
                if (kf.id == m_selectedFizId) { emit fizKeyframeMoved(kf); break; }
            }
        } else if (!m_selectedGantryId.isEmpty()) {
            for (const auto& kf : m_gantryKeyframes) {
                if (kf.id == m_selectedGantryId) { emit gantryKeyframeMoved(kf); break; }
            }
        }
    }
    QWidget::mouseReleaseEvent(event);
}

void FizTrackWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    int track = trackAtY(event->pos().y());
    double time = timeAtX(event->pos().x());
    float val = valueAtY(event->pos().y(), track);
    if (track < 3) {
        emit addKeyframeRequested(track, time, val);
    } else {
        // Un-map value to mm
        float mm = val * 10.0f;
        emit addGantryKeyframeRequested(time, mm);
    }
}

void FizTrackWidget::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        if (!m_selectedFizId.isEmpty()) {
            emit fizKeyframeDeleteRequested(m_selectedFizId);
            m_selectedFizId.clear();
            update();
            return;
        }
        if (!m_selectedGantryId.isEmpty()) {
            emit gantryKeyframeDeleteRequested(m_selectedGantryId);
            m_selectedGantryId.clear();
            update();
            return;
        }
    }
    QWidget::keyPressEvent(event);
}
