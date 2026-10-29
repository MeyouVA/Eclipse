// Eclipse - the Camera view: the label of the drawing in hand, the deformation mesh, and tips sized through the camera.
#include "canvas/Canvas.h"
#include "actions/theme/InterfaceColours.h"
#include "model/document/Commands.h"
#include "model/geometry/Deform.h"

#include <QPainter>

#include <cmath>

void Canvas::paintDrawingLabel(QPainter& painter)
{
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size())
        return;
    Layer const& layer = scene.layers[m_layer];
    int const index = layer.holdsDrawings() ? layer.drawingAt(m_frame) : Layer::kNoDrawing;
    QString const text = index == Layer::kNoDrawing ? tr("%1 (no drawing)").arg(layer.name) : tr("%1: %2").arg(layer.name, layer.drawingLabel(index));
    painter.save();
    painter.resetTransform();
    QFont font = painter.font();
    font.setBold(true);
    painter.setFont(font);
    QRectF const box(8, height() - painter.fontMetrics().height() - 12, painter.fontMetrics().horizontalAdvance(text) + 12, painter.fontMetrics().height() + 6);
    painter.fillRect(box, QColor(0, 0, 0, 120));
    painter.setPen(InterfaceColours::get("cameraDrawingLabel"));
    painter.drawText(box, Qt::AlignCenter, text);
    painter.restore();
}

QVector<QPolygonF> Canvas::deformationMesh(int l, int frame) const
{
    Scene const& scene = m_document->scene();
    if (l < 0 || l >= scene.layers.size() || !scene.layers[l].holdsDrawings())
        return {};
    Layer const& layer = scene.layers[l];
    int const index = layer.drawingAt(frame);
    if (index == Layer::kNoDrawing || index >= layer.drawings.size())
        return {};
    QRectF const box = layer.drawings[index].bounds().adjusted(-8, -8, 8, 8);
    if (box.isEmpty())
        return {};
    // A lattice over the drawing, as lines of points; the deformers move its points as they move the drawing's.
    constexpr int cells = 12, steps = 24;
    Drawing lattice;
    for (int line = 0; line <= cells; ++line)
        for (bool across : {true, false}) {
            Stroke s;
            s.width = 1.0f;
            for (int i = 0; i <= steps; ++i) {
                double const t = double(line) / cells, u = double(i) / steps;
                QPointF const at = across ? QPointF(box.left() + u * box.width(), box.top() + t * box.height())
                                          : QPointF(box.left() + t * box.width(), box.top() + u * box.height());
                s.points.push_back(StrokePoint{at, 1.0f});
            }
            lattice.strokes.push_back(s);
        }
    Pose const pose = scene.poseOf(l, frame);
    Drawing const bent = Deform::ridden(scene, l, frame, Deform::shown(lattice, layer, pose, -1));
    if (bent.strokes.size() != lattice.strokes.size())
        return {};
    double moved = 0.0;
    QVector<QPolygonF> out;
    for (int i = 0; i < bent.strokes.size(); ++i) {
        QPolygonF line;
        for (int p = 0; p < bent.strokes[i].points.size(); ++p) {
            line << bent.strokes[i].points[p].pos;
            if (p < lattice.strokes[i].points.size())
                moved = std::max(moved, QLineF(bent.strokes[i].points[p].pos, lattice.strokes[i].points[p].pos).length());
        }
        out.push_back(line);
    }
    return moved > 1e-3 ? out : QVector<QPolygonF>();
}

void Canvas::paintDeformationMesh(QPainter& painter)
{
    QVector<QPolygonF> const mesh = deformationMesh(m_layer, m_frame);
    if (mesh.isEmpty())
        return;
    painter.save();
    painter.setTransform(layerViewTransform());
    QPen pen(InterfaceColours::get("cameraWireframe"), 1.0);
    pen.setCosmetic(true);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    for (QPolygonF const& line : mesh)
        painter.drawPolyline(line);
    painter.restore();
}

void Canvas::setTipToCamera(int tool, bool on)
{
    if (tool == Pencil)
        m_pencilTipToCamera = on;
    else if (tool == Brush)
        m_brushTipToCamera = on;
    update();
}

double Canvas::tipWidth() const
{
    if (!tipToCamera(m_tool) || m_drawingView)
        return m_brushWidth;
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size())
        return m_brushWidth;
    // How big the layer is seen through the camera, without the view's own zoom.
    double const seen = std::sqrt(std::abs((worldOf(m_layer, m_frame) * scene.cameraTransform(m_frame)).determinant()));
    return seen > 1e-6 ? m_brushWidth / seen : m_brushWidth;
}
