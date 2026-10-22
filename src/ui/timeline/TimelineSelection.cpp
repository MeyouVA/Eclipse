// Eclipse - the Timeline: the block of cells selected - moved, copied, pasted, cycled.
#include "canvas/Canvas.h"
#include "ui/timeline/Timeline.h"

#include "audio/sound/Sound.h"
#include "model/document/Commands.h"

QVector<int> Timeline::selectedLayers() const
{
    int const row = rowOfLayer(m_layer);
    QVector<int> layers;
    if (row < 0 || m_extentRow < 0) {
        layers.push_back(m_layer);
    } else {
        for (int r = std::min(row, m_extentRow); r <= std::max(row, m_extentRow) && r < m_rows.size(); ++r)
            if (!layers.contains(m_rows[r]))
                layers.push_back(m_rows[r]);
    }
    // Then the layers picked one by one, top to bottom.
    for (int l : m_rows)
        if (m_extraLayers.contains(l) && !layers.contains(l))
            layers.push_back(l);
    return layers;
}

void Timeline::toggleLayer(int layer)
{
    if (layer < 0 || layer >= m_document->scene().layers.size() || layer == m_layer)
        return;
    if (m_extraLayers.contains(layer))
        m_extraLayers.removeAll(layer);
    else
        m_extraLayers.push_back(layer);
    viewport()->update();
    emit selectionChanged();
}

void Timeline::setExtraLayers(QVector<int> const& layers)
{
    m_extraLayers.clear();
    for (int layer : layers)
        if (layer >= 0 && layer < m_document->scene().layers.size() && layer != m_layer && !m_extraLayers.contains(layer))
            m_extraLayers.push_back(layer);
    viewport()->update();
    emit selectionChanged();
}

void Timeline::clampMove(int& rows, int& frames) const
{
    int const here = rowOfLayer(m_layer);
    int const r0 = m_extentRow < 0 ? here : std::min(here, m_extentRow), r1 = m_extentRow < 0 ? here : std::max(here, m_extentRow);
    rows = std::clamp(rows, -r0, int(m_rows.size()) - 1 - r1);
    frames = std::clamp(frames, -selectionFirstFrame(), m_document->scene().frameCount() - 1 - selectionLastFrame());
}

void Timeline::moveSelection(int rows, int frames, bool copy)
{
    int const here = rowOfLayer(m_layer);
    if (here < 0)
        return;
    clampMove(rows, frames);
    if (rows == 0 && frames == 0)
        return;
    int const r0 = m_extentRow < 0 ? here : std::min(here, m_extentRow), r1 = m_extentRow < 0 ? here : std::max(here, m_extentRow);
    QVector<int> from, to;
    for (int r = r0; r <= r1; ++r) {
        if (isParamRow(r))
            continue;       // the rows of parameters go with their layer
        from.push_back(m_rows[r]);
        to.push_back(isParamRow(r + rows) ? -1 : m_rows[r + rows]);
    }
    int const first = selectionFirstFrame(), last = selectionLastFrame();
    int const extentRow = m_extentRow, extentFrame = m_extentFrame;
    m_document->moveCells(from, first, last, to, first + frames, copy);
    // The block goes with its cells.
    m_extentRow = m_extentFrame = -1;
    setCurrentCell(m_rows[here + rows], m_frame + frames);
    if (extentRow >= 0) {
        m_extentRow = extentRow + rows;
        m_extentFrame = extentFrame + frames;
    }
    viewport()->update();
    emit selectionChanged();
}

int Timeline::selectionFirstFrame() const
{
    return m_extentRow < 0 ? m_frame : std::min(m_frame, m_extentFrame);
}

int Timeline::selectionLastFrame() const
{
    return m_extentRow < 0 ? m_frame : std::max(m_frame, m_extentFrame);
}

void Timeline::extendSelection(int layer, int frame)
{
    int const row = rowOfLayer(layer);
    if (row < 0)
        return;
    frame = std::clamp(frame, 0, m_document->scene().frameCount() - 1);
    // Back on the current cell there is no block any more.
    bool const none = row == rowOfLayer(m_layer) && frame == m_frame;
    int const newRow = none ? -1 : row, newFrame = none ? -1 : frame;
    if (newRow == m_extentRow && newFrame == m_extentFrame)
        return;
    m_extentRow = newRow;
    m_extentFrame = newFrame;
    viewport()->update();
    emit selectionChanged();
}

void Timeline::clearRange()
{
    if (m_extentRow < 0 && m_extraLayers.isEmpty())
        return;
    m_extraLayers.clear();
    m_extentRow = m_extentFrame = -1;
    viewport()->update();
    emit selectionChanged();
}

// What was copied last: kept by the program, for any view of it.
CellBlock& Timeline::clipboard()
{
    static CellBlock block;
    return block;
}

static CellBlock& cellClipboard()
{
    return Timeline::clipboard();
}

bool Timeline::canPasteCells()
{
    return !cellClipboard().isEmpty();
}

void Timeline::copy()
{
    cellClipboard() = m_document->copyCells(selectedLayers(), selectionFirstFrame(), selectionLastFrame());
    Canvas::setArtworkCopiedLast(false);
}

void Timeline::cut()
{
    copy();
    m_document->clearCells(selectedLayers(), selectionFirstFrame(), selectionLastFrame(), tr("Cut Cells"));
}

void Timeline::paste()
{
    if (Canvas::artworkCopiedLast()) {
        emit pasteArtwork();
        return;
    }
    pasteBlock(cellClipboard(), selectionFirstFrame());
}

void Timeline::pasteBlock(CellBlock const& block, int frame)
{
    if (block.isEmpty())
        return;
    // From the top left cell of what is selected, onto as many rows down as the block has.
    QVector<int> const selected = selectedLayers();
    int const top = rowOfLayer(selected.first());
    QVector<int> layers;
    for (int r = top; r >= 0 && r < m_rows.size() && layers.size() < block.rows.size(); ++r)
        if (!isParamRow(r))
            layers.push_back(m_rows[r]);
    m_document->pasteCells(layers, frame, block);
}

void Timeline::pasteSpecial(bool drawings, bool keyframes, int cycles, bool reverse)
{
    CellBlock block = cellClipboard();
    if (block.isEmpty() || (!drawings && !keyframes))
        return;
    if (reverse)
        block = block.reversed();
    if (cycles > 1)
        block = block.repeated(cycles);
    if (!drawings)
        for (CellBlock::Row& row : block.rows)
            row.holdsDrawings = false;      // the cells where it lands keep what they show
    block.leaveKeys = !keyframes;
    pasteBlock(block, selectionFirstFrame());
}

void Timeline::pasteReversed()
{
    pasteBlock(cellClipboard().reversed(), selectionFirstFrame());
}

void Timeline::pasteCycle(int times)
{
    pasteBlock(cellClipboard().repeated(times), selectionFirstFrame());
}

void Timeline::createCycle(int times)
{
    if (times < 2 || selectionLastFrame() + 1 >= m_document->scene().frameCount())
        return;
    CellBlock const block = m_document->copyCells(selectedLayers(), selectionFirstFrame(), selectionLastFrame());
    pasteBlock(block.repeated(times - 1), selectionLastFrame() + 1);
}

void Timeline::selectAll()
{
    if (m_rows.isEmpty())
        return;
    // From the first cell of the top row to the last cell of the bottom one.
    m_extentRow = m_extentFrame = -1;
    setCurrentCell(m_rows.first(), 0);
    extendSelection(m_rows.last(), m_document->scene().frameCount() - 1);
}
