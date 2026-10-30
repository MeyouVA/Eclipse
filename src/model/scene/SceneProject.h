// Eclipse - a scene kept as a folder: the scene file, a folder of elements (a file for each drawing, its pictures beside it), the palette library and frames.
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>

namespace SceneProject {

// The folders of a scene's folder.
inline constexpr char kElements[] = "elements";
inline constexpr char kPalettes[] = "palette-library";
inline constexpr char kFrames[] = "frames";
// A drawing's file, a scene palette's file, and the table of what a scene file keeps in its folder.
inline constexpr char kDrawingSuffix[] = "ecd";
inline constexpr char kPaletteSuffix[] = "ecp";
inline constexpr char kTableSuffix[] = "elements";

// What a scene keeps beside its file: the files by their path in the folder, and the table that lists them.
struct Split {
    QMap<QString, QByteArray> files;
    QJsonObject table;
};
// The drawings, pictures, palettes and textures of a scene's JSON taken out into files of their own; `owner` ends their names
// when the scene file is not the folder's own (another version of it beside it). `pack`: the files zlib-packed, as the scene file is.
Split split(QJsonObject& scene, QString const& sceneName, QString const& owner, bool pack);
// The files a scene's JSON names, read back into it from its folder; those that cannot be read are listed in `missing`.
void join(QJsonObject& scene, QString const& folder, QStringList* missing);
// Whether a scene's JSON names files of its folder (it was saved as a folder).
bool usesFolder(QJsonObject const& scene);
// A name that can be a file's: the characters a file name cannot have taken out.
QString fileName(QString const& name, QString const& fallback);

}
