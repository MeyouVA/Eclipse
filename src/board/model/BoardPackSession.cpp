#include "board/model/BoardDocument.h"
#include "board/model/BoardPackCache.h"

#include <QFileInfo>
#include <QLockFile>

void BoardDocument::leavePack()
{
    m_packLock.reset();
    m_pack.clear();
}

bool BoardDocument::openPack(QString const& pack, bool keepCache, QString* error)
{
    QFileInfo const info(pack);
    QString const canonical = info.canonicalFilePath();
    if (!BoardPackCache::isPack(pack) || !info.isFile() || info.isSymLink() || canonical.isEmpty()) {
        if (error)
            *error = tr("Choose an existing, regular .ecbz packed project.");
        return false;
    }
    bool const samePack = canonical.compare(m_pack, Qt::CaseInsensitive) == 0;
    auto lock = samePack ? std::move(m_packLock) : lockProject(canonical, error);
    if (!lock)
        return false;
    auto const cached = BoardPackCache::find(canonical);
    QString projectFile = keepCache ? cached.projectFile : QString();
    if (projectFile.isEmpty()) {
        if (samePack && !cached.projectFile.isEmpty() && cached.projectFile == m_path) {
            m_packLock = std::move(lock);
            if (error)
                *error = tr("Close the packed project before unpacking it again.");
            return false;
        }
        if (!cached.folder.isEmpty() && !BoardPackCache::discard(cached, error))
            return false;
        projectFile = BoardPackCache::unpack(canonical, error);
    }
    if (projectFile.isEmpty() || !open(projectFile, error)) {
        if (samePack)
            m_packLock = std::move(lock);
        return false;
    }
    m_packLock = std::move(lock);
    m_pack = canonical;
    return true;
}

bool BoardDocument::isPackCommitted() const
{
    return m_pack.isEmpty() || BoardPackCache::forProject(m_path).committed;
}

bool BoardDocument::commitPack(QString* error)
{
    if (m_pack.isEmpty() || !m_undo.isClean()) {
        if (error)
            *error = tr("Save the packed project before packing it.");
        return false;
    }
    return BoardPackCache::commit(m_project, m_path, error);
}
