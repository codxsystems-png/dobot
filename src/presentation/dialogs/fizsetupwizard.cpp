#include "presentation/dialogs/fizsetupwizard.h"
#include "infrastructure/fiz/nucleusservice.h"
#include <QVBoxLayout>
#include <QPushButton>

FizSetupWizard::FizSetupWizard(NucleusService* nucleus, QWidget* parent)
    : QWizard(parent), m_nucleus(nucleus) {
    setWindowTitle("FIZ Motor Setup Wizard");
    setWizardStyle(QWizard::ModernStyle);
    addPage(createVoltagePage());
    addPage(createMotorPage());
    addPage(createWiringPage());
    addPage(createTestPage());
    addPage(createCalibratePage());
    addPage(createDonePage());
}

QWizardPage* FizSetupWizard::createVoltagePage() {
    auto* page = new QWizardPage();
    page->setTitle("Step 1: Voltage Confirmation");
    auto* lay = new QVBoxLayout(page);
    lay->addWidget(new QLabel("CRITICAL: The Waveshare USB-to-TTL-D adapter\n"
                               "MUST have its voltage jumper set to 3.3V.\n\n"
                               "5V will PERMANENTLY DESTROY the Nucleus-M motors!"));
    auto* chk = new QCheckBox("I confirm the adapter is set to 3.3V");
    lay->addWidget(chk);
    // Connect to wizard's isComplete mechanism
    connect(chk, &QCheckBox::toggled, page, &QWizardPage::completeChanged);
    page->setFinalPage(false);
    return page;
}

QWizardPage* FizSetupWizard::createMotorPage() {
    auto* page = new QWizardPage();
    page->setTitle("Step 2: Motor Numbers");
    auto* lay = new QVBoxLayout(page);
    lay->addWidget(new QLabel("Set Motor Number on each physical motor body:\n\n"
                               "• Motor 1 (Purple LED) = Focus\n"
                               "• Motor 2 (Green LED)  = Iris\n"
                               "• Motor 3 (Blue LED)   = Zoom\n\n"
                               "Hold SET button on motor → press +/- to change number."));
    return page;
}

QWizardPage* FizSetupWizard::createWiringPage() {
    auto* page = new QWizardPage();
    page->setTitle("Step 3: Wiring");
    auto* lay = new QVBoxLayout(page);
    lay->addWidget(new QLabel("Daisy-chain connection:\n\n"
                               "PC USB → TTL Adapter → LEMO → Motor 1 Port A\n"
                               "Motor 1 Port B → Motor 2 Port A\n"
                               "Motor 2 Port B → Motor 3 Port A\n\n"
                               "D-Tap battery → Motor 1 (powers all 3)"));
    return page;
}

QWizardPage* FizSetupWizard::createTestPage() {
    auto* page = new QWizardPage();
    page->setTitle("Step 4: Connection Test");
    auto* lay = new QVBoxLayout(page);
    auto* combo = new QComboBox();
    if (m_nucleus) combo->addItems(m_nucleus->availablePorts());
    lay->addWidget(new QLabel("Select serial port:"));
    lay->addWidget(combo);
    auto* testBtn = new QPushButton("Test Motors");
    lay->addWidget(testBtn);
    auto* result = new QLabel("Press Test to verify all 3 motors respond.");
    lay->addWidget(result);
    connect(testBtn, &QPushButton::clicked, [=]() {
        if (m_nucleus && !combo->currentText().isEmpty()) {
            m_nucleus->connectPort(combo->currentText());
            result->setText("Testing... check if motors move slightly.");
        }
    });
    return page;
}

QWizardPage* FizSetupWizard::createCalibratePage() {
    auto* page = new QWizardPage();
    page->setTitle("Step 5: Calibration");
    auto* lay = new QVBoxLayout(page);
    lay->addWidget(new QLabel("Calibrate all motors to their reference position."));
    auto* calBtn = new QPushButton("Calibrate All");
    lay->addWidget(calBtn);
    auto* status = new QLabel("Ready.");
    lay->addWidget(status);
    connect(calBtn, &QPushButton::clicked, [=]() {
        if (m_nucleus) { m_nucleus->calibrateAll(); status->setText("Calibration sent."); }
    });
    return page;
}

QWizardPage* FizSetupWizard::createDonePage() {
    auto* page = new QWizardPage();
    page->setTitle("Step 6: Complete");
    auto* lay = new QVBoxLayout(page);
    lay->addWidget(new QLabel("✅ FIZ motor setup complete!\n\n"
                               "You can now use the FIZ sliders in the Teach Panel\n"
                               "and add FIZ keyframes to the timeline."));
    return page;
}
