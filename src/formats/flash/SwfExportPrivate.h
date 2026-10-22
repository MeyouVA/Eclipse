// Eclipse - the Flash movie writer, inside: drawings made into shapes and pictures, each written once; the scene's sound.
#pragma once

#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QSet>
#include <QTransform>
#include <QVector>

#include "formats/flash/SwfExport.h"
#include "formats/flash/SwfWriteParts.h"

struct Drawing;
struct Fill;
struct Picture;
struct Stroke;

namespace swfout {

class Shapes {
public:
    Shapes(Scene const& scene, SwfExport::Options const& options, SwfExport::Report& report);
    // The tags that define what has been asked for since this was last taken (to go before the frame that uses them).
    QByteArray take();
    // The shape of a drawing (its coordinates times `toTwips`): its id, written once; 0 when it paints nothing.
    // `lineScale`: how much a line's thickness grows on the way to the frame (for lines kept as thick however scaled).
    int drawing(Drawing const& d, QTransform const& toTwips, double lineScale);
    // A rectangle (in twips) of one colour.
    int card(QColor const& colour, QRect const& twips);
    void leftOut(QString const& what);

private:
    int shapeOf(QVector<Paint> const& paints);
    int bitmapOf(QImage const& image);
    Style styleOf(int swatch, QColor const& own, QPointF from, QPointF to, QTransform const& toTwips);
    void fill(QVector<Paint>& out, Fill const& f, QTransform const& toTwips);
    void stroke(QVector<Paint>& out, Stroke const& s, QTransform const& toTwips, double lineScale);
    void picture(QVector<Paint>& out, Picture const& p, QTransform const& toTwips);

    Scene const& m_scene;
    SwfExport::Options const& m_options;
    SwfExport::Report& m_report;
    QByteArray m_defs;
    int m_nextId = 1;
    QHash<QByteArray, int> m_shapes;    // by what they are (without their ids)
    QHash<qint64, int> m_bitmaps;       // by the image
};

// The scene's sound, streamed along with the frames: the head (before the first frame), then a block for each frame.
struct SoundStream {
    QByteArray head;
    QVector<QByteArray> blocks;
};
// False when there is nothing to hear (or it could not be made, and why).
bool soundStream(Scene const& scene, int first, int last, double fps, SoundStream& out, QString* problem);

}  // namespace swfout
