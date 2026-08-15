// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Camera Preview Widget
// ═══════════════════════════════════════════════════════════════════════════════

#include "ui/camera_preview_widget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QVideoFrame>
#include <QDebug>

CameraPreviewWidget::CameraPreviewWidget(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
    populateDevices();
}

CameraPreviewWidget::~CameraPreviewWidget()
{
    stopCamera();
}

void CameraPreviewWidget::setupUI()
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    // Video display — primary visual panel
    m_videoWidget = new QVideoWidget(this);
    m_videoWidget->setMinimumSize(320, 180);       // 16:9 reduced minimum
    m_videoWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_videoWidget->setStyleSheet("background: #0a0a0a;"); // black letterbox bg
    layout->addWidget(m_videoWidget, 1);

    // Controls bar
    QHBoxLayout* controls = new QHBoxLayout();
    controls->setSpacing(4);

    m_deviceCombo = new QComboBox();
    m_deviceCombo->setMinimumWidth(150);
    controls->addWidget(m_deviceCombo);

    m_startBtn = new QPushButton("Start");
    m_startBtn->setObjectName("cameraStartBtn");
    connect(m_startBtn, &QPushButton::clicked, this, [this]() {
        if (isActive())
            stopCamera();
        else
            startCamera(m_deviceCombo->currentIndex());
    });
    controls->addWidget(m_startBtn);

    m_snapBtn = new QPushButton("SNAP");
    m_snapBtn->setObjectName("cameraSnapBtn");
    m_snapBtn->setEnabled(false);
    connect(m_snapBtn, &QPushButton::clicked, this, [this]() {
        QImage img = grabThumbnail();
        if (!img.isNull())
            emit snapshotTaken(img);
    });
    controls->addWidget(m_snapBtn);

    m_statusLabel = new QLabel("Off");
    m_statusLabel->setStyleSheet("color: #aaaaaa; padding: 0 4px;");
    controls->addWidget(m_statusLabel);

    controls->addStretch();
    layout->addLayout(controls);
}

void CameraPreviewWidget::populateDevices()
{
    m_deviceCombo->clear();
    const auto devices = QMediaDevices::videoInputs();
    for (const auto& dev : devices)
        m_deviceCombo->addItem(dev.description());

    if (devices.isEmpty())
        m_deviceCombo->addItem("No camera detected");
}

void CameraPreviewWidget::startCamera(int deviceIndex)
{
    stopCamera();

    const auto devices = QMediaDevices::videoInputs();
    if (deviceIndex < 0 || deviceIndex >= devices.size()) {
        qWarning() << "CameraPreview: Invalid device index" << deviceIndex;
        return;
    }

    m_camera = new QCamera(devices[deviceIndex], this);
    m_session = new QMediaCaptureSession(this);
    m_session->setCamera(m_camera);
    m_session->setVideoOutput(m_videoWidget);

    m_camera->start();

    m_startBtn->setText("Stop");
    m_snapBtn->setEnabled(true);
    m_statusLabel->setText("Live");
    m_statusLabel->setStyleSheet("color: #00cc44; padding: 0 4px;");

    emit cameraStarted();
    qDebug() << "CameraPreview: Started" << devices[deviceIndex].description();
}

void CameraPreviewWidget::stopCamera()
{
    if (m_camera) {
        m_camera->stop();
        delete m_camera;
        m_camera = nullptr;
    }
    if (m_session) {
        delete m_session;
        m_session = nullptr;
    }

    m_startBtn->setText("Start");
    m_snapBtn->setEnabled(false);
    m_statusLabel->setText("Off");
    m_statusLabel->setStyleSheet("color: #aaaaaa; padding: 0 4px;");

    emit cameraStopped();
}

bool CameraPreviewWidget::isActive() const
{
    return m_camera && m_camera->isActive();
}

QImage CameraPreviewWidget::grabThumbnail()
{
    // Grab from video widget
    QImage full = m_videoWidget->grab().toImage();
    if (full.isNull())
        return {};

    // Scale to 160×90 JPEG thumbnail
    return full.scaled(160, 90, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

void CameraPreviewWidget::setOverlayCrosshair(bool on)
{
    m_showCrosshair = on;
    // TODO: Custom paint overlay via event filter
}

void CameraPreviewWidget::setOverlaySafeArea(bool on)
{
    m_showSafeArea = on;
}
