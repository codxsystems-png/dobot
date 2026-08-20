// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Main Window (Phases 1–7 fully wired)
// ═══════════════════════════════════════════════════════════════════════════════

#include "ui/main_window.h"
#include "ui/status_bar_widget.h"
#include "ui/teach_panel.h"
#include "ui/diagnostics_panel.h"
#include "ui/camera_preview_widget.h"
#include "ui/dialogs/calibration_dialog.h"
#include "ui/dialogs/gantry_setup_dialog.h"
#include "services/connection_service.h"
#include "services/project_service.h"
#include "services/teach_service.h"
#include "services/playback_service.h"
#include "services/media_service.h"
#include "services/export_service.h"
#include "infrastructure/fiz/nucleusservice.h"
#include "application/fizservice.h"
#include "presentation/widgets/fizpanel.h"
#include "presentation/widgets/fiztrackwidget.h"
#include "presentation/dialogs/fizsetupwizard.h"
#include "presentation/dialogs/lensmappingdialog.h"
#include "infrastructure/gantry/stepper_axis_controller.h"
#include "infrastructure/gantry/axis_board_link.h"
#include "infrastructure/gantry/axis_manager.h"
#include "core/structured_logger.h"
#include "application/gantryservice.h"
#include "services/path_recorder_service.h"
#include "core/command_builder.h"
#include "core/motion_estimator.h"
#include "ui/points_library_widget.h"
#include "ui/timeline/timeline_view.h"
#include "ui/timeline/timeline_scene.h"
#include "ui/timeline/timeline_undo_commands.h"
#include "ui/properties_panel.h"
#include "models/segments_model.h"
#include "models/points_model.h"

#include <QApplication>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QFrame>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QFileDialog>
#include <QTabWidget>
#include <QInputDialog>
#include <QUuid>
#include <QTimer>
#include <QDebug>

// Adding a point to the timeline creates a TimelineSegment plus a matching
// standalone Gantry/FIZ keyframe (so the point shows on those curve rows).
// Deriving the keyframe id from the segment id keeps the pair findable, so
// dragging the segment can move its keyframes with it instead of desyncing.
static const QString kAutoKeyframeSuffix = "_auto";

// ─── Constructor ────────────────────────────────────────────────────────────────

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("CamBot Timeline — CODX Systems");
    setMinimumSize(1280, 720);
    resize(1600, 900);

    m_undoStack = new QUndoStack(this);

    // ─── Threads ────────────────────────────────────────────────────────────
    m_robotThread    = new QThread(this);
    m_feedbackThread = new QThread(this);
    m_nucleusThread  = new QThread(this);
    m_gantryThread   = new QThread(this);
    m_robotThread->setObjectName("robotThread");
    m_feedbackThread->setObjectName("feedbackThread");
    m_nucleusThread->setObjectName("nucleusThread");
    m_gantryThread->setObjectName("gantryThread");

    // ─── Init all services ──────────────────────────────────────────────────
    initServices();

    // ─── Build UI ───────────────────────────────────────────────────────────
    createMenuBar();
    createToolBar();
    createCentralLayout();
    // ─── Status bar ─────────────────────────────────────────────────────────
    m_statusBarWidget = new StatusBarWidget(this);
    statusBar()->addPermanentWidget(m_statusBarWidget, 1);

    createConnections();

    // ─── Start threads ──────────────────────────────────────────────────────
    m_robotThread->start();
    m_feedbackThread->start();
    m_nucleusThread->start();
    m_gantryThread->start();

    restoreWindowState();
    qDebug() << "MainWindow: Fully initialized (Phases 1–7)";
}

MainWindow::~MainWindow()
{
    m_robotThread->quit();
    m_feedbackThread->quit();
    m_nucleusThread->quit();
    m_gantryThread->quit();
    m_robotThread->wait(3000);
    m_feedbackThread->wait(3000);
    m_nucleusThread->wait(3000);
    m_gantryThread->wait(3000);
}

// ─── Service Init ───────────────────────────────────────────────────────────────

void MainWindow::initServices()
{
    // Phase 2 — ConnectionService (owns TCP, lives on threads)
    m_connectionService = new ConnectionService(m_robotThread, m_feedbackThread, this);

    // Phase 3 — PointsModel needed by several services
    // PointsModel is created here so it can be shared
    // (we use nullptr for optional args where constructors require it)
    m_projectService = new ProjectService(nullptr, this);   // no PointsModel yet (loaded on open)
    
    // Gantry Service (Phase 8)
    //
    // One board, one serial link, two axes — the same shape the firmware has.
    // Both controllers are built up front and share the link; the project's
    // drive kind decides which one the timeline actually drives. Building only
    // the selected one would mean tearing down and rebuilding the serial
    // connection every time that setting changed.
    // AxisManager owns the links, the controllers, and which one drives
    // each axis. MainWindow keeps m_axisController and m_axisController
    // as views onto it so the ~117 existing references stay valid; they
    // get retired call site by call site rather than in one sweep.
    m_axisManager = new AxisManager(m_gantryThread, this);

    AxisConfig primaryAxis;   // defaults = the DC gantry every project has had
    m_axisManager->configure({ primaryAxis });

    refreshAxisPointers();

    // An axis the manager refuses — too many, or a duplicate board address —
    // must be reported, not silently dropped: otherwise the operator has a
    // configured axis that never moves and no indication why.
    connect(m_axisManager, &AxisManager::axisRejected, this,
            [this](const QString& axisId, const QString& reason) {
                const QString msg = QString("Axis '%1' not started: %2").arg(axisId, reason);
                if (m_diagnosticsPanel) m_diagnosticsPanel->appendLog(msg, "ERROR");
            });

    m_gantryService = new GantryService(this);
    m_gantryService->initialize(m_axisController);

    m_pathRecorder = new PathRecorderService(this);

    m_teachService   = new TeachService(m_connectionService, nullptr, nullptr, m_fizService, m_gantryService, this);

    // Phase 5 — PlaybackService (needs conn + segment/point models — wired after UI built)
    m_playbackService = new PlaybackService(m_connectionService, nullptr, nullptr, this);

    // Phase 6 — MediaService (needs preview widget, built after createCentralLayout)
    // m_mediaService is now created in createCentralLayout after camera widget exists

    // Phase 7 — FIZ
    m_nucleusService = new NucleusService();
    m_nucleusService->moveToThread(m_nucleusThread);
    connect(m_nucleusThread, &QThread::finished, m_nucleusService, &QObject::deleteLater);

    m_fizService = new FizService(this);
    m_fizService->initialize(m_nucleusService);

    // Wire extra services into playback
    m_playbackService->setAdditionalServices(m_gantryService, m_fizService, m_axisController, m_nucleusService);
    m_playbackService->setProjectService(m_projectService);
}

// ─── Menu Bar ───────────────────────────────────────────────────────────────────

