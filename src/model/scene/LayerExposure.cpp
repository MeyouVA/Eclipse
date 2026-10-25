// Eclipse - a layer: key exposures, morphs, the drawing shown, exposures, substitution, drawing sets, compacting.
#include "model/scene/Scene.h"
#include "model/geometry/Morph.h"

#include "model/geometry/Deform.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include "model/scene/LayerShared.h"

using namespace layer_shared;

bool Layer::isKey(int frame) const
{
    int const d = drawingAt(frame);
    return d != kNoDrawing && (frame == 0 || drawingAt(frame - 1) != d || keyExposures.contains(frame));
}

void Layer::tidyKeyExposures()
{
    if (keyExposures.isEmpty())
        return;
    QVector<int> kept;
    for (int frame : std::as_const(keyExposures))
        if (frame > 0 && frame < cells.size() && cells[frame] != kNoDrawing && cells[frame - 1] == cells[frame] && !kept.contains(frame))
            kept.push_back(frame);
    std::sort(kept.begin(), kept.end());
    keyExposures = kept;
}

void Layer::shiftKeyExposures(int at, int delta)
{
    if (keyExposures.isEmpty() || delta == 0)
        return;
    QVector<int> moved;
    for (int frame : std::as_const(keyExposures)) {
        if (frame < at || (delta < 0 && frame == at))
            moved.push_back(frame);                 // before what changed (or the first cell taken out: what comes after it takes its place)
        else if (delta > 0)
            moved.push_back(frame + delta);
        else if (frame >= at - delta)
            moved.push_back(frame + delta);         // after the cells taken out
        // (a mark on a cell that was taken out goes with it)
    }
    keyExposures = moved;
}

int Layer::morphEnd(int index) const
{
    if (index < 0 || index >= morphs.size())
        return -1;
    int const from = morphs[index].from;
    if (drawingAt(from) == kNoDrawing || exposureStart(from) != from)
        return -1;      // its first drawing does not start there any more
    int const next = nextDrawingFrame(from);
    return next > from && drawingAt(next) != kNoDrawing ? next : -1;
}

int Layer::morphIndexAt(int frame) const
{
    int const start = exposureStart(frame);
    if (start < 0)
        return -1;
    for (int i = 0; i < morphs.size(); ++i)
        if (morphs[i].from == start && morphEnd(i) > frame)
            return i;
    return -1;
}

bool Layer::morphing(int frame) const
{
    int const index = morphIndexAt(frame);
    return index >= 0 && frame > morphs[index].from;
}

double Morph::progress(double t) const
{
    t = std::clamp(t, 0.0, 1.0);
    if (velocity.isEmpty())
        return easedProgress(t, easeOut, easeIn);
    QVector<QPointF> p{QPointF(0.0, 0.0)};
    for (QPointF const& v : velocity)
        if (v.x() > p.last().x() + 1e-6 && v.x() < 1.0 - 1e-6)
            p.push_back(v);
    p.push_back(QPointF(1.0, 1.0));
    int i = 0;
    while (i + 2 < p.size() && t > p[i + 1].x())
        ++i;
    auto slope = [&](int k) {
        int const a = std::max(0, k - 1), b = std::min(int(p.size()) - 1, k + 1);
        return (p[b].y() - p[a].y()) / (p[b].x() - p[a].x());
    };
    double const h = p[i + 1].x() - p[i].x(), s = (t - p[i].x()) / h;
    double const s2 = s * s, s3 = s2 * s;
    return (2 * s3 - 3 * s2 + 1) * p[i].y() + (s3 - 2 * s2 + s) * h * slope(i) + (-2 * s3 + 3 * s2) * p[i + 1].y()
           + (s3 - s2) * h * slope(i + 1);
}

Drawing Layer::shownAt(int frame) const
{
    int const drawing = drawingAt(frame);
    if (drawing == kNoDrawing || drawing >= drawings.size())
        return Drawing();
    int const index = morphIndexAt(frame);
    if (index < 0 || frame <= morphs[index].from)
        return drawings[drawing];
    int const from = morphs[index].from, to = morphEnd(index);
    int const other = drawingAt(to);
    if (other < 0 || other >= drawings.size())
        return drawings[drawing];
    double const t = morphs[index].progress(double(frame - from) / double(to - from));
    return Morphing::between(drawings[drawing], drawings[other], t, morphs[index].hints, morphs[index].groups);
}

