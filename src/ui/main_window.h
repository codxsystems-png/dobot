#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Main Window (Phases 1–7 fully wired)
// ═══════════════════════════════════════════════════════════════════════════════

#include <QMainWindow>
#include <QSet>
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
class StepperAxisController;
class AxisControllerBase;
class AxisBoardLink;
class AxisManager;
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

private:
    /// Pushes the project's gantry tuning (encoder calibration, travel limits,
    /// PWM ramp, PID gains) onto the controller's thread. Called at startup,
    /// on project load, and whenever the tuning changes.
    void pushGantryTuning();

    /// Floors a proposed GANTRY-track keyframe time so the move from its
    /// preceding keyframe is physically achievable. Without this, placing or
    /// dragging a diamond on the GANTRY curve row can create a gap that only
    /// fails at Play time. Returns the (possibly raised) time.
    double flooredGantryKeyframeTime(const QString& excludeId,
                                     double proposedTime,
                                     double positionUnits) const;

private slots:
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
    // Both axis kinds exist on one board and share one serial link, mirroring
    // the firmware's own layout: axis 0 is the DC servo, axis 1 the stepper.
    // Which one drives the timeline is chosen by the project's drive kind; the
    // other simply sits idle rather than being torn down and rebuilt whenever
    // the setting changes.
    /// Whichever of the two the project selects. Everything that does not care
    /// about the drive kind goes through this.
    StepperAxisController* m_axisController    = nullptr;

    /// Re-points m_axisController at the controller the project's drive kind
    /// selects, and re-wires the playback path to it.
    AxisManager* m_axisManager = nullptr;

    void applyAxisConfiguration();
    /// Sets the jog readout units from the SELECTED axis, since linear and
    /// rotary axes can be mixed on one rig.
    void refreshAxisUnitLabel(const QString& axisId);
    /// Connects position, error and alarm signals for any axis not yet wired.
    /// Runs at startup AND after every configuration change, because an axis
    /// can be added while the app is running.
    void wireAxisSignals();
    /// Axes whose signals are already connected. Lambdas cannot use
    /// Qt::UniqueConnection, so this is what keeps re-configuration from
    /// duplicating every position update.
    QSet<QString> m_wiredAxisIds;
    /// Re-reads the active controller from the manager and re-points
    /// everything that caches it.
    void refreshAxisPointers();
    /// Rebuilds the timeline's axis rows from the project, including each
    /// row's travel range, which is what the row's vertical scale uses.
    void refreshTrackWidgetAxes();
    /// True when the active axis is a stepper, which has no PID to tune.
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
