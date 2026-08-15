#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Timeline Undo Commands
// QUndoCommand subclasses wrapping SegmentsModel mutations, so every
// segment edit (drag, properties panel, drop, delete, duplicate, paste)
// goes through one shared undo/redo history instead of being irreversible.
// All commands re-resolve the row by segment id at execute time rather than
// caching a row index, since rows can shift under concurrent edits.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QUndoCommand>
#include "models/segments_model.h"
#include "core/types.h"

class AddSegmentCommand : public QUndoCommand
{
public:
    AddSegmentCommand(SegmentsModel* model, TimelineSegment seg, const QString& text = "Add Segment")
        : QUndoCommand(text), m_model(model), m_seg(std::move(seg)) {}

    void redo() override { m_model->addSegment(m_seg); }
    void undo() override {
        int row = m_model->indexOf(m_seg.id);
        if (row >= 0) m_model->removeSegment(row);
    }

private:
    SegmentsModel* m_model;
    TimelineSegment m_seg;
};

class RemoveSegmentCommand : public QUndoCommand
{
public:
    RemoveSegmentCommand(SegmentsModel* model, TimelineSegment seg, const QString& text = "Delete Segment")
        : QUndoCommand(text), m_model(model), m_seg(std::move(seg)) {}

    void redo() override {
        int row = m_model->indexOf(m_seg.id);
        if (row >= 0) m_model->removeSegment(row);
    }
    void undo() override { m_model->addSegment(m_seg); }

private:
    SegmentsModel* m_model;
    TimelineSegment m_seg;
};

class UpdateSegmentCommand : public QUndoCommand
{
public:
    UpdateSegmentCommand(SegmentsModel* model, QString segId,
                          TimelineSegment oldSeg, TimelineSegment newSeg,
                          const QString& text = "Edit Segment")
        : QUndoCommand(text), m_model(model), m_segId(std::move(segId))
        , m_old(std::move(oldSeg)), m_new(std::move(newSeg))
    {}

    void redo() override { apply(m_new); }
    void undo() override { apply(m_old); }

    // Consecutive edits to the same field on the same segment (e.g. typing
    // into a spinbox) collapse into a single undo step instead of one step
    // per keystroke/tick.
    int id() const override { return 1001; }
    bool mergeWith(const QUndoCommand* other) override
    {
        if (other->id() != id()) return false;
        auto* o = static_cast<const UpdateSegmentCommand*>(other);
        if (o->m_segId != m_segId) return false;
        m_new = o->m_new;
        return true;
    }

private:
    void apply(const TimelineSegment& seg) {
        int row = m_model->indexOf(m_segId);
        if (row >= 0) m_model->updateSegment(row, seg);
    }

    SegmentsModel* m_model;
    QString m_segId;
    TimelineSegment m_old;
    TimelineSegment m_new;
};
