// Eclipse - the Preferences dialog: the General, Camera, Exposure Sheet, Advanced, Timeline, Render, Tools, Node View, Onion Skin and Sound pages.
#include "ui/window/MainWindow.h"
#include <functional>
#include <algorithm>
#include <cmath>
#include <QApplication>
#include <QThread>
#include <QTimer>
#include <QPlainTextEdit>
#include <QCheckBox>
#include "ui/panels/ColourEditor.h"
#include <QTabWidget>
#include <QDoubleSpinBox>
#include <QGuiApplication>
#include <QScreen>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QPushButton>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QDirIterator>
#include <QMessageBox>
#include <QSet>
#include <QStatusBar>
#include "audio/sound/Sound.h"
#include "model/document/Commands.h"
#include "canvas/Canvas.h"
#include "ui/timeline/Xsheet.h"
#include "ui/timeline/FunctionView.h"
#include "ui/views/PreviewManagerView.h"
#include "ui/views/CoordView.h"
#include "actions/manager/ActionManager.h"
#include <QAction>
#include <QToolButton>
#include "ui/panels/LibraryView.h"
#include "ui/panels/SubstitutionPanel.h"
#include "ui/io/MovieExport.h"
#include "ui/panels/PalettePanel.h"
#include "ui/io/ImportExportPrivate.h"
#include "ui/views/NodeView.h"
#include "actions/theme/InterfaceColours.h"
#include <QHeaderView>
#include <QTreeWidget>
#include <QLibraryInfo>
#include <QTranslator>
#include <tuple>
#include "ui/io/PreferenceWidgets.h"

