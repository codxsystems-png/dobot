#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Main Window (Phases 1–7 fully wired)
// ═══════════════════════════════════════════════════════════════════════════════

#include <QMainWindow>
#include <QSplitter>
#include <QHBoxLayout>
#include <QToolBar>
#include <QMenuBar>
#include <QStatusBar>
#include <QAction>
#include <QLabel>
#include <QPushButton>
#include <QThread>
#include <QSettings>
#include <QTabWidget>
#include <QUndoStack>

// Phase 2
class StatusBarWidget;
class TeachPanel;
class ConnectionService;

// Phase 3
class PointsLibraryWidget;
class ProjectService;
class TeachService;
class PointsModel;
class ExportService;

// Phase 4
class TimelineView;
class PropertiesPanel;
class SegmentsModel;

// Phase 5
class PlaybackService;

// Phase 6
class MediaService;
class DiagnosticsPanel;
class CameraPreviewWidget;

// Phase 7 — FIZ
class NucleusService;
class FizService;
class FizPanel;
class FizTrackWidget;
class GantryAxisController;
class GantryService;
class PathRecorderService;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    void onNewProject();
    void onOpenProject();
    void onSaveProject();
    void onSaveProjectAs();
    void onExportCsv();
    void onExportPython();

    void onConnectRobot();
    void onDisconnectRobot();
    void onEmergencyStop();
    void onRobotConnected();
    void onRobotDisconnected();
    void onRobotError(const QString& error);

    void onFizConnect();
    void onFizSetupWizard();
    void onLensMapping();

    void onPlay();
    void onPause();
    void onStop();

    void onConnectCamera();
    void onSnapshot();
    void onStartRecord();
    void onStopRecord();

    void onCalibration();
    void onGantryMotorSetup();
    void onDiagnostics();

private:
    void createMenuBar();
    void createToolBar();
    void createCentralLayout();
    void createConnections();
    void saveWindowState();
    void restoreWindowState();
    void initServices();
    void clampBottomSplitter();

    // ─── Layout ──────────────────────────────────────────────────────────────
    QSplitter*   m_rightSplitter  = nullptr;
    QSplitter*   m_bottomSplitter = nullptr;

    // ─── Phase 2 Widgets ─────────────────────────────────────────────────────
    StatusBarWidget* m_statusBarWidget = nullptr;
    TeachPanel*      m_teachPanel      = nullptr;
    QTabWidget*      m_rightTabs       = nullptr;

    // ─── Phase 3 Widgets ─────────────────────────────────────────────────────
    PointsLibraryWidget* m_pointsLibrary = nullptr;

    // ─── Phase 4 Widgets ─────────────────────────────────────────────────────
    TimelineView*  m_timelineView  = nullptr;
    PropertiesPanel* m_propertiesPanel = nullptr;
    SegmentsModel* m_segmentsModel = nullptr;

    // ─── Phase 6 Widgets ─────────────────────────────────────────────────────
    DiagnosticsPanel* m_diagnosticsPanel = nullptr;
    CameraPreviewWidget* m_cameraPreview = nullptr;

    // ─── Phase 7 Widgets ─────────────────────────────────────────────────────
    FizPanel*       m_fizPanel       = nullptr;
    FizTrackWidget* m_fizTrackWidget = nullptr;

    // ─── Services ────────────────────────────────────────────────────────────
    ConnectionService* m_connectionService = nullptr;
    ProjectService*    m_projectService    = nullptr;
    TeachService*      m_teachService      = nullptr;
    PlaybackService*   m_playbackService   = nullptr;
    MediaService*      m_mediaService      = nullptr;
    ExportService*     m_exportService     = nullptr;
    PointsModel*        m_pointsModel      = nullptr;
    NucleusService*    m_nucleusService    = nullptr;
    FizService*        m_fizService        = nullptr;
    GantryAxisController* m_gantryController = nullptr;
    GantryService*     m_gantryService     = nullptr;
    PathRecorderService* m_pathRecorder    = nullptr;

    // ─── Threads ─────────────────────────────────────────────────────────────
    QThread*           m_robotThread       = nullptr;
    QThread*           m_feedbackThread    = nullptr;
    QThread*           m_nucleusThread     = nullptr;
    QThread*           m_gantryThread      = nullptr;

    // ─── Toolbar ─────────────────────────────────────────────────────────────
    QPushButton* m_estopButton      = nullptr;
    QLabel*      m_connectionLabel  = nullptr;
    QLabel*      m_fizStatusLabel   = nullptr;

    // ─── Menu Actions ────────────────────────────────────────────────────────
    QAction* m_newAction        = nullptr;
    QAction* m_openAction       = nullptr;
    QAction* m_saveAction       = nullptr;
    QAction* m_saveAsAction     = nullptr;
    QAction* m_connectAction    = nullptr;
    QAction* m_disconnectAction = nullptr;
    QAction* m_playAction       = nullptr;
    QAction* m_pauseAction      = nullptr;
    QAction* m_stopAction       = nullptr;
    QAction* m_loopAction       = nullptr;
    QAction* m_fizConnectAction = nullptr;

    // ─── Undo/Redo ───────────────────────────────────────────────────────────
    QUndoStack* m_undoStack = nullptr;
};
