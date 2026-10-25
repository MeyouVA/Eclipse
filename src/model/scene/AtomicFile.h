// Eclipse - replacing a file only after all replacement bytes are safely written.
#pragma once

#include <QString>

namespace AtomicFile {
bool replaceFrom(QString const& source, QString const& destination, QString* error);
}
