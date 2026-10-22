// Eclipse - the Node View: made, groups entered and left, where nodes and cables lie, the navigator, zoom.
#include "ui/views/NodeView.h"
#include "ui/window/InterfaceColours.h"

#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include <cmath>

#include "model/document/Commands.h"
#include "render/effects/Effects.h"

NodeView::NodeView(Document* document, QWidget* parent) : QWidget(parent), m_document(document)
{
    setFocusPolicy(Qt::ClickFocus);
    setMouseTracking(false);
    setAcceptDrops(true);       // from the Node Library
    connect(m_document, &Document::changed, this, [this] {
        m_cable = -1;
        // (The graph changed: a cable picked by its place may be another now; a group gone into may be gone.)
        m_graphCable = -1;
        NodeGraph const& g = m_document->scene().graph;
        if (m_graphGroup && !g.node(m_graphGroup))
            m_graphGroup = 0;
        if (m_graphNode && !g.node(m_graphNode))
            m_graphNode = 0;
        update();
    });
    m_search = new QLineEdit(this);
    m_search->setObjectName(QStringLiteral("NodeSearch"));
    m_search->setPlaceholderText(tr("Search"));
    m_search->setClearButtonEnabled(true);
    m_search->setToolTip(tr("Type part of a layer's name and press Enter: its node is brought into view. Enter again goes to the next."));
    connect(m_search, &QLineEdit::returnPressed, this, [this] { find(m_search->text()); });
}

void NodeView::resizeEvent(QResizeEvent*)
{
    int const tall = m_search->sizeHint().height();
    m_search->setGeometry(6, height() - tall - 6, std::min(200, width() - 12), tall);
}

int NodeView::find(QString const& text)
{
    Scene const& scene = m_document->scene();
    int const n = int(scene.layers.size());
    QString const wanted = text.trimmed();
    if (wanted.isEmpty() || n == 0)
        return -1;
    for (int k = 1; k <= n; ++k) {
        int const i = ((m_layer < 0 ? -1 : m_layer) + k) % n;
        if (!scene.layers[i].name.contains(wanted, Qt::CaseInsensitive))
            continue;
        m_layer = i;
        // Its node in the middle of the view.
        QPointF const middle = positions()[i] + QPointF(kNodeWidth / 2.0, kNodeHeight / 2.0);
        m_pan = QPointF(width() / 2.0, height() / 2.0) - middle * m_zoom;
        m_centred = true;
        emit layerPicked(i);
        update();
        return i;
    }
    return -1;
}

static char const* const kNodeMime = "application/x-eclipse-node";

void NodeView::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasFormat(QLatin1String(kNodeMime)))
        event->acceptProposedAction();
}

void NodeView::dragMoveEvent(QDragMoveEvent* event)
{
    if (event->mimeData()->hasFormat(QLatin1String(kNodeMime)))
        event->acceptProposedAction();
}

void NodeView::dropEvent(QDropEvent* event)
{
    if (!event->mimeData()->hasFormat(QLatin1String(kNodeMime)))
        return;
    event->acceptProposedAction();
    // (Its node with its middle under the pointer.)
    emit nodeDropped(QString::fromUtf8(event->mimeData()->data(QLatin1String(kNodeMime))),
                     toGraph(event->position()) - QPointF(kNodeWidth / 2.0, kNodeHeight / 2.0));
}

void NodeView::setCurrentCell(int layer, int frame)
{
    m_frame = frame;
    if (layer != m_layer) {
        m_layer = layer;
        // A layer chosen elsewhere that is not on show: the view goes to the level it is on.
        Scene const& scene = m_document->scene();
        if (layer >= 0 && layer < scene.layers.size() && layer != enteredGroup() && !nodeShown(layer)) {
            int group = -1;
            for (int up = scene.parentIndex(layer), guard = 0; up >= 0 && guard < 256 && group < 0; up = scene.parentIndex(up), ++guard)
                if (scene.layers[up].kind == Layer::Group)
                    group = up;
            m_groupId = group >= 0 ? scene.layers[group].id : 0;
            m_centred = false;
        }
        update();
    }
}

int NodeView::enteredGroup() const
{
    Scene const& scene = m_document->scene();
    int const at = m_groupId != 0 ? scene.layerIndex(m_groupId) : -1;
    return at >= 0 && scene.layers[at].kind == Layer::Group ? at : -1;
}

bool NodeView::nodeShown(int layer) const
{
    Scene const& scene = m_document->scene();
    if (layer < 0 || layer >= scene.layers.size())
        return false;
    // The nearest group it is in is the one the view is inside (or it is in none, and the view is at the top).
    int group = -1;
    for (int up = scene.parentIndex(layer), guard = 0; up >= 0 && guard < 256 && group < 0; up = scene.parentIndex(up), ++guard)
        if (scene.layers[up].kind == Layer::Group)
            group = up;
    return group == enteredGroup();
}

