// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Application Entry Point
// ═══════════════════════════════════════════════════════════════════════════════

#include <QApplication>
#include <QFile>
#include <QDebug>

#include "ui/main_window.h"
#include "core/structured_logger.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    // Application metadata
    QApplication::setApplicationName("CamBotTimeline");
    QApplication::setApplicationVersion("1.2.0");
    QApplication::setOrganizationName("CODX Systems");
    QApplication::setOrganizationDomain("codxsystems.com");

    // Persistent session log — safety-relevant events (E-STOP, limit
    // violations, homing timeouts, connection loss) now survive a restart
    // instead of only ever living in an in-memory UI view.
    if (!StructuredLogger::instance().openSession()) {
        qWarning() << "Failed to open session log file — logging will be console-only this run";
    }

    // Load dark theme stylesheet
    QFile styleFile(":/style/dark_theme.qss");
    if (styleFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        app.setStyleSheet(styleFile.readAll());
        styleFile.close();
        qDebug() << "Dark theme loaded";
    } else {
        qWarning() << "Failed to load dark theme stylesheet";
    }

    // Create and show MainWindow
    MainWindow mainWindow;
    mainWindow.show();

    return app.exec();
}
