// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — FIZ / Axis Track Widget
// ═══════════════════════════════════════════════════════════════════════════════

#include "presentation/widgets/fiztrackwidget.h"
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <algorithm>

namespace {
constexpr const char* kPrimaryAxisId = "gantry";
}

FizTrackWidget::FizTrackWidget(QWidget* parent)
    : QWidget(parent)
{
    // One axis row by default, so a project that has never been through axis
    // setup looks exactly as it always did.
    m_axes = { AxisRow{ kPrimaryAxisId, "GANTRY", 0.0, 100.0, QColor(0xFF, 0x66, 0x33) } };
    rebuildRows();
    setFocusPolicy(Qt::StrongFocus);
}

void FizTrackWidget::rebuildRows()
{
    m_tracks.clear();

    const QColor fizColors[FIZ_ROWS] = {
        QColor(0x99, 0x44, 0xFF),   // FOCUS
        QColor(0x44, 0xAA, 0x44),   // IRIS
        QColor(0x44, 0x88, 0xFF)    // ZOOM
    };
    const char* fizNames[FIZ_ROWS] = { "FOCUS", "IRIS", "ZOOM" };

    for (int i = 0; i < FIZ_ROWS; ++i) {
        TrackInfo t;
        t.kind     = TrackInfo::Kind::Fiz;
        t.name     = fizNames[i];
        t.color    = fizColors[i];
        t.fizIndex = i;
        m_tracks.append(t);
    }

    for (const AxisRow& a : m_axes) {
        TrackInfo t;
        t.kind     = TrackInfo::Kind::Axis;
        t.name     = a.name.isEmpty() ? a.id.toUpper() : a.name;
        t.color    = a.color;
        t.axisId   = a.id;
        t.rangeMin = a.rangeMin;
        // A degenerate range would divide by zero and put every keyframe on one
        // pixel row; fall back to a unit span rather than rendering nonsense.
        t.rangeMax = (a.rangeMax > a.rangeMin) ? a.rangeMax : a.rangeMin + 1.0;
        m_tracks.append(t);
    }

    relayout();
}

void FizTrackWidget::relayout()
{
    for (int i = 0; i < m_tracks.size(); ++i) {
        m_tracks[i].yOffset = i * TRACK_HEIGHT;
    }
    const int h = qMax(1, m_tracks.size()) * TRACK_HEIGHT;
    setMinimumHeight(h);
    setFixedHeight(h);
    update();
}

void FizTrackWidget::setAxes(const QList<AxisRow>& axes)
{
    m_axes = axes;

    // Drop keyframes for axes that no longer exist, so a removed axis cannot
    // keep painting onto whichever row later takes its index.
    QHash<QString, QList<GantryKeyframe>> kept;
    for (const AxisRow& a : m_axes) {
        if (m_axisKeyframes.contains(a.id)) kept.insert(a.id, m_axisKeyframes.value(a.id));
    }
    m_axisKeyframes = kept;

    rebuildRows();
}

float FizTrackWidget::axisPosToPct(const TrackInfo& row, double pos) const
{
    const double span = row.rangeMax - row.rangeMin;
    return std::clamp(static_cast<float>((pos - row.rangeMin) / span) * 100.0f, 0.0f, 100.0f);
}

double FizTrackWidget::axisPctToPos(const TrackInfo& row, float pct) const
{
    const double span = row.rangeMax - row.rangeMin;
    return row.rangeMin + (std::clamp(pct, 0.0f, 100.0f) / 100.0) * span;
}

void FizTrackWidget::setPixelsPerSecond(double pps) { m_pps = qMax(10.0, pps); update(); }
void FizTrackWidget::setScrollOffset(int px) { m_scrollOffset = px; update(); }
void FizTrackWidget::setDuration(double s) { m_duration = qMax(1.0, s); update(); }
void FizTrackWidget::setKeyframes(const QList<FizKeyframe>& kfs) { m_keyframes = kfs; update(); }
void FizTrackWidget::setPlayheadTime(double s) { m_playheadTime = s; update(); }

void FizTrackWidget::setGantryKeyframes(const QList<GantryKeyframe>& kfs)
{
    setAxisKeyframes(kPrimaryAxisId, kfs);
}

void FizTrackWidget::setAxisKeyframes(const QString& axisId, const QList<GantryKeyframe>& kfs)
{
    m_axisKeyframes.insert(axisId, kfs);
    update();
}

