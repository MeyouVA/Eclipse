// Eclipse - the Camera view: drawing guides - the one in use shown, its points dragged, and the
// brush and pencil kept to the ways it runs.
#include "ui/canvas/Canvas.h"

#include <cmath>

#include <QPainter>

#include "model/document/Commands.h"

void Canvas::setActiveGuide(int index)
{
    if (m_activeGuide == index)
        return;
    m_activeGuide = index;
    update();
}

Scene::Guide const* Canvas::drawingGuide() const
{
    QVector<Scene::Guide> const& guides = m_drag == DragGuide ? m_guideEdit : m_document->scene().guides;
    if (!m_showGuides || m_playing || m_activeGuide < 0 || m_activeGuide >= guides.size() || guides[m_activeGuide].kind == Scene::Guide::Line)
        return nullptr;
    return &guides[m_activeGuide];
}

bool Canvas::guideTool() const
{
    return m_tool == Brush || m_tool == Pencil || m_tool == Line || m_tool == Rectangle || m_tool == Ellipse || m_tool == Polyline || m_tool == Eraser;
}

// Guides are laid out on the picture as the camera's frame has it.
QPointF Canvas::toFrame(QPointF widgetPos) const
{
    return cameraViewTransform().inverted().map(widgetPos);
}

int Canvas::guideHandleAt(QPointF widgetPos) const
{
    Scene::Guide const* const guide = drawingGuide();
    if (!guide || !guideTool())
        return -1;
    QTransform const view = cameraViewTransform();
    for (int i = 0; i < guide->points.size(); ++i)
        if (QLineF(view.map(guide->points[i]), widgetPos).length() <= 8.0)
            return i;
    return -1;
}

void Canvas::beginGuidedStroke(QPointF widgetPos)
{
    m_guideStart = toFrame(widgetPos);
    m_guideWay = QPointF();
}

QPointF Canvas::guided(QPointF widgetPos)
{
    Scene::Guide const* const guide = drawingGuide();
    if (!guide)
        return widgetPos;
    QPointF const at = toFrame(widgetPos);
    if (m_guideWay.isNull()) {
        // Which way the line is to run is settled once the pen has gone a little way.
        QTransform const view = cameraViewTransform();
        if (QLineF(view.map(m_guideStart), widgetPos).length() < 6.0)
            return view.map(m_guideStart);
        QVector<QPointF> const ways = Scene::guideDirections(*guide, m_guideStart);
        int const which = Scene::nearestGuideDirection(ways, at - m_guideStart);
        if (which < 0)
            return widgetPos;
        m_guideWay = ways[which];
    }
    return cameraViewTransform().map(Scene::alongGuide(m_guideStart, at, m_guideWay));
}

void Canvas::paintGuide(QPainter& painter)
{
    Scene::Guide const* const guide = drawingGuide();
    if (!guide)
        return;
    painter.save();
    painter.setTransform(cameraViewTransform());
    painter.setRenderHint(QPainter::Antialiasing, true);
    double const reach = 100000.0;
    QPen line(QColor(40, 40, 44, 200));
    line.setCosmetic(true);
    painter.setPen(line);
    // What the guide itself is: a ruler, or the horizon its vanishing points lie on.
    if (guide->kind == Scene::Guide::Ruler && guide->points.size() >= 2) {
        QPointF const way = guide->points[1] - guide->points[0];
        painter.drawLine(guide->points[0] - way * 1000.0, guide->points[1] + way * 1000.0);
    } else if (guide->kind == Scene::Guide::OnePoint && !guide->points.isEmpty()) {
        painter.drawLine(QPointF(-reach, guide->points[0].y()), QPointF(reach, guide->points[0].y()));
    } else if ((guide->kind == Scene::Guide::TwoPoint || guide->kind == Scene::Guide::ThreePoint) && guide->points.size() >= 2) {
        QPointF const way = guide->points[1] - guide->points[0];
        painter.drawLine(guide->points[0] - way * 1000.0, guide->points[1] + way * 1000.0);
    }
    // Through the pointer, the ways a line may run from there.
    if (m_hoverKnown && guideTool() && m_drag != DragGuide) {
        QPointF const from = m_drag == DragDraw ? m_guideStart : m_hoverFrame;
        QPen dotted(QColor(60, 60, 64, 170));
        dotted.setCosmetic(true);
        dotted.setStyle(Qt::DotLine);
        painter.setPen(dotted);
        for (QPointF const& way : Scene::guideDirections(*guide, from))
            painter.drawLine(from - way * reach, from + way * reach);
    }
    // Its points, to take hold of.
    painter.resetTransform();
    QTransform const view = cameraViewTransform();
    painter.setPen(QPen(QColor(20, 20, 20), 1.0));
    painter.setBrush(QColor(60, 200, 230));
    for (QPointF const& point : guide->points)
        painter.drawEllipse(view.map(point), 4.5, 4.5);
    painter.restore();
}

QPointF Canvas::guidePointInView(int point) const
{
    Scene::Guide const* const guide = drawingGuide();
    return guide && point >= 0 && point < guide->points.size() ? cameraViewTransform().map(guide->points[point]) : QPointF();
}
