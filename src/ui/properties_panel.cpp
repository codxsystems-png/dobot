// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Properties Panel
// ═══════════════════════════════════════════════════════════════════════════════

#include "ui/properties_panel.h"
#include "ui/timeline/timeline_undo_commands.h"
#include "models/segments_model.h"
#include "models/points_model.h"
#include "core/motion_estimator.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QScrollArea>
#include <QFrame>

PropertiesPanel::PropertiesPanel(SegmentsModel* model, QWidget* parent)
    : QWidget(parent)
    , m_model(model)
{
    setupUI();
    clearSelection();
}

void PropertiesPanel::setupUI()
{
    // Outer layout holds the scroll area
    QVBoxLayout* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    // Scrollable inner widget — prevents controls from being crushed
    QScrollArea* scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    QWidget* innerWidget = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(innerWidget);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(6);

    // Title
    QLabel* title = new QLabel("Segment Properties");
    title->setStyleSheet("font-weight: bold; font-size: 13px; color: #e0e0e0;");
    layout->addWidget(title);

    m_nameLabel = new QLabel("(No selection)");
    m_nameLabel->setStyleSheet("color: #0078d4; font-size: 12px;");
    layout->addWidget(m_nameLabel);

    // ── Motion group ─────────────────────────────────────────────────────
    QGroupBox* group = new QGroupBox("Motion");
    QFormLayout* form = new QFormLayout(group);
    form->setSpacing(8);
    form->setContentsMargins(8, 16, 8, 8);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    // Enabled — mutes a step without deleting it.
    m_enabledCheck = new QCheckBox("Enabled");
    m_enabledCheck->setChecked(true);
    connect(m_enabledCheck, &QCheckBox::toggled, this, &PropertiesPanel::applyChanges);
    form->addRow(m_enabledCheck);

    // Move type
    m_typeCombo = new QComboBox();
    m_typeCombo->addItems({"MovJ (Joint)", "MovL (Linear)", "Arc"});
    m_typeCombo->setMinimumHeight(26);
    m_typeCombo->setMinimumWidth(100);
    connect(m_typeCombo, &QComboBox::currentIndexChanged, this, &PropertiesPanel::applyChanges);
    connect(m_typeCombo, &QComboBox::currentIndexChanged, this, &PropertiesPanel::updateViaPointVisibility);
    form->addRow("Type:", m_typeCombo);

    // Via-point (Arc only) — the data field already existed, nothing let
    // you pick it before.
    m_viaPointCombo = new QComboBox();
    m_viaPointCombo->setMinimumHeight(26);
    connect(m_viaPointCombo, &QComboBox::currentIndexChanged, this, &PropertiesPanel::applyChanges);
    form->addRow("Via Point:", m_viaPointCombo);
    m_viaPointLabel = qobject_cast<QLabel*>(form->labelForField(m_viaPointCombo));

    // Speed
    m_speedSpin = new QSpinBox();
    m_speedSpin->setRange(1, 100);
    m_speedSpin->setSuffix("%");
    m_speedSpin->setValue(80);
    m_speedSpin->setMinimumHeight(26);
    m_speedSpin->setMinimumWidth(80);
    connect(m_speedSpin, &QSpinBox::valueChanged, this, &PropertiesPanel::applyChanges);
    form->addRow("Speed:", m_speedSpin);

    // Acceleration
    m_accSpin = new QSpinBox();
    m_accSpin->setRange(1, 100);
    m_accSpin->setSuffix("%");
    m_accSpin->setValue(50);
    m_accSpin->setMinimumHeight(26);
    m_accSpin->setMinimumWidth(80);
    connect(m_accSpin, &QSpinBox::valueChanged, this, &PropertiesPanel::applyChanges);
    form->addRow("Accel:", m_accSpin);

    // Stop After Move — a friendlier toggle over the raw "CP=0 means stop"
    // semantics (TimelineSegment::cpValue: "0 = STOP, >0 = blend ratio").
    // The underlying behavior already worked; this just makes it discoverable
    // instead of requiring the operator to know to zero out CP Blend by hand.
    m_stopAfterCheck = new QCheckBox("Stop After Move (no blend)");
    connect(m_stopAfterCheck, &QCheckBox::toggled, this, [this](bool stop) {
        m_cpSpin->blockSignals(true);
        if (stop) {
            if (m_cpSpin->value() > 0.0) m_lastBlendMm = m_cpSpin->value();
            m_cpSpin->setValue(0.0);
        } else {
            m_cpSpin->setValue(m_lastBlendMm > 0.0 ? m_lastBlendMm : 10.0);
        }
        m_cpSpin->setEnabled(!stop && !m_currentId.isEmpty());
        m_cpSpin->blockSignals(false);
        applyChanges();
    });
    form->addRow(m_stopAfterCheck);

    // Pause After Move — a waypoint that halts playback (until the operator
    // hits Play again) once this move is confirmed complete. Useful for a
    // manual camera reload, a set change, etc.
    m_pauseAfterCheck = new QCheckBox("Pause After Move (wait for operator)");
    connect(m_pauseAfterCheck, &QCheckBox::toggled, this, &PropertiesPanel::applyChanges);
    form->addRow(m_pauseAfterCheck);

    // CP (Continuous Path)
    m_cpSpin = new QDoubleSpinBox();
    m_cpSpin->setRange(0.0, 100.0);
    m_cpSpin->setDecimals(1);
    m_cpSpin->setSuffix(" mm");
    m_cpSpin->setMinimumHeight(26);
    m_cpSpin->setMinimumWidth(80);
    connect(m_cpSpin, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        // Keep the Stop toggle in sync if CP is edited directly.
        m_stopAfterCheck->blockSignals(true);
        m_stopAfterCheck->setChecked(v <= 0.0);
        m_stopAfterCheck->blockSignals(false);
        applyChanges();
    });
    form->addRow("CP Blend:", m_cpSpin);

    group->setMinimumHeight(270);
    layout->addWidget(group);

    // ── Timing group ─────────────────────────────────────────────────────
    QGroupBox* timeGroup = new QGroupBox("Timing");
    QFormLayout* timeForm = new QFormLayout(timeGroup);
    timeForm->setSpacing(8);
    timeForm->setContentsMargins(8, 16, 8, 8);
    timeForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    timeForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_triggerTimeSpin = new QDoubleSpinBox();
    m_triggerTimeSpin->setRange(0.0, 9999.0);
    m_triggerTimeSpin->setDecimals(2);
    m_triggerTimeSpin->setSuffix(" s");
    m_triggerTimeSpin->setMinimumHeight(26);
    connect(m_triggerTimeSpin, &QDoubleSpinBox::valueChanged, this, &PropertiesPanel::applyChanges);
    timeForm->addRow("Trigger:", m_triggerTimeSpin);

    m_triggerTimeMinLabel = new QLabel();
    m_triggerTimeMinLabel->setStyleSheet("color: #999; font-size: 10px;");
    timeForm->addRow("", m_triggerTimeMinLabel);

    m_preWaitSpin = new QDoubleSpinBox();
    m_preWaitSpin->setRange(0.0, 60.0);
    m_preWaitSpin->setDecimals(1);
    m_preWaitSpin->setSuffix(" s");
    m_preWaitSpin->setMinimumHeight(26);
    connect(m_preWaitSpin, &QDoubleSpinBox::valueChanged, this, &PropertiesPanel::applyChanges);
    timeForm->addRow("Pre-Wait:", m_preWaitSpin);

    m_postWaitSpin = new QDoubleSpinBox();
    m_postWaitSpin->setRange(0.0, 60.0);
    m_postWaitSpin->setDecimals(1);
    m_postWaitSpin->setSuffix(" s");
    m_postWaitSpin->setMinimumHeight(26);
    connect(m_postWaitSpin, &QDoubleSpinBox::valueChanged, this, &PropertiesPanel::applyChanges);
    timeForm->addRow("Post-Wait:", m_postWaitSpin);

    timeGroup->setMinimumHeight(140);
    layout->addWidget(timeGroup);

    // ── Camera trigger group ─────────────────────────────────────────────
    QGroupBox* camGroup = new QGroupBox("Camera");
    QFormLayout* camForm = new QFormLayout(camGroup);
    camForm->setSpacing(8);
    camForm->setContentsMargins(8, 16, 8, 8);
    camForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    camForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_camTriggerCombo = new QComboBox();
    m_camTriggerCombo->addItems({"None", "Start Record", "Stop Record", "Take Photo"});
    m_camTriggerCombo->setMinimumHeight(26);
    connect(m_camTriggerCombo, &QComboBox::currentIndexChanged, this, &PropertiesPanel::applyChanges);
    camForm->addRow("Trigger:", m_camTriggerCombo);

    // Trigger At — the data field already existed (AtStart/AtEnd/AtBoth),
    // nothing let you pick it before; matters when CP blend is >0 (§8.4:
    // a trigger during a blended transition fires mid-motion, not at the
    // waypoint, unless the operator explicitly picks where it should land).
    m_triggerAtCombo = new QComboBox();
    m_triggerAtCombo->addItems({"At Start", "At End", "At Both"});
    m_triggerAtCombo->setMinimumHeight(26);
    connect(m_triggerAtCombo, &QComboBox::currentIndexChanged, this, &PropertiesPanel::applyChanges);
    camForm->addRow("Trigger At:", m_triggerAtCombo);

    layout->addWidget(camGroup);

    // ── Delete ────────────────────────────────────────────────────────────
    // Previously deletion was keyboard-only (Delete/Backspace on the
    // timeline) with no discoverable UI control.
    m_deleteBtn = new QPushButton("Delete Segment");
    m_deleteBtn->setMinimumHeight(30);
    m_deleteBtn->setStyleSheet(
        "QPushButton { background: #4a2020; color: #ff9999; border: 1px solid #883333; border-radius: 4px; }"
        "QPushButton:hover:!disabled { background: #663030; }"
        "QPushButton:disabled { color: #666; border-color: #444; }"
    );
    connect(m_deleteBtn, &QPushButton::clicked, this, [this]() {
        if (!m_currentId.isEmpty()) emit deleteRequested(m_currentId);
    });
    layout->addWidget(m_deleteBtn);

    layout->addStretch();

    scrollArea->setWidget(innerWidget);
    outerLayout->addWidget(scrollArea);

    // Fixed width — prevent disproportionate stretching
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    setMinimumWidth(240);
    setMaximumWidth(400);
}