void MainWindow::createMenuBar()
{
    // ── File ────────────────────────────────────────────────────────────────
    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
    m_newAction    = fileMenu->addAction(tr("&New Project"),   this, &MainWindow::onNewProject,   QKeySequence::New);
    m_openAction   = fileMenu->addAction(tr("&Open Project"),  this, &MainWindow::onOpenProject,  QKeySequence::Open);
    fileMenu->addSeparator();
    m_saveAction   = fileMenu->addAction(tr("&Save"),          this, &MainWindow::onSaveProject,  QKeySequence::Save);
    m_saveAsAction = fileMenu->addAction(tr("Save &As"),       this, &MainWindow::onSaveProjectAs,QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
    fileMenu->addSeparator();
    QMenu* exportMenu = fileMenu->addMenu(tr("&Export"));
    exportMenu->addAction(tr("Export &CSV Waypoints"),   this, &MainWindow::onExportCsv);
    exportMenu->addAction(tr("Export &Python Script"),   this, &MainWindow::onExportPython);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("E&xit"), this, &QMainWindow::close, QKeySequence::Quit);

    // ── Edit ────────────────────────────────────────────────────────────────
    QMenu* editMenu = menuBar()->addMenu(tr("&Edit"));
    QAction* undoAction = m_undoStack->createUndoAction(this, tr("&Undo"));
    undoAction->setShortcut(QKeySequence::Undo);
    QAction* redoAction = m_undoStack->createRedoAction(this, tr("&Redo"));
    redoAction->setShortcut(QKeySequence::Redo);
    editMenu->addAction(undoAction);
    editMenu->addAction(redoAction);
    editMenu->addSeparator();
    editMenu->addAction(tr("&Duplicate Segment"), this, [this]() {
        if (m_timelineView) m_timelineView->scene()->duplicateSelectedSegments();
    }, QKeySequence(Qt::CTRL | Qt::Key_D));
    editMenu->addAction(tr("&Copy Segment"), this, [this]() {
        if (m_timelineView) m_timelineView->scene()->copySelectedSegments();
    }, QKeySequence::Copy);
    editMenu->addAction(tr("&Paste Segment"), this, [this]() {
        if (m_timelineView) m_timelineView->scene()->pasteSegments();
    }, QKeySequence::Paste);

    // ── Robot ───────────────────────────────────────────────────────────────
    QMenu* robotMenu = menuBar()->addMenu(tr("&Robot"));
    m_connectAction    = robotMenu->addAction(tr("&Connect"),        this, &MainWindow::onConnectRobot);
    m_disconnectAction = robotMenu->addAction(tr("&Disconnect"),     this, &MainWindow::onDisconnectRobot);
    m_disconnectAction->setEnabled(false);
    robotMenu->addSeparator();
    robotMenu->addAction(tr("&Calibration…"), this, &MainWindow::onCalibration);
    robotMenu->addAction(tr("&Gantry Motor Setup…"), this, &MainWindow::onGantryMotorSetup);
    robotMenu->addSeparator();
    robotMenu->addAction(tr("&Emergency Stop"), this, &MainWindow::onEmergencyStop, Qt::Key_F12);

    // ── Timeline ────────────────────────────────────────────────────────────
    QMenu* timelineMenu = menuBar()->addMenu(tr("&Timeline"));
    m_playAction  = timelineMenu->addAction(tr("&Play"),  this, &MainWindow::onPlay,  Qt::Key_Space);
    m_pauseAction = timelineMenu->addAction(tr("P&ause"), this, &MainWindow::onPause, QKeySequence(Qt::CTRL | Qt::Key_Space));
    m_stopAction  = timelineMenu->addAction(tr("&Stop"),  this, &MainWindow::onStop,  Qt::Key_Escape);

    // ── Camera ──────────────────────────────────────────────────────────────
    QMenu* cameraMenu = menuBar()->addMenu(tr("&Camera"));
    cameraMenu->addAction(tr("Co&nnect Camera"), this, &MainWindow::onConnectCamera, Qt::Key_F4);
    cameraMenu->addSeparator();
    cameraMenu->addAction(tr("&Snapshot"),      this, &MainWindow::onSnapshot,    Qt::Key_F5);
    cameraMenu->addAction(tr("&Start Record"),  this, &MainWindow::onStartRecord, Qt::Key_F6);
    cameraMenu->addAction(tr("S&top Record"),   this, &MainWindow::onStopRecord,  Qt::Key_F7);

    // ── FIZ ─────────────────────────────────────────────────────────────────
    QMenu* fizMenu = menuBar()->addMenu(tr("&FIZ"));
    m_fizConnectAction = fizMenu->addAction(tr("&Connect Motor Bus…"), this, &MainWindow::onFizConnect);
    fizMenu->addSeparator();
    fizMenu->addAction(tr("&Setup Wizard…"),        this, &MainWindow::onFizSetupWizard);
    fizMenu->addAction(tr("Configure &Lens Map…"),  this, &MainWindow::onLensMapping);

    // ── Tools ───────────────────────────────────────────────────────────────
    QMenu* toolsMenu = menuBar()->addMenu(tr("&Tools"));
    toolsMenu->addAction(tr("&Diagnostics…"), this, &MainWindow::onDiagnostics);

    // ── Help ────────────────────────────────────────────────────────────────
    QMenu* helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("&About"), this, [this](){
        QMessageBox::about(this, "CamBot Timeline",
            "CamBot Timeline v1.2.0\n"
            "CODX Systems Pvt Ltd\n\n"
            "Dobot Nova 5 Robot Controller\n"
            "with Tilta Nucleus-M FIZ Support\n\n"
            "Build: Phases 1–7 Complete");
    });
}

// ─── Tool Bar ───────────────────────────────────────────────────────────────────

void MainWindow::createToolBar()
{
    QToolBar* toolbar = addToolBar(tr("Main Toolbar"));
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(24, 24));

    // File
    toolbar->addAction(m_newAction);
    toolbar->addAction(m_openAction);
    toolbar->addAction(m_saveAction);
    toolbar->addSeparator();

    // Robot connection
    toolbar->addAction(m_connectAction);
    toolbar->addAction(m_disconnectAction);
    m_connectionLabel = new QLabel("  Disconnected  ");
    m_connectionLabel->setStyleSheet("color: #cc0000; font-weight: bold; padding: 2px 8px;");
    toolbar->addWidget(m_connectionLabel);
    toolbar->addSeparator();

    // Playback
    toolbar->addAction(m_playAction);
    toolbar->addAction(m_pauseAction);
    toolbar->addAction(m_stopAction);
    m_loopAction = new QAction(tr("Loop"), this);
    m_loopAction->setCheckable(true);
    m_loopAction->setToolTip("Repeat the sequence from the start when it finishes");
    connect(m_loopAction, &QAction::toggled, this, [this](bool checked) {
        if (m_playbackService) m_playbackService->setLooping(checked);
    });
    toolbar->addAction(m_loopAction);
    toolbar->addSeparator();

    // FIZ status indicator
    m_fizStatusLabel = new QLabel("  FIZ  ");
    m_fizStatusLabel->setStyleSheet("color: #555555; font-weight: bold; padding: 2px 6px;");
    m_fizStatusLabel->setToolTip("FIZ Motor Bus Status");
    toolbar->addWidget(m_fizStatusLabel);

    // Spacer → E-STOP always far right
    QWidget* spacer = new QWidget();
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(spacer);

    m_estopButton = new QPushButton("E-STOP");
    m_estopButton->setObjectName("estopButton");
    m_estopButton->setMinimumSize(120, 40);
    m_estopButton->setFocusPolicy(Qt::NoFocus);
    m_estopButton->setToolTip("Emergency Stop — also F12");
    m_estopButton->setStyleSheet(
        "QPushButton#estopButton {"
        "  background: #cc0000; color: white; font-weight: bold; font-size: 13px;"
        "  border-radius: 4px; border: 2px solid #ff4444;"
        "}"
        "QPushButton#estopButton:pressed { background: #880000; }");
    connect(m_estopButton, &QPushButton::clicked, this, &MainWindow::onEmergencyStop);
    toolbar->addWidget(m_estopButton);
}

