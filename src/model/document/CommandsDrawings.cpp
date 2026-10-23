// Eclipse - the document: drawings - new, duplicated, exposed, renamed, marked, imported, substituted; drivers baked, skins.
#include "model/document/Commands.h"
#include "model/geometry/Deform.h"

#include "model/document/CommandsPrivate.h"
#include "model/document/CommandsCellsShared.h"

using namespace commands_cells_shared;

void Document::newDrawing(int layer, int frame)
{
    if (validCell(m_scene, layer, frame) && m_scene.layers[layer].holdsDrawings())
        apply(tr("New Drawing"), [&](Scene& s) {
            Drawing made;
            if (m_nameByFrame)
                made.name = QString::number(frame + 1);
            startDrawingAt(s.layers[layer], frame, made);
        });
}

void Document::duplicateDrawing(int layer, int frame)
{
    if (!validCell(m_scene, layer, frame))
        return;
    int const index = m_scene.layers[layer].cells[frame];
    if (index == Layer::kNoDrawing)
        return;
    apply(tr("Duplicate Drawing"), [&](Scene& s) {
        Drawing copy = s.layers[layer].drawings[index];
        bool elsewhere = s.layers[layer].drawings[index].pinned;
        // (The copy takes over from this frame to the end of the exposure.)
        QVector<int> const& cells = s.layers[layer].cells;
        int end = frame + 1;
        while (end < cells.size() && cells[end] == index)
            ++end;
        for (int f = 0; f < cells.size(); ++f)
            elsewhere = elsewhere || ((f < frame || f >= end) && cells[f] == index);
        if (elsewhere)
            copy.name.clear();
        startDrawingAt(s.layers[layer], frame, copy);
    });
}

bool Document::exposeDrawing(int layer, int frame, QString const& label)
{
    if (!validCell(m_scene, layer, frame) || !m_scene.layers[layer].holdsDrawings())
        return false;
    Layer const& l = m_scene.layers[layer];
    QString const clean = label.trimmed();
    if (clean.isEmpty()) {
        if (l.cells[frame] == Layer::kNoDrawing)
            return false;
        clearCell(layer, frame);
        return true;
    }
    for (int i = 0; i < l.drawings.size(); ++i) {
        if (l.drawingLabel(i) != clean)
            continue;
        if (l.cells[frame] == i)
            return false;
        apply(tr("Type Exposure"), [&](Scene& s) {
            s.layers[layer].cells[frame] = i;
            s.layers[layer].compact();
        });
        return true;
    }
    apply(tr("Type Exposure"), [&](Scene& s) {
        Layer& made = s.layers[layer];
        Drawing drawing;
        // The number the new drawing would have anyway is no name.
        bool number = false;
        int const n = clean.toInt(&number);
        if (!(number && n == made.drawings.size() + 1))
            drawing.name = clean;
        made.drawings.push_back(drawing);
        made.cells[frame] = int(made.drawings.size()) - 1;
        made.compact();
    });
    return true;
}

bool Document::renameDrawingsWithPrefix(int layer, QString const& prefix)
{
    if (layer < 0 || layer >= m_scene.layers.size() || !m_scene.layers[layer].holdsDrawings() || prefix.trimmed().isEmpty())
        return false;
    Layer const& l = m_scene.layers[layer];
    bool any = false;
    for (int i = 0; i < l.drawings.size(); ++i)
        any = any || l.drawings[i].name != prefix.trimmed() + QString::number(i + 1);
    if (!any)
        return false;
    apply(tr("Rename Drawings with Prefix"), [&](Scene& s) {
        for (int i = 0; i < s.layers[layer].drawings.size(); ++i)
            s.layers[layer].drawings[i].name = prefix.trimmed() + QString::number(i + 1);
    });
    return true;
}

bool Document::renameDrawingsByFrame(int layer)
{
    if (layer < 0 || layer >= m_scene.layers.size() || !m_scene.layers[layer].holdsDrawings())
        return false;
    Layer const& l = m_scene.layers[layer];
    QVector<QString> names(l.drawings.size());
    bool any = false;
    for (int i = 0; i < l.drawings.size(); ++i) {
        names[i] = l.drawings[i].name;
        int const first = int(l.cells.indexOf(i));
        if (first >= 0)
            names[i] = QString::number(first + 1);
        any = any || names[i] != l.drawings[i].name;
    }
    if (!any)
        return false;
    apply(tr("Rename Drawings by Frame"), [&](Scene& s) {
        for (int i = 0; i < names.size(); ++i)
            s.layers[layer].drawings[i].name = names[i];
    });
    return true;
}

