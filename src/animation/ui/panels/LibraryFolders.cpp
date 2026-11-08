#include "animation/ui/panels/LibraryView.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QListWidget>
#include <QLockFile>
#include <QMessageBox>
#include <QRegularExpression>

namespace {
bool within(QString const& path,QString const& root)
{
    return path.compare(root,Qt::CaseInsensitive)==0 || path.startsWith(root+QLatin1Char('/'),Qt::CaseInsensitive);
}
}

bool LibraryView::validEntryName(QString const& name)
{
    if(name.isEmpty() || name==QLatin1String(".") || name==QLatin1String("..") || name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' '))) return false;
    for(auto c : name) if(c.unicode()<32 || QStringLiteral("\\/:*?\"<>|").contains(c)) return false;
    static QRegularExpression const reserved(QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\.|$)"),QRegularExpression::CaseInsensitiveOption);
    return !reserved.match(name).hasMatch();
}

QString LibraryView::rootFor(QString const& path) const
{
    QString const absolute=QFileInfo(path).absoluteFilePath(); QString found;
    for(auto const& folder : m_folders) if(within(absolute,folder) && folder.size()>found.size()) found=folder;
    return found;
}

bool LibraryView::canModifyPath(QString const& path,QString* error)
{
    auto fail=[&](QString const& why) { if(error) *error=why; return false; };
    if(path.trimmed().isEmpty()) return fail(tr("Select a library path first."));
    if(path.startsWith(kSymbolsFolder) || symbolIdOf(path)) return fail(tr("The scene's symbols are kept in the scene, not in library files."));
    QString const root=rootFor(path);
    if(root.isEmpty()) return true;
    for(QString at=QFileInfo(path).absoluteFilePath();within(at,root);at=QFileInfo(at).absolutePath()) {
        QFileInfo const info(at);
        if(info.isSymLink() || info.isJunction()) return fail(tr("Library changes cannot follow a linked file or folder."));
        if(at.compare(root,Qt::CaseInsensitive)==0) break;
    }
    if(m_readOnly.contains(root)) return fail(tr("This library is read-only. Enable Right To Modify first."));
    if(m_locks.contains(root)) return true;
    if(!QFileInfo(root).isDir() && (root!=QDir(defaultFolder()).absolutePath() || !QDir().mkpath(root))) return fail(tr("The library folder does not exist."));
    auto lock=std::make_shared<QLockFile>(QDir(root).filePath(QStringLiteral(".eclipse-library.lock"))); lock->setStaleLockTime(0);
    if(!lock->tryLock(0)) return fail(tr("The library is locked by another editor or cannot be written."));
    m_locks.insert(root,std::move(lock)); return true;
}

bool LibraryView::setRightToModify(bool enabled,QString* error)
{
    QString const root=rootFor(currentFolder()); if(root.isEmpty()) return false;
    if(!enabled) { m_locks.remove(root); m_readOnly.insert(root); return true; }
    m_readOnly.remove(root);
    if(canModifyPath(currentFolder(),error)) return true;
    m_readOnly.insert(root); return false;
}
bool LibraryView::rightToModify() const { return m_locks.contains(rootFor(currentFolder())); }
void LibraryView::refreshFolders() { showFolders(); }

bool LibraryView::renameFolder(QString const& name)
{
    if(showingSymbols()) return false;
    QString const from=QDir(currentFolder()).absolutePath(),clean=name.trimmed(),root=rootFor(from);
    if(root.isEmpty() || from.compare(root,Qt::CaseInsensitive)==0 || !validEntryName(clean) || !canModifyPath(from)) return false;
    for(auto const& folder : m_folders) if(within(folder,from)) return false;
    QString const to=QDir(QFileInfo(from).absolutePath()).filePath(clean);
    if(!canModifyPath(to) || QFileInfo::exists(to) || !QDir().rename(from,to)) return false;
    showFolders(); setCurrentPath(to); return true;
}

bool LibraryView::deleteFolder(bool ask)
{
    if(showingSymbols()) return false;
    QString const path=QDir(currentFolder()).absolutePath(),root=rootFor(path);
    if(root.isEmpty() || path.compare(root,Qt::CaseInsensitive)==0 || !canModifyPath(path)) return false;
    for(auto const& folder : m_folders) if(within(folder,path)) return false;
    if(ask && QMessageBox::question(this,tr("Delete Library Folder"),tr("Move the folder \"%1\" and its contents to the Recycle Bin?").arg(QFileInfo(path).fileName()))!=QMessageBox::Yes) return false;
    if(!QFile::moveToTrash(path)) { if(ask) QMessageBox::warning(this,tr("Library"),tr("The folder could not be moved to the Recycle Bin.")); return false; }
    showFolders(); setCurrentPath(QFileInfo(path).absolutePath()); return true;
}
