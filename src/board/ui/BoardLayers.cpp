#include "board/ui/BoardLayers.h"
#include "board/model/BoardDocument.h"
#include "actions/manager/ActionManager.h"
#include "actions/theme/Theme.h"
#include "canvas/Canvas.h"

#include <QHeaderView>
#include <QInputDialog>
#include <QMenu>
#include <QPainter>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

BoardLayers::BoardLayers(BoardDocument* document, QWidget* parent)
    : QWidget(parent), m_document(document), m_list(new QTreeWidget(this)), m_add(new QToolButton(this))
{
    m_list->setObjectName(QStringLiteral("BoardLayerList"));
    m_list->setHeaderLabels({tr("Layer"), tr("Type"), tr("Visible"), tr("Locked")});
    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);
    m_list->setIconSize(QSize(66, 38));
    m_list->header()->setStretchLastSection(false);
    m_list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int column = 1; column < m_list->columnCount(); ++column)
        m_list->header()->setSectionResizeMode(column, QHeaderView::ResizeToContents);

    QString const icons = ActionManager::defaultDirectory();
    auto* tools = new QToolBar(this);
    tools->setIconSize(QSize(18, 18));
    m_add->setIcon(Theme::icon(icons, QStringLiteral("timeline/adddrawing")));
    m_add->setToolTip(tr("Add Layer"));
    m_add->setAccessibleName(tr("Add Layer"));
    auto* menu = new QMenu(m_add);
    menu->addAction(tr("New Vector Layer"), this, [this] { editLayer(QStringLiteral("vector")); });
    menu->addAction(tr("New Bitmap Layer"), this, [this] { editLayer(QStringLiteral("bitmap")); });
    m_add->setMenu(menu);
    m_add->setPopupMode(QToolButton::InstantPopup);
    tools->addWidget(m_add);
    auto command = [this, tools, &icons](QString const& text, char const* icon, char const* operation) {
        auto* action = tools->addAction(Theme::icon(icons, QLatin1String(icon)), text);
        connect(action, &QAction::triggered, this, [this, operation] { editLayer(QLatin1String(operation)); });
        return action;
    };
    m_rename = command(tr("Rename Layer"), "timeline/renamelayer", "rename");
    m_duplicate = command(tr("Duplicate Layer"), "timeline/duplicatelayer", "duplicate");
    m_up = command(tr("Move Layer Up"), "timeline/layerup", "up");
    m_down = command(tr("Move Layer Down"), "timeline/layerdown", "down");
    m_remove = command(tr("Delete Layer"), "timeline/deletelayer", "remove");

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 0);
    layout->setSpacing(2);
    layout->addWidget(m_list);
    layout->addWidget(tools);
    connect(m_list, &QTreeWidget::currentItemChanged, this, [this] {
        updateActions();
        emit layerSelected(row());
    });
    connect(m_list, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem*, int column) {
        if (column == 0)
            editLayer(QStringLiteral("rename"));
    });
    connect(m_list, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
        if (column < 2)
            return;
        QScopedValueRollback<bool> const writing(m_writingFlags, true);
        m_document->setLayerFlags(m_shot, m_panel, m_list->indexOfTopLevelItem(item),
                                  item->checkState(2) == Qt::Checked, item->checkState(3) == Qt::Checked);
    });
    updateActions();
}

int BoardLayers::showPanel(int shot, int panel)
{
    if (m_writingFlags && m_shot == shot && m_panel == panel)
        return row();
    int const previous = m_shot == shot && m_panel == panel ? std::max(0, row()) : 0;
    m_shot = shot;
    m_panel = panel;
    QSignalBlocker const block(m_list);
    m_list->clear();
    if (auto const* current = m_document->panel(shot, panel))
        for (auto const& layer : current->layers) {
            auto* item = new QTreeWidgetItem({layer.name, layer.kind == BoardLayer::Kind::Bitmap ? tr("Bitmap") : tr("Vector")});
            item->setCheckState(2, layer.visible ? Qt::Checked : Qt::Unchecked);
            item->setCheckState(3, layer.locked ? Qt::Checked : Qt::Unchecked);
            m_list->addTopLevelItem(item);
            updatePreview(m_list->topLevelItemCount() - 1);
        }
    m_list->setCurrentItem(m_list->topLevelItem(std::min(previous, m_list->topLevelItemCount() - 1)));
    updateActions();
    return row();
}

