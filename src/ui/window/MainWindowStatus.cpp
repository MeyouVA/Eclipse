// Eclipse - the main window: title and status, which commands are on, undo and redo, choosing a tool.
#include "ui/window/MainWindow.h"

#include <QApplication>
#include <QFileInfo>
#include <QDialog>
#include <QDir>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QTextBrowser>
#include <QVBoxLayout>

#include "actions/manager/ActionManager.h"
#include "actions/dialogs/ShortcutsDialog.h"
#include "audio/sound/Sound.h"
#include "model/document/Commands.h"
#include "ui/canvas/Canvas.h"
#include "ui/io/MovieExport.h"
#include "ui/panels/PalettePanel.h"
#include "ui/timeline/Timeline.h"
#include "ui/timeline/Xsheet.h"
#include "ui/views/DepthView.h"
#include "ui/views/LipSyncView.h"
#include "ui/views/PerspectiveView.h"

#include "ui/window/MainWindowPrivate.h"
#include <QLabel>

void MainWindow::updateTitle()
{
    QString const name = m_path.isEmpty() ? tr("Untitled") : QFileInfo(m_path).fileName();
    setWindowTitle(QStringLiteral("%1%2 - Eclipse").arg(name, m_document->undoStack()->isClean() ? QString() : QStringLiteral("*")));
}

void MainWindow::updateActionStates()
{
    Scene const& scene = m_document->scene();
    int const l = layer(), f = frame();
    if (l < 0 || l >= scene.layers.size())
        return;
    Layer const& current = scene.layers[l];
    auto set = [this](char const* id, bool enabled) {
        if (QAction* const a = m_actions->action(QLatin1String(id)))
            a->setEnabled(enabled);
    };
    // Keyframes.
    bool const keyHere = current.keyIndex(f) >= 0;
    bool const keyBefore = keyHere || current.previousKeyFrame(f) >= 0;
    set("DeleteKeyframe", keyHere);
    set("GotoPrevKeyframe", current.previousKeyFrame(f) >= 0);
    set("GotoNextKeyframe", current.nextKeyFrame(f) >= 0);
    set("SetMotionKeyframe", keyBefore);
    set("SetStopMotionKeyframe", keyBefore);
    // Drawings and their exposure: a layer that holds drawings, and for most a drawing in the cell.
    bool const draws = current.holdsDrawings();
    bool const cell = draws && current.drawingAt(f) != Layer::kNoDrawing;
    set("CreateEmptyDrawing", draws);
    set("DeleteDrawing", cell);
    set("EditSceneMarker", scene.markerAt(f) >= 0);
    set("DeleteSceneMarker", scene.markerAt(f) >= 0);
    set("ToggleLayerOnion", draws);
    set("DuplicateDrawing", cell);
    set("ExtendExposure", draws && f > 0 && current.drawingAt(f - 1) != Layer::kNoDrawing);
    set("ClearExposure", cell);
    for (char const* id : {"IncreaseExposure", "DecreaseExposure", "SetExposure1", "SetExposure2", "SetExposure3", "ClearExposureAndPull"})
        set(id, cell);
    set("InsertBlankCell", draws);
    set("FillEmptyCells", draws);
    set("PreviousDrawing", draws && current.previousDrawingFrame(f) >= 0);
    set("NextDrawing", draws && current.nextDrawingFrame(f) >= 0);
    set("SubstitutePrevious", cell && current.drawings.size() > 1);
    set("SubstituteNext", cell && current.drawings.size() > 1);
    // Where the layer hangs.
    bool const attached = scene.parentIndex(l) >= 0, carries = scene.hasChildren(l);
    set("DetachLayer", attached);
    set("SelectParent", attached);
    set("SelectChild", carries);
    set("CollapseExpand", carries);
    set("AttachLayer", scene.layers.size() > 1);
    // With several layers selected, a command is there if it has something to do on any of them.
    bool canLock = false, canUnlock = false, coloured = false;
    for (int s : m_timeline->selectedLayers()) {
        canLock = canLock || !scene.layers[s].locked;
        canUnlock = canUnlock || scene.layers[s].locked;
        coloured = coloured || scene.layers[s].trackColor.isValid();
    }
    set("LockLayer", canLock);
    set("UnlockLayer", canUnlock);
    set("DefaultTrackColour", coloured);
    set("MoveSoundHere", current.kind == Layer::Sound);
    // What is selected in the drawing.
    bool const picked = m_canvas->selecting() && m_canvas->hasSelection();
    for (char const* id : {"FlipHorizontal", "FlipVertical", "Rotate90CW", "Rotate90CCW", "Rotate180", "BringToFront", "SendToBack",
                           "BringForward", "SendBackward"})
        set(id, picked);
}

