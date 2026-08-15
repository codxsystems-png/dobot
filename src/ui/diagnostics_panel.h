#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Diagnostics Panel (Phase 6h)
// Log export, error history, force/torque monitor.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QWidget>
#include <QTextEdit>
#include <QLabel>
#include <QTimer>
#include <QFormLayout>
#include "core/types.h"
#include "core/feedback_parser.h"

class ConnectionService;

class DiagnosticsPanel : public QWidget
{
    Q_OBJECT
public:
    explicit DiagnosticsPanel(ConnectionService* connService,
                              QWidget* parent = nullptr);

public slots:
    void appendLog(const QString& message,
                   const QString& level = "INFO");
    void clearLog();
    void exportLog();
    void onFeedbackUpdated(const FeedbackData& data);

private:
    void setupUI();
    QString levelColor(const QString& level) const;

    ConnectionService* m_connService = nullptr;
    QTextEdit*         m_logView     = nullptr;
    QLabel*            m_modeLabel   = nullptr;
    QLabel*            m_errorLabel  = nullptr;
    QLabel*            m_speedLabel  = nullptr;
};
