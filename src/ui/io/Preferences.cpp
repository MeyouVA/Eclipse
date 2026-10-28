// Eclipse - the main window: preferences - applied, the dialog, preference sets, languages, the style editor.
#include "ui/window/MainWindow.h"

#include <functional>

#include <algorithm>
#include <cmath>

#include <QApplication>
#include <QTimer>
#include <QPlainTextEdit>
#include <QCheckBox>
#include "ui/panels/ColourEditor.h"
#include <QTabWidget>
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
#include "ui/views/DepthView.h"
#include "ui/views/PerspectiveView.h"
#include "actions/theme/InterfaceColours.h"
#include <QHeaderView>
#include <QTreeWidget>
#include <QLibraryInfo>
#include <QTranslator>

#include <tuple>
#include "ui/io/PreferenceWidgets.h"

static QSettings preferences()
{
    return QSettings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse"));
}

void MainWindow::applyPreferences()
{
    QSettings const s = preferences();
    // The colours the views paint with: read again, and every view painted with them.
    InterfaceColours::reload();
    for (DepthView* view : findChildren<DepthView*>())
        view->setPlanningPreferences(s.value(QStringLiteral("prefs/planningDefaultZoom"), 100).toDouble() / 100.0,
                                     s.value(QStringLiteral("prefs/planningThumbnailSize"), 64).toInt());
    for (QWidget* const w : findChildren<QWidget*>())
        w->update();
    m_canvas->setLasso(s.value(QStringLiteral("prefs/lasso"), true).toBool());
    m_canvas->setPencilLinesOpacity(s.value(QStringLiteral("prefs/pencilLinesOpacity"), 100).toInt());
    m_canvas->setBitmapCanvasSize(s.value(QStringLiteral("prefs/bitmapCanvas"), 150).toInt(),
                                  s.value(QStringLiteral("prefs/bitmapCanvasHeight"), s.value(QStringLiteral("prefs/bitmapCanvas"), 150)).toInt());
    m_canvas->setBitmapPpi(s.value(QStringLiteral("prefs/bitmapPpi"), 72).toInt());
    m_canvas->setBitmapCameraDensity(s.value(QStringLiteral("prefs/bitmapCameraDensity"), false).toBool());
    bool const outlineLocked = s.value(QStringLiteral("prefs/outlineLocked"), false).toBool();
    if (m_appliedOutlinePreference != int(outlineLocked)) {
        if (QAction* const action = m_actions->action(QStringLiteral("OutlineLocked"))) {
            if (action->isChecked() != outlineLocked)
                action->trigger();
            else
                m_canvas->onActionOutlineLocked(outlineLocked);
            m_appliedOutlinePreference = int(outlineLocked);
        }
    }
    bool const bboxSelection = s.value(QStringLiteral("prefs/bboxSelection"), false).toBool();
    if (m_appliedBBoxPreference != int(bboxSelection)) {
        if (QAction* const action = m_actions->action(QStringLiteral("BBoxSelectionStyle"))) {
            if (action->isChecked() != bboxSelection)
                action->trigger();
            else
                m_canvas->onActionBBoxSelectionStyle(bboxSelection);
            m_appliedBBoxPreference = int(bboxSelection);
        }
    }
    m_canvas->setBitmapCanvasBasis(s.value(QStringLiteral("prefs/bitmapCanvasBasis"), 0).toInt());
    m_canvas->setPressureFeel(s.value(QStringLiteral("prefs/pressureFeel"), 50).toInt());
    // Palettes: element palettes shown, the Colour view's background, colour recovery and what pasted templates do with colours.
    bool const recovery = s.value(QStringLiteral("prefs/colourRecovery"), true).toBool();
    Canvas::s_colourRecovery = recovery;
    Canvas::s_interactiveRecovery = s.value(QStringLiteral("prefs/interactiveRecovery"), false).toBool();
    Scene::setReplacementColour(recovery ? QColor() : s.value(QStringLiteral("prefs/replacementColour"), QColor(255, 0, 0)).value<QColor>());
    Scene::setPasteColours(s.value(QStringLiteral("prefs/pasteColours"), 0).toInt());
    m_palette->setElementPalettesShown(s.value(QStringLiteral("prefs/advancedPalettes"), true).toBool());
    m_palette->setListBackground(s.value(QStringLiteral("prefs/paletteBackground"), QColor()).value<QColor>());
    m_palette->setSwatchGrid(s.value(QStringLiteral("colour/grid"), false).toBool());
    m_canvas->setStickyDropper(s.value(QStringLiteral("prefs/stickyDropper"), false).toBool());
    m_canvas->setCloseGapInPixels(s.value(QStringLiteral("prefs/gapInPixels"), false).toBool());
    bool const moreArt = s.value(QStringLiteral("prefs/overlayUnderlay"), false).toBool();
    for (char const* id : {"SetOverlayArt", "SetUnderlayArt"}) {
        if (QAction* const a = m_actions->action(QLatin1String(id)))
            a->setVisible(moreArt);
        if (QToolButton* const b = findChild<QToolButton*>(QStringLiteral("Art") + QLatin1String(id)))
            b->setVisible(moreArt);
    }
    if (!moreArt && (m_canvas->art() == Canvas::OverlayArt || m_canvas->art() == Canvas::UnderlayArt))
        m_canvas->setArt(Canvas::LineArt);
    m_document->setUndoLevels(std::clamp(s.value(QStringLiteral("prefs/undoLevels"), 100).toInt(), 0, 10000));
    m_document->setNameDrawingsByFrame(s.value(QStringLiteral("prefs/nameByFrame"), false).toBool());
    m_document->setExtendPreviousExposure(s.value(QStringLiteral("prefs/extendExposure"), true).toBool());
    m_document->setStopMotionKeys(s.value(QStringLiteral("prefs/stopMotionKeys"), false).toBool());
    m_canvas->setKeysFrameOne(s.value(QStringLiteral("prefs/keyFrameOne"), true).toBool());
    m_canvas->setNudgeStep(std::clamp(s.value(QStringLiteral("prefs/nudge"), 1).toInt(), 1, 100));
    m_canvas->setProportionParts(std::clamp(s.value(QStringLiteral("prefs/proportionAcross"), 3).toInt(), 2, 12),
                                 std::clamp(s.value(QStringLiteral("prefs/proportionDown"), 3).toInt(), 2, 12));
    m_canvas->setSafeMargin(std::clamp(s.value(QStringLiteral("prefs/safeMargin"), 10).toInt(), 1, 30) / 100.0);
    m_canvas->setBackdrop(QColor(s.value(QStringLiteral("prefs/backdrop"), QStringLiteral("#7f7f7f")).toString()));
    m_canvas->setLightTableWash(s.value(QStringLiteral("prefs/lightTableWash"), 30).toInt());
    m_canvas->setLightTableWhite(s.value(QStringLiteral("prefs/lightTableWhite"), 0).toInt());
    m_canvas->setLightTableShade(s.value(QStringLiteral("prefs/lightTableShade"), false).toBool(), s.value(QStringLiteral("prefs/lightTableShadeCamera"), false).toBool());
    m_canvas->setOnionWashRange(s.value(QStringLiteral("prefs/onionWashMin"), 20).toInt(), s.value(QStringLiteral("prefs/onionWashMax"), 60).toInt());
    m_autoSaveMinutes = std::clamp(s.value(QStringLiteral("prefs/autosaveMinutes"), 0).toInt(), 0, 240);
    // Timeline: how wide a frame is to begin with.  Render: the preview's size, and whether it keeps itself up to date.
    m_timeline->setCellWidth(std::clamp(s.value(QStringLiteral("prefs/frameWidth"), 8).toInt(), 4, 30));
    m_timeline->setRulerLocked(s.value(QStringLiteral("prefs/lockTimelineRuler"), false).toBool());
    m_loopPause = std::clamp(s.value(QStringLiteral("prefs/loopPause"), 0).toInt(), 0, 5000);
    for (Canvas* const view : findChildren<Canvas*>()) {
        view->setZoomOnMouse(s.value(QStringLiteral("prefs/zoomOnMouse"), false).toBool());
        view->setZoomLimits(s.value(QStringLiteral("prefs/zoomMin"), 2).toInt(), s.value(QStringLiteral("prefs/zoomMax"), 6400).toInt());
        view->setDefaultZoom(s.value(QStringLiteral("prefs/defaultZoom"), 100).toInt());
        view->setQuickCloseUpFactor(s.value(QStringLiteral("prefs/quickCloseUp"), 4.0).toDouble());
        view->setPaperZoom(s.value(QStringLiteral("prefs/paperZoom"), false).toBool(), s.value(QStringLiteral("prefs/screenPpi"), 96).toInt());
        emit view->zoomChanged();
    }
    m_actions->setChecked(QStringLiteral("LockTimelineRuler"), m_timeline->rulerLocked());
    if (PreviewManagerView* const preview = findChild<PreviewManagerView*>()) {
        preview->setSizePercent(s.value(QStringLiteral("prefs/previewSize"), 50).toInt());
        bool const autoRender = s.value(QStringLiteral("prefs/autoRender"), false).toBool();
        if (preview->autoRender() != autoRender) {
            preview->setAutoRender(autoRender);
            m_actions->setChecked(QStringLiteral("AutoRender"), autoRender);
        }
    }
    // Tools: the size the drawing tools start with (set through the Size box, so Tool Properties shows it).
    if (s.contains(QStringLiteral("prefs/toolSize")))
        if (QSpinBox* const size = findChild<QSpinBox*>(QStringLiteral("ToolSize")))
            size->setValue(std::clamp(s.value(QStringLiteral("prefs/toolSize")).toInt(), 1, 200));
    // Smoothing and corners of drawn lines (through Tool Properties, so it shows them).
    for (auto const& [key, box, usual] : {std::tuple<char const*, char const*, int>{"prefs/smoothing", "Smoothing", 25}, {"prefs/corners", "Corners", 50}})
        if (s.contains(QLatin1String(key)))
            if (QSpinBox* const spin = findChild<QSpinBox*>(QLatin1String(box)))
                spin->setValue(s.value(QLatin1String(key), usual).toInt());
    // Onion skin: how many drawings either side, and how strongly (as set; otherwise as it is).
    if (s.contains(QStringLiteral("prefs/onionPrevious")) || s.contains(QStringLiteral("prefs/onionNext")))
        m_canvas->setOnionRange(std::clamp(s.value(QStringLiteral("prefs/onionPrevious"), m_canvas->onionPrevious()).toInt(), 0, 6),
                                std::clamp(s.value(QStringLiteral("prefs/onionNext"), m_canvas->onionNext()).toInt(), 0, 6));
    if (s.contains(QStringLiteral("prefs/onionOpacity")))
        m_canvas->setOnionOpacity(std::clamp(s.value(QStringLiteral("prefs/onionOpacity"), 50).toInt(), 5, 100));
    // Node View: the map in the corner, and straight or curved cables.
    for (NodeView* const view : findChildren<NodeView*>()) {
        view->setNavigatorShown(s.value(QStringLiteral("prefs/nodeNavigator"), true).toBool());
        view->setStraightCables(s.value(QStringLiteral("prefs/nodeStraight"), false).toBool());
    }
    // Units: positions in fields or in pixels.
    if (CoordView* const coord = findChild<CoordView*>())
        coord->setPixelUnits(s.value(QStringLiteral("prefs/units"), 0).toInt() == 1);
    // Sound: heard while playing, and while the frame is changed by hand.
    m_soundOn = s.value(QStringLiteral("prefs/soundOn"), true).toBool();
    m_actions->setChecked(QStringLiteral("PlaySound"), m_soundOn);
    m_scrub = s.value(QStringLiteral("prefs/scrub"), false).toBool();
    m_actions->setChecked(QStringLiteral("PlayScrub"), m_scrub);
    m_autoSave.stop();
    if (m_autoSaveMinutes > 0)
        m_autoSave.start(m_autoSaveMinutes * 60 * 1000);
    m_keepBackup = s.value(QStringLiteral("prefs/keepBackup"), true).toBool();
    m_askAutoSave = s.value(QStringLiteral("prefs/askAutoSave"), false).toBool();
    m_preroll = std::max(0, s.value(QStringLiteral("prefs/preroll"), 0).toInt());
    m_timeline->setKindShown(Layer::Sound, s.value(QStringLiteral("prefs/timelineSound"), true).toBool());
    m_timeline->setKindShown(Layer::Group, s.value(QStringLiteral("prefs/timelineGroups"), true).toBool());
    m_timeline->setShowWaveforms(s.value(QStringLiteral("prefs/timelineWaveforms"), true).toBool());
    m_timeline->setShowSceneEnd(s.value(QStringLiteral("prefs/timelineSceneEnd"), true).toBool());
    m_timeline->setReduceIndentation(s.value(QStringLiteral("prefs/timelineReduceIndent"), false).toBool());
    m_timeline->setOverwriteDrop(s.value(QStringLiteral("prefs/overwriteDrop"), true).toBool());
    m_actions->setChecked(QStringLiteral("OverwriteDrop"), m_timeline->overwriteDrop());
    m_document->setRippleMarkers(s.value(QStringLiteral("prefs/rippleMarkers"), false).toBool());
    m_actions->setChecked(QStringLiteral("RippleSceneMarkers"), m_document->rippleMarkers());
    m_canvas->setBrushOutline(s.value(QStringLiteral("prefs/brushOutline"), false).toBool());
    m_canvas->setOnionBrightnessAlpha(s.value(QStringLiteral("prefs/onionBrightnessAlpha"), false).toBool());
    m_canvas->setOnionOver(s.value(QStringLiteral("prefs/onionOver"), false).toBool());
    m_canvas->setOnionEnhanced(s.value(QStringLiteral("prefs/onionEnhanced"), false).toBool());
    m_canvas->setIkIterations(s.value(QStringLiteral("prefs/ikIterations"), 24).toInt());
    m_document->setSubstitutionCycles(s.value(QStringLiteral("prefs/drawingCycle"), true).toBool());
    m_document->setSubstitutionKeys(s.value(QStringLiteral("prefs/substitutionKeys"), true).toBool());
    m_canvas->setFieldChart(s.value(QStringLiteral("prefs/fieldsAcross"), 12).toInt(), s.value(QStringLiteral("prefs/fieldsDown"), 12).toInt());
    Canvas::setBlursEnabled(s.value(QStringLiteral("prefs/renderBlurs"), true).toBool());
    Canvas::setFocusEnabled(s.value(QStringLiteral("prefs/renderFocus"), true).toBool());
    m_canvas->update();
    m_canvas->setIkError(s.value(QStringLiteral("prefs/ikError"), 0.2).toDouble());
    m_focusOnEnter = s.value(QStringLiteral("prefs/focusOnEnter"), false).toBool();
    m_autoSaveWorkspace = s.value(QStringLiteral("prefs/autoSaveWorkspace"), true).toBool();
    setCurrentViewBorder(s.value(QStringLiteral("prefs/viewBorder"), true).toBool());
    m_canvas->setMiddleButtonPans(s.value(QStringLiteral("prefs/middlePans"), true).toBool());
    m_canvas->setPerformanceReport(s.value(QStringLiteral("prefs/performanceReport"), false).toBool());
    m_canvas->setPasteBehaviour(s.value(QStringLiteral("prefs/pasteBehaviour"), 0).toInt());
    setCurrentViewBorderColour(QColor(s.value(QStringLiteral("prefs/viewBorderColour"), QStringLiteral("#c8281e")).toString()));
    m_canvas->setBoneColours(QColor(s.value(QStringLiteral("prefs/boneColour"), QStringLiteral("#5adc8c")).toString()),
                             QColor(s.value(QStringLiteral("prefs/boneSelectedColour"), QStringLiteral("#ffffff")).toString()));
    m_restoreDrawingOnUndo = s.value(QStringLiteral("prefs/restoreDrawingOnUndo"), false).toBool();
    Scene::setCompressFiles(s.value(QStringLiteral("prefs/compressScenes"), false).toBool());
    m_autoSaveQuiet = std::clamp(s.value(QStringLiteral("prefs/autosaveQuietSeconds"), 1).toInt(), 0, 600);
    {
        int const thumbnails = std::clamp(s.value(QStringLiteral("prefs/thumbnailSize"), 100).toInt(), 50, 200);
        if (m_library)
            m_library->setThumbnailScale(thumbnails);
        if (m_substitution)
            m_substitution->setThumbnailScale(thumbnails);
    }
    if (FunctionView* const function = findChild<FunctionView*>()) {
        function->graph()->setGridShown(s.value(QStringLiteral("prefs/functionGrid"), true).toBool());
        function->graph()->setLoadLimit(s.value(QStringLiteral("prefs/functionLoadLimit"), 50).toInt());
    }
    m_editorsOnTop = s.value(QStringLiteral("prefs/editorsOnTop"), true).toBool();
    FunctionColumn::defaultBezier = s.value(QStringLiteral("prefs/defaultBezier"), true).toBool();
    FunctionColumn::defaultBezierVelocity = s.value(QStringLiteral("prefs/defaultBezierVelocity"), true).toBool();
    FunctionColumn::defaultConstantZ = s.value(QStringLiteral("prefs/pathConstantZ"), false).toBool();
    if (Xsheet* const xsheet = findChild<Xsheet*>())
        xsheet->setColumnWidth(s.value(QStringLiteral("prefs/xsheetColumnWidth"), 78).toInt());
    if (Xsheet* const xsheet = findChild<Xsheet*>()) {
        xsheet->setCentreOnFrame(s.value(QStringLiteral("prefs/xsheetCentre"), false).toBool());
        xsheet->setDefaultHold(s.value(QStringLiteral("prefs/xsheetHold"), 1).toInt());
    }
    applySheetPreferences(s, !m_preferencesApplied);
    applyAnimationPreferences(s, !m_preferencesApplied);
    // (These two set how things start: the toggles in the menus change them afterwards.)
    if (!m_preferencesApplied) {
        bool const loop = s.value(QStringLiteral("prefs/loop"), true).toBool();
        m_actions->setChecked(QStringLiteral("PlayLoop"), loop);
        onActionToggleLoop(loop);
        // The Select tool's lasso, and the gaps the paint bucket closes, as Eclipse starts (their tool properties change them after).
        m_canvas->setLasso(s.value(QStringLiteral("prefs/selectLasso"), true).toBool());
        if (s.value(QStringLiteral("prefs/showStrokes"), false).toBool()) {
            m_actions->setChecked(QStringLiteral("ShowStrokes"), true);
            m_canvas->onActionShowStrokes(true);
        }
        m_canvas->setCloseGapSize(std::clamp(s.value(QStringLiteral("prefs/closeGap"), 2.0).toDouble(), 0.0, 50.0));
        bool const onTop = s.value(QStringLiteral("prefs/drawingOnTop"), false).toBool();
        m_actions->setChecked(QStringLiteral("CurrentDrawingOnTop"), onTop);
        m_canvas->onActionCurrentDrawingOnTop(onTop);
    }
    m_preferencesApplied = true;
    // Onion skins that fade with distance: the nearest drawings so strong, the furthest so faint, those between in step.
    if (s.value(QStringLiteral("prefs/onionFade"), false).toBool()) {
        int const nearest = std::clamp(s.value(QStringLiteral("prefs/onionNearest"), 100).toInt(), 0, 100);
        int const furthest = std::clamp(s.value(QStringLiteral("prefs/onionFurthest"), 30).toInt(), 0, 100);
        for (bool after : {false, true})
            for (int step = 0; step < Canvas::kOnionSteps; ++step)
                m_canvas->setOnionStrength(after, step, nearest + (furthest - nearest) * step / std::max(1, Canvas::kOnionSteps - 1));
    }
    if (NodeView* const view = findChild<NodeView*>())
        view->setDoubleClickOpensEditor(s.value(QStringLiteral("prefs/nodeDoubleClick"), 0).toInt() == 1);
}

