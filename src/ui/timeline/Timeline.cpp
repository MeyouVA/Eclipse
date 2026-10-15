// Eclipse - the Timeline: its rows, where things are in it, and following the scene.
#include "ui/timeline/Timeline.h"

#include <QScrollBar>
#include <QSlider>

#include "audio/sound/Sound.h"
#include "model/document/Commands.h"

Timeline::Timeline(Document* document, QWidget* parent) : QAbstractScrollArea(parent), m_document(document)
{
    setFocusPolicy(Qt::ClickFocus);
    viewport()->setAutoFillBackground(false);
    setAcceptDrops(true);       // templates, from the Library
    viewport()->setAcceptDrops(true);
    // Beside the scroll bar: how wide the frames are drawn.
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    m_zoom = new QSlider(Qt::Horizontal, this);
    m_zoom->setObjectName(QStringLiteral("TimelineZoom"));
    m_zoom->setRange(4, 30);
    m_zoom->setValue(m_cellWidth);
    m_zoom->setFixedWidth(90);
    m_zoom->setToolTip(tr("Zoom: how wide the frames are"));
    addScrollBarWidget(m_zoom, Qt::AlignLeft);
    connect(m_zoom, &QSlider::valueChanged, this, &Timeline::setCellWidth);
    connect(m_document, &Document::changed, this, &Timeline::onDocumentChanged);
    rebuildRows();
    updateScrollBars();
}

bool Timeline::controlled(int layer) const
{
    Scene const& scene = m_document->scene();
    if (layer < 0 || layer >= scene.layers.size())
        return false;
    int const id = scene.layers[layer].id;
    for (Scene::Controller const& c : scene.controllers)
        if (c.layers.contains(id))
            return true;
    if (!scene.layers[layer].drivers.isEmpty())
        return true;
    for (Layer const& other : scene.layers)
        for (Driver const& d : other.drivers)
            if (d.source == id)
                return true;
    return false;
}

void Timeline::rebuildRows()
{
    Scene const& scene = m_document->scene();
    m_rows.clear();
    m_params.clear();
    for (int l = int(scene.layers.size()) - 1; l >= 0; --l) {
        if (scene.foldedAway(l))
            continue;
        // A view of some of the layers only (the current one is always among them).
        if (l != m_layer && ((m_viewMode == ShowTagged && !scene.layers[l].tagged) || (m_viewMode == ShowSelected && !m_shownIds.contains(scene.layers[l].id))
                             || (m_viewMode == ShowControlled && !controlled(l))))
            continue;
        if (l != m_layer && m_hiddenKinds.contains(int(scene.layers[l].kind)))
            continue;       // a kind of layer that is not shown
        m_rows.push_back(l);
        m_params.push_back(-1);
        // The row of each pose controller this layer is the first of.
        for (int c = 0; c < scene.controllers.size(); ++c) {
            int first = -1;
            for (int id : scene.controllers[c].layers)
                if ((first = scene.layerIndex(id)) >= 0)
                    break;
            if (first == l) {
                m_rows.push_back(l);
                m_params.push_back(kControllerRows + c);
            }
        }
        if (!parametersShown(l))
            continue;
        for (int which = 0; which < ParameterCount; ++which) {
            if (!parameterFor(scene.layers[l].kind, Parameter(which)))
                continue;
            m_rows.push_back(l);
            m_params.push_back(which);
        }
        // And the amount of each of its effects.
        for (int e = 0; e < scene.layers[l].effects.size(); ++e) {
            m_rows.push_back(l);
            m_params.push_back(ParameterCount + e);
        }
        // And the angle of each bone of the chain the drawings bend along.
        for (int b = 0; b + 1 < scene.layers[l].bones.size(); ++b) {
            m_rows.push_back(l);
            m_params.push_back(kBoneRows + b);
        }
        // And how far each point of its deformer is moved, across and down.
        if (scene.layers[l].hasWarp())
            for (int p = 0; p < scene.layers[l].warpPoints(); ++p)
                for (int side = 0; side < 2; ++side) {
                    m_rows.push_back(l);
                    m_params.push_back(kWarpRows + 2 * p + side);
                }
    }
}

void Timeline::setViewMode(ViewMode mode)
{
    m_shownIds.clear();
    if (mode == ShowSelected)
        for (int l : selectedLayers())
            m_shownIds.push_back(m_document->scene().layers[l].id);
    m_viewMode = mode;
    m_extentRow = m_extentFrame = -1;
    m_extraLayers.clear();
    rebuildRows();
    updateScrollBars();
    emit selectionChanged();
    viewport()->update();
}

