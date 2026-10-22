// Eclipse - the Colour view: the palettes - chosen, added, renamed, removed, written to a file and read from one.
#include "ui/panels/PalettePanel.h"
#include "ui/panels/GradientBar.h"

#include <algorithm>
#include <cmath>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QInputDialog>
#include <QFileDialog>
#include <QMenu>
#include <QMessageBox>

#include "model/document/Commands.h"
#include <QListWidget>

bool PalettePanel::moveCurrentSwatch(int by)
{
    return m_document->moveSwatch(m_current, by);
}

bool PalettePanel::moveCurrentSwatchTo(int palette)
{
    int const id = m_current;
    if (!m_document->setSwatchPalette(id, palette))
        return false;
    m_palette = palette;
    m_current = id;
    rebuild();
    return true;
}

void PalettePanel::setCurrentPalette(int palette)
{
    if (palette == m_palette || palette < 0 || palette >= m_document->scene().paletteNames.size())
        return;
    m_palette = palette;
    rebuild();
}

bool PalettePanel::exportPalette(QString const& path, QString* error) const
{
    QJsonObject const root = m_document->scene().paletteJson(m_palette);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(root).toJson()) < 0) {
        if (error)
            *error = tr("The palette could not be written to %1.").arg(path);
        return false;
    }
    return true;
}

bool PalettePanel::importPalette(QString const& path, QString* error)
{
    QFile file(path);
    QJsonObject const root = file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
    QJsonArray const colours = root[QStringLiteral("colours")].toArray();
    if (!root.contains(QStringLiteral("eclipsePalette")) || colours.isEmpty()) {
        if (error)
            *error = tr("%1 is not an Eclipse palette file.").arg(path);
        return false;
    }
    QString name = root[QStringLiteral("name")].toString().trimmed();
    if (name.isEmpty())
        name = QFileInfo(path).completeBaseName();
    // One step to undo: the palette and its colours.
    m_document->beginMacro(tr("Import Palette"));
    int const palette = m_document->addPalette(name);
    // A new palette comes with a black of its own: the file's colours take its place.
    int given = Scene::kNoSwatch;
    for (Swatch const& swatch : m_document->scene().palette)
        if (swatch.palette == palette)
            given = swatch.id;
    for (QJsonValue const& value : colours) {
        QJsonObject const colour = value.toObject();
        QColor const c(colour[QStringLiteral("color")].toString());
        int const made = m_document->addSwatch(colour[QStringLiteral("name")].toString(), c.isValid() ? c : QColor(Qt::gray), palette);
        Swatch read;
        read.setStopsJson(colour[QStringLiteral("stops")].toArray());
        if (read.stops.size() >= 2)
            m_document->setSwatchGradient(made, Swatch::Kind(std::clamp(colour[QStringLiteral("kind")].toInt(1), 1, 2)), read.stops);
        if (colour[QStringLiteral("protected")].toBool(false))
            m_document->setSwatchProtected(made, true);
        read.setTextureText(colour[QStringLiteral("texture")].toString());
        if (read.isTexture())
            m_document->setSwatchTexture(made, read.texture);
    }
    if (given != Scene::kNoSwatch)
        m_document->removeSwatch(given);
    m_document->endMacro();
    m_palette = palette;
    rebuild();
    return true;
}

bool PalettePanel::linkPalette(QString const& path, QString* error)
{
    // The file gets the palette as it is now, unless it is there already: then the palette takes what the file has.
    if (QFileInfo::exists(path)) {
        Scene tried = m_document->scene();
        if (!tried.readPaletteFile(m_palette, path)) {
            if (error)
                *error = tr("%1 is not an Eclipse palette file.").arg(path);
            return false;
        }
    } else if (!m_document->scene().writePaletteFile(m_palette, path)) {
        if (error)
            *error = tr("The palette could not be written to %1.").arg(path);
        return false;
    }
    m_document->beginMacro(tr("Link Palette"));
    m_document->setPaletteLink(m_palette, path);
    m_document->updatePaletteFromFile(m_palette);
    m_document->endMacro();
    return true;
}

void PalettePanel::unlinkPalette()
{
    m_document->setPaletteLink(m_palette, QString());
}

int PalettePanel::duplicatePalette()
{
    Scene const& scene = m_document->scene();
    if (m_palette < 0 || m_palette >= scene.paletteNames.size())
        return -1;
    QVector<Swatch> colours;
    for (Swatch const& swatch : scene.palette)
        if (swatch.palette == m_palette)
            colours.push_back(swatch);
    // One step to undo: the palette and its colours.
    m_document->beginMacro(tr("Duplicate Palette"));
    int const made = m_document->addPalette(tr("%1 copy").arg(scene.paletteNames[m_palette]));
    int given = Scene::kNoSwatch;       // a new palette comes with a black of its own: the copies take its place
    for (Swatch const& swatch : m_document->scene().palette)
        if (swatch.palette == made)
            given = swatch.id;
    for (Swatch const& from : std::as_const(colours)) {
        int const id = m_document->addSwatch(from.name, from.color, made);
        if (from.isGradient())
            m_document->setSwatchGradient(id, from.kind, from.stops);
        if (from.isProtected)
            m_document->setSwatchProtected(id, true);
    }
    if (given != Scene::kNoSwatch && !colours.isEmpty())
        m_document->removeSwatch(given);
    m_document->endMacro();
    m_palette = made;
    rebuild();
    return made;
}

void PalettePanel::addPalette()
{
    bool ok = false;
    QString const name = QInputDialog::getText(this, tr("New Palette"), tr("Name:"), QLineEdit::Normal, tr("Palette"), &ok);
    if (!ok)
        return;
    m_palette = m_document->addPalette(name);
    rebuild();
}

void PalettePanel::removePalette()
{
    m_document->removePalette(m_palette);
}

void PalettePanel::renamePalette(QListWidgetItem* item)
{
    int const palette = item ? item->data(Qt::UserRole).toInt() : -1;
    if (palette < 0 || palette >= m_document->scene().paletteNames.size())
        return;
    bool ok = false;
    QString const name = QInputDialog::getText(this, tr("Rename Palette"), tr("Name:"), QLineEdit::Normal, m_document->scene().paletteNames[palette], &ok);
    if (ok)
        m_document->renamePalette(palette, name);
}