void MainWindow::placeCameraView()
{
    for (DepthView* view : findChildren<DepthView*>())
        view->resetView();
    for (PerspectiveView* view : findChildren<PerspectiveView*>()) {
        view->resetView();
        view->setClippingPlanes(1.0, 100000.0);
    }
    // As the preferences say: the whole frame in the view, or at its own size.
    m_canvas->fitToWindow();
    if (preferences().value(QStringLiteral("prefs/cameraZoom"), 0).toInt() == 1)
        m_canvas->onActionResetZoom();
}

void MainWindow::onActionPreferences()
{
    QSettings s = preferences();
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("PreferencesDialog"));
    dialog.setWindowTitle(tr("Preferences"));

    PreferenceWidgets w;
    preferencesGeneral(w, s, dialog);
    preferencesCamera(w, s, dialog);
    preferencesSheet(w, s, dialog);
    preferencesAnimation(w, s, dialog);
    preferencesAdvanced(w, s, dialog);
    preferencesTimeline(w, s, dialog);
    preferencesRender(w, s, dialog);
    preferencesTools(w, s, dialog);
    preferencesNodeView(w, s, dialog);
    preferencesOnion(w, s, dialog);
    preferencesSound(w, s, dialog);
    preferencesDrawing(w, s, dialog);
    preferencesColours(w, s, dialog);
    preferencesPalettes(w, s, dialog);

    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    // Preference sets: every preference saved to a file and loaded back; or all of them as they were to begin with.
    QPushButton* const saveSet = buttons->addButton(tr("Save..."), QDialogButtonBox::ActionRole);
    saveSet->setObjectName(QStringLiteral("PrefSaveSet"));
    saveSet->setToolTip(tr("Save the current preferences to a file."));
    QPushButton* const loadSet = buttons->addButton(tr("Load..."), QDialogButtonBox::ActionRole);
    loadSet->setObjectName(QStringLiteral("PrefLoadSet"));
    loadSet->setToolTip(tr("Load preferences from a file."));
    QPushButton* const defaults = buttons->addButton(tr("Restore All Defaults"), QDialogButtonBox::ResetRole);
    defaults->setObjectName(QStringLiteral("PrefRestoreDefaults"));
    bool reopen = false;
    connect(saveSet, &QPushButton::clicked, &dialog, [this, &dialog] {
        QString const path = QFileDialog::getSaveFileName(&dialog, tr("Save Preferences"), QStringLiteral("preferences.eclprefs"), tr("Preferences (*.eclprefs)"));
        if (!path.isEmpty() && !savePreferenceSet(path))
            QMessageBox::warning(&dialog, tr("Save Preferences"), tr("Error while writing preference file: %1").arg(path));
    });
    connect(loadSet, &QPushButton::clicked, &dialog, [this, &dialog, &reopen] {
        QString const path = QFileDialog::getOpenFileName(&dialog, tr("Load Preferences"), QString(), tr("Preferences (*.eclprefs)"));
        if (path.isEmpty())
            return;
        if (!loadPreferenceSet(path)) {
            QMessageBox::warning(&dialog, tr("Load Preferences"), tr("Unable to open preference file: %1").arg(path));
            return;
        }
        reopen = true;
        dialog.reject();
    });
    connect(defaults, &QPushButton::clicked, &dialog, [this, &dialog, &reopen] {
        if (QMessageBox::question(&dialog, tr("Restore default preferences"), tr("Restore all default preferences?")) != QMessageBox::Yes)
            return;
        restoreDefaultPreferences();
        reopen = true;
        dialog.reject();
    });
    // A tab for each kind of setting.
    QTabWidget* const tabs = new QTabWidget(&dialog);
    tabs->setObjectName(QStringLiteral("PreferencesTabs"));
    for (QGroupBox* const page : {w.general, w.camera, w.animationPage, w.sheet, w.timelinePage, w.drawing, w.toolsPage, w.onionPage, w.nodePage, w.colours, w.palettes, w.renderPage, w.soundPage, w.advanced}) {
        QString const title = page->title();
        page->setTitle(QString());
        page->setFlat(true);
        tabs->addTab(page, title);
    }
    tabs->setCurrentIndex(std::clamp(s.value(QStringLiteral("prefs/lastTab"), 0).toInt(), 0, tabs->count() - 1));
    QVBoxLayout* const layout = new QVBoxLayout(&dialog);
    layout->addWidget(tabs);
    layout->addWidget(buttons);
    int const result = dialog.exec();
    s.setValue(QStringLiteral("prefs/lastTab"), tabs->currentIndex());
    if (reopen) {
        // (Preferences loaded or put back: in effect now, and shown again.)
        applyPreferences();
        QTimer::singleShot(0, this, &MainWindow::onActionPreferences);
        return;
    }
    if (result != QDialog::Accepted)
        return;
    s.setValue(QStringLiteral("prefs/lightTableWash"), w.wash->value());
    s.setValue(QStringLiteral("prefs/lightTableWhite"), w.lightWhite->value());
    s.setValue(QStringLiteral("prefs/lightTableShade"), w.lightShade->isChecked());
    s.setValue(QStringLiteral("prefs/lightTableShadeCamera"), w.lightShadeCamera->isChecked());
    s.setValue(QStringLiteral("prefs/gridOnDefault"), w.gridOnDefault->isChecked());
    s.setValue(QStringLiteral("prefs/onionWashMin"), w.onionWashMin->value());
    s.setValue(QStringLiteral("prefs/onionWashMax"), w.onionWashMax->value());
    s.setValue(QStringLiteral("prefs/frameWidth"), w.frameWidth->value());
    s.setValue(QStringLiteral("prefs/previewSize"), w.previewSize->currentData().toInt());
    s.setValue(QStringLiteral("prefs/autoRender"), w.autoRender->isChecked());
    if (w.toolSize->value() != int(m_canvas->brushWidth()) || s.contains(QStringLiteral("prefs/toolSize")))
        s.setValue(QStringLiteral("prefs/toolSize"), w.toolSize->value());
    s.setValue(QStringLiteral("prefs/soundOn"), w.soundOn->isChecked());
    s.setValue(QStringLiteral("prefs/units"), w.units->currentIndex());
    s.setValue(QStringLiteral("prefs/smoothing"), w.smoothing->value());
    s.setValue(QStringLiteral("prefs/corners"), w.corners->value());
    s.setValue(QStringLiteral("prefs/nodeNavigator"), w.navigator->isChecked());
    s.setValue(QStringLiteral("prefs/nodeStraight"), w.straight->isChecked());
    s.setValue(QStringLiteral("prefs/onionPrevious"), w.onionCountBefore->value());
    s.setValue(QStringLiteral("prefs/onionNext"), w.onionCountAfter->value());
    s.setValue(QStringLiteral("prefs/onionOpacity"), w.onionStrength->value());
    s.setValue(QStringLiteral("prefs/scrub"), w.scrub->isChecked());
    s.setValue(QStringLiteral("prefs/backdrop"), w.backdrop->property("colour").value<QColor>().name());
    m_canvas->setOnionColours(w.onionBefore->property("colour").value<QColor>(), w.onionAfter->property("colour").value<QColor>());
    s.setValue(QStringLiteral("welcome/hide"), !w.welcome->isChecked());
    s.setValue(QStringLiteral("prefs/autosaveMinutes"), w.autoSave->value());
    s.setValue(QStringLiteral("prefs/lasso"), w.selectMode->currentIndex() == 0);
    s.setValue(QStringLiteral("prefs/overlayUnderlay"), w.moreArt->isChecked());
    s.setValue(QStringLiteral("prefs/nudge"), w.nudge->value());
    s.setValue(QStringLiteral("prefs/undoLevels"), w.undoLevels->value());
    s.setValue(QStringLiteral("prefs/keyFrameOne"), w.frameOne->isChecked());
    s.setValue(QStringLiteral("prefs/outlineLocked"), w.outlineLocked->isChecked());
    s.setValue(QStringLiteral("prefs/bboxSelection"), w.bboxSelection->isChecked());
    s.setValue(QStringLiteral("prefs/extendExposure"), w.extend->isChecked());
    s.setValue(QStringLiteral("prefs/stopMotionKeys"), w.stopMotion->isChecked());
    s.setValue(QStringLiteral("prefs/loopPause"), w.loopPause->value());
    s.setValue(QStringLiteral("prefs/zoomOnMouse"), w.zoomOnMouse->isChecked());
    s.setValue(QStringLiteral("prefs/defaultZoom"), w.defaultZoom->value());
    s.setValue(QStringLiteral("prefs/planningDefaultZoom"), w.planningDefaultZoom->value());
    s.setValue(QStringLiteral("prefs/planningThumbnailSize"), w.planningThumbnailSize->value());
    s.setValue(QStringLiteral("prefs/zoomMin"), w.zoomMin->value());
    s.setValue(QStringLiteral("prefs/zoomMax"), w.zoomMax->value());
    s.setValue(QStringLiteral("prefs/quickCloseUp"), w.quickCloseUp->value());
    s.setValue(QStringLiteral("prefs/paperZoom"), w.paperZoom->isChecked());
    s.setValue(QStringLiteral("prefs/screenPpi"), w.screenPpi->value());
    s.setValue(QStringLiteral("prefs/pressureFeel"), w.pressureFeel->value());
    s.setValue(QStringLiteral("prefs/nameByFrame"), w.byFrame->isChecked());
    s.setValue(QStringLiteral("prefs/proportionAcross"), w.gridAcross->value());
    s.setValue(QStringLiteral("prefs/proportionDown"), w.gridDown->value());
    s.setValue(QStringLiteral("prefs/safeMargin"), w.safe->value());
    s.setValue(QStringLiteral("prefs/keepBackup"), w.keepBackup->isChecked());
    s.setValue(QStringLiteral("prefs/askAutoSave"), w.askAutoSave->isChecked());
    s.setValue(QStringLiteral("prefs/cameraZoom"), w.cameraZoom->currentIndex());
    s.setValue(QStringLiteral("prefs/timelineSound"), w.showSound->isChecked());
    s.setValue(QStringLiteral("prefs/timelineWaveforms"), w.waveforms->isChecked());
    s.setValue(QStringLiteral("prefs/timelineGroups"), w.showGroups->isChecked());
    s.setValue(QStringLiteral("prefs/timelineSceneEnd"), w.sceneEnd->isChecked());
    s.setValue(QStringLiteral("prefs/timelineReduceIndent"), w.reduceIndent->isChecked());
    s.setValue(QStringLiteral("prefs/preroll"), w.preroll->value());
    s.setValue(QStringLiteral("prefs/nodeDoubleClick"), w.nodeDoubleClick->currentIndex());
    s.setValue(QStringLiteral("prefs/brushOutline"), w.brushOutline->isChecked());
    s.setValue(QStringLiteral("prefs/loop"), w.loopAtStart->isChecked());
    s.setValue(QStringLiteral("prefs/lipSyncLead"), w.lipLead->value());
    s.setValue(QStringLiteral("prefs/onionBrightnessAlpha"), w.onionBrightness->isChecked());
    s.setValue(QStringLiteral("prefs/lineArtBitmap"), w.lineArtBitmap->isChecked());
    s.setValue(QStringLiteral("prefs/thumbnailSize"), w.thumbnails->value());
    s.setValue(QStringLiteral("prefs/pencilLinesOpacity"), w.lineOpacity->value());
    s.setValue(QStringLiteral("prefs/bitmapCanvas"), w.bitmapCanvas->value());
    s.setValue(QStringLiteral("prefs/bitmapCanvasHeight"), w.bitmapCanvasHeight->value());
    s.setValue(QStringLiteral("prefs/bitmapPpi"), w.bitmapPpi->value());
    s.setValue(QStringLiteral("prefs/bitmapCameraDensity"), w.bitmapCameraDensity->isChecked());
    s.setValue(QStringLiteral("prefs/bitmapCanvasBasis"), w.canvasBasis->currentIndex());
    s.setValue(QStringLiteral("prefs/advancedElementMode"), w.elementMode->isChecked());
    s.setValue(QStringLiteral("prefs/initialTool"), w.initialTool->currentData().toString());
    s.setValue(QStringLiteral("prefs/scalePencilThickness"), w.scalePencil->isChecked());
    s.setValue(QStringLiteral("prefs/onionOver"), w.onionOver->isChecked());
    s.setValue(QStringLiteral("prefs/onionOriginal"), w.onionStyle->currentIndex() == 1);
    m_canvas->setOnionOriginal(w.onionStyle->currentIndex() == 1);
    m_canvas->setOnionOutlines(w.onionStyle->currentIndex() == 2);
    m_canvas->setOnionOthers(w.onionOthers->isChecked());
    s.setValue(QStringLiteral("prefs/onionEnhanced"), w.onionEnhanced->isChecked());
    s.setValue(QStringLiteral("prefs/selectLasso"), w.selectLasso->isChecked());
    s.setValue(QStringLiteral("prefs/showStrokes"), w.showStrokes->isChecked());
    s.setValue(QStringLiteral("prefs/drawingCycle"), w.drawingCycle->isChecked());
    s.setValue(QStringLiteral("prefs/substitutionKeys"), w.substitutionKeys->isChecked());
    s.setValue(QStringLiteral("prefs/fieldsAcross"), w.fieldsAcross->value());
    s.setValue(QStringLiteral("prefs/fieldsDown"), w.fieldsDown->value());
    s.setValue(QStringLiteral("prefs/closeGap"), w.closeGap->value());
    s.setValue(QStringLiteral("prefs/ikIterations"), w.ikIterations->value());
    s.setValue(QStringLiteral("prefs/ikError"), w.ikError->value());
    s.setValue(QStringLiteral("prefs/renderBlurs"), w.renderBlurs->isChecked());
    s.setValue(QStringLiteral("prefs/renderFocus"), w.renderFocus->isChecked());
    s.setValue(QStringLiteral("prefs/xsheetCentre"), w.xsheetCentre->isChecked());
    s.setValue(QStringLiteral("prefs/xsheetHold"), w.xsheetHold->value());
    s.setValue(QStringLiteral("prefs/focusOnEnter"), w.focusOnEnter->isChecked());
    s.setValue(QStringLiteral("prefs/autoSaveWorkspace"), w.autoWorkspace->isChecked());
    s.setValue(QStringLiteral("prefs/viewBorder"), w.viewBorder->isChecked());
    s.setValue(QStringLiteral("prefs/restoreDrawingOnUndo"), w.restoreDrawing->isChecked());
    s.setValue(QStringLiteral("prefs/viewBorderColour"), w.borderColour->property("colour").value<QColor>().name());
    s.setValue(QStringLiteral("prefs/boneColour"), w.boneColour->property("colour").value<QColor>().name());
    s.setValue(QStringLiteral("prefs/boneSelectedColour"), w.boneSelected->property("colour").value<QColor>().name());
    for (int g = 0; g < w.viewColours->topLevelItemCount(); ++g)
        for (int i = 0; i < w.viewColours->topLevelItem(g)->childCount(); ++i) {
            QTreeWidgetItem* const item = w.viewColours->topLevelItem(g)->child(i);
            QColor const colour = item->data(1, Qt::UserRole).value<QColor>();
            InterfaceColours::set(item->data(0, Qt::UserRole).toString(), colour == item->data(0, Qt::UserRole + 1).value<QColor>() ? QColor() : colour);
        }
    s.setValue(QStringLiteral("prefs/darkStyle"), w.darkStyle->isChecked());
    s.setValue(QStringLiteral("prefs/interfaceScale"), w.uiScale->value());
    s.setValue(QStringLiteral("prefs/language"), w.language->currentData().toString());
    s.setValue(QStringLiteral("prefs/compressScenes"), w.compress->isChecked());
    s.setValue(QStringLiteral("prefs/autosaveQuietSeconds"), w.quiet->value());
    s.setValue(QStringLiteral("prefs/pasteBehaviour"), w.pasteBehaviour->currentIndex());
    s.setValue(QStringLiteral("prefs/middlePans"), w.middlePans->isChecked());
    s.setValue(QStringLiteral("prefs/performanceReport"), w.performance->isChecked());
    s.setValue(QStringLiteral("prefs/modelScale"), w.modelScale->value());
    s.setValue(QStringLiteral("prefs/functionGrid"), w.functionGrid->isChecked());
    s.setValue(QStringLiteral("prefs/functionLoadLimit"), w.functionLoadLimit->value());
    s.setValue(QStringLiteral("prefs/pathConstantZ"), w.pathConstantZ->isChecked());
    s.setValue(QStringLiteral("prefs/editorsOnTop"), w.editorsOnTop->isChecked());
    s.setValue(QStringLiteral("prefs/defaultBezier"), w.defaultBezier->isChecked());
    s.setValue(QStringLiteral("prefs/defaultBezierVelocity"), w.defaultBezierVelocity->isChecked());
    s.setValue(QStringLiteral("prefs/xsheetColumnWidth"), w.columnWidth->value());
    s.setValue(QStringLiteral("prefs/drawingOnTop"), w.onTop->isChecked());
    s.setValue(QStringLiteral("prefs/onionFade"), w.onionFade->isChecked());
    s.setValue(QStringLiteral("prefs/onionNearest"), w.onionNearest->value());
    s.setValue(QStringLiteral("prefs/onionFurthest"), w.onionFurthest->value());
    savePalettePreferences(w, s);
    saveSheetPreferences(w, s);
    saveAnimationPreferences(w, s);
    s.sync();
    applyPreferences();
}

