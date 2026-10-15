// Eclipse - Scene > Check Files, and Edit > Preferences.
#include "ui/window/MainWindow.h"

#include <functional>

#include <algorithm>
#include <cmath>

#include <QCheckBox>
#include <QColorDialog>
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

#include "audio/sound/Sound.h"
#include "model/document/Commands.h"
#include "ui/canvas/Canvas.h"
#include "ui/timeline/Xsheet.h"
#include "ui/timeline/FunctionView.h"
#include "ui/views/PreviewManagerView.h"
#include "ui/views/CoordView.h"
#include "actions/manager/ActionManager.h"
#include <QAction>
#include <QToolButton>
#include "ui/panels/LibraryView.h"
#include "ui/io/MovieExport.h"
#include "ui/panels/PalettePanel.h"
#include "ui/io/ImportExportPrivate.h"
#include "ui/views/NodeView.h"

#include <tuple>

QStringList MainWindow::sceneFiles(bool missingOnly) const
{
    QStringList files;
    auto add = [&](QString const& file) {
        if (!file.isEmpty() && !files.contains(file) && !(missingOnly && QFileInfo::exists(file)))
            files << file;
    };
    for (Layer const& layer : m_document->scene().layers) {
        if (layer.kind == Layer::Sound)
            add(layer.soundFile);
        for (Drawing const& drawing : layer.drawings)
            for (Picture const& picture : drawing.pictures)
                if (picture.linked)
                    add(picture.file);
    }
    return files;
}

void MainWindow::onActionCheckFiles()
{
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("CheckFilesDialog"));
    dialog.setWindowTitle(tr("Check Files"));
    QListWidget* const list = new QListWidget(&dialog);
    list->setObjectName(QStringLiteral("CheckFilesList"));
    list->setMinimumSize(520, 240);
    QLabel* const summary = new QLabel(&dialog);
    auto fill = [this, list, summary] {
        QStringList const all = sceneFiles(false), missing = sceneFiles(true);
        list->clear();
        for (QString const& file : all) {
            bool const gone = missing.contains(file);
            QListWidgetItem* const item = new QListWidgetItem((gone ? tr("MISSING   ") : tr("found        ")) + QDir::toNativeSeparators(file), list);
            item->setData(Qt::UserRole, file);
            if (gone)
                item->setForeground(QColor(240, 110, 90));
        }
        summary->setText(all.isEmpty() ? tr("The scene uses no files outside itself: everything is kept in the scene file.")
                         : missing.isEmpty() ? tr("All %1 files the scene uses are where they should be.").arg(all.size())
                                             : tr("%1 of the %2 files the scene uses cannot be found.").arg(missing.size()).arg(all.size()));
    };
    fill();
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    // A file that has been moved: say where it is now.
    QPushButton* const locate = buttons->addButton(tr("Locate..."), QDialogButtonBox::ActionRole);
    locate->setObjectName(QStringLiteral("CheckFilesLocate"));
    locate->setToolTip(tr("Point the file chosen in the list at where it is now."));
    locate->setEnabled(false);
    connect(list, &QListWidget::currentRowChanged, locate, [locate](int row) { locate->setEnabled(row >= 0); });
    connect(locate, &QPushButton::clicked, &dialog, [this, list, fill, &dialog] {
        if (!list->currentItem())
            return;
        QString const was = list->currentItem()->data(Qt::UserRole).toString();
        QString const now = QFileDialog::getOpenFileName(&dialog, tr("Locate %1").arg(QFileInfo(was).fileName()), QFileInfo(was).absolutePath());
        if (!now.isEmpty() && m_document->relinkFile(was, now))
            fill();
    });
    QVBoxLayout* const layout = new QVBoxLayout(&dialog);
    layout->addWidget(summary);
    layout->addWidget(list);
    layout->addWidget(buttons);
    dialog.exec();
}

