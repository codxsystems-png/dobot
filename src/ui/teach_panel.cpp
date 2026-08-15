// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Teach / Jog Panel
// ═══════════════════════════════════════════════════════════════════════════════

#include "ui/teach_panel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QScrollArea>

#ifdef HAS_SERIALPORT
#include <QSerialPortInfo>
#endif

TeachPanel::TeachPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
    setRobotConnected(false);
    setGantryConnected(false);
}

void TeachPanel::setupUI()
{
    // Outer layout holds the scroll area
    QVBoxLayout* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    // Scrollable inner widget — prevents compression when panel height is small
    QScrollArea* scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    QWidget* innerWidget = new QWidget();
    QVBoxLayout* mainLayout = new QVBoxLayout(innerWidget);
    mainLayout->setSpacing(6);
    mainLayout->setContentsMargins(4, 4, 4, 4);

    createConnectionGroup();
    mainLayout->addWidget(m_connectionGroup);

    createJogGroup();
    m_jogGroup->setFixedHeight(270);          // 6 rows × 36px + 16px top + 8px bot + 5×4px spacing
    mainLayout->addWidget(m_jogGroup);

    createCartesianJogGroup();
    m_cartesianGroup->setFixedHeight(270);    // 6 rows × 36px + 16px top + 8px bot + 5×4px spacing
    mainLayout->addWidget(m_cartesianGroup);

    createGantryJogGroup();
    m_gantryGroup->setFixedHeight(236); // +36 for the Record Path row
    mainLayout->addWidget(m_gantryGroup);

    createSettingsGroup(mainLayout);  // pass layout so group lands inside scroll area

    mainLayout->addStretch();

    scrollArea->setWidget(innerWidget);
    outerLayout->addWidget(scrollArea);

    // Fixed width to match the parent QTabWidget (360px).
    // The scroll area handles vertical overflow — content never compresses.
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    setFixedWidth(360);
}

void TeachPanel::createConnectionGroup()
{
    m_connectionGroup = new QGroupBox("Robot Control");
    QGridLayout* grid = new QGridLayout(m_connectionGroup);
    grid->setSpacing(8);
    grid->setContentsMargins(8, 24, 8, 8);

    m_connectBtn = new QPushButton("Connect");
    m_connectBtn->setObjectName("connectBtn");
    m_connectBtn->setMinimumHeight(32);
    connect(m_connectBtn, &QPushButton::clicked, this, &TeachPanel::connectRequested);

    m_powerBtn = new QPushButton("Power On");
    m_powerBtn->setObjectName("powerBtn");
    m_powerBtn->setMinimumHeight(32);
    m_powerBtn->setEnabled(false);
    connect(m_powerBtn, &QPushButton::clicked, this, [this]() {
        emit powerRequested(true);
    });

    m_enableBtn = new QPushButton("Enable Robot");
    m_enableBtn->setObjectName("enableBtn");
    m_enableBtn->setMinimumHeight(32);
    m_enableBtn->setEnabled(false);
    connect(m_enableBtn, &QPushButton::clicked, this, [this]() {
        emit enableRequested(true);
    });

    m_dragBtn = new QPushButton("Drag Mode");
    m_dragBtn->setObjectName("dragBtn");
    m_dragBtn->setMinimumHeight(32);
    m_dragBtn->setCheckable(true);
    m_dragBtn->setEnabled(false);
    connect(m_dragBtn, &QPushButton::toggled, this, [this](bool checked) {
        m_isDragMode = checked;
        m_dragBtn->setText(checked ? "Exit Drag" : "Drag Mode");
        emit dragModeRequested(checked);
    });

    grid->addWidget(m_connectBtn, 0, 0);
    grid->addWidget(m_powerBtn,   0, 1);
    grid->addWidget(m_enableBtn,  1, 0);
    grid->addWidget(m_dragBtn,    1, 1);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
}

