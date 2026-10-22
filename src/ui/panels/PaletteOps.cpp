// Eclipse - the Colour view: the commands of its Palettes menu - new, link, remove, clone, merge, order, overrides.
#include "ui/panels/PalettePanel.h"

#include <QDialog>
#include <QDir>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

#include "actions/theme/AppSettings.h"
#include "model/document/Commands.h"

QVector<int> PalettePanel::layersInScope(int scope) const
{
    Scene const& scene = m_document->scene();
    QVector<int> layers;
    if (scope == 2)
        return layers;      // all of them
    if (scope == 1 && m_selectedLayers)
        layers = m_selectedLayers();
    if (layers.isEmpty() && m_layer >= 0 && m_layer < scene.layers.size())
        layers.push_back(m_layer);
    return layers;
}

int PalettePanel::pickPalette(QString const& title, QString const& label, bool excludeCurrent)
{
    Scene const& scene = m_document->scene();
    QStringList names;
    QVector<int> places;
    for (int i = 0; i < scene.paletteNames.size(); ++i) {
        if (excludeCurrent && i == m_palette)
            continue;
        names << scene.paletteNames[i];
        places << i;
    }
    if (names.isEmpty())
        return -1;
    bool ok = false;
    QString const chosen = QInputDialog::getItem(this, title, label, names, 0, false, &ok);
    return ok ? places.value(names.indexOf(chosen), -1) : -1;
}

void PalettePanel::onActionNewPalette()
{
    addPalette();
}

void PalettePanel::onActionNewElementPalette()
{
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size() || !scene.layers[m_layer].holdsDrawings()) {
        QMessageBox::information(this, tr("Create Element Palette"), tr("Choose a drawing layer first: an element palette belongs to a layer."));
        return;
    }
    bool ok = false;
    QString const name = QInputDialog::getText(this, tr("Create Element Palette"), tr("New Palette Name:"), QLineEdit::Normal, scene.layers[m_layer].name, &ok);
    if (!ok)
        return;
    m_palette = m_document->addPalette(name, scene.layers[m_layer].id);
    rebuild();
}

void PalettePanel::onActionNewPaletteFromSelected()
{
    QVector<int> const layers = layersInScope(1);
    if (layers.isEmpty())
        return;
    bool ok = false;
    QString const name = QInputDialog::getText(this, tr("New Palette from Selected Drawings"), tr("New Palette Name:"), QLineEdit::Normal,
                                               m_document->scene().layers[layers.first()].name, &ok);
    if (!ok)
        return;
    int const made = m_document->paletteFromDrawings(layers, name);
    if (made < 0) {
        QMessageBox::information(this, tr("New Palette from Selected Drawings"), tr("Cannot create Colour palette. There are no coloured vector strokes in the selected drawings."));
        return;
    }
    m_palette = made;
    rebuild();
}

void PalettePanel::onActionLinkPalette()
{
    QString path = QFileDialog::getSaveFileName(this, tr("Link Palette"), m_document->scene().paletteNames.value(m_palette) + QStringLiteral(".eclpal"),
                                                tr("Eclipse palettes (*.eclpal)"), nullptr, QFileDialog::DontConfirmOverwrite);
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().isEmpty())
        path += QStringLiteral(".eclpal");
    QString error;
    if (!linkPalette(path, &error))
        QMessageBox::warning(this, tr("Link Palette"), error);
}

void PalettePanel::onActionImportPalette()
{
    QString const path = QFileDialog::getOpenFileName(this, tr("Import Palette"), QString(), tr("Eclipse palettes (*.eclpal)"));
    QString error;
    if (!path.isEmpty() && !importPalette(path, &error))
        QMessageBox::warning(this, tr("Import Palette"), error);
}

void PalettePanel::onActionExportPalette()
{
    QString const name = m_document->scene().paletteNames.value(m_palette, tr("Palette"));
    QString path = QFileDialog::getSaveFileName(this, tr("Export Palette"), name + QStringLiteral(".eclpal"), tr("Eclipse palettes (*.eclpal)"));
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().isEmpty())
        path += QStringLiteral(".eclpal");
    QString error;
    if (!exportPalette(path, &error))
        QMessageBox::warning(this, tr("Export Palette"), error);
}