void NodeView::enterGroup(int layer)
{
    Scene const& scene = m_document->scene();
    if (layer < 0 || layer >= scene.layers.size() || scene.layers[layer].kind != Layer::Group)
        return;
    m_groupId = scene.layers[layer].id;
    m_cable = -1;
    onActionResetView();
}

void NodeView::leaveGroup()
{
    Scene const& scene = m_document->scene();
    int const inside = enteredGroup();
    if (inside < 0) {
        m_groupId = 0;
        return;
    }
    int group = -1;
    for (int up = scene.parentIndex(inside), guard = 0; up >= 0 && guard < 256 && group < 0; up = scene.parentIndex(up), ++guard)
        if (scene.layers[up].kind == Layer::Group)
            group = up;
    m_groupId = group >= 0 ? scene.layers[group].id : 0;
    m_cable = -1;
    onActionResetView();
}

void NodeView::onActionEnterGroup()
{
    enterGroup(m_layer);
}

QString NodeView::pathText() const
{
    Scene const& scene = m_document->scene();
    QStringList names;
    for (int at = enteredGroup(), guard = 0; at >= 0 && guard < 256; at = scene.parentIndex(at), ++guard)
        if (scene.layers[at].kind == Layer::Group)
            names.prepend(scene.layers[at].name);
    return names.isEmpty() ? QString() : tr("Top") + QStringLiteral("  /  ") + names.join(QStringLiteral("  /  "));
}

QVector<int> NodeView::groupInputs() const
{
    Scene const& scene = m_document->scene();
    int const inside = enteredGroup();
    QVector<int> out;
    for (int l = 0; inside >= 0 && l < scene.layers.size(); ++l)
        if (scene.parentIndex(l) == inside && nodeShown(l))
            out.push_back(l);
    return out;
}

QRectF NodeView::groupInputRect() const
{
    if (enteredGroup() < 0)
        return QRectF();
    QVector<QPointF> const p = positions();
    double top = 0.0;
    bool any = false;
    for (int l = 0; l < p.size(); ++l)
        if (nodeShown(l)) {
            top = any ? std::min(top, p[l].y()) : p[l].y();
            any = true;
        }
    return QRectF(toWidget(QPointF(compositePos(p).x(), top - 84.0)), QSizeF(kNodeWidth * m_zoom, kNodeHeight * m_zoom));
}

QRectF NodeView::groupOutputRect() const
{
    if (enteredGroup() < 0)
        return QRectF();
    QRectF const composite(toWidget(compositePos(positions())), QSizeF(kNodeWidth * m_zoom, kNodeHeight * m_zoom));
    return QRectF(composite.left(), composite.bottom() + 40.0 * m_zoom, composite.width(), composite.height());
}

QRectF NodeView::pathRect() const
{
    QString const text = pathText();
    return text.isEmpty() ? QRectF() : QRectF(6.0, 6.0, fontMetrics().horizontalAdvance(text) + 34.0, 22.0);
}

QVector<QPointF> NodeView::positions() const
{
    Scene const& scene = m_document->scene();
    int const n = int(scene.layers.size());
    QVector<QPointF> out(n);
    QVector<bool> done(n, false);
    QVector<QVector<int>> children(n);
    for (int l = 0; l < n; ++l) {
        int const parent = scene.parentIndex(l);
        if (parent >= 0)
            children[parent].push_back(l);
    }
    int slot = 0;
    auto place = [&](auto&& self, int l, int guard) -> void {
        if (done[l] || guard > 256)
            return;
        done[l] = true;
        if (children[l].isEmpty()) {
            out[l] = QPointF(slot++ * (kNodeWidth + 36.0), 0.0);
            return;
        }
        double left = 1e300, right = -1e300, top = 1e300;
        for (int c : children[l]) {
            self(self, c, guard + 1);
            left = std::min(left, out[c].x());
            right = std::max(right, out[c].x());
            top = std::min(top, out[c].y());
        }
        out[l] = QPointF((left + right) / 2.0, top - 84.0);
    };
    for (int l = n - 1; l >= 0; --l)
        if (scene.parentIndex(l) < 0)
            place(place, l, 0);
    for (int l = n - 1; l >= 0; --l)
        place(place, l, 0);  // anything a damaged file left in a loop
    for (int l = 0; l < n; ++l)
        if (scene.layers[l].nodePlaced)
            out[l] = scene.layers[l].nodePos;
    return out;
}

QPointF NodeView::compositePos(QVector<QPointF> const& positions) const
{
    Scene const& scene = m_document->scene();
    double sum = 0.0, lowest = 0.0;
    int count = 0;
    for (int l = 0; l < scene.layers.size(); ++l) {
        if (!scene.layers[l].holdsDrawings())
            continue;
        sum += positions[l].x();
        // Room for the effects that hang under a drawing.
        double const bottom = positions[l].y() + 40.0 * scene.layers[l].effects.size();
        lowest = count == 0 ? bottom : std::max(lowest, bottom);
        ++count;
    }
    return count ? QPointF(sum / count, lowest + 96.0) : QPointF(0.0, 96.0);
}

