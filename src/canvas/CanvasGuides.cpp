// Eclipse - the Camera view: drawing guides - the one in use shown, its points dragged, and the
// brush, the pencil, the Line tool and the Polyline kept to the ways it runs.
#include "canvas/Canvas.h"

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

// A plain guide within a few pixels of the pointer (the last one drawn is on top).
int Canvas::lineGuideAt(QPointF widgetPos) const
{
    if (!m_showGuides || m_guidesLocked || m_playing)
        return -1;
    QVector<Scene::Guide> const& guides = m_document->scene().guides;
    QTransform const view = cameraViewTransform();
    QPointF const at = view.inverted().map(widgetPos);
    double const perPixel = 1.0 / std::max(std::sqrt(std::abs(view.determinant())), 1e-9);
    for (int i = int(guides.size()) - 1; i >= 0; --i)
        if (guides[i].kind == Scene::Guide::Line && std::abs((guides[i].vertical ? at.x() : at.y()) - guides[i].at) <= 4.0 * perPixel)
            return i;
    return -1;
}

QPointF Canvas::guideLineInView(int guide, QPointF widgetPos) const
{
    QVector<Scene::Guide> const& guides = m_document->scene().guides;
    if (guide < 0 || guide >= guides.size() || guides[guide].kind != Scene::Guide::Line)
        return QPointF();
    QTransform const view = cameraViewTransform();
    QPointF at = view.inverted().map(widgetPos);
    if (guides[guide].vertical)
        at.setX(guides[guide].at);
    else
        at.setY(guides[guide].at);
    return view.map(at);
}

void Canvas::onActionClearGuides()
{
    // The plain lines go; the guides to draw along are kept.
    QVector<Scene::Guide> left;
    for (Scene::Guide const& guide : m_document->scene().guides)
        if (guide.kind != Scene::Guide::Line)
            left.push_back(guide);
    if (left.size() != m_document->scene().guides.size())
        m_document->setGuides(left, tr("Clear Alignment Guides"));
}

void Canvas::onActionNewGuide(QString const& which)
{
    // Through the middle of the frame, to be dragged to where it is wanted.
    Scene const& scene = m_document->scene();
    bool const vertical = which == QLatin1String("vertical");
    QVector<Scene::Guide> guides = scene.guides;
    guides.push_back(Scene::Guide{vertical, vertical ? scene.width / 2.0 : scene.height / 2.0});
    m_document->setGuides(guides, tr("Add Guide"));
    setShowGuides(true);
}

bool Canvas::symmetry(QTransform* mirror) const
{
    Scene::Guide const* const guide = drawingGuide();
    Scene const& scene = m_document->scene();
    if (!guide || guide->kind != Scene::Guide::Symmetry || guide->points.size() < 2 || m_layer < 0 || m_layer >= scene.layers.size())
        return false;
    QPointF const a = guide->points[0], along = guide->points[1] - guide->points[0];
    if (std::hypot(along.x(), along.y()) < 1e-9)
        return false;
    // About the axis, on the picture: brought level, turned over, and put back.
    double const degrees = std::atan2(along.y(), along.x()) * 180.0 / 3.14159265358979323846;
    QTransform const over = QTransform::fromTranslate(-a.x(), -a.y()) * QTransform().rotate(-degrees) * QTransform::fromScale(1.0, -1.0)
                            * QTransform().rotate(degrees) * QTransform::fromTranslate(a.x(), a.y());
    // The layer's own coordinates to the picture and back.
    QTransform const toPicture = m_drawingView ? QTransform() : worldOf(m_layer, m_frame) * scene.cameraTransform(m_frame);
    if (!toPicture.isInvertible())
        return false;
    *mirror = toPicture * over * toPicture.inverted();
    return true;
}

