// Eclipse - the Xsheet: its columns, where things are, the selection and what is shown.
#include "ui/timeline/Xsheet.h"

#include <QLineEdit>
#include <QPlainTextEdit>
#include <QScrollBar>

#include "model/document/Commands.h"
#include "ui/timeline/Timeline.h"

Xsheet::Xsheet(Document* document, QWidget* parent) : QAbstractScrollArea(parent), m_document(document)
{
    setObjectName(QStringLiteral("Xsheet"));
    setFocusPolicy(Qt::ClickFocus);
    viewport()->setAutoFillBackground(false);
    viewport()->setMouseTracking(true);
    connect(m_document, &Document::changed, this, [this] {
        if (m_extent >= m_document->scene().frameCount())
            m_extent = -1;
        rebuildColumns();
        updateScrollBars();
        viewport()->update();
        emit statusChanged();
    });
    rebuildColumns();
    updateScrollBars();
}

bool Xsheet::columnShown(Scene const& scene, int layer) const
{
    Layer const& l = scene.layers[layer];
    if (l.columnHidden || (m_selectionOnly && !m_selectionIds.contains(l.id)))
        return false;
    switch (l.kind) {
    case Layer::DrawingLayer: return m_show & ShowDrawings;
    case Layer::Annotation: return m_show & ShowAnnotations;
    case Layer::Sound: return m_show & ShowSounds;
    default: return m_show & ShowOthers;
    }
}

int Xsheet::widthOf(Layer const& layer) const
{
    if (layer.columnCollapsed)
        return kCollapsedWidth;
    return layer.columnWidth > 0 ? std::clamp(layer.columnWidth, 20, 1000) : m_columnWidth;
}

void Xsheet::rebuildColumns()
{
    Scene const& scene = m_document->scene();
    // The current column and the far one of the block are kept by what they show.
    auto key = [this](int c) { return c >= 0 && c < m_columns.size() ? std::pair(m_columns[c].layer, m_columns[c].function) : std::pair(-1, -1); };
    std::pair<int, int> const current = key(m_column), extent = key(m_extentColumn);
    m_columns.clear();
    int x = 0;
    for (int l = int(scene.layers.size()) - 1; l >= 0; --l) {
        Layer const& layer = scene.layers[l];
        bool const own = columnShown(scene, l);
        if (own) {
            m_columns.push_back({l, -1, x, widthOf(layer)});
            x += m_columns.back().width;
        }
        if (layer.columnHidden || (m_selectionOnly && !m_selectionIds.contains(layer.id)))
            continue;
        if (layer.kind == Layer::Sound || layer.kind == Layer::Group || layer.kind == Layer::ColourCard || layer.kind == Layer::Annotation)
            continue;
        // A column for each value of its pose its keyframes change (or another layer drives).
        for (int p : {int(Timeline::PosX), int(Timeline::PosY), int(Timeline::PosZ), int(Timeline::Angle), int(Timeline::ScaleX), int(Timeline::ScaleY),
                      int(Timeline::Skew), int(Timeline::Opacity), int(Timeline::Fov)}) {
            if (p == Timeline::Fov && layer.kind != Layer::Camera)
                continue;
            int const kind = p == Timeline::PosX || p == Timeline::PosY || p == Timeline::PosZ ? ShowPaths : p == Timeline::Angle ? ShowRotations : ShowFunctions;
            if (!(m_show & kind))
                continue;
            if (layer.hiddenFunctions & (1 << p))
                continue;           // (Tag > Function Columns Invisible)
            bool animated = layer.linkOf(p) != 0;
            for (Driver const& d : layer.drivers)
                animated = animated || d.value == p;
            double first = 0.0;
            bool seen = false;
            for (Keyframe const& k : layer.keys) {
                if (!k.sets(1 << p))
                    continue;
                double const v = Timeline::parameterOf(k.pose, Timeline::Parameter(p));
                if (seen && std::abs(v - first) > 1e-9)
                    animated = true;
                if (!seen)
                    first = v;
                seen = true;
            }
            if (!animated)
                continue;
            m_columns.push_back({l, p, x, layer.columnCollapsed ? int(kCollapsedWidth) : std::max(48, m_columnWidth * 3 / 4)});
            x += m_columns.back().width;
        }
    }
    m_contentWidth = x;
    auto find = [this](std::pair<int, int> k) {
        for (int c = 0; c < m_columns.size(); ++c)
            if (m_columns[c].layer == k.first && m_columns[c].function == k.second)
                return c;
        return -1;
    };
    m_column = current.first == m_layer ? find(current) : -1;
    if (m_column < 0)
        m_column = columnOfLayer(m_layer);
    m_extentColumn = extent.first >= 0 ? find(extent) : -1;
    if (m_extentColumn == m_column)
        m_extentColumn = -1;
    emit columnsChanged();
}

