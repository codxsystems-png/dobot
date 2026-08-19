#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — FIZ / Axis Track Widget
//
// Three fixed FIZ rows (Focus/Iris/Zoom) followed by ONE ROW PER EXTERNAL AXIS.
// The row list is data, not a hardcoded four: adding an axis adds a row.
//
// ─── On the value scale ──────────────────────────────────────────────────────
// Axis rows map position to row height using that axis's OWN travel range.
// This used to be two hardcoded constants that disagreed with each other —
// display divided by 1.8 (assuming 0-180) while placement multiplied by 10
// (assuming 0-1000), so a keyframe dropped at mid-height was stored at a
// position that redrew somewhere else entirely. One range, used in both
// directions, is what makes place-then-redraw a no-op.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QWidget>
#include <QList>
#include <QHash>
#include "core/types.h"

class FizTrackWidget : public QWidget
{
    Q_OBJECT
public:
    explicit FizTrackWidget(QWidget* parent = nullptr);

    /// One external axis's row: how to label it and how to scale it.
    struct AxisRow {
        QString id;
        QString name;
        double  rangeMin = 0.0;
        double  rangeMax = 100.0;
        QColor  color    = QColor(0xFF, 0x66, 0x33);
    };

    /// Replaces the axis rows wholesale. The FIZ rows are always present and
    /// always first. Passing an empty list leaves just the three FIZ rows.
    void setAxes(const QList<AxisRow>& axes);

    void setPixelsPerSecond(double pps);
    void setScrollOffset(int px);
    void setDuration(double seconds);

public slots:
    void setKeyframes(const QList<FizKeyframe>& kfs);

    /// Keyframes for the primary axis. Kept so existing wiring is unchanged;
    /// equivalent to setAxisKeyframes("gantry", ...).
    void setGantryKeyframes(const QList<GantryKeyframe>& kfs);
    void setAxisKeyframes(const QString& axisId, const QList<GantryKeyframe>& kfs);

    void setPlayheadTime(double seconds);

signals:
    void keyframeSelected(const FizKeyframe& kf);
    void addKeyframeRequested(int track, double time, float value);
    void fizKeyframeMoved(const FizKeyframe& kf);
    void fizKeyframeDeleteRequested(const QString& id);

    // Primary-axis signals, unchanged so existing connections keep working.
    // They fire only for the "gantry" axis.
    void gantryKeyframeSelected(const GantryKeyframe& kf);
    void addGantryKeyframeRequested(double time, float value);
    void gantryKeyframeMoved(const GantryKeyframe& kf);
    void gantryKeyframeDeleteRequested(const QString& id);

    // Axis-qualified equivalents, fired for EVERY axis including the primary.
    void axisKeyframeSelected(const QString& axisId, const GantryKeyframe& kf);
    void addAxisKeyframeRequested(const QString& axisId, double time, float value);
    void axisKeyframeMoved(const QString& axisId, const GantryKeyframe& kf);
    void axisKeyframeDeleteRequested(const QString& axisId, const QString& id);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    struct TrackInfo {
        enum class Kind { Fiz, Axis };
        Kind    kind     = Kind::Fiz;
        QString name;
        QColor  color;
        int     yOffset  = 0;
        int     fizIndex = -1;      // 0/1/2 for Focus/Iris/Zoom
        QString axisId;             // Kind::Axis only
        double  rangeMin = 0.0;
        double  rangeMax = 100.0;
    };

    static constexpr int TRACK_HEIGHT = 38;
    static constexpr int FIZ_ROWS     = 3;

    void rebuildRows();
    void relayout();

    /// Position <-> row percentage, using the row's own range. Inverses of
    /// each other by construction, which the old pair of constants was not.
    float  axisPosToPct(const TrackInfo& row, double pos) const;
    double axisPctToPos(const TrackInfo& row, float pct) const;

    double timeAtX(int x) const;
    float  valueAtY(int y, int trackIndex) const;
    int    trackAtY(int y) const;
    int    findFizKeyframeAt(int x) const;
    int    findAxisKeyframeAt(const QString& axisId, int x) const;

    QList<TrackInfo> m_tracks;
    QList<AxisRow>   m_axes;

    QList<FizKeyframe> m_keyframes;
    QHash<QString, QList<GantryKeyframe>> m_axisKeyframes;

    double m_pps          = 100.0;
    double m_duration     = 30.0;
    double m_playheadTime = 0.0;
    int    m_scrollOffset = 0;

    // Selection/drag state — at most one of the two is set at a time.
    QString m_selectedFizId;
    QString m_selectedAxisId;      // which axis the selected keyframe belongs to
    QString m_selectedAxisKfId;
    bool    m_dragging = false;
};