void Timeline::setKindShown(int kind, bool shown)
{
    if (shown == kindShown(kind))
        return;
    if (shown)
        m_hiddenKinds.removeAll(kind);
    else
        m_hiddenKinds.push_back(kind);
    // A block is counted in rows: it does not outlast them.
    m_extentRow = m_extentFrame = -1;
    m_extraLayers.clear();
    rebuildRows();
    updateScrollBars();
    emit selectionChanged();
    viewport()->update();
}

QVector<int> Timeline::rows() const
{
    QVector<int> layers;
    for (int row = 0; row < m_rows.size(); ++row)
        if (m_params[row] < 0)
            layers.push_back(m_rows[row]);
    return layers;
}

int Timeline::rowOfLayer(int layer) const
{
    return int(m_rows.indexOf(layer));
}

int Timeline::layerOfRow(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows[row] : -1;
}

int Timeline::rowAt(int y) const
{
    if (y < kRulerHeight)
        return -1;
    int const row = (y - kRulerHeight + verticalScrollBar()->value()) / kRowHeight;
    return row < m_rows.size() ? row : -1;
}

int Timeline::frameAt(int x) const
{
    return (x - kHeaderWidth + horizontalScrollBar()->value()) / m_cellWidth;
}

QRect Timeline::cellRect(int layer, int frame) const
{
    int const row = rowOfLayer(layer);
    if (row < 0)
        return QRect();
    return QRect(kHeaderWidth + frame * m_cellWidth - horizontalScrollBar()->value(),
                 kRulerHeight + row * kRowHeight - verticalScrollBar()->value(), m_cellWidth, kRowHeight);
}

QRect Timeline::switchRect(int layer, Switch which) const
{
    int const row = rowOfLayer(layer);
    if (row < 0)
        return QRect();
    return QRect(3 + int(which) * kSwitchWidth, kRulerHeight + row * kRowHeight - verticalScrollBar()->value(), kSwitchWidth, kRowHeight);
}

QRect Timeline::foldRect(int layer) const
{
    int const row = rowOfLayer(layer);
    Scene const& scene = m_document->scene();
    if (row < 0 || !scene.hasChildren(layer))
        return QRect();
    int const indent = std::min(scene.depth(layer), 6) * 10;
    return QRect(kSwitches + indent, kRulerHeight + row * kRowHeight - verticalScrollBar()->value(), 12, kRowHeight);
}

QRect Timeline::nameRect(int layer) const
{
    int const row = rowOfLayer(layer);
    if (row < 0)
        return QRect();
    return QRect(kSwitches, kRulerHeight + row * kRowHeight - verticalScrollBar()->value(), kHeaderWidth - kParamWidth - kSwitches, kRowHeight);
}

QRect Timeline::syncMarkRect(int layer) const
{
    Scene const& scene = m_document->scene();
    QRect const name = nameRect(layer);
    if (name.isNull() || layer < 0 || layer >= scene.layers.size() || (scene.layers[layer].syncGroup == 0 && scene.layers[layer].cloneGroup == 0))
        return QRect();
    return QRect(name.right() - 47, name.top() + (kRowHeight - 8) / 2, 14, 8);
}

QRect Timeline::headerButtonRect(HeaderButton which) const
{
    int const nameEnd = kHeaderWidth - kParamWidth;
    return QRect(nameEnd - 56 + int(which) * 18, 3, 16, 16);
}

QRect Timeline::sceneEndRect() const
{
    int const x = kHeaderWidth + m_document->scene().frameCount() * m_cellWidth - horizontalScrollBar()->value();
    return QRect(x - 4, 0, 9, kRulerHeight);
}

QRect Timeline::markerRect(int marker) const
{
    Scene const& scene = m_document->scene();
    if (marker < 0 || marker >= scene.markers.size())
        return QRect();
    Scene::Marker const& m = scene.markers[marker];
    if (marker == m_dragMarker)     // shown where it is being dragged to
        return QRect(kHeaderWidth + m_markerFrame * m_cellWidth - horizontalScrollBar()->value(), 0, m.length * m_cellWidth, 5);
    return QRect(kHeaderWidth + m.frame * m_cellWidth - horizontalScrollBar()->value(), 0, m.length * m_cellWidth, 5);
}