bool Canvas::guidedShape(QPointF a, QPointF p)
{
    Scene::Guide const* const guide = drawingGuide();
    if (!guide || guide->kind == Scene::Guide::SquareGrid || guide->kind == Scene::Guide::Symmetry || guide->kind == Scene::Guide::FourPoint
        || guide->kind == Scene::Guide::FivePoint)
        return false;       // (a square grid runs as the shapes do anyway; a perspective that bends has no straight-sided shapes)
    QTransform const toView = layerViewTransform(), view = cameraViewTransform();
    if (!toView.isInvertible() || !view.isInvertible())
        return false;
    // On the picture, where the guide is laid out.
    QPointF const from = toFrame(toView.map(a)), to = toFrame(toView.map(p));
    auto ways = [&](QPointF at) {
        QVector<QPointF> found = Scene::guideDirections(*guide, at);
        if (found.size() == 1)
            found.push_back(QPointF(-found[0].y(), found[0].x()));      // a ruler: along it, and square to it
        return found;
    };
    QVector<QPointF> const here = ways(from), there = ways(to);
    if (here.size() < 2 || there.size() < 2)
        return false;
    QPointF second, fourth;
    if (QLineF(from, from + here[0]).intersects(QLineF(to, to + there[1]), &second) == QLineF::NoIntersection
        || QLineF(from, from + here[1]).intersects(QLineF(to, to + there[0]), &fourth) == QLineF::NoIntersection)
        return false;
    QPolygonF const corners({from, second, to, fourth});
    auto onLayer = [&](QPointF framePoint) { return toLayer(view.map(framePoint)); };
    m_stroke.points.clear();
    m_stroke.closed = true;
    m_stroke.curve = false;
    if (m_tool == Rectangle) {
        for (QPointF const& corner : corners)
            m_stroke.points.push_back({onLayer(corner), 1.0f});
        return true;
    }
    // The ellipse that fills a square, seen as the square is: twelve points round it.
    QTransform fit;
    if (!QTransform::squareToQuad(corners, fit))
        return false;
    m_stroke.curve = true;
    for (int i = 0; i < 12; ++i) {
        double const angle = i * 3.14159265358979323846 / 6.0;
        m_stroke.points.push_back({onLayer(fit.map(QPointF(0.5 + std::cos(angle) / 2.0, 0.5 + std::sin(angle) / 2.0))), 1.0f});
    }
    return true;
}

QVector<QTransform> Canvas::symmetries() const
{
    QVector<QTransform> found;
    Scene::Guide const* const chosen = drawingGuide();
    Scene const& scene = m_document->scene();
    if (!chosen || chosen->kind != Scene::Guide::Symmetry || m_layer < 0 || m_layer >= scene.layers.size())
        return found;
    QTransform const toPicture = m_drawingView ? QTransform() : worldOf(m_layer, m_frame) * scene.cameraTransform(m_frame);
    if (!toPicture.isInvertible())
        return found;
    QTransform const back = toPicture.inverted();
    auto same = [](QTransform const& a, QTransform const& b) {
        return std::abs(a.m11() - b.m11()) < 1e-6 && std::abs(a.m12() - b.m12()) < 1e-6 && std::abs(a.m21() - b.m21()) < 1e-6
               && std::abs(a.m22() - b.m22()) < 1e-6 && std::abs(a.dx() - b.dx()) < 1e-4 && std::abs(a.dy() - b.dy()) < 1e-4;
    };
    // On the picture: nothing done, then over each axis whatever there is so far.
    QVector<QTransform> all = {QTransform()};
    for (Scene::Guide const& guide : scene.guides) {
        if (guide.kind != Scene::Guide::Symmetry || guide.points.size() < 2)
            continue;
        QPointF const a = guide.points[0], along = guide.points[1] - guide.points[0];
        if (std::hypot(along.x(), along.y()) < 1e-9)
            continue;
        double const degrees = std::atan2(along.y(), along.x()) * 180.0 / 3.14159265358979323846;
        QTransform const over = QTransform::fromTranslate(-a.x(), -a.y()) * QTransform().rotate(-degrees) * QTransform::fromScale(1.0, -1.0)
                                * QTransform().rotate(degrees) * QTransform::fromTranslate(a.x(), a.y());
        QVector<QTransform> const sofar = all;
        for (QTransform const& one : sofar) {
            QTransform const more = one * over;
            bool known = false;
            for (QTransform const& have : std::as_const(all))
                known = known || same(have, more);
            if (!known && all.size() < 16)
                all.push_back(more);
        }
    }
    for (int i = 1; i < all.size(); ++i)
        found.push_back(toPicture * all[i] * back);
    return found;
}

void Canvas::beginGuidedStroke(QPointF widgetPos)
{
    m_guideStart = toFrame(widgetPos);
    m_guideWay = QPointF();
    m_guideBends = false;
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
        m_guideBends = Scene::guideArc(*guide, m_guideStart, which, &m_guideCentre, &m_guideRadius);
    }
    if (m_guideBends) {
        // Kept to the circle: the place on it nearest the pen.
        QPointF const out = at - m_guideCentre;
        double const length = std::hypot(out.x(), out.y());
        if (length > 1e-9)
            return cameraViewTransform().map(m_guideCentre + out * (m_guideRadius / length));
    }
    return cameraViewTransform().map(Scene::alongGuide(m_guideStart, at, m_guideWay));
}

