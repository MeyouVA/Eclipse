#include "board/ui/BoardWindow.h"
#include "board/ui/BoardPlayback.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QRegularExpression>
#include <QStatusBar>

void BoardWindow::cleanProjectArtwork()
{
    if (m_document.path().isEmpty() || m_document.isLegacyProject())
        return;
    QString const title = tr("Clean Up Project Artwork");
    if (QMessageBox::question(this, title,
            tr("Check all saved Storyboard files in this folder and move unused drawings and pictures to a recovery folder?"),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
        return;
    QString recovery;
    QString error;
    auto const count = m_document.moveUnusedArtwork(recovery, &error);
    if (count < 0) {
        if (!recovery.isEmpty())
            error += QStringLiteral("\n\n") + QDir::toNativeSeparators(recovery);
        QMessageBox::warning(this, title, error);
    } else if (count == 0) {
        QMessageBox::information(this, title, tr("No unused drawings or pictures were found."));
    } else {
        QMessageBox::information(this, title,
            tr("Moved %n unused artwork file(s) to:\n%1", nullptr, int(count)).arg(QDir::toNativeSeparators(recovery)));
    }
}

void BoardWindow::saveProjectAs()
{
    if (m_document.path().isEmpty())
        return;
    m_playback->stop();
    QFileInfo const source(m_document.path());
    QFileDialog dialog(this, tr("Save Storyboard As"), source.absolutePath(), tr("Eclipse Storyboard (*.ecb)"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setDefaultSuffix(QStringLiteral("ecb"));
    dialog.setOption(QFileDialog::DontConfirmOverwrite);
    dialog.selectFile(tr("%1 Copy").arg(source.completeBaseName()) + QStringLiteral(".ecb"));
    if (dialog.exec() == QDialog::Accepted)
        saveProjectCopy(dialog.selectedFiles().value(0));
}

void BoardWindow::saveNewVersion()
{
    if (m_document.path().isEmpty())
        return;
    QFileInfo const source(m_document.path());
    QDir const folder = source.dir();
    QString base = source.completeBaseName();
    static QRegularExpression const ending(QStringLiteral(" - v[0-9]{3,6}$"), QRegularExpression::CaseInsensitiveOption);
    base.remove(ending);
    if (base.isEmpty())
        base = m_document.project().name;
    QRegularExpression const version(QStringLiteral("\\A%1 - v([0-9]{3,6})\\.ecb\\z")
        .arg(QRegularExpression::escape(base)), QRegularExpression::CaseInsensitiveOption);
    int last = 0;
    for (auto const& name : folder.entryList(QDir::Files | QDir::Hidden))
        last = std::max(last, version.match(name).captured(1).toInt());
    if (last >= 999999) {
        QMessageBox::warning(this, tr("Save as New Version"), tr("No more numbered versions can be created."));
        return;
    }
    QString const filename = QStringLiteral("%1 - v%2.ecb").arg(base).arg(last + 1, 3, 10, QLatin1Char('0'));
    saveProjectCopy(folder.filePath(filename));
}

void BoardWindow::saveProjectCopy(QString const& path)
{
    if (path.isEmpty())
        return;
    m_playback->stop();
    QString error;
    if (!m_document.saveAs(path, &error)) {
        QMessageBox::warning(this, tr("Save Storyboard Copy"), error);
        return;
    }
    rememberProjectPath();
    updateProjectState();
    statusBar()->showMessage(tr("Saved %1").arg(QDir::toNativeSeparators(m_document.path())), 3500);
}