bool Document::markFrames(QVector<int> const& layers, int first, int last, QColor const& colour)
{
    first = std::max(first, 0);
    last = std::min(last, m_scene.frameCount() - 1);
    bool any = false;
    for (int l : layers) {
        if (l < 0 || l >= m_scene.layers.size())
            continue;
        for (int f = first; f <= last; ++f)
            any = any || (colour.isValid() ? m_scene.layers[l].frameMarks.value(f) != colour : m_scene.layers[l].frameMarks.contains(f));
    }
    if (!any)
        return false;
    apply(colour.isValid() ? tr("Mark Frame") : tr("Remove Frame Marker"), [&](Scene& s) {
        for (int l : layers) {
            if (l < 0 || l >= s.layers.size())
                continue;
            for (int f = first; f <= last; ++f) {
                if (colour.isValid())
                    s.layers[l].frameMarks.insert(f, colour);
                else
                    s.layers[l].frameMarks.remove(f);
            }
        }
    });
    return true;
}

bool Document::markDrawing(int layer, int frame, int mark)
{
    if (!validCell(m_scene, layer, frame) || mark < Drawing::NoMark || mark > Drawing::InBetweenMark)
        return false;
    int const index = m_scene.layers[layer].cells[frame];
    if (index == Layer::kNoDrawing || m_scene.layers[layer].drawings[index].mark == mark)
        return false;
    apply(tr("Mark Drawing"), [&](Scene& s) { s.layers[layer].drawings[index].mark = mark; });
    return true;
}

bool Document::renameDrawing(int layer, int frame, QString const& name)
{
    if (!validCell(m_scene, layer, frame))
        return false;
    Layer const& l = m_scene.layers[layer];
    int const index = l.cells[frame];
    QString const clean = name.trimmed();
    if (index == Layer::kNoDrawing || clean == l.drawings[index].name)
        return false;
    for (int i = 0; i < l.drawings.size(); ++i)
        if (i != index && !clean.isEmpty() && l.drawingLabel(i) == clean && l.drawings[i].set == l.drawings[index].set)
            return false;       // two drawings of a drawing layer cannot go by one name
    apply(tr("Rename Drawing"), [&](Scene& s) { s.layers[layer].drawings[index].name = clean; });
    return true;
}

void Document::extendExposure(int layer, int frame)
{
    if (!validCell(m_scene, layer, frame) || frame == 0)
        return;
    int const previous = m_scene.layers[layer].cells[frame - 1];
    if (previous == Layer::kNoDrawing || previous == m_scene.layers[layer].cells[frame])
        return;
    apply(tr("Extend Exposure"), [&](Scene& s) {
        s.layers[layer].cells[frame] = previous;
        s.layers[layer].compact();
    });
}

void Document::deleteDrawing(int layer, int frame)
{
    if (!validCell(m_scene, layer, frame))
        return;
    int const drawing = m_scene.layers[layer].drawingAt(frame);
    if (drawing == Layer::kNoDrawing)
        return;
    apply(tr("Delete Drawing"), [&](Scene& s) {
        for (int& cell : s.layers[layer].cells)
            if (cell == drawing)
                cell = Layer::kNoDrawing;
        s.layers[layer].compact();
    });
}

void Document::clearCell(int layer, int frame)
{
    if (!validCell(m_scene, layer, frame) || m_scene.layers[layer].cells[frame] == Layer::kNoDrawing)
        return;
    apply(tr("Clear Cell"), [&](Scene& s) {
        s.layers[layer].cells[frame] = Layer::kNoDrawing;
        s.layers[layer].compact();
    });
}

int Document::importDrawings(int layer, QString const& name, int frame, QVector<Drawing> const& drawings, int hold)
{
    if (drawings.isEmpty() || frame < 0)
        return -1;
    if (layer >= 0 && (layer >= m_scene.layers.size() || !m_scene.layers[layer].holdsDrawings()))
        return -1;
    hold = std::max(1, hold);
    int at = layer;
    apply(tr("Import"), [&](Scene& s) {
        int const end = frame + int(drawings.size()) * hold;
        if (end > s.frameCount())
            s.setFrameCount(end);
        if (at < 0)
            at = s.addLayer(name.isEmpty() ? tr("Drawing") : name, int(s.layers.size()), Layer::DrawingLayer);
        Layer& target = s.layers[at];
        int const base = int(target.drawings.size());
        target.drawings += drawings;
        for (int i = 0; i < drawings.size(); ++i)
            for (int h = 0; h < hold; ++h)
                target.cells[frame + i * hold + h] = base + i;
        target.compact();
    });
    return at;
}

