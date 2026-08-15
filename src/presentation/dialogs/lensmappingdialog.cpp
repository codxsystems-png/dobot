#include "presentation/dialogs/lensmappingdialog.h"
#include <QFormLayout>
#include <QDialogButtonBox>

LensMappingDialog::LensMappingDialog(const LensMapping& m, QWidget* parent)
    : QDialog(parent) { setupUI(m); }

void LensMappingDialog::setupUI(const LensMapping& m) {
    setWindowTitle("Lens Mapping Configuration");
    QFormLayout* form = new QFormLayout(this);
    m_lensName  = new QLineEdit(m.lensName); form->addRow("Lens Name:", m_lensName);
    m_focusNear = new QDoubleSpinBox(); m_focusNear->setRange(1,99999); m_focusNear->setSuffix(" mm"); m_focusNear->setValue(m.focusNearMm); form->addRow("Focus Near:", m_focusNear);
    m_focusFar  = new QDoubleSpinBox(); m_focusFar->setRange(1,99999); m_focusFar->setSuffix(" mm"); m_focusFar->setValue(m.focusFarMm);   form->addRow("Focus Far:", m_focusFar);
    m_zoomWide  = new QDoubleSpinBox(); m_zoomWide->setRange(1,9999); m_zoomWide->setSuffix(" mm"); m_zoomWide->setValue(m.zoomWideMm);   form->addRow("Zoom Wide:", m_zoomWide);
    m_zoomTele  = new QDoubleSpinBox(); m_zoomTele->setRange(1,9999); m_zoomTele->setSuffix(" mm"); m_zoomTele->setValue(m.zoomTeleMm);   form->addRow("Zoom Tele:", m_zoomTele);
    auto* btns = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    connect(btns, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
    form->addRow(btns);
}

LensMapping LensMappingDialog::result() const {
    LensMapping m;
    m.lensName = m_lensName->text(); m.focusNearMm = m_focusNear->value(); m.focusFarMm = m_focusFar->value();
    m.zoomWideMm = m_zoomWide->value(); m.zoomTeleMm = m_zoomTele->value();
    return m;
}
