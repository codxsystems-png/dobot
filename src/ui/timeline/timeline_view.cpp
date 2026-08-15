// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Timeline View
// ═══════════════════════════════════════════════════════════════════════════════

#include "ui/timeline/timeline_view.h"
#include "ui/timeline/timeline_scene.h"
#include "ui/timeline/timeline_ruler.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollBar>
#include <QWheelEvent>

TimelineView::TimelineView(QWidget* parent)
    : QWidget(parent)
{
    m_scene = new TimelineScene(nullptr, nullptr, this);
    setupUI();

    // Wire scene signals
    connect(m_scene, &TimelineScene::segmentSelected,
            this, &TimelineView::segmentSelected);
    connect(m_scene, &TimelineScene::segmentDeselected,
            this, &TimelineView::segmentDeselected);
    connect(m_scene, &TimelineScene::segmentMoved,
            this, &TimelineView::segmentMoved);
    connect(m_scene, &TimelineScene::playheadMoved, this, [this](double t) {
        m_ruler->setPlayheadTime(t);
        emit playheadTimeChanged(t);
    });
    connect(m_scene, &TimelineScene::pointDroppedOnTimeline,
            this, &TimelineView::pointDroppedOnTimeline);

    // Ruler click → playhead
    connect(m_ruler, &TimelineRuler::timeClicked, this, [this](double t) {
        m_scene->setPlayheadTime(t);
        emit playheadTimeChanged(t);
    });

    // Sync ruler scroll with graphics view
    connect(m_graphicsView->horizontalScrollBar(), &QScrollBar::valueChanged,
            m_ruler, &TimelineRuler::setScrollOffset);

    // Emit scroll offset for FIZ track sync
    connect(m_graphicsView->horizontalScrollBar(), &QScrollBar::valueChanged,
            this, [this](int val) { emit scrollOffsetChanged(val); });
}

void TimelineView::setSegmentsModel(SegmentsModel* model)
{
    m_segmentsModel = model;
    m_scene->setSegmentsModel(model);
}

void TimelineView::setupUI()
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // ─── Toolbar ────────────────────────────────────────────────────────
    QHBoxLayout* toolbar = new QHBoxLayout();
    toolbar->setSpacing(4);
    toolbar->setContentsMargins(4, 2, 4, 2);

    QLabel* title = new QLabel("Timeline");
    title->setStyleSheet("font-weight: bold; font-size: 13px; color: #e0e0e0;");
    toolbar->addWidget(title);
    toolbar->addStretch();

    // Zoom controls
    QPushButton* zoomOutBtn = new QPushButton("-");
    zoomOutBtn->setFixedSize(24, 24);
    connect(zoomOutBtn, &QPushButton::clicked, this, &TimelineView::zoomOut);
    toolbar->addWidget(zoomOutBtn);

    m_zoomSlider = new QSlider(Qt::Horizontal);
    m_zoomSlider->setRange(20, 400); // pps range
    m_zoomSlider->setValue(100);
    m_zoomSlider->setFixedWidth(120);
    connect(m_zoomSlider, &QSlider::valueChanged, this, [this](int val) {
        m_pps = val;
        m_scene->setPixelsPerSecond(m_pps);
        m_ruler->setPixelsPerSecond(m_pps);
        updateZoomLabel();
        emit pixelsPerSecondChanged(m_pps);
    });
    toolbar->addWidget(m_zoomSlider);

    QPushButton* zoomInBtn = new QPushButton("+");
    zoomInBtn->setFixedSize(24, 24);
    connect(zoomInBtn, &QPushButton::clicked, this, &TimelineView::zoomIn);
    toolbar->addWidget(zoomInBtn);

    m_zoomLabel = new QLabel("100%");
    m_zoomLabel->setFixedWidth(45);
    m_zoomLabel->setStyleSheet("color: #aaaaaa; font-size: 10px;");
    toolbar->addWidget(m_zoomLabel);

    m_zoomFitBtn = new QPushButton("Fit");
    m_zoomFitBtn->setFixedWidth(36);
    connect(m_zoomFitBtn, &QPushButton::clicked, this, &TimelineView::zoomFit);
    toolbar->addWidget(m_zoomFitBtn);

    layout->addLayout(toolbar);

    // ─── Ruler ──────────────────────────────────────────────────────────
    m_ruler = new TimelineRuler(this);
    m_ruler->setPixelsPerSecond(m_pps);
    m_ruler->setDuration(30.0);
    layout->addWidget(m_ruler);

    // ─── Graphics View ──────────────────────────────────────────────────
    m_graphicsView = new QGraphicsView(m_scene, this);
    m_graphicsView->setRenderHint(QPainter::Antialiasing);
    m_graphicsView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    m_graphicsView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_graphicsView->setDragMode(QGraphicsView::NoDrag);
    m_graphicsView->setAcceptDrops(true);
    m_graphicsView->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_graphicsView->setMinimumHeight(90);
    // Without this, the view never actually takes keyboard focus on click,
    // so TimelineScene::keyPressEvent (Delete/Backspace, Ctrl+D/C/V, arrow-
    // key nudge) silently never fires no matter what the scene handles.
    m_graphicsView->setFocusPolicy(Qt::StrongFocus);
    layout->addWidget(m_graphicsView, 1);
}

void TimelineView::setPlayheadTime(double seconds)
{
    m_scene->setPlayheadTime(seconds);
    m_ruler->setPlayheadTime(seconds);
}

double TimelineView::playheadTime() const
{
    return m_scene->playheadTime();
}

void TimelineView::zoomIn()
{
    m_zoomSlider->setValue(qMin(m_zoomSlider->value() + 20, 400));
}

void TimelineView::zoomOut()
{
    m_zoomSlider->setValue(qMax(m_zoomSlider->value() - 20, 20));
}

void TimelineView::zoomFit()
{
    double duration = m_scene->sceneRect().width();
    if (duration <= 0) return;
    double viewWidth = m_graphicsView->viewport()->width();
    int newPps = static_cast<int>(viewWidth / (duration / m_pps) * 0.95);
    m_zoomSlider->setValue(qBound(20, newPps, 400));
}

void TimelineView::wheelEvent(QWheelEvent* event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        // Ctrl+Wheel = zoom
        if (event->angleDelta().y() > 0)
            zoomIn();
        else
            zoomOut();
        event->accept();
    } else {
        QWidget::wheelEvent(event);
    }
}

void TimelineView::updateZoomLabel()
{
    m_zoomLabel->setText(QString("%1%").arg(static_cast<int>(m_pps)));
}