void MainWindow::preferencesGeneral(PreferenceWidgets& w, QSettings& s, QDialog& dialog)
{
    w.general = new QGroupBox(tr("General"), &dialog);
    w.welcome = new QCheckBox(tr("Show the welcome screen when Eclipse starts"), w.general);
    w.welcome->setChecked(!s.value(QStringLiteral("welcome/hide"), false).toBool());
    w.autoSave = new QSpinBox(w.general);
    w.autoSave->setRange(0, 240);
    w.autoSave->setSpecialValueText(tr("Never"));
    w.autoSave->setSuffix(tr(" minutes"));
    w.autoSave->setValue(m_autoSaveMinutes);
    w.generalLayout = new QFormLayout(w.general);
    w.focusOnEnter = new QCheckBox(tr("Focus on mouse enter"), w.general);
    w.focusOnEnter->setObjectName(QStringLiteral("PrefFocusOnEnter"));
    w.focusOnEnter->setToolTip(tr("The Camera view, the Timeline, the Xsheet and the Node View take the keyboard as soon as the pointer is over them."));
    w.focusOnEnter->setChecked(s.value(QStringLiteral("prefs/focusOnEnter"), false).toBool());
    w.modelScale = new QDoubleSpinBox(w.general);
    w.modelScale->setObjectName(QStringLiteral("PrefModelScale"));
    w.modelScale->setRange(0.01, 100.0);
    w.modelScale->setDecimals(2);
    w.modelScale->setSingleStep(0.1);
    w.modelScale->setToolTip(tr("How big a 3D model comes in: 1 is %1 scene units at its biggest.").arg(400));
    w.modelScale->setValue(s.value(QStringLiteral("prefs/modelScale"), 1.0).toDouble());
    w.generalLayout->addRow(w.welcome);
    w.generalLayout->addRow(tr("Save the scene by itself every"), w.autoSave);
    w.generalLayout->addRow(new QLabel(tr("(Only a scene that has been saved once, and only when it has changed.)"), w.general));
    w.undoLevels = new QSpinBox(w.general);
    w.undoLevels->setObjectName(QStringLiteral("PrefUndoLevels"));
    w.undoLevels->setRange(0, 10000);
    w.undoLevels->setSpecialValueText(tr("No limit"));
    w.undoLevels->setToolTip(tr("How many steps can be undone. It takes hold with the next scene made or opened."));
    w.undoLevels->setValue(m_document->undoLevels());
    w.generalLayout->addRow(tr("Levels of undo"), w.undoLevels);
    w.minimumFov = new QDoubleSpinBox(w.general);
    w.maximumFov = new QDoubleSpinBox(w.general);
    w.minimumFov->setObjectName(QStringLiteral("PrefMinimumFov"));
    w.maximumFov->setObjectName(QStringLiteral("PrefMaximumFov"));
    for (QDoubleSpinBox* box : {w.minimumFov, w.maximumFov}) {
        box->setDecimals(3);
        box->setRange(Layer::kMinFov, Layer::kMaxFov);
        box->setSuffix(tr(" deg"));
    }
    w.minimumFov->setValue(m_document->minimumFov());
    w.maximumFov->setValue(m_document->maximumFov());
    w.minimumFov->setToolTip(tr("The smallest angle accepted when directly editing a camera. Existing project values are preserved."));
    w.maximumFov->setToolTip(tr("The largest angle accepted when directly editing a camera. Existing project values are preserved."));
    connect(w.minimumFov, &QDoubleSpinBox::valueChanged, w.maximumFov, &QDoubleSpinBox::setMinimum);
    connect(w.maximumFov, &QDoubleSpinBox::valueChanged, w.minimumFov, &QDoubleSpinBox::setMaximum);
    w.maximumFov->setMinimum(w.minimumFov->value());
    w.minimumFov->setMaximum(w.maximumFov->value());
    w.generalLayout->addRow(tr("Minimum camera field of view"), w.minimumFov);
    w.generalLayout->addRow(tr("Maximum camera field of view"), w.maximumFov);
    w.pasteForceKeys = new QCheckBox(tr("Pasting creates start/end keyframes"), w.general);
    w.pasteForceKeys->setObjectName(QStringLiteral("PrefPasteForceKeys"));
    w.pasteForceKeys->setToolTip(tr("Cells of an animated layer copied and pasted come with keyframes where they begin and end, so the motion pasted is the motion copied."));
    w.pasteForceKeys->setChecked(s.value(QStringLiteral("prefs/pasteForceKeys"), true).toBool());
    w.generalLayout->addRow(w.pasteForceKeys);
    w.confirmLayerDelete = new QCheckBox(tr("Ask before deleting layers"), w.general);
    w.confirmLayerDelete->setObjectName(QStringLiteral("PrefConfirmLayerDelete"));
    w.confirmLayerDelete->setToolTip(tr("Delete Layers asks first (You are about to delete Layer(s). Please Confirm.)."));
    w.confirmLayerDelete->setChecked(s.value(QStringLiteral("prefs/confirmLayerDelete"), false).toBool());
    w.generalLayout->addRow(w.confirmLayerDelete);
    w.syncBitmapColour = new QCheckBox(tr("Keep the bitmap colour in sync with the vector colour"), w.general);
    w.syncBitmapColour->setObjectName(QStringLiteral("PrefSyncBitmapColour"));
    w.syncBitmapColour->setToolTip(tr("With Independent Bitmap Colour on, choosing a colour in the palette makes it the bitmap colour too."));
    w.syncBitmapColour->setChecked(s.value(QStringLiteral("prefs/syncBitmapColour"), false).toBool());
    w.generalLayout->addRow(w.syncBitmapColour);
    w.keepBackup = new QCheckBox(tr("Keep a backup of the scene file when saving"), w.general);
    w.keepBackup->setObjectName(QStringLiteral("PrefKeepBackup"));
    w.keepBackup->setToolTip(tr("Before a scene is saved over, the file as it was is kept beside it, with ~ after its name."));
    w.keepBackup->setChecked(s.value(QStringLiteral("prefs/keepBackup"), true).toBool());
    w.generalLayout->addRow(w.keepBackup);
    w.askConvertScenes = new QCheckBox(tr("Offer to convert older single-file scenes to scene folders"), w.general);
    w.askConvertScenes->setObjectName(QStringLiteral("PrefAskConvertScenes"));
    w.askConvertScenes->setToolTip(tr("A scene saved before scenes were kept as folders asks, when it is opened, whether to make it one."));
    w.askConvertScenes->setChecked(s.value(QStringLiteral("prefs/askConvertScenes"), true).toBool());
    w.generalLayout->addRow(w.askConvertScenes);
    w.editorsOnTop = new QCheckBox(tr("Editors always on top"), w.general);
    w.editorsOnTop->setObjectName(QStringLiteral("PrefEditorsOnTop"));
    w.editorsOnTop->setToolTip(tr("Function editors are always on top of the main window."));
    w.editorsOnTop->setChecked(s.value(QStringLiteral("prefs/editorsOnTop"), true).toBool());
    w.generalLayout->addRow(w.editorsOnTop);
    w.defaultBezier = new QCheckBox(tr("Default Bezier"), w.general);
    w.defaultBezier->setObjectName(QStringLiteral("PrefDefaultBezier"));
    w.defaultBezier->setToolTip(tr("Create a bezier or an ease when creating a function column."));
    w.defaultBezier->setChecked(s.value(QStringLiteral("prefs/defaultBezier"), true).toBool());
    w.generalLayout->addRow(w.defaultBezier);
    w.defaultBezierVelocity = new QCheckBox(tr("Default Bezier Velocity"), w.general);
    w.defaultBezierVelocity->setObjectName(QStringLiteral("PrefDefaultBezierVelocity"));
    w.defaultBezierVelocity->setToolTip(tr("Create a bezier or an ease when creating the velocity of a 3D path."));
    w.defaultBezierVelocity->setChecked(s.value(QStringLiteral("prefs/defaultBezierVelocity"), true).toBool());
    w.generalLayout->addRow(w.defaultBezierVelocity);
    w.compress = new QCheckBox(tr("Compress scene files on save"), w.general);
    w.compress->setObjectName(QStringLiteral("PrefCompressScenes"));
    w.compress->setToolTip(tr("Scene files are written packed: smaller, and read just the same."));
    w.compress->setChecked(s.value(QStringLiteral("prefs/compressScenes"), false).toBool());
    w.generalLayout->addRow(w.compress);
    w.quiet = new QSpinBox(w.general);
    w.quiet->setObjectName(QStringLiteral("PrefAutosaveQuiet"));
    w.quiet->setRange(0, 600);
    w.quiet->setSuffix(tr(" seconds"));
    w.quiet->setToolTip(tr("Saving by itself waits until you have stopped working for this long, so as not to interrupt a stroke."));
    w.quiet->setValue(s.value(QStringLiteral("prefs/autosaveQuietSeconds"), 1).toInt());
    w.generalLayout->addRow(tr("Auto-save waits for a pause of"), w.quiet);
    w.generalLayout->addRow(w.focusOnEnter);
    w.autoWorkspace = new QCheckBox(tr("Automatically save workspace"), w.general);
    w.autoWorkspace->setObjectName(QStringLiteral("PrefAutoSaveWorkspace"));
    w.autoWorkspace->setToolTip(tr("On: the views stay as you leave them. Off: a workspace changes only when saved (Windows > Workspace > Save Workspace);\n"
                                 "on quitting you are asked if it changed."));
    w.autoWorkspace->setChecked(s.value(QStringLiteral("prefs/autoSaveWorkspace"), true).toBool());
    w.generalLayout->addRow(w.autoWorkspace);
    w.viewBorder = new QCheckBox(tr("Current view border"), w.general);
    w.viewBorder->setObjectName(QStringLiteral("PrefViewBorder"));
    w.viewBorder->setToolTip(tr("A red border round the view in use."));
    w.viewBorder->setChecked(s.value(QStringLiteral("prefs/viewBorder"), true).toBool());
    w.generalLayout->addRow(w.viewBorder);
    w.middlePans = new QCheckBox(tr("Middle mouse button pans the view"), w.general);
    w.middlePans->setObjectName(QStringLiteral("PrefMiddlePans"));
    w.middlePans->setChecked(s.value(QStringLiteral("prefs/middlePans"), true).toBool());
    w.generalLayout->addRow(w.middlePans);
    w.performance = new QCheckBox(tr("Camera view performance report"), w.general);
    w.performance->setObjectName(QStringLiteral("PrefPerformanceReport"));
    w.performance->setToolTip(tr("Shows in a corner of the Camera view how long each picture of it takes to draw."));
    w.performance->setChecked(s.value(QStringLiteral("prefs/performanceReport"), false).toBool());
    w.generalLayout->addRow(w.performance);
    w.restoreDrawing = new QCheckBox(tr("Restore current selected drawing on undo"), w.general);
    w.restoreDrawing->setObjectName(QStringLiteral("PrefRestoreDrawingOnUndo"));
    w.restoreDrawing->setToolTip(tr("Undo (and Redo) of a change to another drawing goes to that drawing, to show what was undone."));
    w.restoreDrawing->setChecked(s.value(QStringLiteral("prefs/restoreDrawingOnUndo"), false).toBool());
    w.generalLayout->addRow(w.restoreDrawing);
    w.generalLayout->addRow(tr("3D models come in at"), w.modelScale);
    w.askAutoSave = new QCheckBox(tr("Ask before saving by itself"), w.general);
    w.askAutoSave->setObjectName(QStringLiteral("PrefAskAutoSave"));
    w.askAutoSave->setChecked(s.value(QStringLiteral("prefs/askAutoSave"), false).toBool());
    w.generalLayout->addRow(w.askAutoSave);

    w.darkStyle = new QCheckBox(tr("Use dark style sheet (on the next start)"), w.general);
    w.darkStyle->setObjectName(QStringLiteral("PrefDarkStyle"));
    w.darkStyle->setChecked(s.value(QStringLiteral("prefs/darkStyle"), true).toBool());
    w.generalLayout->addRow(w.darkStyle);
    w.uiScale = new QSpinBox(w.general);
    w.uiScale->setObjectName(QStringLiteral("PrefInterfaceScale"));
    w.uiScale->setRange(50, 300);
    w.uiScale->setSingleStep(25);
    w.uiScale->setSuffix(tr(" %"));
    w.uiScale->setToolTip(tr("How big the whole window is drawn: menus, panels, text (on the next start)."));
    w.uiScale->setValue(s.value(QStringLiteral("prefs/interfaceScale"), 100).toInt());
    w.generalLayout->addRow(tr("Interface scale (on the next start)"), w.uiScale);
    w.language = new QComboBox(w.general);
    w.language->setObjectName(QStringLiteral("PrefLanguage"));
    for (QString const& code : languages()) {
        QString const name = QLocale(code).nativeLanguageName();
        w.language->addItem(name.isEmpty() ? code : name.left(1).toUpper() + name.mid(1), code);
    }
    w.language->setCurrentIndex(std::max(0, w.language->findData(s.value(QStringLiteral("prefs/language"), QStringLiteral("en")).toString())));
    w.language->setToolTip(tr("The language of menus, panels and messages (on the next start)."));
    w.generalLayout->addRow(tr("Language (on the next start)"), w.language);
    w.styleEditor = new QPushButton(tr("Style Editor..."), w.general);
    w.styleEditor->setObjectName(QStringLiteral("PrefStyleEditor"));
    w.generalLayout->addRow(w.styleEditor);
    connect(w.styleEditor, &QPushButton::clicked, &dialog, [this] { showStyleEditor(); });
    Q_UNUSED(s);
    Q_UNUSED(dialog);
}

