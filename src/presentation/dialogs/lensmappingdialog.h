#pragma once
#include <QDialog>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include "core/types.h"

class LensMappingDialog : public QDialog
{
    Q_OBJECT
public:
    explicit LensMappingDialog(const LensMapping& mapping, QWidget* parent = nullptr);
    LensMapping result() const;
private:
    void setupUI(const LensMapping& m);
    QLineEdit*      m_lensName  = nullptr;
    QDoubleSpinBox* m_focusNear = nullptr;
    QDoubleSpinBox* m_focusFar  = nullptr;
    QDoubleSpinBox* m_zoomWide  = nullptr;
    QDoubleSpinBox* m_zoomTele  = nullptr;
};