bool MainWindow::savePreferenceSet(QString const& path) const
{
    // Every preference, as an ini file.
    QFile::remove(path);
    QSettings out(path, QSettings::IniFormat);
    QSettings const in(QStringLiteral("Eclipse"), QStringLiteral("Eclipse"));
    for (QString const& key : in.allKeys())
        if (key.startsWith(QStringLiteral("prefs/")) && key != QStringLiteral("prefs/lastTab"))
            out.setValue(key, in.value(key));
    out.sync();
    return out.status() == QSettings::NoError && QFileInfo::exists(path);
}

bool MainWindow::loadPreferenceSet(QString const& path)
{
    if (!QFileInfo::exists(path))
        return false;
    QSettings const in(path, QSettings::IniFormat);
    QStringList const keys = in.allKeys();
    bool any = false;
    for (QString const& key : keys)
        any = any || key.startsWith(QStringLiteral("prefs/"));
    if (in.status() != QSettings::NoError || !any)
        return false;
    QSettings out(QStringLiteral("Eclipse"), QStringLiteral("Eclipse"));
    for (QString const& key : keys)
        if (key.startsWith(QStringLiteral("prefs/")))
            out.setValue(key, in.value(key));
    return true;
}

QStringList MainWindow::languages()
{
    QStringList codes = {QStringLiteral("en")};
    QDir const dir(ActionManager::defaultDirectory() + QStringLiteral("/translations"));
    for (QString const& file : dir.entryList({QStringLiteral("eclipse_*.qm")}, QDir::Files, QDir::Name))
        codes.append(file.mid(8, file.size() - 11));
    return codes;
}