void MainWindow::updateStatus()
{
    Scene const& scene = m_document->scene();
    if (layer() < 0 || layer() >= scene.layers.size())
        return;
    // The drawing under the playhead, by the layer's name and its own: "Drawing-1".
    Layer const& current = scene.layers[layer()];
    int const drawing = current.holdsDrawings() ? current.drawingAt(frame()) : Layer::kNoDrawing;
    m_drawingLabel->setText(!current.holdsDrawings() ? current.name
                            : drawing == Layer::kNoDrawing ? tr("%1 Empty Cell").arg(current.name) : tr("%1-%2").arg(current.name, current.drawingLabel(drawing)));
    // The frame, and where the pointer is.
    m_statusLabel->setText(m_pointerKnown ? tr("Fr %1      %2:%3").arg(frame() + 1).arg(m_pointer.x(), 0, 'f', 2).arg(m_pointer.y(), 0, 'f', 2)
                                          : tr("Fr %1").arg(frame() + 1));
    // The colour in hand.
    Swatch const* const swatch = m_palette ? scene.findSwatch(m_palette->currentSwatch()) : nullptr;
    m_colourLabel->setText(swatch ? swatch->name : QString());
    m_colourPatch->setStyleSheet(swatch ? QStringLiteral("background: %1; border: 1px solid #101010;").arg(swatch->color.name()) : QString());
}

void MainWindow::onActionUndo()
{
    m_document->undoStack()->undo();
}

void MainWindow::onActionRedo()
{
    m_document->undoStack()->redo();
}

void MainWindow::onActionEditShortcuts()
{
    ShortcutsDialog dialog(m_actions, this);
    dialog.exec();
}

// Help > Eclipse Help: the pages kept in `resources/help` beside the program, in a window of their own.
void MainWindow::onActionHelp()
{
    QDialog* window = findChild<QDialog*>(QStringLiteral("HelpWindow"));
    if (!window) {
        QString const folder = QCoreApplication::applicationDirPath() + QStringLiteral("/resources/help");
        if (!QFileInfo::exists(folder + QStringLiteral("/index.html"))) {
            QMessageBox::information(this, tr("Eclipse Help"), tr("The help pages were not found in %1.").arg(QDir::toNativeSeparators(folder)));
            return;
        }
        window = new QDialog(this);
        window->setObjectName(QStringLiteral("HelpWindow"));
        window->setWindowTitle(tr("Eclipse Help"));
        window->resize(760, 680);
        QTextBrowser* const pages = new QTextBrowser(window);
        pages->setObjectName(QStringLiteral("HelpPages"));
        pages->setSearchPaths({folder});
        pages->setOpenExternalLinks(false);
        QPushButton* const back = new QPushButton(tr("Back"), window);
        back->setObjectName(QStringLiteral("HelpBack"));
        QPushButton* const forward = new QPushButton(tr("Forward"), window);
        QPushButton* const contents = new QPushButton(tr("Contents"), window);
        contents->setObjectName(QStringLiteral("HelpContents"));
        back->setEnabled(false);
        forward->setEnabled(false);
        connect(back, &QPushButton::clicked, pages, &QTextBrowser::backward);
        connect(forward, &QPushButton::clicked, pages, &QTextBrowser::forward);
        connect(contents, &QPushButton::clicked, pages, [pages] { pages->setSource(QUrl(QStringLiteral("index.html"))); });
        connect(pages, &QTextBrowser::backwardAvailable, back, &QPushButton::setEnabled);
        connect(pages, &QTextBrowser::forwardAvailable, forward, &QPushButton::setEnabled);
        QHBoxLayout* const bar = new QHBoxLayout;
        bar->addWidget(back);
        bar->addWidget(forward);
        bar->addWidget(contents);
        bar->addStretch(1);
        QVBoxLayout* const layout = new QVBoxLayout(window);
        layout->addLayout(bar);
        layout->addWidget(pages, 1);
        pages->setSource(QUrl(QStringLiteral("index.html")));
    }
    window->show();
    window->raise();
    window->activateWindow();
}

