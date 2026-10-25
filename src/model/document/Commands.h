// Eclipse - undoable edits.
//
// Every change to the scene goes through a Document, which pushes a command on its undo
// stack and emits `changed`.  Two kinds of command are enough for now: adding one stroke
// (cheap, the common case) and replacing the whole scene state (any structural edit).
#pragma once

#include "EclipseExport.h"

#include <QObject>
#include <QUndoStack>

#include <functional>

#include "model/scene/Scene.h"

struct CellBlock {
    struct Row {
        bool holdsDrawings = false;
        QVector<Drawing> drawings;
        QVector<int> cells;        // one per frame of the block: index into `drawings`, or -1
        QVector<Keyframe> keys;    // their frames count from the start of the block
        QStringList labels;        // what each of `drawings` went by in its layer
        QString layerName;
        QPointF pivot;
    };
    // Paste Special: what becomes of the drawings (-1: copies, as a plain paste makes) ...
    enum DrawingMode { CopyDrawings = -1, TimingOnly, CreateMissing, CreateOverwrite, CreateRenamed };
    int drawingMode = CopyDrawings;
    // ... the parts of the keyframes pasted (Keyframe::Part), the layers met by name, the pivot taken along.
    int keyParts = 1023;
    bool matchNames = false;
    bool pastePivot = false;
    // The keyframes pasted added to what the layer pasted onto has there (Offset Keyframes).
    bool offsetKeys = false;
    // Only the drawings of the drawing layer the layer pasted onto shows where the paste begins (synced layers).
    bool currentSetOnly = false;
    int frames = 0;
    QVector<Row> rows;
    bool isEmpty() const { return frames <= 0 || rows.isEmpty(); }
    // Pasted, the block leaves the keyframes where it lands as they are (Paste Special without keyframes).
    bool leaveKeys = false;
    // The same cells from the last frame to the first.
    CellBlock reversed() const;
    // The block `times` times over, one after the other.
    CellBlock repeated(int times) const;
    // ... as a cycle: plain, each time on from where the last ended (keyframes moved on: a walk), or there and back again.
    enum CycleKind { Repeat, RepeatOffset, PingPong };
    CellBlock cycled(int times, int kind) const;
};

class ECLIPSE_MODEL_API Document : public QObject {
    Q_OBJECT

public:
    explicit Document(QObject* parent = nullptr);

    Scene const& scene() const { return m_scene; }
    bool extendsPreviousExposure() const { return m_extendPrevious; }
    void setExtendPreviousExposure(bool on) { m_extendPrevious = on; }
    // A keyframe made new holds its pose up to the next one (stop-motion) instead of moving on to it.
    bool stopMotionKeys() const { return m_stopMotionKeys; }
    void setStopMotionKeys(bool on) { m_stopMotionKeys = on; }
    bool namesDrawingsByFrame() const { return m_nameByFrame; }
    void setNameDrawingsByFrame(bool on) { m_nameByFrame = on; }
    int undoLevels() const { return m_undoLevels; }
    void setSubstitutionCycles(bool on) { m_substitutionCycles = on; }
    bool substitutionCycles() const { return m_substitutionCycles; }
    void setUndoLevels(int levels);
    QUndoStack* undoStack() { return &m_undo; }

    // Replaces everything (new / open); clears the undo history.
    void reset(Scene const& scene);

    // ---- drawing ------------------------------------------------------------------------
    // Adds a stroke to the drawing shown at (layer, frame); creates a drawing there when
    // the cell is empty.
    void addStroke(int layer, int frame, Stroke const& stroke);
    int eraseAt(int layer, int frame, QPointF p, double radius);

    void setDrawing(int layer, int frame, Drawing const& drawing, QString const& text);

