#pragma once

#include <QByteArray>
#include <QJsonObject>

namespace BoardProjectFolder {
inline constexpr qsizetype maxProjectBytes = 128 * 1024 * 1024;
bool save(QString const& path, QByteArray const& embedded, QString* error, QString const& assetRoot = {});
bool assemble(QString const& path, QJsonObject& manifest, QString* error);
}