void MainWindow::onActionAbout()
{
    QMessageBox::about(this, tr("About Eclipse"),
                       tr("<b>Eclipse</b> %1<br>2D animation.").arg(QApplication::applicationVersion()));
}

void MainWindow::stepColour(int direction)
{
    QVector<Swatch> const& palette = m_document->scene().palette;
    for (int i = 0; i < palette.size(); ++i) {
        if (palette[i].id != m_palette->currentSwatch())
            continue;
        if (i + direction >= 0 && i + direction < palette.size())
            m_palette->setCurrentSwatch(palette[i + direction].id);
        return;
    }
}

void MainWindow::onActionChooseTool(QString const& name)
{
    static QHash<QString, Canvas::Tool> const tools{
        {QStringLiteral("select"), Canvas::Select}, {QStringLiteral("transform"), Canvas::Transform},
        {QStringLiteral("brush"), Canvas::Brush},   {QStringLiteral("eraser"), Canvas::Eraser},
        {QStringLiteral("paint"), Canvas::Paint},   {QStringLiteral("hand"), Canvas::Hand},
        {QStringLiteral("pencil"), Canvas::Pencil}, {QStringLiteral("line"), Canvas::Line},
        {QStringLiteral("rectangle"), Canvas::Rectangle}, {QStringLiteral("ellipse"), Canvas::Ellipse},
        {QStringLiteral("contour"), Canvas::ContourEditor}, {QStringLiteral("rigging"), Canvas::Rigging},
        {QStringLiteral("ik"), Canvas::IK}, {QStringLiteral("dropper"), Canvas::Dropper},
        {QStringLiteral("zoom"), Canvas::Zoom}, {QStringLiteral("polyline"), Canvas::Polyline},
        {QStringLiteral("paintunpainted"), Canvas::PaintUnpainted}, {QStringLiteral("repaint"), Canvas::Repaint},
        {QStringLiteral("unpaint"), Canvas::Unpaint}, {QStringLiteral("text"), Canvas::Text},
        {QStringLiteral("rotateview"), Canvas::RotateView}, {QStringLiteral("cutter"), Canvas::Cutter},
        {QStringLiteral("stroke"), Canvas::StrokeTool}, {QStringLiteral("closegap"), Canvas::CloseGap},
        {QStringLiteral("gradient"), Canvas::EditGradient}, {QStringLiteral("reposition"), Canvas::Reposition}, {QStringLiteral("stamp"), Canvas::Stamp}, {QStringLiteral("pencileditor"), Canvas::PencilEditor}, {QStringLiteral("perspective"), Canvas::Perspective}, {QStringLiteral("ink"), Canvas::Ink}, {QStringLiteral("smootheditor"), Canvas::SmoothEditor}, {QStringLiteral("envelope"), Canvas::Envelope}};
    if (tools.contains(name)) {
        m_canvas->setTransformMode(Canvas::TransformAll);       // the Transform tool itself does everything
        m_canvas->setTool(tools.value(name));
    }
}

// Animation > Tools > Translate, Rotate, Scale: the Transform tool, doing that only.
void MainWindow::onActionTransformMode(QString const& mode)
{
    m_canvas->setTool(Canvas::Transform);
    m_canvas->setTransformMode(mode == QLatin1String("translate") ? Canvas::TranslateOnly : mode == QLatin1String("rotate") ? Canvas::RotateOnly
                               : mode == QLatin1String("skew") ? Canvas::SkewOnly : Canvas::ScaleOnly);
    statusBar()->showMessage(mode == QLatin1String("translate") ? tr("Translate: drag the layer anywhere to move it.")
                             : mode == QLatin1String("rotate") ? tr("Rotate: drag anywhere to turn the layer about its pivot.")
                             : mode == QLatin1String("skew") ? tr("Skew: drag to the right or the left to lean the layer over.")
                                                              : tr("Scale: drag away from the pivot to make the layer bigger, towards it smaller."), 6000);
}