void MainWindow::preferencesCamera(PreferenceWidgets& w, QSettings& s, QDialog& dialog)
{
    w.camera = new QGroupBox(tr("Camera"), &dialog);
    w.selectMode = new QComboBox(w.camera);
    w.selectMode->addItems({tr("Lasso"), tr("Marquee")});
    w.selectMode->setCurrentIndex(s.value(QStringLiteral("prefs/lasso"), true).toBool() ? 0 : 1);
    w.cameraLayout = new QFormLayout(w.camera);
    w.cameraLayout->addRow(tr("The Select tool starts with"), w.selectMode);
    w.cameraZoom = new QComboBox(w.camera);
    w.cameraZoom->setObjectName(QStringLiteral("PrefCameraZoom"));
    w.cameraZoom->addItems({tr("Fit to View"), tr("100%")});
    w.cameraZoom->setCurrentIndex(std::clamp(s.value(QStringLiteral("prefs/cameraZoom"), 0).toInt(), 0, 1));
    w.cameraZoom->setToolTip(tr("How the Camera view shows a scene made or opened: the whole frame in the view, or at its own size."));
    w.cameraLayout->addRow(tr("A scene opens"), w.cameraZoom);
    w.outlineLocked = new QCheckBox(tr("Show Locked Drawings as Outlines"), w.camera);
    w.outlineLocked->setObjectName(QStringLiteral("PrefOutlineLocked"));
    w.outlineLocked->setToolTip(tr("Show locked drawings as outlines when a scene is opened."));
    w.outlineLocked->setChecked(s.value(QStringLiteral("prefs/outlineLocked"), false).toBool());
    w.cameraLayout->addRow(w.outlineLocked);
    w.bboxSelection = new QCheckBox(tr("Bounding Box Selection Style"), w.camera);
    w.bboxSelection->setObjectName(QStringLiteral("PrefBBoxSelection"));
    w.bboxSelection->setToolTip(tr("Show selected layers with a bounding box instead of a colour highlight when a scene is opened."));
    w.bboxSelection->setChecked(s.value(QStringLiteral("prefs/bboxSelection"), false).toBool());
    w.cameraLayout->addRow(w.bboxSelection);
    w.washCamera = new QCheckBox(tr("Wash background images in Camera"), w.camera);
    w.washCamera->setObjectName(QStringLiteral("PrefWashCamera"));
    w.washCamera->setChecked(s.value(QStringLiteral("prefs/washCamera"), false).toBool());
    w.washCamera->setToolTip(tr("Wash imported images in the Camera view while using animation tools."));
    w.cameraLayout->addRow(w.washCamera);
    w.washDrawing = new QCheckBox(tr("Wash background images while drawing"), w.camera);
    w.washDrawing->setObjectName(QStringLiteral("PrefWashDrawing"));
    w.washDrawing->setChecked(s.value(QStringLiteral("prefs/washDrawing"), false).toBool());
    w.washDrawing->setToolTip(tr("Wash imported images in the Camera view while using drawing tools."));
    w.cameraLayout->addRow(w.washDrawing);
    w.washPercent = new QSpinBox(w.camera);
    w.washPercent->setObjectName(QStringLiteral("PrefWashPercent"));
    w.washPercent->setRange(0, 100);
    w.washPercent->setSuffix(tr(" %"));
    w.washPercent->setValue(s.value(QStringLiteral("prefs/washPercent"), 50).toInt());
    w.cameraLayout->addRow(tr("Background wash amount"), w.washPercent);
    w.washColour = new QPushButton(w.camera);
    w.washColour->setObjectName(QStringLiteral("PrefWashColour"));
    auto showWashColour = [button = w.washColour](QColor const& colour) {
        button->setProperty("colour", colour);
        button->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid #202020; min-height: 16px; min-width: 60px;").arg(colour.name()));
    };
    showWashColour(s.value(QStringLiteral("prefs/washColour"), QColor(Qt::white)).value<QColor>());
    connect(w.washColour, &QPushButton::clicked, &dialog, [button = w.washColour, showWashColour, &dialog] {
        QColor const colour = ColourEditor::getColour(button->property("colour").value<QColor>(), &dialog, tr("Background Wash Colour"));
        if (colour.isValid()) showWashColour(colour);
    });
    w.cameraLayout->addRow(tr("Background wash colour"), w.washColour);
    w.washStalePreview = new QCheckBox(tr("Enable for Out of Date Previews"), w.camera);
    w.washStalePreview->setObjectName(QStringLiteral("PrefWashStalePreview"));
    w.washStalePreview->setChecked(s.value(QStringLiteral("prefs/washStalePreview"), false).toBool());
    w.washStalePreview->setToolTip(tr("A rendered preview the scene has changed since is still played back, washed with the wash colour and amount, until it is rendered again. Off, it is dropped."));
    w.cameraLayout->addRow(w.washStalePreview);
    w.selectionShading = new QCheckBox(tr("Selection Shading"), w.camera);
    w.selectionShading->setObjectName(QStringLiteral("PrefSelectionShading"));
    w.selectionShading->setToolTip(tr("The selected layer shaded in the Top and Side views."));
    w.selectionShading->setChecked(s.value(QStringLiteral("prefs/selectionShading"), true).toBool());
    w.cameraLayout->addRow(w.selectionShading);
    w.nudge = new QSpinBox(w.camera);
    w.nudge->setObjectName(QStringLiteral("PrefNudge"));
    w.nudge->setRange(1, 100);
    w.nudge->setSuffix(tr(" pixels"));
    w.nudge->setToolTip(tr("How far an arrow key moves what the Select tool has selected (ten times as far with Shift)."));
    w.nudge->setValue(int(m_canvas->nudgeStep()));
    w.cameraLayout->addRow(tr("Nudging factor"), w.nudge);
    w.frameOne = new QCheckBox(tr("Set Keyframe at Frame One with First Application of the Transform Tool"), w.camera);
    w.frameOne->setObjectName(QStringLiteral("PrefKeyFrameOne"));
    w.frameOne->setToolTip(tr("The first time a layer is moved, on a frame after the first, the first frame gets a keyframe of the layer as it was: it goes from there to where you put it."));
    w.frameOne->setChecked(m_canvas->keysFrameOne());
    w.cameraLayout->addRow(w.frameOne);
    w.gridAcross = new QSpinBox(w.camera);
    w.gridAcross->setObjectName(QStringLiteral("PrefGridAcross"));
    w.gridAcross->setRange(2, 12);
    w.gridAcross->setValue(m_canvas->proportionAcross());
    w.gridDown = new QSpinBox(w.camera);
    w.gridDown->setObjectName(QStringLiteral("PrefGridDown"));
    w.gridDown->setRange(2, 12);
    w.gridDown->setValue(m_canvas->proportionDown());
    w.gridParts = new QHBoxLayout;
    w.gridParts->addWidget(w.gridAcross);
    w.gridParts->addWidget(new QLabel(tr("across by"), w.camera));
    w.gridParts->addWidget(w.gridDown);
    w.gridParts->addWidget(new QLabel(tr("down"), w.camera));
    w.gridParts->addStretch(1);
    w.cameraLayout->addRow(tr("Proportion grid"), w.gridParts);
    w.safe = new QSpinBox(w.camera);
    w.safe->setObjectName(QStringLiteral("PrefSafeMargin"));
    w.safe->setRange(1, 30);
    w.safe->setSuffix(tr(" %"));
    w.safe->setToolTip(tr("How much of the frame, at each edge, lies outside the safe area (the title-safe area leaves twice as much)."));
    w.safe->setValue(int(std::lround(m_canvas->safeMargin() * 100.0)));
    w.cameraLayout->addRow(tr("TV safety"), w.safe);
    w.zoomOnMouse = new QCheckBox(tr("Zoom In and Zoom Out about the pointer"), w.camera);
    w.zoomOnMouse->setObjectName(QStringLiteral("PrefZoomOnMouse"));
    w.zoomOnMouse->setToolTip(tr("The zoom shortcuts zoom about where the pointer is over the view, instead of about its middle."));
    w.zoomOnMouse->setChecked(m_canvas->zoomsOnMouse());
    w.cameraLayout->addRow(w.zoomOnMouse);
    // Zoom Settings.
    auto percentBox = [&](char const* name, int low, int high, int value) {
        QSpinBox* const box = new QSpinBox(w.camera);
        box->setObjectName(QLatin1String(name));
        box->setRange(low, high);
        box->setSuffix(tr(" %"));
        box->setValue(std::clamp(value, low, high));
        return box;
    };
    w.defaultZoom = percentBox("PrefDefaultZoom", 1, 25600, s.value(QStringLiteral("prefs/defaultZoom"), 100).toInt());
    w.defaultZoom->setToolTip(tr("The zoom View > Reset Zoom goes to."));
    w.cameraLayout->addRow(tr("Camera View Default Zoom"), w.defaultZoom);
    w.planningDefaultZoom = percentBox("PrefPlanningDefaultZoom", 20, 4000, s.value(QStringLiteral("prefs/planningDefaultZoom"), 100).toInt());
    w.planningDefaultZoom->setToolTip(tr("Initial and reset zoom for the Top and Side views. A saved project's view takes precedence when opened."));
    w.cameraLayout->addRow(tr("Top/Side View Default Zoom"), w.planningDefaultZoom);
    w.planningThumbnailSize = new QSpinBox(w.camera);
    w.planningThumbnailSize->setObjectName(QStringLiteral("PrefPlanningThumbnailSize"));
    w.planningThumbnailSize->setRange(0, 256);
    w.planningThumbnailSize->setSuffix(tr(" px"));
    w.planningThumbnailSize->setSpecialValueText(tr("Hidden"));
    w.planningThumbnailSize->setValue(std::clamp(s.value(QStringLiteral("prefs/planningThumbnailSize"), 64).toInt(), 0, 256));
    w.planningThumbnailSize->setToolTip(tr("Drawing thumbnail size in Top and Side views. Set to zero to hide thumbnails."));
    w.cameraLayout->addRow(tr("Top/Side Thumbnail Size"), w.planningThumbnailSize);
    w.zoomMin = percentBox("PrefZoomMin", 1, 100, s.value(QStringLiteral("prefs/zoomMin"), 2).toInt());
    w.cameraLayout->addRow(tr("Minimum Zoom Level"), w.zoomMin);
    w.zoomMax = percentBox("PrefZoomMax", 100, 25600, s.value(QStringLiteral("prefs/zoomMax"), 6400).toInt());
    w.cameraLayout->addRow(tr("Maximum Zoom Level"), w.zoomMax);
    w.quickCloseUp = new QDoubleSpinBox(w.camera);
    w.quickCloseUp->setObjectName(QStringLiteral("PrefQuickCloseUp"));
    w.quickCloseUp->setRange(1.1, 16.0);
    w.quickCloseUp->setSingleStep(0.5);
    w.quickCloseUp->setPrefix(QStringLiteral("x"));
    w.quickCloseUp->setValue(s.value(QStringLiteral("prefs/quickCloseUp"), 4.0).toDouble());
    w.quickCloseUp->setToolTip(tr("View > Quick Close-up: how much closer it goes, about the pointer."));
    w.cameraLayout->addRow(tr("Quick Close Up Zoom Factor"), w.quickCloseUp);
    w.paperZoom = new QCheckBox(tr("Enable Paper Zoom"), w.camera);
    w.paperZoom->setObjectName(QStringLiteral("PrefPaperZoom"));
    w.paperZoom->setToolTip(tr("Zoom levels are of the frame's real size: at 100 % its twelve fields are twelve inches wide on this screen."));
    w.paperZoom->setChecked(s.value(QStringLiteral("prefs/paperZoom"), false).toBool());
    w.cameraLayout->addRow(w.paperZoom);
    w.screenPpi = new QSpinBox(w.camera);
    w.screenPpi->setObjectName(QStringLiteral("PrefScreenPpi"));
    w.screenPpi->setRange(30, 600);
    w.screenPpi->setValue(s.value(QStringLiteral("prefs/screenPpi"), qRound(QGuiApplication::primaryScreen() ? QGuiApplication::primaryScreen()->physicalDotsPerInchX() : 96.0)).toInt());
    w.screenPpi->setToolTip(tr("How many pixels of this screen make an inch, for paper zoom."));
    w.screenPpi->setEnabled(w.paperZoom->isChecked());
    connect(w.paperZoom, &QCheckBox::toggled, w.screenPpi, &QWidget::setEnabled);
    w.cameraLayout->addRow(tr("Number of Pixels per Inch"), w.screenPpi);

    Q_UNUSED(s);
    Q_UNUSED(dialog);
}

