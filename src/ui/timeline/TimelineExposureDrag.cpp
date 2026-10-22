// Eclipse - the Timeline: Alt-drag - an exposure held on (or cut short) to where it is let go; and what Alt-dragged cells fill in.
#include "ui/timeline/Timeline.h"

#include <QScrollBar>

#include "model/document/Commands.h"

bool Timeline::beginExposureDrag(QPoint pos)
{
    int const row = rowAt(pos.y());
    if (pos.x() < kHeaderWidth || row < 0 || isParamRow(row))
        return false;
    Scene const& scene = m_document->scene();
    int const l = m_rows[row];
    int const f = frameAt(pos.x());
    if (l < 0 || l >= scene.layers.size() || !scene.layers[l].holdsDrawings() || scene.layers[l].locked || f < 0 || f >= scene.frameCount())
        return false;
    Layer const& layer = scene.layers[l];
    // On an empty cell: the exposure before it (Extend Exposure of Previous Drawing).
    int start = f;
    while (start >= 0 && layer.drawingAt(start) == Layer::kNoDrawing)
        --start;
    if (start < 0)
        return false;
    int const drawing = layer.drawingAt(start);
    while (start > 0 && layer.drawingAt(start - 1) == drawing && !layer.isKey(start))
        --start;
    int end = start + 1;
    while (end < scene.frameCount() && layer.drawingAt(end) == drawing && !layer.isKey(end))
        ++end;
    clearRange();
    setCurrentCell(l, start);
    m_exposureLayer = l;
    m_exposureStart = start;
    m_exposureEnd = end;
    m_exposureCells = layer.cells;
    m_exposureBegun = false;
    moveExposureDrag(pos);
    return true;
}

void Timeline::moveExposureDrag(QPoint pos)
{
    Scene const& scene = m_document->scene();
    if (m_exposureLayer >= scene.layers.size())
        return;
    // The exposure runs from its start to the frame under the pointer (at least its first frame).
    int const last = std::clamp(frameAt(std::max(pos.x(), int(kHeaderWidth))), m_exposureStart, scene.frameCount() - 1);
    int const drawing = m_exposureCells.value(m_exposureStart, Layer::kNoDrawing);
    int const through = std::max(last + 1, m_exposureEnd);
    QVector<int> cells;
    for (int f = m_exposureStart; f < through && f < m_exposureCells.size(); ++f)
        cells.push_back(f <= last ? drawing : f < m_exposureEnd ? int(Layer::kNoDrawing) : m_exposureCells[f]);
    if (!m_exposureBegun) {
        m_exposureBegun = true;
        m_document->beginMacro(tr("Extend Exposure"));
    }
    m_document->setCells(m_exposureLayer, m_exposureStart, cells, tr("Extend Exposure"));
    setCurrentCell(m_exposureLayer, last);
    viewport()->update();
}

void Timeline::endExposureDrag()
{
    if (m_exposureBegun)
        m_document->endMacro();
    m_exposureLayer = -1;
    m_exposureBegun = false;
    m_exposureCells.clear();
}

QVector<int> Timeline::exposuresCut(QVector<int> const& layers, int end) const
{
    Scene const& scene = m_document->scene();
    QVector<int> cut;
    for (int l : layers) {
        int reach = -1;
        if (l >= 0 && l < scene.layers.size() && scene.layers[l].holdsDrawings() && end > 0 && end < scene.layers[l].cells.size()) {
            Layer const& layer = scene.layers[l];
            int const d = layer.cells[end];
            if (d != Layer::kNoDrawing && layer.cells[end - 1] == d && !layer.keyExposures.contains(end)) {
                reach = end;
                while (reach < layer.cells.size() && layer.cells[reach] == d && !(reach > end && layer.keyExposures.contains(reach)))
                    ++reach;
            }
        }
        cut.push_back(reach);
    }
    return cut;
}

void Timeline::holdOverCut(QVector<int> const& layers, QVector<int> const& cut, int end)
{
    Scene const& scene = m_document->scene();
    for (int i = 0; i < layers.size() && i < cut.size(); ++i) {
        int const l = layers[i];
        if (cut[i] <= end || l < 0 || l >= scene.layers.size() || end - 1 >= scene.layers[l].cells.size())
            continue;
        int const dropped = scene.layers[l].cells[end - 1];
        if (dropped == Layer::kNoDrawing)
            continue;
        m_document->setCells(l, end, QVector<int>(cut[i] - end, dropped), tr("Fill to Next Key Exposure"));
    }
}

void Timeline::fillGap(QVector<int> const& layers, int first, int last)
{
    Scene const& scene = m_document->scene();
    for (int l : layers) {
        if (l < 0 || l >= scene.layers.size() || !scene.layers[l].holdsDrawings() || first < 1)
            continue;
        Layer const& layer = scene.layers[l];
        int const before = layer.drawingAt(first - 1);
        if (before == Layer::kNoDrawing)
            continue;
        QVector<int> cells;
        for (int f = first; f <= last && f < layer.cells.size(); ++f)
            cells.push_back(layer.cells[f] == Layer::kNoDrawing ? before : layer.cells[f]);
        m_document->setCells(l, first, cells, tr("Fill Gap with Previous Drawing"));
    }
}
