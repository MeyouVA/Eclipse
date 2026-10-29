// Eclipse - the main window: Select in Camera (a node of the Node View given a layer picked in the Camera view) and pictures dropped on the Camera view.
#include "ui/window/MainWindow.h"
#include "canvas/Canvas.h"
#include "model/document/Commands.h"
#include "ui/views/NodeView.h"

#include <QCursor>
#include <QLineEdit>
#include <QSet>
#include <QStatusBar>

void MainWindow::onActionSelectInCamera()
{
    if (!m_nodeView || !m_canvas)
        return;
    Scene const& scene = m_document->scene();
    m_cameraPickGraph = m_nodeView->graphMode();
    if (m_cameraPickGraph) {
        GraphNode const* const n = scene.graph.node(m_nodeView->pickedGraphNode());
        if (!n || (n->kind != GraphNode::Composite && n->kind != GraphNode::EffectNode && n->kind != GraphNode::Display && n->kind != GraphNode::Group)) {
            statusBar()->showMessage(tr("Pick a composite, effect, group or display node first: the layer picked in the Camera view is cabled into it."), 6000);
            return;
        }
        m_cameraPickTarget = n->id;
    } else {
        int const target = layer();
        if (target < 0 || target >= scene.layers.size()) {
            statusBar()->showMessage(tr("Pick a node first: the layer picked in the Camera view is attached to it."), 6000);
            return;
        }
        m_cameraPickTarget = scene.layers[target].id;
    }
    if (m_cameraPickPrompt)
        m_cameraPickPrompt->deleteLater();
    QLineEdit* const prompt = new QLineEdit(m_nodeView);
    prompt->setObjectName(QStringLiteral("SelectInCameraPrompt"));
    prompt->setReadOnly(true);
    prompt->setPlaceholderText(tr("Select in Camera..."));
    prompt->setToolTip(tr("Click a layer's artwork in the Camera view; Escape to stop."));
    prompt->adjustSize();
    QPoint const at = m_nodeView->mapFromGlobal(QCursor::pos());
    prompt->move(m_nodeView->rect().contains(at) ? at : QPoint(12, 12));
    prompt->show();
    m_cameraPickPrompt = prompt;
    statusBar()->showMessage(tr("Select in Camera: click the layer in the Camera view."), 8000);
}

void MainWindow::finishSelectInCamera(int picked)
{
    if (m_cameraPickPrompt)
        m_cameraPickPrompt->deleteLater();
    m_cameraPickPrompt = nullptr;
    int const target = m_cameraPickTarget;
    m_cameraPickTarget = 0;
    Scene const& scene = m_document->scene();
    if (picked < 0 || picked >= scene.layers.size() || !target)
        return;
    if (!m_cameraPickGraph) {
        int const into = scene.layerIndex(target);
        if (into < 0 || into == picked || scene.isAncestor(picked, into) || !m_document->setParent(picked, into)) {
            statusBar()->showMessage(tr("%1 cannot be attached there.").arg(scene.layers[picked].name), 5000);
            return;
        }
        statusBar()->showMessage(tr("%1 is attached to %2.").arg(m_document->scene().layers[picked].name, m_document->scene().layers[into].name), 5000);
        return;
    }
    // Wired by hand: the layer's node (made if it has none) cabled into the node waiting, on a new input of a composite.
    GraphNode const* const n = scene.graph.node(target);
    if (!n)
        return;
    int const layerId = scene.layers[picked].id;
    int from = scene.graph.layerNode(layerId);
    m_document->beginMacro(tr("Select in Camera"));
    if (!from) {
        GraphNode made;
        made.kind = GraphNode::LayerNode;
        made.layer = layerId;
        made.name = scene.layers[picked].name;
        made.group = n->group;
        from = m_nodeView->addGraphNode(made, n->at + QPointF(-160, -90));
    }
    GraphNode const* const into = m_document->scene().graph.node(target);
    int port = 0;
    if (into && into->kind == GraphNode::Composite) {
        port = -1;
        for (int i = 0; i < into->inputs() && port < 0; ++i)
            if (!m_document->scene().graph.into(target, i))
                port = i;
        if (port < 0) {
            port = into->inputs();
            m_nodeView->changeGraphPorts(target, false, 1);
        }
    }
    bool const joined = from && m_nodeView->connectGraph(from, 0, target, port);
    m_document->endMacro();
    m_nodeView->pickGraphNode(target);
    statusBar()->showMessage(joined ? tr("%1 is cabled into %2.").arg(m_document->scene().layers[picked].name, m_nodeView->graphPortName(*m_document->scene().graph.node(target), port, true))
                                    : tr("%1 cannot be cabled there.").arg(m_document->scene().layers[picked].name), 5000);
}

int MainWindow::dropPicturesOnCamera(QStringList const& files, QPointF scenePos)
{
    int made = 0;
    m_document->beginMacro(tr("Camera View: Drop"));
    for (QString const& file : files) {
        QSet<int> before;
        for (Layer const& l : m_document->scene().layers)
            before.insert(l.id);
        ImageImport options;
        options.files = {file};
        options.fit = false;
        QString error;
        if (importImages(options, &error) < 0) {
            if (!error.isEmpty())
                statusBar()->showMessage(error, 6000);
            continue;
        }
        Scene const& scene = m_document->scene();
        for (int l = 0; l < scene.layers.size(); ++l) {
            Layer const& added = scene.layers[l];
            if (before.contains(added.id) || !added.holdsDrawings())
                continue;
            int const index = added.drawingAt(frame());
            QTransform const world = scene.worldTransform(l, frame());
            if (index == Layer::kNoDrawing || index >= added.drawings.size() || !world.isInvertible())
                continue;
            // Its middle where it was let go (in the layer's own coordinates; no keyframe is made).
            QPointF const by = world.inverted().map(scenePos) - added.drawings[index].bounds().center();
            QTransform const move = QTransform::fromTranslate(by.x(), by.y());
            m_document->editLayers({l}, tr("Camera View: Drop"), [move](Layer& layer) {
                for (Drawing& d : layer.drawings) {
                    for (Fill& f : d.fills)
                        f.transform(move);
                    for (Stroke& s : d.strokes)
                        s.transform(move);
                    for (Picture& p : d.pictures)
                        p.transform(move);
                }
            });
            ++made;
        }
    }
    m_document->endMacro();
    if (made > 0)
        statusBar()->showMessage(tr("%n picture(s) dropped on the Camera view.", nullptr, made), 4000);
    return made;
}
