// Eclipse - the document model.
//
// A scene is a stack of layers over a number of frames.  Each layer owns its drawings and
// an exposure sheet: for every frame, which drawing is shown (or none).  A drawing held
// over several frames is simply the same index repeated, as on a paper exposure sheet.
#pragma once

#include "model/scene/NodeGraph.h"

#include <QHash>
#include <QSet>

class Scene {
public:
    Scene();

    int width = 1920;
    int height = 1080;
    double fps = 24.0;
    // Marks on the ruler of the Timeline: a name over a frame or a run of frames.
    struct Marker {
        int frame = 0;
        int length = 1;
        QString name;
        QColor color = QColor(60, 190, 205);
        QString note;       // what there is to remember about it
        bool operator==(Marker const&) const = default;
    };
    QVector<Marker> markers;    // sorted by frame
    struct Controller {
        struct State {
            QString name;
            QVector<Pose> poses;        // one per layer of the controller
            QVector<int> drawings;      // the drawing each shows (its place in the layer), or -1: left as it is
            QPointF at;                 // a 2D point controller: where the state lies on its pad (0..1 across and down)
            bool operator==(State const&) const = default;
        };
        int id = 0;
        QString name;
        QVector<int> layers;            // the ids of the layers
        QVector<State> states;
        double value = 0.0;             // where its slider was last left (0: the first state, 1 the second ...)
        bool point = false;
        QPointF where{0.5, 0.5};
        QVector<double> weightsAt(QPointF spot) const;
        QMap<int, double> keys;
        QMap<int, QPointF> pointKeys;
        double valueAt(int frame) const;
        QPointF whereAt(int frame) const;
        bool operator==(Controller const&) const = default;
    };
    QVector<Controller> controllers;
    // The node graph, once wired by hand (empty: the layers stack as they are).
    NodeGraph graph;
    NodeGraph graphFromLayers() const;
    void graphLayerAdded(int layer);
    void graphLayerRemoved(int layerId);
    struct Write {
        QString name;
        bool movie = false;         // a movie file (its kind by its extension); otherwise images
        QString path;               // the folder of the images, or the movie file
        QString format = QStringLiteral("png");     // of the images
        int percent = 100;          // of the scene's size
        bool enabled = true;
        bool operator==(Write const&) const = default;
    };
    QVector<Write> writes;
    struct Guide {
        bool vertical = false;      // a Line: runs down the picture, at an x; else across it, at a y
        double at = 0.0;
        enum Kind { Line, Ruler, SquareGrid, Isometric, OnePoint, TwoPoint, ThreePoint, Symmetry, FourPoint, FivePoint };
        static constexpr Kind kLastKind = FivePoint;
        Kind kind = Line;
        QVector<QPointF> points;
        bool operator==(Guide const&) const = default;
    };
    QVector<Guide> guides;
    static QString guideName(Guide::Kind kind);
    // A guide of a kind, set out to suit the size of this scene.
    Guide makeGuide(Guide::Kind kind) const;
    // The ways a line may run from a place (each of length one; a line runs either way along it).
    static QVector<QPointF> guideDirections(Guide const& guide, QPointF at);
    static bool guideArc(Guide const& guide, QPointF at, int way, QPointF* centre, double* radius);
    static QVector<QPointF> guideVanishingPoints(Guide const& guide);
    // Which of those is nearest to the way the pen has gone (-1: there are none) ...
    static int nearestGuideDirection(QVector<QPointF> const& ways, QPointF drag);
    // ... and the place on the line from `from` running `way` that is nearest to `to`.
    static QPointF alongGuide(QPointF from, QPointF to, QPointF way);
    // Names and values kept with the scene.
    QVector<QPair<QString, QString>> metadata;
    // The pictures of the Reference view: the files they are read from.
    QStringList references;
    // The Desk: drawings kept at hand to trace from (Shift and Trace), by layer id and drawing.
    struct DeskItem {
        int layer = 0;
        int drawing = 0;
        bool shown = true;
        bool operator==(DeskItem const&) const = default;
    };
    QVector<DeskItem> desk;
    // The marker over `frame`, or -1.
    int markerAt(int frame) const;
    QColor background = Qt::white;
    QVector<Layer> layers;  // index 0 is the back
    QVector<Swatch> palette;
    bool orthographic = false;
    // The camera's angle of view is measured from side to side (horizontal fit) rather than from top to bottom.
    bool fovAcross = false;
    // Saving: scene files written packed (zlib), smaller; they are read either way.  (Set from Preferences.)
    static void setCompressFiles(bool on);
    static bool compressFiles();
    double fov = Layer::kDefaultFov;
    QStringList paletteNames = {QStringLiteral("Scene")};
    QStringList paletteLinks;
    QString paletteLink(int which) const { return paletteLinks.value(which); }
    // Whose palette it is: 0 the scene's (every layer's), else the id of the layer (element) it belongs to.
    QVector<int> paletteOwners;
    int paletteOwner(int which) const { return paletteOwners.value(which, 0); }
    // A palette of the scene's list: the scene's own, or one whose layer is gone.
    bool paletteIsScene(int which) const { return paletteOwner(which) == 0 || layerIndex(paletteOwner(which)) < 0; }
    // The colours of a palette, in order (their ids).
    QVector<int> paletteSwatches(int which) const;
    // A palette taken to another place in the list; and one taken out (its colours with it: what used them keeps their values).
    void movePalette(int from, int to);
    void dropPalette(int which);
    // The ids of the colours that something is drawn or painted with.
    QSet<int> usedSwatches() const;
    // Every line and area of the given layers (all of them when empty) in colour `from` given colour `to`.  How many.
    int recolour(int from, int to, QVector<int> const& layers = {});
    // The colours a layer is drawn with in place of others, by its palette overrides: original id -> clone id.
    QHash<int, int> overridesOf(int layer) const;
    // When a colour is gone (its id unknown), what is drawn in it is shown in this colour instead of the colour it kept;
    // no colour (the default): it keeps its own (colour recovery).
    static void setReplacementColour(QColor const& colour);
    static QColor replacementColour();
    // Colours the artwork of the scene is linked to that the scene does not have: each made again, from what was
    // drawn with it, in a palette "Recovered Palette".  How many.
    int recoverColours();
    // What becomes of the colours of a template brought in (Preferences > Library).
    enum PasteColours { ReuseColours, CopyPalettes, UpdateColours, NewColours };
    static void setPasteColours(int mode);
    static int pasteColours();
    // A colour of `piece` (by id) as a colour of this scene, made or found as setPasteColours says; `map` remembers it.
    int takeColour(Scene const& piece, int swatch, QColor const& colour, QHash<int, int>& map);
    QJsonObject paletteJson(int which) const;
    bool takePaletteJson(int which, QJsonObject const& root);
    bool writePaletteFile(int which, QString const& path) const;
    bool readPaletteFile(int which, QString const& path);