void Document::substituteDrawing(int layer, int frame, int drawing)
{
    if (!validCell(m_scene, layer, frame))
        return;
    Layer const& l = m_scene.layers[layer];
    if (drawing < 0 || drawing >= l.drawings.size() || l.cells[frame] == drawing)
        return;
    bool const midway = frame > 0 && l.cells[frame - 1] == l.cells[frame] && l.cells[frame] != Layer::kNoDrawing;
    // Enforce Key Exposure: from the frame chosen on (a new exposure starts there); off, the whole exposure is swapped.
    int from = frame;
    if (!m_substitutionKeys && midway)
        while (from > 0 && l.cells[from - 1] == l.cells[frame] && !l.keyExposures.contains(from))
            --from;
    apply(tr("Substitute Drawing"), [&](Scene& s) { s.layers[layer].substitute(from, drawing); });
}

void Document::stepFrameSubstitution(int layer, int frame, int direction)
{
    if (!validCell(m_scene, layer, frame))
        return;
    Layer const& l = m_scene.layers[layer];
    int const shown = l.cells[frame];
    // The next (previous) exposure along the layer that shows another drawing.
    for (int f = frame + direction; f >= 0 && f < l.cells.size(); f += direction)
        if (l.cells[f] != shown && l.cells[f] != Layer::kNoDrawing) {
            substituteDrawing(layer, frame, l.cells[f]);
            return;
        }
}

bool Document::moveDrawing(int layer, int drawing, int direction)
{
    if (layer < 0 || layer >= m_scene.layers.size())
        return false;
    int const other = drawing + direction;
    int const count = int(m_scene.layers[layer].drawings.size());
    if (drawing < 0 || drawing >= count || other < 0 || other >= count)
        return false;
    int const id = m_scene.layers[layer].id;
    apply(direction < 0 ? tr("Move Drawing Up") : tr("Move Drawing Down"), [&](Scene& s) {
        Layer& edited = s.layers[layer];
        edited.drawings.swapItemsAt(drawing, other);
        for (int& cell : edited.cells)
            cell = cell == drawing ? other : cell == other ? drawing : cell;
        QVector<int> remap(count);
        for (int i = 0; i < count; ++i)
            remap[i] = i == drawing ? other : i == other ? drawing : i;
        edited.renumberMoves(remap);
        for (Scene::DeskItem& item : s.desk)
            if (item.layer == id)
                item.drawing = item.drawing == drawing ? other : item.drawing == other ? drawing : item.drawing;
    });
    return true;
}

void Document::stepSubstitution(int layer, int frame, int direction)
{
    if (!validCell(m_scene, layer, frame))
        return;
    Layer const& l = m_scene.layers[layer];
    int const count = int(l.drawings.size());
    if (count == 0)
        return;
    int const shown = l.cells[frame];
    if (!m_substitutionCycles && shown != Layer::kNoDrawing && (shown + direction < 0 || shown + direction >= count))
        return;     // (at an end, and not going round)
    int const next = shown == Layer::kNoDrawing ? (direction > 0 ? 0 : count - 1) : ((shown + direction) % count + count) % count;
    substituteDrawing(layer, frame, next);
}

void Document::stepSubstitutionInSet(int layer, int frame, int direction)
{
    if (!validCell(m_scene, layer, frame))
        return;
    Layer const& l = m_scene.layers[layer];
    int const shown = l.cells[frame];
    if (shown == Layer::kNoDrawing) {
        stepSubstitution(layer, frame, direction);
        return;
    }
    int const count = int(l.drawings.size());
    for (int step = 1; step < count; ++step) {
        if (!m_substitutionCycles && (shown + direction * step < 0 || shown + direction * step >= count))
            return;
        int const next = ((shown + direction * step) % count + count) % count;
        if (l.drawings[next].set == l.drawings[shown].set) {
            substituteDrawing(layer, frame, next);
            return;
        }
    }
}

bool Document::stepDrawingSet(int layer, int frame, int direction)
{
    if (!validCell(m_scene, layer, frame))
        return false;
    Layer const& l = m_scene.layers[layer];
    int const shown = l.cells[frame];
    if (shown == Layer::kNoDrawing)
        return false;
    QStringList const sets = l.drawingSets();
    int const at = int(sets.indexOf(l.drawings[shown].set));
    QString const label = l.drawingLabel(shown);
    for (int step = 1; step < sets.size(); ++step) {
        QString const other = sets[((at + direction * step) % int(sets.size()) + int(sets.size())) % int(sets.size())];
        int const match = l.drawingInSet(other, label);
        if (match != Layer::kNoDrawing) {
            substituteDrawing(layer, frame, match);
            return true;
        }
    }
    return false;
}

