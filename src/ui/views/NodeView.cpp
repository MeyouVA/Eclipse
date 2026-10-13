#include "ui/views/NodeView.h"

#include <QContextMenuEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include <cmath>

#include "model/document/Commands.h"
#include "ui/panels/Effects.h"

NodeView::NodeView(Document* document, QWidget* parent) : QWidget(parent), m_document(document)
{
    setFocusPolicy(Qt::ClickFocus);
    setMouseTracking(false);
    connect(m_document, &Document::changed, this, [this] {
        m_cable = -1;
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

void NodeView::setCurrentCell(int layer, int)
{
    if (layer != m_layer) {
        m_layer = layer;
        update();
    }
}

// ---- layout -------------------------------------------------------------------------------

QVector<QPointF> NodeView::positions() const
{
    Scene const& scene = m_document->scene();
    int const n = int(scene.layers.size());
    QVector<QPointF> out(n);
    QVector<bool> done(n, false);
    // Layers nothing is attached to stand in a row, the front one on the left; what they
    // are attached to stands above them, over the middle of its group.
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

QVector<QPointF> NodeView::cablePoints(QPointF from, QPointF to)
{
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
        if (QRectF(toWidget(p[l]), QSizeF(kNodeWidth * m_zoom, kNodeHeight * m_zoom)).contains(widgetPos))
            return l;
    return -1;
}

int NodeView::cableAt(QPointF widgetPos) const
{
    Scene const& scene = m_document->scene();
    for (int l = 0; l < scene.layers.size(); ++l) {
        int const parent = scene.parentIndex(l);
        if (parent < 0)
            continue;
        QRectF const r = nodeRect(l);
        for (QPointF const& q : cablePoints(outPort(parent), QPointF(r.center().x(), r.top())))
            if (std::hypot(q.x() - widgetPos.x(), q.y() - widgetPos.y()) <= 6.0)
                return l;
    }
    return -1;
}

void NodeView::onActionResetView()
{
    // The whole graph, centred, at its natural size (smaller when it does not fit).
    QVector<QPointF> const p = positions();
    QRectF all(compositePos(p), QSizeF(kNodeWidth, kNodeHeight + 70.0));
    for (QPointF const& q : p)
        all = all.united(QRectF(q, QSizeF(kNodeWidth, kNodeHeight)));
    m_zoom = std::clamp(std::min((width() - 40.0) / all.width(), (height() - 40.0) / all.height()), 0.25, 1.0);
    m_pan = QPointF(width() / 2.0, height() / 2.0) - all.center() * m_zoom;
    update();
}

// ---- painting -----------------------------------------------------------------------------

void NodeView::paintEvent(QPaintEvent*)
{
    if (!m_centred && width() > 50) {
        m_centred = true;
        onActionResetView();
    }
    Scene const& scene = m_document->scene();
    QPainter p(this);
    p.fillRect(rect(), QColor(69, 69, 69));
    p.setRenderHint(QPainter::Antialiasing, true);
    QVector<QPointF> pos = positions();
    // A node being dragged is drawn where the pointer has it, cables and all.
    if (m_drag == DragNode && m_moved && m_dragLayer >= 0 && m_dragLayer < pos.size())
        pos[m_dragLayer] = m_nodeStart + (m_dragNow - m_pressPos) / m_zoom;
    auto rectOf = [&](QPointF graph) { return QRectF(toWidget(graph), QSizeF(kNodeWidth * m_zoom, kNodeHeight * m_zoom)); };
    auto cable = [&](QPointF from, QPointF to, QColor const& color, double width) {
        QVector<QPointF> const pts = cablePoints(from, to);
        p.setPen(QPen(color, width));
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(pts.constData(), int(pts.size()));
    };

    QColor const pegCable(255, 190, 70), imageCable(111, 168, 232), picked(255, 255, 255);
    QRectF const composite = rectOf(compositePos(pos));
    QRectF const display(composite.left(), composite.bottom() + 40.0 * m_zoom, composite.width(), composite.height());

    // ---- cables: drawings into the composite, the composite into the display ----
    QVector<int> drawings;
    for (int l = int(scene.layers.size()) - 1; l >= 0; --l)
        if (scene.layers[l].holdsDrawings())
            drawings.push_back(l);
    // The front drawing takes the leftmost port, as it stands leftmost by default.
    for (int i = 0; i < drawings.size(); ++i) {
        Layer const& layer = scene.layers[drawings[i]];
        QRectF const r = rectOf(pos[drawings[i]]);
        double const x = composite.left() + composite.width() * (i + 1.0) / (drawings.size() + 1.0);
        QColor c = imageCable;
        if (!layer.visible)
            c.setAlpha(70);
        // Through its effects, one under the other, and on into the composite.
        QPointF from(r.center().x(), r.bottom());
        for (int e = 0; e < layer.effects.size(); ++e) {
            QRectF const er = rectOf(pos[drawings[i]] + QPointF(0, 40.0 * (e + 1)));
            p.setPen(QPen(c, 1.6));
            p.drawLine(from, QPointF(er.center().x(), er.top()));
            // The matte of a cutter comes in from the side.
            int const matte = layer.effects[e].type == Effect::Cutter ? scene.layerIndex(layer.effects[e].matte) : -1;
            if (matte >= 0) {
                QRectF const mr = rectOf(pos[matte]);
                p.setPen(QPen(QColor(200, 120, 220), 1.4, Qt::DashLine));
                p.drawLine(QPointF(mr.center().x(), mr.bottom()), QPointF(er.left(), er.center().y()));
            }
            from = QPointF(er.center().x(), er.bottom());
        }
        cable(from, QPointF(x, composite.top()), c, 1.6);
    }
    cable(QPointF(composite.center().x(), composite.bottom()), QPointF(display.center().x(), display.top()), imageCable, 1.6);
    // ---- cables: what a layer is attached to ----
    for (int l = 0; l < scene.layers.size(); ++l) {
        int const parent = scene.parentIndex(l);
        if (parent < 0)
            continue;
        QRectF const from = rectOf(pos[parent]), to = rectOf(pos[l]);
        cable(QPointF(from.center().x(), from.bottom()), QPointF(to.center().x(), to.top()), l == m_cable ? picked : pegCable, l == m_cable ? 2.6 : 1.8);
    }
    if (m_drag == DragCable && m_dragLayer >= 0)
        cable(outPort(m_dragLayer), m_dragNow, picked, 1.8);

    // ---- nodes ----
    QFont font = this->font();
    font.setPointSizeF(std::max(5.0, font.pointSizeF() * m_zoom));
    p.setFont(font);
    auto node = [&](QRectF const& r, QString const& name, QColor const& color, bool current, bool dim) {
        p.setPen(QPen(current ? QColor(255, 255, 255) : QColor(20, 21, 23), current ? 2.0 : 1.0));
        p.setBrush(dim ? color.darker(190) : color);
        p.drawRoundedRect(r, 4 * m_zoom, 4 * m_zoom);
        p.setPen(dim ? QColor(150, 150, 150) : QColor(245, 245, 245));
        QRectF const text = r.adjusted(22 * m_zoom, 0, -6 * m_zoom, 0);
        p.drawText(text, Qt::AlignVCenter | Qt::AlignLeft, p.fontMetrics().elidedText(name, Qt::ElideRight, int(text.width())));
    };
    for (int l = 0; l < scene.layers.size(); ++l) {
        Layer const& layer = scene.layers[l];
        QRectF const r = rectOf(pos[l]);
        QColor const color = layer.kind == Layer::Peg ? QColor(150, 108, 30) : layer.kind == Layer::Camera ? QColor(60, 96, 140)
                             : layer.kind == Layer::Sound ? QColor(52, 120, 84) : QColor(72, 92, 128);
        node(r, layer.name, color, l == m_layer, !layer.visible);
        // The square on the left switches the layer on and off.
        QRectF const eye(r.left() + 6 * m_zoom, r.center().y() - 5 * m_zoom, 10 * m_zoom, 10 * m_zoom);
        p.setPen(QPen(QColor(235, 235, 235), 1));
        p.setBrush(layer.visible ? QColor(235, 235, 235) : QColor(Qt::transparent));
        p.drawRect(eye);
        // Ports: in on top, out below.
        p.setPen(QPen(QColor(20, 21, 23), 1));
        p.setBrush(QColor(210, 212, 216));
        p.drawEllipse(QPointF(r.center().x(), r.top()), 3.5 * m_zoom, 3.5 * m_zoom);
        p.drawEllipse(QPointF(r.center().x(), r.bottom()), 4.5 * m_zoom, 4.5 * m_zoom);
    }
    for (int l = 0; l < scene.layers.size(); ++l) {
        Layer const& layer = scene.layers[l];
        for (int e = 0; e < layer.effects.size(); ++e) {
            QString const text = Effects::name(layer.effects[e], scene).section(QStringLiteral("  "), 0, 0);
            node(rectOf(pos[l] + QPointF(0, 40.0 * (e + 1))), text, QColor(128, 70, 110), false, !layer.effects[e].enabled || !layer.visible);
        }
    }
    node(composite, tr("Composite"), QColor(96, 76, 128), false, false);
    node(display, tr("Display"), QColor(70, 72, 78), false, false);
}

// ---- input --------------------------------------------------------------------------------

void NodeView::mousePressEvent(QMouseEvent* event)
{
    m_pressPos = m_dragNow = event->position();
    m_moved = false;
    if (event->button() == Qt::MiddleButton) {
        m_drag = DragPan;
        return;
    }
    if (event->button() != Qt::LeftButton && event->button() != Qt::RightButton)
        return;
    Scene const& scene = m_document->scene();
    // The port under a node: pull a cable out of it.
    if (event->button() == Qt::LeftButton) {
        for (int l = int(scene.layers.size()) - 1; l >= 0; --l) {
            // The lower edge of the node and just under it - not its middle, which is
            // close by when the graph is shown small.
            QRectF const r = nodeRect(l);
            QPointF const port(r.center().x(), r.bottom());
            if (std::hypot(port.x() - m_pressPos.x(), port.y() - m_pressPos.y()) <= std::max(5.0, 7.0 * m_zoom)
                && m_pressPos.y() > r.bottom() - r.height() * 0.2) {
                m_drag = DragCable;
                m_dragLayer = l;
                return;
            }
        }
    }
    int const hit = nodeAt(m_pressPos);
    if (hit >= 0) {
        m_cable = -1;
        QRectF const r = nodeRect(hit);
        if (event->button() == Qt::LeftButton && m_pressPos.x() < r.left() + 20 * m_zoom) {
            m_document->setLayerVisible(hit, !scene.layers[hit].visible);
            return;
        }
        emit layerPicked(hit);
        if (event->button() == Qt::LeftButton) {
            m_drag = DragNode;
            m_dragLayer = hit;
            m_nodeStart = positions()[hit];
        }
        update();
        return;
    }
    m_cable = cableAt(m_pressPos);
    if (m_cable < 0 && event->button() == Qt::LeftButton)
        m_drag = DragPan;  // empty space: move the view
    update();
}

void NodeView::mouseMoveEvent(QMouseEvent* event)
{
    QPointF const now = event->position();
    if (m_drag == DragPan) {
        m_pan += now - m_dragNow;
    } else if (m_drag == DragNode) {
        m_moved = m_moved || (now - m_pressPos).manhattanLength() >= 4.0;
    }
    m_dragNow = now;
    if (m_drag != DragNone)
        update();
}

void NodeView::mouseReleaseEvent(QMouseEvent* event)
{
    Drag const drag = m_drag;
    int const layer = m_dragLayer;
    m_drag = DragNone;
    m_dragLayer = -1;
    if (drag == DragNode && m_moved) {
        m_document->setNodePos(layer, m_nodeStart + (event->position() - m_pressPos) / m_zoom);
    } else if (drag == DragCable) {
        // Dropped on a node: that layer is attached to the one the cable came from.
        int const target = nodeAt(event->position());
        if (target >= 0 && target != layer)
            m_document->setParent(target, layer);
    }
    update();
}

void NodeView::mouseDoubleClickEvent(QMouseEvent* event)
{
    int const hit = nodeAt(event->position());
    if (hit >= 0)
        emit renameRequested(hit);
}

void NodeView::wheelEvent(QWheelEvent* event)
{
    // About the pointer: the graph point under it stays under it.
    QPointF const anchor = event->position();
    QPointF const graph = toGraph(anchor);
    m_zoom = std::clamp(m_zoom * std::pow(1.0015, event->angleDelta().y()), 0.2, 3.0);
    m_pan = anchor - graph * m_zoom;
    update();
}

void NodeView::contextMenuEvent(QContextMenuEvent* event)
{
    emit contextMenuRequested(event->globalPos());
}

void NodeView::deleteSelection()
{
    if (m_cable >= 0) {
        int const child = m_cable;
        m_cable = -1;
        m_document->setParent(child, -1);
        return;
    }
    m_document->removeLayer(m_layer);
}
