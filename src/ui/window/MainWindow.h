// Eclipse - the main window.  Its menus, toolbars and shortcuts come from the resource
// files through the action manager; the window is the "window" responder, and the slots
// below are what those files call.
#pragma once

#include "EclipseExport.h"

#include <QPointer>
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
class QCheckBox;
class QTimer;
class QToolBar;
class SubstitutionPanel;
class PalettePanel;
class ColourEditor;
#include "ui/timeline/Timeline.h"
class ViewTitleBar;

struct MovieChoices {
    int first = 0;
    int last = -1;
    int percent = 100;
    int quality = 18;
    int codec = -1;         // as MovieExport::Options
    int speed = 1;
    int audioKbps = 192;
    bool transparentBackground = false;
};

class ECLIPSE_UI_API MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    // (Lets go of the application's signals first: nothing reaches a window half taken apart.)
    ~MainWindow() override;

    bool openFile(QString const& path);
    QJsonObject projectUiState() const;
    void restoreProjectUi(QJsonObject const& state);
    // The frames playback runs between (the first is 0).  `stop` -1 is the end of the scene.
    void setPlayRange(int start, int stop);
    int playStart() const { return m_playStart; }
    // How fast playing goes, as a percentage of the scene's own rate (sound only at 100).
    int playbackSpeed() const { return m_playSpeed; }
    void setPlaybackSpeed(int percent);
    int playStop() const;
    bool createScene(QString const& path, int width, int height, double fps, double fov = 0.0);
    ActionManager* actions() const { return m_actions; }

    // ---- workspaces: named arrangements of the views ------------------------------------
    // The ones that come with the program, then the ones the user saved.
    QStringList workspaces() const;
    QString currentWorkspace() const { return m_workspace; }
    // Remembers the arrangement of the current workspace and shows that of `name`.
    void switchWorkspace(QString const& name);
    bool renameWorkspace(QString const& name, QString const& to);
    QString duplicateWorkspace(QString const& name);
    bool deleteWorkspace(QString const& name);
    bool moveWorkspace(QString const& name, int by);
    // Whether a workspace is in the toolbar's list (all of them are, to begin with).
    bool workspaceInToolbar(QString const& name) const;
    void setWorkspaceInToolbar(QString const& name, bool shown);
    // Preferences: the arrangement kept as it is left (on), or only when saved (off: asked on quitting if it changed).
    bool autoSavesWorkspace() const { return m_autoSaveWorkspace; }
    // Preferences: an undo (or redo) of a change to another drawing goes to that drawing.
    bool restoresDrawingOnUndo() const { return m_restoreDrawingOnUndo; }
    // Scene > Remove Unused Files: the files of the scene's media folder that the scene no longer uses (to the Recycle Bin).
    Q_INVOKABLE void onActionRemoveUnusedFiles();
    QStringList unusedFiles() const;
    // File > Manage Versions: the scene's versions (name, name_v2, name_v3 ...) - opened, or deleted.
    Q_INVOKABLE void onActionManageVersions();
    QStringList sceneVersions() const;
    // Scene Markers: a marker of one frame on the current frame.
    Q_INVOKABLE void onActionMarkCurrentFrame();
    // File > Delete Backup Files: the scene's backup copies (name.ecl~, name_backup_*) to the Recycle Bin.
    Q_INVOKABLE void onActionDeleteBackupFiles();
    QStringList backupFiles() const;
    // Preferences: a red border round the view in use.
    void setCurrentViewBorder(bool on);
    bool currentViewBorder() const;
    void setCurrentViewBorderColour(QColor const& colour);
    QColor currentViewBorderColour() const;
    // Preference sets: every preference saved to a file (an ini file), loaded back, or all put back as they were at first.
    bool savePreferenceSet(QString const& path) const;
    bool loadPreferenceSet(QString const& path);
    static QStringList languages();
    static bool installLanguage(QString const& code);
    void restoreDefaultPreferences();
    // The Style Editor: the user's additions to the look.
    void showStyleEditor();
    // Whether the arrangement is not as the workspace was when it was opened or last saved.
    bool workspaceChanged() const;
    void holdBackFloating();
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
    int insertTemplate(QString const& path, int frame, QString* error);
    // The template put in as a symbol (its layers in the symbol, one layer showing it); that layer, or -1.
    int insertTemplateAsSymbol(QString const& path, int frame, QString* error);
    int pasteTemplateSpecial(QString const& path, int frame, bool onto, bool drawings, bool keys, bool extend, QString* error);
    // Library > Paste Special...: the window that asks how.
    void onTemplatePasteSpecial(QString const& path);
    QString recordForPlay(QString* error);
    class LibraryView* library() const { return m_library; }
    QStringList sceneFiles(bool missingOnly) const;
    void applyPreferences();
    // The Camera view made to show a scene just made or opened, as the preferences say (fitted, or at its own size).
    void placeCameraView();
    int addDrawingLayer(QString const& name, bool wholeScene);
    QWidget* planningPanel(QWidget* view, std::function<void(bool)> const& showCamera, std::function<void()> const& resetView);
    int autoSaveMinutes() const { return m_autoSaveMinutes; }
    // The view in the middle (Camera / Drawing) out in a window of its own, and back into the middle.
    void setCameraFloating(bool floating);
    bool cameraFloating() const { return m_cameraWindow != nullptr; }
    void setCameraDocked(Qt::DockWidgetArea area, bool tabbed = false);
    Qt::DockWidgetArea cameraDockArea() const;
    // Shown at all: Close View hides it, Windows > Camera brings it back.
    void setCameraShown(bool shown);
    bool cameraShown() const { return cameraFloating() || m_cameraDock || !m_middleCollapsed; }
    int addReferences(QStringList const& files);
    // Returns the layer they went to, or -1 with a message.
    int importImages(ImageImport const& options, QString* error);
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
    int fillSelection();
    int sequenceFill(int start, int increment, int hold, bool cycle);
    // The sound of the scene, mixed, as a WAV file.  False when there is none or it cannot be written.
    bool exportAudio(QString const& path, QString* error);

    int exportPresets(QString const& path) const;
    int importPresets(QString const& path);

    int detectLipSync(int soundLayer);
    // Preferences: the frames the detected mouth shapes are shown before they are heard.
    int lipSyncLead() const;
    // The same, from the words said (see LipSync::fromText); the text is kept for the next time.
    int lipSyncFromWords(int soundLayer, QString const& words);

