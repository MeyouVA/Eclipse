// Eclipse - reading the flattened picture of a Photoshop file (PSD or PSB).
#pragma once

#include <QImage>
#include <QString>

namespace Psd {

// The picture the file shows (its layers as merged when it was saved); null, with `error` set, when it cannot be read.
QImage read(QString const& file, QString* error = nullptr);
// Whether a file is one, by its first bytes.
bool isPsd(QString const& file);

}
