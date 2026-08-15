// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Step Response Plot
// ═══════════════════════════════════════════════════════════════════════════════

#include "ui/widgets/step_response_plot.h"
#include <QPainter>
#include <QPaintEvent>
#include <algorithm>
#include <cmath>

namespace {

// Palette matches the rest of the timeline UI: the setpoint uses the gantry
// readout accent, the measured trace the GANTRY track colour.
const QColor kBackground   (20, 20, 20);
const QColor kGridLine     (45, 45, 45);
const QColor kAxisText     (150, 150, 150);
const QColor kSetpointCol  (0x55, 0xAA, 0xFF);
const QColor kMeasuredCol  (0xFF, 0x66, 0x33);
const QColor kPwmCol       (0x88, 0x88, 0x88);
const QColor kEmptyText    (90, 90, 90);
const QColor kBadgeCol     (0xFF, 0x66, 0x33);

constexpr int kMarginLeft    = 58;
constexpr int kMarginBottom  = 24;
constexpr int kMarginTop     = 10;
constexpr int kMarginRight   = 12;
constexpr int kPwmAxisWidth  = 40;   // extra right margin when the PWM trace is on
constexpr int kGridDivisions = 5;
constexpr int kMaxPwm        = 255;

constexpr double kMinTimeSpan = 2.0;   // keep an in-progress capture from jittering
constexpr double kYPadding    = 0.10;  // 10% headroom above/below the data

} // namespace