    // ---- palette ------------------------------------------------------------------------
    int addSwatch(QString const& name, QColor const& color, int palette = 0);
    // Another palette of the scene, with one colour to start with.  Returns its place.
    int addPalette(QString const& name, int owner = 0);
    void renamePalette(int palette, QString const& name);
    void removePalette(int palette);
    void editSwatch(int id, QString const& name, QColor const& color);
    void setSwatchGradient(int id, Swatch::Kind kind, QVector<GradientStop> const& stops);
    // Makes a colour a texture colour: what has it shows this picture (a null picture: one colour again).
    void setSwatchTexture(int id, QImage const& texture);
    // Strokes and fills that used the swatch keep the colour it had.
    void removeSwatch(int id);
    // A colour moved up (-1) or down (+1) among those of its palette; and into another palette, at its end.
    bool moveSwatch(int id, int by);
    bool setSwatchPalette(int id, int palette);
    // Protected: the paint tools leave what has this colour as it is.
    bool setSwatchProtected(int id, bool on);
    // How depth is seen: in perspective, or (orthographic) with nothing bigger or smaller for it.
    bool setOrthographic(bool on);
    // The angle of view measured side to side (horizontal fit) or top to bottom.
    bool setFovAcross(bool on);
    bool setPaletteLink(int palette, QString const& path);
    bool updatePaletteFromFile(int palette);
    bool relinkFile(QString const& from, QString const& to);
    // Whose palette it is: the scene's (0) or a layer's (its id).
    bool setPaletteOwner(int palette, int owner);
    // A palette moved up (-1) or down (+1) in the list.
    bool movePalette(int palette, int by);
    // A palette of clones of another's colours, to override it with on some layers.  Its place.
    int clonePalette(int palette, QString const& name);
    // The colours of one palette brought into another: those it has by name already replace them in the drawings.
    bool mergePalette(int from, int into);
    // A new palette of the colours the given layers are drawn with; their drawings then use it.  Its place (-1: no colours).
    int paletteFromDrawings(QVector<int> const& layers, QString const& name, int owner = 0);
    // Colours and palettes taken out, as one step (palettes after colours; a palette keeps one colour at least).
    bool removeColours(QVector<int> const& swatches, QVector<int> const& palettes, QString const& text);
    // What is in colour `from` (on the given layers; all when empty) put in colour `to`.  How much changed.
    int recolour(int from, int to, QVector<int> const& layers = {});
    // What is in the colour loses its paint: areas go, lines are no longer seen (they still bound areas).
    int unpaintColour(int id, QVector<int> const& layers = {});
    // The colours mixed toward `tint`: by `amount` (0..1) of the way (blend), or by `amount` of the tint added (offset).
    bool tintColours(QVector<int> const& swatches, QColor const& tint, double amount, bool offset);
    // Colours added to a palette, as copies (or as clones) of these.  Their ids.
    QVector<int> addColours(QVector<Swatch> const& colours, int palette, bool clones);
    // The values (colour, gradient, texture) of these colours given to those, in order.
    bool setColourValues(QVector<int> const& targets, QVector<Swatch> const& values);
    bool setPaletteOverrides(int layer, QVector<int> const& palettes);
    int recoverColours();
    // Colours of another scene found here (by name and value) or made again in "Recovered Palette": their id -> id here.
    QHash<int, int> bringColours(QVector<Swatch> const& colours);

    // ---- exposure -----------------------------------------------------------------------
    // A new empty drawing starting at `frame` (the rest of the old exposure keeps its drawing).
    void newDrawing(int layer, int frame);
    bool renameDrawing(int layer, int frame, QString const& name);
    // The drawing shown on that frame is marked (Drawing::Mark) as a key, a breakdown or an in-between - or as nothing.
    bool markDrawing(int layer, int frame, int mark);
    // Frames of layers marked with a colour in the Timeline (an invalid colour: the marks go).
    bool markFrames(QVector<int> const& layers, int first, int last, QColor const& colour);
    // Every drawing of the layer is named after the frame it is first shown on.  False when that changes nothing.
    bool renameDrawingsByFrame(int layer);
    // ... or `prefix` and then its number in the layer ("arm-1", "arm-2" ...).
    bool renameDrawingsWithPrefix(int layer, QString const& prefix);
    bool exposeDrawing(int layer, int frame, QString const& label);
    // Holds the drawing of the previous frame over `frame`.
    void extendExposure(int layer, int frame);
    // Copies the drawing at `frame` into a new drawing starting there.
    void duplicateDrawing(int layer, int frame);
    void clearCell(int layer, int frame);
    // Takes the drawing shown at (layer, frame) out of the layer: every cell that showed it is empty.
    void deleteDrawing(int layer, int frame);
    // Shows another drawing of the layer on `frame` (to the end of its exposure).
    void substituteDrawing(int layer, int frame, int drawing);
    // Enforce Key Exposure: a substitution made in the middle of an exposure starts a new one there (off: the whole exposure is swapped).
    void setSubstitutionKeys(bool on) { m_substitutionKeys = on; }
    // Ripple Scene Markers: a marker moved takes the markers after it along.
    void setRippleMarkers(bool on) { m_rippleMarkers = on; }
    bool rippleMarkers() const { return m_rippleMarkers; }
    // A marker changed whole (its frames, name, colour and note); false when another starts on the same frame.
    bool setMarker(int index, Scene::Marker const& marker);
    void setSoloMode(bool on);
    bool substitutionKeys() const { return m_substitutionKeys; }
    // A drawing one place up or down the layer's list (its cells follow it).
    bool moveDrawing(int layer, int drawing, int direction);
    // Substitute Frame: the cell takes the drawing of the exposure after (or before) its own.
    void stepFrameSubstitution(int layer, int frame, int direction);
    // Insert Action Inbetween Drawing: an empty drawing, marked in-between, over the second half of the exposure at `frame`.
    int insertInbetweenDrawing(int layer, int frame);
    // The drawing after (+1) or before (-1) the one shown, going round at the ends.
    void stepSubstitution(int layer, int frame, int direction);
    // Substitute Drawing Next / Previous [Same Layer]: the next drawing (or the one before) of the same drawing layer.
    void stepSubstitutionInSet(int layer, int frame, int direction);
    bool stepDrawingSet(int layer, int frame, int direction);
    bool setDrawingSet(int layer, int frame, QString const& set);
    int applySkin(QVector<int> const& layers, QString const& set, int first, int last);
    int bakeDrivers(QVector<int> const& layers);
    int moveLayerBeside(int layer, int target, bool behind);