// Scene > Element Manager: the layers that hold drawings - the elements of the scene - with how many drawings
// each has; one can be added, given another name, or deleted with all its drawings.
void MainWindow::onActionElementManager()
{
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("ElementManagerDialog"));
    dialog.setWindowTitle(tr("Element Manager"));
    QListWidget* const list = new QListWidget(&dialog);
    list->setObjectName(QStringLiteral("ElementList"));
    list->setMinimumSize(420, 260);
    QLineEdit* const name = new QLineEdit(&dialog);
    name->setObjectName(QStringLiteral("ElementName"));
    name->setPlaceholderText(tr("Name of the element"));
    QPushButton* const add = new QPushButton(tr("Add"), &dialog);
    add->setObjectName(QStringLiteral("ElementAdd"));
    add->setToolTip(tr("Adds a drawing layer with that name."));
    QPushButton* const rename = new QPushButton(tr("Rename"), &dialog);
    rename->setObjectName(QStringLiteral("ElementRename"));
    rename->setToolTip(tr("Gives the element chosen in the list that name."));
    QPushButton* const remove = new QPushButton(tr("Delete"), &dialog);
    remove->setObjectName(QStringLiteral("ElementDelete"));
    remove->setToolTip(tr("Deletes the element chosen in the list, with all its drawings."));
    auto layerOf = [this](QListWidgetItem const* item) {
        Scene const& scene = m_document->scene();
        for (int l = 0; item && l < scene.layers.size(); ++l)
            if (scene.layers[l].id == item->data(Qt::UserRole).toInt())
                return l;
        return -1;
    };
    auto fill = [this, list](int chosenId) {
        Scene const& scene = m_document->scene();
        list->clear();
        // From the front of the picture to the back, as the Timeline has them.
        for (int l = int(scene.layers.size()) - 1; l >= 0; --l) {
            Layer const& layer = scene.layers[l];
            if (!layer.holdsDrawings())
                continue;
            int shown = 0;
            for (int cell : layer.cells)
                shown += cell != Layer::kNoDrawing ? 1 : 0;
            QListWidgetItem* const item = new QListWidgetItem(tr("%1   -   %n drawing(s)", nullptr, int(layer.drawings.size())).arg(layer.name)
                                                              + tr(", shown on %n frame(s)", nullptr, shown), list);
            item->setData(Qt::UserRole, layer.id);
            if (layer.id == chosenId)
                list->setCurrentItem(item);
        }
    };
    fill(-1);
    auto enable = [=] {
        bool const one = list->currentItem() != nullptr, named = !name->text().trimmed().isEmpty();
        add->setEnabled(named);
        rename->setEnabled(one && named);
        remove->setEnabled(one);
    };
    enable();
    connect(name, &QLineEdit::textChanged, &dialog, enable);
    connect(list, &QListWidget::currentItemChanged, &dialog, [=](QListWidgetItem* item) {
        if (item) {
            int const layer = layerOf(item);
            if (layer >= 0)
                name->setText(m_document->scene().layers[layer].name);
        }
        enable();
    });
    connect(add, &QPushButton::clicked, &dialog, [=] {
        m_document->beginMacro(tr("Add Element"));
        int const made = m_document->addLayer(int(m_document->scene().layers.size()) - 1);
        if (made >= 0)
            m_document->renameLayer(made, name->text().trimmed());
        m_document->endMacro();
        fill(made >= 0 ? m_document->scene().layers[made].id : -1);
        enable();
    });
    connect(rename, &QPushButton::clicked, &dialog, [=] {
        int const layer = layerOf(list->currentItem());
        if (layer < 0)
            return;
        int const id = m_document->scene().layers[layer].id;
        m_document->renameLayer(layer, name->text().trimmed());
        fill(id);
        enable();
    });
    connect(remove, &QPushButton::clicked, &dialog, [=] {
        int const layer = layerOf(list->currentItem());
        if (layer < 0)
            return;
        m_document->removeLayers({layer});
        fill(-1);
        enable();
    });
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QHBoxLayout* const row = new QHBoxLayout;
    row->addWidget(name, 1);
    row->addWidget(add);
    row->addWidget(rename);
    row->addWidget(remove);
    QVBoxLayout* const layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(tr("The elements of the scene: the layers that hold drawings."), &dialog));
    layout->addWidget(list);
    layout->addLayout(row);
    layout->addWidget(buttons);
    dialog.exec();
}

static QSettings preferences()
{
    return QSettings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse"));
}