// ─── Central Layout ─────────────────────────────────────────────────────────────

void MainWindow::createCentralLayout()
{
    // Phase 3 — needs models (create models first)
    // PointsModel is needed by several widgets — create it here
    auto* pointsModel = new PointsModel(this);   // owned by MainWindow
    m_pointsModel     = pointsModel;
    m_segmentsModel   = new SegmentsModel(pointsModel, this);

    // Late-bind the model to TeachService, PlaybackService, and ProjectService
    if (m_teachService)
        m_teachService->setModel(pointsModel);
    if (m_playbackService)
        m_playbackService->setModels(m_segmentsModel, pointsModel);
    if (m_projectService) {
        m_projectService->setModels(pointsModel, m_segmentsModel, m_fizService, m_gantryService);
        m_segmentsModel->setGantryMotorSpec(m_projectService->project().gantryMotorSpec);
        // Keep the model's copy in sync with whatever's loaded/configured —
        // SegmentsModel needs its own spec to auto-floor new segments'
        // trigger times (see dropMimeData) without a ProjectService pointer.
        connect(m_projectService, &ProjectService::projectLoaded, this, [this]() {
            m_segmentsModel->setGantryMotorSpec(m_projectService->project().gantryMotorSpec);
            pushGantryTuning();
            refreshTrackWidgetAxes();
        });
        connect(m_projectService, &ProjectService::gantryMotorSpecChanged,
                m_segmentsModel, &SegmentsModel::setGantryMotorSpec);
        // driveKind lives in the motor spec, so this is the signal that
        // actually carries a change of it.
        connect(m_projectService, &ProjectService::gantryMotorSpecChanged,
                this, [this](const GantryMotorSpec&) { applyAxisConfiguration(); });
        // Travel limits define each axis row's vertical scale, so the rows are
        // rebuilt whenever the tuning or the axis list changes.
        connect(m_projectService, &ProjectService::gantryTuningChanged,
                this, [this](const GantryTuning&) { refreshTrackWidgetAxes(); });
        connect(m_projectService, &ProjectService::axesChanged,
                this, [this](const QList<AxisConfig>&) {
                    applyAxisConfiguration();     // build/reconfigure the runtimes
                    refreshTrackWidgetAxes();     // then redraw to match
                    if (m_teachPanel) refreshAxisUnitLabel(m_teachPanel->selectedAxisId());
                });
        // Axis type drives the units shown on the jog readout (mm vs degrees).
        connect(m_projectService, &ProjectService::gantryMotorSpecChanged,
                this, [this](const GantryMotorSpec& spec) {
                    if (m_teachPanel) m_teachPanel->setAxisUnitLabel(motion::unitLabel(spec));
                });
        // Encoder calibration / travel limits / PID gains only take effect if
        // they're actually pushed to the controller thread — before this they
        // were persisted but never applied, leaving m_countsPerMm hardcoded.
        connect(m_projectService, &ProjectService::gantryTuningChanged,
                this, [this](const GantryTuning&) { pushGantryTuning(); });
        pushGantryTuning(); // initial push, even for a default project
    }

    m_exportService = new ExportService(m_segmentsModel, pointsModel, this);

    m_pointsLibrary   = new PointsLibraryWidget(pointsModel, this);

    // Phase 4
    m_timelineView    = new TimelineView(this);
    m_propertiesPanel = new PropertiesPanel(m_segmentsModel, this);
    m_timelineView->setSegmentsModel(m_segmentsModel);
    m_timelineView->scene()->setPointsModel(pointsModel);
    m_timelineView->scene()->setUndoStack(m_undoStack);
    m_propertiesPanel->setPointsModel(pointsModel);
    m_propertiesPanel->setUndoStack(m_undoStack);

    // ── Phase 7: FIZ widgets ────────────────────────────────────────────────
    m_fizPanel       = new FizPanel(this);
    m_fizPanel->setFizService(m_fizService);
    m_fizPanel->setNucleusService(m_nucleusService);

    m_fizTrackWidget = new FizTrackWidget(this);

    // ── Phase 6: Diagnostics (tab) & Camera ─────────────────────────────────
    m_diagnosticsPanel = new DiagnosticsPanel(m_connectionService, this);
    m_cameraPreview    = new CameraPreviewWidget(this);
    m_mediaService     = new MediaService(m_cameraPreview, this);

    // ── Teach Panel (Phase 2) ───────────────────────────────────────────────
    m_teachPanel = new TeachPanel(this);

    // ── RIGHT PANEL: Tab widget — FIXED width, permanently anchored ────────
    m_rightTabs = new QTabWidget();
    m_rightTabs->setTabPosition(QTabWidget::North);
    m_rightTabs->addTab(m_teachPanel,        "Teach");
    m_rightTabs->addTab(m_fizPanel,          "FIZ");
    m_rightTabs->addTab(m_diagnosticsPanel,  "Diagnostics");
    m_rightTabs->setFixedWidth(360);
    m_rightTabs->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

    // ── Timeline area: compact timeline above, FIZ tracks below ─────────────
    QWidget* timelineArea = new QWidget();
    QVBoxLayout* timelineVlay = new QVBoxLayout(timelineArea);
    timelineVlay->setContentsMargins(0, 0, 0, 0);
    timelineVlay->setSpacing(0);
    m_timelineView->setMinimumHeight(120);
    m_timelineView->setMaximumHeight(220);
    timelineVlay->addWidget(m_timelineView, 1);
    // Height follows the row count now — the widget fixes its own height in
    // relayout(), and pinning it here would clip every axis past the first.
    timelineVlay->addWidget(m_fizTrackWidget, 0);

    // Bottom row: Properties | Points Library | Camera (primary)
    m_bottomSplitter = new QSplitter(Qt::Horizontal);
    m_bottomSplitter->addWidget(m_propertiesPanel);
    m_bottomSplitter->addWidget(m_pointsLibrary);
    m_bottomSplitter->addWidget(m_cameraPreview);
    m_bottomSplitter->setStretchFactor(0, 1);
    m_bottomSplitter->setStretchFactor(1, 1);
    m_bottomSplitter->setStretchFactor(2, 3);
    m_bottomSplitter->setChildrenCollapsible(false);
    // Keep minimums low so they fit within any reasonable window width.
    m_propertiesPanel->setMinimumHeight(200);
    m_propertiesPanel->setMinimumWidth(160);
    m_pointsLibrary->setMinimumHeight(200);
    m_pointsLibrary->setMinimumWidth(80);
    m_cameraPreview->setMinimumHeight(180);
    m_cameraPreview->setMinimumWidth(200);
    // Remove any maximum width constraints that could fight the splitter
    m_propertiesPanel->setMaximumWidth(QWIDGETSIZE_MAX);
    m_pointsLibrary->setMaximumWidth(QWIDGETSIZE_MAX);
    m_cameraPreview->setMaximumWidth(QWIDGETSIZE_MAX);

    // Clamp bottom splitter on drag to prevent any child from collapsing
    connect(m_bottomSplitter, &QSplitter::splitterMoved,
            this, [this]() { clampBottomSplitter(); });

    m_rightSplitter = new QSplitter(Qt::Vertical);
    m_rightSplitter->setOpaqueResize(false);
    m_rightSplitter->addWidget(timelineArea);
    m_rightSplitter->addWidget(m_bottomSplitter);
    m_rightSplitter->setStretchFactor(0, 1);
    m_rightSplitter->setStretchFactor(1, 3);
    m_rightSplitter->setChildrenCollapsible(false);

    // Main layout: content area (flexible) + right tabs (fixed 360px).
    // Using QHBoxLayout instead of QSplitter so the right panel can NEVER
    // be dragged off-screen — there is no splitter handle to grab.
    QWidget* centralContainer = new QWidget();
    QHBoxLayout* mainHBox = new QHBoxLayout(centralContainer);
    mainHBox->setContentsMargins(0, 0, 0, 0);
    mainHBox->setSpacing(0);
    mainHBox->addWidget(m_rightSplitter, 1);   // content: takes all stretch
    mainHBox->addWidget(m_rightTabs, 0);        // right tabs: fixed, no stretch

    setCentralWidget(centralContainer);
}

