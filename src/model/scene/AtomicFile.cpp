// Eclipse - transactional replacement of generated media.
#include "model/scene/AtomicFile.h"

#include <QFile>
#include <QSaveFile>

bool AtomicFile::replaceFrom(QString const& source, QString const& destination, QString* error)
{
    QFile input(source);
    QSaveFile output(destination);
    auto fail = [&](QString const& why) { if (error) *error = why; return false; };
    if (!input.open(QIODevice::ReadOnly))
        return fail(input.errorString());
    if (!output.open(QIODevice::WriteOnly))
        return fail(output.errorString());
    while (!input.atEnd()) {
        QByteArray const bytes = input.read(1024 * 1024);
        if (bytes.isEmpty() && input.error() != QFile::NoError)
            return fail(input.errorString());
        if (output.write(bytes) != bytes.size())
            return fail(output.errorString());
    }
    return output.commit() || fail(output.errorString());
}
