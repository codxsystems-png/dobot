#include "presentation/dialogs/fizkeyframedialog.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>

FizKeyframeDialog::FizKeyframeDialog(const FizKeyframe& kf, QWidget* parent)
    : QDialog(parent), m_id(kf.id) { setupUI(kf); }

void FizKeyframeDialog::setupUI(const FizKeyframe& kf) {
    setWindowTitle("Edit FIZ Keyframe");
    QFormLayout* form = new QFormLayout(this);
    m_timeSpin  = new QDoubleSpinBox(); m_timeSpin->setRange(0,9999); m_timeSpin->setDecimals(2); m_timeSpin->setSuffix(" s"); m_timeSpin->setValue(kf.time);
    m_focusSpin = new QDoubleSpinBox(); m_focusSpin->setRange(0,100); m_focusSpin->setDecimals(1); m_focusSpin->setSuffix("%"); m_focusSpin->setValue(kf.state.focus);
    m_irisSpin  = new QDoubleSpinBox(); m_irisSpin->setRange(0,100); m_irisSpin->setDecimals(1); m_irisSpin->setSuffix("%"); m_irisSpin->setValue(kf.state.iris);
    m_zoomSpin  = new QDoubleSpinBox(); m_zoomSpin->setRange(0,100); m_zoomSpin->setDecimals(1); m_zoomSpin->setSuffix("%"); m_zoomSpin->setValue(kf.state.zoom);
    m_easingCombo = new QComboBox(); m_easingCombo->addItems({"Linear","EaseIn","EaseOut","EaseInOut"}); m_easingCombo->setCurrentIndex(static_cast<int>(kf.easing));
    form->addRow("Time:", m_timeSpin); form->addRow("Focus:", m_focusSpin); form->addRow("Iris:", m_irisSpin); form->addRow("Zoom:", m_zoomSpin); form->addRow("Easing:", m_easingCombo);
    auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(btns, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
    form->addRow(btns);
}

FizKeyframe FizKeyframeDialog::result() const {
    FizKeyframe kf;
    kf.id = m_id; kf.time = m_timeSpin->value();
    kf.state.focus = m_focusSpin->value(); kf.state.iris = m_irisSpin->value(); kf.state.zoom = m_zoomSpin->value();
    kf.easing = static_cast<FizKeyframe::Easing>(m_easingCombo->currentIndex());
    return kf;
}