QPushButton* TeachPanel::createJogButton(const QString& text, const QString& axis)
{
    QPushButton* btn = new QPushButton(text);
    btn->setFixedSize(48, 36);
    btn->setFocusPolicy(Qt::NoFocus);

    // HOLD = jog, RELEASE = stop (MoveJog("") on release)
    connect(btn, &QPushButton::pressed, this, [this, axis]() {
        emit jogRequested(axis);
    });
    connect(btn, &QPushButton::released, this, [this]() {
        emit jogStopRequested();
    });

    return btn;
}

void TeachPanel::createJogGroup()
{
    m_jogGroup = new QGroupBox("Joint Jog");
    QGridLayout* grid = new QGridLayout(m_jogGroup);
    grid->setSpacing(4);
    grid->setContentsMargins(8, 16, 8, 8);

    // J1 through J6, each with +/- buttons
    const char* names[] = {"J1", "J2", "J3", "J4", "J5", "J6"};
    for (int i = 0; i < 6; ++i) {
        QLabel* label = new QLabel(names[i]);
        label->setAlignment(Qt::AlignCenter);
        label->setFixedWidth(36);

        QPushButton* minus = createJogButton("-", QString("%1-").arg(names[i]));
        QPushButton* plus  = createJogButton("+", QString("%1+").arg(names[i]));

        grid->addWidget(minus, i, 0);
        grid->addWidget(label, i, 1);
        grid->addWidget(plus,  i, 2);
    }
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 0);
    grid->setColumnStretch(2, 1);
}

void TeachPanel::createCartesianJogGroup()
{
    m_cartesianGroup = new QGroupBox("Cartesian Jog");
    QGridLayout* grid = new QGridLayout(m_cartesianGroup);
    grid->setSpacing(4);
    grid->setContentsMargins(8, 16, 8, 8);

    const char* axes[] = {"X", "Y", "Z", "RX", "RY", "RZ"};
    for (int i = 0; i < 6; ++i) {
        QLabel* label = new QLabel(axes[i]);
        label->setAlignment(Qt::AlignCenter);
        label->setFixedWidth(36);

        QPushButton* minus = createJogButton("-", QString("%1-").arg(axes[i]));
        QPushButton* plus  = createJogButton("+", QString("%1+").arg(axes[i]));

        grid->addWidget(minus, i, 0);
        grid->addWidget(label, i, 1);
        grid->addWidget(plus,  i, 2);
    }
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 0);
    grid->setColumnStretch(2, 1);
}

