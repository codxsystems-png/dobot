#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Camera Preview Widget
// QVideoWidget + overlay painter. USB camera first, RTSP later (Phase 6).
// ═══════════════════════════════════════════════════════════════════════════════

#include <QWidget>
#include <QCamera>
#include <QMediaCaptureSession>
#include <QVideoWidget>
#include <QMediaDevices>
#include <QPushButton>
#include <QComboBox>
#include <QImage>
#include <QLabel>

class CameraPreviewWidget : public QWidget
{
    Q_OBJECT
public:
    explicit CameraPreviewWidget(QWidget* parent = nullptr);
    ~CameraPreviewWidget() override;

    /// Grab current frame as 160×90 thumbnail
    QImage grabThumbnail();

    /// Check if camera is active
    bool isActive() const;

public slots:
    void startCamera(int deviceIndex = 0);
    void stopCamera();
    void setOverlayCrosshair(bool on);
    void setOverlaySafeArea(bool on);

signals:
    void snapshotTaken(const QImage& image);
    void cameraStarted();
    void cameraStopped();

private:
    void setupUI();
    void populateDevices();

    QVideoWidget*         m_videoWidget  = nullptr;
    QCamera*              m_camera       = nullptr;
    QMediaCaptureSession* m_session      = nullptr;

    QComboBox*   m_deviceCombo  = nullptr;
    QPushButton* m_startBtn     = nullptr;
    QPushButton* m_snapBtn      = nullptr;
    QLabel*      m_statusLabel  = nullptr;

    bool m_showCrosshair = false;
    bool m_showSafeArea  = false;
};
