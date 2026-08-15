#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Media Service (Phase 6c)
// Handles USB + RTSP/IP cameras via Qt Multimedia + FFmpeg fallback.
// Recording triggers integrated with PlaybackService.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QObject>
#include <QMediaRecorder>
#include <QMediaCaptureSession>
#include <QCamera>
#include <QString>
#include <QTimer>

class CameraPreviewWidget;

class MediaService : public QObject
{
    Q_OBJECT
public:
    enum SourceType { USB, RTSP };

    explicit MediaService(CameraPreviewWidget* preview, QObject* parent = nullptr);
    ~MediaService() override;

    bool isRecording() const;
    SourceType sourceType() const { return m_sourceType; }
    QString sourceUrl() const { return m_sourceUrl; }

public slots:
    /// Connect to USB camera (deviceIndex from QMediaDevices)
    void openUsbCamera(int deviceIndex = 0);

    /// Connect to RTSP/IP stream
    void openRtspCamera(const QString& url);

    /// Close active camera
    void closeCamera();

    /// Start video recording to file
    void startRecording(const QString& outputPath = "");

    /// Stop video recording
    void stopRecording();

    /// Take single photo snapshot
    void takeSnapshot(const QString& outputPath = "");

signals:
    void recordingStarted(const QString& filePath);
    void recordingStopped(const QString& filePath);
    void snapshotTaken(const QString& filePath);
    void cameraOpened(SourceType type, const QString& url);
    void cameraClosed();
    void errorOccurred(const QString& error);

private:
    QString defaultRecordingPath() const;
    QString defaultSnapshotPath() const;

    CameraPreviewWidget*  m_preview     = nullptr;
    QMediaRecorder*       m_recorder    = nullptr;
    QMediaCaptureSession* m_session     = nullptr;
    QCamera*              m_camera      = nullptr;

    SourceType m_sourceType = USB;
    QString    m_sourceUrl;
    QString    m_currentRecordingPath;
};
