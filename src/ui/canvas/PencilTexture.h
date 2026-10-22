// Eclipse - pencil textures: the grain a line can be drawn with instead of solid ink (chalk,
// charcoal and so on).  Each is a strip of grain that is laid along the line: its length runs
// with the line, its height spans the line's thickness.
#pragma once

#include <QIcon>
#include <QImage>
#include <QString>
#include <QVector>

namespace PencilTexture {

int count();
// Every kind there is to choose from: none, those that come with the program, then your own by name.
QVector<int> kinds();
// Whether a kind can be drawn (a texture of your own may have been thrown away, or never have come with the scene).
bool known(int kind);

// Textures of your own.  They are kept in a folder of this computer, a picture each, and read when first asked for.
QString libraryDirectory();
void loadLibrary();
// Keeps a grain as a texture (in the bank and in that folder) and returns its number.
int keep(QString const& name, QImage const& grain, double edge);
// Throws one away: lines drawn with it are solid from then on.
void discard(int id);
// Gives one another name (the lines drawn with it are not touched: they know it by its number).
void rename(int id, QString const& name);
bool exportTo(int id, QString const& path);
int importFile(QString const& path);
// The grain a picture makes: ink where it is dark (or where it is light), none where it is clear.
QImage grainFromPicture(QImage const& picture, bool lightIsInk);
QImage makeGrain(int size, int density, int streaks, unsigned seed);
// A picture of a grain as a line and lying flat, for the windows that make one.
QImage preview(QImage const& grain, double edge, QSize size);
// What kind `kind` (1 to count) is called.
QString name(int kind);
QImage const& tile(int kind, bool crisp = false);
// The same grain lying flat, as on paper - what a brush stroke shows of it.  It repeats both ways.
QImage const& paper(int kind);
// A small picture of a line drawn with it (kind 0: a solid line), for a list.
QIcon icon(int kind);

}
