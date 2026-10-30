// Eclipse - a drawing: its painted areas, its pictures, its lines.
#pragma once

#include "model/scene/Stroke.h"
#include "EclipseExport.h"

ECLIPSE_MODEL_API quint64 nextDrawingIdentity();

struct Fill {
    QVector<QPolygonF> outlines;
    quint64 sourceIdentity = 0;
    // Only while two sheets of a drawing are worked on as one: the sheet this came from (not kept in the file).
    int sheet = -1;
    QString text;
    QString textFont;
    QPointF textAt;
    int textAlign = 0;
    double textSpacing = 1.0;
    // How far apart its letters stand, as a percentage of what the font gives.
    int textKerning = 100;
    QTransform textShape;
    int swatch = -1;
    int group = 0;
    int order = 0;
    QColor color = Qt::black;
    QVector<Stroke> contours;
    bool gradPlaced = false;
    QPointF gradFrom;
    QPointF gradTo;
    bool follows = false;
    QPointF seed;
    int link = 0;

    QRectF bounds() const;
    int texture = 0;
    float textureAmount = 1.0f;
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

    bool contains(QPointF p) const;
    // Inside it, or within `radius` of its edge.
    bool hit(QPointF p, double radius) const;
    void transform(QTransform const& t);
    // Makes `outlines` again from `contours` (after a contour was edited).
    void rebuild();
    void giveContours(double tolerance);
    QVector<Fill> minus(QVector<QPolygonF> const& area, double tolerance, bool* changed = nullptr) const;
    // The other half of the same cut: what of this area lies inside `area`, in pieces as minus() gives them (none when nothing does).
    QVector<Fill> within(QVector<QPolygonF> const& area, double tolerance) const;
    // The two areas as one, in the colour of this one; false (and nothing done) when they do not meet.
    bool mergeWith(Fill const& other, double tolerance);
    bool overlaps(Fill const& other) const;
    QVector<Fill> pieces(QVector<int> const& counts = {}) const;
    static Fill fromStroke(Stroke const& stroke, double tolerance);
    // A closed curve through the corners of a polygon, fitted with as few points as will do.
    static Stroke contourOf(QPolygonF const& polygon, double tolerance);
};

struct Picture {
    QImage image;           // null when the file of a linked picture could not be read
    QString file;
    bool linked = false;
    // Colour management: read through the profile in its file (to sRGB) each time the file is read; and the colour space of its pixels (empty: the layer's).
    bool ownProfile = false;
    QString colourSpace;
    // The pixels a bitmap layer is painted in: made by the program, painted on by the brush.
    bool painted = false;
    int sheet = 2;
    // From the pixels of the image to the coordinates of the drawing.
    QTransform placement;
    int order = -1;
    QVector<QPolygonF> clip;
    QSize size;

    QRectF bounds() const { return placement.mapRect(QRectF(QPointF(0, 0), QSizeF(size))); }
    void transform(QTransform const& t) { placement = placement * t; }
};

// The pixels of a picture file; with `ownProfile`, taken from the colour profile the file carries to sRGB.
QImage readPictureFile(QString const& file, bool ownProfile);

struct Drawing {
    QVector<Picture> pictures;
    QVector<Fill> fills;
    QVector<Stroke> strokes;
    bool pinned = false;
    // A name of its own ("A", "blink"); without one it goes by its number in the layer.
    QString name;
    QString set;
    QString model;
    QPointF modelCentre;    // where the middle of the model is, in the drawing
    double modelScale = 1.0;    // how big, as a share of Model3D::kSize at its biggest
    enum Mark { NoMark, KeyMark, BreakdownMark, InBetweenMark };
    int mark = NoMark;
    // Shift and Trace: where the drawing is shown while tracing from it (its lines stay where they are).
    QTransform trace;
    // Its own pivot (Drawing Pivot tool), used where the layer says so.
    bool pivotSet = false;
    QPointF pivot;
    static QColor markColour(int mark)
    {
        return mark == KeyMark ? QColor(232, 76, 61) : mark == BreakdownMark ? QColor(80, 150, 240) : mark == InBetweenMark ? QColor(90, 200, 110) : QColor(24, 24, 24);
    }
    QVector<Fill> colourFills;
    QVector<Stroke> colourStrokes;
    QVector<Fill> underlayFills;
    QVector<Stroke> underlayStrokes;
    QVector<Fill> overlayFills;
    QVector<Stroke> overlayStrokes;
    // The four sheets, from the one underneath to the one on top.
    enum Sheet { UnderlaySheet, ColourSheet, LineSheet, OverlaySheet, SheetCount };
    QVector<Fill>& fillsOf(int sheet) { return sheet == UnderlaySheet ? underlayFills : sheet == ColourSheet ? colourFills : sheet == OverlaySheet ? overlayFills : fills; }
    QVector<Fill> const& fillsOf(int sheet) const { return const_cast<Drawing*>(this)->fillsOf(sheet); }
    QVector<Stroke>& strokesOf(int sheet) { return sheet == UnderlaySheet ? underlayStrokes : sheet == ColourSheet ? colourStrokes : sheet == OverlaySheet ? overlayStrokes : strokes; }
    QVector<Stroke> const& strokesOf(int sheet) const { return const_cast<Drawing*>(this)->strokesOf(sheet); }
    bool isEmpty() const
    {
        for (int sheet = 0; sheet < SheetCount; ++sheet)
            if (!fillsOf(sheet).isEmpty() || !strokesOf(sheet).isEmpty())
                return false;
        return pictures.isEmpty() && model.isEmpty();
    }
    QRectF bounds() const;
    static int topOrder(QVector<Fill> const& fills, QVector<Stroke> const& strokes);
    void keepJoints(Drawing const& before);
    void keepPointIdentitiesUnique();
    void renewPointIdentities();
    int nextJoint() const;
    static void freshJoints(QVector<Stroke>& strokes, Drawing const& into);
    static int bottomOrder(QVector<Fill> const& fills, QVector<Stroke> const& strokes);
    // The order a new painted area takes to lie on top of everything in the sheet.
    static int orderOnTop(QVector<Fill> const& fills, QVector<Stroke> const& strokes);
    // The colour art on its own, as a drawing.
    Drawing colourArt() const
    {
        Drawing art;
        art.fills = colourFills;
        art.strokes = colourStrokes;
        return art;
    }
    // Runtime identity follows a drawing through edits and undo; project files keep their existing format.
    quint64 editIdentity = nextDrawingIdentity();
};
