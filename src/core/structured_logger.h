#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Structured Logger (Phase 4: persistent diagnostics)
// Writes categorized, timestamped JSON-lines entries to a per-session log
// file on disk, so safety-relevant events (E-STOP, limit violations, homing
// timeouts, connection loss) survive an app restart — today they only ever
// reach an in-memory UI text view (or nothing at all).
// Thread-safe: gantry/robot code paths run on their own QThreads and all
// call log() directly, not via queued signals.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QString>
#include <QDateTime>
#include <QFile>
#include <QMutex>
#include <QList>

class StructuredLogger
{
public:
    enum class Category { Safety, Motion, Connection, Error, Info };

    struct LogEntry {
        QDateTime timestamp;
        Category  category = Category::Info;
        QString   subsystem;
        QString   message;
    };

    static StructuredLogger& instance();

    /// Opens a new timestamped session log file under directoryPath
    /// (defaults to "<app dir>/logs"). Each call starts a fresh file — that's
    /// the "rotated per session" behavior. Safe to call more than once (e.g.
    /// in tests) — the previous file, if any, is closed first.
    bool openSession(const QString& directoryPath = QString());

    /// Appends one entry. No-ops (silently) if no session is open yet, so
    /// call sites don't need to guard on whether logging has been set up.
    void log(Category category, const QString& subsystem, const QString& message);

    QString currentLogFilePath() const;

    /// Reads a session log file back into entries — used by tests, and
    /// available for a future "load past session" diagnostics view.
    static QList<LogEntry> readEntries(const QString& filePath);

    static QString categoryName(Category category);
    static Category categoryFromName(const QString& name);

private:
    StructuredLogger() = default;
    ~StructuredLogger();
    StructuredLogger(const StructuredLogger&) = delete;
    StructuredLogger& operator=(const StructuredLogger&) = delete;

    QFile   m_file;
    QMutex  m_mutex;
    QString m_filePath;
};
