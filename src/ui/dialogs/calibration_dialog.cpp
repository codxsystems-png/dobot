// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Calibration Dialog
// ═══════════════════════════════════════════════════════════════════════════════

#include "ui/dialogs/calibration_dialog.h"
#include "services/connection_service.h"
#include "core/command_builder.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QMessageBox>

CalibrationDialog::CalibrationDialog(ConnectionService* connService, QWidget* parent)
    : QDialog(parent)
    , m_connService(connService)
{
    setWindowTitle("Calibration — Tool & User Coordinates");
    setMinimumWidth(460);
    setupUI();
}

void CalibrationDialog::setupUI()
{
    QVBoxLayout* layout = new QVBoxLayout(this);

    // ─── Tool Coordinate ─────────────────────────────────────────────────
    QGroupBox* toolGroup = new QGroupBox("Tool Coordinate (SetTool)");
    QFormLayout* toolForm = new QFormLayout(toolGroup);

    m_toolIndex = new QComboBox();
    for (int i = 0; i <= 9; ++i) m_toolIndex->addItem(QString("Tool %1").arg(i));
    toolForm->addRow("Index:", m_toolIndex);

    auto makeSpin = [](double min, double max, QString suffix) {
        auto* s = new QDoubleSpinBox();
        s->setRange(min, max); s->setDecimals(3);
        s->setSuffix(suffix); s->setValue(0.0);
        return s;
    };

    m_toolX  = makeSpin(-2000, 2000, " mm"); toolForm->addRow("X:", m_toolX);
    m_toolY  = makeSpin(-2000, 2000, " mm"); toolForm->addRow("Y:", m_toolY);
    m_toolZ  = makeSpin(-2000, 2000, " mm"); toolForm->addRow("Z:", m_toolZ);
    m_toolRX = makeSpin(-360, 360, "°");     toolForm->addRow("RX:", m_toolRX);
    m_toolRY = makeSpin(-360, 360, "°");     toolForm->addRow("RY:", m_toolRY);
    m_toolRZ = makeSpin(-360, 360, "°");     toolForm->addRow("RZ:", m_toolRZ);

    QHBoxLayout* toolBtns = new QHBoxLayout();
    QPushButton* applyToolBtn = new QPushButton("Apply SetTool");
    QPushButton* resetToolBtn = new QPushButton("Reset to Zero");
    connect(applyToolBtn, &QPushButton::clicked, this, &CalibrationDialog::applyTool);
    connect(resetToolBtn, &QPushButton::clicked, this, &CalibrationDialog::resetTool);
    toolBtns->addWidget(applyToolBtn);
    toolBtns->addWidget(resetToolBtn);
    toolForm->addRow(toolBtns);
    layout->addWidget(toolGroup);

    // ─── User Coordinate ─────────────────────────────────────────────────
    QGroupBox* userGroup = new QGroupBox("User Coordinate (SetUser)");
    QFormLayout* userForm = new QFormLayout(userGroup);

    m_userIndex = new QComboBox();
    for (int i = 0; i <= 9; ++i) m_userIndex->addItem(QString("User %1").arg(i));
    userForm->addRow("Index:", m_userIndex);

    m_userX  = makeSpin(-2000, 2000, " mm"); userForm->addRow("X:", m_userX);
    m_userY  = makeSpin(-2000, 2000, " mm"); userForm->addRow("Y:", m_userY);
    m_userZ  = makeSpin(-2000, 2000, " mm"); userForm->addRow("Z:", m_userZ);
    m_userRX = makeSpin(-360, 360, "°");     userForm->addRow("RX:", m_userRX);
    m_userRY = makeSpin(-360, 360, "°");     userForm->addRow("RY:", m_userRY);
    m_userRZ = makeSpin(-360, 360, "°");     userForm->addRow("RZ:", m_userRZ);

    QHBoxLayout* userBtns = new QHBoxLayout();
    QPushButton* applyUserBtn = new QPushButton("Apply SetUser");
    QPushButton* resetUserBtn = new QPushButton("Reset to Zero");
    connect(applyUserBtn, &QPushButton::clicked, this, &CalibrationDialog::applyUser);
    connect(resetUserBtn, &QPushButton::clicked, this, &CalibrationDialog::resetUser);
    userBtns->addWidget(applyUserBtn);
    userBtns->addWidget(resetUserBtn);
    userForm->addRow(userBtns);
    layout->addWidget(userGroup);

    // ─── Status + Close ──────────────────────────────────────────────────
    m_statusLabel = new QLabel("Ready");
    m_statusLabel->setStyleSheet("color: #aaaaaa;");
    layout->addWidget(m_statusLabel);

    QPushButton* closeBtn = new QPushButton("Close");
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    layout->addWidget(closeBtn);
}

void CalibrationDialog::applyTool()
{
    if (!m_connService || !m_connService->isConnected()) {
        m_statusLabel->setText("Not connected");
        m_statusLabel->setStyleSheet("color: #ff4444;");
        return;
    }

    QString params = QString("%1,%2,%3,%4,%5,%6")
        .arg(m_toolX->value(), 0, 'f', 3)
        .arg(m_toolY->value(), 0, 'f', 3)
        .arg(m_toolZ->value(), 0, 'f', 3)
        .arg(m_toolRX->value(), 0, 'f', 3)
        .arg(m_toolRY->value(), 0, 'f', 3)
        .arg(m_toolRZ->value(), 0, 'f', 3);
    QString cmd = CommandBuilder::setTool(m_toolIndex->currentIndex(), params);

    int id = m_connService->enqueueMotionCommand(cmd);
    if (id >= 0) {
        m_statusLabel->setText(QString("Tool applied (id=%1): %2").arg(id).arg(cmd));
        m_statusLabel->setStyleSheet("color: #00cc44;");
    } else {
        m_statusLabel->setText("Failed to send: " + cmd);
        m_statusLabel->setStyleSheet("color: #ff4444;");
    }
}

void CalibrationDialog::applyUser()
{
    if (!m_connService || !m_connService->isConnected()) {
        m_statusLabel->setText("Not connected");
        m_statusLabel->setStyleSheet("color: #ff4444;");
        return;
    }

    QString params = QString("%1,%2,%3,%4,%5,%6")
        .arg(m_userX->value(), 0, 'f', 3)
        .arg(m_userY->value(), 0, 'f', 3)
        .arg(m_userZ->value(), 0, 'f', 3)
        .arg(m_userRX->value(), 0, 'f', 3)
        .arg(m_userRY->value(), 0, 'f', 3)
        .arg(m_userRZ->value(), 0, 'f', 3);
    QString cmd = CommandBuilder::setUser(m_userIndex->currentIndex(), params);

    int id = m_connService->enqueueMotionCommand(cmd);
    if (id >= 0) {
        m_statusLabel->setText(QString("User applied (id=%1): %2").arg(id).arg(cmd));
        m_statusLabel->setStyleSheet("color: #00cc44;");
    } else {
        m_statusLabel->setText("Failed to send: " + cmd);
        m_statusLabel->setStyleSheet("color: #ff4444;");
    }
}

void CalibrationDialog::resetTool()
{
    m_toolX->setValue(0); m_toolY->setValue(0); m_toolZ->setValue(0);
    m_toolRX->setValue(0); m_toolRY->setValue(0); m_toolRZ->setValue(0);
}

void CalibrationDialog::resetUser()
{
    m_userX->setValue(0); m_userY->setValue(0); m_userZ->setValue(0);
    m_userRX->setValue(0); m_userRY->setValue(0); m_userRZ->setValue(0);
}
