#include "board/ui/BoardThumbnails.h"
#include "board/ui/BoardStage.h"
#include "board/model/BoardDocument.h"
#include "canvas/Canvas.h"

#include <QAbstractListModel>
#include <QApplication>
#include <QCache>
#include <QDrag>
#include <QDropEvent>
#include <QIcon>
#include <QMimeData>
#include <QPainter>
#include <QSignalBlocker>
#include <cmath>

namespace {
constexpr auto panelMime = "application/x-eclipse-panel-index";
constexpr QSize thumbnailSize{128, 72};
}

class BoardThumbnailModel : public QAbstractListModel {
public:
    BoardThumbnailModel(BoardDocument* document, QObject* parent) : QAbstractListModel(parent), m_document(document) {}

    void rebuild()
    {
        beginResetModel();
        m_rows.clear();
        m_icons.clear();
        auto const& shots = m_document->project().shots;
        for (int s = 0; s < shots.size(); ++s)
            for (int p = 0; p < shots[s].panels.size(); ++p)
                m_rows.push_back({s, p});
        endResetModel();
    }

    QPair<int, int> panel(int row) const { return m_rows.value(row, {-1, -1}); }
    int rowFor(int shot, int panel) const { return int(m_rows.indexOf({shot, panel})); }
    int rowCount(QModelIndex const& parent = {}) const override { return parent.isValid() ? 0 : int(m_rows.size()); }
    Qt::DropActions supportedDragActions() const override { return Qt::MoveAction; }
    Qt::DropActions supportedDropActions() const override { return Qt::MoveAction; }
    bool canDropMimeData(QMimeData const* mime, Qt::DropAction action, int, int, QModelIndex const&) const override
    {
        return action == Qt::MoveAction && mime->hasFormat(QLatin1String(panelMime));
    }

    void refresh(int shot, int panel)
    {
        int const row = rowFor(shot, panel);
        if (row >= 0) {
            m_icons.remove(row);
            emit dataChanged(index(row), index(row), {Qt::DecorationRole});
        }
    }

    Qt::ItemFlags flags(QModelIndex const& index) const override
    {
        return QAbstractListModel::flags(index) | Qt::ItemIsDropEnabled
               | (index.isValid() ? Qt::ItemIsDragEnabled : Qt::NoItemFlags);
    }

    QVariant data(QModelIndex const& index, int role) const override
    {
        auto const [s, p] = panel(index.row());
        auto const& project = m_document->project();
        if (!index.isValid() || s < 0 || s >= project.shots.size() || p >= project.shots[s].panels.size())
            return {};
        auto const& shot = project.shots[s];
        auto const& panelData = shot.panels[p];
        if (role == Qt::DisplayRole)
            return BoardThumbnails::tr("%1 · %2/%3 · %4f").arg(shot.name).arg(p + 1).arg(shot.panels.size()).arg(panelData.length);
        if (role == Qt::ToolTipRole)
            return BoardThumbnails::tr("Scene %1, Panel %2 (%3 frames)\nDrag to move a panel. Shift-drag to move the scene.")
                .arg(shot.name).arg(p + 1).arg(panelData.length);
        if (role != Qt::DecorationRole)
            return {};
        if (auto const* icon = m_icons.object(index.row()))
            return *icon;
        QPixmap image(thumbnailSize);
        image.fill(QColor(43, 43, 43));
        double const scale = std::min(double(image.width()) / project.width, double(image.height()) / project.height);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.translate((image.width() - project.width * scale) / 2.0, (image.height() - project.height * scale) / 2.0);
        painter.scale(scale, scale);
        painter.setClipRect(QRectF(0, 0, project.width, project.height));
        Canvas::paintFrame(painter, boardPanelScene(project, s, p), 0);
        painter.end();
        QIcon const icon(image);
        m_icons.insert(index.row(), new QIcon(icon));
        return icon;
    }

private:
    BoardDocument* m_document;
    QVector<QPair<int, int>> m_rows;
    mutable QCache<int, QIcon> m_icons{128};
};