void MainWindow::preferencesSheet(PreferenceWidgets& w, QSettings& s, QDialog& dialog)
{
    w.sheet = new QGroupBox(tr("Exposure Sheet"), &dialog);
    w.extend = new QCheckBox(tr("Extend Exposure of Previous Drawing"), w.sheet);
    w.extend->setObjectName(QStringLiteral("PrefExtendExposure"));
    w.extend->setToolTip(tr("When you draw on an empty frame, the drawing before it is held over the empty frames up to it.\n"
                          "Off: those frames stay empty."));
    w.extend->setChecked(m_document->extendsPreviousExposure());
    w.sheetLayout = new QFormLayout(w.sheet);
    w.sheetLayout->addRow(w.extend);
    w.byFrame = new QCheckBox(tr("Use Current Frame as Drawing Name"), w.sheet);
    w.byFrame->setObjectName(QStringLiteral("PrefNameByFrame"));
    w.byFrame->setToolTip(tr("A drawing made on a frame is named after that frame; off, it goes by its number in the layer."));
    w.byFrame->setChecked(m_document->namesDrawingsByFrame());
    w.sheetLayout->addRow(w.byFrame);
    w.columnWidth = new QSpinBox(w.sheet);
    w.columnWidth->setObjectName(QStringLiteral("PrefXsheetColumnWidth"));
    w.columnWidth->setRange(40, 240);
    w.columnWidth->setSuffix(tr(" pixels"));
    w.columnWidth->setValue(std::clamp(s.value(QStringLiteral("prefs/xsheetColumnWidth"), 78).toInt(), 40, 240));
    w.sheetLayout->addRow(tr("Column width"), w.columnWidth);
    w.xsheetCentre = new QCheckBox(tr("Centre on current frame"), w.sheet);
    w.xsheetCentre->setObjectName(QStringLiteral("PrefXsheetCentre"));
    w.xsheetCentre->setToolTip(tr("The Xsheet keeps the current frame in the middle of the sheet, scrolling as it moves."));
    w.xsheetCentre->setChecked(s.value(QStringLiteral("prefs/xsheetCentre"), false).toBool());
    w.sheetLayout->addRow(w.xsheetCentre);
    w.xsheetHold = new QSpinBox(w.sheet);
    w.xsheetHold->setObjectName(QStringLiteral("PrefXsheetHold"));
    w.xsheetHold->setRange(1, 100);
    w.xsheetHold->setSuffix(tr(" frames"));
    w.xsheetHold->setToolTip(tr("A drawing typed into a cell of the Xsheet fills so many frames; the next is typed below them."));
    w.xsheetHold->setValue(s.value(QStringLiteral("prefs/xsheetHold"), 1).toInt());
    w.sheetLayout->addRow(tr("Default hold"), w.xsheetHold);
    preferencesSheetMore(w, s, dialog);

    Q_UNUSED(s);
    Q_UNUSED(dialog);
}

