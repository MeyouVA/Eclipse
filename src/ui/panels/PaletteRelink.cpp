// Eclipse - the Colour view: linked palettes whose files went missing, and a palette copied with new colour ids for the selected drawings.
#include "ui/panels/PalettePanel.h"

#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>

#include "model/document/Commands.h"

QVector<int> PalettePanel::missingPalettes() const
{
    Scene const& scene = m_document->scene();
    QVector<int> missing;
    for (int which = 0; which < scene.paletteNames.size(); ++which)
        if (!scene.paletteLink(which).isEmpty() && !QFileInfo::exists(scene.paletteLink(which)))
            missing.push_back(which);
    return missing;
}

int PalettePanel::linkMissingPalettes(QString const& folder)
{
    // Each missing file looked for by its name in the folder and those inside it; those found are linked again, in one step.
    QHash<QString, QString> found;
    QDirIterator walk(folder, {QStringLiteral("*.eclpal")}, QDir::Files, QDirIterator::Subdirectories);
    while (walk.hasNext()) {
        walk.next();
        found.insert(walk.fileName().toLower(), walk.filePath());
    }
    QVector<QPair<int, QString>> relinks;
    for (int which : missingPalettes()) {
        QString const now = found.value(QFileInfo(m_document->scene().paletteLink(which)).fileName().toLower());
        if (!now.isEmpty())
            relinks.push_back({which, now});
    }
    if (relinks.isEmpty())
        return 0;
    m_document->beginMacro(tr("Link Missing Palettes"));
    for (auto const& [which, now] : std::as_const(relinks)) {
        m_document->setPaletteLink(which, now);
        m_document->updatePaletteFromFile(which);
    }
    m_document->endMacro();
    return int(relinks.size());
}

void PalettePanel::onActionLinkMissingPalettes()
{
    QVector<int> const missing = missingPalettes();
    if (missing.isEmpty()) {
        QMessageBox::information(this, tr("Link Missing Palettes"), tr("Every linked palette has its file."));
        return;
    }
    QString const folder = QFileDialog::getExistingDirectory(this, tr("Link Missing Palettes: the folder to look in"));
    if (folder.isEmpty())
        return;
    int const linked = linkMissingPalettes(folder);
    QStringList still;
    for (int which : missingPalettes())
        still << m_document->scene().paletteNames.value(which);
    if (!still.isEmpty())
        QMessageBox::information(this, tr("Link Missing Palettes"),
                                 tr("%n palette(s) linked again. Not found:\n%1", nullptr, linked).arg(still.join(QLatin1Char('\n'))));
    rebuild();
}

int PalettePanel::duplicateAndRecolour()
{
    // The palette copied (each colour with an id of its own), and the drawings of the selected layers moved onto the copies.
    Scene const& scene = m_document->scene();
    int const from = m_palette;
    QVector<int> const old = scene.paletteSwatches(from);
    QVector<int> const layers = layersInScope(1);
    m_document->beginMacro(tr("Generate New Colour IDs and Recolour Selected Drawings"));
    int const made = duplicatePalette();
    if (made >= 0) {
        QVector<int> const copies = m_document->scene().paletteSwatches(made);
        for (int i = 0; i < old.size() && i < copies.size(); ++i)
            m_document->recolour(old[i], copies[i], layers);
    }
    m_document->endMacro();
    return made;
}

void PalettePanel::onActionDuplicateAndRecolour()
{
    int const made = duplicateAndRecolour();
    if (made >= 0) {
        m_palette = made;
        rebuild();
    }
}