void PropertiesPanel::loadSegment(const QString& segId)
{
    int idx = m_model->indexOf(segId);
    if (idx < 0) {
        clearSelection();
        return;
    }

    m_currentId = segId;
    TimelineSegment seg = m_model->segmentAt(idx);

    refreshViaPointChoices();

    // Block signals while loading
    m_enabledCheck->blockSignals(true);
    m_typeCombo->blockSignals(true);
    m_speedSpin->blockSignals(true);
    m_accSpin->blockSignals(true);
    m_stopAfterCheck->blockSignals(true);
    m_pauseAfterCheck->blockSignals(true);
    m_cpSpin->blockSignals(true);
    m_triggerTimeSpin->blockSignals(true);
    m_preWaitSpin->blockSignals(true);
    m_postWaitSpin->blockSignals(true);
    m_camTriggerCombo->blockSignals(true);
    m_triggerAtCombo->blockSignals(true);
    m_viaPointCombo->blockSignals(true);

    m_nameLabel->setText(m_model->data(m_model->index(idx), Qt::DisplayRole).toString());
    m_enabledCheck->setChecked(seg.enabled);
    m_typeCombo->setCurrentIndex(static_cast<int>(seg.type));
    m_speedSpin->setValue(seg.speedPct);
    m_accSpin->setValue(seg.accPct);
    bool stopAfter = seg.cpValue <= 0.0;
    m_stopAfterCheck->setChecked(stopAfter);
    m_pauseAfterCheck->setChecked(seg.pauseAfter);
    if (seg.cpValue > 0.0) m_lastBlendMm = seg.cpValue;
    m_cpSpin->setValue(seg.cpValue);
    m_triggerTimeSpin->setValue(seg.triggerTime);
    m_preWaitSpin->setValue(seg.preWait);
    m_postWaitSpin->setValue(seg.postWait);
    m_camTriggerCombo->setCurrentIndex(static_cast<int>(seg.camTrigger));
    m_triggerAtCombo->setCurrentIndex(static_cast<int>(seg.triggerAt));
    int viaIdx = m_viaPointCombo->findData(seg.arcViaPointId);
    m_viaPointCombo->setCurrentIndex(viaIdx >= 0 ? viaIdx : 0);

    m_enabledCheck->blockSignals(false);
    m_typeCombo->blockSignals(false);
    m_speedSpin->blockSignals(false);
    m_accSpin->blockSignals(false);
    m_stopAfterCheck->blockSignals(false);
    m_pauseAfterCheck->blockSignals(false);
    m_cpSpin->blockSignals(false);
    m_triggerTimeSpin->blockSignals(false);
    m_preWaitSpin->blockSignals(false);
    m_postWaitSpin->blockSignals(false);
    m_camTriggerCombo->blockSignals(false);
    m_triggerAtCombo->blockSignals(false);
    m_viaPointCombo->blockSignals(false);

    setFieldsEnabled(true);
    m_cpSpin->setEnabled(!stopAfter); // CP Blend only makes sense when not stopping
    updateViaPointVisibility();

    // Physically-minimum trigger time relative to this segment's chronological
    // predecessor — a floor manual edits can't go below (see applyChanges()).
    m_minTriggerTime = 0.0;
    if (m_ptModel) {
        TimelineSegment predecessor;
        bool havePredecessor = false;
        for (int i = 0; i < m_model->rowCount(); ++i) {
            if (i == idx) continue;
            TimelineSegment candidate = m_model->segmentAt(i);
            if (candidate.triggerTime <= seg.triggerTime &&
                (!havePredecessor || candidate.triggerTime > predecessor.triggerTime)) {
                predecessor = candidate;
                havePredecessor = true;
            }
        }
        if (havePredecessor) {
            CameraPoint fromPt = m_ptModel->pointById(predecessor.pointId);
            CameraPoint toPt   = m_ptModel->pointById(seg.pointId);
            if (!fromPt.id.isEmpty() && !toPt.id.isEmpty()) {
                double minGap = motion::minGantryDurationSec(fromPt, toPt, m_model->gantryMotorSpec(), 0.0);
                m_minTriggerTime = predecessor.triggerTime + minGap;
            }
        }
    }
    m_triggerTimeMinLabel->setText(
        m_minTriggerTime > 0.0 ? QString("(min: %1s)").arg(m_minTriggerTime, 0, 'f', 2) : "");
}

