#pragma once

#include <QString>
#include <functional>

class QDir;

namespace BoardExport {
bool publishFolder(QString const& folder, std::function<bool(QDir const&)> const& write, QString* error);
bool fail(QString* error, QString const& message);
}
