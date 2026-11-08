#pragma once
#include <QString>
class QWidget;
namespace RawSoundImport {
QString prepare(QWidget* parent,QString const& path,QString const& scenePath,QString* error);
}
