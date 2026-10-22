// Eclipse - the Colour view: how it is put together, the list of colours, and the colours in hand.
#include "ui/panels/PalettePanel.h"
#include "ui/panels/GradientBar.h"

#include <algorithm>
#include <cmath>
#include <QDir>
#include <QMenu>
#include <QSettings>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QListWidget>
#include <QPainter>
#include <QVBoxLayout>

#include "model/document/Commands.h"

static QIcon swatchIcon(Swatch const& swatch)
{
    // A long low patch of the colour, in a white edge so that dark colours show on the dark list.
    QPixmap pixmap(34, 14);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    // A checkerboard shows through translucent colours.
    for (int y = 0; y < 14; y += 7)
        for (int x = 0; x < 34; x += 7)
            painter.fillRect(x, y, 7, 7, ((x + y) / 7) % 2 ? QColor(200, 200, 200) : QColor(255, 255, 255));
    painter.fillRect(pixmap.rect(), swatch.kind == Swatch::Radial ? swatch.brush(QPointF(17, 7), QPointF(34, 7)) : swatch.brush(QPointF(0, 7), QPointF(34, 7)));
    painter.setPen(QColor(240, 240, 240));
    painter.drawRect(0, 0, 33, 13);
    painter.end();
    // The same picture when the row is selected: the default would tint it with the highlight.
    QIcon icon;
    icon.addPixmap(pixmap, QIcon::Normal);
    icon.addPixmap(pixmap, QIcon::Selected);
    return icon;
}

PalettePanel::PalettePanel(Document* document, QWidget* parent) : QWidget(parent), m_document(document)
{
    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("ColourList"));
    m_list->setIconSize(QSize(34, 14));
    m_list->setEditTriggers(QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);
    m_list->setToolTip(tr("Double-click a colour to change it. Press F2 to rename it."));
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);

    QWidget* const palettesTitle = createPaletteHeader();
    m_paletteHeader = palettesTitle;
    m_palettes = new QListWidget(this);
    m_palettes->setObjectName(QStringLiteral("PaletteList"));
    m_palettes->setFixedHeight(64);
    m_palettes->setFocusPolicy(Qt::NoFocus);
    m_palettes->setToolTip(tr("Double-click a palette to rename it."));
    connect(m_palettes, &QListWidget::currentRowChanged, this, [this](int row) {
        QListWidgetItem* const item = m_palettes->item(row);
        if (!m_rebuilding && item && item->data(Qt::UserRole).toInt() >= 0)
            setCurrentPalette(item->data(Qt::UserRole).toInt());
    });
    connect(m_palettes, &QListWidget::itemDoubleClicked, this, &PalettePanel::renamePalette);
    m_palettes->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_palettes, &QListWidget::customContextMenuRequested, this, [this](QPoint const& pos) {
        if (QListWidgetItem* const item = m_palettes->itemAt(pos); item && item->data(Qt::UserRole).toInt() >= 0)
            m_palettes->setCurrentItem(item);
        emit menuWanted(QStringLiteral("menu.palettes"), m_palettes->viewport()->mapToGlobal(pos));
    });

    QWidget* const bar = createColourBar();

    QVBoxLayout* const layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(3);
    layout->addWidget(palettesTitle);
    layout->addWidget(m_palettes);
    layout->addWidget(bar);
    layout->addWidget(m_list, 1);
    if (!QSettings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse")).value(QStringLiteral("colour/paletteList"), true).toBool()) {
        m_palettes->hide();
        palettesTitle->hide();
    }

    connect(this, &PalettePanel::currentSwatchChanged, this, &PalettePanel::showInHand);
    connect(m_document, &Document::changed, this, &PalettePanel::showInHand);
    // The colour picked is that of the kind of tool in hand - or of all three, while they are linked.
    connect(this, &PalettePanel::currentSwatchChanged, this, [this](int id) {
        for (int kind = 0; kind < 3; ++kind)
            if (m_linked || kind == m_kind || m_colours[kind] < 0)
                m_colours[kind] = id;
        showKinds();
    });
    connect(m_list, &QListWidget::itemDoubleClicked, this, &PalettePanel::editColor);
    connect(m_list, &QListWidget::itemChanged, this, &PalettePanel::itemRenamed);
    connect(m_list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
        if (m_rebuilding || !item)
            return;
        m_current = item->data(Qt::UserRole).toInt();
        emit currentSwatchChanged(m_current);
    });
    // The right button on a colour: its place in the palette, and the palette it is in.
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_list, &QListWidget::customContextMenuRequested, this, [this](QPoint const& pos) {
        // The colour under the pointer comes into hand (with the others selected, when it is one of them).
        if (QListWidgetItem* const item = m_list->itemAt(pos); item && !item->isSelected())
            m_list->setCurrentItem(item);
        emit menuWanted(QStringLiteral("menu.colours"), m_list->viewport()->mapToGlobal(pos));
    });
    connect(m_document, &Document::changed, this, &PalettePanel::rebuild);
    rebuild();
}

