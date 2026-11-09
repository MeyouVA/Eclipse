#include "board/ui/BoardThumbnails.h"
#include "board/ui/BoardStage.h"
#include "board/model/BoardDocument.h"
#include "actions/manager/ActionManager.h"
#include "actions/theme/Theme.h"
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
#include <QStyledItemDelegate>
#include <cmath>

namespace {
constexpr auto panelMime = "application/x-eclipse-panel-index";
constexpr QSize thumbnailSize{168, 168};
constexpr QSize cardSize{204, 205};
constexpr int namesLockedRole = Qt::UserRole + 1;
constexpr int cardTitleRole = Qt::UserRole + 2;
constexpr int layerNamesRole = Qt::UserRole + 3;

class ThumbnailCard : public QStyledItemDelegate {
public:
    explicit ThumbnailCard(QObject* parent)
        : QStyledItemDelegate(parent), m_lock(Theme::icon(ActionManager::defaultDirectory(), QStringLiteral("timeline/lock"))) {}

    QSize sizeHint(QStyleOptionViewItem const&, QModelIndex const&) const override { return cardSize; }

    void paint(QPainter* painter, QStyleOptionViewItem const& option, QModelIndex const& index) const override
    {
        painter->save();
        QRect const card = option.rect.adjusted(1, 1, -1, -1);
        bool const selected = option.state.testFlag(QStyle::State_Selected);
        QColor const border = selected ? QColor(190, 61, 55) : option.palette.mid().color();
        painter->fillRect(card, option.palette.window());
        painter->setPen(option.palette.shadow().color());
        painter->drawRect(card.adjusted(0, 0, -1, -1));
        QRect const inside = card.adjusted(5, 5, -5, -5);
        painter->setPen(border);
        painter->drawRect(inside);
        QRect title(inside.left() + 7, inside.top() + 1, inside.width() - 14, 17);
        if (index.data(namesLockedRole).toBool()) {
            m_lock.paint(painter, QRect(title.right() - 13, title.top() + 3, 14, 14));
            title.adjust(0, 0, -18, 0);
        }
        painter->setPen(option.palette.windowText().color());
        painter->drawText(title, Qt::AlignVCenter, option.fontMetrics.elidedText(index.data(cardTitleRole).toString(), Qt::ElideRight, title.width()));
        auto const icon = index.data(Qt::DecorationRole).value<QIcon>();
        QRect const art(QPoint(inside.left() + 2, title.bottom() + 1), thumbnailSize);
        icon.paint(painter, art);
        auto const layers = index.data(layerNamesRole).toStringList();
        for (int i = 0; i < std::min(5, int(layers.size())); ++i) {
            QRect const cell(art.right() + 2, art.top() + i * 24, 18, 23);
            painter->fillRect(cell, selected && i == 0 ? option.palette.highlight() : option.palette.button());
            painter->setPen(option.palette.windowText().color());
            painter->drawText(cell, Qt::AlignCenter, option.fontMetrics.elidedText(layers[i], Qt::ElideRight, cell.width()));
        }
        painter->restore();
    }

private:
    QIcon m_lock;
};
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

