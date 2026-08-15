// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — FIZ Panel
// ═══════════════════════════════════════════════════════════════════════════════

#include "presentation/widgets/fizpanel.h"
#include "application/fizservice.h"
#include "infrastructure/fiz/nucleusservice.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QFrame>

FizPanel::FizPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
}

void FizPanel::setFizService(FizService* fiz)
{
    m_fiz = fiz;
}

void FizPanel::setNucleusService(NucleusService* nucleus)
{
    m_nucleus = nucleus;
}

void FizPanel::setupUI()
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
    layout->setSpacing(6);

    QLabel* title = new QLabel("FIZ LENS CONTROL");
    title->setStyleSheet("font-weight: bold; font-size: 13px; color: #e0e0e0;");
    layout->addWidget(title);

    // ─── Connection ──────────────────────────────────────────────────────
    QHBoxLayout* connRow = new QHBoxLayout();
    connRow->addWidget(new QLabel("Port:"));
    m_portCombo = new QComboBox();
    m_portCombo->setMinimumWidth(80);
    connRow->addWidget(m_portCombo);

    QPushButton* refreshBtn = new QPushButton("Refresh");
    connect(refreshBtn, &QPushButton::clicked, this, &FizPanel::refreshPorts);
    connRow->addWidget(refreshBtn);

    m_connectBtn = new QPushButton("Connect");
    connect(m_connectBtn, &QPushButton::clicked, this, [this]() {
        if (m_nucleus && m_nucleus->isConnected())
            emit disconnectRequested();
        else
            emit connectRequested(m_portCombo->currentText());
    });
    connRow->addWidget(m_connectBtn);

    m_statusLabel = new QLabel("Status");
    m_statusLabel->setStyleSheet("color: #555555; font-size: 16px;");
    connRow->addWidget(m_statusLabel);
    connRow->addStretch();
    layout->addLayout(connRow);

    // ─── Voltage Safety ──────────────────────────────────────────────────
    m_voltageCheck = new QCheckBox("I confirm adapter is set to 3.3V");
    m_voltageCheck->setStyleSheet("color: #ffaa00;");
    layout->addWidget(m_voltageCheck);

    // ─── Sliders ─────────────────────────────────────────────────────────
    auto makeSliderRow = [&](const QString& label, const QString& color,
                              QSlider*& slider, QLabel*& valueLbl, QLabel*& mmLbl) {
        QGroupBox* group = new QGroupBox(label);
        group->setStyleSheet(QString("QGroupBox { color: %1; }").arg(color));
        QVBoxLayout* gl = new QVBoxLayout(group);
        gl->setSpacing(2);

        QHBoxLayout* valRow = new QHBoxLayout();
        valueLbl = new QLabel("0.0%");
        valueLbl->setStyleSheet("font-family: Consolas; font-size: 12px; color: #ffffff;");
        valRow->addWidget(valueLbl);
        mmLbl = new QLabel("");
        mmLbl->setStyleSheet("font-family: Consolas; font-size: 10px; color: #aaaaaa;");
        valRow->addWidget(mmLbl);
        valRow->addStretch();
        gl->addLayout(valRow);

        slider = new QSlider(Qt::Horizontal);
        slider->setRange(0, 1000);  // 0.0–100.0 with 0.1% precision
        slider->setValue(0);
        gl->addWidget(slider);
        group->setMinimumHeight(70);  // value row + slider + spacing
        layout->addWidget(group);
    };

    makeSliderRow("FOCUS", "#9944ff", m_focusSlider, m_focusValue, m_focusMm);
    makeSliderRow("IRIS",  "#44aa44", m_irisSlider,  m_irisValue,  m_zoomMm); // temp
    makeSliderRow("ZOOM",  "#4488ff", m_zoomSlider,  m_zoomValue,  m_zoomMm);

    // Slider → FizService (live, per spec §6.4)
    connect(m_focusSlider, &QSlider::valueChanged, this, [this](int v) {
        if (m_updatingSliders) return;
        float pct = v / 10.0f;
        m_focusValue->setText(QString("%1%").arg(pct, 0, 'f', 1));
        if (m_fiz) m_fiz->setFocus(pct);
    });
    connect(m_irisSlider, &QSlider::valueChanged, this, [this](int v) {
        if (m_updatingSliders) return;
        float pct = v / 10.0f;
        m_irisValue->setText(QString("%1%").arg(pct, 0, 'f', 1));
        if (m_fiz) m_fiz->setIris(pct);
    });
    connect(m_zoomSlider, &QSlider::valueChanged, this, [this](int v) {
        if (m_updatingSliders) return;
        float pct = v / 10.0f;
        m_zoomValue->setText(QString("%1%").arg(pct, 0, 'f', 1));
        if (m_fiz) m_fiz->setZoom(pct);
    });

    // ─── Buttons ─────────────────────────────────────────────────────────
    QGridLayout* calGrid = new QGridLayout();
    auto addCalBtn = [&](const QString& text, uint8_t motorId, int row, int col) {
        QPushButton* btn = new QPushButton(text);
        connect(btn, &QPushButton::clicked, this, [this, motorId]() {
            emit calibrateRequested(motorId);
        });
        calGrid->addWidget(btn, row, col);
    };
    addCalBtn("Cal Focus", 0x01, 0, 0);
    addCalBtn("Cal Iris",  0x02, 0, 1);
    addCalBtn("Cal Zoom",  0x03, 1, 0);
    QPushButton* calAllBtn = new QPushButton("Cal All");
    connect(calAllBtn, &QPushButton::clicked, this, [this]() {
        emit calibrateRequested(0xFF); // sentinel = all
    });
    calGrid->addWidget(calAllBtn, 1, 1);
    layout->addLayout(calGrid);

    QHBoxLayout* miscRow = new QHBoxLayout();
    QPushButton* lensBtn = new QPushButton("Lens Mapping…");
    connect(lensBtn, &QPushButton::clicked, this, &FizPanel::lensMappingRequested);
    miscRow->addWidget(lensBtn);

    QPushButton* recFizBtn = new QPushButton("Record FIZ with Point");
    recFizBtn->setStyleSheet("color: #ff4444;");
    connect(recFizBtn, &QPushButton::clicked, this, &FizPanel::recordFizRequested);
    miscRow->addWidget(recFizBtn);
    layout->addLayout(miscRow);

    layout->addStretch();

    scrollArea->setWidget(innerWidget);
    outerLayout->addWidget(scrollArea);

    // Fixed width to match the parent QTabWidget (360px).
    // The scroll area handles vertical overflow.
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    setFixedWidth(360);
}