StepResponsePlot::StepResponsePlot(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(360, 220);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void StepResponsePlot::setSamples(const QVector<tuning::StepSample>& samples)
{
    m_samples = samples;
    update();
}

void StepResponsePlot::setSetpoint(double setpoint)      { m_setpoint = setpoint; update(); }
void StepResponsePlot::setUnitLabel(const QString& label){ m_unitLabel = label;   update(); }
void StepResponsePlot::setShowPwm(bool show)             { m_showPwm = show;      update(); }
void StepResponsePlot::setCapturing(bool capturing)      { m_capturing = capturing; update(); }

void StepResponsePlot::setSettlingBandPercent(double percent)
{
    m_settlingBandPercent = percent;
    update();
}

void StepResponsePlot::clear()
{
    m_samples.clear();
    m_capturing = false;
    update();
}

StepResponsePlot::Ranges StepResponsePlot::computeRanges() const
{
    Ranges r;
    if (m_samples.isEmpty()) return r;

    // X grows with the capture but never shrinks below a floor, so an
    // in-progress trace extends smoothly instead of rescaling every frame.
    r.tMin = 0.0;
    r.tMax = std::max(kMinTimeSpan, m_samples.last().t * 1.05);

    double lo = m_samples.first().measured;
    double hi = lo;
    for (const auto& s : m_samples) {
        lo = std::min(lo, s.measured);
        hi = std::max(hi, s.measured);
    }

    // Always frame the whole step, even if the axis never actually moved —
    // otherwise a stalled run draws a flat line filling the plot and looks
    // like a perfectly tracked response.
    double start = m_samples.first().measured;
    lo = std::min({lo, m_setpoint, start});
    hi = std::max({hi, m_setpoint, start});

    double span = hi - lo;
    if (span < 1e-6) {
        // Degenerate: pick an arbitrary but sane window so we never divide by
        // zero mapping values to pixels.
        span = std::max(1.0, std::abs(hi) * 0.1);
        lo -= span / 2.0;
        hi += span / 2.0;
    } else {
        lo -= span * kYPadding;
        hi += span * kYPadding;
    }

    r.vMin = lo;
    r.vMax = hi;
    r.valid = true;
    return r;
}

void StepResponsePlot::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    p.fillRect(rect(), kBackground);

    const int rightMargin = kMarginRight + (m_showPwm ? kPwmAxisWidth : 0);
    QRect plotRect(kMarginLeft, kMarginTop,
                   width() - kMarginLeft - rightMargin,
                   height() - kMarginTop - kMarginBottom);
    if (plotRect.width() < 20 || plotRect.height() < 20) return;

    Ranges r = computeRanges();

    drawGrid(p, plotRect, r);

    if (!r.valid) {
        drawEmptyState(p, plotRect);
        return;
    }

    drawOverlays(p, plotRect, r);
    if (m_showPwm) drawPwmTrace(p, plotRect, r);
    drawTraces(p, plotRect, r);

    // Frame last so traces can't paint over the border.
    p.setPen(QPen(kGridLine, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(plotRect);

    if (m_capturing) {
        QFont f("Consolas", 8, QFont::Bold);
        p.setFont(f);
        p.setPen(kBadgeCol);
        p.drawText(plotRect.adjusted(0, 4, -6, 0), Qt::AlignRight | Qt::AlignTop, "CAPTURING");
    }
}

void StepResponsePlot::drawEmptyState(QPainter& p, const QRect& plotRect) const
{
    p.setPen(kEmptyText);
    p.setFont(QFont("Consolas", 9));
    p.drawText(plotRect, Qt::AlignCenter, "No capture — press Run Step Test");

    p.setPen(QPen(kGridLine, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(plotRect);
}

void StepResponsePlot::drawGrid(QPainter& p, const QRect& plotRect, const Ranges& r) const
{
    QFont f("Consolas", 8, QFont::Bold);
    p.setFont(f);

    for (int i = 0; i <= kGridDivisions; ++i) {
        double frac = static_cast<double>(i) / kGridDivisions;

        // Horizontal lines + value labels (top row is vMax)
        int y = plotRect.bottom() - static_cast<int>(frac * plotRect.height());
        p.setPen(QPen(kGridLine, 1));
        p.drawLine(plotRect.left(), y, plotRect.right(), y);

        if (r.valid) {
            double value = r.vMin + frac * (r.vMax - r.vMin);
            p.setPen(kAxisText);
            p.drawText(QRect(0, y - 8, kMarginLeft - 6, 16),
                       Qt::AlignRight | Qt::AlignVCenter,
                       QString::number(value, 'f', 1));
        }

        // Vertical lines + time labels
        int x = plotRect.left() + static_cast<int>(frac * plotRect.width());
        p.setPen(QPen(kGridLine, 1));
        p.drawLine(x, plotRect.top(), x, plotRect.bottom());

        if (r.valid) {
            double t = r.tMin + frac * (r.tMax - r.tMin);
            p.setPen(kAxisText);
            p.drawText(QRect(x - 28, plotRect.bottom() + 4, 56, 16),
                       Qt::AlignCenter, QString::number(t, 'f', 2) + "s");
        }
    }

    // Y-axis unit, rotated up the left edge.
    if (r.valid) {
        p.save();
        p.setPen(kAxisText);
        p.translate(12, plotRect.center().y());
        p.rotate(-90);
        p.drawText(QRect(-40, -8, 80, 16), Qt::AlignCenter, m_unitLabel);
        p.restore();
    }
}

void StepResponsePlot::drawOverlays(QPainter& p, const QRect& plotRect, const Ranges& r) const
{
    auto toY = [&](double v) {
        double frac = (v - r.vMin) / (r.vMax - r.vMin);
        return plotRect.bottom() - frac * plotRect.height();
    };

    // Settling band around the achieved final value, matching how
    // computeStepMetrics() defines it — so a settling time read off the
    // metrics strip can be checked against the picture.
    double start = m_samples.first().measured;
    double amplitude = std::abs(m_setpoint - start);
    if (amplitude > 1e-6) {
        int tailCount = std::max(1, static_cast<int>(m_samples.size() * 0.20));
        double tailSum = 0.0;
        for (int i = m_samples.size() - tailCount; i < m_samples.size(); ++i) {
            tailSum += m_samples[i].measured;
        }
        double finalValue = tailSum / tailCount;
        double band = amplitude * (m_settlingBandPercent / 100.0);

        double yTop = toY(finalValue + band);
        double yBot = toY(finalValue - band);
        QRectF bandRect(plotRect.left(), yTop, plotRect.width(), yBot - yTop);
        p.fillRect(bandRect, QColor(kSetpointCol.red(), kSetpointCol.green(),
                                     kSetpointCol.blue(), 30));
    }

    // Setpoint reference line.
    double ySetpoint = toY(m_setpoint);
    if (ySetpoint >= plotRect.top() && ySetpoint <= plotRect.bottom()) {
        p.setPen(QPen(kSetpointCol, 1, Qt::DotLine));
        p.drawLine(QPointF(plotRect.left(), ySetpoint),
                   QPointF(plotRect.right(), ySetpoint));
    }
}

void StepResponsePlot::drawTraces(QPainter& p, const QRect& plotRect, const Ranges& r) const
{
    auto toX = [&](double t) {
        double frac = (t - r.tMin) / (r.tMax - r.tMin);
        return plotRect.left() + frac * plotRect.width();
    };
    auto toY = [&](double v) {
        double frac = (v - r.vMin) / (r.vMax - r.vMin);
        return plotRect.bottom() - frac * plotRect.height();
    };

    // Commanded setpoint. Held constant through the capture, but drawn as a
    // real trace rather than assumed flat so a future ramped setpoint shows.
    p.setPen(QPen(kSetpointCol, 1.5));
    for (int i = 1; i < m_samples.size(); ++i) {
        p.drawLine(QPointF(toX(m_samples[i - 1].t), toY(m_samples[i - 1].setpoint)),
                   QPointF(toX(m_samples[i].t),     toY(m_samples[i].setpoint)));
    }

    // Measured position. Segments touching a stale sample are dashed and
    // dimmed — those are carried-over values from a dropped encoder reply,
    // not real measurements, and silently interpolating them would hide a
    // serial problem behind a plausible-looking curve.
    const QPen solidPen(kMeasuredCol, 1.5);
    const QPen stalePen(kMeasuredCol.darker(200), 1.5, Qt::DashLine);
    for (int i = 1; i < m_samples.size(); ++i) {
        bool stale = m_samples[i - 1].stale || m_samples[i].stale;
        p.setPen(stale ? stalePen : solidPen);
        p.drawLine(QPointF(toX(m_samples[i - 1].t), toY(m_samples[i - 1].measured)),
                   QPointF(toX(m_samples[i].t),     toY(m_samples[i].measured)));
    }
}

void StepResponsePlot::drawPwmTrace(QPainter& p, const QRect& plotRect, const Ranges& r) const
{
    // PWM gets its own fixed -255..255 axis on the right. Fixed rather than
    // auto-ranged so saturation is visually obvious: a trace pinned to the
    // top or bottom edge is the motor at full effort.
    auto toX = [&](double t) {
        double frac = (t - r.tMin) / (r.tMax - r.tMin);
        return plotRect.left() + frac * plotRect.width();
    };
    auto pwmToY = [&](int pwm) {
        double frac = (static_cast<double>(pwm) + kMaxPwm) / (2.0 * kMaxPwm);
        return plotRect.bottom() - frac * plotRect.height();
    };

    p.setPen(QPen(kPwmCol, 1.0));
    for (int i = 1; i < m_samples.size(); ++i) {
        p.drawLine(QPointF(toX(m_samples[i - 1].t), pwmToY(m_samples[i - 1].pwm)),
                   QPointF(toX(m_samples[i].t),     pwmToY(m_samples[i].pwm)));
    }

    // Right-hand axis labels.
    QFont f("Consolas", 8, QFont::Bold);
    p.setFont(f);
    p.setPen(kPwmCol);
    for (int pwm : { kMaxPwm, 0, -kMaxPwm }) {
        int y = static_cast<int>(pwmToY(pwm));
        p.drawText(QRect(plotRect.right() + 4, y - 8, kPwmAxisWidth - 6, 16),
                   Qt::AlignLeft | Qt::AlignVCenter, QString::number(pwm));
    }
    p.drawText(QRect(plotRect.right() + 4, plotRect.top() - 2, kPwmAxisWidth - 6, 14),
               Qt::AlignLeft | Qt::AlignTop, "PWM");
}