void TeachPanel::createGantryJogGroup()
{
    m_gantryGroup = new QGroupBox("Linear Gantry");
    QGridLayout* grid = new QGridLayout(m_gantryGroup);
    grid->setSpacing(4);
    grid->setContentsMargins(8, 16, 8, 8);

    // Row 0: COM port selector + Connect button
    m_gantryPortCombo = new QComboBox();
    m_gantryPortCombo->setMinimumHeight(28);
    m_gantryPortCombo->setEditable(true);
    // Populate with available serial ports
    {
#ifdef HAS_SERIALPORT
        const auto ports = QSerialPortInfo::availablePorts();
        for (const auto& info : ports)
            m_gantryPortCombo->addItem(info.portName() + " - " + info.description(), info.portName());
#endif
        // Always allow manual entry
        if (m_gantryPortCombo->count() == 0) {
            m_gantryPortCombo->addItem("COM6", "COM6");
            m_gantryPortCombo->addItem("COM3", "COM3");
            m_gantryPortCombo->addItem("COM4", "COM4");
        }
    }

    m_gantryConnectBtn = new QPushButton("Connect");
    m_gantryConnectBtn->setMinimumHeight(28);
    m_gantryConnectBtn->setStyleSheet(
        "QPushButton { background: #335533; color: #88ff88; border: 1px solid #55aa55; border-radius: 4px; padding: 4px 12px; }"
        "QPushButton:hover { background: #446644; }"
    );
    connect(m_gantryConnectBtn, &QPushButton::clicked, this, [this]() {
        if (m_gantryConnected) {
            emit gantryDisconnectRequested();
        } else {
            QString port = m_gantryPortCombo->currentData().toString();
            if (port.isEmpty())
                port = m_gantryPortCombo->currentText().split(" ").first();
            emit gantryConnectRequested(port);
        }
    });

    grid->addWidget(m_gantryPortCombo, 0, 0, 1, 2);
    grid->addWidget(m_gantryConnectBtn, 0, 2);

    // Row 1: Home Button and Position Label
    m_gantryHomeBtn = new QPushButton("Home Gantry");
    m_gantryHomeBtn->setMinimumHeight(32);
    connect(m_gantryHomeBtn, &QPushButton::clicked, this, [this]() {
        emit gantryHomeRequested();
    });
    
    m_gantryPosLabel = new QLabel("[ 0.0 mm ]");
    m_gantryPosLabel->setAlignment(Qt::AlignCenter);
    m_gantryPosLabel->setStyleSheet("font-weight: bold; color: #55aaff; background: #222; border-radius: 4px;");
    m_gantryPosLabel->setMinimumHeight(32);

    grid->addWidget(m_gantryHomeBtn, 1, 0);
    grid->addWidget(m_gantryPosLabel, 1, 1, 1, 2);

    // Row 2: Jog controls
    QLabel* label = new QLabel("GANTRY");
    label->setAlignment(Qt::AlignCenter);
    label->setFixedWidth(56);

    m_gantryJogNeg = new QPushButton("<");
    m_gantryJogNeg->setFixedSize(48, 36);
    m_gantryJogNeg->setFocusPolicy(Qt::NoFocus);
    connect(m_gantryJogNeg, &QPushButton::pressed, this, [this]() { emit gantryJogRequested(-m_gantryPwm); });
    connect(m_gantryJogNeg, &QPushButton::released, this, &TeachPanel::gantryJogStopRequested);

    m_gantryJogPos = new QPushButton(">");
    m_gantryJogPos->setFixedSize(48, 36);
    m_gantryJogPos->setFocusPolicy(Qt::NoFocus);
    connect(m_gantryJogPos, &QPushButton::pressed, this, [this]() { emit gantryJogRequested(m_gantryPwm); });
    connect(m_gantryJogPos, &QPushButton::released, this, &TeachPanel::gantryJogStopRequested);

    grid->addWidget(m_gantryJogNeg, 2, 0, Qt::AlignRight);
    grid->addWidget(label, 2, 1);
    grid->addWidget(m_gantryJogPos, 2, 2, Qt::AlignLeft);

    // Row 3: Speed selector
    QLabel* speedLabel = new QLabel("Speed:");
    speedLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_gantrySpeedCombo = new QComboBox();
    m_gantrySpeedCombo->setMinimumHeight(26);
    m_gantrySpeedCombo->addItem("Slow (25%)",   64);
    m_gantrySpeedCombo->addItem("Medium (50%)", 128);
    m_gantrySpeedCombo->addItem("Fast (75%)",   192);
    m_gantrySpeedCombo->addItem("Full (100%)",  255);
    m_gantrySpeedCombo->setCurrentIndex(2); // default: Fast
    m_gantryPwm = 192;
    connect(m_gantrySpeedCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        m_gantryPwm = m_gantrySpeedCombo->itemData(idx).toInt();
    });

    grid->addWidget(speedLabel, 3, 0);
    grid->addWidget(m_gantrySpeedCombo, 3, 1, 1, 2);

    // Row 4: Record Path — captures continuous hand-jogged motion instead
    // of only discrete taught points (see PathRecorderService).
    m_gantryRecordBtn = new QPushButton("Record Path");
    m_gantryRecordBtn->setMinimumHeight(28);
    m_gantryRecordBtn->setCheckable(true);
    m_gantryRecordBtn->setStyleSheet(
        "QPushButton { background: #333; color: #ccc; border: 1px solid #555; border-radius: 4px; }"
        "QPushButton:checked { background: #663333; color: #ff8888; border: 1px solid #aa5555; }"
    );
    connect(m_gantryRecordBtn, &QPushButton::toggled, this, [this](bool checked) {
        m_gantryRecordBtn->setText(checked ? "Stop Recording" : "Record Path");
        emit gantryRecordToggled(checked);
    });
    grid->addWidget(m_gantryRecordBtn, 4, 0, 1, 3);

    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 0);
    grid->setColumnStretch(2, 1);
}