int Document::bakeDrivers(QVector<int> const& layers)
{
    QVector<QPair<int, QVector<Pose>>> baked;
    for (int l : layers) {
        if (l < 0 || l >= m_scene.layers.size() || m_scene.layers[l].drivers.isEmpty())
            continue;
        QVector<Pose> poses;
        for (int f = 0; f < m_scene.frameCount(); ++f)
            poses.push_back(m_scene.poseOf(l, f));
        baked.push_back({l, poses});
    }
    if (baked.isEmpty())
        return 0;
    apply(tr("Bake Attribute Controls"), [&](Scene& s) {
        for (auto const& b : baked) {
            Layer& layer = s.layers[b.first];
            layer.drivers.clear();
            for (int f = 0; f < b.second.size(); ++f)
                layer.setKey(f, b.second[f]);
        }
    });
    return int(baked.size());
}

int Document::applySkin(QVector<int> const& layers, QString const& set, int first, int last)
{
    // Worked out first, so that a skin that changes nothing leaves nothing to undo.
    QVector<QPair<int, QVector<int>>> changes;
    int changed = 0;
    for (int l : layers) {
        if (l < 0 || l >= m_scene.layers.size() || !m_scene.layers[l].holdsDrawings())
            continue;
        Layer const& layer = m_scene.layers[l];
        QVector<int> cells = layer.cells;
        for (int f = std::max(0, first); f <= std::min(last, int(cells.size()) - 1); ++f) {
            if (cells[f] == Layer::kNoDrawing || layer.drawings[cells[f]].set == set)
                continue;
            int const match = layer.drawingInSet(set, layer.drawingLabel(cells[f]));
            if (match != Layer::kNoDrawing) {
                cells[f] = match;
                ++changed;
            }
        }
        if (cells != layer.cells)
            changes.push_back({l, cells});
    }
    if (changes.isEmpty())
        return 0;
    apply(tr("Apply Skin"), [&](Scene& s) {
        for (auto const& change : changes) {
            Layer& layer = s.layers[change.first];
            for (int f = 0; f < change.second.size(); ++f)
                if (change.second[f] != layer.cells[f] && change.second[f] != Layer::kNoDrawing) {
                    layer.drawings[change.second[f]].pinned = true;
                    if (layer.cells[f] != Layer::kNoDrawing)
                        layer.drawings[layer.cells[f]].pinned = true;
                }
            layer.cells = change.second;
        }
    });
    return changed;
}

bool Document::setDrawingSet(int layer, int frame, QString const& set)
{
    if (!validCell(m_scene, layer, frame))
        return false;
    Layer const& l = m_scene.layers[layer];
    int const index = l.cells[frame];
    QString const clean = set.trimmed();
    if (index == Layer::kNoDrawing || l.drawings[index].set == clean)
        return false;
    int const same = l.drawingInSet(clean, l.drawingLabel(index));
    if (same != Layer::kNoDrawing && same != index)
        return false;       // that drawing layer has a drawing of this name
    apply(tr("Set Drawing Layer"), [&](Scene& s) { s.layers[layer].drawings[index].set = clean; });
    return true;
}

int Document::insertInbetweenDrawing(int layer, int frame)
{
    // The exposure the frame is in, held two frames or more: its second half shows a new empty drawing, named between the two either side.
    if (!validCell(m_scene, layer, frame))
        return -1;
    Layer const& l = m_scene.layers[layer];
    int const shown = l.cells[frame];
    if (shown == Layer::kNoDrawing)
        return -1;
    int start = frame, end = frame + 1;
    while (start > 0 && l.cells[start - 1] == shown && !l.keyExposures.contains(start))
        --start;
    while (end < l.cells.size() && l.cells[end] == shown && !l.keyExposures.contains(end))
        ++end;
    if (end - start < 2)
        return -1;
    int const middle = start + (end - start) / 2;
    QString const before = l.drawingLabel(shown);
    QString const after = end < l.cells.size() && l.cells[end] != Layer::kNoDrawing ? l.drawingLabel(l.cells[end]) : QString();
    QString name = after.isEmpty() ? before + QStringLiteral("+") : before + QStringLiteral("+") + after;
    for (int d = 0; d < l.drawings.size(); ++d)
        if (l.drawingLabel(d) == name)
            name += QLatin1Char('_');
    int made = -1;
    apply(tr("Insert Action Inbetween Drawing"), [&](Scene& s) {
        Layer& edited = s.layers[layer];
        Drawing inbetween;
        inbetween.name = name;
        inbetween.set = edited.drawings[shown].set;
        inbetween.mark = Drawing::InBetweenMark;
        made = int(edited.drawings.size());
        edited.drawings.push_back(inbetween);
        for (int f = middle; f < end; ++f)
            edited.cells[f] = made;
    });
    return made;
}