QPointF Canvas::guidedFrom(QPointF fromWidget, QPointF widgetPos) const
{
    Scene::Guide const* const guide = drawingGuide();
    if (!guide || QLineF(fromWidget, widgetPos).length() < 3.0)
        return widgetPos;
    // A straight line may still change its mind: the way is chosen afresh as its end moves.
    QPointF const from = toFrame(fromWidget), at = toFrame(widgetPos);
    QVector<QPointF> const ways = Scene::guideDirections(*guide, from);
    int const which = Scene::nearestGuideDirection(ways, at - from);
    return which < 0 ? widgetPos : cameraViewTransform().map(Scene::alongGuide(from, at, ways[which]));
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
    if (guide->kind == Scene::Guide::Symmetry && guide->points.size() >= 2) {
        // The axis: what is drawn on one side of it appears on the other.
        QPen axis(QColor(230, 120, 200, 220));
        axis.setCosmetic(true);
        axis.setStyle(Qt::DashLine);
        painter.setPen(axis);
        QPointF const way = guide->points[1] - guide->points[0];
        painter.drawLine(guide->points[0] - way * 1000.0, guide->points[1] + way * 1000.0);
        // The other axes of symmetry of the scene work with it: they show too, fainter.
        axis.setColor(QColor(230, 120, 200, 110));
        painter.setPen(axis);
        for (Scene::Guide const& other : m_document->scene().guides) {
            if (&other == guide || other.kind != Scene::Guide::Symmetry || other.points.size() < 2)
                continue;
            QPointF const otherWay = other.points[1] - other.points[0];
            painter.drawLine(other.points[0] - otherWay * 1000.0, other.points[1] + otherWay * 1000.0);
        }
        painter.setPen(line);
    } else if (guide->kind == Scene::Guide::Ruler && guide->points.size() >= 2) {
        QPointF const way = guide->points[1] - guide->points[0];
        painter.drawLine(guide->points[0] - way * 1000.0, guide->points[1] + way * 1000.0);
    } else if (guide->kind == Scene::Guide::OnePoint && !guide->points.isEmpty()) {
        painter.drawLine(QPointF(-reach, guide->points[0].y()), QPointF(reach, guide->points[0].y()));
    } else if ((guide->kind == Scene::Guide::TwoPoint || guide->kind == Scene::Guide::ThreePoint) && guide->points.size() >= 2) {
        QPointF const way = guide->points[1] - guide->points[0];
        painter.drawLine(guide->points[0] - way * 1000.0, guide->points[1] + way * 1000.0);
    } else if (guide->kind == Scene::Guide::FourPoint && guide->points.size() >= 2) {
        // The horizon, and its four vanishing points.
        QPointF const way = guide->points[1] - guide->points[0];
        painter.drawLine(guide->points[0] - way * 1000.0, guide->points[1] + way * 1000.0);
    } else if (guide->kind == Scene::Guide::FivePoint && guide->points.size() >= 2) {
        // The circle the picture is seen in, and the cross through its middle.
        QPointF const out = guide->points[1] - guide->points[0], turned(-out.y(), out.x());
        double const radius = std::hypot(out.x(), out.y());
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(guide->points[0], radius, radius);
        painter.drawLine(guide->points[0] - out, guide->points[0] + out);
        painter.drawLine(guide->points[0] - turned, guide->points[0] + turned);
    }
    // Through the pointer, the ways a line may run from there.
    if (m_hoverKnown && guideTool() && m_drag != DragGuide) {
        // (From where the line being drawn began, when there is one.)
        QPointF const from = m_drag == DragDraw || m_drag == DragShape ? m_guideStart
                             : m_polyActive && !m_stroke.points.isEmpty() ? toFrame(layerViewTransform().map(m_stroke.points.last().pos)) : m_hoverFrame;
        QPen dotted(QColor(60, 60, 64, 170));
        dotted.setCosmetic(true);
        dotted.setStyle(Qt::DotLine);
        painter.setPen(dotted);
        QVector<QPointF> const ways = Scene::guideDirections(*guide, from);
        painter.setBrush(Qt::NoBrush);
        for (int i = 0; i < ways.size(); ++i) {
            QPointF centre;
            double radius = 0.0;
            if (Scene::guideArc(*guide, from, i, &centre, &radius))
                painter.drawEllipse(centre, radius, radius);        // a way that bends: the circle it is part of
            else
                painter.drawLine(from - ways[i] * reach, from + ways[i] * reach);
        }
    }
    // Its points, to take hold of.
    painter.resetTransform();
    QTransform const view = cameraViewTransform();
    painter.setPen(QPen(QColor(20, 20, 20), 1.0));
    painter.setBrush(QColor(60, 200, 230));
    for (QPointF const& point : guide->points)
        painter.drawEllipse(view.map(point), 4.5, 4.5);
    // The vanishing points that are worked out from those: smaller, and not to be taken hold of.
    painter.setBrush(QColor(230, 230, 235));
    for (QPointF const& point : Scene::guideVanishingPoints(*guide))
        if (!guide->points.contains(point))
            painter.drawEllipse(view.map(point), 3.0, 3.0);
    painter.restore();
}

QPointF Canvas::guidePointInView(int point) const
{
    Scene::Guide const* const guide = drawingGuide();
    return guide && point >= 0 && point < guide->points.size() ? cameraViewTransform().map(guide->points[point]) : QPointF();
}