    // ---- animation ----------------------------------------------------------------------
    // Sets (or adds) the keyframe of `layer` at `frame`, as one undo step named `text`.
    void setKeyframe(int layer, int frame, Pose const& pose, QString const& text);
    void setPartKeyframe(int layer, int frame, int part, Pose const& pose, QString const& text);
    // Animation tools edit the visible pose, routing linked values into editable function columns.
    bool animatePose(int layer, int frame, Pose const& from, Pose const& to, QString const& text,
                     int parts = Keyframe::AllParts, bool offset = false, int first = -1, int last = -1);
    // A keyframe at `frame` holding the pose the layer already has there.
    void insertKeyframe(int layer, int frame);
    void removeKeyframe(int layer, int frame);
    bool moveKeyframe(int layer, int from, int to);

    // ---- morphing ---------------------------------------------------------------------------
    // From the key drawing `frame` shows to the next key drawing of the layer: the frames in
    // between show drawings made on the way from the one to the other.  False when there is
    // no next key drawing, no frame in between, or a morphing there already.
    bool createMorph(int layer, int frame);
    // The morphing `frame` lies in taken away (the frames show the first drawing again).
    bool deleteMorph(int layer, int frame);
    bool setMorphEase(int layer, int frame, double easeIn, double easeOut);
    // The velocity curve of the morphing on that frame (see Morph::velocity); none: the eases say.
    bool setMorphVelocity(int layer, int frame, QVector<QPointF> const& points);
    // The hints of a morphing of the layer (by its number among them): what is to go to what.
    bool setMorphHints(int layer, int index, QVector<QLineF> const& hints, QString const& text);
    // The morphing layers of a morphing of the layer (Morph::groups), in place of what it had.
    bool setMorphGroups(int layer, int index, QVector<Morph::Group> const& groups, QString const& text);
    // The drawings in between made real drawings of the layer, one a frame; the morphing goes.
    bool convertMorph(int layer, int frame);
    void setKeyTween(int layer, int frame, bool tween);
    void clearKeyframes(int layer);
    void setKeyEase(int layer, int frame, double easeIn, double easeOut, double liftIn = 0.0, double liftOut = 0.0);
    // The Function view.  Values' own curves at keyframes, set or dropped (`drop`), as one step.
    struct CurveChange {
        int frame = 0;
        ValueCurve curve;
        bool drop = false;
    };
    void setValueCurves(int layer, QVector<CurveChange> const& changes, QString const& text);
    // Values (Driver::Value) set on frames: on the keyframes there, or on new ones that set just those values.
    struct ValueChange {
        int frame = 0;
        int value = 0;
        double to = 0.0;
    };
    void setKeyValues(int layer, QVector<ValueChange> const& changes, QString const& text);
    // Some values' points (frame, Keyframe::Part bits) moved by `by` frames, the rest of their keyframes staying (see devnotes).
    bool moveValuePoints(int layer, QVector<QPair<int, int>> const& points, int by, QString const& text);
    // Values taken out of keyframes (frame, Keyframe::Part bits): a keyframe left setting nothing goes.
    void removeKeyValues(int layer, QVector<QPair<int, int>> const& points, QString const& text);
    // Values' curves from one keyframe to the next shaped as an ease (Layer::shapeSegment), as one step.
    struct SegmentEase {
        int layer = 0;
        int value = 0;
        int from = 0, to = 0;       // the frames of the two keyframes
        QPointF leave, arrive;
    };
    void setSegmentEases(QVector<SegmentEase> const& eases, QString const& text);
    // Function columns.  A new one (its name made unique): its id.  Changed as a whole (found by its id); taken away (the
    // values linked to it go back to their keyframes); renamed.
    int addFunction(FunctionColumn column, QString const& text);
    void setFunction(FunctionColumn const& column, QString const& text);
    void removeFunction(int id);
    bool renameFunction(int id, QString const& name);
    // A value of a layer linked to a column (0: back to its own keyframes).
    void linkValue(int layer, int value, int function, QString const& text);
    // A layer value's animation made into a new column of that kind, linked to it: the column's id (0: none made).
    int storeAsFunction(int layer, int value, int kind);
    // A column made another kind, its values kept (sampled on each frame when they must be); in a new column when `intoNew`.
    int convertFunction(int id, int kind, bool intoNew);
    void setEffects(int layer, QVector<Effect> const& effects, QString const& text);
    // Takes effect `index` out, and its amounts out of the keyframes.
    void removeEffect(int layer, int index);
    // The amount of an animated effect on `frame`: sets a keyframe there.
    void setEffectAmount(int layer, int frame, int index, double amount);
    // Where the node of the layer stands in the Node View.
    void setNodePos(int layer, QPointF pos);
    // Which bone of its parent the layer rides on (-1: none).
    void setParentBone(int layer, int bone);
    void setActiveCamera(int layer);
    // The Camera view shows that layer and what hangs under it (-1: the whole scene).
    void setDisplay(int layer);
    // The scene's write outputs, all at once.
    void setWrites(QVector<Scene::Write> const& writes);
    void setFov(int layer, double degrees, int frame = 0);
    void setBones(int layer, QVector<QPointF> const& joints, QString const& text);
    // The curve or envelope deformer of a drawing layer (see Layer::warp); no points takes it away.
    void setWarp(int layer, QVector<QPointF> const& points, bool closed, QString const& text);
    // Moves the pivot without moving the artwork at any keyframe.
    void setPivot(int layer, QPointF pivot);

