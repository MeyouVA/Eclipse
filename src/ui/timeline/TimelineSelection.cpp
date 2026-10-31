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

void Timeline::moveSelection(int rows, int frames, bool copy, bool alt)
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
    bool const gap = alt && !copy && (m_hasPreset ? m_preset.fillGap : m_dragGap);
    bool const toKey = alt && (m_hasPreset ? m_preset.fillToKey : m_dragToKey);
    QVector<int> landing;
    for (int l : std::as_const(to))
        if (l >= 0)
            landing.push_back(l);
    QVector<int> const cut = toKey ? exposuresCut(landing, first + frames + (last - first + 1)) : QVector<int>();
    bool const edges = pasteAddsEdgeKeys();
    if (gap || toKey || edges)
        m_document->beginMacro(copy ? tr("Copy Cells") : tr("Move Cells"));
    if (edges)
        addEdgeKeys(landing, first + frames, last + frames);
    m_document->moveCells(from, first, last, to, first + frames, copy, !m_overwriteDrop);
    if (toKey)
        holdOverCut(landing, cut, first + frames + (last - first + 1));
    if (gap)
        fillGap(from, first, last);
    if (gap || toKey || edges)
        m_document->endMacro();
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
    cellClipboard() = m_document->copyCells(selectedLayers(), selectionFirstFrame(), selectionLastFrame(), CellBlock::forceEdgeKeys);
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
    if (m_hasPreset) {
        pasteSpecial(m_preset);
        return;
    }
    if (pasteAddsEdgeKeys()) {
        PasteOptions options;
        options.edgeKeys = true;
        pasteSpecial(options);
        return;
    }
    pasteBlock(cellClipboard(), selectionFirstFrame());
}

bool& Timeline::pasteAddsEdgeKeys()
{
    static bool on = false;
    return on;
}

void Timeline::addEdgeKeys(QVector<int> const& layers, int first, int last)
{
    for (int l : layers) {
        Scene const& scene = m_document->scene();
        if (l < 0 || l >= scene.layers.size() || scene.layers[l].keys.isEmpty())
            continue;
        for (int f : {first, last})
            if (m_document->scene().layers[l].keyIndex(f) < 0)
                m_document->setKeyframe(l, f, m_document->scene().layers[l].poseAt(f), tr("Paste"));
    }
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
    PasteOptions options;
    options.drawings = drawings;
    options.keyframes = keyframes;
    options.cycles = cycles;
    options.reverse = reverse;
    pasteSpecial(options);
}

void Timeline::pasteSpecial(PasteOptions const& options)
{
    CellBlock block = cellClipboard();
    if (block.isEmpty() || (!options.drawings && !options.keyframes))
        return;
    if (options.reverse)
        block = block.reversed();
    if (options.cycles > 1)
        block = block.repeated(options.cycles);
    if (!options.drawings)
        for (CellBlock::Row& row : block.rows)
            row.holdsDrawings = false;      // the cells where it lands keep what they show
    block.leaveKeys = !options.keyframes;
    block.drawingMode = options.drawingMode;
    block.keyParts = options.parts;
    block.matchNames = options.matchNames;
    block.pastePivot = options.pivot;
    block.offsetKeys = options.offsetKeys;
    block.currentSetOnly = options.currentSetOnly;
    block.updateDrawingPivot = options.updateDrawingPivot;
    if (options.edgeKeys && options.keyframes) {
        // Each layer pasted onto: keyframes where the paste begins and ends, as the layer is there now.
        QVector<int> const selected = selectedLayers();
        int const first = selectionFirstFrame(), last = first + block.frames - 1;
        m_document->beginMacro(tr("Paste"));
        int rows = 0;
        for (int r = selected.isEmpty() ? -1 : rowOfLayer(selected.first()); r >= 0 && r < m_rows.size() && rows < block.rows.size(); ++r) {
            if (isParamRow(r))
                continue;
            ++rows;
            addEdgeKeys({m_rows[r]}, first, last);
        }
        if (!options.fillToKey)
            pasteBlock(block, first);
        m_document->endMacro();
        if (!options.fillToKey)
            return;
    }
    if (!options.fillToKey) {
        pasteBlock(block, selectionFirstFrame());
        return;
    }
    // Fill to Next Key Exposure: what was pasted holds on over the rest of the exposure it cut into.
    QVector<int> const selected = selectedLayers();
    QVector<int> layers;
    for (int r = rowOfLayer(selected.first()); r >= 0 && r < m_rows.size() && layers.size() < block.rows.size(); ++r)
        if (!isParamRow(r))
            layers.push_back(m_rows[r]);
    int const end = selectionFirstFrame() + block.frames;
    QVector<int> const cut = exposuresCut(layers, end);
    m_document->beginMacro(tr("Paste"));
    pasteBlock(block, selectionFirstFrame());
    holdOverCut(layers, cut, end);
    m_document->endMacro();
}

void Timeline::pasteReversed()
{
    pasteBlock(cellClipboard().reversed(), selectionFirstFrame());
}

void Timeline::pasteCycle(int times, int kind)
{
    pasteBlock(cellClipboard().cycled(times, kind), selectionFirstFrame());
}

void Timeline::createCycle(int times, int kind)
{
    if (times < 2 || selectionLastFrame() + 1 >= m_document->scene().frameCount())
        return;
    // The frames selected, then the cycle on after them (the first time round is the one there already).
    CellBlock const block = m_document->copyCells(selectedLayers(), selectionFirstFrame(), selectionLastFrame());
    CellBlock const all = block.cycled(times, kind);
    CellBlock after = all;
    after.frames = all.frames - block.frames;
    for (CellBlock::Row& row : after.rows) {
        row.cells = row.cells.mid(block.frames);
        QVector<Keyframe> keys;
        for (Keyframe key : row.keys)
            if (key.frame >= block.frames) {
                key.frame -= block.frames;
                keys.push_back(key);
            }
        row.keys = keys;
    }
    pasteBlock(after, selectionLastFrame() + 1);
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