int Xsheet::columnOfLayer(int layer) const
{
    for (int c = 0; c < m_columns.size(); ++c)
        if (m_columns[c].layer == layer && m_columns[c].function < 0)
            return c;
    return -1;
}

int Xsheet::layerOfColumn(int column) const
{
    return column >= 0 && column < m_columns.size() ? m_columns[column].layer : -1;
}

void Xsheet::setCurrentColumn(int column)
{
    if (column < 0 || column >= m_columns.size())
        return;
    if (m_columns[column].layer != m_layer) {
        emit cellPicked(m_columns[column].layer, m_frame);       // (the window sets the cell back here)
        m_layer = m_columns[column].layer;
    }
    m_column = column;
    m_extentColumn = -1;
    viewport()->update();
    emit statusChanged();
}

int Xsheet::headerHeight() const
{
    int h = m_minimal ? 20 : 34;
    if (m_thumbnails && !m_minimal)
        h += 46;
    if (m_grouping)
        h += 16;
    return h;
}

void Xsheet::setCurrentCell(int layer, int frame)
{
    bool const sameColumn = m_column >= 0 && m_column < m_columns.size() && m_columns[m_column].layer == layer;
    if (layer == m_layer && frame == m_frame && (sameColumn || m_column < 0))
        return;
    endNote(true);
    m_layer = layer;
    m_frame = frame;
    if (!sameColumn)
        m_column = columnOfLayer(layer);
    m_extent = -1;      // the frames selected are counted from the current cell
    m_extentColumn = -1;
    keepInView();
    viewport()->update();
    emit statusChanged();
}

void Xsheet::keepInView()
{
    int const rh = rowHeight();
    int const y = m_frame * rh;
    int const viewHeight = viewport()->height() - headerHeight();
    QScrollBar* const v = verticalScrollBar();
    if (m_centreOnFrame)
        v->setValue(y + rh / 2 - viewHeight / 2);
    else if (y < v->value())
        v->setValue(y);
    else if (y + rh > v->value() + viewHeight)
        v->setValue(y + rh - viewHeight);
    if (m_column < 0 || m_column >= m_columns.size())
        return;
    int const x = m_columns[m_column].x, cw = m_columns[m_column].width;
    int const viewWidth = viewport()->width() - kRowHeaderWidth;
    QScrollBar* const h = horizontalScrollBar();
    if (x < h->value())
        h->setValue(x);
    else if (x + cw > h->value() + viewWidth)
        h->setValue(x + cw - viewWidth);
}

void Xsheet::setColumnWidth(int pixels)
{
    m_columnWidth = std::clamp(pixels, 40, 240);
    rebuildColumns();
    updateScrollBars();
    viewport()->update();
}

void Xsheet::setZoomRange(int lowest, int highest)
{
    m_zoomMin = std::clamp(lowest, 1, 30);
    m_zoomMax = std::clamp(highest, m_zoomMin, 40);
    setZoomLevel(m_zoom);
}

void Xsheet::setZoomLevel(int level)
{
    level = std::clamp(level, m_zoomMin, m_zoomMax);
    if (level == m_zoom)
        return;
    // The frame at the top stays at the top.
    int const top = verticalScrollBar()->value() / std::max(1, rowHeight());
    m_zoom = level;
    endTyping(false);
    endNote(true);
    updateScrollBars();
    verticalScrollBar()->setValue(top * rowHeight());
    viewport()->update();
}

void Xsheet::resetPan()
{
    horizontalScrollBar()->setValue(0);
    verticalScrollBar()->setValue(0);
}

void Xsheet::zoomExtents()
{
    int const frames = std::max(1, m_document->scene().frameCount());
    int const room = viewport()->height() - headerHeight();
    setZoomLevel(std::max(m_zoomMin, room / (2 * frames)));
    verticalScrollBar()->setValue(0);
}

void Xsheet::setShown(int what)
{
    if (what == m_show)
        return;
    m_show = what & ShowEverything;
    rebuildColumns();
    updateScrollBars();
    viewport()->update();
}

void Xsheet::setHeldLook(int look)
{
    m_heldLook = look == HeldValue ? HeldValue : HeldLine;
    viewport()->update();
}