// ─── Signal / Slot Wiring ───────────────────────────────────────────────────────

void MainWindow::createConnections()
{
    // ── Robot connection state ───────────────────────────────────────────────
    connect(m_connectionService, &ConnectionService::connected,
            this, &MainWindow::onRobotConnected);
    connect(m_connectionService, &ConnectionService::disconnected,
            this, &MainWindow::onRobotDisconnected);
    connect(m_connectionService, &ConnectionService::errorOccurred,
            this, &MainWindow::onRobotError);

    // Feedback → status bar
    connect(m_connectionService, &ConnectionService::feedbackUpdated,
            m_statusBarWidget,   &StatusBarWidget::updateFeedback);
    connect(m_connectionService, &ConnectionService::connectionStateChanged,
            m_statusBarWidget,   &StatusBarWidget::updateConnectionState);

    // ── Teach Panel → ConnectionService ──────────────────────────────────
    // Connect button → prompt for IP then connect
    connect(m_teachPanel, &TeachPanel::connectRequested, this, &MainWindow::onConnectRobot);
    // Power / Enable buttons → re-arm robot (recover from stop / re-enable)
    connect(m_teachPanel, &TeachPanel::powerRequested, this, [this](bool) {
        if (m_connectionService && m_connectionService->isConnected())
            m_connectionService->recoverFromEmergency();
    });
    connect(m_teachPanel, &TeachPanel::enableRequested, this, [this](bool) {
        if (m_connectionService && m_connectionService->isConnected())
            m_connectionService->recoverFromEmergency();
    });
    connect(m_teachPanel, &TeachPanel::jogRequested,
            m_connectionService, &ConnectionService::jogAxis);
    connect(m_teachPanel, &TeachPanel::jogStopRequested,
            m_connectionService, &ConnectionService::jogStop);
    connect(m_teachPanel, &TeachPanel::dragModeRequested,
            m_connectionService, &ConnectionService::setDragMode);
    connect(m_teachPanel, &TeachPanel::speedChanged,
            m_connectionService, &ConnectionService::setSpeed);

    // Points Library -> TeachService & Timeline wiring
    if (m_pointsLibrary && m_teachService) {
        connect(m_pointsLibrary, &PointsLibraryWidget::recordPointRequested,
                m_teachService, [this]() { m_teachService->recordPoint(); });
    }

    if (m_pointsLibrary) {
        connect(m_pointsLibrary, &PointsLibraryWidget::addToTimelineRequested,
                this, [this](const QString& pointId) {
                    if (!m_segmentsModel) return;
                    double timeSec = m_segmentsModel->totalDuration();

                    // Retrieve CameraPoint to extract Gantry / FIZ data
                    CameraPoint pt;
                    if (m_pointsLibrary->listView()->model()) {
                        auto* pointsModel = qobject_cast<PointsModel*>(m_pointsLibrary->listView()->model());
                        if (pointsModel) pt = pointsModel->pointById(pointId);
                    }

                    // Floor timeSec against the physically-minimum gantry
                    // move time from the last segment's point, if configured.
                    if (m_segmentsModel->rowCount() > 0 && !pt.id.isEmpty()) {
                        TimelineSegment lastSeg = m_segmentsModel->segmentAt(m_segmentsModel->rowCount() - 1);
                        CameraPoint fromPt = m_pointsModel ? m_pointsModel->pointById(lastSeg.pointId) : CameraPoint();
                        if (!fromPt.id.isEmpty()) {
                            double minGap = motion::minGantryDurationSec(fromPt, pt, m_segmentsModel->gantryMotorSpec(), 0.0);
                            timeSec = qMax(timeSec, lastSeg.triggerTime + minGap);
                        }
                    }

                    // Create TimelineSegment for robot
                    TimelineSegment seg;
                    seg.id          = QUuid::createUuid().toString(QUuid::WithoutBraces);
                    seg.pointId     = pointId;
                    seg.type        = TimelineSegment::MovJ;
                    seg.speedPct    = 80;
                    seg.accPct      = 50;
                    seg.triggerTime = timeSec;
                    m_segmentsModel->addSegment(seg);

                    // Sync Gantry keyframe (id derived from the segment so a
                    // later drag can carry it — see segmentMoved below)
                    if (m_gantryService) {
                        GantryKeyframe gkf;
                        gkf.id         = seg.id + kAutoKeyframeSuffix;
                        gkf.time       = timeSec;
                        gkf.positionMm = pt.id.isEmpty() ? m_gantryService->currentPositionMm() : pt.gantryPositionMm;
                        m_gantryService->addKeyframe(gkf);
                    }

                    // Sync FIZ keyframe
                    if (m_fizService) {
                        FizKeyframe fkf;
                        fkf.id    = seg.id + kAutoKeyframeSuffix;
                        fkf.time  = timeSec;
                        fkf.state = pt.id.isEmpty() ? m_fizService->currentState() : pt.fizState;
                        m_fizService->addKeyframe(fkf);
                    }
                });

        // Delete — previously dead: right-click "Delete" on a taught point
        // emitted deletePointRequested but nothing was connected to it.
        // Cascade-removes any timeline segments that referenced the point so
        // the timeline doesn't keep showing segments pointing at nothing.
        connect(m_pointsLibrary, &PointsLibraryWidget::deletePointRequested,
                this, [this](const QString& pointId) {
                    if (!m_pointsModel) return;
                    int row = m_pointsModel->indexOf(pointId);
                    if (row < 0) return;

                    if (m_segmentsModel) {
                        for (int i = m_segmentsModel->rowCount() - 1; i >= 0; --i) {
                            if (m_segmentsModel->segmentAt(i).pointId == pointId) {
                                m_segmentsModel->removeSegment(i);
                            }
                        }
                    }
                    m_pointsModel->removePoint(row);
                });

        // Go To Point ("jog preview") — previously dead: double-click / the
        // context-menu action emitted goToPointRequested but nothing moved
        // the robot. Sends a single MovJ to the point's saved pose, gated on
        // the same connected+idle check the rest of the app uses.
        connect(m_pointsLibrary, &PointsLibraryWidget::goToPointRequested,
                this, [this](const QString& pointId) {
                    if (!m_pointsModel || !m_connectionService) return;
                    if (!m_connectionService->isConnected()) {
                        QMessageBox::warning(this, "Go To Point", "Robot is not connected.");
                        return;
                    }
                    if (m_connectionService->currentMode() != RobotMode::Idle) {
                        QMessageBox::warning(this, "Go To Point", "Robot is not Idle — cannot jog to point.");
                        return;
                    }
                    CameraPoint pt = m_pointsModel->pointById(pointId);
                    if (pt.id.isEmpty()) return;
                    m_connectionService->enqueueMotionCommand(
                        CommandBuilder::movJ(pt.pose, /*speedPct=*/50, /*accPct=*/50, /*cpValue=*/0.0));
                });
    }

    if (m_timelineView) {
        // Previously unwired: clicking a segment never actually loaded it
        // into the Properties Panel — TimelineScene emitted the selection
        // signals correctly, but nothing downstream connected to them.
        connect(m_timelineView, &TimelineView::segmentSelected,
                m_propertiesPanel, &PropertiesPanel::loadSegment);
        connect(m_timelineView, &TimelineView::segmentDeselected,
                m_propertiesPanel, &PropertiesPanel::clearSelection);
        // Refresh the panel after a drag so its Trigger field doesn't show
        // a stale time (the dragged segment is always the selected one).
        connect(m_timelineView, &TimelineView::segmentMoved,
                this, [this](const QString& segId, double newTime) {
                    m_propertiesPanel->loadSegment(segId);
                    // Carry the point's Gantry/FIZ curve keyframes with it.
                    // Without this the segment moves but its diamonds stay
                    // behind, so the curve rows silently disagree with the
                    // timeline about when the axis is meant to be where.
                    const QString autoId = segId + kAutoKeyframeSuffix;
                    if (m_gantryService) {
                        for (auto kf : m_gantryService->keyframes()) {
                            if (kf.id == autoId) {
                                kf.time = newTime;
                                m_gantryService->updateKeyframe(kf);
                                break;
                            }
                        }
                    }
                    if (m_fizService) {
                        for (auto kf : m_fizService->keyframes()) {
                            if (kf.id == autoId) {
                                kf.time = newTime;
                                m_fizService->updateKeyframe(kf);
                                break;
                            }
                        }
                    }
                });
        // Explain drag/edit clamps instead of letting them look like silent
        // snaps — the gantry physically can't arrive any sooner.
        connect(m_timelineView->scene(), &TimelineScene::segmentMoveClamped,
                this, [this](const QString&, double clampedTime, double) {
                    statusBar()->showMessage(
                        QString("Trigger time clamped to %1s — gantry cannot physically arrive sooner.")
                            .arg(clampedTime, 0, 'f', 2), 5000);
                });
        connect(m_propertiesPanel, &PropertiesPanel::triggerTimeClamped,
                this, [this](double clampedTime, double) {
                    statusBar()->showMessage(
                        QString("Trigger time clamped to %1s — gantry cannot physically arrive sooner.")
                            .arg(clampedTime, 0, 'f', 2), 5000);
                });
        // Delete button — previously deletion was keyboard-only (Delete/
        // Backspace on the timeline) with no button anywhere.
        connect(m_propertiesPanel, &PropertiesPanel::deleteRequested, this, [this](const QString& segId) {
            if (!m_segmentsModel) return;
            int row = m_segmentsModel->indexOf(segId);
            if (row < 0) return;
            if (m_undoStack) {
                m_undoStack->push(new RemoveSegmentCommand(m_segmentsModel, m_segmentsModel->segmentAt(row)));
            } else {
                m_segmentsModel->removeSegment(row);
            }
        });

        connect(m_timelineView, &TimelineView::pointDroppedOnTimeline,
                this, [this](const QString& pointId, double timeSec, const QString& segId) {
                    // Retrieve CameraPoint
                    CameraPoint pt;
                    if (m_pointsLibrary && m_pointsLibrary->listView()->model()) {
                        auto* pointsModel = qobject_cast<PointsModel*>(m_pointsLibrary->listView()->model());
                        if (pointsModel) pt = pointsModel->pointById(pointId);
                    }

                    // Sync Gantry/FIZ keyframes on drop. Their ids are derived
                    // from the segment id (not random) so a later segment drag
                    // can find and carry them along — see segmentMoved below.
                    if (m_gantryService) {
                        GantryKeyframe gkf;
                        gkf.id         = segId + kAutoKeyframeSuffix;
                        gkf.time       = timeSec;
                        gkf.positionMm = pt.id.isEmpty() ? m_gantryService->currentPositionMm() : pt.gantryPositionMm;
                        m_gantryService->addKeyframe(gkf);
                    }

                    if (m_fizService) {
                        FizKeyframe fkf;
                        fkf.id    = segId + kAutoKeyframeSuffix;
                        fkf.time  = timeSec;
                        fkf.state = pt.id.isEmpty() ? m_fizService->currentState() : pt.fizState;
                        m_fizService->addKeyframe(fkf);
                    }
                });
    }

    // ── Gantry Panel ────────────────────────────────────────────────────────
    //
    // Routed through m_axisController rather than bound to one controller by
    // name: with a stepper selected, a jog wired straight to the DC controller
    // would drive the WRONG MOTOR while the readout showed the wrong axis.
    connect(m_teachPanel, &TeachPanel::axisJogRequested, this,
            [this](const QString& axisId, int speed) {
        AxisControllerBase* axis = m_axisManager ? m_axisManager->controller(axisId) : nullptr;
        if (!axis) return;
        // The panel speaks PWM (-255..255). A stepper's jog is steps/sec, so
        // the same number would mean a crawl — scale it to a fraction of the
        // axis's own rate ceiling instead of passing it through raw.
        // The panel speaks PWM (-255..255). A stepper's jog is steps/sec, so
        // scale to a fraction of THAT axis's own rate ceiling.
        int cmd = speed;
        if (qobject_cast<StepperAxisController*>(axis) && m_projectService) {
            double ceiling = m_projectService->project().gantryMotorSpec.stepRateCeilingHz;
            for (const AxisConfig& a : m_projectService->project().axes) {
                if (a.id == axisId) { ceiling = a.motorSpec.stepRateCeilingHz; break; }
            }
            cmd = qRound(speed / 255.0 * ceiling);
        }
        QMetaObject::invokeMethod(axis, [axis, cmd]() { axis->jogGantry(cmd); },
                                  Qt::QueuedConnection);
    });
    connect(m_teachPanel, &TeachPanel::axisJogStopRequested, this,
            [this](const QString& axisId) {
        AxisControllerBase* axis = m_axisManager ? m_axisManager->controller(axisId) : nullptr;
        if (!axis) return;
        QMetaObject::invokeMethod(axis, [axis]() { axis->stopJog(); }, Qt::QueuedConnection);
    });
    connect(m_teachPanel, &TeachPanel::axisHomeRequested, this,
            [this](const QString& axisId) {
        AxisControllerBase* axis = m_axisManager ? m_axisManager->controller(axisId) : nullptr;
        if (!axis) return;
        QMetaObject::invokeMethod(axis, [axis]() { axis->homeGantry(); }, Qt::QueuedConnection);
    });
    // Both controllers report position, but only the active one runs a control
    // loop (see AxisControllerBase::setActive), so only one is ever talking.
    // Reported WITH the axis id: several axes poll independently, and the
    // panel shows only the selected one rather than whichever replied last.
    if (m_axisManager) {
        for (const QString& id : m_axisManager->axisIds()) {
            if (auto* c = m_axisManager->controller(id)) {
                connect(c, &StepperAxisController::positionChanged, this,
                        [this, id](double p) { m_teachPanel->updateAxisPosition(id, p); });
            }
        }
    }
    // Units belong to the SELECTED axis, not to the primary. Each axis has
    // its own linear/rotary setting, so a rotary second axis was reading out
    // in mm purely because the label was seeded once from axis 0.
    connect(m_teachPanel, &TeachPanel::axisSelectionChanged,
            this, [this](const QString& axisId) { refreshAxisUnitLabel(axisId); });
    if (m_teachPanel) refreshAxisUnitLabel(m_teachPanel->selectedAxisId());
    connect(m_axisController, &StepperAxisController::connected,
            this, [this]() { m_teachPanel->setGantryConnected(true); });
    connect(m_axisController, &StepperAxisController::disconnected,
            this, [this]() { m_teachPanel->setGantryConnected(false); });
    // Previously unwired: homing timeouts, serial errors, and travel-limit
    // clamps vanished silently. StructuredLogger already persists them at
    // the point of emission; this makes them visible to the operator too.
    connect(m_axisController, &StepperAxisController::errorOccurred,
            this, [this](const QString& err) { m_diagnosticsPanel->appendLog(err, "ERROR"); });
    connect(m_axisController, &StepperAxisController::errorOccurred,
            this, [this](const QString& err) { m_diagnosticsPanel->appendLog(err, "ERROR"); });
    // A drive alarm is the stepper's only integrity signal, so it gets said
    // out loud rather than only appearing in the log.
    connect(m_axisController, &StepperAxisController::alarmRaised,
            this, [this](const QString& text) {
                m_diagnosticsPanel->appendLog(
                    QString("STEPPER DRIVE ALARM: %1 - position reference lost, "
                            "re-zero the axis before shooting.").arg(text), "ERROR");
            });

    // Record Path (Phase 9): captures continuous hand-jogged gantry motion
    // as an alternative to teaching only discrete fixed points. The sample
    // feed stays connected unconditionally — PathRecorderService::addSample
    // no-ops whenever a recording isn't active.
    connect(m_axisController, &StepperAxisController::positionChanged,
            m_pathRecorder, &PathRecorderService::addSample);
    connect(m_axisController, &StepperAxisController::positionChanged,
            m_pathRecorder, &PathRecorderService::addSample);
    connect(m_teachPanel, &TeachPanel::gantryRecordToggled, this, [this](bool recording) {
        if (recording) {
            m_pathRecorder->startRecording();
        } else {
            m_pathRecorder->stopRecording();
        }
    });
    connect(m_pathRecorder, &PathRecorderService::recordingStopped, this, [this](int sampleCount) {
        if (sampleCount < 2) {
            m_diagnosticsPanel->appendLog(
                QString("Path recording captured only %1 sample(s) — nothing to commit").arg(sampleCount),
                "WARN");
            return;
        }
        auto keyframes = m_pathRecorder->simplifyToGantryKeyframes(1.0 /* mm tolerance */);
        for (const auto& kf : keyframes) {
            m_gantryService->addKeyframe(kf);
        }
        m_diagnosticsPanel->appendLog(
            QString("Recorded gantry path: %1 samples simplified to %2 keyframes")
                .arg(sampleCount).arg(keyframes.size()),
            "INFO");
    });

    // Connect/Disconnect from TeachPanel UI
    connect(m_teachPanel, &TeachPanel::gantryConnectRequested,
            this, [this](const QString& port) {
                // Connecting is a BOARD-level action: any controller on the
                // link opens the same port. Routed through the active axis so
                // it works whichever drive kind is selected.
                QMetaObject::invokeMethod(m_axisController, [this, port]() {
                    m_axisController->connectPort(port);
                }, Qt::QueuedConnection);
            });
    connect(m_teachPanel, &TeachPanel::gantryDisconnectRequested,
            this, [this]() {
                QMetaObject::invokeMethod(m_axisController, [this]() {
                    m_axisController->disconnectPort();
                }, Qt::QueuedConnection);
            });

    // ── FIZ Panel ───────────────────────────────────────────────────────────
    connect(m_fizPanel, &FizPanel::connectRequested,
            this, [this](const QString& port) {
                QMetaObject::invokeMethod(m_nucleusService, [this, port]() {
                    m_nucleusService->connectPort(port);
                }, Qt::QueuedConnection);
            });
    connect(m_fizPanel, &FizPanel::disconnectRequested, this, [this]() {
        QMetaObject::invokeMethod(m_nucleusService,
            &NucleusService::disconnectPort, Qt::QueuedConnection);
    });
    connect(m_fizPanel, &FizPanel::calibrateRequested, this, [this](uint8_t motorId) {
        if (motorId == 0xFF)
            QMetaObject::invokeMethod(m_nucleusService,
                &NucleusService::calibrateAll, Qt::QueuedConnection);
        else
            QMetaObject::invokeMethod(m_nucleusService, [this, motorId]() {
                m_nucleusService->calibrateMotor(motorId);
            }, Qt::QueuedConnection);
    });
    connect(m_fizPanel, &FizPanel::lensMappingRequested,
            this, &MainWindow::onLensMapping);

    // NucleusService → FizPanel connection status
    connect(m_nucleusService, &NucleusService::connected, this, [this](const QString&) {
        m_fizPanel->onConnectionChanged(true);
        m_fizStatusLabel->setText("  FIZ Connected  ");
        m_fizStatusLabel->setStyleSheet("color: #00cc44; font-weight: bold; padding: 2px 6px;");
    });
    connect(m_nucleusService, &NucleusService::disconnected, this, [this]() {
        m_fizPanel->onConnectionChanged(false);
        m_fizStatusLabel->setText("  FIZ Disconnected  ");
        m_fizStatusLabel->setStyleSheet("color: #555555; font-weight: bold; padding: 2px 6px;");
    });
    connect(m_nucleusService, &NucleusService::errorOccurred, this, [this](const QString& err) {
        statusBar()->showMessage("FIZ Error: " + err, 5000);
    });

    // FizService → FIZ panel + track widget
    connect(m_fizService, &FizService::fizStateChanged,
            m_fizPanel,   &FizPanel::onFizStateChanged);
    connect(m_fizService, &FizService::keyframesChanged, this, [this]() {
        m_fizTrackWidget->setKeyframes(m_fizService->keyframes());
    });

    // Timeline scroll sync: FIZ track follows main timeline
    connect(m_timelineView, &TimelineView::scrollOffsetChanged,
            m_fizTrackWidget, &FizTrackWidget::setScrollOffset);
    connect(m_timelineView, &TimelineView::pixelsPerSecondChanged,
            m_fizTrackWidget, &FizTrackWidget::setPixelsPerSecond);

    // FIZ track → add keyframe on double-click
    connect(m_fizTrackWidget, &FizTrackWidget::addKeyframeRequested,
            this, [this](int track, double time, float value) {
                FizKeyframe kf;
                kf.id   = QUuid::createUuid().toString();
                kf.time = time;
                FizState current = m_fizService->currentState();
                if (track == 0)      { kf.state = current; kf.state.focus = value; }
                else if (track == 1) { kf.state = current; kf.state.iris  = value; }
                else                 { kf.state = current; kf.state.zoom  = value; }
                m_fizService->addKeyframe(kf);
            });

    // Gantry track → add keyframe on double-click
    connect(m_fizTrackWidget, &FizTrackWidget::addGantryKeyframeRequested,
            this, [this](double time, float valueMm) {
                GantryKeyframe kf;
                kf.id         = QUuid::createUuid().toString();
                kf.positionMm = valueMm;
                // Placed by hand at an arbitrary time — floor it so the move
                // from the preceding keyframe is actually achievable, instead
                // of only finding out at Play time.
                kf.time = flooredGantryKeyframeTime(kf.id, time, valueMm);
                if (kf.time > time + 1e-6) {
                    statusBar()->showMessage(
                        QString("Gantry keyframe placed at %1s — the axis cannot reach that "
                                "position any sooner.").arg(kf.time, 0, 'f', 2), 5000);
                }
                m_gantryService->addKeyframe(kf);
            });

    // GantryService → track widget keyframe updates
    connect(m_gantryService, &GantryService::keyframesChanged, this, [this]() {
        m_fizTrackWidget->setGantryKeyframes(m_gantryService->keyframes());
    });

    // FIZ/Gantry track keyframes — retime (drag) and delete. Previously these
    // curve keyframes (independent of the segment blocks above — see
    // TimelineCompiler's merge of GantryService/FizService keyframes onto the
    // same track) could be added via double-click but never touched again,
    // so an infeasible gap between two of them could never be fixed from the UI.
    connect(m_fizTrackWidget, &FizTrackWidget::fizKeyframeMoved,
            this, [this](const FizKeyframe& kf) { m_fizService->updateKeyframe(kf); });
    connect(m_fizTrackWidget, &FizTrackWidget::gantryKeyframeMoved,
            this, [this](const GantryKeyframe& kf) {
                // Same floor as placement — dragging a diamond earlier than
                // the axis can physically get there snaps it back.
                GantryKeyframe adjusted = kf;
                adjusted.time = flooredGantryKeyframeTime(kf.id, kf.time, kf.positionMm);
                if (adjusted.time > kf.time + 1e-6) {
                    statusBar()->showMessage(
                        QString("Gantry keyframe clamped to %1s — the axis cannot reach that "
                                "position any sooner.").arg(adjusted.time, 0, 'f', 2), 5000);
                }
                m_gantryService->updateKeyframe(adjusted);
            });
    connect(m_fizTrackWidget, &FizTrackWidget::fizKeyframeDeleteRequested,
            this, [this](const QString& id) { m_fizService->removeKeyframe(id); });
    connect(m_fizTrackWidget, &FizTrackWidget::gantryKeyframeDeleteRequested,
            this, [this](const QString& id) { m_gantryService->removeKeyframe(id); });

    // Playback → Track Widget playhead sync
    connect(m_playbackService, &PlaybackService::playheadTimeUpdated, this, [this](double timeSec) {
        if (m_fizTrackWidget) m_fizTrackWidget->setPlayheadTime(timeSec);
    });

    // Previously unwired: preflight failures, adapter errors, and E-STOP
    // from playback vanished silently. StructuredLogger already persists
    // them at the point of emission; this makes them visible to the operator.
    connect(m_playbackService, &PlaybackService::errorOccurred,
            this, [this](const QString& err) {
                m_diagnosticsPanel->appendLog(err, "ERROR");
                // Play is a user-initiated action — a rejected/degraded start
                // must be in the operator's face, not only in a log panel.
                QMessageBox::warning(this, "Playback", err);
            });
}

