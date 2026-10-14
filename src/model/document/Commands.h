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

// A block of cells lifted out of the timeline: for each of its rows the drawings shown
// and the keyframes, over a run of frames.
struct CellBlock {
    struct Row {
        bool holdsDrawings = false;
        QVector<Drawing> drawings;
        QVector<int> cells;        // one per frame of the block: index into `drawings`, or -1
        QVector<Keyframe> keys;    // their frames count from the start of the block
    };
    int frames = 0;
    QVector<Row> rows;
    bool isEmpty() const { return frames <= 0 || rows.isEmpty(); }
    // Pasted, the block leaves the keyframes where it lands as they are (Paste Special without keyframes).
    bool leaveKeys = false;
    // The same cells from the last frame to the first.
    CellBlock reversed() const;
    // The block `times` times over, one after the other.
    CellBlock repeated(int times) const;
};

class ECLIPSE_MODEL_API Document : public QObject {
    Q_OBJECT

public:
    explicit Document(QObject* parent = nullptr);

    Scene const& scene() const { return m_scene; }
    // A drawing begun on an empty frame: with this on, the drawing before it is held over the
    // empty frames up to it (off: they stay empty).
    bool extendsPreviousExposure() const { return m_extendPrevious; }
    void setExtendPreviousExposure(bool on) { m_extendPrevious = on; }
    // A keyframe made new holds its pose up to the next one (stop-motion) instead of moving on to it.
    bool stopMotionKeys() const { return m_stopMotionKeys; }
    void setStopMotionKeys(bool on) { m_stopMotionKeys = on; }
    // How many steps can be undone (0: as many as there are).  It takes hold at once when there is
    // nothing to undo yet, and otherwise with the next scene made or opened.
    // A drawing made on a frame is named after that frame (off: it goes by its number in the layer).
    bool namesDrawingsByFrame() const { return m_nameByFrame; }
    void setNameDrawingsByFrame(bool on) { m_nameByFrame = on; }
    int undoLevels() const { return m_undoLevels; }
    void setUndoLevels(int levels);
    QUndoStack* undoStack() { return &m_undo; }

    // Replaces everything (new / open); clears the undo history.
    void reset(Scene const& scene);

    // ---- drawing ------------------------------------------------------------------------
    // Adds a stroke to the drawing shown at (layer, frame); creates a drawing there when
    // the cell is empty.
    void addStroke(int layer, int frame, Stroke const& stroke);
    // Removes every stroke of the drawing at (layer, frame) within `radius` of `p`.
    // Returns how many were removed.
    int eraseAt(int layer, int frame, QPointF p, double radius);

    // Replaces the drawing shown at (layer, frame) - or creates one on an empty cell - as
    // one undo step named `text`.  What the selection and paint tools use.
    void setDrawing(int layer, int frame, Drawing const& drawing, QString const& text);

    // ---- palette ------------------------------------------------------------------------
    int addSwatch(QString const& name, QColor const& color, int palette = 0);
    // Another palette of the scene, with one colour to start with.  Returns its place.
    int addPalette(QString const& name);
    void renamePalette(int palette, QString const& name);
    // Takes a palette out with its colours; what was drawn with them keeps the colour it had.
    // The last palette of a scene stays.
    void removePalette(int palette);
    void editSwatch(int id, QString const& name, QColor const& color);
    // Makes a colour a gradient of these colours (two at least), or one colour again
    // (Swatch::Solid: it keeps the colour it shows).
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
    // A palette linked to a file (an empty path: linked no longer); and its colours brought up to
    // those the file has now.
    bool setPaletteLink(int palette, QString const& path);
    bool updatePaletteFromFile(int palette);
    // A file the scene reads from outside itself (a sound, a linked picture) is somewhere else
    // now: everything that read `from` reads `to` instead.  False when nothing read it.
    bool relinkFile(QString const& from, QString const& to);

    // ---- exposure -----------------------------------------------------------------------
    // A new empty drawing starting at `frame` (the rest of the old exposure keeps its drawing).
    void newDrawing(int layer, int frame);
    // The drawing shown on that frame gets a name of its own (an empty one: it goes by its number
    // again).  False when there is none there, or another drawing of the layer is called that.
    bool renameDrawing(int layer, int frame, QString const& name);
    // The drawing shown on that frame is marked (Drawing::Mark) as a key, a breakdown or an in-between - or as nothing.
    bool markDrawing(int layer, int frame, int mark);
    // Frames of layers marked with a colour in the Timeline (an invalid colour: the marks go).
    bool markFrames(QVector<int> const& layers, int first, int last, QColor const& colour);
    // Every drawing of the layer is named after the frame it is first shown on.  False when that changes nothing.
    bool renameDrawingsByFrame(int layer);
    // ... or `prefix` and then its number in the layer ("arm-1", "arm-2" ...).
    bool renameDrawingsWithPrefix(int layer, QString const& prefix);
    // What is typed into a cell of the Xsheet: that frame shows the drawing of the layer called
    // `label` (its name or its number) - a new, empty one of that name when there is none; nothing
    // typed empties the cell.  False when nothing changed.
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
    // The drawing after (+1) or before (-1) the one shown, going round at the ends.
    void stepSubstitution(int layer, int frame, int direction);
    // Puts `layer` just in front of (or with `behind`, just behind) layer `target`.
    // Returns where it ended up.
    int moveLayerBeside(int layer, int target, bool behind);