    // ---- structure ----------------------------------------------------------------------
    int addLayer(int above);
    // The node graph replaced by another (one step to undo).
    void setGraph(NodeGraph const& graph, QString const& text);
    int createSymbol(QVector<int> const& layers, QString const& name);
    int insertSymbol(int symbolId, int above);
    int expandSymbol(int layer);
    // A new symbol with one empty drawing layer, and a layer above `above` that shows it.  Returns that layer.
    int insertEmptySymbol(QString const& name, int above);
    // The layer gets a symbol of its own: a copy of the one it shows.
    bool duplicateSymbol(int layer);
    // Clone: a second layer that shows the same drawings (see Scene::cloneLayer).  Returns it, or -1.
    int cloneLayer(int layer);
    bool enterSymbol(int symbolId);
    bool leaveSymbol();
    bool insideSymbol() const { return !m_around.isEmpty(); }
    // The symbols gone into, the outermost first, by name.
    QStringList symbolTrail() const;
    // The scene as it is to be saved: the outermost one, with what is being worked on inside symbols put back.
    Scene wholeScene() const;
    bool unsaved() const;
    void setSaved();
    int addPeg(int above);
    // The camera of the scene, in front of everything; made if there is none yet.
    int addCamera();
    // A colour-card behind everything.
    int addColourCard();
    // An empty group just above `above`.
    int addGroup(int above);
    // A group in the place of `layer`, with `layer` in it.  Returns the index of the group.
    int groupLayer(int layer);
    // The same with several layers: the group goes over the front one of them.
    int groupLayers(QVector<int> const& layers);
    int importDrawings(int layer, QString const& name, int frame, QVector<Drawing> const& drawings, int hold = 1);
    int insertTemplate(Scene const& piece, int frame);
    // The piece put onto the layers of the same names (Scene::pasteOnto); -1 when they are not all there.
    int pasteTemplateOnto(Scene const& piece, int frame, bool drawings = true, bool keys = true);
    int mergeLayers(QVector<int> const& layers);
    int addBone(int current);
    // A quadmap just above `above`, its corners those of the frame.
    int addQuadmap(int above);
    // An annotation column: notes and pen marks down the Xsheet; nothing in the picture.
    int addAnnotation(int above);
    void setCardColor(int layer, QColor const& color);
    void setBoneLength(int layer, double length);
    int addSyncedLayer(int layer);
    // Pose controllers (see Scene::Controller).  `controller` is its place in the scene's list.
    int addController(QString const& name, QVector<int> const& layers);
    void removeController(int controller);
    void renameController(int controller, QString const& name);
    // The poses (and drawings) the controller's layers have on `frame`, kept as a state; or put in place of one.
    int addControllerState(int controller, int frame, QString const& name);
    void replaceControllerState(int controller, int state, int frame);
    void removeControllerState(int controller, int state);
    void applyController(int controller, int frame, double value);
    void setControllerPoint(int controller, bool on);
    // Where a state lies on the pad of a 2D point controller (0..1 across and down).
    void moveControllerState(int controller, int state, QPointF at);
    void applyControllerPoint(int controller, int frame, QPointF where);
    // Control points of a layer's motion path (see Scene).  `key` is the keyframe the stretch starts at.
    int insertControlPoint(int layer, int frame);
    void moveControlPoint(int layer, int key, int control, QPointF to);
    bool offsetPath(int layer, QPointF by);
    void removeControlPoint(int layer, int key, int control);
    // How far a control point stands towards the camera: the path bends in depth through it.
    void setControlPointDepth(int layer, int key, int control, double z);
    int lockControlPointInTime(int layer, int key, int control);
    void setPathShape(int layer, int key, double tension, double continuity, double bias);
    // A clone of the layer: the same drawings and timing, changed on either; its place, or -1.
    int addCloneLayer(int layer, bool timing = true);
    void makeCloneIndependent(int layer);
    // Advanced Element Mode (Layer Properties): the layer shows the drawings of another (-1: its own again).
    bool useDrawingsOf(int layer, int source);
    void syncLayerWith(int layer, int other);
    // IK constraints of a bone: left out of IK; and whether it is turned only between two angles, and which.
    void setIkConstraints(int layer, bool excluded, bool limited, double minimum, double maximum);
    void setQuad(int layer, QVector<QPointF> const& corners, int frame = 0);
    void insertWarpPoint(int layer, QVector<QPointF> const& points, int index, double along);
    bool convertDeformed(int layer);
    void shiftPoses(int layer, int frame, Pose const& from, Pose const& to, QString const& text, int first = -1, int last = -1);
    // A sound layer in front, named after the file, starting on `startFrame`.
    int addSound(QString const& file, int startFrame);
    void setSoundStart(int layer, int frame);
    void setLipSync(int layer, QString const& shapes, QString const& text);
    void setMouth(int layer, int frame, QChar shape);
    bool mapLipSync(int soundLayer, int target, QVector<int> const& drawingFor);
    int addParentPeg(int layer);
    // -1 detaches.  False when it would make a loop.
    bool setParent(int layer, int parent);
    void removeLayer(int layer);
    void moveLayer(int layer, int delta);
    void renameLayer(int layer, QString const& name);
    void setLayerVisible(int layer, bool visible);
    void setLayerLocked(int layer, bool locked);
    // An art layer (Drawing::Sheet) of the layer's drawings shown in the picture, or left out of it.
    void setLayerSheetShown(int layer, int sheet, bool shown);
    // A drawing layer painted in pixels (a bitmap layer), or drawn in lines.
    void setLayerBitmap(int layer, bool bitmap);
    void setLayerSolo(int layer, bool solo);
    void setLayerCollapsed(int layer, bool collapsed);
    // An invalid colour gives the row its usual grey back.
    void setLayerColor(int layer, QColor const& color);
    void setAllLocked(bool locked, int except = -1);
    void setAllVisible(bool visible, int except = -1);
    void setAllCollapsed(bool collapsed);
    void clearSolo();
    // A copy of the layer in front of it.  Returns its index.
    int duplicateLayer(int layer);