public slots:
    void onActionCameraCone(bool on);
    // The Colour Editor, on the colour in hand (Palettes > Colour Editor, or a double click on a colour).
    void showColourEditor();
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
    void onActionSaveWorkspace();
    void onActionWorkspaceManager();
    void onShowToolbars(QMenu* menu);
    void onShowWindows(QMenu* menu);
    void onShowCameras(QMenu* menu);
    void onActionResetAllExceptZ();
    void onActionSet3DAnimationTiming();
    void onActionSnapshot3D();
    void onActionUpdate3DModelPosition();
    void set3DAnimationTiming(int layer, int first, int hold);

    // ---- Play ----
    void onActionTogglePlayback();
    void onActionToggleLoop(bool on) { m_loop = on; }
    void onActionEnablePlayback(QString const& which);
    void onActionCreateStampFromDrawings();
    // Edit > Symbol.
    void onActionCreateSymbol();
    void onActionCreateEmptySymbol();
    void onActionInsertSymbol();
    void onActionEditSymbol();
    void onActionLeaveSymbol();
    void onActionDuplicateSymbol();
    void onActionCloneLayer();
    void onActionCloneLayerTiming();
    void onActionInsertControlPoint();
    void onActionInsertPositionKeyframe();
    void onActionFlipScale(QString const& axis);
    void onActionStopMotionMode(bool on);
    void onActionSnapKeyframe(bool on);
    void onActionToggleLineCurve();
    void onActionSelectPathPoint(QString const& which);
    void onActionSelectionKeyframes(QString const& what);
    void onActionCaptureMotion();
    void onActionColumnProperties();
    void onShowDisplays(QMenu* menu);
    // Runs a script kept by the Script Editor, by name.
    void runScript(QString const& name);
    // Map Lip-Sync for one sound layer: the window that asks which drawings are the mouths, and the mapping.
    bool mapLipSyncFor(int sound);
    void setDrawingWindowShown(bool on);
    // What a workspace keeps besides the panels: whether the Camera view floats, and the Drawing window, and where they are.
    void saveWorkspaceExtras(QString const& name);
    int renderWrites(QString* error);
    void onActionRenderWrites();
    // Scene > Write Outputs...: the outputs listed, to add to, change and take away.
    void onActionWriteOutputs();
    void loadWorkspaceExtras(QString const& name);
    class Canvas* drawingWindowView() const;
    // The shortcuts and Scripts toolbar buttons the scripts were given.
    void refreshScriptCommands();
    void onActionOpenPreviewManager();
    // Camera and scene planning views: hide the selected layers from them, or show again all that were hidden.
    void onActionHideSelectedInViews();
    void onActionShowHiddenInViews();
    void refreshViewHidden();
    void onActionRenderPreview();
    void onShowScripts(QMenu* menu);
    void onActionAutoRender(bool on);
    void onActionRecalculateAll();
    void onActionCancelPreviewRender();
    void onActionMakeCloneIndependent();
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
    // Pages scanned (the system's scanning dialog), then imported as images are.
    void onActionImportScanner();
    // A 3D model (Wavefront OBJ) on a layer of its own - several files: one a frame, a 3D animation.
    void onActionImport3DModel();
    // `files`: the models, one after the other; returns the layer, or -1 (`error` says why).
    int import3DModels(QStringList const& files, QString* error = nullptr);
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
    void onActionPasteInPlace();
    void onActionCreateCycle();
    void onActionAddFramesAtStart();
    void onActionAddFramesAtEnd();
    void onActionAddFramesBeforeSelection();
    void onActionAddFramesAfterSelection();
    void onActionRemoveSelectedFrames();
    void onActionPreviousLayer() { stepLayer(-1); }
    void onActionNextLayer() { stepLayer(+1); }
    // Go to Previous / Next Column: the column of the Xsheet to the left or right of the current one.
    void onActionPreviousColumn() { stepColumn(-1); }
    void onActionNextColumn() { stepColumn(+1); }
    void stepColumn(int direction);
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
    // File > Export > SWF: the scene as a Flash movie of vector shapes.
    void onActionExportSwf();
    // File > Import > SWF: a Flash movie, a layer on a peg for each depth of it (or its frames as the drawings of one layer).
    void onActionImportSwf();
    // The same, from a file; the layer made (the peg over them all, `asLayers`), or -1 (and why).
    int importSwf(QString const& path, QString* error = nullptr, bool asLayers = false, int* layers = nullptr, bool groups = false);
    // A Flash movie's layers on pegs saved as a template in the Library's folder: the template's file, or empty (and why).
    QString importSwfToLibrary(QString const& path, QString* error = nullptr, bool groups = false);
    // File > Import > FLA: each layer of a Flash document's main timeline as a layer, frame by frame.
    void onActionImportFla();
    // The same, from a file; how many layers were made, or -1 (and why).
    int importFla(QString const& path, QString* error = nullptr);
    void onActionSubstituteSameSet(QString const& direction);
    void onActionSubstituteLayer(QString const& direction);
    void onActionSetDrawingLayer();
    // Skins: a drawing layer applied to the whole scene or the layers selected, on all frames or those of a scene marker.
    void onActionSkins();
    // The layers selected (or the current one) that follow others: their driven poses made keyframes, the drivers gone.
    void onActionBakeDrivers();
    // Lock Timeline Ruler (on or off; kept with the preferences).
    void onActionLockTimelineRuler();
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
    // Skipping effects: with the Node View's parenting followed, past effect nodes; otherwise as Select Parent and Select Child.
    void onActionSelectParentSkippingEffects();
    void onActionSelectChildSkippingEffects();
    // Animation > Select Group Content: what is in the current group layer, picked in the Timeline.
    void onActionSelectGroupContent();
    // File > Print Node View...: the Node View as it is at its level, on paper.
    void onActionPrintNodeView();
    // The Node View menu's switches made to show how the view is (they can be changed by its own keys too).
    void syncNodeViewChecks();
    // Every layer attached to the current one, and those attached to them: all picked together.
    void onActionSelectChildren();
    // The layers selected hidden; or shown, and every other one hidden - one step of undo each.
    void onActionHideSelection();
    void onActionShowSelectionHideOthers();
    // Morphing between the key drawing of the current cell and the next one of the layer.
    void onActionCreateMorphing();
    void onActionAutoLipSync();
    // The line that is said, typed: its words' mouth shapes laid over the stretches of speech in the sound.
    void onActionLipSyncFromWords();
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
    void onActionSetExposureAsk();
    void onActionEasyFlip(bool on);
    void onActionInsertInbetween();
    void onActionSubstituteFrame(QString const& direction);
    void onActionGoToDrawing(QString const& which);
    bool cycleDialog(QString const& title, QString const& label, int least, int* times, int* kind);
    void onActionHoldCells(QString const& every);
    void onActionFillCellsRandomly();
    void onActionExtendExposureTo();
    void onActionExtendExposureToEnd();
    // Quick ease on the keyframes of the layers and frames selected: "in", "out", "both" or "none".
    void onActionSetEase(QString const& which);
    void onActionShowLayerProperties();
    // Editor...: the properties of the current layer shown, or put away again when they are in front.
    void onActionToggleEditor();
    // Node View > Select in Camera...: the next layer clicked in the Camera view is attached to (or cabled into) the node picked.
    void onActionSelectInCamera();
    // Pictures dropped on the Camera view: a layer each, centred where they were let go (one step to undo).
    int dropPicturesOnCamera(QStringList const& files, QPointF scenePos);
    void onActionShowMessageLog();
    void onActionDebugMode(bool on);
    bool debugMode() const { return m_debugMode; }
    void onActionHelp();
    void onActionSceneMarkerNote();
    void onActionSetEaseDialog();
    // Quick Ease (in and out), Quick Ease In, Quick Ease Out: 33 % on the keyframes selected.
    void onActionQuickEase(QString const& which);
    void onActionSetEaseMultiple();
    void onActionApplyEaseToSelection();
    void onActionVelocityEditor(bool on);
    // Function columns: the Xsheet's current value column sent to the Function view, its column in the Function Editor,
    // converted; a layer's function columns shown or hidden in the Xsheet.
    void onActionSendToFunctionView();
    void onActionOpenFunctionEditor();
    void onActionConvertFunction(QString const& how);
    void onActionFunctionColumnsTag(QString const& how);
    void openFunctionEditor(int column);
    // The ease of the Set Ease for Multiple Parameters dialog given to the moves selected, for the kinds of value named.
    int easeSelection(QStringList const& kinds, QPointF leave, QPointF arrive);
    // The ease of the keyframes on the frames and layers selected (0 to 1 each; below 0: left as it is).  How many were changed.
    int setEaseOnSelection(double easeIn, double easeOut);
    bool applyBreakdown(double percent);
    void onActionMarkFrame(QString const& colour);
    void onActionLayerKeys(QString const& how);
    void onActionPasteSpecialAgain();
    void onActionEditExposurePreset();
    void onActionPasteModeExposure(bool on);
    void onActionPasteModeCycle();
    void onActionPasteMode(QString const& mode);
    void onActionEditPastePreset(QString const& which);
    // The Timeline's own commands (MainWindowTimeline.cpp).
    void onActionThumbnails(QString const& which);
    void onActionCentreOnSelection();
    void onActionNextViewMode();
    void onActionGotoFrameMark(QString const& which);
    void onActionEnableLayers(QString const& how);
    void onActionSoloMode();
    void onActionSelectSyncedLayers();
    void onActionUnsyncLayer();
    void onActionShowFunctions();
    void onActionShowManager();
    void onActionRippleMarkers(bool on);
    void onActionOverwriteDrop(bool on);
    // The Xsheet view (MainWindowXsheet).
    void onActionXsheetGesturalDrag(bool on);
    void onActionXsheetInsertMode(bool on);
    void onActionXsheetView(QString const& what);
    void onActionXsheetColumnTypes();
    void onActionHideSelectedColumns();
    void onActionUnhideAllColumns();
    void onActionShowHiddenColumns();
    void onActionChangeColumnsColour();
    void onActionDefaultColumnsColour();
    void onActionSetColumnsWidth();
    void onActionDefaultColumnsWidth();
    void onActionAddColumns();
    void onActionAddDrawingColumn();
    void onActionAddAnnotationColumn();
    void onActionColumnEditor();
    void onActionAddSheetMarker();
    void onActionSheetMarkerColour(QString const& colour);
    void onActionDeleteSheetMarker();
    void onActionTempoMarks();
    void onActionAnnotation(QString const& what);
    void onActionPrintXsheet();
    void onActionXsheetPreview();
    // So many columns of a kind (Layer::Kind): 0 right of the current column, 1 left of it, 2 at the start, 3 at the end. The last added.
    int addXsheetColumns(int kind, QString const& name, int count, int where);
    void onActionLayerPrefixSuffix();
    void onActionDeleteSelectedDrawings();
    void onActionRenameWithPrefix();
    void onActionOnionLayers(QString const& which);
    void onActionOnionAdvanced(bool on);
    void onActionReference(QString const& what);
    void onActionDesk(QString const& how);
    void onActionDeskThumbnails(QString const& which);
    void syncDesk();
    void onActionShowReferenceView();
    // The current drawing as a picture, kept beside the scene, for the Reference view.
    QString referenceOfDrawing();
    void onActionOnionLinkSliders(bool on);
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
    // The size of the Brush, the Pencil or the Eraser, a step bigger ("up") or smaller ("down").
    void onActionChangeBrushSize(QString const& way);
    // Tool Preset Shortcut #n: the n-th preset of the tool in hand (as Tool Properties lists them) taken up.
    void onActionToolPreset(QString const& which);
    // The pen stabiliser off, or back on as it was.
    void onActionToggleStabilizer();
    // Snapping and Auto-Create Colour Art: as they were left last time, and kept as they are turned on or off.
    void rememberToggles();
    // How strongly the onion skin shows.
    void onActionTopLight();

    // ---- Help ----
    void onActionAbout();
    void onActionShowWelcome();