// ─── Slot Implementations ───────────────────────────────────────────────────────

void MainWindow::onNewProject()
{
    m_projectService->newProject();
    setWindowTitle("CamBot Timeline — Untitled *");
}

void MainWindow::onOpenProject()
{
    QString path = QFileDialog::getOpenFileName(this, "Open Project", "",
        "CamBot Project (*.crp);;All Files (*)");
    if (!path.isEmpty()) {
        m_projectService->loadProject(path);
        setWindowTitle(QString("CamBot Timeline — %1").arg(QFileInfo(path).fileName()));
    }
}

void MainWindow::onSaveProject()
{
    m_projectService->saveProject();
}

void MainWindow::onSaveProjectAs()
{
    QString path = QFileDialog::getSaveFileName(this, "Save Project As", "",
        "CamBot Project (*.crp)");
    if (!path.isEmpty())
        m_projectService->saveProjectAs(path);
}

void MainWindow::onExportCsv()
{
    if (!m_exportService) return;
    QString path = QFileDialog::getSaveFileName(this, "Export CSV", "", "CSV (*.csv)");
    if (path.isEmpty()) return;

    if (m_exportService->exportTo(path, ExportService::CSV))
        QMessageBox::information(this, "Export", "CSV exported to " + path);
    else
        QMessageBox::warning(this, "Export Failed", "Could not export CSV to " + path);
}

