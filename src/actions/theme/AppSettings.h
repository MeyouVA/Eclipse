// Eclipse - where the program keeps what it remembers between sessions.
#pragma once

#include <QSettings>

inline QSettings settings()
{
    return QSettings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse"));
}