    void refreshAll()
    {
        m_icons.clear();
        if (!m_rows.isEmpty())
            emit dataChanged(index(0), index(int(m_rows.size()) - 1), {Qt::DecorationRole});
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
        if (role == namesLockedRole)
            return shot.namesLocked;
        QString const panelName = panelData.name.trimmed();
        if (role == layerNamesRole) {
            QStringList layers;
            for (auto const& layer : panelData.layers)
                layers.push_back(layer.name);
            return layers;
        }
        if (role == cardTitleRole) {
            QString const name = panelName.isEmpty() ? QString::number(p + 1) : panelName;
            return BoardThumbnails::tr("%1 (%2/%3)  %4").arg(shot.name, name).arg(shot.panels.size())
                .arg(Storyboard::duration(panelData.length, project.fps, true));
        }
        if (role == Qt::DisplayRole) {
            QString label = BoardThumbnails::tr("%1/%2").arg(p + 1).arg(shot.panels.size());
            if (!panelName.isEmpty())
                label = BoardThumbnails::tr("%1 (%2)").arg(panelName, label);
            return BoardThumbnails::tr("%1 · %2 · %3f").arg(shot.name, label).arg(panelData.length);
        }
        if (role == Qt::ToolTipRole) {
            QString tooltip = BoardThumbnails::tr("Scene %1, Panel %2 (%3 frames)").arg(shot.name).arg(p + 1).arg(panelData.length);
            if (!panelName.isEmpty())
                tooltip += QLatin1Char('\n') + BoardThumbnails::tr("Panel name: %1").arg(panelName);
            if (!shot.sequence.isEmpty())
                tooltip += QLatin1Char('\n') + BoardThumbnails::tr("Sequence %1").arg(shot.sequence);
            if (!shot.act.isEmpty())
                tooltip += QLatin1Char('\n') + BoardThumbnails::tr("Act %1").arg(shot.act);
            if (shot.namesLocked)
                tooltip += QLatin1Char('\n') + BoardThumbnails::tr("Scene and panel names are locked.");
            return tooltip + QLatin1Char('\n') + BoardThumbnails::tr("Drag to move a panel. Shift-drag to move the scene.");
        }
        if (role != Qt::DecorationRole)
            return {};
        if (auto const* icon = m_icons.object(index.row()))
            return *icon;
        QPixmap image(thumbnailSize * 3);
        image.fill(QColor(238, 238, 238));
        double const scale = std::min(double(image.width()) / project.width, double(image.height()) / project.height);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.translate((image.width() - project.width * scale) / 2.0, (image.height() - project.height * scale) / 2.0);
        painter.scale(scale, scale);
        painter.setClipRect(QRectF(0, 0, project.width, project.height));
        Canvas::paintFrame(painter, boardPanelScene(project, s, p, BoardCamera::poseAt(panelData.camera, 0)), 0);
        QPen guide(QColor(177, 177, 177));
        guide.setCosmetic(true);
        painter.setPen(guide);
        painter.drawRect(QRectF(0, 0, project.width, project.height));
        painter.end();
        QIcon const icon(image.scaled(thumbnailSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
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
    setItemDelegate(new ThumbnailCard(this));
    setViewMode(QListView::IconMode);
    setWrapping(false);
    setMovement(QListView::Static);
    setUniformItemSizes(true);
    setIconSize(thumbnailSize);
    setGridSize(cardSize + QSize(16, 12));
    setViewportMargins(24, 12, 8, 3);
    setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::MoveAction);
    setDropIndicatorShown(false);
    setVertical(false);
    connect(selectionModel(), &QItemSelectionModel::currentChanged, this, [this](QModelIndex const& current) {
        auto const [shot, panel] = m_panels->panel(current.row());
        if (shot >= 0)
            emit panelSelected(shot, panel);
    });
}

void BoardThumbnails::setVertical(bool vertical)
{
    vertical = vertical && !m_overview;
    setFlow(vertical ? QListView::TopToBottom : QListView::LeftToRight);
    bool const grid = m_overview;
    setWrapping(grid);
    setMinimumSize(vertical ? QSize(gridSize().width() + 16, 100) : QSize(0, 218));
    setHorizontalScrollBarPolicy(vertical || grid ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAlwaysOn);
    setVerticalScrollBarPolicy(vertical || grid ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
}

void BoardThumbnails::setOverview(bool overview)
{
    m_overview = overview;
    setVertical(flow() == QListView::TopToBottom);
}

void BoardThumbnails::rebuild()
{
    QSignalBlocker const block(selectionModel());
    m_panels->rebuild();
}

void BoardThumbnails::refreshAll() { m_panels->refreshAll(); }

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
    bool const vertical = flow() == QListView::TopToBottom;
    if (!target.isValid()) {
        QRect const first = visualRect(m_panels->index(0));
        int const x = int(std::lround(double(point.x() - first.center().x()) / gridSize().width()));
        int const y = int(std::lround(double(point.y() - first.center().y()) / gridSize().height()));
        int const columns = std::max(1, viewport()->width() / gridSize().width());
        int const position = vertical ? y : isWrapping() ? std::max(0, y) * columns + std::clamp(x, 0, columns - 1) : x;
        int const row = std::clamp(position, 0, m_panels->rowCount() - 1);
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
    QPoint const middle = visualRect(target).center();
    emit panelDropped(sourceShot, sourcePanel, targetShot, targetPanel,
                      vertical ? point.y() >= middle.y() : point.x() >= middle.x(), payload.endsWith(":scene"));
}
