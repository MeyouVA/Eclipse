// Eclipse - a line: its points, its thickness along it, and what can be done to it.
#pragma once

#include <functional>
#include <QBrush>
#include <QColor>
#include <QPair>
#include <QJsonArray>
#include <QMap>
#include <QImage>
#include <QJsonObject>
#include <QPainterPath>
#include <QPointF>
#include <QPolygonF>
#include <QTransform>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

// How far a point is from the stretch between two others.
double distanceToSegment(QPointF p, QPointF a, QPointF b);

// One sample of a stroke: where the pen was and how hard it pressed (0..1).
struct StrokePoint {
    QPointF pos;
    float pressure = 1.0f;
    // On a curve: the line turns sharply here instead of bending through the point.
    bool corner = false;
    bool handles = false;
    QPointF in;
    QPointF out;
    float lean = 0.0f;
    // How solid the line is here (0 to 1): a line can fade along its length.
    float opacity = 1.0f;
    int joint = 0;
};

struct ThicknessPoint {
    double at = 0.0;
    float left = 1.0f;
    float right = 1.0f;
    bool operator==(ThicknessPoint const& o) const { return at == o.at && left == o.left && right == o.right; }
};

// One pen stroke, in scene coordinates (pixels of the camera frame).
struct Stroke {
    QVector<StrokePoint> points;
    // Only while two sheets of a drawing are worked on as one: the sheet this came from (not kept in the file).
    int sheet = -1;
    bool curve = false;
    // A closed line returns from its last point to its first.
    bool closed = false;
    enum Join { RoundJoin, MitreJoin, BevelJoin };
    enum Cap { RoundCap, FlatCap };
    Join join = RoundJoin;
    Cap capStart = RoundCap;  // at the first point
    Cap capEnd = RoundCap;    // at the last
    static constexpr double kMitreLimit = 50.0;
    // True when the line is as thick at every point (so that joins and caps apply).
    bool uniform() const;
    // Whether any of it leans to one side; whether any of it is less than solid.
    bool leans() const;
    bool fades() const;
    int swatch = -1;
    int group = 0;
    // A line that paint was made inside of (Auto Fill): the number that paint knows it by (0: none). See Fill::link.
    int link = 0;
    int order = 0;
    QColor color = Qt::black;
    float width = 4.0f;  // at full pressure
    bool gradPlaced = false;
    QPointF gradFrom;
    QPointF gradTo;
    static constexpr int kTextureCount = 5;
    int texture = 0;
    float textureAmount = 1.0f;
    // A crisp edge: the grain frays at the edge of the line but does not fade there, and is not blurred.
    bool textureCrisp = false;
    float textureLow = 0.0f;
    float textureHigh = 1.0f;
    double textureRamp(double grain) const
    {
        if (textureLow <= 0.0f && textureHigh >= 1.0f)
            return grain;
        double const low = textureLow, high = textureHigh;
        return high - low < 1e-4 ? (grain >= low && grain > 0.0 ? 1.0 : 0.0) : (grain <= low ? 0.0 : grain >= high ? 1.0 : (grain - low) / (high - low));
    }

    void transform(QTransform const& t);

    QVector<StrokePoint> path(QVector<int>* segmentOf = nullptr) const;
    // The same, with the place on the curve of each piece's start (see ThicknessPoint::at).
    QVector<StrokePoint> pathWithPlaces(QVector<double>& places) const;
    // The shape alone (places along it, with the points' own thickness where the line has none of its own).
    QVector<StrokePoint> shapeWithPlaces(QVector<double>& places) const;
    QVector<ThicknessPoint> keysByLength(QVector<double> const& places, QVector<double> const& along) const;
    static ThicknessPoint sidesAlong(QVector<ThicknessPoint> keys, double length, bool loop, double total);

    QVector<ThicknessPoint> thickness;
    // The pieces of the curve: places run from 0 to this.
    int pieces() const;
    // How thick the line is at a place (as `pressure`), and to which side (as `lean`); its two sides there.
    void thicknessAt(double at, float& pressure, float& lean) const;
    float pressureAt(double at) const;
    ThicknessPoint sidesAt(double at) const;
    // The thickness made a profile of its own, from the points' (a place at each); one there is already is kept.
    void ownThickness();
    void takeThicknessFrom(QVector<StrokePoint> const& drawn);
    // The thickness, as places along the line by how far along it they are (0 its start, 1 its end), and set so.
    QVector<QPair<double, ThicknessPoint>> thicknessByLength() const;
    void setThicknessByLength(QVector<QPair<double, ThicknessPoint>> const& byLength);
    // Places that a smooth run through their neighbours says within `tolerance` anyway: dropped.
    void simplifyThickness(double tolerance);
    // A thickness place put at `at` (as thick as the line is there), or the one already within `near`; its index.
    int thicknessPointAt(double at, double near = 1e-6);
    // The place on the curve nearest `p`, and the point of the curve at a place.
    double placeAt(QPointF p) const;
    QPointF pointAt(double at) const;
    // Which way the line goes at a place (a unit vector; null where it has no length).
    QPointF directionAt(double at) const;
    Stroke section(int from, int to) const;
    // The line end for end: its points, its tips, its thickness (and which side is which).
    void reverse();
    void append(Stroke const& other, bool meets = true);
    // The thickness at a share of the way along the line (0 its start, 1 its end).
    ThicknessPoint sidesAtLength(double fraction) const;
    // A point taken out of the line; its thickness kept where it was along the line.
    void removePoint(int index);
    void freezeHandles();
    // (After piece `s` was cut in two at `t`: its thickness places moved to where they now are.)
    void splitThickness(int s, double t);
    // The index of the point after which a new point at `p` would go.
    int segmentAt(QPointF p) const;
    void fit(double tolerance, double cornerDegrees = 50.0);
    void setCorner(int index, bool corner);
    // Whether the line turns sharply at a point (always, on a line of straight pieces).
    bool isCorner(int index) const;
    void handlesAt(int index, QPointF& in, QPointF& out) const;
    // Gives a point its handles; the stroke becomes a curve if it was not one.
    void setHandles(int index, QPointF in, QPointF out);
    int insertPoint(QPointF p);

    QRectF bounds() const;
    // True when the stroke passes within `radius` of `p`.
    bool hit(QPointF p, double radius) const;
    QVector<QPolygonF> outlinePolygons() const;
    QVector<Stroke> minus(QVector<QPolygonF> const& area, double tolerance, bool* changed = nullptr) const;
};