protected:
    void closeEvent(QCloseEvent*) override;
    bool eventFilter(QObject*, QEvent*) override;
    void showEvent(QShowEvent* event) override;

private:
    void startCaptureMotion(int target, int first, int last, int countdown, int fps);
    // The Preferences dialog's pages, each made into the widgets it is read back from.
    void preferencesGeneral(struct PreferenceWidgets& w, class QSettings& s, class QDialog& dialog);
    void preferencesCamera(struct PreferenceWidgets& w, class QSettings& s, class QDialog& dialog);
    void preferencesSheet(struct PreferenceWidgets& w, class QSettings& s, class QDialog& dialog);
    void preferencesSheetMore(struct PreferenceWidgets& w, class QSettings& s, class QDialog& dialog);
    void saveSheetPreferences(struct PreferenceWidgets& w, class QSettings& s);
    void applySheetPreferences(class QSettings const& s, bool starting);
    void preferencesAnimation(struct PreferenceWidgets& w, class QSettings& s, class QDialog& dialog);
    void saveAnimationPreferences(struct PreferenceWidgets& w, class QSettings& s);
    void applyAnimationPreferences(class QSettings const& s, bool starting);
    void preferencesAdvanced(struct PreferenceWidgets& w, class QSettings& s, class QDialog& dialog);
    void preferencesTimeline(struct PreferenceWidgets& w, class QSettings& s, class QDialog& dialog);
    void preferencesRender(struct PreferenceWidgets& w, class QSettings& s, class QDialog& dialog);
    void preferencesTools(struct PreferenceWidgets& w, class QSettings& s, class QDialog& dialog);
    void preferencesNodeView(struct PreferenceWidgets& w, class QSettings& s, class QDialog& dialog);
    void saveNodePreferences(struct PreferenceWidgets& w, class QSettings& s);
    void applyNodePreferences(class QSettings const& s);
    void preferencesOnion(struct PreferenceWidgets& w, class QSettings& s, class QDialog& dialog);
    void preferencesSound(struct PreferenceWidgets& w, class QSettings& s, class QDialog& dialog);
    void preferencesDrawing(struct PreferenceWidgets& w, class QSettings& s, class QDialog& dialog);
    void preferencesColours(struct PreferenceWidgets& w, class QSettings& s, class QDialog& dialog);
    void preferencesPalettes(struct PreferenceWidgets& w, class QSettings& s, class QDialog& dialog);
    void savePalettePreferences(struct PreferenceWidgets& w, class QSettings& s);
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
    // (`force`: even when workspaces are not saved by themselves - Save Workspace.)
    void saveCurrentWorkspace(bool force = false);
    // The arrangement as it was opened or last saved, to tell whether it has changed since.
    void markWorkspaceClean();
    QByteArray m_cleanWorkspace;
    bool m_autoSaveWorkspace = true;
    bool m_restoreDrawingOnUndo = false;
    int m_autoSaveQuiet = 1;            // seconds without input before saving by itself
    QElapsedTimer m_lastInput;
    void autoSaveNow();
    // Undo / Redo, and with that preference, to the drawing it changed.
    void stepUndo(bool redo);
    void refreshWorkspaceBox();
    QDockWidget* dockOf(char const* objectName) const;
    // Brings the tabs across the top of every panel up to date with where the views are.
    void refreshViewTabs();
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
    QWidget* m_cameraPanel = nullptr;      // the view in the middle, strip of tabs and all
    QWidget* m_cameraWindow = nullptr;     // the window it is in while it floats
    class QDockWidget* m_cameraDock = nullptr;     // the panel it is in while it is docked
    QHash<QDockWidget*, QSize> m_sizesBeforeMiddle;     // how big the panels were with the middle view there
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
    bool m_restoreWorkspacePending = false;
    // Marks the view that was clicked in last: it is outlined.
    void setActiveView(QWidget* frame);
    void updateTitle();
    void updateStatus();
    // Greys out the commands that have nothing to act on just now.
    void updateActionStates();
    // Asks how many, then puts that many empty frames in before frame `at`.
    void addFrames(int at);
    void stepLayer(int direction);
    // (`files`: those to import, already chosen; none: they are asked for first.)
    void askImportImages(bool linked, QStringList const& files = {});
    QWidget* makeReferenceView();
    QListWidget* m_references = nullptr;
    // The frames an exposure command works on, for a layer.
    void cellRange(int layer, int* first, int* last) const;
