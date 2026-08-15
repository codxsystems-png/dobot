// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Media Service
// ═══════════════════════════════════════════════════════════════════════════════

#include "services/media_service.h"
#include "ui/camera_preview_widget.h"
#include <QMediaDevices>
#include <QDir>
#include <QDateTime>
#include <QDebug>

MediaService::MediaService(CameraPreviewWidget* preview, QObject* parent)
    : QObject(parent)
    , m_preview(preview)
{
}

MediaService::~MediaService()
{
    closeCamera();
}

bool MediaService::isRecording() const
{
    return m_recorder &&
           m_recorder->recorderState() == QMediaRecorder::RecordingState;
}

// ─── Open Camera ──────────────────────────────────────────────────────────────

void MediaService::openUsbCamera(int deviceIndex)
{
    closeCamera();

    const auto devices = QMediaDevices::videoInputs();
    if (deviceIndex < 0 || deviceIndex >= devices.size()) {
        emit errorOccurred(QString("USB camera index %1 not found").arg(deviceIndex));
        return;
    }

    m_camera  = new QCamera(devices[deviceIndex], this);
    m_session = new QMediaCaptureSession(this);
    m_session->setCamera(m_camera);

    // Link to preview widget if available
    if (m_preview)
        m_preview->startCamera(deviceIndex);

    m_sourceType = USB;
    m_sourceUrl  = devices[deviceIndex].description();

    m_camera->start();
    qDebug() << "MediaService: USB camera opened" << m_sourceUrl;
    emit cameraOpened(USB, m_sourceUrl);
}

void MediaService::openRtspCamera(const QString& url)
{
    closeCamera();

    // Qt6.7 supports RTSP via QMediaPlayer — use CameraPreviewWidget
    // with custom source if needed. For now, delegate to preview widget.
    m_sourceType = RTSP;
    m_sourceUrl  = url;

    // TODO: Phase 6c — full FFmpeg RTSP pipeline
    qDebug() << "MediaService: RTSP source set" << url;
    emit cameraOpened(RTSP, url);
}

void MediaService::closeCamera()
{
    if (isRecording())
        stopRecording();

    if (m_camera) {
        m_camera->stop();
        delete m_camera;
        m_camera = nullptr;
    }
    if (m_session) {
        delete m_session;
        m_session = nullptr;
    }
    if (m_recorder) {
        delete m_recorder;
        m_recorder = nullptr;
    }

    if (m_preview)
        m_preview->stopCamera();

    emit cameraClosed();
}

// ─── Recording ────────────────────────────────────────────────────────────────

void MediaService::startRecording(const QString& outputPath)
{
    if (isRecording()) {
        qWarning() << "MediaService: Already recording";
        return;
    }

    if (!m_session) {
        // No Qt camera — snapshot/recording unsupported in RTSP mode
        // TODO: Phase 6c FFmpeg recording
        emit errorOccurred("Recording requires an active camera session");
        return;
    }

    m_currentRecordingPath = outputPath.isEmpty()
                             ? defaultRecordingPath()
                             : outputPath;

    // Ensure output directory exists
    QDir().mkpath(QFileInfo(m_currentRecordingPath).absolutePath());

    if (!m_recorder) {
        m_recorder = new QMediaRecorder(this);
        m_session->setRecorder(m_recorder);
    }

    m_recorder->setOutputLocation(QUrl::fromLocalFile(m_currentRecordingPath));
    m_recorder->record();

    qDebug() << "MediaService: Recording started →" << m_currentRecordingPath;
    emit recordingStarted(m_currentRecordingPath);
}

void MediaService::stopRecording()
{
    if (!m_recorder || !isRecording()) return;

    m_recorder->stop();
    qDebug() << "MediaService: Recording stopped →" << m_currentRecordingPath;
    emit recordingStopped(m_currentRecordingPath);
}

void MediaService::takeSnapshot(const QString& outputPath)
{
    if (!m_preview) {
        emit errorOccurred("No camera preview available for snapshot");
        return;
    }

    QString path = outputPath.isEmpty() ? defaultSnapshotPath() : outputPath;
    QDir().mkpath(QFileInfo(path).absolutePath());

    QImage img = m_preview->grabThumbnail();
    if (img.isNull()) {
        emit errorOccurred("Snapshot failed — camera not active");
        return;
    }

    // Grab full-res from widget paint (thumbnail is 160×90; for snapshots use grab())
    if (img.save(path, "JPEG", 92)) {
        qDebug() << "MediaService: Snapshot saved →" << path;
        emit snapshotTaken(path);
    } else {
        emit errorOccurred("Failed to save snapshot to: " + path);
    }
}

// ─── Helpers ──────────────────────────────────────────────────────────────────

QString MediaService::defaultRecordingPath() const
{
    QString ts = QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss");
    return QString("%1/CamBot/Recordings/recording_%2.mp4")
               .arg(QDir::homePath(), ts);
}

QString MediaService::defaultSnapshotPath() const
{
    QString ts = QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss");
    return QString("%1/CamBot/Snapshots/snapshot_%2.jpg")
               .arg(QDir::homePath(), ts);
}
