#include "board/ui/BoardStage.h"
#include "board/model/BoardDocument.h"
#include "canvas/Canvas.h"
#include "model/document/Commands.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

namespace {
QColor const stagePaper(238, 238, 238);
}

Scene boardPanelScene(BoardProject const& project, int shot, int panel)
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
        layer.bitmap = it->kind == BoardLayer::Kind::Bitmap;
        if (layer.bitmap)
            layer.bitmapArt = (1 << Drawing::SheetCount) - 1;
        layer.drawings = {it->drawing};
        layer.cells[0] = 0;
    }
    return scene;
}

BoardStage::BoardStage(BoardDocument* document, QWidget* parent)
    : QWidget(parent), m_board(document), m_scratch(new Document),
      m_canvas(new Canvas(m_scratch, this)), m_layers(new QComboBox(this))
{
    m_scratch->QObject::setParent(m_canvas);
    m_scratch->setUndoLevels(1);
    m_canvas->setObjectName(QStringLiteral("BoardStageCanvas"));
    m_canvas->setAcceptDrops(false);
    m_canvas->setBackdrop(stagePaper);
    m_layers->setObjectName(QStringLiteral("BoardStageLayer"));
    auto* label = new QLabel(tr("Drawing layer:"), this);
    label->setBuddy(m_layers);
    auto* header = new QHBoxLayout;
    header->addWidget(label);
    header->addWidget(m_layers);
    header->addStretch(1);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 2, 4, 4);
    layout->addLayout(header);
    layout->addWidget(m_canvas, 1);
    connect(m_layers, &QComboBox::currentIndexChanged, this, &BoardStage::chooseLayer);
    connect(m_scratch->undoStack(), &QUndoStack::indexChanged, this, &BoardStage::captureArtwork);
    connect(m_canvas, &Canvas::message, this, &BoardStage::message);
    showPanel(-1, -1, false);
}

void BoardStage::showPanel(int shot, int panel, bool resetView)
{
    QScopedValueRollback<bool> const guard(m_loading, true);
    QSignalBlocker const block(m_layers);
    int const previous = m_panel == qMakePair(shot, panel) ? m_layers->currentIndex() : 0;
    m_panel = {shot, panel};
    Scene const scene = boardPanelScene(m_board->project(), shot, panel);
    m_scratch->reset(scene);
    m_canvas->clearSelection();
    m_layers->clear();
    if (!scene.layers.isEmpty())
        for (auto const& layer : m_board->project().shots[shot].panels[panel].layers)
            m_layers->addItem(layer.name);
    m_layers->setEnabled(!scene.layers.isEmpty());
    m_canvas->setEnabled(!scene.layers.isEmpty());
    m_layers->setCurrentIndex(scene.layers.isEmpty() ? -1 : std::clamp(previous, 0, int(scene.layers.size()) - 1));
    chooseLayer(m_layers->currentIndex());
    if (resetView)
        QTimer::singleShot(0, m_canvas, [this] { fit(); });
}

void BoardStage::chooseLayer(int layer)
{
    int const mapped = layer < 0 ? -1 : int(m_scratch->scene().layers.size()) - 1 - layer;
    m_canvas->setCurrentCell(mapped, 0);
}

void BoardStage::captureArtwork()
{
    int const layer = m_layers->currentIndex();
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
    static const QHash<QString, Canvas::Tool> tools{{QStringLiteral("select"), Canvas::Select},
        {QStringLiteral("brush"), Canvas::Brush}, {QStringLiteral("pencil"), Canvas::Pencil},
        {QStringLiteral("eraser"), Canvas::Eraser}, {QStringLiteral("paint"), Canvas::Paint},
        {QStringLiteral("hand"), Canvas::Hand}, {QStringLiteral("zoom"), Canvas::Zoom}};
    auto const found = tools.constFind(tool);
    if (found != tools.constEnd()) {
        m_canvas->setTool(*found);
        if (m_canvas->isEnabled())
            m_canvas->setFocus();
    }
}

void BoardStage::fit() { m_canvas->fitToWindow(); }
