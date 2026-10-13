// Eclipse - textures of your own: the grain a line or a brush stroke can be drawn with, made from a
// picture or put together in the program.  The bank holds every one the program knows just now -
// those kept on this computer and those that came in with a scene.  A line names its texture by a
// number that is worked out from the grain itself, so the same texture has the same number in
// every scene and on every computer.
#pragma once

#include <QImage>
#include <QString>
#include <QVector>

namespace TextureBank {

// Numbers from here on are textures of your own (below: none, and the ones that come with the program).
constexpr int kFirst = 1000;
// The height of a strip of grain, in pixels: it spans the thickness of the line.
constexpr int kStripHeight = 64;

struct Entry {
    int id = 0;
    QString name;
    // The grain: grey, 255 where the ink is solid and 0 where there is none; kStripHeight high.
    // It repeats along its length (and, lying flat under a brush stroke, down as well).
    QImage grain;
    // How much the grain thins out towards the edges of a line: 0 not at all (the edge is cut clean), 1 a great deal.
    double edge = 0.5;
};

// Brings a picture to the form a grain has (grey, kStripHeight high, not too long).
QImage normalised(QImage const& grain);
// Puts a texture in the bank (it is there already when the same grain was put in before) and returns its number.
int add(QString const& name, QImage const& grain, double edge);
void remove(int id);
bool contains(int id);
// The texture of that number (its id is 0 when there is none).
Entry entry(int id);
// All of them, by name.
QVector<Entry> all();

// For a file: the grain as text, and back.
QString grainToText(QImage const& grain);
QImage grainFromText(QString const& text);

}
