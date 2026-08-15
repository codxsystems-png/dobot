// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Points Library Widget
// ═══════════════════════════════════════════════════════════════════════════════

#include "ui/points_library_widget.h"
#include "models/points_model.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMenu>
#include <QAction>
#include <QItemSelectionModel>

PointsLibraryWidget::PointsLibraryWidget(PointsModel* model, QWidget* parent)
    : QWidget(parent)
    , m_model(model)
{
    setupUI();
}

void PointsLibraryWidget::setupUI()
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    // Title bar
    QHBoxLayout* header = new QHBoxLayout();
    QLabel* title = new QLabel("Points Library");
    title->setStyleSheet("font-weight: bold; font-size: 13px; color: #e0e0e0;");
    header->addWidget(title);

    m_countLabel = new QLabel("(0)");
    m_countLabel->setStyleSheet("color: #aaaaaa;");
    header->addWidget(m_countLabel);
    header->addStretch();

    m_toggleBtn = new QPushButton("Grid");
    m_toggleBtn->setMinimumWidth(60);
    connect(m_toggleBtn, &QPushButton::clicked, this, [this]() {
        m_gridMode = !m_gridMode;
        m_toggleBtn->setText(m_gridMode ? "List" : "Grid");
        if (m_gridMode) {
            m_listView->setViewMode(QListView::IconMode);
            m_listView->setIconSize(QSize(160, 90));
            m_listView->setGridSize(QSize(176, 110));
        } else {
            m_listView->setViewMode(QListView::ListMode);
            m_listView->setIconSize(QSize(80, 45));
            m_listView->setGridSize(QSize());
        }
    });
    header->addWidget(m_toggleBtn);
    layout->addLayout(header);

    // List view
    m_listView = new QListView();
    m_listView->setModel(m_model);
    m_listView->setDragEnabled(true);
    m_listView->setDefaultDropAction(Qt::CopyAction);
    m_listView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_listView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_listView->setViewMode(QListView::IconMode);
    m_listView->setIconSize(QSize(160, 90));
    m_listView->setGridSize(QSize(176, 110));
    m_listView->setWrapping(true);
    m_listView->setResizeMode(QListView::Adjust);
    m_listView->setSpacing(4);

    connect(m_listView, &QListView::doubleClicked,
            this, &PointsLibraryWidget::onDoubleClicked);
    connect(m_listView, &QListView::customContextMenuRequested,
            this, &PointsLibraryWidget::onContextMenu);

    // Delete key on the list, matching the timeline's Delete/Backspace UX.
    QAction* deleteAction = new QAction(m_listView);
    deleteAction->setShortcuts({QKeySequence::Delete, QKeySequence(Qt::Key_Backspace)});
    deleteAction->setShortcutContext(Qt::WidgetShortcut);
    connect(deleteAction, &QAction::triggered, this, [this]() {
        QModelIndex index = m_listView->currentIndex();
        if (!index.isValid()) return;
        emit deletePointRequested(m_model->data(index, PointsModel::IdRole).toString());
    });
    m_listView->addAction(deleteAction);

    layout->addWidget(m_listView, 1);

    // Record / Delete buttons
    QHBoxLayout* buttonRow = new QHBoxLayout();
    buttonRow->setSpacing(4);

    m_recordBtn = new QPushButton("+ Record Point");
    m_recordBtn->setObjectName("recordPointBtn");
    m_recordBtn->setMinimumHeight(32);
    connect(m_recordBtn, &QPushButton::clicked,
            this, &PointsLibraryWidget::recordPointRequested);
    buttonRow->addWidget(m_recordBtn, 1);

    m_deleteBtn = new QPushButton("Delete");
    m_deleteBtn->setMinimumHeight(32);
    m_deleteBtn->setEnabled(false); // enabled once a point is selected
    m_deleteBtn->setStyleSheet(
        "QPushButton { background: #4a2020; color: #ff9999; border: 1px solid #883333; border-radius: 4px; padding: 0 12px; }"
        "QPushButton:hover:!disabled { background: #663030; }"
        "QPushButton:disabled { color: #666; border-color: #444; background: transparent; }"
    );
    connect(m_deleteBtn, &QPushButton::clicked, this, [this]() {
        QModelIndex index = m_listView->currentIndex();
        if (!index.isValid()) return;
        emit deletePointRequested(m_model->data(index, PointsModel::IdRole).toString());
    });
    buttonRow->addWidget(m_deleteBtn);

    layout->addLayout(buttonRow);

    connect(m_listView->selectionModel(), &QItemSelectionModel::currentChanged,
            this, [this](const QModelIndex& current, const QModelIndex&) {
                m_deleteBtn->setEnabled(current.isValid());
            });

    // Track count changes
    connect(m_model, &QAbstractItemModel::rowsInserted, this, [this]() {
        m_countLabel->setText(QString("(%1)").arg(m_model->rowCount()));
    });
    connect(m_model, &QAbstractItemModel::rowsRemoved, this, [this]() {
        m_countLabel->setText(QString("(%1)").arg(m_model->rowCount()));
    });
    connect(m_model, &QAbstractItemModel::modelReset, this, [this]() {
        m_countLabel->setText(QString("(%1)").arg(m_model->rowCount()));
    });
}

void PointsLibraryWidget::onDoubleClicked(const QModelIndex& index)
{
    if (!index.isValid()) return;
    QString id = m_model->data(index, PointsModel::IdRole).toString();
    emit goToPointRequested(id);
}

void PointsLibraryWidget::onContextMenu(const QPoint& pos)
{
    QModelIndex index = m_listView->indexAt(pos);
    if (!index.isValid()) return;

    QString id = m_model->data(index, PointsModel::IdRole).toString();
    QString name = m_model->data(index, PointsModel::NameRole).toString();

    QMenu menu;
    menu.addAction("Add to Timeline", this, [this, id]() {
        emit addToTimelineRequested(id);
    });
    menu.addAction("Go To Point", this, [this, id]() {
        emit goToPointRequested(id);
    });
    menu.addSeparator();
    menu.addAction("Delete", this, [this, id]() {
        emit deletePointRequested(id);
    });

    menu.exec(m_listView->viewport()->mapToGlobal(pos));
}
