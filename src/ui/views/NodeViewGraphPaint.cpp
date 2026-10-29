// Eclipse - the Node View's graph: a node's settings edited, and the graph painted.
#include "ui/views/NodeView.h"
#include "actions/theme/InterfaceColours.h"

#include <QCheckBox>
#include "ui/panels/ColourEditor.h"
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <functional>

#include "model/document/Commands.h"
#include "ui/panels/EffectsPanel.h"

namespace {

// Which effects take a colour, and which can be turned inside out.
bool takesColour(Effect::Type t)
{
    return t == Effect::Tint || t == Effect::Glow || t == Effect::Shadow || t == Effect::ColourScale || t == Effect::Tone || t == Effect::Highlight
           || t == Effect::FieldChart
           || t == Effect::MatteGenerator || t == Effect::Gradient;
}

bool turnsInsideOut(Effect::Type t)
{
    return GraphNode::takesMatte(t) || t == Effect::Gradient || t == Effect::MatteResize;
}

}  // namespace

void NodeView::editGraphNode(int id)
{
    NodeGraph g = m_document->scene().graph;
    int const i = g.nodeIndex(id);
    if (i < 0 || g.nodes[i].kind != GraphNode::EffectNode)
        return;
    Effect e = g.nodes[i].effect;
    QDialog dialog(this);
    dialog.setWindowTitle(graphNodeName(g.nodes[i]));
    QFormLayout* const form = new QFormLayout(&dialog);
    QCheckBox* const on = new QCheckBox(&dialog);
    on->setChecked(e.enabled);
    form->addRow(tr("On"), on);
    QDoubleSpinBox* const amount = new QDoubleSpinBox(&dialog);
    amount->setObjectName(QStringLiteral("GraphEffectAmount"));
    amount->setRange(0.0, 1000.0);
    amount->setDecimals(2);
    amount->setValue(g.nodes[i].amountAt(m_frame));
    form->addRow(tr("Amount"), amount);
    // Animated: the amount a keyframe of this frame (the frames between go straight from one keyframe to the next).
    QCheckBox* const keyed = new QCheckBox(tr("Keyframe on frame %1").arg(m_frame + 1), &dialog);
    keyed->setObjectName(QStringLiteral("GraphEffectKeyed"));
    keyed->setChecked(!g.nodes[i].amountKeys.isEmpty());
    form->addRow(QString(), keyed);
    QPushButton* colour = nullptr;
    if (takesColour(e.type)) {
        colour = new QPushButton(e.color.name(), &dialog);
        connect(colour, &QPushButton::clicked, &dialog, [&] {
            QColor const picked = ColourEditor::getColour(e.color, &dialog, tr("Colour"));
            if (picked.isValid()) {
                e.color = picked;
                colour->setText(picked.name());
            }
        });
        form->addRow(tr("Colour"), colour);
    }
    QCheckBox* invert = nullptr;
    if (turnsInsideOut(e.type)) {
        invert = new QCheckBox(&dialog);
        invert->setChecked(e.invert);
        form->addRow(GraphNode::takesMatte(e.type) ? tr("Outside the matte") : tr("Inverted"), invert);
    }
    QComboBox* channel = nullptr;
    if (e.type == Effect::ChannelSelector) {
        channel = new QComboBox(&dialog);
        channel->addItems({tr("Red"), tr("Green"), tr("Blue"), tr("Opacity")});
        channel->setCurrentIndex(std::clamp(e.channel, 0, 3));
        form->addRow(tr("Channel"), channel);
    }
    QPlainTextEdit* text = nullptr;
    if (e.type == Effect::Script || e.type == Effect::External) {
        text = new QPlainTextEdit(&dialog);
        text->setObjectName(QStringLiteral("GraphEffectText"));
        text->setLineWrapMode(QPlainTextEdit::NoWrap);
        text->setPlainText(e.text);
        text->setMinimumSize(360, 120);
        form->addRow(e.type == Effect::Script ? tr("Script") : tr("Command Line"), text);
    }
    QComboBox* peg = nullptr;
    if (e.type == Effect::ApplyPeg) {
        // The layer whose motion moves the picture.
        Scene const& scene = m_document->scene();
        peg = new QComboBox(&dialog);
        peg->setObjectName(QStringLiteral("GraphEffectPeg"));
        peg->addItem(tr("(none)"), 0);
        for (int l = int(scene.layers.size()) - 1; l >= 0; --l)
            peg->addItem(scene.layers[l].name, scene.layers[l].id);
        peg->setCurrentIndex(std::max(0, peg->findData(e.matte)));
        form->addRow(tr("Peg"), peg);
    }
    QComboBox* camera = nullptr;
    QCheckBox* viewpoint = nullptr;
    QCheckBox* flatten = nullptr;
    if (e.type == Effect::ApplyCamera) {
        Scene const& scene = m_document->scene();
        camera = new QComboBox(&dialog);
        camera->setObjectName(QStringLiteral("GraphEffectCamera"));
        camera->addItem(tr("(none)"), 0);
        for (int l : scene.cameras())
            camera->addItem(scene.layers[l].name, scene.layers[l].id);
        camera->setCurrentIndex(std::max(0, camera->findData(e.matte)));
        form->addRow(tr("Camera"), camera);
        viewpoint = new QCheckBox(tr("Apply Camera Viewpoint"), &dialog);
        viewpoint->setObjectName(QStringLiteral("GraphEffectViewpoint"));
        viewpoint->setToolTip(tr("Seen through the camera. Off, the picture moves with the camera instead, as with a peg."));
        viewpoint->setChecked(e.cameraViewpoint);
        form->addRow(viewpoint);
        flatten = new QCheckBox(tr("Flatten Depth Buffer"), &dialog);
        flatten->setObjectName(QStringLiteral("GraphEffectFlatten"));
        flatten->setToolTip(tr("What comes in is seen as one flat picture. Off, each of its layers is seen from the camera in its own depth."));
        flatten->setChecked(e.flattenDepth);
        form->addRow(flatten);
    }
    QCheckBox* truck = nullptr;
    if (Effect::trucks(e.type)) {
        truck = new QCheckBox(tr("Truck Factor"), &dialog);
        truck->setObjectName(QStringLiteral("GraphEffectTruckFactor"));
        truck->setToolTip(tr("The blur grows as the layer that comes in nears the camera and shrinks as it goes further away."));
        truck->setChecked(e.truckFactor);
        form->addRow(truck);
    }
    QComboBox* mode = nullptr;
    if (e.type == Effect::Blending) {
        mode = new QComboBox(&dialog);
        mode->setObjectName(QStringLiteral("GraphEffectBlend"));
        mode->addItems({tr("Normal"), tr("Multiply"), tr("Screen"), tr("Overlay"), tr("Darken"), tr("Lighten"), tr("Colour Dodge"), tr("Colour Burn"),
                        tr("Hard Light"), tr("Soft Light"), tr("Difference"), tr("Exclusion"), tr("Add")});
        mode->setCurrentIndex(std::clamp(e.blendMode, 0, int(Layer::BlendCount) - 1));
        form->addRow(tr("Over the second input"), mode);
    }
    // Colour Curves: the four curves, each to drag.
    QVector<QPointF> curves[4];
    for (int c = 0; c < 4; ++c)
        curves[c] = e.curves[c];
    if (e.type == Effect::ColourCurves) {
        QComboBox* const which = new QComboBox(&dialog);
        which->addItems({tr("RGB"), tr("Red"), tr("Green"), tr("Blue")});
        form->addRow(tr("Curve"), which);
        ColourCurveEdit* const curve = new ColourCurveEdit(&dialog);
        curve->setPoints(curves[0]);
        form->addRow(curve);
        connect(which, &QComboBox::currentIndexChanged, curve, [curve, &curves](int c) { curve->setPoints(curves[std::clamp(c, 0, 3)]); });
        connect(curve, &ColourCurveEdit::edited, which, [which, &curves](QVector<QPointF> const& points) {
            bool const straight = points.size() == 2 && points[0] == QPointF(0, 0) && points[1] == QPointF(1, 1);
            curves[std::clamp(which->currentIndex(), 0, 3)] = straight ? QVector<QPointF>() : points;
        });
    }
    QDoubleSpinBox* area[4] = {};
    if (e.type == Effect::Crop) {
        double const now[4] = {e.area.x(), e.area.y(), e.area.width(), e.area.height()};
        QString const names[4] = {tr("Left"), tr("Top"), tr("Width"), tr("Height")};
        for (int k = 0; k < 4; ++k) {
            area[k] = new QDoubleSpinBox(&dialog);
            area[k]->setRange(k < 2 ? -10000.0 : 0.0, 10000.0);
            area[k]->setValue(now[k]);
            form->addRow(names[k], area[k]);
        }
    }
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    if (channel)
        e.channel = channel->currentIndex();
    if (mode)
        e.blendMode = mode->currentIndex();
    if (peg)
        e.matte = peg->currentData().toInt();
    if (camera)
        e.matte = camera->currentData().toInt();
    if (viewpoint)
        e.cameraViewpoint = viewpoint->isChecked();
    if (flatten)
        e.flattenDepth = flatten->isChecked();
    if (truck)
        e.truckFactor = truck->isChecked();
    if (text)
        e.text = text->toPlainText();
    if (area[0])
        e.area = QRectF(area[0]->value(), area[1]->value(), area[2]->value(), area[3]->value());
    for (int c = 0; c < 4; ++c)
        e.curves[c] = curves[c];
    e.enabled = on->isChecked();
    if (keyed->isChecked()) {
        g.nodes[i].amountKeys.insert(m_frame, amount->value());
    } else {
        g.nodes[i].amountKeys.clear();
        e.amount = amount->value();
    }
    if (invert)
        e.invert = invert->isChecked();
    g.nodes[i].effect = e;
    m_document->setGraph(g, tr("Edit Effect Node"));
}

