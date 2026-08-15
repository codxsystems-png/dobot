// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Diagnostics Panel
// ═══════════════════════════════════════════════════════════════════════════════

#include "ui/diagnostics_panel.h"
#include "services/connection_service.h"
#include "core/feedback_parser.h"
#include "core/structured_logger.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QGroupBox>
#include <QFormLayout>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QScrollArea>
#include <QFrame>

DiagnosticsPanel::DiagnosticsPanel(ConnectionService* connService, QWidget* parent)
    : QWidget(parent)
    , m_connService(connService)
{
    setupUI();
}

void DiagnosticsPanel::setupUI()
{
    // Outer layout holds the scroll area
    QVBoxLayout* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    QScrollArea* scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    QWidget* innerWidget = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(innerWidget);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    // ─── Status summary ──────────────────────────────────────────────────
    QGroupBox* statusGroup = new QGroupBox("Live Status");
    statusGroup->setMinimumHeight(90);
    QFormLayout* statusForm = new QFormLayout(statusGroup);
    statusForm->setSpacing(3);

    m_modeLabel  = new QLabel("—");
    m_errorLabel = new QLabel("—");
    m_speedLabel = new QLabel("—");

    m_modeLabel->setStyleSheet("font-family: Consolas; color: #00cc44;");
    m_errorLabel->setStyleSheet("font-family: Consolas; color: #ff4444;");
    m_speedLabel->setStyleSheet("font-family: Consolas; color: #cccccc;");

    statusForm->addRow("Mode:",  m_modeLabel);
    statusForm->addRow("Error:", m_errorLabel);
    statusForm->addRow("Speed:", m_speedLabel);
    layout->addWidget(statusGroup);

    // ─── Log view ────────────────────────────────────────────────────────
    QLabel* logTitle = new QLabel("Event Log");
    logTitle->setStyleSheet("font-weight: bold; color: #e0e0e0;");
    layout->addWidget(logTitle);

    m_logView = new QTextEdit();
    m_logView->setReadOnly(true);
    m_logView->setFont(QFont("Consolas", 8));
    m_logView->setStyleSheet("background: #1a1a1a; color: #cccccc; border: 1px solid #333;");
    m_logView->setMinimumHeight(200);
    layout->addWidget(m_logView, 1);

    // ─── Controls ────────────────────────────────────────────────────────
    QHBoxLayout* controls = new QHBoxLayout();

    QPushButton* clearBtn = new QPushButton("Clear");
    clearBtn->setFixedWidth(60);
    connect(clearBtn, &QPushButton::clicked, this, &DiagnosticsPanel::clearLog);
    controls->addWidget(clearBtn);

    QPushButton* exportBtn = new QPushButton("Export Log…");
    exportBtn->setFixedWidth(90);
    connect(exportBtn, &QPushButton::clicked, this, &DiagnosticsPanel::exportLog);
    controls->addWidget(exportBtn);

    controls->addStretch();
    layout->addLayout(controls);

    scrollArea->setWidget(innerWidget);
    outerLayout->addWidget(scrollArea);

    // Fixed width to match the parent QTabWidget (360px).
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    setFixedWidth(360);
}

QString DiagnosticsPanel::levelColor(const QString& level) const
{
    if (level == "ERROR" || level == "ESTOP") return "#ff4444";
    if (level == "WARN")                       return "#ffaa00";
    if (level == "INFO")                       return "#aaaaaa";
    return "#cccccc";
}

void DiagnosticsPanel::appendLog(const QString& message, const QString& level)
{
    QString ts    = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    QString color = levelColor(level);
    QString html  = QString("<span style='color:#555555'>[%1]</span> "
                            "<span style='color:%2;font-weight:bold'>[%3]</span> "
                            "<span style='color:#cccccc'>%4</span>")
                        .arg(ts, color, level, message.toHtmlEscaped());

    m_logView->append(html);

    // Keep log bounded at 2000 lines
    while (m_logView->document()->blockCount() > 2000) {
        QTextCursor cur = m_logView->textCursor();
        cur.movePosition(QTextCursor::Start);
        cur.select(QTextCursor::BlockUnderCursor);
        cur.removeSelectedText();
        cur.deleteChar();
    }

    // Persist to disk in addition to the in-memory view above — previously
    // every entry here only ever lived in this QTextEdit and vanished on
    // app close unless the operator remembered to hit "Export Log…".
    StructuredLogger::Category category = StructuredLogger::Category::Info;
    if (level == "ESTOP")                     category = StructuredLogger::Category::Safety;
    else if (level == "ERROR" || level == "WARN") category = StructuredLogger::Category::Error;
    StructuredLogger::instance().log(category, "UI", message);
}

void DiagnosticsPanel::clearLog()
{
    m_logView->clear();
}

void DiagnosticsPanel::exportLog()
{
    QString defaultPath = QString("%1/CamBot/Logs/log_%2.txt")
        .arg(QDir::homePath(),
             QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss"));

    QString path = QFileDialog::getSaveFileName(this, "Export Log",
                                                defaultPath,
                                                "Text Files (*.txt)");
    if (path.isEmpty()) return;

    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;

    QTextStream out(&file);
    out << m_logView->toPlainText();
    file.close();

    appendLog("Log exported to: " + path);
}

void DiagnosticsPanel::onFeedbackUpdated(const FeedbackData& data)
{
    // Map RobotMode enum to display string
    RobotMode rm = data.robotMode(); // robotMode() is a method
    int modeInt  = static_cast<int>(rm);

    auto modeStr = [](int m) -> QString {
        switch (m) {
        case 1:  return "Init";
        case 2:  return "BrakeOpen";
        case 3:  return "PowerOff";
        case 4:  return "Disabled";
        case 5:  return "Idle";
        case 6:  return "Drag";
        case 7:  return "Running";
        case 8:  return "SingleMove";
        case 9:  return "Error";
        case 10: return "Pause";
        case 11: return "Collision";
        default: return QString("Mode(%1)").arg(m);
        }
    };

    m_modeLabel->setText(modeStr(modeInt));
    // Use errorStatus (int8_t) as the error indicator
    m_errorLabel->setText(data.errorStatus == 0
                           ? "None"
                           : QString("err=0x%1").arg((uint8_t)data.errorStatus, 2, 16, QChar('0')));
    m_speedLabel->setText(QString("%1%").arg(data.speedScaling, 0, 'f', 1));

    // Color-code mode label
    if (rm == RobotMode::Idle)
        m_modeLabel->setStyleSheet("font-family:Consolas; color:#00cc44;");
    else if (rm == RobotMode::Error || rm == RobotMode::Collision)
        m_modeLabel->setStyleSheet("font-family:Consolas; color:#ff4444;");
    else
        m_modeLabel->setStyleSheet("font-family:Consolas; color:#ffaa00;");
}