void BoardLayers::refreshSelectedPreview() { updatePreview(row()); }

void BoardLayers::updatePreview(int index)
{
    auto const* current = m_document->panel(m_shot, m_panel);
    if (!current || index < 0 || index >= current->layers.size())
        return;
    auto* item = m_list->topLevelItem(index);
    if (!item)
        return;
    auto const& project = m_document->project();
    auto const& layer = current->layers[index];
    Scene scene;
    scene.width = project.width;
    scene.height = project.height;
    QSize const size = m_list->iconSize();
    QPixmap thumbnail(size * 2);
    thumbnail.fill(QColor(238, 238, 238));
    QPainter painter(&thumbnail);
    double const scale = std::min(double(thumbnail.width()) / project.width, double(thumbnail.height()) / project.height);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    painter.translate((thumbnail.width() - project.width * scale) / 2, (thumbnail.height() - project.height * scale) / 2);
    painter.scale(scale, scale);
    painter.setClipRect(QRectF(0, 0, project.width, project.height));
    Canvas::paintDrawing(painter, scene, layer.drawing, 1.0);
    painter.end();
    item->setIcon(0, QIcon(thumbnail.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)));
    item->setSizeHint(0, QSize(0, size.height() + 8));
}

int BoardLayers::row() const { return m_list->indexOfTopLevelItem(m_list->currentItem()); }

void BoardLayers::updateActions()
{
    int const current = row();
    m_add->setEnabled(m_document->panel(m_shot, m_panel) != nullptr);
    m_rename->setEnabled(current >= 0);
    m_duplicate->setEnabled(current >= 0);
    m_remove->setEnabled(current >= 0);
    m_up->setEnabled(current > 0);
    m_down->setEnabled(current >= 0 && current + 1 < m_list->topLevelItemCount());
}

void BoardLayers::editLayer(QString const& operation)
{
    int const shot = m_shot;
    int const panel = m_panel;
    auto const* current = m_document->panel(shot, panel);
    if (!current)
        return;
    int const layer = row();
    if (operation == QLatin1String("vector") || operation == QLatin1String("bitmap")) {
        auto const kind = operation == QLatin1String("bitmap") ? BoardLayer::Kind::Bitmap : BoardLayer::Kind::Vector;
        QString name;
        int number = 1;
        do {
            name = tr("Layer %1").arg(number++);
        } while (std::any_of(current->layers.cbegin(), current->layers.cend(), [&name](BoardLayer const& item) { return item.name == name; }));
        if (m_document->addLayer(shot, panel, name, kind, std::max(0, layer)))
            m_list->setCurrentItem(m_list->topLevelItem(std::max(0, layer)));
    } else if (layer < 0) {
        return;
    } else if (operation == QLatin1String("rename")) {
        bool accepted = false;
        QString const name = QInputDialog::getText(this, tr("Rename Layer"), tr("Layer name"),
            QLineEdit::Normal, current->layers[layer].name, &accepted);
        if (accepted)
            m_document->renameLayer(shot, panel, layer, name);
    } else if (operation == QLatin1String("duplicate")) {
        m_document->duplicateLayer(shot, panel, layer);
    } else if (operation == QLatin1String("remove")) {
        m_document->removeLayer(shot, panel, layer);
    } else {
        int const target = layer + (operation == QLatin1String("up") ? -1 : 1);
        if (m_document->moveLayer(shot, panel, layer, target))
            m_list->setCurrentItem(m_list->topLevelItem(target));
    }
}
