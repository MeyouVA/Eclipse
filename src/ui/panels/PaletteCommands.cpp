// Eclipse - the Colour view: the commands of its Colours menu - copy, paste, new, edit, delete, textures, protection.
#include "ui/panels/PalettePanel.h"

#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QFileInfo>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QToolButton>

#include "actions/theme/AppSettings.h"
#include "model/document/Commands.h"

namespace {

// What Copy put aside: shared by every Colour view, lost when the program closes.
QVector<Swatch> s_copied;

QString picturesFilter()
{
    return QObject::tr("Pictures (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.gif)");
}

}  // namespace

QVector<Swatch> PalettePanel::copiedColours()
{
    return s_copied;
}

QVector<int> PalettePanel::selectedSwatches() const
{
    QVector<int> ids;
    if (m_document->scene().findSwatch(m_current))
        ids.push_back(m_current);
    for (QListWidgetItem* const item : m_list->selectedItems()) {
        int const id = item->data(Qt::UserRole).toInt();
        if (!ids.contains(id))
            ids.push_back(id);
    }
    // In the order of the palette.
    QVector<int> ordered;
    for (Swatch const& s : m_document->scene().palette)
        if (ids.contains(s.id))
            ordered.push_back(s.id);
    return ordered;
}

void PalettePanel::copy()
{
    QVector<Swatch> colours;
    for (int id : selectedSwatches())
        if (Swatch const* const s = m_document->scene().findSwatch(id))
            colours.push_back(*s);
    if (colours.isEmpty())
        return;
    s_copied = colours;
    // As text too, for elsewhere: a colour a line, as #AARRGGBB and its name.
    QStringList lines;
    for (Swatch const& s : std::as_const(colours))
        lines << s.color.name(QColor::HexArgb) + QLatin1Char(' ') + s.name;
    QApplication::clipboard()->setText(lines.join(QLatin1Char('\n')));
}

void PalettePanel::cut()
{
    QVector<int> const ids = selectedSwatches();
    if (ids.isEmpty())
        return;
    copy();
    deleteColours(ids, tr("Cut Colours"));
}

void PalettePanel::paste()
{
    onActionPasteAsNewColours();
}

void PalettePanel::deleteSelection()
{
    QVector<int> const ids = selectedSwatches();
    if (ids.isEmpty())
        return;
    if (m_document->scene().paletteSwatches(m_palette).size() <= ids.size()) {
        QMessageBox::information(this, tr("Delete Colour"), tr("A palette keeps one colour at least."));
        return;
    }
    deleteColours(ids, tr("Delete Colour"));
}

void PalettePanel::onActionCopyColourId()
{
    QStringList ids;
    for (int id : selectedSwatches())
        ids << QString::number(id);
    if (!ids.isEmpty())
        QApplication::clipboard()->setText(ids.join(QLatin1Char('\n')));
}

void PalettePanel::onActionPasteColourValues()
{
    if (!s_copied.isEmpty())
        m_document->setColourValues(selectedSwatches(), s_copied);
}

void PalettePanel::onActionPasteAsNewColours()
{
    QVector<int> const made = m_document->addColours(s_copied, m_palette, false);
    if (!made.isEmpty())
        setCurrentSwatch(made.last());
}

void PalettePanel::onActionPasteAsClones()
{
    QVector<int> const made = m_document->addColours(s_copied, m_palette, true);
    if (!made.isEmpty())
        setCurrentSwatch(made.last());
}

void PalettePanel::onActionNewColour()
{
    // A new colour starts as the colour in hand (or as the default colour, by Preferences).
    Swatch const* const now = m_document->scene().findSwatch(m_current);
    bool const byDefault = settings().value(QStringLiteral("colour/newUsesDefault"), false).toBool();
    if (byDefault || !now) {
        onActionNewDefaultColour();
        return;
    }
    Swatch copy = *now;
    copy.name = tr("New");
    copy.isProtected = false;
    QVector<int> const made = m_document->addColours({copy}, m_palette, false);
    if (!made.isEmpty())
        setCurrentSwatch(made.last());
}

void PalettePanel::onActionNewDefaultColour()
{
    QColor const colour = settings().value(QStringLiteral("colour/defaultNew"), QColor(Qt::black)).value<QColor>();
    int const id = m_document->addSwatch(tr("New"), colour.isValid() ? colour : QColor(Qt::black), m_palette);
    setCurrentSwatch(id);
}

void PalettePanel::onActionEditColour()
{
    if (QListWidgetItem* const item = m_list->currentItem())
        editColor(item);
}

void PalettePanel::onActionNewTexture()
{
    QString const file = QFileDialog::getOpenFileName(this, tr("New Texture"), QString(), picturesFilter());
    if (file.isEmpty())
        return;
    QImage const picture(file);
    if (picture.isNull()) {
        QMessageBox::warning(this, tr("New Texture"), tr("%1 could not be read as a picture.").arg(QFileInfo(file).fileName()));
        return;
    }
    addTextureSwatch(picture, QFileInfo(file).completeBaseName());
}