// Over the colours: the palettes of the scene, with + and - to add and remove one.
QWidget* PalettePanel::createPaletteHeader()
{
    QWidget* const palettesTitle = new QWidget(this);
    QHBoxLayout* const row = new QHBoxLayout(palettesTitle);
    row->setContentsMargins(0, 0, 2, 0);
    row->setSpacing(2);
    row->addWidget(new QLabel(tr("Palettes"), palettesTitle));
    row->addStretch();
    QToolButton* const addPaletteButton = new QToolButton(palettesTitle);
    addPaletteButton->setObjectName(QStringLiteral("AddPalette"));
    addPaletteButton->setText(QStringLiteral("+"));
    addPaletteButton->setToolTip(tr("New palette"));
    addPaletteButton->setAutoRaise(true);
    QToolButton* const removePaletteButton = new QToolButton(palettesTitle);
    removePaletteButton->setObjectName(QStringLiteral("RemovePalette"));
    removePaletteButton->setText(QString(QChar(0x2212)));
    removePaletteButton->setToolTip(tr("Remove the selected palette"));
    removePaletteButton->setAutoRaise(true);
    QToolButton* const importButton = new QToolButton(palettesTitle);
    importButton->setObjectName(QStringLiteral("ImportPalette"));
    importButton->setText(QString(QChar(0x2193)));
    importButton->setToolTip(tr("Import a palette from a file"));
    importButton->setAutoRaise(true);
    QToolButton* const exportButton = new QToolButton(palettesTitle);
    exportButton->setObjectName(QStringLiteral("ExportPalette"));
    exportButton->setText(QString(QChar(0x2191)));
    exportButton->setToolTip(tr("Export the selected palette to a file"));
    exportButton->setAutoRaise(true);
    row->addWidget(addPaletteButton);
    row->addWidget(removePaletteButton);
    row->addWidget(importButton);
    row->addWidget(exportButton);
    connect(importButton, &QToolButton::clicked, this, [this] {
        QString const path = QFileDialog::getOpenFileName(this, tr("Import Palette"), QString(), tr("Eclipse palettes (*.eclpal)"));
        QString error;
        if (!path.isEmpty() && !importPalette(path, &error))
            QMessageBox::warning(this, tr("Import Palette"), error);
    });
    connect(exportButton, &QToolButton::clicked, this, [this] {
        QString const name = m_document->scene().paletteNames.value(m_palette, tr("Palette"));
        QString path = QFileDialog::getSaveFileName(this, tr("Export Palette"), name + QStringLiteral(".eclpal"), tr("Eclipse palettes (*.eclpal)"));
        if (path.isEmpty())
            return;
        if (QFileInfo(path).suffix().isEmpty())
            path += QStringLiteral(".eclpal");
        QString error;
        if (!exportPalette(path, &error))
            QMessageBox::warning(this, tr("Export Palette"), error);
    });
    connect(addPaletteButton, &QToolButton::clicked, this, &PalettePanel::addPalette);
    connect(removePaletteButton, &QToolButton::clicked, this, &PalettePanel::removePalette);
    return palettesTitle;
}