int Layer::exposureStart(int frame) const
{
    int const d = drawingAt(frame);
    if (d == kNoDrawing)
        return -1;
    while (frame > 0 && cells[frame - 1] == d && !keyExposures.contains(frame))
        --frame;
    return frame;
}

int Layer::previousDrawingFrame(int frame) const
{
    int const start = exposureStart(frame);
    int f = (start >= 0 ? start : frame) - 1;
    while (f >= 0 && cells[f] == kNoDrawing)
        --f;
    return f >= 0 ? exposureStart(f) : -1;
}

int Layer::nextDrawingFrame(int frame) const
{
    int const d = drawingAt(frame);
    int f = frame + 1;
    while (f < cells.size() && (cells[f] == kNoDrawing || (cells[f] == d && !keyExposures.contains(f))))
        ++f;
    return f < cells.size() ? f : -1;
}

void Layer::substitute(int frame, int drawing)
{
    if (frame < 0 || frame >= cells.size() || drawing < 0 || drawing >= drawings.size())
        return;
    int const old = cells[frame];
    int end = frame + 1;
    if (old != kNoDrawing) {
        while (end < cells.size() && cells[end] == old)
            ++end;
        drawings[old].pinned = true;
    }
    drawings[drawing].pinned = true;
    for (int f = frame; f < end; ++f)
        cells[f] = drawing;
}

QStringList Layer::drawingSets() const
{
    QStringList sets;
    for (Drawing const& d : drawings)
        if (!sets.contains(d.set))
            sets << d.set;
    return sets;
}

int Layer::drawingInSet(QString const& set, QString const& label) const
{
    for (int i = 0; i < drawings.size(); ++i)
        if (drawings[i].set == set && drawingLabel(i) == label)
            return i;
    return kNoDrawing;
}

// Two drawings of a drawing layer cannot go by one name (pasted cells bring theirs along): the later one loses it.
static void oneNameEach(QVector<Drawing>& drawings)
{
    for (int i = 1; i < drawings.size(); ++i) {
        if (drawings[i].name.isEmpty())
            continue;
        for (int k = 0; k < i; ++k) {
            if (drawings[k].name == drawings[i].name && drawings[k].set == drawings[i].set) {
                drawings[i].name.clear();
                break;
            }
        }
    }
}

void Layer::compact()
{
    keepDrawingIdentitiesUnique();
    QVector<int> remap(drawings.size(), kNoDrawing);
    QVector<Drawing> kept;
    bool bank = false;
    for (Drawing const& d : drawings)
        bank = bank || d.pinned;
    if (bank) {
        QVector<bool> used(drawings.size(), false);
        for (int cell : cells)
            if (cell != kNoDrawing)
                used[cell] = true;
        for (int i = 0; i < drawings.size(); ++i) {
            if (used[i] || drawings[i].pinned) {
                remap[i] = int(kept.size());
                kept.push_back(drawings[i]);
            }
        }
        for (int& cell : cells)
            if (cell != kNoDrawing)
                cell = remap[cell];
        drawings = kept;
        oneNameEach(drawings);
        renumberMoves(remap);
        return;
    }
    for (int& cell : cells) {
        if (cell == kNoDrawing)
            continue;
        if (remap[cell] == kNoDrawing) {
            remap[cell] = int(kept.size());
            kept.push_back(drawings[cell]);
        }
        cell = remap[cell];
    }
    drawings = kept;
    oneNameEach(drawings);
    renumberMoves(remap);
}

void Layer::renumberMoves(QVector<int> const& remap)
{
    for (Keyframe& k : keys) {
        QVector<PointMove> kept;
        for (PointMove m : std::as_const(k.pose.points)) {
            if (m.drawing < 0 || m.drawing >= remap.size() || remap[m.drawing] == kNoDrawing)
                continue;
            m.drawing = remap[m.drawing];
            kept.push_back(m);
        }
        std::sort(kept.begin(), kept.end(), [](PointMove const& a, PointMove const& b) { return a.comesBefore(b); });
        k.pose.points = kept;
    }
}