void PalettePanel::onActionEditTexture()
{
    Swatch const* const swatch = m_document->scene().findSwatch(m_current);
    if (!swatch || !swatch->isTexture())
        return;
    int const id = m_current;
    QString const file = QFileDialog::getOpenFileName(this, tr("Edit Texture"), QString(), picturesFilter());
    QImage const picture = file.isEmpty() ? QImage() : QImage(file);
    if (!picture.isNull())
        m_document->setSwatchTexture(id, picture);
}

void PalettePanel::onActionScaleDownTexture()
{
    Swatch const* const swatch = m_document->scene().findSwatch(m_current);
    if (!swatch || !swatch->isTexture() || std::min(swatch->texture.width(), swatch->texture.height()) < 2)
        return;
    QImage const half = swatch->texture.scaled(swatch->texture.size() / 2, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    m_document->setSwatchTexture(m_current, half);
}

void PalettePanel::onActionProtectColour()
{
    QVector<int> const ids = selectedSwatches();
    if (ids.isEmpty())
        return;
    // All protected when any was not; else none.
    bool any = false;
    for (int id : ids)
        if (Swatch const* const s = m_document->scene().findSwatch(id))
            any = any || !s->isProtected;
    m_document->beginMacro(any ? tr("Protect Colour") : tr("Unprotect Colour"));
    for (int id : ids)
        m_document->setSwatchProtected(id, any);
    m_document->endMacro();
}

void PalettePanel::onActionSwatchMode(bool on)
{
    setSwatchGrid(on);
    if (QToolButton* const grid = findChild<QToolButton*>(QStringLiteral("ColourGrid")))
        grid->setChecked(on);
    settings().setValue(QStringLiteral("colour/grid"), on);
}

void PalettePanel::onActionDisplayColourValues(bool on)
{
    if (on == m_values)
        return;
    m_values = on;
    rebuild();
}

void PalettePanel::onActionRecoverColours()
{
    int const count = m_document->recoverColours();
    QMessageBox::information(this, tr("Recover Colours"), count == 0 ? tr("Every colour the drawings use is in a palette.")
                                                                     : tr("%n colour(s) recovered into the palette \"Recovered Palette\".", nullptr, count));
}

void PalettePanel::onShowColoursMenu(QMenu* menu)
{
    Swatch const* const swatch = m_document->scene().findSwatch(m_current);
    for (QAction* const a : menu->actions()) {
        QString const id = a->objectName();
        if (id == QLatin1String("SwatchMode"))
            a->setChecked(m_grid);
        else if (id == QLatin1String("DisplayColourValues"))
            a->setChecked(m_values);
        else if (id == QLatin1String("ProtectColour"))
            a->setChecked(swatch && swatch->isProtected);
        else if (id == QLatin1String("EditTexture") || id == QLatin1String("ScaleDownTexture"))
            a->setEnabled(swatch && swatch->isTexture());
        else if (id == QLatin1String("PasteColourValues") || id == QLatin1String("PasteAsNewColours") || id == QLatin1String("PasteAsClones"))
            a->setEnabled(!s_copied.isEmpty());
        else if (id == QLatin1String("ShowPaletteList"))
            a->setChecked(showsPaletteList());
        else if (id == QLatin1String("UpdatePaletteFromFile") || id == QLatin1String("UnlinkPalette"))
            a->setEnabled(!m_document->scene().paletteLink(m_palette).isEmpty());
        else if (id == QLatin1String("RemovePalette") || id == QLatin1String("RemoveAndDeletePalette") || id == QLatin1String("MergePalettes"))
            a->setEnabled(m_document->scene().paletteNames.size() > 1);
        else if (id == QLatin1String("EditGradientColour"))
            a->setEnabled(swatch != nullptr);
    }
}

void PalettePanel::onActionNewGradient()
{
    editGradient(addGradientSwatch());
}

void PalettePanel::onActionEditGradient()
{
    editGradient(m_current);
}

void PalettePanel::onActionMoveColourUp()
{
    moveCurrentSwatch(-1);
}

void PalettePanel::onActionMoveColourDown()
{
    moveCurrentSwatch(+1);
}

void PalettePanel::onShowMoveToPalette(QMenu* menu)
{
    menu->clear();
    QStringList const names = m_document->scene().paletteNames;
    for (int palette = 0; palette < names.size(); ++palette) {
        QAction* const a = menu->addAction(names[palette]);
        a->setEnabled(palette != m_palette);
        connect(a, &QAction::triggered, this, [this, palette] { moveCurrentSwatchTo(palette); });
    }
}
