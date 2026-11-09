#include "board/ui/BoardWindow.h"
#include "board/model/BoardArchive.h"
#include "board/ui/BoardPlayback.h"
#include "actions/theme/AppSettings.h"

#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QStatusBar>

void BoardWindow::archiveProject(QString const& operation)
{
    QString const current = m_document.path();
    if (current.isEmpty())
        return;
    m_playback->stop();
    bool const backup = operation == QLatin1String("backup");
    QString const title = backup ? tr("Back Up Storyboard") : tr("Save and Pack");
    QFileInfo const info(current);
    QString name = info.completeBaseName();
    if (backup)
        name += tr(" Backup %1").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HHmmss")));
    QFileDialog dialog(this, title, info.absolutePath(), tr("Storyboard ZIP Archive (*.zip)"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setDefaultSuffix(QStringLiteral("zip"));
    dialog.selectFile(name + QStringLiteral(".zip"));
    if (dialog.exec() != QDialog::Accepted)
        return;
    QString const path = dialog.selectedFiles().value(0);
    QString error;
    if (!BoardArchive::pack(m_document.project(), current, path, &error)) {
        QMessageBox::warning(this, title, error);
        return;
    }
    if (!backup && !saveChanges())
        return;
    statusBar()->showMessage(tr("Archive saved to %1").arg(QDir::toNativeSeparators(path)), 6000);
}

void BoardWindow::restoreArchive()
{
    QString const title = tr("Restore and Open Backup");
    QString const initial = m_document.path().isEmpty()
        ? settings().value(QStringLiteral("board/welcome/directory")).toString()
        : QFileInfo(m_document.path()).absolutePath();
    QString const archive = QFileDialog::getOpenFileName(this, title, initial, tr("Storyboard ZIP Archive (*.zip)"));
    if (archive.isEmpty())
        return;
    QString const parent = QFileDialog::getExistingDirectory(this, tr("Choose a Folder for the Restored Project"),
        QFileInfo(archive).absolutePath());
    if (parent.isEmpty() || !confirmDiscard())
        return;
    m_playback->stop();
    QString error;
    QString const restored = BoardArchive::restore(archive, parent, &error);
    if (restored.isEmpty()) {
        QMessageBox::warning(this, title, error);
        return;
    }
    loadProject(restored);
}
