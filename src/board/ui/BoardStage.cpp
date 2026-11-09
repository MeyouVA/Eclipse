#include "board/ui/BoardStage.h"
#include "board/ui/BoardCameraCanvas.h"
#include "board/ui/BoardPreview.h"
#include "board/model/BoardDocument.h"
#include "model/document/Commands.h"

#include <QApplication>
#include <QScopedValueRollback>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPainter>
#include <QToolBar>
#include <QStackedLayout>
#include <QTimer>
#include <QVBoxLayout>

namespace {
QColor const stagePaper(238, 238, 238);
}

Scene boardPanelScene(BoardProject const& project, int shot, int panel, BoardCameraPose const& camera)
{
    Scene scene;
    scene.width = project.width;
    scene.height = project.height;
    scene.fps = project.fps;
    scene.background = stagePaper;
    scene.setFrameCount(1);
    scene.layers.clear();
    if (shot < 0 || shot >= project.shots.size() || panel < 0 || panel >= project.shots[shot].panels.size())
        return scene;
    auto const& layers = project.shots[shot].panels[panel].layers;
    for (auto it = layers.crbegin(); it != layers.crend(); ++it) {
        int const index = scene.addLayer(it->name);
        Layer& layer = scene.layers[index];
        layer.visible = it->visible;
        layer.locked = it->locked;
        layer.bitmap = it->kind == BoardLayer::Kind::Bitmap;
        if (layer.bitmap)
            layer.bitmapArt = (1 << Drawing::SheetCount) - 1;
        layer.drawings = {it->drawing};
        layer.cells[0] = 0;
    }
    if (camera != BoardCameraPose{}) {
        int const index = scene.addLayer(QStringLiteral("Camera"), -1, Layer::Camera);
        Layer& lens = scene.layers[index];
        lens.pivot = QPointF(scene.width / 2.0, scene.height / 2.0);
        lens.setKey(0, boardCameraLayerPose(camera));
    }
    return scene;
}

BoardStage::BoardStage(BoardDocument* document, QWidget* parent)
    : QWidget(parent), m_board(document), m_scratch(new Document),
      m_canvas(new BoardCameraCanvas(m_scratch, this)), m_preview(new BoardPreview(this)), m_stack(new QStackedLayout),
      m_viewBar(new QToolBar(this)), m_zoomLabel(new QLabel(this)), m_layerLabel(new QLabel(this)),
      m_toolLabel(new QLabel(tr("Brush"), this))
{
    m_scratch->QObject::setParent(m_canvas);
    m_scratch->setUndoLevels(1);
    m_canvas->setObjectName(QStringLiteral("BoardStageCanvas"));
    m_canvas->setAcceptDrops(false);
    m_canvas->setBackdrop(stagePaper);
    auto* title = new QLabel(tr("Stage View"), this);
    title->setObjectName(QStringLiteral("BoardStageTitle"));
    title->setAlignment(Qt::AlignCenter);
    title->setFixedSize(120, 22);
    auto* heading = new QHBoxLayout;
    heading->addStretch();
    heading->addWidget(title);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);
    layout->addLayout(heading);
    layout->addLayout(m_stack, 1);
    m_viewBar->setIconSize(QSize(16, 16));
    m_viewBar->setObjectName(QStringLiteral("BoardStageFooter"));
    layout->addWidget(m_viewBar);
    m_stack->addWidget(m_canvas);
    m_stack->addWidget(m_preview);
    connect(qApp, &QApplication::focusChanged, this, [this] { update(); });
    connect(m_scratch->undoStack(), &QUndoStack::indexChanged, this, &BoardStage::captureArtwork);
    connect(m_canvas, &Canvas::message, this, &BoardStage::message);
    connect(m_canvas, &Canvas::zoomChanged, this, [this] {
        m_zoomLabel->setText(tr("%1%").arg(qRound(m_canvas->zoomPercent())));
    });
    connect(m_canvas, &BoardCameraCanvas::cameraEdited, this, [this](BoardCameraPose const& pose) {
        m_board->setCameraKey(m_panel.first, m_panel.second, m_frame, pose);
    });
    showPanel(-1, -1, -1, false);
}

void BoardStage::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    QWidget const* focused = QApplication::focusWidget();
    painter.setPen(focused && (focused == this || isAncestorOf(focused)) ? QColor(190, 61, 55) : palette().mid().color());
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
}

void BoardStage::setViewActions(QList<QAction*> const& actions)
{
    for (auto* action : actions)
        if (action)
            m_viewBar->addAction(action);
    m_zoomLabel->setContentsMargins(8, 0, 8, 0);
    m_zoomLabel->setText(tr("%1%").arg(qRound(m_canvas->zoomPercent())));
    m_viewBar->addWidget(m_zoomLabel);
    m_layerLabel->setContentsMargins(8, 0, 8, 0);
    m_viewBar->addWidget(m_layerLabel);
    auto* space = new QWidget(m_viewBar);
    space->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_viewBar->addWidget(space);
    m_toolLabel->setContentsMargins(8, 0, 12, 0);
    m_viewBar->addWidget(m_toolLabel);
}

void BoardStage::showPanel(int shot, int panel, int layer, bool resetView)
{
    QScopedValueRollback<bool> const guard(m_loading, true);
    m_panel = {shot, panel};
    Scene const scene = boardPanelScene(m_board->project(), shot, panel);
    m_scratch->reset(scene);
    m_canvas->clearSelection();
    m_canvas->setEnabled(m_board->panel(shot, panel) != nullptr);
    chooseLayer(layer);
    setCameraFrame(0);
    if (resetView)
        QTimer::singleShot(0, m_canvas, [this] { fit(); });
}