void MainWindow::applyPreferences()
{
    QSettings const s = preferences();
    m_canvas->setLasso(s.value(QStringLiteral("prefs/lasso"), true).toBool());
    // A drawing has four sheets; two of them - overlay and underlay - are out of the way
    // unless they are asked for.
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
    m_autoSaveMinutes = std::clamp(s.value(QStringLiteral("prefs/autosaveMinutes"), 0).toInt(), 0, 240);
    // Timeline: how wide a frame is to begin with.  Render: the preview's size, and whether it keeps itself up to date.
    m_timeline->setCellWidth(std::clamp(s.value(QStringLiteral("prefs/frameWidth"), 8).toInt(), 4, 30));
    m_timeline->setRulerLocked(s.value(QStringLiteral("prefs/lockTimelineRuler"), false).toBool());
    m_loopPause = std::clamp(s.value(QStringLiteral("prefs/loopPause"), 0).toInt(), 0, 5000);
    for (Canvas* const view : findChildren<Canvas*>())
        view->setZoomOnMouse(s.value(QStringLiteral("prefs/zoomOnMouse"), false).toBool());
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
    m_canvas->setBrushOutline(s.value(QStringLiteral("prefs/brushOutline"), false).toBool());
    if (FunctionView* const function = findChild<FunctionView*>())
        function->graph()->setGridShown(s.value(QStringLiteral("prefs/functionGrid"), true).toBool());
    if (Xsheet* const xsheet = findChild<Xsheet*>())
        xsheet->setColumnWidth(s.value(QStringLiteral("prefs/xsheetColumnWidth"), 78).toInt());
    // (These two set how things start: the toggles in the menus change them afterwards.)
    if (!m_preferencesApplied) {
        bool const loop = s.value(QStringLiteral("prefs/loop"), true).toBool();
        m_actions->setChecked(QStringLiteral("PlayLoop"), loop);
        onActionToggleLoop(loop);
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

    QGroupBox* const general = new QGroupBox(tr("General"), &dialog);
    QCheckBox* const welcome = new QCheckBox(tr("Show the welcome screen when Eclipse starts"), general);
    welcome->setChecked(!s.value(QStringLiteral("welcome/hide"), false).toBool());
    QSpinBox* const autoSave = new QSpinBox(general);
    autoSave->setRange(0, 240);
    autoSave->setSpecialValueText(tr("Never"));
    autoSave->setSuffix(tr(" minutes"));
    autoSave->setValue(m_autoSaveMinutes);
    QFormLayout* const generalLayout = new QFormLayout(general);
    generalLayout->addRow(welcome);
    generalLayout->addRow(tr("Save the scene by itself every"), autoSave);
    generalLayout->addRow(new QLabel(tr("(Only a scene that has been saved once, and only when it has changed.)"), general));
    QSpinBox* const undoLevels = new QSpinBox(general);
    undoLevels->setObjectName(QStringLiteral("PrefUndoLevels"));
    undoLevels->setRange(0, 10000);
    undoLevels->setSpecialValueText(tr("No limit"));
    undoLevels->setToolTip(tr("How many steps can be undone. It takes hold with the next scene made or opened."));
    undoLevels->setValue(m_document->undoLevels());
    generalLayout->addRow(tr("Levels of undo"), undoLevels);
    QCheckBox* const keepBackup = new QCheckBox(tr("Keep a backup of the scene file when saving"), general);
    keepBackup->setObjectName(QStringLiteral("PrefKeepBackup"));
    keepBackup->setToolTip(tr("Before a scene is saved over, the file as it was is kept beside it, with ~ after its name."));
    keepBackup->setChecked(s.value(QStringLiteral("prefs/keepBackup"), true).toBool());
    generalLayout->addRow(keepBackup);
    QCheckBox* const askAutoSave = new QCheckBox(tr("Ask before saving by itself"), general);
    askAutoSave->setObjectName(QStringLiteral("PrefAskAutoSave"));
    askAutoSave->setChecked(s.value(QStringLiteral("prefs/askAutoSave"), false).toBool());
    generalLayout->addRow(askAutoSave);

    QGroupBox* const camera = new QGroupBox(tr("Camera"), &dialog);
    QComboBox* const selectMode = new QComboBox(camera);
    selectMode->addItems({tr("Lasso"), tr("Marquee")});
    selectMode->setCurrentIndex(s.value(QStringLiteral("prefs/lasso"), true).toBool() ? 0 : 1);
    QFormLayout* const cameraLayout = new QFormLayout(camera);
    cameraLayout->addRow(tr("The Select tool starts with"), selectMode);
    QComboBox* const cameraZoom = new QComboBox(camera);
    cameraZoom->setObjectName(QStringLiteral("PrefCameraZoom"));
    cameraZoom->addItems({tr("Fit to View"), tr("100%")});
    cameraZoom->setCurrentIndex(std::clamp(s.value(QStringLiteral("prefs/cameraZoom"), 0).toInt(), 0, 1));
    cameraZoom->setToolTip(tr("How the Camera view shows a scene made or opened: the whole frame in the view, or at its own size."));
    cameraLayout->addRow(tr("A scene opens"), cameraZoom);
    QSpinBox* const nudge = new QSpinBox(camera);
    nudge->setObjectName(QStringLiteral("PrefNudge"));
    nudge->setRange(1, 100);
    nudge->setSuffix(tr(" pixels"));
    nudge->setToolTip(tr("How far an arrow key moves what the Select tool has selected (ten times as far with Shift)."));
    nudge->setValue(int(m_canvas->nudgeStep()));
    cameraLayout->addRow(tr("Nudging factor"), nudge);
    QCheckBox* const frameOne = new QCheckBox(tr("Set Keyframe at Frame One with First Application of the Transform Tool"), camera);
    frameOne->setObjectName(QStringLiteral("PrefKeyFrameOne"));
    frameOne->setToolTip(tr("The first time a layer is moved, on a frame after the first, the first frame gets a keyframe of the layer as it was: it goes from there to where you put it."));
    frameOne->setChecked(m_canvas->keysFrameOne());
    cameraLayout->addRow(frameOne);
    QSpinBox* const gridAcross = new QSpinBox(camera);
    gridAcross->setObjectName(QStringLiteral("PrefGridAcross"));
    gridAcross->setRange(2, 12);
    gridAcross->setValue(m_canvas->proportionAcross());
    QSpinBox* const gridDown = new QSpinBox(camera);
    gridDown->setObjectName(QStringLiteral("PrefGridDown"));
    gridDown->setRange(2, 12);
    gridDown->setValue(m_canvas->proportionDown());
    QHBoxLayout* const gridParts = new QHBoxLayout;
    gridParts->addWidget(gridAcross);
    gridParts->addWidget(new QLabel(tr("across by"), camera));
    gridParts->addWidget(gridDown);
    gridParts->addWidget(new QLabel(tr("down"), camera));
    gridParts->addStretch(1);
    cameraLayout->addRow(tr("Proportion grid"), gridParts);
    QSpinBox* const safe = new QSpinBox(camera);
    safe->setObjectName(QStringLiteral("PrefSafeMargin"));
    safe->setRange(1, 30);
    safe->setSuffix(tr(" %"));
    safe->setToolTip(tr("How much of the frame, at each edge, lies outside the safe area (the title-safe area leaves twice as much)."));
    safe->setValue(int(std::lround(m_canvas->safeMargin() * 100.0)));
    cameraLayout->addRow(tr("TV safety"), safe);
    QCheckBox* const zoomOnMouse = new QCheckBox(tr("Zoom In and Zoom Out about the pointer"), camera);
    zoomOnMouse->setObjectName(QStringLiteral("PrefZoomOnMouse"));
    zoomOnMouse->setToolTip(tr("The zoom shortcuts zoom about where the pointer is over the view, instead of about its middle."));
    zoomOnMouse->setChecked(m_canvas->zoomsOnMouse());
    cameraLayout->addRow(zoomOnMouse);

    QGroupBox* const sheet = new QGroupBox(tr("Exposure Sheet"), &dialog);
    QCheckBox* const extend = new QCheckBox(tr("Extend Exposure of Previous Drawing"), sheet);
    extend->setObjectName(QStringLiteral("PrefExtendExposure"));
    extend->setToolTip(tr("When you draw on an empty frame, the drawing before it is held over the empty frames up to it.\n"
                          "Off: those frames stay empty."));
    extend->setChecked(m_document->extendsPreviousExposure());
    QFormLayout* const sheetLayout = new QFormLayout(sheet);
    sheetLayout->addRow(extend);
    QCheckBox* const byFrame = new QCheckBox(tr("Use Current Frame as Drawing Name"), sheet);
    byFrame->setObjectName(QStringLiteral("PrefNameByFrame"));
    byFrame->setToolTip(tr("A drawing made on a frame is named after that frame; off, it goes by its number in the layer."));
    byFrame->setChecked(m_document->namesDrawingsByFrame());
    sheetLayout->addRow(byFrame);
    QSpinBox* const columnWidth = new QSpinBox(sheet);
    columnWidth->setObjectName(QStringLiteral("PrefXsheetColumnWidth"));
    columnWidth->setRange(40, 240);
    columnWidth->setSuffix(tr(" pixels"));
    columnWidth->setValue(std::clamp(s.value(QStringLiteral("prefs/xsheetColumnWidth"), 78).toInt(), 40, 240));
    sheetLayout->addRow(tr("Column width"), columnWidth);

    QGroupBox* const advanced = new QGroupBox(tr("Advanced"), &dialog);
    QCheckBox* const moreArt = new QCheckBox(tr("Support Overlay and Underlay Arts"), advanced);
    moreArt->setObjectName(QStringLiteral("PrefOverlayUnderlay"));
    moreArt->setToolTip(tr("Two more sheets in every drawing: an overlay over the line art and an underlay beneath the colour art."));
    moreArt->setChecked(s.value(QStringLiteral("prefs/overlayUnderlay"), false).toBool());
    QFormLayout* const advancedLayout = new QFormLayout(advanced);
    advancedLayout->addRow(moreArt);

    // Timeline: how wide a frame is.
    QGroupBox* const timelinePage = new QGroupBox(tr("Timeline"), &dialog);
    QSpinBox* const frameWidth = new QSpinBox(timelinePage);
    frameWidth->setObjectName(QStringLiteral("PrefFrameWidth"));
    frameWidth->setRange(4, 30);
    frameWidth->setSuffix(tr(" pixels"));
    frameWidth->setValue(std::clamp(s.value(QStringLiteral("prefs/frameWidth"), 8).toInt(), 4, 30));
    frameWidth->setToolTip(tr("How wide a frame of the Timeline is when Eclipse starts (the zoom of the Timeline changes it afterwards)."));
    QFormLayout* const timelineLayout = new QFormLayout(timelinePage);
    timelineLayout->addRow(tr("Frame width"), frameWidth);
    QCheckBox* const stopMotion = new QCheckBox(tr("Default: Stop-Motion Keyframes"), timelinePage);
    stopMotion->setObjectName(QStringLiteral("PrefStopMotionKeys"));
    stopMotion->setToolTip(tr("A keyframe made new holds its pose until the next keyframe, instead of moving on to it.\n"
                              "(A keyframe can be turned either way afterwards, as always.)"));
    stopMotion->setChecked(m_document->stopMotionKeys());
    timelineLayout->addRow(stopMotion);
    QSpinBox* const loopPause = new QSpinBox(timelinePage);
    loopPause->setObjectName(QStringLiteral("PrefLoopPause"));
    loopPause->setRange(0, 5000);
    loopPause->setSingleStep(100);
    loopPause->setSuffix(tr(" ms"));
    loopPause->setSpecialValueText(tr("None"));
    loopPause->setToolTip(tr("Playing in a loop: how long the last frame is held before the loop plays again."));
    loopPause->setValue(m_loopPause);
    timelineLayout->addRow(tr("Pause at the end of a loop"), loopPause);
    QCheckBox* const showSound = new QCheckBox(tr("Show sound layers"), timelinePage);
    showSound->setObjectName(QStringLiteral("PrefShowSound"));
    showSound->setChecked(s.value(QStringLiteral("prefs/timelineSound"), true).toBool());
    timelineLayout->addRow(showSound);
    QCheckBox* const waveforms = new QCheckBox(tr("Show sound waveforms"), timelinePage);
    waveforms->setObjectName(QStringLiteral("PrefWaveforms"));
    waveforms->setChecked(s.value(QStringLiteral("prefs/timelineWaveforms"), true).toBool());
    timelineLayout->addRow(waveforms);
    QCheckBox* const showGroups = new QCheckBox(tr("Show group layers"), timelinePage);
    showGroups->setObjectName(QStringLiteral("PrefShowGroups"));
    showGroups->setChecked(s.value(QStringLiteral("prefs/timelineGroups"), true).toBool());
    timelineLayout->addRow(showGroups);
    QSpinBox* const preroll = new QSpinBox(timelinePage);
    preroll->setObjectName(QStringLiteral("PrefPreroll"));
    preroll->setRange(0, 1000);
    preroll->setSpecialValueText(tr("None"));
    preroll->setSuffix(tr(" frames"));
    preroll->setToolTip(tr("Play starts so many frames before the current frame, to see what leads up to it."));
    preroll->setValue(m_preroll);
    timelineLayout->addRow(tr("Preroll"), preroll);
    QCheckBox* const loopAtStart = new QCheckBox(tr("Play in a loop when Eclipse starts"), timelinePage);
    loopAtStart->setObjectName(QStringLiteral("PrefLoop"));
    loopAtStart->setChecked(s.value(QStringLiteral("prefs/loop"), true).toBool());
    timelineLayout->addRow(loopAtStart);
    QCheckBox* const functionGrid = new QCheckBox(tr("Show the grid in the Function view"), timelinePage);
    functionGrid->setObjectName(QStringLiteral("PrefFunctionGrid"));
    functionGrid->setChecked(s.value(QStringLiteral("prefs/functionGrid"), true).toBool());
    timelineLayout->addRow(functionGrid);

    // Render: the preview.
    QGroupBox* const renderPage = new QGroupBox(tr("Render"), &dialog);
    QComboBox* const previewSize = new QComboBox(renderPage);
    previewSize->setObjectName(QStringLiteral("PrefPreviewSize"));
    previewSize->addItem(tr("Full size"), 100);
    previewSize->addItem(tr("Half size"), 50);
    previewSize->addItem(tr("Quarter size"), 25);
    previewSize->setCurrentIndex(std::max(0, previewSize->findData(s.value(QStringLiteral("prefs/previewSize"), 50).toInt())));
    QCheckBox* const autoRender = new QCheckBox(tr("Auto Render the preview"), renderPage);
    autoRender->setObjectName(QStringLiteral("PrefAutoRender"));
    autoRender->setToolTip(tr("The preview of the Preview Manager is brought up to date by itself a moment after each change."));
    autoRender->setChecked(s.value(QStringLiteral("prefs/autoRender"), false).toBool());
    QFormLayout* const renderLayout = new QFormLayout(renderPage);
    renderLayout->addRow(tr("Preview size"), previewSize);
    renderLayout->addRow(autoRender);

    // Tools: what the drawing tools start with.
    QGroupBox* const toolsPage = new QGroupBox(tr("Tools"), &dialog);
    QSpinBox* const toolSize = new QSpinBox(toolsPage);
    toolSize->setObjectName(QStringLiteral("PrefToolSize"));
    toolSize->setRange(1, 200);
    toolSize->setSuffix(tr(" pixels"));
    toolSize->setValue(std::clamp(s.value(QStringLiteral("prefs/toolSize"), int(m_canvas->brushWidth())).toInt(), 1, 200));
    toolSize->setToolTip(tr("The size of the brush, the eraser and the other drawing tools when Eclipse starts."));
    QFormLayout* const toolsLayout = new QFormLayout(toolsPage);
    toolsLayout->addRow(tr("Size of the drawing tools"), toolSize);
    QCheckBox* const brushOutline = new QCheckBox(tr("Show the brush's size round the pointer"), toolsPage);
    brushOutline->setObjectName(QStringLiteral("PrefBrushOutline"));
    brushOutline->setToolTip(tr("The Brush, the Eraser and the Pencil show a circle as big as what they draw, where the pointer is."));
    brushOutline->setChecked(s.value(QStringLiteral("prefs/brushOutline"), false).toBool());
    toolsLayout->addRow(brushOutline);
    QComboBox* const units = new QComboBox(toolsPage);
    units->setObjectName(QStringLiteral("PrefUnits"));
    units->addItems({tr("Fields"), tr("Pixels")});
    units->setCurrentIndex(std::clamp(s.value(QStringLiteral("prefs/units"), 0).toInt(), 0, 1));
    units->setToolTip(tr("How positions are written in the Coord. and Control Points view: in fields, with the letter of the direction (3 E, 2 N), or in pixels of the scene."));
    toolsLayout->addRow(tr("Positions in"), units);
    QSpinBox* const smoothing = new QSpinBox(toolsPage);
    smoothing->setObjectName(QStringLiteral("PrefSmoothing"));
    smoothing->setRange(0, 100);
    smoothing->setSuffix(tr(" %"));
    smoothing->setToolTip(tr("How much a line drawn with the brush or pencil is smoothed when the pen lifts, when Eclipse starts."));
    smoothing->setValue(std::clamp(s.value(QStringLiteral("prefs/smoothing"), 25).toInt(), 0, 100));
    toolsLayout->addRow(tr("Smoothing"), smoothing);
    QSpinBox* const corners = new QSpinBox(toolsPage);
    corners->setObjectName(QStringLiteral("PrefCorners"));
    corners->setRange(0, 180);
    corners->setSuffix(tr(" deg"));
    corners->setToolTip(tr("A turn sharper than this many degrees stays a sharp corner when a line is smoothed, when Eclipse starts."));
    corners->setValue(std::clamp(s.value(QStringLiteral("prefs/corners"), 50).toInt(), 0, 180));
    toolsLayout->addRow(tr("Corners"), corners);

    // Node View: how the graph is shown.
    QGroupBox* const nodePage = new QGroupBox(tr("Node View"), &dialog);
    QCheckBox* const navigator = new QCheckBox(tr("Show the small map of the graph in the corner"), nodePage);
    navigator->setObjectName(QStringLiteral("PrefNodeNavigator"));
    navigator->setChecked(s.value(QStringLiteral("prefs/nodeNavigator"), true).toBool());
    QCheckBox* const straight = new QCheckBox(tr("Straight cables (instead of curves)"), nodePage);
    straight->setObjectName(QStringLiteral("PrefNodeStraight"));
    straight->setChecked(s.value(QStringLiteral("prefs/nodeStraight"), false).toBool());
    QFormLayout* const nodeLayout = new QFormLayout(nodePage);
    nodeLayout->addRow(navigator);
    nodeLayout->addRow(straight);
    QComboBox* const nodeDoubleClick = new QComboBox(nodePage);
    nodeDoubleClick->setObjectName(QStringLiteral("PrefNodeDoubleClick"));
    nodeDoubleClick->addItems({tr("Renames the layer"), tr("Opens Layer Properties")});
    nodeDoubleClick->setCurrentIndex(std::clamp(s.value(QStringLiteral("prefs/nodeDoubleClick"), 0).toInt(), 0, 1));
    nodeLayout->addRow(tr("A double-click on a layer's node"), nodeDoubleClick);

    // Onion Skin: how many drawings either side it shows, and how strongly, when Eclipse starts.
    QGroupBox* const onionPage = new QGroupBox(tr("Onion Skin"), &dialog);
    QFormLayout* const onionLayout = new QFormLayout(onionPage);
    QSpinBox* const onionCountBefore = new QSpinBox(onionPage);
    onionCountBefore->setObjectName(QStringLiteral("PrefOnionPrevious"));
    onionCountBefore->setRange(0, 6);
    onionCountBefore->setValue(std::clamp(s.value(QStringLiteral("prefs/onionPrevious"), m_canvas->onionPrevious()).toInt(), 0, 6));
    QSpinBox* const onionCountAfter = new QSpinBox(onionPage);
    onionCountAfter->setObjectName(QStringLiteral("PrefOnionNext"));
    onionCountAfter->setRange(0, 6);
    onionCountAfter->setValue(std::clamp(s.value(QStringLiteral("prefs/onionNext"), m_canvas->onionNext()).toInt(), 0, 6));
    QSpinBox* const onionStrength = new QSpinBox(onionPage);
    onionStrength->setObjectName(QStringLiteral("PrefOnionOpacity"));
    onionStrength->setRange(5, 100);
    onionStrength->setSuffix(tr(" %"));
    onionStrength->setValue(std::clamp(s.value(QStringLiteral("prefs/onionOpacity"), m_canvas->onionOpacity()).toInt(), 5, 100));
    onionLayout->addRow(tr("Drawings before"), onionCountBefore);
    onionLayout->addRow(tr("Drawings after"), onionCountAfter);
    onionLayout->addRow(tr("Opacity"), onionStrength);
    QCheckBox* const onionFade = new QCheckBox(tr("Fade with distance"), onionPage);
    onionFade->setObjectName(QStringLiteral("PrefOnionFade"));
    onionFade->setToolTip(tr("The drawings nearest the current one show the most, those furthest the least."));
    onionFade->setChecked(s.value(QStringLiteral("prefs/onionFade"), false).toBool());
    onionLayout->addRow(onionFade);
    QSpinBox* const onionNearest = new QSpinBox(onionPage);
    onionNearest->setObjectName(QStringLiteral("PrefOnionNearest"));
    onionNearest->setRange(0, 100);
    onionNearest->setSuffix(tr(" %"));
    onionNearest->setValue(std::clamp(s.value(QStringLiteral("prefs/onionNearest"), 100).toInt(), 0, 100));
    onionLayout->addRow(tr("Nearest"), onionNearest);
    QSpinBox* const onionFurthest = new QSpinBox(onionPage);
    onionFurthest->setObjectName(QStringLiteral("PrefOnionFurthest"));
    onionFurthest->setRange(0, 100);
    onionFurthest->setSuffix(tr(" %"));
    onionFurthest->setValue(std::clamp(s.value(QStringLiteral("prefs/onionFurthest"), 30).toInt(), 0, 100));
    onionLayout->addRow(tr("Furthest"), onionFurthest);

    // Sound: when it is heard.
    QGroupBox* const soundPage = new QGroupBox(tr("Sound"), &dialog);
    QCheckBox* const soundOn = new QCheckBox(tr("Play the sound when the scene plays"), soundPage);
    soundOn->setObjectName(QStringLiteral("PrefSoundOn"));
    soundOn->setChecked(m_soundOn);
    QCheckBox* const scrub = new QCheckBox(tr("Play a frame of sound when the frame is changed by hand (scrubbing)"), soundPage);
    scrub->setObjectName(QStringLiteral("PrefScrub"));
    scrub->setChecked(m_scrub);
    QFormLayout* const soundLayout = new QFormLayout(soundPage);
    soundLayout->addRow(soundOn);
    soundLayout->addRow(scrub);

    // Drawing: the light table.
    QGroupBox* const drawing = new QGroupBox(tr("Drawing"), &dialog);
    QSpinBox* const wash = new QSpinBox(drawing);
    wash->setObjectName(QStringLiteral("PrefLightTableWash"));
    wash->setRange(10, 90);
    wash->setSuffix(tr(" %"));
    wash->setToolTip(tr("With the light table on: how strongly the layers that are not being drawn on show."));
    wash->setValue(m_canvas->lightTableWash());
    QFormLayout* const drawingLayout = new QFormLayout(drawing);
    drawingLayout->addRow(tr("Light table: other layers show at"), wash);
    QCheckBox* const onTop = new QCheckBox(tr("Current drawing on top when Eclipse starts"), drawing);
    onTop->setObjectName(QStringLiteral("PrefDrawingOnTop"));
    onTop->setToolTip(tr("The layer being drawn on is shown over the others in the Camera view (View > Current Drawing on Top)."));
    onTop->setChecked(s.value(QStringLiteral("prefs/drawingOnTop"), false).toBool());
    drawingLayout->addRow(onTop);

    // Colours: of the view round the frame, and of the onion skin.
    QGroupBox* const colours = new QGroupBox(tr("Colours"), &dialog);
    QFormLayout* const coloursLayout = new QFormLayout(colours);
    auto colourButton = [&](char const* name, QColor const& now, QString const& title) {
        QPushButton* const button = new QPushButton(colours);
        button->setObjectName(QLatin1String(name));
        auto show = [button](QColor const& colour) {
            button->setProperty("colour", colour);
            button->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid #202020; min-height: 16px; min-width: 60px;").arg(colour.name()));
        };
        show(now);
        connect(button, &QPushButton::clicked, &dialog, [button, show, title, &dialog] {
            QColor const chosen = QColorDialog::getColor(button->property("colour").value<QColor>(), &dialog, title);
            if (chosen.isValid())
                show(chosen);
        });
        return button;
    };
    QPushButton* const backdrop = colourButton("PrefBackdrop", m_canvas->backdrop(), tr("Around the Frame"));
    QPushButton* const onionBefore = colourButton("PrefOnionBefore", m_canvas->onionBefore(), tr("Onion Skin: Before"));
    QPushButton* const onionAfter = colourButton("PrefOnionAfter", m_canvas->onionAfter(), tr("Onion Skin: After"));
    coloursLayout->addRow(tr("Camera view, around the frame"), backdrop);
    coloursLayout->addRow(tr("Onion skin, drawings before"), onionBefore);
    coloursLayout->addRow(tr("Onion skin, drawings after"), onionAfter);
    QPushButton* const usual = new QPushButton(tr("Restore the Usual Colours"), colours);
    usual->setObjectName(QStringLiteral("PrefColoursReset"));
    coloursLayout->addRow(usual);
    connect(usual, &QPushButton::clicked, &dialog, [backdrop, onionBefore, onionAfter] {
        struct Usual {
            QPushButton* button;
            QColor colour;
        };
        for (Usual const& one : {Usual{backdrop, QColor(127, 127, 127)}, Usual{onionBefore, QColor(220, 60, 60)}, Usual{onionAfter, QColor(50, 160, 80)}}) {
            one.button->setProperty("colour", one.colour);
            one.button->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid #202020; min-height: 16px; min-width: 60px;").arg(one.colour.name()));
        }
    });

    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    // A tab for each kind of setting, as Harmony has them.
    QTabWidget* const tabs = new QTabWidget(&dialog);
    tabs->setObjectName(QStringLiteral("PreferencesTabs"));
    for (QGroupBox* const page : {general, camera, sheet, timelinePage, drawing, toolsPage, onionPage, nodePage, colours, renderPage, soundPage, advanced}) {
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
    if (result != QDialog::Accepted)
        return;
    s.setValue(QStringLiteral("prefs/lightTableWash"), wash->value());
    s.setValue(QStringLiteral("prefs/frameWidth"), frameWidth->value());
    s.setValue(QStringLiteral("prefs/previewSize"), previewSize->currentData().toInt());
    s.setValue(QStringLiteral("prefs/autoRender"), autoRender->isChecked());
    if (toolSize->value() != int(m_canvas->brushWidth()) || s.contains(QStringLiteral("prefs/toolSize")))
        s.setValue(QStringLiteral("prefs/toolSize"), toolSize->value());
    s.setValue(QStringLiteral("prefs/soundOn"), soundOn->isChecked());
    s.setValue(QStringLiteral("prefs/units"), units->currentIndex());
    s.setValue(QStringLiteral("prefs/smoothing"), smoothing->value());
    s.setValue(QStringLiteral("prefs/corners"), corners->value());
    s.setValue(QStringLiteral("prefs/nodeNavigator"), navigator->isChecked());
    s.setValue(QStringLiteral("prefs/nodeStraight"), straight->isChecked());
    s.setValue(QStringLiteral("prefs/onionPrevious"), onionCountBefore->value());
    s.setValue(QStringLiteral("prefs/onionNext"), onionCountAfter->value());
    s.setValue(QStringLiteral("prefs/onionOpacity"), onionStrength->value());
    s.setValue(QStringLiteral("prefs/scrub"), scrub->isChecked());
    s.setValue(QStringLiteral("prefs/backdrop"), backdrop->property("colour").value<QColor>().name());
    m_canvas->setOnionColours(onionBefore->property("colour").value<QColor>(), onionAfter->property("colour").value<QColor>());
    s.setValue(QStringLiteral("welcome/hide"), !welcome->isChecked());
    s.setValue(QStringLiteral("prefs/autosaveMinutes"), autoSave->value());
    s.setValue(QStringLiteral("prefs/lasso"), selectMode->currentIndex() == 0);
    s.setValue(QStringLiteral("prefs/overlayUnderlay"), moreArt->isChecked());
    s.setValue(QStringLiteral("prefs/nudge"), nudge->value());
    s.setValue(QStringLiteral("prefs/undoLevels"), undoLevels->value());
    s.setValue(QStringLiteral("prefs/keyFrameOne"), frameOne->isChecked());
    s.setValue(QStringLiteral("prefs/extendExposure"), extend->isChecked());
    s.setValue(QStringLiteral("prefs/stopMotionKeys"), stopMotion->isChecked());
    s.setValue(QStringLiteral("prefs/loopPause"), loopPause->value());
    s.setValue(QStringLiteral("prefs/zoomOnMouse"), zoomOnMouse->isChecked());
    s.setValue(QStringLiteral("prefs/nameByFrame"), byFrame->isChecked());
    s.setValue(QStringLiteral("prefs/proportionAcross"), gridAcross->value());
    s.setValue(QStringLiteral("prefs/proportionDown"), gridDown->value());
    s.setValue(QStringLiteral("prefs/safeMargin"), safe->value());
    s.setValue(QStringLiteral("prefs/keepBackup"), keepBackup->isChecked());
    s.setValue(QStringLiteral("prefs/askAutoSave"), askAutoSave->isChecked());
    s.setValue(QStringLiteral("prefs/cameraZoom"), cameraZoom->currentIndex());
    s.setValue(QStringLiteral("prefs/timelineSound"), showSound->isChecked());
    s.setValue(QStringLiteral("prefs/timelineWaveforms"), waveforms->isChecked());
    s.setValue(QStringLiteral("prefs/timelineGroups"), showGroups->isChecked());
    s.setValue(QStringLiteral("prefs/preroll"), preroll->value());
    s.setValue(QStringLiteral("prefs/nodeDoubleClick"), nodeDoubleClick->currentIndex());
    s.setValue(QStringLiteral("prefs/brushOutline"), brushOutline->isChecked());
    s.setValue(QStringLiteral("prefs/loop"), loopAtStart->isChecked());
    s.setValue(QStringLiteral("prefs/functionGrid"), functionGrid->isChecked());
    s.setValue(QStringLiteral("prefs/xsheetColumnWidth"), columnWidth->value());
    s.setValue(QStringLiteral("prefs/drawingOnTop"), onTop->isChecked());
    s.setValue(QStringLiteral("prefs/onionFade"), onionFade->isChecked());
    s.setValue(QStringLiteral("prefs/onionNearest"), onionNearest->value());
    s.setValue(QStringLiteral("prefs/onionFurthest"), onionFurthest->value());
    s.sync();
    applyPreferences();
}