    static constexpr int kNoSwatch = -1;
    QColor resolve(int swatch, QColor const& fallback) const;
    // What an area or a line is painted with: its colour, or the gradient of its swatch.
    QBrush brushFor(Fill const& fill) const;
    QBrush brushFor(Stroke const& stroke) const;
    // Where the gradient of an area lies: where it was placed, or across the area.
    void gradientLine(Fill const& fill, QPointF& from, QPointF& to) const;
    void gradientLine(Stroke const& stroke, QPointF& from, QPointF& to) const;
    Swatch const* findSwatch(int id) const;
    // Whether the paint tools are to leave what has this colour alone.
    bool protects(int swatch) const;
    int addSwatch(QString const& name, QColor const& color, int inPalette = 0);

    int frameCount() const { return m_frameCount; }
    void setFrameCount(int count);

    int addLayer(QString const& name, int at = -1, Layer::Kind kind = Layer::DrawingLayer);
    int layerIndex(int id) const;                  // -1 when there is no such layer
    int activeCamera = 0;
    int display = 0;
    // Whether a layer is in what the display shows.
    bool inDisplay(int layer) const;
    // The camera the scene is seen through, or -1: then the scene is seen as it is.
    int cameraIndex() const;
    QVector<int> cameras() const;
    double depthOf(int layer, int frame, PoseOverride const* over = nullptr) const;
    // How far the camera stands from artwork of no depth: from the angle it sees.
    double focalDistance(int frame = 0) const;
    // Whether the layer, or something it hangs under, is turned in space on a frame.
    bool turned(int layer, int frame, PoseOverride const* over = nullptr) const;
    void toSpace(int layer, int frame, QPointF p, double& x, double& y, double& z, PoseOverride const* over = nullptr) const;
    void depthPlane(int layer, int frame, double& a, double& b, double& c) const;
    // Level with the camera or behind it: not seen at all.
    bool behindCamera(int layer, int frame) const;
    QTransform depthTransform(double z, int frame) const;
    // By how much (1: not at all).
    double depthScale(double z, int frame) const;
    QVector<int> drawOrder(int frame) const;
    // From the coordinates of the scene to those of the picture, at a frame.
    QTransform cameraTransform(int frame) const;
    int parentIndex(int layer) const;              // -1 for a layer without parent
    bool isAncestor(int ancestor, int layer) const;
    // Whether the layer is rendered: it is enabled, and so is every group it is in.
    bool enabled(int layer) const;
    // Whether the Camera view shows the layer: it is enabled, and solo if any layer is.
    bool shows(int layer) const;
    // Whether a layer it hangs under is collapsed (the Timeline then leaves it out).
    bool foldedAway(int layer) const;
    bool hasChildren(int layer) const;
    // A copy of the layer just in front of it, under the same parent.  Returns its index.
    int duplicateLayer(int layer);
    // How many layers `layer` hangs under.
    int depth(int layer) const;
    QTransform worldTransform(int layer, int frame, PoseOverride const* over = nullptr) const;
    // The pose of a layer on a frame as the scene has it: its keyframes', with the values its drivers set from other layers.
    Pose poseOf(int layer, int frame, PoseOverride const* over = nullptr) const;
    // The same without depth: where the layer lies on its own plane.
    QTransform flatTransform(int layer, int frame, PoseOverride const* over = nullptr) const;
    // Attaches `layer` to `parent` (-1 to detach).  False when that would make a loop.
    bool setParent(int layer, int parent);
    void removeLayer(int layer);
    QString uniqueLayerName(QString const& base) const;