public:
    int holdCells(int every);
    QVector<int> flipFrames() const;
    int deleteEmptyElements();
    bool fillCellsRandomly(int layer, int first, int last, int shortest, int longest, quint32 seed);
    bool extendExposureTo(int layer, int from, int to);
private:
    QListWidget* m_desk = nullptr;
    void createFlipToolbar();
    QSpinBox* m_flipSpeed = nullptr;
    QSpinBox* m_flipBefore = nullptr;
    QSpinBox* m_flipAfter = nullptr;
    QCheckBox* m_flipKeys = nullptr;
    QCheckBox* m_flipBreakdowns = nullptr;
    QCheckBox* m_flipInbetweens = nullptr;
    QTimer* m_flipTimer = nullptr;
    QVector<int> m_flipFrames;
    int m_flipAt = 0, m_flipWay = 1, m_flipLayer = 0, m_flipHome = 0;
    int m_deskThumbnails = 2;
    QWidget* createDeskView();
    class LibraryView* m_library = nullptr;
    class QToolBar* m_scriptBar = nullptr;     // Scripts: the buttons of the scripts put on it
    // (`onto`: put on the layers of the same names when the scene has them all - a drop on the cells of the Timeline.)
    void dropTemplate(QString const& path, int frame, bool onto = false);
    void askExportImages(bool preview);
    void exportMovieTo(QString const& path, struct MovieChoices const* choices = nullptr);
    void syncReferences();
    // The sound of one frame, heard.
    void playFrameSound(int frame);
    int m_envelopePoints = 8;      // how many points Create Envelope puts round a drawing
    // Paste Special as it was last asked for (Paste Special Again does the same without asking).
    Timeline::PasteOptions m_paste, m_pasteExposure, m_pasteAll, m_pasteKeys;
    // The paste mode: 0 all, 1 exposures only, 2 keyframes only; the All preset counts once it has been edited.
    int m_pasteMode = 0;
    bool m_pasteAllEdited = false;
    void setPasteMode(int mode);
    bool pasteDialog(Timeline::PasteOptions& options, QString const& title);
    bool m_envelopeOutline = true; // ... round the outline of the artwork itself, not the hull round it
    QStringList m_referenceFiles;
    // Brings a view up: back where it was, or as a tab behind Tool Properties.
    void showView(QDockWidget* dock);
    void stepSibling(int direction);
    void stepColour(int direction);
    // The exposure commands act on every layer of the block selected, from its first frame.
    void onExposure(QString const& text, std::function<void(int layer, int frame)> const& command);
    QVector<int> exposureLayers() const;
    int layer() const;
    int frame() const;
    // Says so and is true when the current layer shows no 3D model.
    bool refuseUnlessModel(QString const& what);
    // Floating panels and toolbars kept hidden until the window is first shown (see restoreWorkspace).
    QVector<QPointer<QWidget>> m_heldBack;
    // The ease last given with Set Ease for Multiple Parameters (Apply Ease to Selection gives it again).
    QPointF m_easeLeave{0.5, 0.0}, m_easeArrive{0.5, 1.0};
    QStringList m_easeKinds;
    QPointer<QWidget> m_velocityEditor;
    // Preferences: the Function Editors stay in front of the window.
    bool m_editorsOnTop = true;
    int m_appliedOutlinePreference = -1;
    int m_appliedBBoxPreference = -1;
    void connectFunctionColumns();
    // The value column the Xsheet is on (layer, value); value -1 when it is on a layer's own column.
    QPair<int, int> xsheetValueColumn() const;
    bool m_focusOnEnter = false;
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
    void createXsheetView();
    void connectXsheetToggles();
    void syncXsheetChecks();
    void refreshXsheetList();
    // The layers of the columns selected in the Xsheet (the current one when none).
    QVector<int> xsheetColumnLayers() const;
    void columnEditorNext();
    void setLocked(bool locked);
    void pickTrackColour(int layer);

    ActionManager* m_actions;
    Document* m_document;
    Canvas* m_canvas;
    Timeline* m_timeline;
    PalettePanel* m_palette = nullptr;
    ColourEditor* m_colourEditor = nullptr;
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
    class QListWidget* m_xsheetList = nullptr;
    bool m_xsheetMirroring = false;     // the Xsheet is setting the Timeline's cell or block
    bool m_columnEditorNext = false;    // Column Properties opened as the Column Editor (with Apply/Next)
    LayerPanel* m_layerPanel = nullptr;
    NodeView* m_nodeView = nullptr;
    // Preferences > Node View: Select Parent and the rest go by the Node View's cables (a graph wired by hand), not the Timeline's.
    bool m_followNodeParenting = false;
    // Select in Camera: the node waiting for a layer (a graph node's id, or a layer's), and the prompt shown meanwhile.
    int m_cameraPickTarget = 0;
    bool m_cameraPickGraph = false;
    QPointer<QWidget> m_cameraPickPrompt;
    void finishSelectInCamera(int picked);
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
    bool m_keepBackup = true;       // the file as it was kept beside it when a scene is saved over
    bool m_askAutoSave = false;     // saving by itself asks first
    int m_preroll = 0;              // play starts so many frames before the current one
    bool m_preferencesApplied = false;  // (what only sets how things start is set the first time only)
    QTimer m_autoSave;
    int m_playSpeed = 100;
    int m_loopPause = 0;        // ms the last frame is held before a loop plays again (Preferences)
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
