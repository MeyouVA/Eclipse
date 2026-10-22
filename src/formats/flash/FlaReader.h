// Eclipse - the Flash document (.fla) reader: what it keeps while it reads, and what it does.
#pragma once

#include "formats/flash/FlaImport.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QHash>
#include <QMap>
#include <QPolygonF>
#include <QTransform>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <functional>
#include <memory>
#include "model/geometry/Morph.h"
#include "model/scene/Scene.h"
#include "formats/flash/FlashCut.h"
#include "formats/flash/FlashVideo.h"
#include "formats/flash/SwfImport.h"
#include "formats/archive/Zip.h"
#include "formats/flash/FlaParts.h"

namespace fla {

class Reader {
public:
    QHash<QString, QByteArray> const* files = nullptr;
    QHash<QString, QString> bitmapFiles;    // a bitmap of the library, by its name: its file in bin/
    struct Bitmap {
        QImage image;
        QSizeF shown;
    };
    QHash<QString, Bitmap> bitmaps;
    QHash<QString, Node> symbols;           // library symbols, by their names: their timelines
    QHash<Node const*, std::shared_ptr<Shape>> shapes;
    QHash<Node const*, int> lengths;
    QHash<QByteArray, int> gradientKeys;    // a gradient of `out->gradients`, by what it is
    FlaImport::Document* out = nullptr;
    // The gradient, among the document's (the one already there that is the same, or added).
    int gradient(Swatch const& s);
    Paint paintOf(Node const& fill);
    // A fill painted as `paint` says, coloured by `tint`, placed by `place`.
    void paintFill(Fill& fill, Paint const& paint, Tint const& tint, QTransform const& place);
    // The gradient as a colour effect leaves it (the same one when it changes nothing).
    int tinted(int gradientIndex, Tint const& tint);
    struct Built {
        Drawing main;
        QMap<QString, Drawing> sides;
    };
    QHash<QPair<Node const*, int>, Built> symbolFrames;
    Built const& symbolFrame(Node const& t, int frame, int depth);
    // Where what is drawn with an effect goes while a frame is drawn (by what the effect is); none: it is drawn in place.
    QMap<QString, Drawing>* sides = nullptr;
    // The effects met, by the names they are kept under.
    struct Lift {
        QString blend;
        QVector<FlaImport::Filter> filters;
    };
    QHash<QString, Lift> effects;
    // The blend mode and filters a symbol is drawn with, under a name that says what they are (empty: none).
    QString effectOf(Node const& element);
    // A symbol's frame put into a drawing, what of it has an effect into the drawings of those effects.
    void placeBuilt(Built const& built, QTransform const& place, Tint const& tint, Drawing& drawing, int& order);
    // A drawing put into another: placed, coloured by the effect, over what is there.
    void placeInto(Drawing const& inner, QTransform const& place, Tint const& tint, Drawing& drawing, int& order);
    // How many frames a timeline runs.
    int length(Node const& timeline);
    Node const* timelineOf(QString const& symbol);
    // What a layer shows on a frame, drawn into `drawing` placed by `place`.
    void layer(Node const& layerNode, int frame, QTransform const& place, Tint const& tint, Drawing& drawing, int& order, int depth);
    // Where a placed thing turns and scales about, in its own coordinates.
    static QPointF pivotOf(Node const& element);
    static double named(QString const& name, double t);
    static double eased(Node const& frame, double t);
    static QTransform between(QTransform const& a, QTransform const& b, double t, QPointF pivot = QPointF());
    void timeline(Node const& t, int frame, QTransform const& place, Tint const& tint, Drawing& drawing, int& order, int depth);
    void shapeInto(Node const& element, QTransform const& place, Tint const& tint, Drawing& drawing, int& order);
    static QFont fontOf(Node const* attrs);
    void text(Node const& element, QTransform const& place, Tint const& tint, Drawing& drawing, int& order);
    // A bitmap of the library, read once.
    Bitmap const& bitmap(QString const& name);
    // A video of the library: its frames, decoded once (none, when they could not be).
    struct Video {
        QStringList files;
        QSize size;
    };
    QHash<QString, Node> videoItems;        // the videos of the library, by their names
    QHash<QString, Video> videos;
    QHash<QString, QImage> videoFrames;     // the frames, by their files
    QString mediaFolder;
    QTemporaryDir* passing = nullptr;
    Video const& video(QString const& name);
    void draw(Node const& element, int offset, QTransform const& place, Tint const& tint, Drawing& drawing, int& order, int depth);
};

}  // namespace fla
