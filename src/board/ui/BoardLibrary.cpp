#include "board/ui/BoardLibrary.h"
#include "board/ui/BoardLayerPreview.h"
#include "board/model/BoardDocument.h"
#include "actions/manager/ActionManager.h"
#include "actions/theme/Theme.h"

#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QToolBar>
#include <QVBoxLayout>

#include <algorithm>

BoardLibrary::BoardLibrary(BoardDocument* document, QWidget* parent)
    : QWidget(parent), m_document(document), m_list(new QListWidget(this)), m_preview(new QLabel(this))
{
    setObjectName(QStringLiteral("BoardLibrary"));
    m_list->setObjectName(QStringLiteral("BoardLibraryItems"));
    m_list->setAccessibleName(tr("Project library items"));
    m_list->setUniformItemSizes(true);
    m_list->setTextElideMode(Qt::ElideMiddle);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setMinimumHeight(114);
    m_preview->setWordWrap(true);
    auto* tools = new QToolBar(this);
    tools->setIconSize(QSize(16, 16));
    QString const resources = ActionManager::defaultDirectory();
    m_import = tools->addAction(Theme::icon(resources, QStringLiteral("file/open")), tr("Import Images into Library"),
                                this, &BoardLibrary::importRequested);
    m_capture = tools->addAction(Theme::icon(resources, QStringLiteral("timeline/adddrawing")), tr("Add Current Layer to Library"),
                                 this, &BoardLibrary::captureLayer);
    m_place = tools->addAction(Theme::icon(resources, QStringLiteral("edit/paste")), tr("Place in Current Panel"),
                               this, &BoardLibrary::placeItem);
    tools->addSeparator();
    m_rename = tools->addAction(Theme::icon(resources, QStringLiteral("timeline/renamelayer")), tr("Rename Library Item"),
                                this, &BoardLibrary::renameItem);
    m_remove = tools->addAction(Theme::icon(resources, QStringLiteral("edit/delete")), tr("Remove Library Item"),
                                this, &BoardLibrary::removeItem);
    m_import->setObjectName(QStringLiteral("BoardLibraryImport"));
    m_capture->setObjectName(QStringLiteral("BoardLibraryCapture"));
    m_place->setObjectName(QStringLiteral("BoardLibraryPlace"));
    m_remove->setObjectName(QStringLiteral("BoardLibraryRemove"));
    m_list->setContextMenuPolicy(Qt::ActionsContextMenu);
    m_list->addActions({m_place, m_rename, m_remove});

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 3, 4, 4);
    layout->setSpacing(4);
    layout->addWidget(tools);
    layout->addWidget(m_list, 1);
    layout->addWidget(m_preview);
    connect(m_document, &BoardDocument::projectChanged, this, &BoardLibrary::refreshItems);
    connect(m_list, &QListWidget::currentRowChanged, this, &BoardLibrary::refreshSelection);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &BoardLibrary::placeItem);
    refreshItems();
}

void BoardLibrary::setPanel(int shot, int panel, int layer)
{
    m_shot = shot;
    m_panel = panel;
    m_layer = layer;
    updateActions();
}

void BoardLibrary::selectLastItem() { m_list->setCurrentRow(m_list->count() - 1); }

void BoardLibrary::refreshItems()
{
    auto const& project = m_document->project();
    QSize const projectSize(project.width, project.height);
    if (m_items.constData() == project.library.constData() && m_projectSize == projectSize) {
        updateActions();
        return;
    }
    m_projectSize = projectSize;
    m_items = project.library;
    QSignalBlocker const block(m_list);
    int const row = m_list->currentRow();
    m_list->clear();
    for (auto const& item : std::as_const(m_items)) {
        auto* entry = new QListWidgetItem(item.name, m_list);
        entry->setToolTip(item.kind == BoardLayer::Kind::Bitmap ? tr("Bitmap layer") : tr("Vector layer"));
    }
    m_list->setCurrentRow(std::clamp(row, 0, std::max(0, m_list->count() - 1)));
    refreshSelection();
}

void BoardLibrary::refreshSelection()
{
    updateActions();
    int const row = m_list->currentRow();
    if (row >= 0) {
        QSize const preview = m_projectSize.scaled(QSize(192, 108), Qt::KeepAspectRatio).expandedTo(QSize(1, 1));
        m_preview->setPixmap(boardLayerPreview(m_document->project(), m_items.at(row), preview));
    } else {
        m_preview->setText(tr("Import an image or add the current layer to reuse it in other panels."));
    }
}

void BoardLibrary::updateActions()
{
    bool const projectOpen = !m_document->path().isEmpty();
    bool const hasRoom = m_items.size() < BoardProject::maxLibraryItems;
    auto const* panel = m_document->panel(m_shot, m_panel);
    bool const selected = m_list->currentRow() >= 0;
    m_import->setEnabled(projectOpen && hasRoom);
    m_capture->setEnabled(hasRoom && panel && m_layer >= 0 && m_layer < panel->layers.size());
    m_place->setEnabled(selected && panel);
    m_rename->setEnabled(selected);
    m_remove->setEnabled(selected);
}

void BoardLibrary::captureLayer()
{
    if (!m_capture->isEnabled())
        return;
    if (m_document->addLayerToLibrary(m_shot, m_panel, m_layer))
        selectLastItem();
    else
        QMessageBox::warning(this, tr("Add Current Layer to Library"), tr("This layer could not be added within the project’s limits."));
}

void BoardLibrary::placeItem()
{
    if (m_place->isEnabled() && !m_document->placeLibraryItem(m_list->currentRow(), m_shot, m_panel, m_layer))
        QMessageBox::warning(this, tr("Place in Current Panel"), tr("This item could not be placed within the project’s limits."));
}

void BoardLibrary::renameItem()
{
    int const row = m_list->currentRow();
    if (row < 0)
        return;
    QString const oldName = m_items.at(row).name;
    bool accepted = false;
    QString const name = QInputDialog::getText(this, tr("Rename Library Item"), tr("Item name"), QLineEdit::Normal, oldName, &accepted).trimmed();
    if (accepted && name != oldName && !m_document->renameLibraryItem(row, name))
        QMessageBox::information(this, tr("Rename Library Item"),
            tr("Use a unique name of 1–%1 characters that fits within the project’s size limit.").arg(BoardProject::maxLibraryName));
}

void BoardLibrary::removeItem() { m_document->removeLibraryItem(m_list->currentRow()); }
