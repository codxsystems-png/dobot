#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — FIZ Panel (Phase 7)
// Manual slider control for Focus/Iris/Zoom during teaching.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QWidget>
#include <QSlider>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QCheckBox>
#include "core/types.h"

class FizService;
class NucleusService;

class FizPanel : public QWidget
{
    Q_OBJECT
public:
    explicit FizPanel(QWidget* parent = nullptr);

    void setFizService(FizService* fiz);
    void setNucleusService(NucleusService* nucleus);

signals:
    void connectRequested(const QString& portName);
    void disconnectRequested();
    void recordFizRequested();
    void calibrateRequested(uint8_t motorId);
    void lensMappingRequested();

public slots:
    void onFizStateChanged(const FizState& state);
    void onConnectionChanged(bool connected);
    void refreshPorts();

private:
    void setupUI();
    void updateDisplay(const FizState& state);

    FizService*     m_fiz     = nullptr;
    NucleusService* m_nucleus = nullptr;

    QComboBox*    m_portCombo     = nullptr;
    QPushButton*  m_connectBtn    = nullptr;
    QLabel*       m_statusLabel   = nullptr;
    QCheckBox*    m_voltageCheck  = nullptr;

    QSlider*      m_focusSlider   = nullptr;
    QSlider*      m_irisSlider    = nullptr;
    QSlider*      m_zoomSlider    = nullptr;

    QLabel*       m_focusValue    = nullptr;
    QLabel*       m_irisValue     = nullptr;
    QLabel*       m_zoomValue     = nullptr;

    QLabel*       m_focusMm       = nullptr;
    QLabel*       m_zoomMm        = nullptr;

    bool          m_updatingSliders = false;
};