double FizTrackWidget::timeAtX(int x) const
{
    return qBound(0.0, (x + m_scrollOffset) / m_pps, m_duration);
}

float FizTrackWidget::valueAtY(int y, int trackIndex) const
{
    if (trackIndex < 0 || trackIndex >= m_tracks.size()) return 0.0f;
    const int top = m_tracks[trackIndex].yOffset;
    return 100.0f - std::clamp(static_cast<float>(y - top) / TRACK_HEIGHT, 0.0f, 1.0f) * 100.0f;
}

int FizTrackWidget::trackAtY(int y) const
{
    if (m_tracks.isEmpty()) return 0;
    return std::clamp(y / TRACK_HEIGHT, 0, static_cast<int>(m_tracks.size()) - 1);
}

void FizTrackWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const int w = width();

    auto valueForTrack = [](const FizState& s, int idx) -> float {
        if (idx == 0) return s.focus;
        if (idx == 1) return s.iris;
        return s.zoom;
    };

    // Row backgrounds and labels.
    for (int t = 0; t < m_tracks.size(); ++t) {
        const int yOff = m_tracks[t].yOffset;
        const QColor col = m_tracks[t].color;
        p.fillRect(0, yOff, w, TRACK_HEIGHT, QColor(0x1E, 0x1E, 0x1E));
        p.setPen(QColor(0x33, 0x33, 0x33));
        p.drawLine(0, yOff, w, yOff);
        p.setPen(col);
        p.drawText(4, yOff + 14, m_tracks[t].name);
    }

    // Curves and diamonds.
    for (int t = 0; t < m_tracks.size(); ++t) {
        const TrackInfo& row = m_tracks[t];
        const int yOff = row.yOffset;
        const QColor col = row.color;

        // Each row plots (time, percentage) pairs; only where the percentage
        // comes from differs, so the drawing itself is written once.
        struct Point { double time; float pct; QString id; };
        QList<Point> pts;

        if (row.kind == TrackInfo::Kind::Fiz) {
            for (const auto& kf : m_keyframes) {
                pts.append({ kf.time, valueForTrack(kf.state, row.fizIndex), kf.id });
            }
        } else {
            for (const auto& kf : m_axisKeyframes.value(row.axisId)) {
                pts.append({ kf.time, axisPosToPct(row, kf.positionMm), kf.id });
            }
        }

        auto yFor = [&](float pct) {
            return yOff + static_cast<int>((1.0f - pct / 100.0f) * (TRACK_HEIGHT - 8)) + 4;
        };

        p.setPen(QPen(col, 1.5));
        QPointF prev;
        bool hasPrev = false;
        for (const Point& pt : pts) {
            const QPointF here(static_cast<int>(pt.time * m_pps) - m_scrollOffset, yFor(pt.pct));
            if (hasPrev) p.drawLine(prev, here);
            prev = here;
            hasPrev = true;
        }

        // Selected keyframe gets a white outline, so it is clear what Delete
        // will remove and what is being dragged.
        for (const Point& pt : pts) {
            const int x = static_cast<int>(pt.time * m_pps) - m_scrollOffset;
            const int y = yFor(pt.pct);
            QPolygon diamond;
            diamond << QPoint(x, y - 4) << QPoint(x + 4, y)
                    << QPoint(x, y + 4) << QPoint(x - 4, y);

            const bool selected = (row.kind == TrackInfo::Kind::Fiz)
                ? (pt.id == m_selectedFizId)
                : (row.axisId == m_selectedAxisId && pt.id == m_selectedAxisKfId);

            p.setBrush(col);
            p.setPen(selected ? QPen(Qt::white, 2) : QPen(col.darker(150), 1));
            p.drawPolygon(diamond);
        }
    }

    // Playhead
    const int phx = static_cast<int>(m_playheadTime * m_pps) - m_scrollOffset;
    p.setPen(QPen(QColor(220, 50, 50), 1));
    p.drawLine(phx, 0, phx, height());
}

int FizTrackWidget::findFizKeyframeAt(int x) const
{
    for (int i = 0; i < m_keyframes.size(); ++i) {
        const int kx = static_cast<int>(m_keyframes[i].time * m_pps) - m_scrollOffset;
        if (qAbs(x - kx) < 8) return i;
    }
    return -1;
}