    // ---- animation ----------------------------------------------------------------------
    // Sets (or adds) the keyframe of `layer` at `frame`, as one undo step named `text`.
    void setKeyframe(int layer, int frame, Pose const& pose, QString const& text);
    // A keyframe of one part of the pose only (a Keyframe::Part): its value as `pose` has it.  On a frame with a keyframe,
    // that keyframe sets that part too.
    void setPartKeyframe(int layer, int frame, int part, Pose const& pose, QString const& text);
    // A keyframe at `frame` holding the pose the layer already has there.
    void insertKeyframe(int layer, int frame);
    void removeKeyframe(int layer, int frame);
    // The keyframe on `from` put on `to` instead, as it is (pose, ease, kind).  False when
    // there is none, or `to` has a keyframe already or is not a frame of the scene.
    bool moveKeyframe(int layer, int from, int to);

    // ---- morphing ---------------------------------------------------------------------------
    // From the key drawing `frame` shows to the next key drawing of the layer: the frames in
    // between show drawings made on the way from the one to the other.  False when there is
    // no next key drawing, no frame in between, or a morphing there already.
    bool createMorph(int layer, int frame);
    // The morphing `frame` lies in taken away (the frames show the first drawing again).
    bool deleteMorph(int layer, int frame);
    bool setMorphEase(int layer, int frame, double easeIn, double easeOut);
    // The hints of a morphing of the layer (by its number among them): what is to go to what.
    bool setMorphHints(int layer, int index, QVector<QLineF> const& hints, QString const& text);
    // The drawings in between made real drawings of the layer, one a frame; the morphing goes.
    bool convertMorph(int layer, int frame);
    // Motion (tween to the next keyframe) or stop-motion (hold).  Applies to the keyframe
    // at or before `frame`.
    void setKeyTween(int layer, int frame, bool tween);
    void clearKeyframes(int layer);
    // The ease of the keyframe at exactly `frame` (0..1 each).
    // (And how far its handles are pulled up or down, -2..2 each: flat unless told.)
    void setKeyEase(int layer, int frame, double easeIn, double easeOut, double liftIn = 0.0, double liftOut = 0.0);
    void setEffects(int layer, QVector<Effect> const& effects, QString const& text);
    // Takes effect `index` out, and its amounts out of the keyframes.
    void removeEffect(int layer, int index);
    // The amount of an animated effect on `frame`: sets a keyframe there.
    void setEffectAmount(int layer, int frame, int index, double amount);
    // Where the node of the layer stands in the Node View.
    void setNodePos(int layer, QPointF pos);
    // Which bone of its parent the layer rides on (-1: none).
    void setParentBone(int layer, int bone);
    // The joints of the bone chain of a drawing layer, at rest.
    // Which camera the scene is seen through.
    void setActiveCamera(int layer);
    // The Camera view shows that layer and what hangs under it (-1: the whole scene).
    void setDisplay(int layer);
    // The scene's write outputs, all at once.
    void setWrites(QVector<Scene::Write> const& writes);
    // The angle a camera sees, top to bottom (degrees).  A camera that has keyframes gets
    // the angle on a keyframe at `frame`, so that it can change in time; one that has none
    // simply has that angle.
    void setFov(int layer, double degrees, int frame = 0);
    void setBones(int layer, QVector<QPointF> const& joints, QString const& text);
    // The curve or envelope deformer of a drawing layer (see Layer::warp); no points takes it away.
    void setWarp(int layer, QVector<QPointF> const& points, bool closed, QString const& text);
    // Moves the pivot without moving the artwork at any keyframe.
    void setPivot(int layer, QPointF pivot);

