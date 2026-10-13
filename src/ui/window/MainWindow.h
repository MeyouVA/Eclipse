// Eclipse - the main window.  Its menus, toolbars and shortcuts come from the resource
// files through the action manager; the window is the "window" responder, and the slots
// below are what those files call.
#pragma once

#include "EclipseExport.h"

#include <QElapsedTimer>
#include <functional>
#include <QMainWindow>
#include <QTimer>

#include "audio/sound/SoundPlayer.h"

class ActionManager;
class Canvas;
class Document;
class LayerPanel;
class NodeView;
class QComboBox;
class QDockWidget;
class QListWidget;
class Xsheet;
class QLabel;
class QMenu;
class QPrinter;
class QSlider;
class QSpinBox;
class QToolBar;
class SubstitutionPanel;
class PalettePanel;
class Timeline;
class ViewTitleBar;

// What the Export Movie dialog was told: the frames (counted from 0), the size as a
// percentage of the scene's, and the quality (18 fine, 23 medium, 28 small).
struct MovieChoices {
    int first = 0;
    int last = -1;
    int percent = 100;
    int quality = 18;
};

class ECLIPSE_UI_API MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

    bool openFile(QString const& path);
    // The frames playback runs between (the first is 0).  `stop` -1 is the end of the scene.
    void setPlayRange(int start, int stop);
    int playStart() const { return m_playStart; }
    // How fast playing goes, as a percentage of the scene's own rate (sound only at 100).
    int playbackSpeed() const { return m_playSpeed; }
    void setPlaybackSpeed(int percent);
    int playStop() const;
    // Starts an empty scene of this camera size and saves it as `path`, making the
    // folder it goes in.
    bool createScene(QString const& path, int width, int height, double fps);
    ActionManager* actions() const { return m_actions; }

    // ---- workspaces: named arrangements of the views ------------------------------------
    // The ones that come with the program, then the ones the user saved.
    QStringList workspaces() const;
    QString currentWorkspace() const { return m_workspace; }
    // Remembers the arrangement of the current workspace and shows that of `name`.
    void switchWorkspace(QString const& name);
    // Puts the docks and toolbars back where the user left them last time.
    void restoreWorkspace();

    // The file a new version of `path` is saved as: name_v2, name_v3 ...
    static QString nextVersionPath(QString const& path);
    // Saves a copy of the scene beside it; false for a scene never saved.
    bool backupScene(QString* madePath);

    // ---- bringing things in and writing things out (the dialogs call these) ----------------
    struct ImageImport {
        QStringList files;
        bool newLayer = true;       // else onto the current layer, from the current frame
        QString layerName;          // of the new layer; empty: the name of the first file
        bool fit = true;            // made to fit the frame; else at its own size, in the middle
        bool pan = false;           // (with fit) made to fill the frame instead: what does not fit hangs over, to pan across
        QColor paper = QColor(255, 255, 255);   // traced in colour: the colour left out as paper (invalid: none is)
        bool vectorize = false;     // the dark parts become painted shapes; else the picture is kept as a bitmap
        int threshold = 128;        // how dark is dark (0..255)
        bool centreLines = false;   // (vectorising in black and white) lines down the middle of the strokes, not shapes round them
        int colours = 0;            // when vectorising: 0 for black and white, else flat shapes of at most this many colours
        bool linked = false;        // read from the file each time, not kept in the scene
        int hold = 1;               // frames each picture is shown for
    };
    // ---- the Library ------------------------------------------------------------------------
    // The layers selected in the Timeline (with everything attached to them), over the frames
    // selected - or the whole scene when no frames are - written as a template file.
    bool saveTemplate(QString const& path, QString* error);
    // The layers of a template file brought into the scene from `frame` on.  Returns the
    // first of them, or -1 with a message.
    int insertTemplate(QString const& path, int frame, QString* error);
    // Record and Play: the frames playing runs between, written as a small movie in the temporary folder.
    // The file, or nothing (with a message, unless it was stopped).
    QString recordForPlay(QString* error);
    class LibraryView* library() const { return m_library; }
    // The files the scene reads from outside itself (linked pictures, sounds), each once;
    // with `missingOnly`, those that are not where the scene expects them.
    QStringList sceneFiles(bool missingOnly) const;
    // Takes up what Edit > Preferences keeps: how the Select tool takes things, how often
    // the scene is saved by itself (0: never).
    void applyPreferences();
    // A drawing layer over the current one, called `name` (when that is not empty); with
    // `wholeScene`, an empty drawing in it held from the first frame to the last.  Its place.
    int addDrawingLayer(QString const& name, bool wholeScene);
    int autoSaveMinutes() const { return m_autoSaveMinutes; }
    // Pictures to look at while drawing, shown in the Reference view (which comes up).
    // Returns how many could be read.
    int addReferences(QStringList const& files);
    // Returns the layer they went to, or -1 with a message.
    int importImages(ImageImport const& options, QString* error);
    // The frames of a movie as pictures on a new layer, and its sound on a sound layer.
    // Needs ffmpeg, and a scene that has been saved (the frames are kept beside it).
    int importMovie(QString const& path, QString* error);
    struct ImageExport {
        QString folder;
        QString name;               // files are name_0001.png ...
        QString format = QStringLiteral("png");
        int first = 0, last = -1;   // frames; -1: the last of the scene
        int percent = 100;          // of the scene's resolution
        bool preview = false;       // quick: no smoothing of edges
    };
    // Returns how many were written; fewer than asked means it stopped, with a message.
    int exportImages(ImageExport const& options, QString* error);
    // A frame of the scene on a page, as large as the page takes it.
    bool printFrame(QPrinter& printer, int frame);
    // The drawing of the current cell on every frame selected (of the current layer); and the
    // drawings of the layer in order from `start` (a place in its list), each held for `hold`
    // frames, going on by `increment`.  Each returns how many frames it filled.
    int fillSelection();
    int sequenceFill(int start, int increment, int hold, bool cycle);
    // The sound of the scene, mixed, as a WAV file.  False when there is none or it cannot be written.
    bool exportAudio(QString const& path, QString* error);

    // The tool presets of your own, written to a file and read from one (those already
    // there by the same name and tool are not doubled).  The number written or taken in; -1 on failure.
    int exportPresets(QString const& path) const;
    int importPresets(QString const& path);

    // Lip-sync: listens to the sound of a sound layer and gives each of its frames a mouth
    // shape.  The number of frames lettered; -1 when the layer has no sound that can be read.
    int detectLipSync(int soundLayer);