    // ---- templates: a piece of a scene kept to be used again -----------------------------
    // These layers (and everything attached to them) over the frames `first`..`last`, as a
    // scene of its own: what a template file holds.  A layer that was moved by keyframes
    // outside those frames keeps the pose it had on `first`.
    Scene extract(QVector<int> const& layers, int first, int last) const;
    int insert(Scene const& piece, int frame);
    int pasteOnto(Scene const& piece, int frame, bool drawings = true, bool keys = true);
    static Scene pasteReady(Scene piece, bool drawings, bool keys, int room);

    QJsonObject toJson(QString const& folder = QString()) const;

    // ---- symbols ------------------------------------------------------------------------------
    // Layers kept together under a name: a character, a prop, a cycle.  A layer of the scene shows a symbol
    // (Layer::symbolId), and any number of layers may show the same one - change the symbol and they all change.
    struct Symbol {
        int id = 0;
        QString name;
        QVector<Layer> layers;      // index 0 is the back, as in a scene
        int frameCount = 1;
    };
    QVector<Symbol> symbols;
    static constexpr int kSymbolDepth = 6;      // symbols inside symbols, so deep and no deeper
    Symbol const* findSymbol(int id) const;
    // The layers of a symbol as a scene of their own: to paint, and to work on.
    Scene symbolScene(Symbol const& symbol) const;
    // All that a symbol shows on one of its frames, as one drawing.
    Drawing symbolPicture(int symbolId, int frame, int depth = 0) const;
    // Gives the layers that show a symbol (this one, or - 0 - any) their drawings anew.
    void refreshSymbolInstances(int symbolId = 0);
    void copyTiming(int from, int to);
    // After a change: the synced layers whose timing was not touched take that of the one whose timing was.
    void followSyncedTiming(Scene const& before);
    // A new drawing layer over `layer`, synced with it; and a layer synced with another (-1: with none any more).
    int addSyncedLayer(int layer);
    int insertControlPoint(int layer, int frame);
    // The frame on which the layer is nearest the control point (between the two keyframes it lies between).
    int controlPointFrame(int layer, int key, int control) const;
    // The control point becomes a keyframe on that frame (locked in time); the frame, or -1.
    int lockControlPointInTime(int layer, int key, int control);
    // After a change: the clones of a layer whose drawings or timing it touched take them over.
    void followClones(Scene const& before);
    // A clone of the layer, over it; its place, or -1.  And a clone made a layer of its own again.
    int addCloneLayer(int layer, bool timing = true);
    void makeCloneIndependent(int layer);
    bool useDrawingsOf(int layer, int source);
    // The layer whose drawings it shows (another of its drawings-only group), or -1 for its own.
    int drawingsSource(int layer) const;
    void syncLayerWith(int layer, int other);
    int makeSymbol(QVector<int> const& layers, QString const& name);
    // Another layer that shows a symbol.  Returns where it is, or -1.
    int addSymbolInstance(int symbolId, int at = -1);
    int expandSymbol(int layer);
    void takeSymbol(int symbolId, Scene const& worked);
    // A symbol with one empty drawing layer in it.  Its id.
    int addEmptySymbol(QString const& name);
    // A symbol of its own for a layer that shows one: a copy, to change without changing the other.  Its id, or 0.
    int duplicateSymbol(int layer);
    int cloneLayer(int layer);
    // The symbols these layers show - and those shown inside them, and so on.
    QVector<Symbol> symbolsUsed(QVector<Layer> const& by) const;
    static bool fromJson(QJsonObject const& json, Scene& out, QString* error, QString const& folder = QString());
    bool save(QString const& path, QString* error) const;
    static bool load(QString const& path, Scene& out, QString* error);

private:
    int m_frameCount = 24;
    int m_nextSwatchId = 1;
    int m_nextLayerId = 1;
};