    // ---- structure ----------------------------------------------------------------------
    int addLayer(int above);
    // Symbols.  Create: these layers, and what is attached to them, become a symbol that one layer shows in their
    // place (returns that layer, or -1).  Insert: another layer that shows a symbol.  Expand: the layer becomes
    // a peg with copies of the symbol's layers attached.
    int createSymbol(QVector<int> const& layers, QString const& name);
    int insertSymbol(int symbolId, int above);
    int expandSymbol(int layer);
    // A new symbol with one empty drawing layer, and a layer above `above` that shows it.  Returns that layer.
    int insertEmptySymbol(QString const& name, int above);
    // The layer gets a symbol of its own: a copy of the one it shows.
    bool duplicateSymbol(int layer);
    // Clone: a second layer that shows the same drawings (see Scene::cloneLayer).  Returns it, or -1.
    int cloneLayer(int layer);
    // Working inside a symbol: its layers become the scene that is worked on (with a list of things to undo of
    // its own), until it is left again - and every layer that shows the symbol shows what was done.
    bool enterSymbol(int symbolId);
    bool leaveSymbol();
    bool insideSymbol() const { return !m_around.isEmpty(); }
    // The symbols gone into, the outermost first, by name.
    QStringList symbolTrail() const;
    // The scene as it is to be saved: the outermost one, with what is being worked on inside symbols put back.
    Scene wholeScene() const;
    // Whether there is anything that has not been saved; and that it has just been saved.
    // Going into a symbol or out of one changes nothing that is saved: only other steps since the last save count.
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
    // Drawings brought in from outside (pictures, the frames of a movie): one after the other
    // from `frame`, each shown for `hold` frames, on `layer` or - when that is -1 - on a new
    // drawing layer in front called `name`.  The scene grows when they run past its end.
    // Returns the layer.
    int importDrawings(int layer, QString const& name, int frame, QVector<Drawing> const& drawings, int hold = 1);
    // The layers of a template (a piece cut from a scene: see Scene::extract) brought in from
    // `frame` on, as one undo step.  Returns the first of them, or -1.
    int insertTemplate(Scene const& piece, int frame);
    // The piece put onto the layers of the same names (Scene::pasteOnto); -1 when they are not all there.
    int pasteTemplateOnto(Scene const& piece, int frame, bool drawings = true, bool keys = true);
    // One drawing layer in the place of several: on each frame what they showed, where they
    // showed it (so the new layer needs no peg).  Returns its index; -1 with fewer than two
    // drawing layers.
    int mergeLayers(QVector<int> const& layers);
    // A bone.  When `current` is a bone the new one is attached to it and starts at its
    // tip; otherwise it stands on its own in the middle of the frame.
    int addBone(int current);
    // A quadmap just above `above`, its corners those of the frame.
    int addQuadmap(int above);
    void setCardColor(int layer, QColor const& color);
    void setBoneLength(int layer, double length);
    // Synced layers: a new drawing layer with the timing of `layer`, kept the same from then on; and a layer
    // synced with another (-1: with none any more).
    int addSyncedLayer(int layer);
    // Pose controllers (see Scene::Controller).  `controller` is its place in the scene's list.
    int addController(QString const& name, QVector<int> const& layers);
    void removeController(int controller);
    void renameController(int controller, QString const& name);
    // The poses (and drawings) the controller's layers have on `frame`, kept as a state; or put in place of one.
    int addControllerState(int controller, int frame, QString const& name);
    void replaceControllerState(int controller, int state, int frame);
    void removeControllerState(int controller, int state);
    // The controller set to `value` on `frame`: keyframes there for its layers, the poses blended between the two states
    // either side of the value, and the drawing of the nearer one shown.  One step to undo.
    void applyController(int controller, int frame, double value);
    // A 2D point controller (or a slider again).  Made one, states that all lie on the same place are spread round the
    // middle of the pad.
    void setControllerPoint(int controller, bool on);
    // Where a state lies on the pad of a 2D point controller (0..1 across and down).
    void moveControllerState(int controller, int state, QPointF at);
    // The 2D point set to `where` on `frame`: keyframes there for its layers, the poses of all the states blended by how
    // near each is (Scene::Controller::weightsAt), and the drawing of the nearest shown.  One step to undo.
    void applyControllerPoint(int controller, int frame, QPointF where);
    // Control points of a layer's motion path (see Scene).  `key` is the keyframe the stretch starts at.
    int insertControlPoint(int layer, int frame);
    void moveControlPoint(int layer, int key, int control, QPointF to);
    // Spline Offset: the whole motion path of the layer - every keyframe's place and every control point - moved by `by`
    // (in the space of what the layer hangs from).  False when it has no keyframes to move.
    bool offsetPath(int layer, QPointF by);
    void removeControlPoint(int layer, int key, int control);
    // How far a control point stands towards the camera: the path bends in depth through it.
    void setControlPointDepth(int layer, int key, int control, double z);
    int lockControlPointInTime(int layer, int key, int control);
    void setPathShape(int layer, int key, double tension, double continuity, double bias);
    // A clone of the layer: the same drawings and timing, changed on either; its place, or -1.
    int addCloneLayer(int layer, bool timing = true);
    void makeCloneIndependent(int layer);
    void syncLayerWith(int layer, int other);
    // IK constraints of a bone: left out of IK; and whether it is turned only between two angles, and which.
    void setIkConstraints(int layer, bool excluded, bool limited, double minimum, double maximum);
    // The four corners of a quadmap: top left, top right, bottom right, bottom left.
    // The corners of a quadmap.  One that has keyframes gets them on a keyframe at `frame`,
    // so that the pull can change in time; one that has none simply has those corners.
    void setQuad(int layer, QVector<QPointF> const& corners, int frame = 0);
    // A deformer with one point more, at place `index` (see Deform::insertWarpPoint): what
    // the keyframes did to the points after it stays with those points.
    void insertWarpPoint(int layer, QVector<QPointF> const& points, int index, double along);
    // The drawings of a layer made into what its deformer shows of them, frame by frame,
    // and the deformer taken away.  False when the layer has none.
    bool convertDeformed(int layer);
    // A layer moved as a whole, in all its keyframes alike, by what takes pose `from` to
    // pose `to` (animating switched off).  A layer without keyframes gets one, at `frame`.
    // (`first` to `last`: only the keyframes on those frames go along, the others stay.)
    void shiftPoses(int layer, int frame, Pose const& from, Pose const& to, QString const& text, int first = -1, int last = -1);
    // A sound layer in front, named after the file, starting on `startFrame`.
    int addSound(QString const& file, int startFrame);
    void setSoundStart(int layer, int frame);
    // Lip-sync (see Layer::lipsync): all the shapes of a sound layer at once; the shape of
    // one frame of the scene; and the drawings of a layer put on its frames to match - the
    // drawing `drawingFor[i]` wherever the sound has shape i (A, B ... X; -1: leave those
    // frames as they are).  False when there is nothing to map.
    void setLipSync(int layer, QString const& shapes, QString const& text);
    void setMouth(int layer, int frame, QChar shape);
    bool mapLipSync(int soundLayer, int target, QVector<int> const& drawingFor);
    // A new peg just above `layer`, taking its place under its parent, with `layer`
    // attached to it.  Returns the index of the peg.
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
    // Every layer at once.  `except` (when not -1) is left as it is and the others
    // changed: "lock all others", "disable all others".
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
    // Puts a block down from `frame` on, its rows onto these layers in turn (rows without
    // a layer, and frames past the end of the scene, are left out).
    void pasteCells(QVector<int> const& layers, int frame, CellBlock const& block);
    // Lifts the block out of `from` and puts it down from `frame` on, its rows onto `to` in
    // turn (-1: that row is dropped).  With `copy` the cells it came from stay as they were.
    void moveCells(QVector<int> const& from, int first, int last, QVector<int> const& to, int frame, bool copy);
    // One change made to several layers, as one undo step.
    void editLayers(QVector<int> const& layers, QString const& text, std::function<void(Layer&)> const& change);
    void removeLayers(QVector<int> const& layers);