void PropertiesPanel::clearSelection()
{
    m_currentId.clear();
    m_nameLabel->setText("(No selection)");
    setFieldsEnabled(false);
}

void PropertiesPanel::setFieldsEnabled(bool enabled)
{
    m_enabledCheck->setEnabled(enabled);
    m_typeCombo->setEnabled(enabled);
    m_speedSpin->setEnabled(enabled);
    m_accSpin->setEnabled(enabled);
    m_stopAfterCheck->setEnabled(enabled);
    m_pauseAfterCheck->setEnabled(enabled);
    m_cpSpin->setEnabled(enabled);
    m_triggerTimeSpin->setEnabled(enabled);
    m_preWaitSpin->setEnabled(enabled);
    m_postWaitSpin->setEnabled(enabled);
    m_camTriggerCombo->setEnabled(enabled);
    m_triggerAtCombo->setEnabled(enabled);
    m_viaPointCombo->setEnabled(enabled);
    m_deleteBtn->setEnabled(enabled);
    if (!enabled) updateViaPointVisibility();
}

void PropertiesPanel::setPointsModel(PointsModel* model)
{
    m_ptModel = model;
}

void PropertiesPanel::refreshViaPointChoices()
{
    QString previous = m_viaPointCombo->currentData().toString();
    m_viaPointCombo->blockSignals(true);
    m_viaPointCombo->clear();
    m_viaPointCombo->addItem("(none)", QString());
    if (m_ptModel) {
        for (const CameraPoint& pt : m_ptModel->allPoints()) {
            m_viaPointCombo->addItem(pt.name, pt.id);
        }
    }
    int idx = m_viaPointCombo->findData(previous);
    m_viaPointCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    m_viaPointCombo->blockSignals(false);
}

