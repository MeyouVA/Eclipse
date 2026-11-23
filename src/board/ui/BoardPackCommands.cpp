#include "board/ui/BoardWindow.h"
#include "board/model/BoardArchive.h"
#include "board/model/BoardPackCache.h"
#include "board/ui/BoardPlayback.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QLockFile>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>

bool BoardWindow::loadPack(QString const& pack)
{
    auto const cached = BoardPackCache::find(pack);
    bool keep = false;
    if (cached.pending() && QFileInfo(cached.projectFile) != QFileInfo(m_document.path())) {
        QMessageBox prompt(QMessageBox::Question, tr("Open Packed Project"),
            tr("The cache holds saved changes to “%1” that were never packed. Open those changes, or unpack the packed file again?")
                .arg(QFileInfo(pack).fileName()), QMessageBox::NoButton, this);
        prompt.setObjectName(QStringLiteral("BoardPackPendingPrompt"));
        auto* cachedButton = prompt.addButton(tr("Open Cached Changes"), QMessageBox::AcceptRole);
        auto* fresh = prompt.addButton(tr("Unpack Packed File"), QMessageBox::DestructiveRole);
        prompt.addButton(QMessageBox::Cancel);
        prompt.setDefaultButton(cachedButton);
        prompt.exec();
        if (prompt.clickedButton() != cachedButton && prompt.clickedButton() != fresh)
            return false;
        keep = prompt.clickedButton() == cachedButton;
    }
    QString error;
    if (!m_document.openPack(pack, keep, &error)) {
        QMessageBox::warning(this, tr("Eclipse Storyboard"),
                             tr("Could not open %1:\n%2").arg(QDir::toNativeSeparators(pack), error));
        return false;
    }
    return true;
}

bool BoardWindow::settlePack(BoardPackCache::Entry* drop)
{
    if (drop)
        *drop = {};
    if (m_document.packPath().isEmpty())
        return true;
    if (!m_document.isPackCommitted()) {
        QMessageBox prompt(QMessageBox::Question, tr("Save and Pack"),
            tr("Pack the saved changes into “%1” before leaving it?").arg(QFileInfo(m_document.packPath()).fileName()),
            QMessageBox::NoButton, this);
        prompt.setObjectName(QStringLiteral("BoardPackLeavePrompt"));
        auto* pack = prompt.addButton(tr("Pack"), QMessageBox::AcceptRole);
        auto* discard = prompt.addButton(tr("Discard Unpacked Changes"), QMessageBox::DestructiveRole);
        prompt.addButton(QMessageBox::Cancel);
        prompt.setDefaultButton(pack);
        prompt.exec();
        if (prompt.clickedButton() == pack) {
            QString error;
            if (!m_document.commitPack(&error)) {
                QMessageBox::warning(this, tr("Save and Pack"), error);
                return false;
            }
        } else if (prompt.clickedButton() != discard) {
            return false;
        }
    }
    if (drop)
        *drop = BoardPackCache::forProject(m_document.path());
    return true;
}

void BoardWindow::dropPackCache(BoardPackCache::Entry const& entry)
{
    if (entry.folder.isEmpty() || QFileInfo(entry.projectFile) == QFileInfo(m_document.path()))
        return;
    QString error;
    if (!BoardPackCache::discard(entry, &error))
        statusBar()->showMessage(tr("The packed project cache was kept: %1").arg(error), 6000);
}

void BoardWindow::savePacked(QString const& destination)
{
    QString const current = m_document.path();
    if (current.isEmpty() || destination.isEmpty())
        return;
    m_playback->stop();
    QString const title = tr("Save as Packed Project");
    if (QFileInfo(destination).absoluteFilePath().compare(QFileInfo(m_document.packPath()).absoluteFilePath(), Qt::CaseInsensitive) == 0) {
        if (saveChanges())
            archiveProject(QStringLiteral("pack"));
        return;
    }
    QString error;
    {
        QLockFile probe(destination + QStringLiteral(".lock"));
        probe.setStaleLockTime(0);
        if (!probe.tryLock()) {
            QMessageBox::warning(this, title, tr("That packed project is open in another editor, or its folder is read-only."));
            return;
        }
        if (!BoardArchive::pack(m_document.project(), current, destination, &error)) {
            QMessageBox::warning(this, title, error);
            return;
        }
    }
    auto const drop = BoardPackCache::forProject(current);
    QString const previousPack = m_document.packPath();
    m_document.close();
    if (!loadPack(destination)) {
        loadProject(previousPack.isEmpty() ? current : previousPack);
        return;
    }
    dropPackCache(drop);
    rememberProjectPath();
    updateProjectState();
    statusBar()->showMessage(tr("Now working in the packed project %1").arg(QDir::toNativeSeparators(destination)), 6000);
}

void BoardWindow::offerPackRecovery()
{
    auto const waiting = BoardPackCache::pending();
    for (auto const& entry : waiting) {
        if (QFileInfo(entry.projectFile) == QFileInfo(m_document.path()))
            continue;
        bool const packExists = QFileInfo(entry.pack).isFile();
        QMessageBox prompt(QMessageBox::Question, tr("Packed Project Recovery"),
            packExists ? tr("The cache holds changes to “%1” that were never packed. Open them now so you can save and pack them?")
                             .arg(QDir::toNativeSeparators(entry.pack))
                       : tr("The cache holds changes to “%1”, but the packed file is missing. Delete the cached changes or keep them for later?")
                             .arg(QDir::toNativeSeparators(entry.pack)),
            QMessageBox::NoButton, this);
        prompt.setObjectName(QStringLiteral("BoardPackRecoveryPrompt"));
        QPushButton* open = packExists ? prompt.addButton(tr("Recover"), QMessageBox::AcceptRole) : nullptr;
        auto* skip = prompt.addButton(tr("Skip"), QMessageBox::RejectRole);
        auto* remove = prompt.addButton(tr("Delete"), QMessageBox::DestructiveRole);
        prompt.setDefaultButton(open ? open : skip);
        prompt.setEscapeButton(skip);
        prompt.exec();
        if (open && prompt.clickedButton() == open) {
            BoardPackCache::Entry drop;
            if (confirmDiscard() && settlePack(&drop)) {
                QString error;
                if (m_document.openPack(entry.pack, true, &error)) {
                    dropPackCache(drop);
                    rememberProjectPath();
                    selectPanel(0, 0, true);
                    if (m_document.hasRecovery())
                        offerProjectRecovery(m_document.path());
                    statusBar()->showMessage(tr("Recovered from the cache. Use Save and Pack to update the packed file."), 8000);
                } else {
                    QMessageBox::warning(this, tr("Packed Project Recovery"), error);
                }
            }
        } else if (prompt.clickedButton() == remove
                   && QMessageBox::warning(this, tr("Packed Project Recovery"),
                          tr("Delete the cached changes permanently? This cannot be undone."),
                          QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Yes) {
            QString error;
            if (!BoardPackCache::discard(entry, &error))
                QMessageBox::warning(this, tr("Packed Project Recovery"), error);
        }
        return;
    }
}