void BoardStage::chooseLayer(int layer)
{
    m_layer = layer;
    int const mapped = layer < 0 ? -1 : int(m_scratch->scene().layers.size()) - 1 - layer;
    m_canvas->setCurrentCell(mapped, 0);
    m_layerLabel->setText(mapped < 0 ? QString() : m_scratch->scene().layers[mapped].name);
}

void BoardStage::captureArtwork()
{
    int const layer = m_layer;
    if (m_loading || layer < 0 || m_panel.first < 0 || !m_scratch->undoStack()->canUndo())
        return;
    auto const& layers = m_scratch->scene().layers;
    int const index = int(layers.size()) - 1 - layer;
    if (index < 0 || index >= layers.size())
        return;
    auto const& drawnLayer = layers[index];
    int const drawing = drawnLayer.drawingAt(0);
    QScopedValueRollback<bool> const guard(m_writing, true);
    if (!m_board->setLayerDrawing(m_panel.first, m_panel.second, layer,
            drawing == Layer::kNoDrawing ? Drawing{} : drawnLayer.drawings[drawing],
            m_scratch->undoStack()->undoText()))
        emit message(tr("This artwork could not be stored in the panel."));
}

void BoardStage::chooseTool(QString const& tool)
{
    bool const camera = tool == QLatin1String("camera");
    if (camera)
        m_canvas->setTool(Canvas::Hand);
    m_canvas->setCameraTool(camera);
    if (camera) {
        m_canvas->setFocus();
        return;
    }
    static const QHash<QString, Canvas::Tool> tools{{QStringLiteral("select"), Canvas::Select},
        {QStringLiteral("brush"), Canvas::Brush}, {QStringLiteral("pencil"), Canvas::Pencil},
        {QStringLiteral("eraser"), Canvas::Eraser}, {QStringLiteral("paint"), Canvas::Paint},
        {QStringLiteral("hand"), Canvas::Hand}, {QStringLiteral("zoom"), Canvas::Zoom},
        {QStringLiteral("contour"), Canvas::ContourEditor},
        {QStringLiteral("pencileditor"), Canvas::PencilEditor},
        {QStringLiteral("pencilretouch"), Canvas::PencilRetouch},
        {QStringLiteral("cutter"), Canvas::Cutter},
        {QStringLiteral("perspective"), Canvas::Perspective},
        {QStringLiteral("stamp"), Canvas::Stamp},
        {QStringLiteral("text"), Canvas::Text},
        {QStringLiteral("rectangle"), Canvas::Rectangle},
        {QStringLiteral("ellipse"), Canvas::Ellipse},
        {QStringLiteral("line"), Canvas::Line},
        {QStringLiteral("polyline"), Canvas::Polyline},
        {QStringLiteral("paintunpainted"), Canvas::PaintUnpainted},
        {QStringLiteral("unpaint"), Canvas::Unpaint},
        {QStringLiteral("closegap"), Canvas::CloseGap},
        {QStringLiteral("gradient"), Canvas::EditGradient},
        {QStringLiteral("dropper"), Canvas::Dropper},
        {QStringLiteral("rotateview"), Canvas::RotateView}};
    auto const found = tools.constFind(tool);
    if (found != tools.constEnd()) {
        m_canvas->setTool(*found);
        if (m_canvas->isEnabled())
            m_canvas->setFocus();
    }
}

void BoardStage::fit() { m_canvas->fitToWindow(); }
void BoardStage::zoom(bool in) { m_canvas->zoomBy(in ? 1.25 : 0.8); }
void BoardStage::setToolLabel(QString const& text) { m_toolLabel->setText(text); }

QWidget* BoardStage::createToolProperties(QWidget* parent)
{
    auto* panel = new QWidget(parent);
    auto* form = new QFormLayout(panel);
    auto* width = new QDoubleSpinBox(panel);
    width->setRange(0.1, 512.0);
    width->setDecimals(1);
    width->setValue(m_canvas->brushWidth());
    form->addRow(tr("Stroke width"), width);
    connect(width, &QDoubleSpinBox::valueChanged, m_canvas, [this](double value) { m_canvas->setBrushWidth(value); });
    return panel;
}

void BoardStage::showPreview(QPair<int, int> panel, int frame, QPair<int, int> previous, double mix)
{
    m_preview->showFrame(m_board->project(), panel, frame, previous, mix);
    m_stack->setCurrentWidget(m_preview);
}

void BoardStage::hidePreview()
{
    m_stack->setCurrentWidget(m_canvas);
    m_preview->clear();
}

void BoardStage::setCameraFrame(int frame)
{
    m_frame = frame;
    auto const* panel = m_board->panel(m_panel.first, m_panel.second);
    auto const& project = m_board->project();
    m_canvas->setCameraFrame(panel ? QSize(project.width, project.height) : QSize(),
                             panel ? BoardCamera::poseAt(panel->camera, frame) : BoardCameraPose{},
                             panel && !panel->camera.isEmpty(), frame);
}

void BoardStage::showCameraGuide(bool show) { m_canvas->setCameraGuide(show); }
bool BoardStage::cameraTool() const { return m_canvas->cameraTool(); }