    // ---- exposure, moving what follows --------------------------------------------------
    // One more cell of the drawing at `frame`; the cells after move along (the last one
    // of the layer falls off the end of the scene).
    void increaseExposure(int layer, int frame);
    // Key exposures.  Add: an exposure of its own begins at `frame`, of the drawing held there.  Remove: the
    // exposure `frame` is in is no more - the one before it is held over its cells (a key exposure of the same
    // drawing just stops being one).  Remove duplicates: between two frames, every key exposure that only
    // repeats the drawing before it.
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
    // Shows these drawings of the layer (places in its list; kNoDrawing for none) on the
    // frames from `first` on, one each.  Frames past the end, and drawings the layer has not, are left out.
    void setCells(int layer, int first, QVector<int> const& drawings, QString const& text);
    void setFrameCount(int count);
    void setGuides(QVector<Scene::Guide> const& guides, QString const& text);
    // The names and values kept with a layer, or - for -1 - with the scene.
    void setMetadata(int layer, QVector<QPair<QString, QString>> const& pairs, QString const& text);
    // The pictures of the Reference view.
    void setReferences(QStringList const& files, QString const& text);
    // A marker over `length` frames from `frame`; markers it would lie over give way.
    // Returns its place among the markers.
    int addMarker(int frame, int length, QString const& name, QColor const& color);
    void editMarker(int index, QString const& name, QColor const& color);
    bool setMarkerNote(int index, QString const& note);
    void removeMarker(int index);
    // Moves a marker so that it starts on `frame` (kept inside the scene).  False, and nothing
    // done, when it would lie over another marker or would not move.
    bool moveMarker(int index, int frame);
    void setLayerOnion(int layer, bool onion);
    // `count` empty frames before frame `at` (at == the number of frames: after the last);
    // drawings, keyframes and sounds from there on move along.
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
    QUndoStack m_undo;
};
