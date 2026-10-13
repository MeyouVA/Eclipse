// Eclipse - pencil textures: the grain a line can be drawn with instead of solid ink (chalk,
// charcoal and so on).  Each is a strip of grain that is laid along the line: its length runs
// with the line, its height spans the line's thickness.
#pragma once

#include <QIcon>
#include <QImage>
#include <QString>

namespace PencilTexture {

// How many there are; a line's `texture` is 0 for none, or 1 to this.
int count();
// What kind `kind` (1 to count) is called.
QString name(int kind);
// The strip of grain: only how opaque each pixel is counts.  It repeats along its length.
QImage const& tile(int kind);
// A small picture of a line drawn with it (kind 0: a solid line), for a list.
QIcon icon(int kind);

}