void FizPanel::onFizStateChanged(const FizState& state)
{
    updateDisplay(state);
}

void FizPanel::onConnectionChanged(bool connected)
{
    m_connectBtn->setText(connected ? "Disconnect" : "Connect");
    m_statusLabel->setStyleSheet(connected
        ? "color: #00cc44; font-size: 16px;"
        : "color: #555555; font-size: 16px;");
}

void FizPanel::refreshPorts()
{
    m_portCombo->clear();
    if (m_nucleus) {
        m_portCombo->addItems(m_nucleus->availablePorts());
    }
}

void FizPanel::updateDisplay(const FizState& state)
{
    m_updatingSliders = true;
    m_focusSlider->setValue(static_cast<int>(state.focus * 10));
    m_irisSlider->setValue(static_cast<int>(state.iris * 10));
    m_zoomSlider->setValue(static_cast<int>(state.zoom * 10));

    m_focusValue->setText(QString("%1%").arg(state.focus, 0, 'f', 1));
    m_irisValue->setText(QString("%1%").arg(state.iris, 0, 'f', 1));
    m_zoomValue->setText(QString("%1%").arg(state.zoom, 0, 'f', 1));

    // Lens mapping display
    if (m_nucleus) {
        m_focusMm->setText(QString("%1mm").arg(m_nucleus->focusPercentToMm(state.focus), 0, 'f', 0));
        m_zoomMm->setText(QString("%1mm").arg(m_nucleus->zoomPercentToMm(state.zoom), 0, 'f', 0));
    }
    m_updatingSliders = false;
}