void PropertiesPanel::updateViaPointVisibility()
{
    bool isArc = m_typeCombo->currentIndex() == static_cast<int>(TimelineSegment::Arc);
    bool visible = isArc && !m_currentId.isEmpty();
    if (m_viaPointLabel) m_viaPointLabel->setVisible(visible);
    m_viaPointCombo->setVisible(visible);
}

void PropertiesPanel::applyChanges()
{
    if (m_currentId.isEmpty()) return;

    int idx = m_model->indexOf(m_currentId);
    if (idx < 0) return;

    TimelineSegment oldSeg = m_model->segmentAt(idx);
    TimelineSegment seg = oldSeg;
    seg.enabled       = m_enabledCheck->isChecked();
    seg.type          = static_cast<TimelineSegment::Type>(m_typeCombo->currentIndex());
    seg.speedPct      = m_speedSpin->value();
    seg.accPct        = m_accSpin->value();
    seg.cpValue       = m_cpSpin->value();

    // Hard floor: the gantry can't arrive sooner than physics allows, so a
    // manually-entered time below that gets clamped up (not silently — the
    // spinbox is reset and a signal fires so MainWindow can explain why).
    double requestedTime = m_triggerTimeSpin->value();
    double clampedTime = qMax(requestedTime, m_minTriggerTime);
    if (clampedTime != requestedTime) {
        m_triggerTimeSpin->blockSignals(true);
        m_triggerTimeSpin->setValue(clampedTime);
        m_triggerTimeSpin->blockSignals(false);
        emit triggerTimeClamped(clampedTime, m_minTriggerTime);
    }
    seg.triggerTime   = clampedTime;
    seg.preWait       = m_preWaitSpin->value();
    seg.postWait      = m_postWaitSpin->value();
    seg.camTrigger    = static_cast<TimelineSegment::CamTrigger>(m_camTriggerCombo->currentIndex());
    seg.triggerAt     = static_cast<TimelineSegment::TriggerAt>(m_triggerAtCombo->currentIndex());
    seg.pauseAfter    = m_pauseAfterCheck->isChecked();
    seg.arcViaPointId = m_viaPointCombo->currentData().toString();

    if (m_undoStack) {
        m_undoStack->push(new UpdateSegmentCommand(m_model, m_currentId, oldSeg, seg));
    } else {
        m_model->updateSegment(idx, seg);
    }
    emit segmentModified(m_currentId);
    updateViaPointVisibility();
}
