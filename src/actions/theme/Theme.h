// Eclipse - the look of the program: one stylesheet (resources/stylesheets/Master.qss)
// whose colours come from the theme in use, and the icons (resources/icons/*.svg).
#pragma once

#include <QIcon>
#include <QString>

class QApplication;

namespace Theme {

// Applies the named theme to the whole program.  False, with a message, when its files
// cannot be read; the program then keeps the plain look.
bool apply(QApplication& app, QString const& resources, QString const& theme, QString* error);

// The icon `name` (as written in the menu files, for example "tool/brush"), or an empty
// icon when there is no such file.
QIcon icon(QString const& resources, QString const& name);

}