void MainWindow::preferencesAdvanced(PreferenceWidgets& w, QSettings& s, QDialog& dialog)
{
    w.advanced = new QGroupBox(tr("Advanced"), &dialog);
    w.moreArt = new QCheckBox(tr("Support Overlay and Underlay Arts"), w.advanced);
    w.moreArt->setObjectName(QStringLiteral("PrefOverlayUnderlay"));
    w.moreArt->setToolTip(tr("Two more sheets in every drawing: an overlay over the line art and an underlay beneath the colour art."));
    w.moreArt->setChecked(s.value(QStringLiteral("prefs/overlayUnderlay"), false).toBool());
    w.advancedLayout = new QFormLayout(w.advanced);
    w.advancedLayout->addRow(w.moreArt);
    w.elementMode = new QCheckBox(tr("Advanced Element Mode"), w.advanced);
    w.elementMode->setObjectName(QStringLiteral("PrefAdvancedElementMode"));
    w.elementMode->setToolTip(tr("Layer Properties shows which drawings a layer uses: a layer can use the drawings of another, timing them its own way."));
    w.elementMode->setChecked(s.value(QStringLiteral("prefs/advancedElementMode"), false).toBool());
    w.advancedLayout->addRow(w.elementMode);

    Q_UNUSED(s);
    Q_UNUSED(dialog);
}

void MainWindow::preferencesTimeline(PreferenceWidgets& w, QSettings& s, QDialog& dialog)
{
    // Timeline: how wide a frame is.
    w.timelinePage = new QGroupBox(tr("Timeline"), &dialog);
    w.frameWidth = new QSpinBox(w.timelinePage);
    w.frameWidth->setObjectName(QStringLiteral("PrefFrameWidth"));
    w.frameWidth->setRange(4, 30);
    w.frameWidth->setSuffix(tr(" pixels"));
    w.frameWidth->setValue(std::clamp(s.value(QStringLiteral("prefs/frameWidth"), 8).toInt(), 4, 30));
    w.frameWidth->setToolTip(tr("How wide a frame of the Timeline is when Eclipse starts (the zoom of the Timeline changes it afterwards)."));
    w.timelineLayout = new QFormLayout(w.timelinePage);
    w.timelineLayout->addRow(tr("Frame width"), w.frameWidth);
    w.stopMotion = new QCheckBox(tr("Default: Stop-Motion Keyframes"), w.timelinePage);
    w.stopMotion->setObjectName(QStringLiteral("PrefStopMotionKeys"));
    w.stopMotion->setToolTip(tr("A keyframe made new holds its pose until the next keyframe, instead of moving on to it.\n"
                              "(A keyframe can be turned either way afterwards, as always.)"));
    w.stopMotion->setChecked(m_document->stopMotionKeys());
    w.timelineLayout->addRow(w.stopMotion);
    w.loopPause = new QSpinBox(w.timelinePage);
    w.loopPause->setObjectName(QStringLiteral("PrefLoopPause"));
    w.loopPause->setRange(0, 5000);
    w.loopPause->setSingleStep(100);
    w.loopPause->setSuffix(tr(" ms"));
    w.loopPause->setSpecialValueText(tr("None"));
    w.loopPause->setToolTip(tr("Playing in a loop: how long the last frame is held before the loop plays again."));
    w.loopPause->setValue(m_loopPause);
    w.timelineLayout->addRow(tr("Pause at the end of a loop"), w.loopPause);
    w.showSound = new QCheckBox(tr("Show sound layers"), w.timelinePage);
    w.showSound->setObjectName(QStringLiteral("PrefShowSound"));
    w.showSound->setChecked(s.value(QStringLiteral("prefs/timelineSound"), true).toBool());
    w.timelineLayout->addRow(w.showSound);
    w.waveforms = new QCheckBox(tr("Show sound waveforms"), w.timelinePage);
    w.waveforms->setObjectName(QStringLiteral("PrefWaveforms"));
    w.waveforms->setChecked(s.value(QStringLiteral("prefs/timelineWaveforms"), true).toBool());
    w.timelineLayout->addRow(w.waveforms);
    w.showGroups = new QCheckBox(tr("Show group layers"), w.timelinePage);
    w.showGroups->setObjectName(QStringLiteral("PrefShowGroups"));
    w.showGroups->setChecked(s.value(QStringLiteral("prefs/timelineGroups"), true).toBool());
    w.timelineLayout->addRow(w.showGroups);
    w.sceneEnd = new QCheckBox(tr("Show End of Scene Marker"), w.timelinePage);
    w.sceneEnd->setObjectName(QStringLiteral("PrefSceneEnd"));
    w.sceneEnd->setToolTip(tr("A draggable end of scene marker is displayed on the timeline, allowing the user to modify the scene length."));
    w.sceneEnd->setChecked(s.value(QStringLiteral("prefs/timelineSceneEnd"), true).toBool());
    w.timelineLayout->addRow(w.sceneEnd);
    w.reduceIndent = new QCheckBox(tr("Reduce Indentation"), w.timelinePage);
    w.reduceIndent->setObjectName(QStringLiteral("PrefReduceIndent"));
    w.reduceIndent->setToolTip(tr("Reduce the indentation of the timeline."));
    w.reduceIndent->setChecked(s.value(QStringLiteral("prefs/timelineReduceIndent"), false).toBool());
    w.timelineLayout->addRow(w.reduceIndent);
    w.preroll = new QSpinBox(w.timelinePage);
    w.preroll->setObjectName(QStringLiteral("PrefPreroll"));
    w.preroll->setRange(0, 1000);
    w.preroll->setSpecialValueText(tr("None"));
    w.preroll->setSuffix(tr(" frames"));
    w.preroll->setToolTip(tr("Play starts so many frames before the current frame, to see what leads up to it."));
    w.preroll->setValue(m_preroll);
    w.timelineLayout->addRow(tr("Preroll"), w.preroll);
    w.loopAtStart = new QCheckBox(tr("Play in a loop when Eclipse starts"), w.timelinePage);
    w.loopAtStart->setObjectName(QStringLiteral("PrefLoop"));
    w.loopAtStart->setChecked(s.value(QStringLiteral("prefs/loop"), true).toBool());
    w.timelineLayout->addRow(w.loopAtStart);
    w.functionGrid = new QCheckBox(tr("Show the grid in the Function view"), w.timelinePage);
    w.functionGrid->setObjectName(QStringLiteral("PrefFunctionGrid"));
    w.functionGrid->setChecked(s.value(QStringLiteral("prefs/functionGrid"), true).toBool());
    w.timelineLayout->addRow(w.functionGrid);
    // Function Editors.
    w.functionLoadLimit = new QSpinBox(w.timelinePage);
    w.functionLoadLimit->setObjectName(QStringLiteral("PrefFunctionLoadLimit"));
    w.functionLoadLimit->setRange(1, 1000);
    w.functionLoadLimit->setToolTip(tr("Controls the number of functions to display in the function editor."));
    w.functionLoadLimit->setValue(s.value(QStringLiteral("prefs/functionLoadLimit"), 50).toInt());
    w.timelineLayout->addRow(tr("Number of functions displayed in the canvas"), w.functionLoadLimit);
    w.pathConstantZ = new QCheckBox(tr("3D Path Constant Z Default"), w.timelinePage);
    w.pathConstantZ->setObjectName(QStringLiteral("PrefPathConstantZ"));
    w.pathConstantZ->setToolTip(tr("Sets On or Off the Constant Z attribute of new 3D path function columns."));
    w.pathConstantZ->setChecked(s.value(QStringLiteral("prefs/pathConstantZ"), false).toBool());
    w.timelineLayout->addRow(w.pathConstantZ);

    Q_UNUSED(s);
    Q_UNUSED(dialog);
}

