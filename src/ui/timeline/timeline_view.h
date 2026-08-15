#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Timeline View
// QGraphicsView + TimelineRuler + zoom/scroll controls.
// Drop target for points from library.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QWidget>
#include <QGraphicsView>
#include <QSlider>
#include <QPushButton>
#include <QLabel>

class TimelineScene;
class TimelineRuler;
class SegmentsModel;
class PointsModel;

class TimelineView : public QWidget
{
    Q_OBJECT
public:
    explicit TimelineView(QWidget* parent = nullptr);

    void setSegmentsModel(SegmentsModel* model);

    TimelineScene* scene() const { return m_scene; }

    void setPlayheadTime(double seconds);
    double playheadTime() const;

signals:
    void segmentSelected(const QString& segId);
    void segmentDeselected();
    void segmentMoved(const QString& segId, double newTime);
    void playheadTimeChanged(double seconds);
    void scrollOffsetChanged(int px);
    void pixelsPerSecondChanged(double pps);
    void pointDroppedOnTimeline(const QString& pointId, double timeSec, const QString& segId);

public slots:
    void zoomIn();
    void zoomOut();
    void zoomFit();

protected:
    void wheelEvent(QWheelEvent* event) override;

private:
    void setupUI();
    void updateZoomLabel();

    QGraphicsView*  m_graphicsView = nullptr;
    TimelineScene*  m_scene        = nullptr;
    TimelineRuler*  m_ruler        = nullptr;

    QSlider*        m_zoomSlider   = nullptr;
    QLabel*         m_zoomLabel    = nullptr;
    QPushButton*    m_zoomFitBtn   = nullptr;
    QPushButton*    m_addSegBtn    = nullptr;

    double          m_pps          = 100.0; // pixels per second
    SegmentsModel*  m_segmentsModel = nullptr;
};
