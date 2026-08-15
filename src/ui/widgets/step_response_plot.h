#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Step Response Plot
// Draws a captured step response: commanded setpoint vs measured position over
// time, with an optional PWM trace. Rendered with QPainter following the same
// conventions as FizTrackWidget — this project has no charting library, and
// pulling one in for a single diagnostic plot isn't worth the dependency.
//
// Display only: no hit-testing, no interaction. Feed it samples and it draws.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QWidget>
#include <QVector>
#include <QString>
#include "core/step_response_metrics.h"

class StepResponsePlot : public QWidget
{
    Q_OBJECT
public:
    explicit StepResponsePlot(QWidget* parent = nullptr);

    void setSamples(const QVector<tuning::StepSample>& samples);
    void setSetpoint(double setpoint);
    void setUnitLabel(const QString& label);   // "mm" | "deg"
    void setShowPwm(bool show);
    /// Draws a CAPTURING badge and keeps the x-axis growing smoothly.
    void setCapturing(bool capturing);
    /// Band drawn around the final value, matching computeStepMetrics().
    void setSettlingBandPercent(double percent);
    void clear();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    struct Ranges {
        double tMin = 0.0, tMax = 2.0;
        double vMin = 0.0, vMax = 1.0;
        bool   valid = false;
    };
    Ranges computeRanges() const;

    void drawEmptyState(QPainter& p, const QRect& plotRect) const;
    void drawGrid(QPainter& p, const QRect& plotRect, const Ranges& r) const;
    void drawTraces(QPainter& p, const QRect& plotRect, const Ranges& r) const;
    void drawOverlays(QPainter& p, const QRect& plotRect, const Ranges& r) const;
    void drawPwmTrace(QPainter& p, const QRect& plotRect, const Ranges& r) const;

    QVector<tuning::StepSample> m_samples;
    double  m_setpoint      = 0.0;
    QString m_unitLabel     = "mm";
    bool    m_showPwm       = false;
    bool    m_capturing     = false;
    double  m_settlingBandPercent = 2.0;
};
