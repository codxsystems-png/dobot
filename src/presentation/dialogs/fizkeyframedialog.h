#pragma once
#include <QDialog>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QLabel>
#include "core/types.h"

class FizKeyframeDialog : public QDialog
{
    Q_OBJECT
public:
    explicit FizKeyframeDialog(const FizKeyframe& kf, QWidget* parent = nullptr);
    FizKeyframe result() const;

private:
    void setupUI(const FizKeyframe& kf);
    QDoubleSpinBox* m_timeSpin  = nullptr;
    QDoubleSpinBox* m_focusSpin = nullptr;
    QDoubleSpinBox* m_irisSpin  = nullptr;
    QDoubleSpinBox* m_zoomSpin  = nullptr;
    QComboBox*      m_easingCombo = nullptr;
    QString         m_id;
};