int FizTrackWidget::findAxisKeyframeAt(const QString& axisId, int x) const
{
    const auto& kfs = m_axisKeyframes[axisId];
    for (int i = 0; i < kfs.size(); ++i) {
        const int kx = static_cast<int>(kfs[i].time * m_pps) - m_scrollOffset;
        if (qAbs(x - kx) < 8) return i;
    }
    return -1;
}

void FizTrackWidget::mousePressEvent(QMouseEvent* event)
{
    setFocus(Qt::MouseFocusReason);

    if (event->button() == Qt::LeftButton) {
        const int t = trackAtY(event->pos().y());
        if (t >= 0 && t < m_tracks.size()) {
            const TrackInfo& row = m_tracks[t];

            if (row.kind == TrackInfo::Kind::Fiz) {
                const int idx = findFizKeyframeAt(event->pos().x());
                if (idx >= 0) {
                    m_selectedFizId = m_keyframes[idx].id;
                    m_selectedAxisId.clear();
                    m_selectedAxisKfId.clear();
                    m_dragging = true;
                    emit keyframeSelected(m_keyframes[idx]);
                    update();
                    return;
                }
            } else {
                const int idx = findAxisKeyframeAt(row.axisId, event->pos().x());
                if (idx >= 0) {
                    const GantryKeyframe kf = m_axisKeyframes[row.axisId][idx];
                    m_selectedAxisId   = row.axisId;
                    m_selectedAxisKfId = kf.id;
                    m_selectedFizId.clear();
                    m_dragging = true;
                    emit axisKeyframeSelected(row.axisId, kf);
                    if (row.axisId == kPrimaryAxisId) emit gantryKeyframeSelected(kf);
                    update();
                    return;
                }
            }
        }

        // Clicked empty space — clear selection.
        m_selectedFizId.clear();
        m_selectedAxisId.clear();
        m_selectedAxisKfId.clear();
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
    // Snap to 0.1s, matching the segment-block drag on the main timeline.
    newTime = qRound(newTime * 10.0) / 10.0;

    if (!m_selectedFizId.isEmpty()) {
        for (auto& kf : m_keyframes) {
            if (kf.id == m_selectedFizId) { kf.time = newTime; break; }
        }
    } else if (!m_selectedAxisId.isEmpty()) {
        auto& kfs = m_axisKeyframes[m_selectedAxisId];
        for (auto& kf : kfs) {
            if (kf.id == m_selectedAxisKfId) { kf.time = newTime; break; }
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
        } else if (!m_selectedAxisId.isEmpty()) {
            for (const auto& kf : m_axisKeyframes.value(m_selectedAxisId)) {
                if (kf.id == m_selectedAxisKfId) {
                    emit axisKeyframeMoved(m_selectedAxisId, kf);
                    if (m_selectedAxisId == kPrimaryAxisId) emit gantryKeyframeMoved(kf);
                    break;
                }
            }
        }
    }
    QWidget::mouseReleaseEvent(event);
}

void FizTrackWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    const int t = trackAtY(event->pos().y());
    if (t < 0 || t >= m_tracks.size()) return;

    const TrackInfo& row = m_tracks[t];
    const double time = timeAtX(event->pos().x());
    const float  pct  = valueAtY(event->pos().y(), t);

    if (row.kind == TrackInfo::Kind::Fiz) {
        emit addKeyframeRequested(row.fizIndex, time, pct);
    } else {
        // Uses the row's OWN range, so the keyframe redraws exactly where it
        // was dropped. The old code multiplied by a constant that did not
        // match the one used to draw, and the diamond jumped on the next
        // repaint.
        const double pos = axisPctToPos(row, pct);
        emit addAxisKeyframeRequested(row.axisId, time, static_cast<float>(pos));
        if (row.axisId == kPrimaryAxisId) {
            emit addGantryKeyframeRequested(time, static_cast<float>(pos));
        }
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
        if (!m_selectedAxisId.isEmpty() && !m_selectedAxisKfId.isEmpty()) {
            emit axisKeyframeDeleteRequested(m_selectedAxisId, m_selectedAxisKfId);
            if (m_selectedAxisId == kPrimaryAxisId) {
                emit gantryKeyframeDeleteRequested(m_selectedAxisKfId);
            }
            m_selectedAxisId.clear();
            m_selectedAxisKfId.clear();
            update();
            return;
        }
    }
    QWidget::keyPressEvent(event);
}
