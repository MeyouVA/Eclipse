// Eclipse - the Library: the folder of files packed with a template, kept with it; and Version Swapper Mode.
#include "animation/ui/panels/LibraryView.h"

#include <QAction>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

#include "render/images/ImageFiles.h"

namespace {

bool copyTree(QString const& from, QString const& to)
{
    if (!QDir().mkpath(to))
        return false;
    QDirIterator it(from, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString const source = it.next();
        QString const target = QDir(to).filePath(QDir(from).relativeFilePath(source));
        if (it.fileInfo().isDir()) {
            if (!QDir().mkpath(target))
                return false;
        } else if (!QFile::copy(source, target) || QFileInfo(target).size() != it.fileInfo().size()) {
            return false;
        }
    }
    return true;
}

}  // namespace

bool LibraryView::isPackFolder(QString const& folder)
{
    QFileInfo const info(folder);
    if (!info.fileName().endsWith(QStringLiteral(".files"), Qt::CaseInsensitive))
        return false;
    QString const base = info.fileName().chopped(6);
    return QFileInfo::exists(info.absoluteDir().filePath(base + QLatin1Char('.') + kSuffix));
}

bool LibraryView::carryPackFolder(QString const& from, QString const& to, bool move)
{
    QString const source = Scene::templateFilesFolder(from), target = Scene::templateFilesFolder(to);
    if (!QFileInfo(source).isDir() || source.compare(target, Qt::CaseInsensitive) == 0)
        return true;    // nothing packed with it, or it stays where it is
    if (QFileInfo::exists(target))
        return false;
    bool const moved = move && QDir().rename(source, target);
    if (!moved) {
        if (!copyTree(source, target)) {
            QDir(target).removeRecursively();
            return false;
        }
        if (move)
            QDir(source).removeRecursively();
    }
    // The template names its files by where they were: pointed at the folder's new place.
    Scene piece;
    if (!Scene::load(to, piece, nullptr))
        return false;
    if (piece.repointFolder(source, target) > 0)
        return piece.save(to, nullptr);
    return true;
}

bool LibraryView::isPictureFile(QString const& path)
{
    if (path.isEmpty() || symbolIdOf(path) || !QFileInfo(path).isFile())
        return false;
    auto const* format = ImageFiles::find(QFileInfo(path).suffix());
    return format && format->readable;
}

void LibraryView::setVersionSwapper(bool on)
{
    m_versionSwapper = on;
    for (char const* name : {"LibraryVersionSwapper"})
        if (QAction* const action = findChild<QAction*>(QLatin1String(name)))
            action->setChecked(on);
}
