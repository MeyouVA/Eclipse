// Eclipse - pictures from a scanner, through the system's own scanning dialog (WIA on Windows, which drives TWAIN
// scanners too): the scanner picked, the scan set up and made there, each page kept as a picture file.
#pragma once

#include "EclipseExport.h"

#include <QString>
#include <QStringList>

#include <functional>

class QWidget;

namespace Scanner {

ECLIPSE_UI_API QStringList acquire(QWidget* parent, QString const& folder, QString* error = nullptr);
// Something to ask in place of the scanner (for trying the rest out where there is none); none: the scanner itself.
ECLIPSE_UI_API void setStandIn(std::function<QStringList(QString*)> standIn);

}
