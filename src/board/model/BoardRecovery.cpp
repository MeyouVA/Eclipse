#include "board/model/BoardDocument.h"

#include "board/model/BoardProjectFolder.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>

namespace {
QByteArray projectDigest(QString const& path)
{
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!file.open(QIODevice::ReadOnly) || file.size() > BoardProjectFolder::maxProjectBytes
        || !hash.addData(&file) || file.error() != QFileDevice::NoError)
        return {};
    return hash.result();
}
}

std::unique_ptr<QLockFile> BoardDocument::lockProject(QString const& path, QString* error)
{
    auto lock = std::make_unique<QLockFile>(path + QStringLiteral(".lock"));
    lock->setStaleLockTime(0);
    if (lock->tryLock())
        return lock;
    if (error) {
        *error = lock->error() == QLockFile::LockFailedError
            ? tr("This project is already open in another editor.")
            : tr("Could not lock the project. Check that its folder is writable.");
    }
    return {};
}

QString BoardDocument::recoveryPath() const
{
    return m_path + QStringLiteral(".recovery");
}

bool BoardDocument::hasRecovery() const
{
    if (m_path.isEmpty())
        return false;
    QFile file(recoveryPath());
    QFileInfo const recovery(file.fileName());
    if (!recovery.isFile() || recovery.isSymLink() || !file.open(QIODevice::ReadOnly))
        return false;
    auto const savedDigest = projectDigest(m_path);
    return !savedDigest.isEmpty() && file.read(savedDigest.size()) == savedDigest;
}

bool BoardDocument::writeRecovery(QString* error)
{
    if (error)
        error->clear();
    if (m_path.isEmpty() || m_undo.isClean())
        return true;
    auto const bytes = m_project.toBytes(error);
    if (bytes.isEmpty())
        return false;
    auto const digest = projectDigest(m_path);
    QSaveFile file(recoveryPath());
    if (digest.isEmpty() || QFileInfo(file.fileName()).isSymLink() || !file.open(QIODevice::WriteOnly)
        || file.write(digest) != digest.size() || file.write(bytes) != bytes.size() || !file.commit()) {
        if (error)
            *error = tr("Could not write a recovery copy for this project.");
        return false;
    }
    m_wroteRecovery = true;
    return true;
}

bool BoardDocument::recover(QString* error)
{
    if (error)
        error->clear();
    if (m_path.isEmpty() || !m_undo.isClean())
        return false;
    QFile file(recoveryPath());
    auto const digest = projectDigest(m_path);
    if (digest.isEmpty() || QFileInfo(file.fileName()).isSymLink() || !file.open(QIODevice::ReadOnly)
        || file.read(digest.size()) != digest) {
        if (error)
            *error = tr("The recovery copy no longer matches the saved project.");
        return false;
    }
    BoardProject project;
    auto const bytes = file.read(BoardProjectFolder::maxProjectBytes + 1);
    if (file.error() != QFileDevice::NoError) {
        if (error)
            *error = tr("Could not read the recovery copy.");
        return false;
    }
    if (!BoardProject::fromBytes(bytes, project, error))
        return false;
    QString const audio = project.audioPath(m_path);
    if (project.name != m_project.name || (!audio.isEmpty() && !QFileInfo(audio).isFile())) {
        if (error)
            *error = tr("The recovery copy does not match this project or its sound is missing.");
        return false;
    }
    m_wroteRecovery = true;
    apply(std::move(project), tr("Recover Unsaved Changes"));
    return true;
}

void BoardDocument::clearRecovery()
{
    if (m_path.isEmpty())
        return;
    QString const path = recoveryPath();
    if (QFileInfo(path).isFile() && !QFileInfo(path).isSymLink())
        QFile::remove(path);
    m_wroteRecovery = false;
}
