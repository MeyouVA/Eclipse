#pragma once

#include <QByteArray>
#include <QFileInfo>
#include <QSet>
#include <QJsonObject>
#include <QRegularExpression>

namespace BoardProjectFolder {
inline constexpr qsizetype maxProjectBytes = 128 * 1024 * 1024;
inline constexpr char saveLockName[] = ".eclipse-board-save.lock";
inline constexpr char copyLockName[] = ".eclipse-board-copy.lock";
QRegularExpression const& drawingAssetPattern();
QRegularExpression const& pictureAssetPattern();
bool save(QString const& path, QByteArray const& embedded, QString* error, QString const& assetRoot = {});
bool assemble(QString const& path, QJsonObject& manifest, QString* error);
bool referencedArtwork(QJsonObject const& manifest, QSet<QString>& used);
bool matchesDigest(QFileInfo const& info, QString const& digest);
bool isLocalDirectory(QFileInfo const& folder);
}