BoardThumbnails::BoardThumbnails(BoardDocument* document, QWidget* parent)
    : QListView(parent), m_panels(new BoardThumbnailModel(document, this))
{
    setObjectName(QStringLiteral("BoardThumbnails"));
    setModel(m_panels);
    setViewMode(QListView::IconMode);
    setFlow(QListView::LeftToRight);
    setWrapping(false);
    setMovement(QListView::Static);
    setUniformItemSizes(true);
    setIconSize(thumbnailSize);
    setGridSize(QSize(150, 104));
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::MoveAction);
    setDropIndicatorShown(false);
    setMinimumHeight(120);
    connect(selectionModel(), &QItemSelectionModel::currentChanged, this, [this](QModelIndex const& current) {
        auto const [shot, panel] = m_panels->panel(current.row());
        if (shot >= 0)
            emit panelSelected(shot, panel);
    });
}

void BoardThumbnails::rebuild()
{
    QSignalBlocker const block(selectionModel());
    m_panels->rebuild();
}

void BoardThumbnails::selectPanel(int shot, int panel)
{
    QSignalBlocker const block(selectionModel());
    auto const index = m_panels->index(m_panels->rowFor(shot, panel));
    setCurrentIndex(index);
    if (index.isValid())
        scrollTo(index);
}

void BoardThumbnails::refreshPanel(int shot, int panel) { m_panels->refresh(shot, panel); }

void BoardThumbnails::startDrag(Qt::DropActions)
{
    auto const index = currentIndex();
    if (!index.isValid())
        return;
    auto* mime = new QMimeData;
    bool const whole = QApplication::keyboardModifiers().testFlag(Qt::ShiftModifier);
    mime->setData(QLatin1String(panelMime), QByteArray::number(index.row()) + (whole ? ":scene" : ":panel"));
    auto* drag = new QDrag(this);
    drag->setMimeData(mime);
    drag->setPixmap(index.data(Qt::DecorationRole).value<QIcon>().pixmap(thumbnailSize));
    drag->exec(Qt::MoveAction);
}

bool BoardThumbnails::acceptsDrag(QDropEvent const* event) const
{
    return event->source() == this && event->mimeData()->hasFormat(QLatin1String(panelMime));
}

void BoardThumbnails::dragEnterEvent(QDragEnterEvent* event)
{
    if (acceptsDrag(event))
        event->acceptProposedAction();
}

void BoardThumbnails::dragMoveEvent(QDragMoveEvent* event)
{
    if (acceptsDrag(event)) {
        QListView::dragMoveEvent(event);
        event->acceptProposedAction();
    }
}

void BoardThumbnails::dropEvent(QDropEvent* event)
{
    if (!acceptsDrag(event) || m_panels->rowCount() == 0)
        return;
    QPoint const point = event->position().toPoint();
    QModelIndex target = indexAt(point);
    if (!target.isValid()) {
        QRect const first = visualRect(m_panels->index(0));
        double const column = double(point.x() - first.center().x()) / gridSize().width();
        int const row = std::clamp(int(std::lround(column)), 0, m_panels->rowCount() - 1);
        target = m_panels->index(row);
    }
    QByteArray const payload = event->mimeData()->data(QLatin1String(panelMime));
    bool ok = false;
    int const source = payload.left(payload.indexOf(':')).toInt(&ok);
    auto const [sourceShot, sourcePanel] = m_panels->panel(ok ? source : -1);
    auto const [targetShot, targetPanel] = m_panels->panel(target.row());
    if (sourceShot < 0 || targetShot < 0)
        return;
    event->acceptProposedAction();
    emit panelDropped(sourceShot, sourcePanel, targetShot, targetPanel,
                      point.x() >= visualRect(target).center().x(), payload.endsWith(":scene"));
}
