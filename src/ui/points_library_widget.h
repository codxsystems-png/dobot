#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Points Library Widget
// QListView with thumbnail grid/list toggle. Drag to timeline.
// ═══════════════════════════════════════════════════════════════════════════════

#include <QWidget>
#include <QListView>
#include <QPushButton>
#include <QLabel>

class PointsModel;

class PointsLibraryWidget : public QWidget
{
    Q_OBJECT
public:
    explicit PointsLibraryWidget(PointsModel* model, QWidget* parent = nullptr);

    QListView* listView() const { return m_listView; }

signals:
    void recordPointRequested();
    void goToPointRequested(const QString& pointId);
    void deletePointRequested(const QString& pointId);
    void addToTimelineRequested(const QString& pointId);

private slots:
    void onDoubleClicked(const QModelIndex& index);
    void onContextMenu(const QPoint& pos);

private:
    void setupUI();

    QListView*   m_listView    = nullptr;
    PointsModel* m_model       = nullptr;
    QPushButton* m_recordBtn   = nullptr;
    QPushButton* m_deleteBtn   = nullptr;
    QPushButton* m_toggleBtn   = nullptr;
    QLabel*      m_countLabel  = nullptr;
    bool         m_gridMode    = true;
};