void Timeline::setCellWidth(int width)
{
    width = std::clamp(width, 4, 30);
    if (width == m_cellWidth)
        return;
    // The current frame stays where it is on the screen.
    int const before = m_frame * m_cellWidth - horizontalScrollBar()->value();
    m_cellWidth = width;
    updateScrollBars();
    horizontalScrollBar()->setValue(m_frame * m_cellWidth - before);
    if (m_zoom->value() != width) {
        QSignalBlocker const block(m_zoom);
        m_zoom->setValue(width);
    }
    viewport()->update();
}

void Timeline::setPlayRange(int start, int stop)
{
    if (start == m_playStart && stop == m_playStop)
        return;
    m_playStart = start;
    m_playStop = stop;
    viewport()->update();
}

void Timeline::setOnion(bool on, int previous, int next)
{
    m_onionOn = on;
    m_onionPrev = std::max(0, previous);
    m_onionNext = std::max(0, next);
    viewport()->update();
}

QRect Timeline::onionHandleRect(bool before) const
{
    int const sx = horizontalScrollBar()->value();
    if (before)
        return QRect(kHeaderWidth + (m_frame - m_onionPrev) * m_cellWidth - sx - 3, kRulerHeight - 8, 7, 8);
    return QRect(kHeaderWidth + (m_frame + m_onionNext + 1) * m_cellWidth - sx - 4, kRulerHeight - 8, 7, 8);
}

void Timeline::setShowWaveforms(bool on)
{
    m_waveforms = on;
    viewport()->update();
}

void Timeline::setCurrentCell(int layer, int frame)
{
    Scene const& scene = m_document->scene();
    layer = std::clamp(layer, 0, int(scene.layers.size()) - 1);
    frame = std::clamp(frame, 0, scene.frameCount() - 1);
    if (layer == m_layer && frame == m_frame)
        return;
    bool const other = layer != m_layer;
    m_layer = layer;
    m_frame = frame;
    if (other && m_viewMode != ShowAll) {
        rebuildRows();      // the current layer has a row whatever is shown
        updateScrollBars();
    }
    // A block is counted from the current cell: with another current cell it is gone.
    if (m_extentRow >= 0) {
        m_extentRow = m_extentFrame = -1;
        emit selectionChanged();
    }

    // Keep the current frame in view.
    int const x = frame * m_cellWidth;
    int const visible = viewport()->width() - kHeaderWidth;
    QScrollBar* const h = horizontalScrollBar();
    if (x < h->value())
        h->setValue(x);
    else if (x + m_cellWidth > h->value() + visible)
        h->setValue(x + m_cellWidth - visible);

    viewport()->update();
    emit currentCellChanged(m_layer, m_frame);
}

void Timeline::onDocumentChanged()
{
    closeEntry(false);
    rebuildRows();
    updateScrollBars();
    Scene const& scene = m_document->scene();
    if (m_extentRow >= 0 && (m_extentRow >= m_rows.size() || m_extentFrame >= scene.frameCount() || rowOfLayer(m_layer) < 0))
        m_extentRow = m_extentFrame = -1;
    m_extraLayers.erase(std::remove_if(m_extraLayers.begin(), m_extraLayers.end(), [&](int l) { return l >= scene.layers.size(); }),
                        m_extraLayers.end());
    // The scene may have shrunk under the selection (undo, delete layer, shorter scene).
    int const layer = std::clamp(m_layer, 0, int(scene.layers.size()) - 1);
    int const frame = std::clamp(m_frame, 0, scene.frameCount() - 1);
    if (layer != m_layer || frame != m_frame) {
        m_layer = layer;
        m_frame = frame;
        emit currentCellChanged(m_layer, m_frame);
    }
    viewport()->update();
}

void Timeline::updateScrollBars()
{
    Scene const& scene = m_document->scene();
    // The ruler runs on a little after the scene ends, so that its end can be dragged out.
    int const contentWidth = (scene.frameCount() + kPastEnd) * m_cellWidth;
    int const contentHeight = int(m_rows.size()) * kRowHeight;
    int const viewWidth = viewport()->width() - kHeaderWidth;
    int const viewHeight = viewport()->height() - kRulerHeight;
    horizontalScrollBar()->setRange(0, std::max(0, contentWidth - viewWidth));
    horizontalScrollBar()->setPageStep(std::max(1, viewWidth));
    horizontalScrollBar()->setSingleStep(m_cellWidth);
    verticalScrollBar()->setRange(0, std::max(0, contentHeight - viewHeight));
    verticalScrollBar()->setPageStep(std::max(1, viewHeight));
    verticalScrollBar()->setSingleStep(kRowHeight);
}

void Timeline::resizeEvent(QResizeEvent*)
{
    updateScrollBars();
}
