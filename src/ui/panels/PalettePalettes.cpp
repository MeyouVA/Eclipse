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
    Scene const& scene = m_document->scene();
    QJsonArray colours;
    for (Swatch const& swatch : scene.palette) {
        if (swatch.palette != m_palette)
            continue;
        QJsonObject colour;
        colour[QStringLiteral("name")] = swatch.name;
        colour[QStringLiteral("color")] = swatch.color.name(QColor::HexArgb);
        if (swatch.isGradient()) {
            colour[QStringLiteral("kind")] = int(swatch.kind);
            colour[QStringLiteral("stops")] = swatch.stopsJson();
        }
        if (swatch.isProtected)
            colour[QStringLiteral("protected")] = true;
        colours.push_back(colour);
    }
    QJsonObject root;
    root[QStringLiteral("eclipsePalette")] = 1;
    root[QStringLiteral("name")] = scene.paletteNames.value(m_palette);
    root[QStringLiteral("colours")] = colours;
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
    }
    if (given != Scene::kNoSwatch)
        m_document->removeSwatch(given);
    m_document->endMacro();
    m_palette = palette;
    rebuild();
    return true;
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
    int const palette = m_palettes->row(item) - 1;
    if (palette < 0 || palette >= m_document->scene().paletteNames.size())
        return;
    bool ok = false;
    QString const name = QInputDialog::getText(this, tr("Rename Palette"), tr("Name:"), QLineEdit::Normal, m_document->scene().paletteNames[palette], &ok);
    if (ok)
        m_document->renamePalette(palette, name);
}