void PalettePanel::onActionRemovePalette()
{
    Scene const& scene = m_document->scene();
    if (scene.paletteNames.size() < 2)
        return;
    QSet<int> const used = scene.usedSwatches();
    bool inUse = false;
    for (int id : scene.paletteSwatches(m_palette))
        inUse = inUse || used.contains(id);
    if (inUse && QMessageBox::question(this, tr("Remove Palette"), tr("Some colours are used by drawings in the scene. Do you really want to remove this palette?\n"
                                                                    "What is drawn in them keeps their colours."))
                     != QMessageBox::Yes)
        return;
    removePalette();
}

void PalettePanel::onActionRemoveAndDeletePalette()
{
    QString const file = m_document->scene().paletteLink(m_palette);
    if (file.isEmpty()) {
        onActionRemovePalette();
        return;
    }
    if (QMessageBox::question(this, tr("Remove and Delete Palette"),
                              tr("Deleting a palette will remove it from the scene and delete its file:\n%1\nDo you really want to delete this palette and its disk file?")
                                  .arg(QDir::toNativeSeparators(file)))
        != QMessageBox::Yes)
        return;
    int const count = int(m_document->scene().paletteNames.size());
    removePalette();
    if (m_document->scene().paletteNames.size() < count)
        QFile::remove(file);
}

void PalettePanel::onActionRenamePalette()
{
    for (int row = 0; row < m_palettes->count(); ++row)
        if (m_palettes->item(row)->data(Qt::UserRole).toInt() == m_palette)
            renamePalette(m_palettes->item(row));
}

void PalettePanel::onActionDuplicatePalette()
{
    duplicatePalette();
}

void PalettePanel::onActionClonePalette()
{
    Scene const& scene = m_document->scene();
    bool ok = false;
    QString const name = QInputDialog::getText(this, tr("Clone Palette"), tr("New Palette Name:"), QLineEdit::Normal,
                                               tr("%1 Clone").arg(scene.paletteNames.value(m_palette)), &ok);
    if (!ok)
        return;
    int const made = m_document->clonePalette(m_palette, name);
    if (made >= 0) {
        m_palette = made;
        rebuild();
    }
}

void PalettePanel::onActionMergePalettes()
{
    int const into = pickPalette(tr("Merge Palettes"), tr("Merge \"%1\" into:").arg(m_document->scene().paletteNames.value(m_palette)), true);
    if (into < 0)
        return;
    int const from = m_palette;
    if (m_document->mergePalette(from, into)) {
        m_palette = into > from ? into - 1 : into;
        rebuild();
    }
}

void PalettePanel::onActionMovePaletteUp()
{
    if (m_document->movePalette(m_palette, -1)) {
        --m_palette;
        rebuild();
    }
}

void PalettePanel::onActionMovePaletteDown()
{
    if (m_document->movePalette(m_palette, +1)) {
        ++m_palette;
        rebuild();
    }
}

bool PalettePanel::showsPaletteList() const
{
    return !m_palettes->isHidden();
}

void PalettePanel::onActionShowPaletteList(bool on)
{
    m_palettes->setVisible(on);
    if (m_paletteHeader)
        m_paletteHeader->setVisible(on);
    settings().setValue(QStringLiteral("colour/paletteList"), on);
}

void PalettePanel::setCurrentLayer(int layer)
{
    if (layer == m_layer)
        return;
    m_layer = layer;
    rebuild();
}

void PalettePanel::onActionUpdatePaletteFromFile()
{
    if (!m_document->updatePaletteFromFile(m_palette))
        QMessageBox::warning(this, tr("Update Palette"), tr("Failed to update palettes from %1").arg(QDir::toNativeSeparators(m_document->scene().paletteLink(m_palette))));
}

void PalettePanel::onActionUnlinkPalette()
{
    unlinkPalette();
}

void PalettePanel::onActionColourEditor()
{
    emit colourEditorWanted();
}
