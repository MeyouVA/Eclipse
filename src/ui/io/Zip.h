// Eclipse - reads the files out of a .zip (as a Flash .fla is): stored or deflated, walked from entry to entry, so that
// an archive whose directory at the end is odd (as Flash writes some) is read all the same.
#pragma once

#include "EclipseExport.h"

#include <QByteArray>
#include <QHash>
#include <QString>

namespace Zip {

// Every file of the archive by its name (folders as "a/b.xml").  Empty when it is no zip.
ECLIPSE_UI_API QHash<QString, QByteArray> read(QByteArray const& archive);
// Raw deflated data (RFC 1951) unpacked; empty, and `ok` false, when it is broken.
ECLIPSE_UI_API QByteArray inflate(QByteArray const& data, bool* ok = nullptr);

}