void MainWindow::onExportPython()
{
    if (!m_exportService) return;
    QString path = QFileDialog::getSaveFileName(this, "Export Python Script", "", "Python (*.py)");
    if (path.isEmpty()) return;

    if (m_exportService->exportTo(path, ExportService::Python))
        QMessageBox::information(this, "Export", "Python script exported to " + path);
    else
        QMessageBox::warning(this, "Export Failed", "Could not export Python script to " + path);
}

void MainWindow::onConnectRobot()
{
    bool ok;
    QString ip = QInputDialog::getText(this, "Connect to Robot",
        "Robot IP Address:", QLineEdit::Normal, "192.168.1.6", &ok);
    if (ok && !ip.isEmpty()) {
        m_connectionService->connectToRobot(ip);
        m_connectionLabel->setText("  Connecting...  ");
        m_connectionLabel->setStyleSheet("color: #ffaa00; font-weight: bold; padding: 2px 8px;");
    }
}

void MainWindow::onDisconnectRobot()
{
    m_connectionService->disconnectFromRobot();
}

void MainWindow::onEmergencyStop()
{
    qWarning() << "!!! E-STOP TRIGGERED !!!";
    m_connectionService->emergencyStop();
    m_playbackService->stop();

    // The gantry needs stopping explicitly, not just via PlaybackService:
    // PlaybackEngine::stop() returns early when playback is already stopped,
    // and a PID tuning run REQUIRES playback stopped — so E-STOP would
    // Every axis, not just the primary: a stop that leaves one still running
    // is not a stop.
    if (m_axisManager) {
        for (const QString& id : m_axisManager->axisIds()) {
            if (auto* c = m_axisManager->controller(id)) {
                QMetaObject::invokeMethod(c, [c]() { c->stopJog(); }, Qt::QueuedConnection);
            }
        }
    }

    m_statusBarWidget->flashEmergency();
}

