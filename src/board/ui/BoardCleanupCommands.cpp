#include "board/ui/BoardWindow.h"
#include "board/ui/BoardPlayback.h"
#include "board/model/BoardRecoveredAssets.h"

#include <QDir>
#include <QFileInfo>
#include <QInputDialog>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>

namespace {
QString describeFiles(QStringList const& files, QString const& root, qint64* bytes = nullptr)
{
    int drawings = 0;
    int pictures = 0;
    int sounds = 0;
    qint64 total = 0;
    for (auto const& file : files) {
        if (file.startsWith(QLatin1String("elements/drawings/")))
            ++drawings;
        else if (file.startsWith(QLatin1String("elements/pictures/")))
            ++pictures;
        else
            ++sounds;
        total += QFileInfo(QDir(root).filePath(file)).size();
    }
    if (bytes)
        *bytes = total;
    QStringList parts;
    if (drawings)
        parts << BoardWindow::tr("%n drawing(s)", nullptr, drawings);
    if (pictures)
        parts << BoardWindow::tr("%n picture(s)", nullptr, pictures);
    if (sounds)
        parts << BoardWindow::tr("%n sound file(s)", nullptr, sounds);
    return parts.join(QStringLiteral(", "));
}
}

void BoardWindow::cleanProjectArtwork()
{
    if (m_document.path().isEmpty() || m_document.isLegacyProject() || !m_document.packPath().isEmpty())
        return;
    m_playback->stop();
    QString const title = tr("Clean Up Project Assets");
    QString const root = QFileInfo(m_document.path()).absolutePath();
    QStringList unused;
    QString error;
    qsizetype const found = m_document.findUnusedAssets(unused, &error);
    if (found < 0) {
        QMessageBox::warning(this, title, error);
        return;
    }
    if (found == 0) {
        QMessageBox::information(this, title,
            tr("No unused project assets were found. Saved versions, recovery copies, and undo history were checked."));
        return;
    }
    qint64 bytes = 0;
    QString const summary = describeFiles(unused, root, &bytes);
    QMessageBox prompt(QMessageBox::Question, title,
        tr("%1 (%2) are not used by this board, its undo history, other saved versions, or recovery copies.\n\n"
           "Move them to a recovery folder inside the project folder? You can put them back later with "
           "Restore Recovered Assets.").arg(summary, QLocale().formattedDataSize(bytes)),
        QMessageBox::Yes | QMessageBox::Cancel, this);
    prompt.setObjectName(QStringLiteral("BoardCleanupPreview"));
    prompt.button(QMessageBox::Yes)->setText(tr("Move to Recovery Folder"));
    prompt.setDefaultButton(QMessageBox::Cancel);
    prompt.setDetailedText(unused.join(QLatin1Char('\n')));
    if (prompt.exec() != QMessageBox::Yes)
        return;
    QString recovery;
    qsizetype const count = m_document.moveUnusedAssets(recovery, &error);
    if (count < 0) {
        if (!recovery.isEmpty())
            error += QStringLiteral("\n\n") + QDir::toNativeSeparators(recovery);
        QMessageBox::warning(this, title, error);
    } else if (count == 0) {
        QMessageBox::information(this, title, tr("No unused project assets were found."));
    } else {
        QMessageBox::information(this, title,
            tr("Moved %n unused project asset(s) to:\n%1\n\nTo put them back, choose File > Project Management > "
               "Restore Recovered Assets, or copy a file to the matching subfolder in the project folder.", nullptr, int(count))
                .arg(QDir::toNativeSeparators(recovery)));
    }
}

