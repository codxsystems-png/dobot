// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Structured Logger
// ═══════════════════════════════════════════════════════════════════════════════

#include "core/structured_logger.h"
#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QMutexLocker>
#include <QDebug>

StructuredLogger& StructuredLogger::instance()
{
    static StructuredLogger logger;
    return logger;
}

StructuredLogger::~StructuredLogger()
{
    QMutexLocker locker(&m_mutex);
    if (m_file.isOpen()) {
        m_file.close();
    }
}

bool StructuredLogger::openSession(const QString& directoryPath)
{
    QMutexLocker locker(&m_mutex);

    if (m_file.isOpen()) {
        m_file.close();
    }

    QString dir = directoryPath;
    if (dir.isEmpty()) {
        dir = QCoreApplication::applicationDirPath() + "/logs";
    }

    if (!QDir().mkpath(dir)) {
        qWarning() << "StructuredLogger: failed to create log directory" << dir;
        return false;
    }

    QString fileName = QString("session_%1.jsonl")
        .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    m_filePath = dir + "/" + fileName;

    m_file.setFileName(m_filePath);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        qWarning() << "StructuredLogger: failed to open log file" << m_filePath;
        return false;
    }

    return true;
}

void StructuredLogger::log(Category category, const QString& subsystem, const QString& message)
{
    QMutexLocker locker(&m_mutex);
    if (!m_file.isOpen()) {
        return; // No session opened — logging is best-effort, not a hard dependency.
    }

    QJsonObject obj;
    obj["ts"]        = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    obj["category"]  = categoryName(category);
    obj["subsystem"] = subsystem;
    obj["message"]   = message;

    QTextStream out(&m_file);
    out << QJsonDocument(obj).toJson(QJsonDocument::Compact) << "\n";
    out.flush();
    m_file.flush();
}

QString StructuredLogger::currentLogFilePath() const
{
    return m_filePath;
}

QList<StructuredLogger::LogEntry> StructuredLogger::readEntries(const QString& filePath)
{
    QList<LogEntry> entries;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return entries;
    }

    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine();
        if (line.trimmed().isEmpty()) continue;

        QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8());
        if (!doc.isObject()) continue;

        QJsonObject obj = doc.object();
        LogEntry entry;
        entry.timestamp = QDateTime::fromString(obj["ts"].toString(), Qt::ISODateWithMs);
        entry.category  = categoryFromName(obj["category"].toString());
        entry.subsystem = obj["subsystem"].toString();
        entry.message   = obj["message"].toString();
        entries.append(entry);
    }

    return entries;
}

QString StructuredLogger::categoryName(Category category)
{
    switch (category) {
    case Category::Safety:     return "SAFETY";
    case Category::Motion:     return "MOTION";
    case Category::Connection: return "CONNECTION";
    case Category::Error:      return "ERROR";
    case Category::Info:       return "INFO";
    }
    return "INFO";
}

StructuredLogger::Category StructuredLogger::categoryFromName(const QString& name)
{
    if (name == "SAFETY")     return Category::Safety;
    if (name == "MOTION")     return Category::Motion;
    if (name == "CONNECTION") return Category::Connection;
    if (name == "ERROR")      return Category::Error;
    return Category::Info;
}