void MainWindow::onRobotConnected()
{
    m_connectionLabel->setText("  Connected  ");
    m_connectionLabel->setStyleSheet("color: #00cc44; font-weight: bold; padding: 2px 8px;");
    m_connectAction->setEnabled(false);
    m_disconnectAction->setEnabled(true);
    m_teachPanel->setRobotConnected(true);
}

void MainWindow::onRobotDisconnected()
{
    m_connectionLabel->setText("  Disconnected  ");
    m_connectionLabel->setStyleSheet("color: #cc0000; font-weight: bold; padding: 2px 8px;");
    m_connectAction->setEnabled(true);
    m_disconnectAction->setEnabled(false);
    m_teachPanel->setRobotConnected(false);
}

void MainWindow::onRobotError(const QString& error)
{
    QMessageBox::warning(this, "Robot Error", error);
}

void MainWindow::onPlay()  { m_playbackService->play();  }
void MainWindow::onPause() { m_playbackService->pause(); }
void MainWindow::onStop()  { m_playbackService->stop();  }

void MainWindow::onConnectCamera() { if (m_mediaService) m_mediaService->openUsbCamera(0); }

void MainWindow::onSnapshot()    { if (m_mediaService) m_mediaService->takeSnapshot(); }
void MainWindow::onStartRecord() { if (m_mediaService) m_mediaService->startRecording(); }
void MainWindow::onStopRecord()  { if (m_mediaService) m_mediaService->stopRecording(); }

