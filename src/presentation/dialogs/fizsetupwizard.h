#pragma once
#include <QWizard>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
class NucleusService;

class FizSetupWizard : public QWizard
{
    Q_OBJECT
public:
    explicit FizSetupWizard(NucleusService* nucleus, QWidget* parent = nullptr);
private:
    QWizardPage* createVoltagePage();
    QWizardPage* createMotorPage();
    QWizardPage* createWiringPage();
    QWizardPage* createTestPage();
    QWizardPage* createCalibratePage();
    QWizardPage* createDonePage();
    NucleusService* m_nucleus = nullptr;
};