    // ---- blocks of cells ----------------------------------------------------------------
    // What is in the frames `first`..`last` of these layers.
    CellBlock copyCells(QVector<int> const& layers, int first, int last) const;
    // Empties those cells: no drawing shown, no keyframe.
    void clearCells(QVector<int> const& layers, int first, int last, QString const& text);
    void pasteCells(QVector<int> const& layers, int frame, CellBlock const& block);
    // insert: the cells already there are pushed on to make room, not overwritten.
    void moveCells(QVector<int> const& from, int first, int last, QVector<int> const& to, int frame, bool copy, bool insert = false);
    // One change made to several layers, as one undo step.
    void editLayers(QVector<int> const& layers, QString const& text, std::function<void(Layer&)> const& change);
    // Any change to the scene as a whole, as one step to undo.
    void editScene(QString const& text, std::function<void(Scene&)> const& change);
    void removeLayers(QVector<int> const& layers);

    // ---- exposure, moving what follows --------------------------------------------------
    // One more cell of the drawing at `frame`; the cells after move along (the last one
    // of the layer falls off the end of the scene).
    void increaseExposure(int layer, int frame);
    void addKeyExposure(int layer, int frame);
    void removeKeyExposure(int layer, int frame);
    void removeDuplicateKeyExposures(int layer, int first, int last);
    // Takes the cell at `frame` out; the cells after move back.
    void decreaseExposure(int layer, int frame);
    // Makes the exposure `frame` is in exactly `length` cells long.
    void setExposure(int layer, int frame, int length);
    // An empty cell at `frame`, pushing the rest along.
    void insertBlankCell(int layer, int frame);
    // Takes the whole exposure `frame` is in out and pulls the rest back.
    void clearExposureAndPull(int layer, int frame);
    // Every empty cell after a drawing holds that drawing.
    void fillEmptyCells(int layer);
    void setCells(int layer, int first, QVector<int> const& drawings, QString const& text);
    void setFrameCount(int count);
    void setGuides(QVector<Scene::Guide> const& guides, QString const& text);
    // The names and values kept with a layer, or - for -1 - with the scene.
    void setMetadata(int layer, QVector<QPair<QString, QString>> const& pairs, QString const& text);
    // The pictures of the Reference view.
    void setReferences(QStringList const& files, QString const& text);
    // Shift and Trace: where a drawing is shown to trace from (identity: where it is); and the Desk.
    bool setTrace(int layer, int drawing, QTransform const& trace, QString const& text);
    bool clearTraces(int layer);
    void setDesk(QVector<Scene::DeskItem> const& desk, QString const& text);
    int addMarker(int frame, int length, QString const& name, QColor const& color);
    void editMarker(int index, QString const& name, QColor const& color);
    bool setMarkerNote(int index, QString const& note);
    void removeMarker(int index);
    bool moveMarker(int index, int frame);
    void setLayerOnion(int layer, bool onion);
    void insertFrames(int at, int count);
    // Takes the frames `first`..`last` out of the scene, which keeps one frame at least.
    void removeFrames(int first, int last);
    // The same exposure command on several layers, as one undo step.
    void beginMacro(QString const& text) { m_undo.beginMacro(text); }
    void endMacro() { m_undo.endMacro(); }
    // `fov` (above 0): the angle of view of the camera size; a camera that had the old one, and does not key it, takes it.
    void setSceneSettings(int width, int height, double fps, double fov = 0.0);

signals:
    void changed();
    // The scene worked on is another one: a symbol gone into or come out of (by undo and redo too).
    void levelChanged();

private:
    friend class SceneStateCommand;
    friend class AddStrokeCommand;
    friend class SymbolLevelCommand;

    // Runs `edit` on a copy of the scene and pushes the result as one undo step.
    template <class Edit> void apply(QString const& text, Edit edit);
    void setScene(Scene const& scene);

    Scene m_scene;
public:
    // (For the step of undo that goes into a symbol or out of one.)
    struct Around {
        Scene scene;        // the scene the symbol was gone into from
        int symbolId = 0;
    };

private:
    QVector<Around> m_around;
    bool m_unsavedInside = false;       // (what was not saved when the list of things to undo was emptied)
    bool m_extendPrevious = false;
    bool m_stopMotionKeys = false;
    // A keyframe of that pose on that frame; one made new is stop-motion if that is asked for.
    void keyOn(Layer& layer, int frame, Pose const& pose) const;
    bool m_nameByFrame = false;
    int m_undoLevels = 0;
    bool m_substitutionCycles = true;
    bool m_substitutionKeys = true;
    bool m_rippleMarkers = false;
    QUndoStack m_undo;
};