void MainWindow::preferencesRender(PreferenceWidgets& w, QSettings& s, QDialog& dialog)
{
    // Render: the preview.
    w.renderPage = new QGroupBox(tr("Render"), &dialog);
    w.previewSize = new QComboBox(w.renderPage);
    w.previewSize->setObjectName(QStringLiteral("PrefPreviewSize"));
    w.previewSize->addItem(tr("Full size"), 100);
    w.previewSize->addItem(tr("Half size"), 50);
    w.previewSize->addItem(tr("Quarter size"), 25);
    w.previewSize->setCurrentIndex(std::max(0, w.previewSize->findData(s.value(QStringLiteral("prefs/previewSize"), 50).toInt())));
    w.autoRender = new QCheckBox(tr("Auto Render the preview"), w.renderPage);
    w.autoRender->setObjectName(QStringLiteral("PrefAutoRender"));
    w.autoRender->setToolTip(tr("The preview of the Preview Manager is brought up to date by itself a moment after each change."));
    w.autoRender->setChecked(s.value(QStringLiteral("prefs/autoRender"), false).toBool());
    w.renderLayout = new QFormLayout(w.renderPage);
    w.renderLayout->addRow(tr("Preview size"), w.previewSize);
    w.renderLayout->addRow(w.autoRender);
    w.renderBlurs = new QCheckBox(tr("Enable blur"), w.renderPage);
    w.renderBlurs->setObjectName(QStringLiteral("PrefRenderBlurs"));
    w.renderBlurs->setToolTip(tr("Off: Blur effects are left out wherever a frame is drawn - faster while working; switch on again to render."));
    w.renderBlurs->setChecked(s.value(QStringLiteral("prefs/renderBlurs"), true).toBool());
    w.renderLayout->addRow(w.renderBlurs);
    w.renderFocus = new QCheckBox(tr("Enable focus"), w.renderPage);
    w.renderFocus->setObjectName(QStringLiteral("PrefRenderFocus"));
    w.renderFocus->setToolTip(tr("Off: Focus (depth of field) effects are left out wherever a frame is drawn."));
    w.renderFocus->setChecked(s.value(QStringLiteral("prefs/renderFocus"), true).toBool());
    w.renderLayout->addRow(w.renderFocus);
    w.renderToneHighlight = new QCheckBox(tr("Enable tone and highlight"), w.renderPage);
    w.renderToneHighlight->setObjectName(QStringLiteral("PrefRenderToneHighlight"));
    w.renderToneHighlight->setToolTip(tr("Off: Tone and Highlight effects are left out wherever a frame is drawn - faster while working."));
    w.renderToneHighlight->setChecked(s.value(QStringLiteral("prefs/renderToneHighlight"), true).toBool());
    w.renderLayout->addRow(w.renderToneHighlight);
    w.renderColourOverride = new QCheckBox(tr("Enable colour override"), w.renderPage);
    w.renderColourOverride->setObjectName(QStringLiteral("PrefRenderColourOverride"));
    w.renderColourOverride->setToolTip(tr("Off: Colour-Override nodes let the layer through in its own colours."));
    w.renderColourOverride->setChecked(s.value(QStringLiteral("prefs/renderColourOverride"), true).toBool());
    w.renderLayout->addRow(w.renderColourOverride);
    auto check = [&](QCheckBox*& box, QString const& text, char const* name, char const* key, bool usual, QString const& tip) {
        box = new QCheckBox(text, w.renderPage);
        box->setObjectName(QLatin1String(name));
        box->setToolTip(tip);
        box->setChecked(s.value(QLatin1String(key), usual).toBool());
        w.renderLayout->addRow(box);
    };
    check(w.lineTextures, tr("Enable line texture"), "PrefLineTextures", "prefs/lineTextures", true,
          tr("Off: lines and brush strokes are drawn solid instead of with their pencil textures, in renders as well."));
    // Rendering: how many frames at once, and what the Render View keeps.
    w.renderThreads = new QSpinBox(w.renderPage);
    w.renderThreads->setObjectName(QStringLiteral("PrefRenderThreads"));
    w.renderThreads->setRange(0, 64);
    w.renderThreads->setSpecialValueText(tr("Auto (%1)").arg(QThread::idealThreadCount()));
    w.renderThreads->setToolTip(tr("How many frames are rendered at once for exports, write outputs and the preview. "
                                   "0 sets it by itself from the number of cores of the computer."));
    w.renderThreads->setValue(s.value(QStringLiteral("prefs/renderThreads"), 0).toInt());
    w.renderLayout->addRow(tr("Number of Rendering Threads (0=auto)"), w.renderThreads);
    w.renderCacheMb = new QSpinBox(w.renderPage);
    w.renderCacheMb->setObjectName(QStringLiteral("PrefRenderCache"));
    w.renderCacheMb->setRange(0, 64 * 1024);
    w.renderCacheMb->setSingleStep(64);
    w.renderCacheMb->setSuffix(tr(" MB"));
    w.renderCacheMb->setToolTip(tr("Render View Image Cache: how much memory the frames the Render View rendered may take, kept to be shown "
                                   "again without rendering them again (also while playing). 0 keeps only the frame shown."));
    w.renderCacheMb->setValue(s.value(QStringLiteral("prefs/renderCacheMb"), 256).toInt());
    QPushButton* const clearCache = new QPushButton(tr("Clear Cache"), w.renderPage);
    clearCache->setObjectName(QStringLiteral("PrefClearRenderCache"));
    connect(clearCache, &QPushButton::clicked, &dialog, [this, &dialog] {
        if (QMessageBox::question(&dialog, tr("Render View Image Cache"), tr("Are you sure you want to clear the cached images?")) == QMessageBox::Yes)
            for (Canvas* const view : findChildren<Canvas*>())
                view->clearRenderCache();
    });
    QHBoxLayout* const cacheRow = new QHBoxLayout;
    cacheRow->addWidget(w.renderCacheMb, 1);
    cacheRow->addWidget(clearCache);
    w.renderLayout->addRow(tr("Render View Image Cache"), cacheRow);
    check(w.disableRenderPreview, tr("Disable render preview in camera view"), "PrefDisableRenderPreview", "prefs/disableRenderPreview", false,
          tr("The view modes and the Render View's buttons are taken out of the Camera view's status bar."));
    // The Camera view: what it may leave out to be quicker (renders always have all of it), and its time limits.
    w.renderLayout->addRow(new QLabel(tr("<b>Camera view</b> (renders are not changed by these)"), w.renderPage));
    check(w.viewEffects, tr("Render effects"), "PrefViewEffects", "prefs/viewEffects", true,
          tr("Off: the Normal View shows the frame without effects - quicker. The Render View still shows them."));
    check(w.viewCutters, tr("Enable cutters and masks"), "PrefViewCutters", "prefs/viewCutters", true,
          tr("Disabling cutters and mask effects will produce a drawing performance improvement in the Camera view."));
    check(w.viewTextures, tr("Enable textures and gradients in drawings"), "PrefViewTextures", "prefs/viewTextures", true,
          tr("Off: gradients and textured colours are shown as their colour alone - quicker."));
    check(w.viewPencilTextures, tr("Enable textures in pencil lines"), "PrefViewPencilTextures", "prefs/viewPencilTextures", true,
          tr("Off: pencil textures are left out of the view - quicker."));
    check(w.viewLineThickness, tr("Enable variable line thickness"), "PrefViewLineThickness", "prefs/viewLineThickness", true,
          tr("Off: lines of changing thickness are shown as thick all along as they are on average - quicker."));
    check(w.smoothTextures, tr("Smooth textures"), "PrefSmoothTextures", "prefs/smoothTextures", true,
          tr("Pictures in drawings are scaled smoothly in the view. Off: to the nearest pixel - quicker."));
    check(w.viewAntialias, tr("Full scene antialiasing"), "PrefViewAntialias", "prefs/viewAntialias", true,
          tr("The view's edges are smoothed. Off: hard edges - quicker."));
    w.viewAntialiasMode = new QComboBox(w.renderPage);
    w.viewAntialiasMode->setObjectName(QStringLiteral("PrefViewAntialiasMode"));
    w.viewAntialiasMode->addItem(tr("Performance"));
    w.viewAntialiasMode->addItem(tr("Quality"));
    w.viewAntialiasMode->setToolTip(tr("Anti-Aliasing Mode: Performance draws paint with a quick edge while things move; Quality keeps its fine edge always."));
    w.viewAntialiasMode->setCurrentIndex(std::clamp(s.value(QStringLiteral("prefs/viewAntialiasMode"), 0).toInt(), 0, 1));
    w.renderLayout->addRow(tr("Anti-Aliasing Mode"), w.viewAntialiasMode);
    auto milliseconds = [&](QSpinBox*& box, char const* name, char const* key, QString const& tip) {
        box = new QSpinBox(w.renderPage);
        box->setObjectName(QLatin1String(name));
        box->setRange(0, 60000);
        box->setSingleStep(25);
        box->setSuffix(tr(" ms"));
        box->setSpecialValueText(tr("None"));
        box->setToolTip(tip);
        box->setValue(s.value(QLatin1String(key), 0).toInt());
    };
    milliseconds(w.effectsTimeout, "PrefEffectsTimeout", "prefs/effectsTimeout",
                 tr("Effect Render Timeout: past this the Normal View leaves out the effects still to be drawn, to keep working quick (renders are whole)."));
    milliseconds(w.drawTimeout, "PrefDrawTimeout", "prefs/drawTimeout",
                 tr("Render Timeout: past this the Camera view stops drawing the layers still to be drawn; the picture is incomplete until it is drawn again."));
    w.renderLayout->addRow(tr("Effect Render Timeout"), w.effectsTimeout);
    w.renderLayout->addRow(tr("Render Timeout"), w.drawTimeout);
    // What new drawing layers start with (each layer's own is in Layer Properties).
    w.renderLayout->addRow(new QLabel(tr("<b>New layers</b>"), w.renderPage));
    w.newAntialiasing = new QComboBox(w.renderPage);
    w.newAntialiasing->setObjectName(QStringLiteral("PrefNewAntialiasing"));
    w.newAntialiasing->addItems({tr("Low"), tr("Medium"), tr("High"), tr("Very High")});
    w.newAntialiasing->setToolTip(tr("Antialiasing Quality of new drawing layers: Low draws hard edges; Very High works the edges out at twice the size."));
    w.newAntialiasing->setCurrentIndex(std::clamp(s.value(QStringLiteral("prefs/newAntialiasing"), 2).toInt(), 0, 3));
    w.renderLayout->addRow(tr("Antialiasing Quality"), w.newAntialiasing);
    w.newAntialiasExponent = new QDoubleSpinBox(w.renderPage);
    w.newAntialiasExponent->setObjectName(QStringLiteral("PrefNewAntialiasExponent"));
    w.newAntialiasExponent->setRange(0.1, 10.0);
    w.newAntialiasExponent->setSingleStep(0.1);
    w.newAntialiasExponent->setToolTip(tr("Antialiasing Exponent of new drawing layers: above 1 their soft edges are harder, below 1 softer."));
    w.newAntialiasExponent->setValue(s.value(QStringLiteral("prefs/newAntialiasExponent"), 1.0).toDouble());
    w.renderLayout->addRow(tr("Antialiasing Exponent"), w.newAntialiasExponent);
}