QString NodeView::graphPortName(GraphNode const& n, int port, bool input) const
{
    if (!input)
        return n.kind == GraphNode::GroupIn ? tr("Into the group %1").arg(port + 1) : n.outputs() > 1 ? tr("Output %1").arg(port + 1) : tr("Output image");
    if (n.kind == GraphNode::EffectNode) {
        if (port == 0)
            return tr("Input image");
        Effect::Type const t = n.effect.type;
        return t == Effect::Blending || t == Effect::Glue ? tr("Under") : tr("Matte");
    }
    if (n.kind == GraphNode::Display)
        return tr("Image shown");
    return tr("Input %1").arg(port + 1);
}

void NodeView::paintGraphMode(QPainter& p)
{
    Scene const& scene = m_document->scene();
    NodeGraph const& g = scene.graph;
    QColor const imageCable(120, 200, 140), picked(255, 220, 90);
    auto cable = [&](QPointF from, QPointF to, QColor const& color, double width) {
        QVector<QPointF> const pts = cablePoints(from, to);
        p.setPen(QPen(color, width));
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(pts.constData(), int(pts.size()));
    };
    // A node being dragged is drawn where the pointer has it, cables and all.
    QPointF const dragBy = m_drag == DragGraphNode && m_moved ? nodeDragDelta(m_dragNow) * m_zoom : QPointF();
    auto shift = [&](int id) { return id == m_graphNode || m_graphPicked.contains(id) ? dragBy : QPointF(); };
    // ---- cables ----
    for (int c = 0; c < g.cables.size(); ++c) {
        GraphCable const& k = g.cables[c];
        if (c == m_unplugged || graphNodeRect(k.from).isNull() || graphNodeRect(k.to).isNull())
            continue;
        cable(graphOutPort(k.from, k.fromPort) + shift(k.from), graphInPort(k.to, k.toPort) + shift(k.to), c == m_graphCable ? picked : imageCable,
              c == m_graphCable ? 2.6 : 1.8);
    }
    if (m_drag == DragWire) {
        if (m_wireReverse)
            cable(m_dragNow, graphInPort(m_wireFrom, m_wireFromPort), picked, 1.8);
        else
            cable(graphOutPort(m_wireFrom, m_wireFromPort), m_dragNow, picked, 1.8);
    }
    // ---- the parenting of the layers: from a node's right end to the left end of each node that hangs from it ----
    {
        QPen pegPen(QColor(230, 160, 60), 1.6, Qt::DashLine);
        p.setPen(pegPen);
        p.setBrush(Qt::NoBrush);
        for (GraphNode const& n : g.nodes) {
            if (n.kind != GraphNode::LayerNode || n.group != m_graphGroup || n.id == m_pegChild)
                continue;
            int const l = scene.layerIndex(n.layer);
            int const parent = l >= 0 ? scene.parentIndex(l) : -1;
            int const from = parent >= 0 ? g.layerNode(scene.layers[parent].id) : 0;
            if (!from || graphNodeRect(from).isNull())
                continue;
            QPointF const a = graphTransformOut(from) + shift(from), b = graphTransformIn(n.id) + shift(n.id);
            QPainterPath path(a);
            double const bow = std::max(30.0 * m_zoom, std::abs(b.x() - a.x()) / 2.0);
            path.cubicTo(a + QPointF(bow, 0), b - QPointF(bow, 0), b);
            p.drawPath(path);
        }
        if (m_drag == DragPeg) {
            p.drawLine(graphTransformOut(m_pegFrom), m_dragNow);
        }
    }
    // ---- nodes ----
    QFont font = this->font();
    font.setPointSizeF(std::max(5.0, font.pointSizeF() * m_zoom));
    p.setFont(font);
    for (GraphNode const& n : g.nodes) {
        if (n.group != m_graphGroup)
            continue;
        QRectF const r = graphNodeRect(n.id).translated(shift(n.id));
        if (n.kind == GraphNode::Note) {
            // A note: what it says, on a pale card.
            QRectF const card(r.topLeft(), QSizeF(std::max(r.width(), 180.0 * m_zoom), std::max(r.height(), 60.0 * m_zoom)));
            p.setPen(QPen(n.id == m_graphNode || m_graphPicked.contains(n.id) ? QColor(255, 255, 255) : QColor(120, 110, 60), 1.2));
            p.setBrush(QColor(240, 225, 140));
            p.drawRect(card);
            p.setPen(QColor(40, 36, 20));
            p.drawText(card.adjusted(6 * m_zoom, 4 * m_zoom, -6 * m_zoom, -4 * m_zoom), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, graphNodeName(n));
            continue;
        }
        int const shownLayer = n.kind == GraphNode::LayerNode ? m_document->scene().layerIndex(n.layer) : -1;
        int const layerKind = shownLayer >= 0 ? m_document->scene().layers[shownLayer].kind : -1;
        QColor const color = layerKind == Layer::Camera ? InterfaceColours::get("nodeCamera") : layerKind == Layer::Peg ? InterfaceColours::get("nodePeg")
                             : layerKind == Layer::Sound ? InterfaceColours::get("nodeSound") : n.kind == GraphNode::LayerNode ? InterfaceColours::get("nodeDrawing")
                             : n.kind == GraphNode::Composite ? InterfaceColours::get("nodeComposite") : n.kind == GraphNode::EffectNode ? InterfaceColours::get("nodeEffect")
                             : n.kind == GraphNode::Display ? InterfaceColours::get("nodeDisplay") : InterfaceColours::get("nodeGroup");
        bool const off = n.kind == GraphNode::EffectNode && !n.effect.enabled;
        bool const chosen = n.id == m_graphNode || m_graphPicked.contains(n.id);
        QColor const border = layerKind == Layer::Camera ? InterfaceColours::get("nodeCameraBorder") : QColor(20, 21, 23);
        p.setPen(QPen(chosen ? InterfaceColours::get("nodeSelected") : border, chosen ? 2.0 : 1.0));
        p.setBrush(off ? color.darker(190) : color);
        p.drawRoundedRect(r, 4 * m_zoom, 4 * m_zoom);
        p.setPen(off ? QColor(150, 150, 150) : QColor(245, 245, 245));
        QRectF const text = r.adjusted(8 * m_zoom, 0, -(n.kind == GraphNode::Group ? 18 : 6) * m_zoom, 0);
        p.drawText(text, Qt::AlignVCenter | Qt::AlignLeft, p.fontMetrics().elidedText(graphNodeName(n), Qt::ElideRight, int(text.width())));
        if (n.kind == GraphNode::Group) {
            // A group can be gone into: a small arrow at its right end says so.
            QPointF const tip(r.right() - 10 * m_zoom, r.center().y() + 4 * m_zoom);
            p.setPen(QPen(QColor(235, 235, 235), 1.4));
            p.drawLine(tip + QPointF(0, -8 * m_zoom), tip);
            p.drawLine(tip + QPointF(-3.5 * m_zoom, -3.5 * m_zoom), tip);
            p.drawLine(tip + QPointF(3.5 * m_zoom, -3.5 * m_zoom), tip);
        }
        // Ports: in along the top (filled when something comes in), out along the bottom.
        p.setPen(QPen(QColor(20, 21, 23), 1));
        for (int i = 0; i < n.inputs(); ++i) {
            p.setBrush(g.into(n.id, i) ? QColor(210, 212, 216) : QColor(90, 92, 96));
            p.drawEllipse(graphInPort(n.id, i) + shift(n.id), 3.5 * m_zoom, 3.5 * m_zoom);
        }
        p.setBrush(QColor(210, 212, 216));
        for (int o = 0; o < n.outputs() && !pictureless(n); ++o)
            p.drawEllipse(graphOutPort(n.id, o) + shift(n.id), 4.5 * m_zoom, 4.5 * m_zoom);
        if (n.kind == GraphNode::LayerNode) {
            // Its transformation ports: small squares at its two ends.
            double const s = 3.5 * m_zoom;
            p.setBrush(QColor(230, 160, 60));
            p.drawRect(QRectF(graphTransformIn(n.id) + shift(n.id) - QPointF(s, s), QSizeF(2 * s, 2 * s)));
            p.drawRect(QRectF(graphTransformOut(n.id) + shift(n.id) - QPointF(s, s), QSizeF(2 * s, 2 * s)));
        }
    }
    // The map in the corner: the nodes of this level as small blocks, and what the view shows as a frame.
    if (m_navigator && width() > 320 && height() > 200) {
        QRectF const map = navigatorRect();
        p.setFont(this->font());
        p.setPen(QPen(QColor(24, 24, 24), 1));
        p.setBrush(QColor(44, 44, 46, 230));
        p.drawRect(map);
        p.setClipRect(map.adjusted(1, 1, -1, -1));
        QRectF const all = graphLevelBounds().adjusted(-20, -20, 20, 20), inner = map.adjusted(5, 5, -5, -5);
        double const k = std::min(inner.width() / std::max(all.width(), 1.0), inner.height() / std::max(all.height(), 1.0));
        auto onMap = [&](QPointF graph) { return inner.center() + (graph - all.center()) * k; };
        p.setPen(Qt::NoPen);
        for (GraphNode const& n : g.nodes) {
            if (n.group != m_graphGroup)
                continue;
            p.setBrush(n.id == m_graphNode ? QColor(72, 190, 200) : QColor(150, 152, 158));
            p.drawRect(QRectF(onMap(n.at), QSizeF(std::max(3.0, graphNodeWidth(n) * k), std::max(2.0, kNodeHeight * k))));
        }
        p.setPen(QPen(QColor(235, 235, 235), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(onMap(toGraph(QPointF(0, 0))), onMap(toGraph(QPointF(width(), height())))));
        p.setClipping(false);
    }
    paintBoxSelection(p);
    // Inside a group: the way here along the top; a click on it goes out one level.
    if (QRectF const path = graphPathRect(); !path.isNull()) {
        QStringList names;
        for (GraphNode const* at = g.node(m_graphGroup); at; at = g.node(at->group))
            names.prepend(graphNodeName(*at));
        p.setFont(this->font());
        p.setPen(QPen(QColor(24, 24, 24), 1));
        p.setBrush(QColor(40, 118, 122));
        p.drawRoundedRect(path, 4, 4);
        p.setPen(QColor(245, 245, 245));
        p.drawText(path.adjusted(8, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("â†‘  ") + tr("Top") + QStringLiteral(" / ") + names.join(QStringLiteral(" / ")));
    }
}
