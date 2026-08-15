#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — FIZ Track Widget (Phase 7)
// Three keyframe tracks (Focus/Iris/Zoom) rendered below robot timeline.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QWidget>
#include <QList>
#include "core/types.h"

class FizTrackWidget : public QWidget
{
    Q_OBJECT
public:
    explicit FizTrackWidget(QWidget* parent = nullptr);

    void setPixelsPerSecond(double pps);
    void setScrollOffset(int px);
    void setDuration(double seconds);

public slots:
    void setKeyframes(const QList<FizKeyframe>& kfs);
    void setGantryKeyframes(const QList<GantryKeyframe>& kfs);
    void setPlayheadTime(double seconds);

signals:
    void keyframeSelected(const FizKeyframe& kf);
    void gantryKeyframeSelected(const GantryKeyframe& kf);
    void addKeyframeRequested(int track, double time, float value);
    void addGantryKeyframeRequested(double time, float value);
    // Retiming (horizontal drag) and deletion — without these, a keyframe
    // placed via double-click could never be moved or removed again.
    void fizKeyframeMoved(const FizKeyframe& kf);
    void gantryKeyframeMoved(const GantryKeyframe& kf);
    void fizKeyframeDeleteRequested(const QString& id);
    void gantryKeyframeDeleteRequested(const QString& id);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    struct TrackInfo {
        QString name;
        QColor  color;
        int     yOffset;
    };
    static constexpr int TRACK_HEIGHT = 38;
    QList<TrackInfo> m_tracks;

    double timeAtX(int x) const;
    float  valueAtY(int y, int trackIndex) const;
    int    trackAtY(int y) const;
    int    findFizKeyframeAt(int x) const;
    int    findGantryKeyframeAt(int x) const;

    QList<FizKeyframe> m_keyframes;
    QList<GantryKeyframe> m_gantryKeyframes;
    double m_pps          = 100.0;
    double m_duration     = 30.0;
    double m_playheadTime = 0.0;
    int    m_scrollOffset = 0;

    // Selection/drag state — at most one of the two ids is non-empty at a time.
    QString m_selectedFizId;
    QString m_selectedGantryId;
    bool    m_dragging = false;
};