public slots:
    // ---- File ----
    void onActionNewScene();
    void onActionOpenScene();
    bool onActionSave();
    bool onActionSaveAs();
    void onActionExportFrames();
    void onActionExportMovie();
    void onActionImportSound();
    void onActionMoveSoundHere();
    void onShowRecent(QMenu* menu);

    // ---- Edit ----
    void onActionUndo();
    void onActionRedo();
    void onActionEditShortcuts();

    // ---- View / Windows ----
    void onActionToggleFullScreen(bool on);
    void onActionRestoreDefaultWorkspace();
    void onShowWorkspaces(QMenu* menu);
    void onActionSaveWorkspaceAs();
    void onActionDeleteWorkspace();
    void onShowToolbars(QMenu* menu);
    void onShowWindows(QMenu* menu);
    void onShowCameras(QMenu* menu);
    void onActionResetAllExceptZ();

    // ---- Play ----
    void onActionTogglePlayback();
    void onActionToggleLoop(bool on) { m_loop = on; }
    // Play > Enable Playback: whether the Top, Side or Perspective view plays along (they stand still otherwise,
    // and show the frame again when playing stops).
    void onActionEnablePlayback(QString const& which);
    // Drawing > Create Stamp from Selected Drawings: the drawings on the frames selected in the Timeline, of the
    // current layer, become one stamp that puts them down in turn.
    void onActionCreateStampFromDrawings();
    // Edit > Symbol.
    void onActionCreateSymbol();
    void onActionCreateEmptySymbol();
    void onActionInsertSymbol();
    void onActionEditSymbol();
    void onActionLeaveSymbol();
    void onActionDuplicateSymbol();
    void onActionCloneLayer();
    void onActionAddSyncedLayer();
    void onActionSyncLayerWith();
    void onActionExpandSymbol();
    bool playsIn(QString const& which) const { return m_playViews.contains(which); }
    void onActionToggleSound(bool on) { m_soundOn = on; }
    void onActionFirstFrame();
    void onActionPreviousFrame() { stepFrame(-1); }
    void onActionNextFrame() { stepFrame(+1); }
    void onActionLastFrame();
    void onActionGotoFrame();
    void onActionPlaybackStartFrame();
    void onActionCloseScene();
    void onActionCreateTemplate();
    void onActionInsertTemplate();
    void onActionUngroupLayers();
    void onActionMoveToParentGroup();
    void onActionPreferences();
    void onActionCheckFiles();
    void onActionElementManager();
    void onActionStopPlayback() { stop(); }
    void onActionPlaybackSpeed();
    void onActionImportImages() { askImportImages(false); }
    void onActionLinkImages() { askImportImages(true); }
    void onActionImportMovie();
    void onActionImportReference();
    void onActionExportGif();
    void onActionExportOpenGLFrames() { askExportImages(true); }
    void onActionExportAudio();
    void onActionDeleteDrawing();
    void onActionRenameDrawing();
    void onActionRenameByFrame();
    void onActionMarkDrawing(QString const& as);
    bool onActionSaveNewVersion();
    void onActionBackupScene();
    void onActionSelectPreviousSibling() { stepSibling(+1); }
    void onActionSelectNextSibling() { stepSibling(-1); }
    void onActionResetAll();
    void onActionCreateSceneMarker();
    void onActionEditSceneMarker();
    void onActionDeleteSceneMarker();
    void onActionToggleLayerOnion();
    void onActionMergeLayers();
    void onActionPasteCycle();
    void onActionPasteReverse();
    void onActionPasteSpecial();
    void onActionCreateCycle();
    void onActionAddFramesAtStart();
    void onActionAddFramesAtEnd();
    void onActionAddFramesBeforeSelection();
    void onActionAddFramesAfterSelection();
    void onActionRemoveSelectedFrames();
    void onActionPreviousLayer() { stepLayer(-1); }
    void onActionNextLayer() { stepLayer(+1); }
    void onActionPreviousColour() { stepColour(-1); }
    void onActionNextColour() { stepColour(+1); }
    void onActionPlaybackStopFrame();

    // ---- Insert / layers ----
    void onActionAddDrawingLayer();
    void onActionAddPeg();
    void onActionAddCamera();
    void onActionAddParentPeg();
    void onActionAddColourCard();
    void onActionAddGroup();
    void onActionAddBone();
    void onActionAddQuadmap();
    void onActionGroupSelection();
    void onActionDeleteLayer();
    void onActionRenameLayer();
    void onActionMoveLayerUp();
    void onActionMoveLayerDown();
    void onActionAttachTo();
    void onActionDetach();
    void onActionDuplicateLayer();
    void onActionLockLayer() { setLocked(true); }
    void onActionUnlockLayer() { setLocked(false); }
    void onActionLockAll();
    void onActionTagLayers(QString const& how);
    void onActionTimelineViewMode(QString const& mode);
    void onActionTimelineShowKind(QString const& which);
    void onActionUnlockAll();
    void onActionLockAllOthers();
    void onActionToggleLayerEnabled();
    void onActionEnableAll();
    void onActionDisableAllOthers();
    void onActionToggleSolo();
    void onActionCollapseExpand();
    void onActionCollapseAll();
    void onActionExpandAll();
    void onActionChangeTrackColour();
    void onActionDefaultTrackColour();

    // ---- Scene ----
    void onActionSceneLength();
    void onActionSceneSettings();

    // ---- Drawing ----
    void onActionChooseTool(QString const& name);
    void onActionCreateEmptyDrawing();
    void onActionDuplicateDrawing();
    void onActionPreviousDrawing() { stepDrawing(-1); }
    void onActionNextDrawing() { stepDrawing(+1); }
    void onActionExtendExposure();
    void onActionClearExposure();
    void onActionIncreaseExposure();
    void onActionDecreaseExposure();
    void onActionAddKeyExposure();
    void onActionRemoveKeyExposure();
    void onActionRemoveDuplicateKeyExposure();
    void onActionSetExposure(int length);
    void onActionInsertBlankCell();
    void onActionClearExposureAndPull();
    void onActionFillEmptyCells();

    // ---- Animation ----
    void onActionInsertKeyframe();
    void onActionDeleteKeyframe();
    void onActionSetMotionKeyframe();
    void onActionSetStopMotionKeyframe();
    void onActionGotoPreviousKeyframe() { stepKeyframe(-1); }
    void onActionGotoNextKeyframe() { stepKeyframe(+1); }
    void onActionResetTransformation();
    void onActionSubstitutePrevious();
    void onActionSubstituteNext();
    void onActionSelectParent();
    void onActionSelectChild();
    // Every layer attached to the current one, and those attached to them: all picked together.
    void onActionSelectChildren();
    // Morphing between the key drawing of the current cell and the next one of the layer.
    void onActionCreateMorphing();
    void onActionAutoLipSync();
    void onActionMapLipSync();
    void onActionClearLipSync();
    void onActionMouthShape(QString const& shape);
    void onActionRemoveDeformer();
    void onActionCreateEnvelope();
    void onActionConvertDeformed();
    void onActionExportPresets();
    void onActionImportPresets();
    void onActionResetDeformer();
    void onActionDeleteMorphing();
    void onActionConvertMorphing();
    void onActionMorphingEase();
    void onActionPrint();
    void onActionFillSelection();
    void onActionSequenceFill();
    void onActionHoldExposure();
    // Quick ease on the keyframes of the layers and frames selected: "in", "out", "both" or "none".
    void onActionSetEase(QString const& which);
    void onActionShowLayerProperties();
    void onActionShowMessageLog();
    // Help > Debug Mode: the Message Log also tells every command that is run, how long it took, and every step
    // that goes onto the list of things to undo.
    void onActionDebugMode(bool on);
    bool debugMode() const { return m_debugMode; }
    void onActionHelp();
    void onActionSceneMarkerNote();
    void onActionSetEaseDialog();
    // The ease of the keyframes on the frames and layers selected (0 to 1 each; below 0: left as it is).  How many were changed.
    int setEaseOnSelection(double easeIn, double easeOut);
    // Breakdown Assistant: a keyframe on the current frame of the current layer, so many per cent of the way
    // from the keyframe before it to the keyframe after it (below 0 or over 100: beyond them).  False when
    // the frame does not lie between two keyframes.
    bool applyBreakdown(double percent);
    void onActionMarkFrame(QString const& colour);
    void onActionLayerKeys(QString const& how);
    void onActionPasteSpecialAgain();
    void onActionRenameWithPrefix();
    void onActionOnionLayers(QString const& which);
    void onActionTransformMode(QString const& mode);
    void onActionRenderAndPlay();
    void onActionMiniPlayback();
    void onActionRecordAndPlay();
    void onActionMiniPlaybackRange();
    void onActionKeyframeAndDuplicate();
    void onActionToggleSoundScrubbing(bool on) { m_scrub = on; }
    void onActionToggleWaveforms(bool on);
    // Turns the current layer over on the current frame (a keyframe with the scale reversed).
    void onActionFlipHorizontalAnimation();
    void onActionFlipVerticalAnimation();
    // The size in hand, kept under a name among the presets of the tool.
    void onActionNewToolPreset();
    void onActionManagePresets();
    // How strongly the onion skin shows.
    void onActionTopLight();

    // ---- Help ----
    void onActionAbout();
    void onActionShowWelcome();