QRectF NodeView::nodeRect(int layer) const
{
    QVector<QPointF> const p = positions();
    if (layer < 0 || layer >= p.size())
        return QRectF();
    return QRectF(toWidget(p[layer]), QSizeF(kNodeWidth * m_zoom, kNodeHeight * m_zoom));
}

QPointF NodeView::outPort(int layer) const
{
    QRectF const r = nodeRect(layer);
    return QPointF(r.center().x(), r.bottom());
}

void NodeView::setNavigatorShown(bool on)
{
    m_navigator = on;
    update();
}

void NodeView::setStraightCables(bool on)
{
    m_straight = on;
    update();
}

QVector<QPointF> NodeView::cablePoints(QPointF from, QPointF to) const
{
    if (m_straight) {
        QVector<QPointF> out;
        for (int i = 0; i <= 24; ++i)
            out.push_back(from + (to - from) * (i / 24.0));
        return out;
    }
    // An S: down out of the port, across, down into the other.
    double const reach = std::max(24.0, std::abs(to.y() - from.y()) / 2.0);
    QPainterPath path(from);
    path.cubicTo(from + QPointF(0, reach), to - QPointF(0, reach), to);
    QVector<QPointF> out;
    for (int i = 0; i <= 24; ++i)
        out.push_back(path.pointAtPercent(i / 24.0));
    return out;
}

QPointF NodeView::cableMiddle(int child) const
{
    int const parent = m_document->scene().parentIndex(child);
    if (parent < 0)
        return QPointF();
    QRectF const r = nodeRect(child);
    return cablePoints(outPort(parent), QPointF(r.center().x(), r.top()))[12];
}

int NodeView::nodeAt(QPointF widgetPos) const
{
    QVector<QPointF> const p = positions();
    for (int l = int(p.size()) - 1; l >= 0; --l)
        if (nodeShown(l) && QRectF(toWidget(p[l]), QSizeF(kNodeWidth * m_zoom, kNodeHeight * m_zoom)).contains(widgetPos))
            return l;
    return -1;
}

int NodeView::cableAt(QPointF widgetPos) const
{
    Scene const& scene = m_document->scene();
    for (int l = 0; l < scene.layers.size(); ++l) {
        int const parent = scene.parentIndex(l);
        if (parent < 0 || !nodeShown(l) || !nodeShown(parent))
            continue;
        QRectF const r = nodeRect(l);
        for (QPointF const& q : cablePoints(outPort(parent), QPointF(r.center().x(), r.top())))
            if (std::hypot(q.x() - widgetPos.x(), q.y() - widgetPos.y()) <= 6.0)
                return l;
    }
    return -1;
}

QRectF NodeView::graphBounds() const
{
    QVector<QPointF> const p = positions();
    QRectF all(compositePos(p), QSizeF(kNodeWidth, kNodeHeight + 70.0));
    for (int l = 0; l < p.size(); ++l)
        if (nodeShown(l))
            all = all.united(QRectF(p[l], QSizeF(kNodeWidth, kNodeHeight)));
    if (enteredGroup() >= 0)
        all = all.united(QRectF(QPointF(compositePos(p).x(), all.top() - 84.0), QSizeF(kNodeWidth, kNodeHeight)));
    return all;
}

QRectF NodeView::navigatorRect() const
{
    return QRectF(width() - 136.0, 6.0, 130.0, 84.0);
}

QPointF NodeView::navigatorPoint(QPointF graph) const
{
    // The whole graph fitted into the map, with a little room round it.
    QRectF const map = navigatorRect().adjusted(5, 5, -5, -5), all = graphBounds();
    double const k = std::min(map.width() / std::max(all.width(), 1.0), map.height() / std::max(all.height(), 1.0));
    return map.center() + (graph - all.center()) * k;
}

void NodeView::onActionResetView()
{
    if (graphMode()) {
        resetGraphView();
        return;
    }
    // The whole graph, centred, at its natural size (smaller when it does not fit).
    QRectF const all = graphBounds();
    m_zoom = std::clamp(std::min((width() - 40.0) / all.width(), (height() - 40.0) / all.height()), 0.25, 1.0);
    m_pan = QPointF(width() / 2.0, height() / 2.0) - all.center() * m_zoom;
    update();
}

void NodeView::zoomBy(double factor)
{
    QPointF const middle(width() / 2.0, height() / 2.0);
    QPointF const graph = toGraph(middle);
    m_zoom = std::clamp(m_zoom * factor, 0.2, 3.0);
    m_pan = middle - graph * m_zoom;
    update();
}

void NodeView::onActionResetPan()
{
    m_pan = QPointF(width() / 2.0, height() / 2.0) - graphBounds().center() * m_zoom;
    update();
}