void MainWindow::preferencesTools(PreferenceWidgets& w, QSettings& s, QDialog& dialog)
{
    // Tools: what the w.drawing tools start with.
    w.toolsPage = new QGroupBox(tr("Tools"), &dialog);
    w.toolSize = new QSpinBox(w.toolsPage);
    w.toolSize->setObjectName(QStringLiteral("PrefToolSize"));
    w.toolSize->setRange(1, 200);
    w.toolSize->setSuffix(tr(" pixels"));
    w.toolSize->setValue(std::clamp(s.value(QStringLiteral("prefs/toolSize"), int(m_canvas->brushWidth())).toInt(), 1, 200));
    w.toolSize->setToolTip(tr("The size of the brush, the eraser and the other drawing tools when Eclipse starts."));
    w.toolsLayout = new QFormLayout(w.toolsPage);
    w.toolsLayout->addRow(tr("Size of the drawing tools"), w.toolSize);
    w.brushOutline = new QCheckBox(tr("Show the brush's size round the pointer"), w.toolsPage);
    w.brushOutline->setObjectName(QStringLiteral("PrefBrushOutline"));
    w.brushOutline->setToolTip(tr("The Brush, the Eraser and the Pencil show a circle as big as what they draw, where the pointer is."));
    w.brushOutline->setChecked(s.value(QStringLiteral("prefs/brushOutline"), false).toBool());
    w.toolsLayout->addRow(w.brushOutline);
    w.pressureFeel = new QSpinBox(w.toolsPage);
    w.pressureFeel->setObjectName(QStringLiteral("PrefPressureFeel"));
    w.pressureFeel->setRange(1, 100);
    w.pressureFeel->setToolTip(tr("The feel of the pen tablet: 50 as you press; more for a light touch (a little pressure gives a thick line),\n"
                                "less for a heavy one (it takes pressing hard)."));
    w.pressureFeel->setValue(s.value(QStringLiteral("prefs/pressureFeel"), 50).toInt());
    w.toolsLayout->addRow(tr("Pen pressure feel"), w.pressureFeel);
    w.units = new QComboBox(w.toolsPage);
    w.units->setObjectName(QStringLiteral("PrefUnits"));
    w.units->addItems({tr("Fields"), tr("Pixels")});
    w.units->setCurrentIndex(std::clamp(s.value(QStringLiteral("prefs/units"), 0).toInt(), 0, 1));
    w.units->setToolTip(tr("How positions are written in the Coord. and Control Points view: in fields, with the letter of the direction (3 E, 2 N), or in pixels of the scene."));
    w.toolsLayout->addRow(tr("Positions in"), w.units);
    w.smoothing = new QSpinBox(w.toolsPage);
    w.smoothing->setObjectName(QStringLiteral("PrefSmoothing"));
    w.smoothing->setRange(0, 100);
    w.smoothing->setSuffix(tr(" %"));
    w.smoothing->setToolTip(tr("How much a line drawn with the brush or pencil is smoothed when the pen lifts, when Eclipse starts."));
    w.smoothing->setValue(std::clamp(s.value(QStringLiteral("prefs/smoothing"), 25).toInt(), 0, 100));
    w.toolsLayout->addRow(tr("Smoothing"), w.smoothing);
    w.corners = new QSpinBox(w.toolsPage);
    w.corners->setObjectName(QStringLiteral("PrefCorners"));
    w.corners->setRange(0, 180);
    w.corners->setSuffix(tr(" deg"));
    w.corners->setToolTip(tr("A turn sharper than this many degrees stays a sharp corner when a line is smoothed, when Eclipse starts."));
    w.corners->setValue(std::clamp(s.value(QStringLiteral("prefs/corners"), 50).toInt(), 0, 180));
    w.toolsLayout->addRow(tr("Corners"), w.corners);

    Q_UNUSED(s);
    Q_UNUSED(dialog);
}

