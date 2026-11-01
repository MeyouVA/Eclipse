#pragma once
#include <QImage>
#include <QString>

namespace IconRead {
QImage read(QString const& path, QString* error = nullptr);
}