void Xsheet::setLineThreshold(int frames)
{
    m_lineThreshold = std::clamp(frames, 0, 1000);
    viewport()->update();
}

void Xsheet::setFeetAndFrames(bool on)
{
    m_feet = on;
    viewport()->update();
}

QString Xsheet::frameLabel(int frame) const
{
    if (!m_feet)
        return QString::number(frame + 1);
    // 35 mm film: sixteen frames to the foot.
    int const n = frame + 1;
    return QStringLiteral("%1+%2").arg(n / 16).arg(n % 16, 2, 10, QLatin1Char('0'));
}

void Xsheet::setThumbnails(bool on)
{
    m_thumbnails = on;
    updateScrollBars();
    viewport()->update();
}

void Xsheet::setMinimalHeaders(bool on)
{
    m_minimal = on;
    updateScrollBars();
    viewport()->update();
}

void Xsheet::setGrouping(bool on)
{
    m_grouping = on;
    updateScrollBars();
    viewport()->update();
}

void Xsheet::setGridShown(bool on)
{
    m_grid = on;
    viewport()->update();
}

void Xsheet::setSelectionOnly(bool on, QVector<int> const& layers)
{
    m_selectionOnly = on;
    if (on)
        setSelectionLayers(layers);
    rebuildColumns();
    updateScrollBars();
    viewport()->update();
}

void Xsheet::setSelectionLayers(QVector<int> const& layers)
{
    Scene const& scene = m_document->scene();
    QSet<int> ids;
    for (int l : layers)
        if (l >= 0 && l < scene.layers.size())
            ids.insert(scene.layers[l].id);
    if (ids == m_selectionIds)
        return;
    m_selectionIds = ids;
    if (m_selectionOnly) {
        rebuildColumns();
        updateScrollBars();
        viewport()->update();
    }
}

void Xsheet::setOnion(bool on, int previous, int next)
{
    m_onion = on;
    m_onionPrevious = previous;
    m_onionNext = next;
    viewport()->update();
}

void Xsheet::setLightTable(bool on)
{
    m_lightTable = on;
    viewport()->update();
}

void Xsheet::setAnnotationFrameMarks(bool on)
{
    m_annotationMarks = on;
    viewport()->update();
}

void Xsheet::setAnnotationSmoothing(double hardness)
{
    m_annotationHardness = std::clamp(hardness, 0.0, 100.0);
    viewport()->update();
}

void Xsheet::collapseColumns(bool collapse, bool all)
{
    Scene const& scene = m_document->scene();
    QVector<int> layers;
    if (all) {
        for (int l = 0; l < scene.layers.size(); ++l)
            if (scene.layers[l].columnCollapsed != collapse)
                layers.push_back(l);
    } else {
        for (int l : selectedLayers())
            if (l >= 0 && l < scene.layers.size() && scene.layers[l].columnCollapsed != collapse)
                layers.push_back(l);
    }
    if (!layers.isEmpty())
        m_document->editLayers(layers, collapse ? tr("Collapse Columns") : tr("Expand Columns"), [collapse](Layer& l) { l.columnCollapsed = collapse; });
}

void Xsheet::deleteSelection()
{
    Scene const& scene = m_document->scene();
    QVector<int> layers;
    for (int l : selectedLayers())
        if (l >= 0 && l < scene.layers.size() && scene.layers[l].holdsDrawings())
            layers.push_back(l);
    // Every drawing column, from the first frame to the last: the frames themselves may go.
    int drawingColumns = 0;
    for (Layer const& l : scene.layers)
        drawingColumns += l.holdsDrawings() ? 1 : 0;
    if (hasSelection() && layers.size() == drawingColumns && drawingColumns > 0) {
        emit removeFramesRequested(selectionFirstFrame(), selectionLastFrame());
        return;
    }
    if (!hasSelection())
        m_document->clearCell(m_layer, m_frame);
    else
        m_document->clearCells(selectedLayers(), selectionFirstFrame(), selectionLastFrame(), tr("Clear Cells"));
}

void Xsheet::updateScrollBars()
{
    Scene const& scene = m_document->scene();
    int const contentHeight = scene.frameCount() * rowHeight();
    int const viewWidth = viewport()->width() - kRowHeaderWidth;
    int const viewHeight = viewport()->height() - headerHeight();
    horizontalScrollBar()->setRange(0, std::max(0, m_contentWidth - viewWidth));
    horizontalScrollBar()->setPageStep(std::max(1, viewWidth));
    horizontalScrollBar()->setSingleStep(std::max(8, m_columnWidth / 2));
    verticalScrollBar()->setRange(0, std::max(0, contentHeight - viewHeight));
    verticalScrollBar()->setPageStep(std::max(1, viewHeight));
    verticalScrollBar()->setSingleStep(rowHeight());
}