void BoardWindow::restoreRecoveredAssets()
{
    if (m_document.path().isEmpty() || m_document.isLegacyProject() || !m_document.packPath().isEmpty())
        return;
    m_playback->stop();
    QString const title = tr("Restore Recovered Assets");
    QString const project = m_document.path();
    auto const folders = BoardRecoveredAssets::list(project);
    if (folders.isEmpty()) {
        QMessageBox::information(this, title, tr("There are no recovered asset folders in this project folder."));
        return;
    }
    auto label = [](BoardRecoveredAssets::Folder const& folder) {
        return tr("%1 — %2, moved %3").arg(QFileInfo(folder.path).fileName(), describeFiles(folder.files, folder.path),
                                           QLocale().toString(folder.modified, QLocale::ShortFormat));
    };
    qsizetype chosen = 0;
    if (folders.size() == 1) {
        QMessageBox prompt(QMessageBox::Question, title,
            tr("Put the files from this folder back into the project folder?\n\n%1").arg(label(folders.front())),
            QMessageBox::Yes | QMessageBox::Cancel, this);
        prompt.setObjectName(QStringLiteral("BoardRestoreAssetsPrompt"));
        prompt.button(QMessageBox::Yes)->setText(tr("Restore"));
        prompt.setDetailedText(folders.front().files.join(QLatin1Char('\n')));
        if (prompt.exec() != QMessageBox::Yes)
            return;
    } else {
        QStringList items;
        for (auto const& folder : folders)
            items << label(folder);
        QInputDialog dialog(this);
        dialog.setObjectName(QStringLiteral("BoardRestoreAssetsChooser"));
        dialog.setWindowTitle(title);
        dialog.setLabelText(tr("Choose the recovery folder to put back into the project folder:"));
        dialog.setComboBoxItems(items);
        dialog.setComboBoxEditable(false);
        dialog.setOkButtonText(tr("Restore"));
        if (dialog.exec() != QDialog::Accepted)
            return;
        chosen = items.indexOf(dialog.textValue());
        if (chosen < 0)
            return;
    }
    BoardRecoveredAssets::Result result;
    QString error;
    QString const folder = folders[chosen].path;
    bool const ok = BoardRecoveredAssets::restore(project, folder, {}, result, &error);
    updateProjectState();
    QStringList lines;
    if (result.restored)
        lines << tr("Restored %n file(s) to the project folder.", nullptr, result.restored);
    if (result.duplicates)
        lines << tr("%n file(s) were already in the project and were removed from the recovery folder.", nullptr, result.duplicates);
    if (!result.conflicts.isEmpty())
        lines << tr("%n file(s) stayed in the recovery folder because the project already has a different file with "
                    "the same name, or the recovered copy is damaged:", nullptr, int(result.conflicts.size()))
                     + QLatin1Char('\n') + result.conflicts.join(QLatin1Char('\n'));
    if (!result.removedFolder && ok)
        lines << QDir::toNativeSeparators(folder);
    if (!ok) {
        lines.prepend(error);
        QMessageBox::warning(this, title, lines.join(QStringLiteral("\n\n")));
        return;
    }
    QMessageBox::information(this, title, lines.join(QStringLiteral("\n\n")));
}

bool BoardWindow::restoreMissingArtwork(QString const& path, QString& error)
{
    QStringList const missing = BoardRecoveredAssets::missingArtwork(path);
    if (missing.isEmpty())
        return false;
    QVector<QPair<QString, QStringList>> sources;
    QStringList found;
    for (auto const& folder : BoardRecoveredAssets::list(path)) {
        QStringList files;
        for (auto const& file : folder.files)
            if (missing.contains(file) && !found.contains(file))
                files << file;
        found << files;
        if (!files.isEmpty())
            sources.append({folder.path, files});
    }
    if (found.isEmpty())
        return false;
    QString text = tr("“%1” needs %n file(s) that Clean Up moved to a recovery folder in the project folder.", nullptr,
                      int(found.size())).arg(QFileInfo(path).fileName());
    if (found.size() < missing.size())
        text += QLatin1Char(' ') + tr("%n other missing file(s) are not in any recovery folder.", nullptr, int(missing.size() - found.size()));
    text += QStringLiteral("\n\n") + tr("Put the recovered files back and open the storyboard?");
    QMessageBox prompt(QMessageBox::Question, tr("Eclipse Storyboard"), text, QMessageBox::NoButton, this);
    prompt.setObjectName(QStringLiteral("BoardRestoreMissingPrompt"));
    auto* restore = prompt.addButton(tr("Restore and Open"), QMessageBox::AcceptRole);
    auto* cancel = prompt.addButton(QMessageBox::Cancel);
    prompt.setDefaultButton(restore);
    prompt.setEscapeButton(cancel);
    prompt.setDetailedText(found.join(QLatin1Char('\n')));
    prompt.exec();
    if (prompt.clickedButton() != restore)
        return false;
    for (auto const& [folder, files] : sources) {
        BoardRecoveredAssets::Result result;
        if (!BoardRecoveredAssets::restore(path, folder, files, result, &error))
            return false;
    }
    error.clear();
    return m_document.open(path, &error);
}