void TeachPanel::createSettingsGroup(QVBoxLayout* targetLayout)
{
    QGroupBox* group = new QGroupBox("Settings");
    QGridLayout* grid = new QGridLayout(group);
    grid->setSpacing(6);
    grid->setContentsMargins(8, 16, 8, 8);

    // Step size
    QLabel* stepLabel = new QLabel("Step:");
    stepLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    grid->addWidget(stepLabel, 0, 0);
    m_stepSizeCombo = new QComboBox();
    m_stepSizeCombo->setMinimumHeight(26);
    m_stepSizeCombo->addItems({"0.1°", "0.5°", "1°", "5°", "10°"});
    m_stepSizeCombo->setCurrentIndex(2);
    grid->addWidget(m_stepSizeCombo, 0, 1);

    // Speed factor
    QLabel* speedLabel = new QLabel("Speed:");
    speedLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    grid->addWidget(speedLabel, 1, 0);
    m_speedCombo = new QComboBox();
    m_speedCombo->setMinimumHeight(26);
    m_speedCombo->addItems({"10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%"});
    m_speedCombo->setCurrentIndex(7); // 80% default per design spec
    connect(m_speedCombo, &QComboBox::currentIndexChanged, this, [this](int idx) {
        emit speedChanged((idx + 1) * 10);
    });
    grid->addWidget(m_speedCombo, 1, 1);
    grid->setColumnStretch(0, 0);
    grid->setColumnStretch(1, 1);

    targetLayout->addWidget(group);  // correctly adds to scroll-area inner layout
}

void TeachPanel::setRobotConnected(bool connected)
{
    m_powerBtn->setEnabled(connected);
    m_enableBtn->setEnabled(connected);
    m_dragBtn->setEnabled(connected);
    m_jogGroup->setEnabled(connected);
    m_cartesianGroup->setEnabled(connected);
}

void TeachPanel::setGantryConnected(bool connected)
{
    m_gantryConnected = connected;
    if (m_gantryConnectBtn) {
        if (connected) {
            m_gantryConnectBtn->setText("Disconnect");
            m_gantryConnectBtn->setStyleSheet(
                "QPushButton { background: #553333; color: #ff8888; border: 1px solid #aa5555; border-radius: 4px; padding: 4px 12px; }"
                "QPushButton:hover { background: #664444; }"
            );
        } else {
            m_gantryConnectBtn->setText("Connect");
            m_gantryConnectBtn->setStyleSheet(
                "QPushButton { background: #335533; color: #88ff88; border: 1px solid #55aa55; border-radius: 4px; padding: 4px 12px; }"
                "QPushButton:hover { background: #446644; }"
            );
        }
    }
    if (m_gantryHomeBtn)  m_gantryHomeBtn->setEnabled(connected);
    if (m_gantryJogNeg)   m_gantryJogNeg->setEnabled(connected);
    if (m_gantryJogPos)   m_gantryJogPos->setEnabled(connected);
    if (m_gantryPortCombo) m_gantryPortCombo->setEnabled(!connected);
}

void TeachPanel::updateGantryPosition(double posMm)
{
    if (m_gantryPosLabel) {
        m_gantryPosLabel->setText(QString("[ %1 mm ]").arg(posMm, 0, 'f', 1));
    }
}