void Xsheet::resizeEvent(QResizeEvent*)
{
    updateScrollBars();
}

int Xsheet::columnAt(int x) const
{
    if (x < kRowHeaderWidth)
        return -1;
    int const at = x - kRowHeaderWidth + horizontalScrollBar()->value();
    for (int c = 0; c < m_columns.size(); ++c)
        if (at >= m_columns[c].x && at < m_columns[c].x + m_columns[c].width)
            return c;
    return -1;
}

int Xsheet::frameAt(int y) const
{
    int const at = y - headerHeight() + verticalScrollBar()->value();
    return at < 0 ? -1 - (-at - 1) / rowHeight() : at / rowHeight();
}

QRect Xsheet::columnCellRect(int column, int frame) const
{
    if (column < 0 || column >= m_columns.size())
        return QRect();
    return QRect(kRowHeaderWidth + m_columns[column].x - horizontalScrollBar()->value(), headerHeight() + frame * rowHeight() - verticalScrollBar()->value(),
                 m_columns[column].width, rowHeight());
}

QRect Xsheet::cellRect(int layer, int frame) const
{
    return columnCellRect(columnOfLayer(layer), frame);
}

QRect Xsheet::headerRect(int column) const
{
    if (column < 0 || column >= m_columns.size())
        return QRect();
    return QRect(kRowHeaderWidth + m_columns[column].x - horizontalScrollBar()->value(), 0, m_columns[column].width, headerHeight());
}

void Xsheet::extendSelection(int frame, int layer)
{
    int const scenes = int(m_document->scene().layers.size());
    extendSelectionToColumn(frame, layer < 0 || layer >= scenes || layer == m_layer ? -1 : columnOfLayer(layer));
}

void Xsheet::extendSelectionToColumn(int frame, int column)
{
    frame = std::clamp(frame, 0, m_document->scene().frameCount() - 1);
    int const extent = frame == m_frame ? -1 : frame;
    int const extentColumn = column < 0 || column >= m_columns.size() || column == m_column ? -1 : column;
    if (extent == m_extent && extentColumn == m_extentColumn)
        return;
    m_extent = extent;
    m_extentColumn = extentColumn;
    viewport()->update();
    notifySelection();
}

void Xsheet::notifySelection()
{
    emit selectionChanged();
    emit statusChanged();
}

QVector<int> Xsheet::selectedColumns() const
{
    QVector<int> columns;
    if (m_column < 0 || m_column >= m_columns.size())
        return columns;
    int const there = m_extentColumn < 0 || m_extentColumn >= m_columns.size() ? m_column : m_extentColumn;
    for (int c = std::min(m_column, there); c <= std::max(m_column, there); ++c)
        columns.push_back(c);
    return columns;
}

QVector<int> Xsheet::selectedLayers() const
{
    QVector<int> layers;
    for (int c : selectedColumns())
        if (!layers.contains(m_columns[c].layer))
            layers.push_back(m_columns[c].layer);
    if (layers.isEmpty())
        layers.push_back(m_layer);
    return layers;
}

void Xsheet::selectAll()
{
    emit cellPicked(m_layer, 0);
    extendSelection(m_document->scene().frameCount() - 1);
}

QString Xsheet::statusText() const
{
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size())
        return QString();
    Layer const& layer = scene.layers[m_layer];
    int const function = functionOfColumn(m_column);
    QString text = function >= 0 ? tr("%1: %2").arg(layer.name, Timeline::parameterName(Timeline::Parameter(function))) : layer.name;
    text += tr("   Frame %1").arg(frameLabel(m_frame));
    if (function >= 0)
        text += tr("   Value %1").arg(functionText(m_layer, function, m_frame));
    else if (layer.holdsDrawings() && layer.drawingAt(m_frame) != Layer::kNoDrawing)
        text += tr("   Drawing %1").arg(layer.drawingLabel(layer.drawingAt(m_frame)));
    if (hasSelection())
        text += tr("   %n frame(s) selected", nullptr, selectionLastFrame() - selectionFirstFrame() + 1);
    if (m_insert)
        text += tr("   Insert");
    return text;
}
