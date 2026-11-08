// Eclipse - the information and status program (eclipse-info): what a file is (found from what is in it, not its name) and
// what it holds; the version of the programs; the computer they run on (a virtual machine or not); and the state of the
// work beside Eclipse - the scene database, the Batch Rendering queue and the services the Service Launcher keeps.
#pragma once

#include "EclipseExport.h"

#include <QString>
#include <QStringList>

#include <functional>

namespace ToolInfo {

struct Description {
    QString kind;           // "scene", "drawing", "palette", "flash", "picture", "sound", "movie", "timeline", "pdf", "zip", "xml", "json", "unknown"
    QString format;         // said for people ("Eclipse scene, version 3", "Flash movie (Animate format)", ...)
    QStringList details;    // "Frames: 24" ...
};
// What a file is and what it holds.
ECLIPSE_UI_API Description describe(QString const& path);
ECLIPSE_UI_API QStringList describeLines(QString const& path);

// The hypervisor the computer runs on: "None (Physical)", a name ("Microsoft Hyper-V", "VMware", "KVM", "Xen", "VirtualBox",
// "Parallels", "QEMU"), "Unknown Hypervisor", or "Not determined - treat as Physical" where it cannot be asked; and a
// cloud it is in ("Amazon EC2", "Google Compute", "Microsoft Azure"; empty: none known).
ECLIPSE_UI_API QString hypervisor();
ECLIPSE_UI_API QString cloud();
ECLIPSE_UI_API QStringList systemLines();
ECLIPSE_UI_API QStringList versionLines();
// The scene database (Preferences > General, or `databaseRoot`), the Batch Rendering queue and the services.
ECLIPSE_UI_API QStringList statusLines(QString const& databaseRoot = QString());

using Say = std::function<void(QString const& line, bool error)>;
ECLIPSE_UI_API int run(QStringList const& arguments, Say const& say);
ECLIPSE_UI_API QString usage();

}  // namespace ToolInfo