protected:
    void closeEvent(QCloseEvent*) override;
    bool eventFilter(QObject*, QEvent*) override;

private:
    void createViews();
    // The parts of that: a view put in a panel of its own, and the views that take some making.
    QDockWidget* addView(QString const& title, QString const& name, QWidget* content, Qt::DockWidgetArea area);
    void createCameraView();
    void createTimelineView();
    void createWindowsMenuViews();
    QWidget* createOnionSkinView();
    QWidget* createMessageLogView();
    QWidget* createSceneMarkersView();
    QWidget* createBreakdownView();
    void createToolbars();
    static QStringList builtinWorkspaces();
    // Arranges the views the way a workspace that comes with the program has them.
    void applyBuiltinWorkspace(QString const& name);
    void loadWorkspace(QString const& name);
    void saveCurrentWorkspace();
    void refreshWorkspaceBox();
    QDockWidget* dockOf(char const* objectName) const;
    // Brings the tabs across the top of every panel up to date with where the views are.
    void refreshViewTabs();
    // The collapse arrows: a pair at each edge of the middle view that has panels beyond it, and a
    // pair on the line between two panels of the same side.  Each arrow pushes the line its own
    // way: it folds away what lies on the side it points to (the panels of that side, the middle
    // view, one of the two panels), or brings back what was folded away on the other side.
    enum Side { LeftSide, RightSide, TopSide, BottomSide, SideCount };
    void createCollapseArrows();
    void placeCollapseArrows();
    void placeBetweenArrows();
    void pressSide(int side, bool second);
    void pressBetween(int spot, bool second);
    void toggleSide(int side);
    void setMiddleCollapsed(bool collapsed);
    void expandAllSides();
    class CollapseHandle* m_collapse[SideCount] = {nullptr, nullptr, nullptr, nullptr};
    QList<QDockWidget*> m_collapsed[SideCount];        // the panels a press hid, to bring back
    QList<QDockWidget*> m_collapsedFront[SideCount];   // and which of them were in front of their tabs
    bool m_middleCollapsed = false;
    QHash<QDockWidget*, QSize> m_sizesBeforeMiddle;     // how big the panels were with the middle view there
    // A panel (with those on its tabs) folded away from between two: which, which of them was in
    // front, the neighbour its arrows stay by, and whether it lay before that neighbour.
    struct Fold {
        QList<QDockWidget*> docks, front;
        QDockWidget* anchor = nullptr;
        bool before = false;
        bool stacked = true;
        int size = 0, anchorSize = 0;       // how much room the two had along the way they lie
    };
    QList<Fold> m_folds;
    // What each pair of arrows between panels stands for: two panels there to be seen, or a fold.
    struct BetweenSpot {
        QDockWidget* a = nullptr;
        QDockWidget* b = nullptr;
        int fold = -1;
    };
    QList<BetweenSpot> m_betweenSpots;
    QList<class CollapseHandle*> m_betweenHandles;
    bool m_collapsing = false;
    bool m_placePending = false;
    // Marks the view that was clicked in last: it is outlined.
    void setActiveView(QWidget* frame);
    void updateTitle();
    void updateStatus();
    // Greys out the commands that have nothing to act on just now.
    void updateActionStates();
    // Asks how many, then puts that many empty frames in before frame `at`.
    void addFrames(int at);
    void stepLayer(int direction);
    void askImportImages(bool linked);
    QWidget* makeReferenceView();
    QListWidget* m_references = nullptr;
    class LibraryView* m_library = nullptr;
    // (`onto`: put on the layers of the same names when the scene has them all - a drop on the cells of the Timeline.)
    void dropTemplate(QString const& path, int frame, bool onto = false);
    void askExportImages(bool preview);
    void exportMovieTo(QString const& path, struct MovieChoices const* choices = nullptr);
    void syncReferences();
    // The sound of one frame, heard.
    void playFrameSound(int frame);
    int m_envelopePoints = 8;      // how many points Create Envelope puts round a drawing
    // Paste Special as it was last asked for (Paste Special Again does the same without asking).
    bool m_pasteDrawings = true, m_pasteKeys = true, m_pasteReverse = false;
    int m_pasteCycles = 1;
    bool m_envelopeOutline = true; // ... round the outline of the artwork itself, not the hull round it
    QStringList m_referenceFiles;
    // Brings a view up: back where it was, or as a tab behind Tool Properties.
    void showView(QDockWidget* dock);
    void stepSibling(int direction);
    void stepColour(int direction);
    // The exposure commands act on every layer of the block selected, from its first frame.
    void onExposure(QString const& text, std::function<void(int layer, int frame)> const& command);
    int layer() const;
    int frame() const;
    Q_INVOKABLE void shuttleStep();     // one tick of the shuttle

    bool saveTo(QString const& path);
    // True when it is fine to throw the current scene away (saved, or the user said so).
    bool confirmDiscard();
    void addRecent(QString const& path);

    void stop();
    void showFrameInStillViews();
    void advanceFrame();
    void stepFrame(int delta);
    void stepDrawing(int direction);
    void stepKeyframe(int direction);
    void renameLayer(int layer);
    void setLocked(bool locked);
    void pickTrackColour(int layer);

    ActionManager* m_actions;
    Document* m_document;
    Canvas* m_canvas;
    Timeline* m_timeline;
    PalettePanel* m_palette = nullptr;
    QString m_path;
    QList<QDockWidget*> m_docks;
    QList<ViewTitleBar*> m_titleBars;
    QWidget* m_activeView = nullptr;
    bool m_tabsStale = false;
    QList<QToolBar*> m_toolbars;   // those of the window itself, not of a view
    QString m_workspace = QStringLiteral("Default");
    bool m_persistWorkspaces = false;  // set once the saved arrangement has been restored
    QComboBox* m_workspaceBox = nullptr;
    Xsheet* m_xsheet = nullptr;
    LayerPanel* m_layerPanel = nullptr;
    NodeView* m_nodeView = nullptr;
    SubstitutionPanel* m_substitution = nullptr;

    QTimer m_playTimer;
    bool m_loop = true;
    bool m_debugMode = false;
    QStringList m_playViews;
    // Mini Playback: the few frames either side of one, over and over; what to go back to when it stops.
    bool m_mini = false;
    int m_miniFrame = 0, m_miniStart = 0, m_miniStop = -1;
    bool m_miniLoop = true;
    bool m_soundOn = true;
    bool m_scrub = false;           // a frame of sound is heard when the frame is changed by hand
    QVector<qint16> m_scrubMix;     // the sound of the scene for that; emptied when the scene changes
    // Playback follows the clock, not a count of timer ticks: a slow frame is skipped
    // rather than putting the picture behind the sound.
    QElapsedTimer m_playClock;
    int m_playFrom = 0;
    QVector<qint16> m_playMix;
    SoundPlayer m_player;
    void startPlayingFrom(int frame);
    QSpinBox* m_frameBox = nullptr;
    QSpinBox* m_startBox = nullptr;
    QSpinBox* m_stopBox = nullptr;
    QComboBox* m_fpsBox = nullptr;
    QSlider* m_jog = nullptr;
    // The shuttle: held to one side it runs through the frames, the faster the further; let go, it springs back.
    QSlider* m_shuttle = nullptr;
    QTimer m_shuttleTimer;
    double m_shuttleAt = 0.0;
    int m_playStart = 0;
    int m_autoSaveMinutes = 0;
    QTimer m_autoSave;
    int m_playSpeed = 100;
    int m_playStop = -1;   // -1: the end of the scene, however long it gets
    QLabel* m_statusLabel = nullptr;   // the frame, and where the pointer is in fields
    QLabel* m_drawingLabel = nullptr;  // the drawing under the playhead: "Drawing-1", or "Drawing Empty Cell"
    QLabel* m_colourLabel = nullptr;   // the colour in hand, by name ...
    QLabel* m_colourPatch = nullptr;   // ... and a patch of it
    QPointF m_pointer;                 // where the pointer is over the Camera view, in fields
    bool m_pointerKnown = false;
    class QToolButton* m_zoomLabel = nullptr;   // in the strip under the Camera view
    QLabel* m_messageLabel = nullptr;  // the tool in use, or something the user should know
    QString m_toolName;
    std::function<void()> m_refreshToolProperties;
    // "brush", "pencil" or "eraser" for the tools that have presets; else empty.
    QString presetKind() const;
};