void MainWindow::onFizConnect()
{
    m_fizPanel->refreshPorts();
    // Switch to FIZ tab so user can see the port selector
    if (m_rightTabs) m_rightTabs->setCurrentIndex(1); // FIZ tab
}

void MainWindow::onFizSetupWizard()
{
    FizSetupWizard wizard(m_nucleusService, this);
    wizard.exec();
}

void MainWindow::onLensMapping()
{
    LensMappingDialog dlg(m_nucleusService->lensMapping(), this);
    if (dlg.exec() == QDialog::Accepted) {
        m_nucleusService->setLensMapping(dlg.result());
    }
}

void MainWindow::onCalibration()
{
    CalibrationDialog dlg(m_connectionService, this);
    dlg.exec();
}

void MainWindow::onGantryMotorSetup()
{
    GantrySetupDialog dlg(m_projectService, this);
    dlg.exec();
}

void MainWindow::refreshTrackWidgetAxes()
{
    if (!m_fizTrackWidget || !m_projectService) return;

    QList<FizTrackWidget::AxisRow> rows;
    QList<AxisConfig> axes = m_projectService->project().axes;
    if (axes.isEmpty()) {
        AxisConfig primary;
        primary.tuning = m_projectService->project().gantryTuning;
        axes.append(primary);
    }
    for (const AxisConfig& a : axes) {
        FizTrackWidget::AxisRow row;
        row.id   = a.id;
        row.name = a.displayName.isEmpty() ? a.id.toUpper() : a.displayName.toUpper();
        // The row's scale IS the axis's travel range. Previously two constants
        // stood in for this and disagreed with each other, so a keyframe
        // dropped by double-click redrew somewhere else.
        row.rangeMin = a.tuning.travelLimits.minMm;
        row.rangeMax = a.tuning.travelLimits.maxMm;
        rows.append(row);
    }
    m_fizTrackWidget->setAxes(rows);

    if (m_teachPanel) {
        QStringList ids, names;
        for (const FizTrackWidget::AxisRow& r : rows) { ids << r.id; names << r.name; }
        m_teachPanel->setAxes(ids, names);
    }
}

void MainWindow::refreshAxisPointers()
{
    if (!m_axisManager) return;
    m_axisController = m_axisManager->primary();

    // Both of these cache the pointer, and a stale cache is exactly how
    // setpoints end up at the axis nobody is driving.
    if (m_gantryService)   m_gantryService->initialize(m_axisController);
    if (m_playbackService) {
        m_playbackService->setAdditionalServices(m_gantryService, m_fizService,
                                                 m_axisController, m_nucleusService);

        // Every axis past the first needs its own adapter, or its compiled
        // track streams into nothing: no error, just an axis that never moves.
        for (const QString& id : m_axisManager->axisIds()) {
            if (id == "gantry") continue;
            m_playbackService->registerAxisAdapter(id, m_axisManager->controller(id));
        }
    }
}

void MainWindow::refreshAxisUnitLabel(const QString& axisId)
{
    if (!m_teachPanel || !m_projectService) return;

    for (const AxisConfig& a : m_projectService->project().axes) {
        if (a.id != axisId) continue;
        m_teachPanel->setAxisUnitLabel(motion::unitLabel(a.motorSpec));
        return;
    }
    // Unknown axis (or a project with no list yet): fall back to the primary.
    m_teachPanel->setAxisUnitLabel(
        motion::unitLabel(m_projectService->project().gantryMotorSpec));
}

void MainWindow::applyAxisConfiguration()
{
    if (!m_axisManager || !m_projectService) return;

    // The manager creates whatever is new, reconfigures whatever exists, and
    // refuses anything it cannot honour (too many axes, or a duplicate board
    // address) with a reason that reaches the diagnostics panel.
    QList<AxisConfig> axes = m_projectService->project().axes;
    if (axes.isEmpty()) {
        AxisConfig primary;
        primary.motorSpec = m_projectService->project().gantryMotorSpec;
        primary.tuning    = m_projectService->project().gantryTuning;
        axes.append(primary);
    }
    m_axisManager->configure(axes);
    refreshAxisPointers();
}

double MainWindow::flooredGantryKeyframeTime(const QString& excludeId,
                                             double proposedTime,
                                             double positionUnits) const
{
    if (!m_gantryService || !m_projectService) return proposedTime;

    const GantryMotorSpec& spec = m_projectService->project().gantryMotorSpec;

    // Nearest keyframe strictly before the proposed time (excluding the one
    // being moved, which would otherwise anchor against itself).
    bool havePrev = false;
    GantryKeyframe prev;
    for (const auto& kf : m_gantryService->keyframes()) {
        if (kf.id == excludeId) continue;
        if (kf.time <= proposedTime && (!havePrev || kf.time > prev.time)) {
            prev = kf;
            havePrev = true;
        }
    }
    if (!havePrev) return proposedTime; // nothing precedes it — unconstrained

    double minGap = motion::minGantryDurationForDistanceSec(
        positionUnits - prev.positionMm, spec, 0.0);
    return qMax(proposedTime, prev.time + minGap);
}

void MainWindow::pushGantryTuning()
{
    if (!m_projectService) return;

    // configure() re-selects each axis's drive kind AND pushes its settings
    // to whichever controller ends up driving it, so this is one call now.
    // Doing it in that order matters: pushing first would land the settings
    // on the axis we are about to stop driving.
    applyAxisConfiguration();
}

void MainWindow::onDiagnostics()
{
    // Switch to Diagnostics tab
    if (m_rightTabs) m_rightTabs->setCurrentIndex(2);
}

// ─── Events ─────────────────────────────────────────────────────────────────────

void MainWindow::closeEvent(QCloseEvent* event)
{
    saveWindowState();
    event->accept();
}

void MainWindow::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_F12) { onEmergencyStop(); return; }
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) { onPlay(); return; }
    QMainWindow::keyPressEvent(event);
}

void MainWindow::saveWindowState()
{
    QSettings settings;
    settings.setValue("mainwindow/geometry", saveGeometry());
    settings.setValue("mainwindow/state",    saveState());
}

void MainWindow::restoreWindowState()
{
    QSettings settings;
    restoreGeometry(settings.value("mainwindow/geometry").toByteArray());
    restoreState(settings.value("mainwindow/state").toByteArray());

    // Deferred clamp — QTimer::singleShot(0) ensures the layout engine has
    // finished computing sizes from the restored state before we clamp.
    QTimer::singleShot(0, this, [this]() { clampBottomSplitter(); });
}

void MainWindow::clampBottomSplitter()
{
    if (!m_bottomSplitter) return;

    const int totalWidth = m_bottomSplitter->width();
    if (totalWidth <= 0) return;

    QList<int> sizes = m_bottomSplitter->sizes();
    if (sizes.size() < 3) return;

    const int handleW = m_bottomSplitter->handleWidth();
    const int minProp  = 160;
    const int minLib   = 80;
    const int minCam   = 200;
    bool changed = false;

    if (sizes[0] < minProp) { sizes[0] = minProp; changed = true; }
    if (sizes[1] < minLib)  { sizes[1] = minLib;  changed = true; }
    if (sizes[2] < minCam)  { sizes[2] = minCam;  changed = true; }

    if (changed) {
        // Redistribute: give any excess to camera (index 2)
        int used = sizes[0] + sizes[1] + sizes[2] + handleW * 2;
        if (used < totalWidth) {
            sizes[2] += totalWidth - used;
        }
        m_bottomSplitter->setSizes(sizes);
    }
}