void MainWindow::preferencesOnion(PreferenceWidgets& w, QSettings& s, QDialog& dialog)
{
    // Onion Skin: how many drawings either side it shows, and how strongly, when Eclipse starts.
    w.onionPage = new QGroupBox(tr("Onion Skin"), &dialog);
    w.onionLayout = new QFormLayout(w.onionPage);
    w.onionCountBefore = new QSpinBox(w.onionPage);
    w.onionCountBefore->setObjectName(QStringLiteral("PrefOnionPrevious"));
    w.onionCountBefore->setRange(0, 6);
    w.onionCountBefore->setValue(std::clamp(s.value(QStringLiteral("prefs/onionPrevious"), m_canvas->onionPrevious()).toInt(), 0, 6));
    w.onionCountAfter = new QSpinBox(w.onionPage);
    w.onionCountAfter->setObjectName(QStringLiteral("PrefOnionNext"));
    w.onionCountAfter->setRange(0, 6);
    w.onionCountAfter->setValue(std::clamp(s.value(QStringLiteral("prefs/onionNext"), m_canvas->onionNext()).toInt(), 0, 6));
    w.onionStrength = new QSpinBox(w.onionPage);
    w.onionStrength->setObjectName(QStringLiteral("PrefOnionOpacity"));
    w.onionStrength->setRange(5, 100);
    w.onionStrength->setSuffix(tr(" %"));
    w.onionStrength->setValue(std::clamp(s.value(QStringLiteral("prefs/onionOpacity"), m_canvas->onionOpacity()).toInt(), 5, 100));
    w.onionLayout->addRow(tr("Drawings before"), w.onionCountBefore);
    w.onionLayout->addRow(tr("Drawings after"), w.onionCountAfter);
    w.onionLayout->addRow(tr("Opacity"), w.onionStrength);
    w.onionFade = new QCheckBox(tr("Fade with distance"), w.onionPage);
    w.onionFade->setObjectName(QStringLiteral("PrefOnionFade"));
    w.onionFade->setToolTip(tr("The drawings nearest the current one show the most, those furthest the least."));
    w.onionFade->setChecked(s.value(QStringLiteral("prefs/onionFade"), false).toBool());
    w.onionLayout->addRow(w.onionFade);
    w.onionNearest = new QSpinBox(w.onionPage);
    w.onionNearest->setObjectName(QStringLiteral("PrefOnionNearest"));
    w.onionNearest->setRange(0, 100);
    w.onionNearest->setSuffix(tr(" %"));
    w.onionNearest->setValue(std::clamp(s.value(QStringLiteral("prefs/onionNearest"), 100).toInt(), 0, 100));
    w.onionLayout->addRow(tr("Nearest"), w.onionNearest);
    w.onionFurthest = new QSpinBox(w.onionPage);
    w.onionFurthest->setObjectName(QStringLiteral("PrefOnionFurthest"));
    w.onionFurthest->setRange(0, 100);
    w.onionFurthest->setSuffix(tr(" %"));
    w.onionFurthest->setValue(std::clamp(s.value(QStringLiteral("prefs/onionFurthest"), 30).toInt(), 0, 100));
    w.onionLayout->addRow(tr("Furthest"), w.onionFurthest);
    w.onionBrightness = new QCheckBox(tr("Lighter colours more see-through"), w.onionPage);
    w.onionBrightness->setObjectName(QStringLiteral("PrefOnionBrightness"));
    w.onionBrightness->setToolTip(tr("In the onion skins, the lighter a colour is the fainter it shows: the painted areas fade, the dark lines stay."));
    w.onionBrightness->setChecked(s.value(QStringLiteral("prefs/onionBrightnessAlpha"), false).toBool());
    w.onionLayout->addRow(w.onionBrightness);
    w.onionOver = new QCheckBox(tr("Over the drawings, not under them"), w.onionPage);
    w.onionOver->setObjectName(QStringLiteral("PrefOnionOver"));
    w.onionOver->setToolTip(tr("The onion skins are shown on top of everything, rather than underneath the drawing being worked on."));
    w.onionOver->setChecked(s.value(QStringLiteral("prefs/onionOver"), false).toBool());
    w.onionLayout->addRow(w.onionOver);
    w.onionStyle = new QComboBox(w.onionPage);
    w.onionStyle->setObjectName(QStringLiteral("PrefOnionStyle"));
    w.onionStyle->addItems({tr("Tinted"), tr("Original Colours"), tr("Outlines")});
    w.onionStyle->setToolTip(tr("How the drawings before and after are shown: washed with the onion skin colours, in their own colours, or as outlines."));
    w.onionStyle->setCurrentIndex(m_canvas->onionOutlines() ? 2 : m_canvas->onionOriginal() ? 1 : 0);
    w.onionLayout->addRow(tr("Onion Skin Render Style"), w.onionStyle);
    w.onionWashMin = new QSpinBox(w.onionPage);
    w.onionWashMin->setObjectName(QStringLiteral("PrefOnionWashMin"));
    w.onionWashMax = new QSpinBox(w.onionPage);
    w.onionWashMax->setObjectName(QStringLiteral("PrefOnionWashMax"));
    for (QSpinBox* const box : {w.onionWashMin, w.onionWashMax}) {
        box->setRange(0, 100);
        box->setSuffix(tr(" %"));
        box->setToolTip(tr("Apply Colour Wash (Onion Skin view): how much of the onion colour is laid over the drawings' own colours, the furthest (minimum) to the nearest (maximum)."));
    }
    w.onionWashMin->setValue(m_canvas->onionWashMin());
    w.onionWashMax->setValue(m_canvas->onionWashMax());
    w.onionLayout->addRow(tr("Onion Skin: Minimum Wash Value"), w.onionWashMin);
    w.onionLayout->addRow(tr("Onion Skin: Maximum Wash Value"), w.onionWashMax);
    w.onionOthers = new QCheckBox(tr("Enable Onion Skin for Other Elements"), w.onionPage);
    w.onionOthers->setObjectName(QStringLiteral("PrefOnionOthers"));
    w.onionOthers->setToolTip(tr("Layers added to the onion skin (Timeline) show theirs too; off, only the layer worked on does."));
    w.onionOthers->setChecked(m_canvas->onionOthers());
    w.onionLayout->addRow(w.onionOthers);
    w.onionEnhanced = new QCheckBox(tr("Shows Auto Light Table on Every Drawing Visible in the Onion Skin"), w.onionPage);
    w.onionEnhanced->setObjectName(QStringLiteral("PrefOnionEnhanced"));
    w.onionEnhanced->setToolTip(tr("With the light table on, every layer it shows has its onion skin too, not only the layer worked on and those added to the onion skin."));
    w.onionEnhanced->setChecked(s.value(QStringLiteral("prefs/onionEnhanced"), false).toBool());
    w.onionLayout->addRow(w.onionEnhanced);

    Q_UNUSED(s);
    Q_UNUSED(dialog);
}

void MainWindow::preferencesSound(PreferenceWidgets& w, QSettings& s, QDialog& dialog)
{
    // Sound: when it is heard.
    w.soundPage = new QGroupBox(tr("Sound"), &dialog);
    w.soundOn = new QCheckBox(tr("Play the sound when the scene plays"), w.soundPage);
    w.soundOn->setObjectName(QStringLiteral("PrefSoundOn"));
    w.soundOn->setChecked(m_soundOn);
    w.scrub = new QCheckBox(tr("Play a frame of sound when the frame is changed by hand (scrubbing)"), w.soundPage);
    w.scrub->setObjectName(QStringLiteral("PrefScrub"));
    w.scrub->setChecked(m_scrub);
    w.soundLayout = new QFormLayout(w.soundPage);
    w.soundLayout->addRow(w.soundOn);
    w.soundLayout->addRow(w.scrub);
    w.lipLead = new QSpinBox(w.soundPage);
    w.lipLead->setObjectName(QStringLiteral("PrefLipSyncLead"));
    w.lipLead->setRange(0, 4);
    w.lipLead->setSuffix(tr(" frames"));
    w.lipLead->setToolTip(tr("Auto lip-sync: the mouth shapes shown so many frames before they are heard (the mouth gets there first)."));
    w.lipLead->setValue(s.value(QStringLiteral("prefs/lipSyncLead"), 0).toInt());
    w.soundLayout->addRow(tr("Lip-sync shapes early by"), w.lipLead);

    Q_UNUSED(s);
    Q_UNUSED(dialog);
}
