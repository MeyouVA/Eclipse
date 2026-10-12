// Eclipse - what the parts of the main window (MainWindow*.cpp) have in common and nobody else needs.
#pragma once

#include <QSettings>
#include <QString>

// The kind of file a scene is kept in, as the file dialogs name it.
inline QString const kFilter = QStringLiteral("Eclipse Scene (*.ecl)");
// Raised when the views change so much that an arrangement saved before no longer fits.
inline int const kLayoutVersion = 8;

// Where the program keeps what it remembers between sessions.
inline QSettings settings()
{
    return QSettings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse"));
}
