// Eclipse - the look of the program: one stylesheet (resources/stylesheets/Master.qss)
// whose colours come from the theme in use, and the icons (resources/icons/*.svg).
#pragma once

#include <QIcon>
#include <QString>

class QApplication;

namespace Theme {

bool apply(QApplication& app, QString const& resources, QString const& theme, QString* error);

QIcon icon(QString const& resources, QString const& name);

}