// Between the two lists: add and delete a colour at the left, the colour in hand at the right.
QWidget* PalettePanel::createColourBar()
{
    QWidget* const bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("ColourBar"));
    auto button = [bar](QString const& text, QString const& tip) {
        QToolButton* const b = new QToolButton(bar);
        b->setText(text);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        return b;
    };
    QToolButton* const add = button(QStringLiteral("+"), tr("Add a colour"));
    add->setObjectName(QStringLiteral("AddColour"));
    QToolButton* const remove = button(QString(QChar(0x2212)), tr("Delete the selected colour"));
    remove->setObjectName(QStringLiteral("RemoveColour"));
    m_inHand = new QLabel(bar);
    m_inHand->setObjectName(QStringLiteral("ColourInHand"));
    m_inHand->setFixedSize(26, 16);
    m_inHand->setToolTip(tr("The colour that is drawn and painted with"));
    QHBoxLayout* const buttons = new QHBoxLayout(bar);
    buttons->setContentsMargins(0, 0, 2, 0);
    buttons->setSpacing(2);
    buttons->addWidget(add);
    buttons->addWidget(remove);
    QToolButton* const grid = button(QString(QChar(0x25A6)), tr("Show the colours as a grid of patches"));
    grid->setObjectName(QStringLiteral("ColourGrid"));
    grid->setCheckable(true);
    buttons->addWidget(grid);
    connect(grid, &QToolButton::toggled, this, &PalettePanel::setSwatchGrid);
    connect(grid, &QToolButton::clicked, this, [](bool on) { QSettings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse")).setValue(QStringLiteral("colour/grid"), on); });
    grid->setChecked(QSettings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse")).value(QStringLiteral("colour/grid"), false).toBool());
    buttons->addStretch();
    // The three colours in hand, and whether they are kept the same.
    {
        char const* const names[3] = {"ColourBrush", "ColourPencil", "ColourPaint"};
        QString const tips[3] = {tr("The colour the brush draws with"), tr("The colour the pencil and the shape tools draw with"), tr("The colour the paint bucket paints with")};
        for (int kind = 0; kind < 3; ++kind) {
            QToolButton* const b = new QToolButton(bar);
            b->setObjectName(QLatin1String(names[kind]));
            b->setToolTip(tips[kind]);
            b->setFixedSize(18, 16);
            connect(b, &QToolButton::clicked, this, [this, kind] { setToolKind(kind); });
            buttons->addWidget(b);
            m_kindButtons[kind] = b;
        }
        QToolButton* const link = button(QString(QChar(0x221E)), tr("Keep the three colours the same"));
        link->setObjectName(QStringLiteral("ColourLink"));
        link->setCheckable(true);
        link->setChecked(true);
        connect(link, &QToolButton::toggled, this, &PalettePanel::setColoursLinked);
        buttons->addWidget(link);
    }
    buttons->addWidget(m_inHand);
    connect(add, &QToolButton::clicked, this, &PalettePanel::addSwatch);
    connect(remove, &QToolButton::clicked, this, &PalettePanel::removeSwatch);
    return bar;
}

// The patch that shows the colour drawn and painted with.
void PalettePanel::showInHand()
{
    Swatch const* const swatch = m_document->scene().findSwatch(m_current);
    QString paint = swatch ? swatch->color.name() : QStringLiteral("transparent");
    if (swatch && swatch->isGradient()) {
        // Along the patch, or out from its middle, as the gradient is.
        paint = swatch->kind == Swatch::Radial ? QStringLiteral("qradialgradient(cx:0.5, cy:0.5, radius:0.6, fx:0.5, fy:0.5")
                                               : QStringLiteral("qlineargradient(x1:0, y1:0, x2:1, y2:0");
        for (GradientStop const& stop : swatch->stops)
            paint += QStringLiteral(", stop:%1 %2").arg(stop.at, 0, 'f', 4).arg(stop.color.name());
        paint += QLatin1Char(')');
    }
    m_inHand->setStyleSheet(QStringLiteral("background: %1; border: 1px solid #101010;").arg(paint));
}

void PalettePanel::rebuild()
{
    Scene const& scene = m_document->scene();
    m_rebuilding = true;
    // The palettes: the scene's under a heading, then those of the layer worked on (its element palettes) under another.
    m_palette = std::clamp(m_palette, 0, int(scene.paletteNames.size()) - 1);
    m_palettes->clear();
    int const owner = m_layer >= 0 && m_layer < scene.layers.size() ? scene.layers[m_layer].id : -1;
    auto headingRow = [&](QString const& text) {
        QListWidgetItem* const heading = new QListWidgetItem(text, m_palettes);
        heading->setFlags(Qt::NoItemFlags);
        heading->setData(Qt::UserRole, -1);
        QFont bold = heading->font();
        bold.setBold(true);
        heading->setFont(bold);
        heading->setForeground(QColor(200, 200, 200));
    };
    int currentRow = -1;
    auto paletteRow = [&](int i) {
        QListWidgetItem* const item = new QListWidgetItem(QStringLiteral("    ") + scene.paletteNames[i], m_palettes);
        item->setData(Qt::UserRole, i);
        if (!scene.paletteLink(i).isEmpty()) {
            // A palette shared through a file is told apart by its slanted name.
            QFont slanted = item->font();
            slanted.setItalic(true);
            item->setFont(slanted);
            item->setToolTip(tr("Linked to %1").arg(QDir::toNativeSeparators(scene.paletteLink(i))));
        }
        bool clones = false;
        for (Swatch const& swatch : scene.palette)
            clones = clones || (swatch.palette == i && swatch.cloneOf != 0);
        if (clones)
            item->setToolTip(item->toolTip().isEmpty() ? tr("A palette of clones") : item->toolTip() + QLatin1Char('\n') + tr("A palette of clones"));
        if (i == m_palette)
            currentRow = m_palettes->count() - 1;
    };
    headingRow(tr("Scene Palettes"));
    for (int i = 0; i < scene.paletteNames.size(); ++i)
        if (scene.paletteIsScene(i))
            paletteRow(i);
    bool element = false;
    for (int i = 0; i < scene.paletteNames.size(); ++i)
        element = element || (!scene.paletteIsScene(i) && (scene.paletteOwner(i) == owner || i == m_palette));
    if (element) {
        headingRow(tr("Element Palettes"));
        for (int i = 0; i < scene.paletteNames.size(); ++i)
            if (!scene.paletteIsScene(i) && (scene.paletteOwner(i) == owner || i == m_palette))
                paletteRow(i);
    }
    m_palettes->setCurrentRow(currentRow);
    // The colours of the one chosen.
    m_list->clear();
    bool currentHere = false;
    for (Swatch const& swatch : scene.palette)
        currentHere = currentHere || (swatch.id == m_current && swatch.palette == m_palette);
    for (Swatch const& swatch : scene.palette) {
        if (swatch.palette != m_palette)
            continue;
        if (!currentHere) {
            m_current = swatch.id;      // the colour in hand is of another palette, or gone: the first of this one
            currentHere = true;
        }
        QString text = m_grid ? QString() : swatch.name;
        if (m_values && !m_grid)
            text += QStringLiteral("    %1, %2, %3, %4").arg(swatch.color.red()).arg(swatch.color.green()).arg(swatch.color.blue()).arg(swatch.color.alpha());
        QListWidgetItem* const item = new QListWidgetItem(swatchIcon(swatch), text, m_list);
        item->setData(Qt::UserRole, swatch.id);
        if (m_grid)
            item->setToolTip(swatch.name);
        else if (!m_values)
            item->setFlags(item->flags() | Qt::ItemIsEditable);
        if (swatch.cloneOf != 0) {
            Swatch const* const original = scene.findSwatch(swatch.cloneOf);
            item->setToolTip(tr("%1 - a clone of %2").arg(swatch.name, original ? original->name : tr("a colour no longer there")));
        }
        if (swatch.isProtected) {
            // A protected colour is told apart by its slanted name.
            QFont slanted = item->font();
            slanted.setItalic(true);
            item->setFont(slanted);
            item->setToolTip(tr("%1 - protected: the paint tools leave it as it is").arg(swatch.name));
        }
    }
    // Keep the same swatch current; if it is gone, fall back to the first.
    if (!scene.findSwatch(m_current))
        m_current = scene.palette.isEmpty() ? -1 : scene.palette.first().id;
    for (int i = 0; i < m_list->count(); ++i)
        if (m_list->item(i)->data(Qt::UserRole).toInt() == m_current)
            m_list->setCurrentRow(i);
    m_rebuilding = false;
    emit currentSwatchChanged(m_current);
}

void PalettePanel::showKinds()
{
    for (int kind = 0; kind < 3; ++kind) {
        if (!m_kindButtons[kind])
            continue;
        Swatch const* const swatch = m_document->scene().findSwatch(m_colours[kind]);
        m_kindButtons[kind]->setStyleSheet(QStringLiteral("QToolButton { background-color: %1; border: %2; }")
                                               .arg(swatch ? swatch->color.name() : QStringLiteral("transparent"),
                                                    kind == m_kind ? QStringLiteral("2px solid #e8e8e8") : QStringLiteral("1px solid #101010")));
    }
}

void PalettePanel::setToolKind(int kind)
{
    if (kind < 0 || kind > 2 || kind == m_kind)
        return;
    m_kind = kind;
    // Unlinked, each kind has its own: the one of this kind comes into hand.
    if (!m_linked && m_colours[kind] != m_current && m_document->scene().findSwatch(m_colours[kind]))
        setCurrentSwatch(m_colours[kind]);
    showKinds();
}

void PalettePanel::setColoursLinked(bool on)
{
    m_linked = on;
    if (on)
        for (int& colour : m_colours)
            colour = m_current;
    showKinds();
}

void PalettePanel::setSwatchGrid(bool on)
{
    if (on == m_grid)
        return;
    m_grid = on;
    m_list->setViewMode(on ? QListView::IconMode : QListView::ListMode);
    m_list->setMovement(QListView::Static);
    m_list->setResizeMode(QListView::Adjust);
    m_list->setWrapping(on);
    m_list->setGridSize(on ? QSize(42, 22) : QSize());
    rebuild();
}

void PalettePanel::setCurrentSwatch(int id)
{
    // A colour of another palette: that palette is shown first.
    if (Swatch const* const swatch = m_document->scene().findSwatch(id); swatch && swatch->palette != m_palette) {
        m_palette = swatch->palette;
        m_current = id;
        rebuild();
        return;
    }
    for (int i = 0; i < m_list->count(); ++i)
        if (m_list->item(i)->data(Qt::UserRole).toInt() == id)
            m_list->setCurrentRow(i);
}
