// Eclipse - what the parts of the main window (MainWindow*.cpp) have in common and nobody else needs.
#pragma once

#include <QFrame>
#include <QScrollArea>
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

// A view made of settings, put where it can be scrolled: when its panel is too small for
// all of it, a scroll bar shows instead of the bottom (or the side) being cut off.
inline QWidget* scrollingView(QWidget* view)
{
    QScrollArea* const area = new QScrollArea(view->parentWidget());
    area->setObjectName(QStringLiteral("ViewScroll"));
    area->setFrameShape(QFrame::NoFrame);
    area->setWidgetResizable(true);     // it still fills the panel when there is room
    area->setWidget(view);
    return area;
}
