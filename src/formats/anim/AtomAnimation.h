// Eclipse - layers' keyframes read from and written to ATOM files (the animation transfer files of a 3D program): each layer a
// node of that name, its position, angles and scale as animation curves.
//
// Channels: translateX/Y/Z - position X, Y (up the picture is up the curve) and Z; rotateX/Y/Z - the angles (Z the other way
// round, as up is); scaleX/Y. Times count from 1 in the file's own frame rate. With `fields`, a position of 1 is one field (a
// twelfth of the scene's width); without, it is a unit of the scene.
#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

class Scene;

namespace AtomAnimation {

struct Options {
    bool fields = true;         // positions in fields (see above)
    double scale = 1.0;         // positions multiplied by this as well (an additional scale)
    bool overwrite = true;      // writing: a file there already replaced
    bool selectedOnly = false;  // writing: only `layers`
    QVector<int> layers;        // writing, with selectedOnly: the layers
};

struct Result {
    bool ok = false;            // false: nothing done; `error` says why
    QString error;
    QStringList warnings;       // done, but with these
    QStringList notes;          // how it was done (units, scale)
    int layers = 0;             // layers read into or written
    int keys = 0;               // keyframes set or written
};

// The scene's layers' keyframes written to an .atom file.
Result write(Scene const& scene, QString const& path, Options const& options);
// An .atom file's curves set as keyframes on the layers of the same names.
Result read(Scene& scene, QString const& path, Options const& options);
// The same from the text of a file (`path` only named in messages).
Result readText(Scene& scene, QString const& text, Options const& options);
QString writeText(Scene const& scene, Options const& options, Result* result);

}  // namespace AtomAnimation