bool MainWindow::installLanguage(QString const& code)
{
    if (code.isEmpty() || code == QStringLiteral("en"))
        return true;
    QTranslator* const own = new QTranslator(qApp);
    if (!own->load(QStringLiteral("eclipse_") + code, ActionManager::defaultDirectory() + QStringLiteral("/translations"))) {
        delete own;
        return false;
    }
    QCoreApplication::installTranslator(own);
    // Qt's own words (the buttons of its dialogs), when Qt has them in that language.
    QTranslator* const qt = new QTranslator(qApp);
    if (qt->load(QStringLiteral("qtbase_") + code, QLibraryInfo::path(QLibraryInfo::TranslationsPath))
        || qt->load(QStringLiteral("qtbase_") + code, QCoreApplication::applicationDirPath() + QStringLiteral("/translations")))
        QCoreApplication::installTranslator(qt);
    else
        delete qt;
    return true;
}

void MainWindow::restoreDefaultPreferences()
{
    QSettings s(QStringLiteral("Eclipse"), QStringLiteral("Eclipse"));
    int const tab = s.value(QStringLiteral("prefs/lastTab"), 0).toInt();
    s.remove(QStringLiteral("prefs"));
    s.setValue(QStringLiteral("prefs/lastTab"), tab);
}

void MainWindow::showStyleEditor()
{
    // Additions to the look, in Qt's style sheet language: shown as they are typed when Apply is pressed, kept by Save.
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("StyleEditor"));
    dialog.setWindowTitle(tr("Style Editor"));
    QPlainTextEdit* const text = new QPlainTextEdit(&dialog);
    text->setObjectName(QStringLiteral("StyleEditorText"));
    QSettings s(QStringLiteral("Eclipse"), QStringLiteral("Eclipse"));
    QString const kept = s.value(QStringLiteral("prefs/styleExtra")).toString();
    text->setPlainText(kept);
    text->setPlaceholderText(tr("For example:\nQMenu { font-size: 11pt; }"));
    QString const base = qApp->property("eclipseBaseStyle").toString();
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Apply | QDialogButtonBox::Close, &dialog);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, &dialog, [text, base] { qApp->setStyleSheet(base + QLatin1Char('\n') + text->toPlainText()); });
    connect(buttons->button(QDialogButtonBox::Save), &QPushButton::clicked, &dialog, [text, base, &dialog] {
        QSettings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse")).setValue(QStringLiteral("prefs/styleExtra"), text->toPlainText());
        qApp->setStyleSheet(base + QLatin1Char('\n') + text->toPlainText());
        dialog.accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, [kept, base, &dialog] {
        // (Closed without saving: as it was.)
        qApp->setStyleSheet(base + QLatin1Char('\n') + kept);
        dialog.reject();
    });
    QVBoxLayout* const layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(tr("Style Sheet: additions to Eclipse's look"), &dialog));
    layout->addWidget(text, 1);
    layout->addWidget(buttons);
    dialog.resize(520, 380);
    dialog.exec();
}
