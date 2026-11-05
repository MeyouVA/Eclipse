// Eclipse - the document: a morphing's options, its suggested hints, a key drawing put in it, its length.
#include "model/document/Commands.h"
#include "model/geometry/Morph.h"

#include "model/document/CommandsPrivate.h"

bool Document::setMorph(int layer, int index, Morph const& morph, QString const& text)
{
    if (layer < 0 || layer >= m_scene.layers.size() || index < 0 || index >= m_scene.layers[layer].morphs.size())
        return false;
    Morph changed = morph;
    changed.from = m_scene.layers[layer].morphs[index].from;
    changed.quality = std::clamp(changed.quality, 0.0, 1.0);
    for (Morph::Group& g : changed.groups) {
        g.start = std::clamp(g.start, 0.0, 1.0);
        g.stop = std::clamp(g.stop, g.start, 1.0);
        if (g.path >= changed.paths.size())
            g.path = -1;
    }
    if (changed == m_scene.layers[layer].morphs[index])
        return false;
    apply(text, [&](Scene& s) { s.layers[layer].morphs[index] = changed; });
    return true;
}

int Document::suggestMorphHints(int layer, int index)
{
    if (layer < 0 || layer >= m_scene.layers.size() || index < 0 || index >= m_scene.layers[layer].morphs.size())
        return 0;
    Layer const& l = m_scene.layers[layer];
    int const from = l.morphs[index].from, to = l.morphEnd(index);
    int const a = l.drawingAt(from), b = to >= 0 ? l.drawingAt(to) : Layer::kNoDrawing;
    if (a < 0 || b < 0 || a >= l.drawings.size() || b >= l.drawings.size())
        return 0;
    QVector<int> types;
    QVector<QLineF> const suggested = Morphing::suggestedHints(l.drawings[a], l.drawings[b], &types);
    Morph morph = l.morphs[index];
    // (Not twice the same.)
    int added = 0;
    while (morph.hintTypes.size() < morph.hints.size())
        morph.hintTypes.push_back(Morph::Contour);
    for (int k = 0; k < suggested.size(); ++k) {
        if (morph.hints.contains(suggested[k]))
            continue;
        morph.hints.push_back(suggested[k]);
        morph.hintTypes.push_back(types.value(k, Morph::Contour));
        ++added;
    }
    if (added > 0)
        setMorph(layer, index, morph, tr("Suggest Hints"));
    return added;
}

bool Document::insertMorphKey(int layer, int frame, QString* why)
{
    auto refuse = [&](QString const& text) {
        if (why)
            *why = text;
        return false;
    };
    if (!validCell(m_scene, layer, frame))
        return refuse(tr("Please select one cell in a morphing."));
    Layer const& l = m_scene.layers[layer];
    int const index = l.morphIndexAt(frame);
    if (index < 0 || frame <= l.morphs[index].from)
        return refuse(tr("Please select one cell in a morphing."));
    Morph const& morph = l.morphs[index];
    if (!morph.groups.isEmpty())
        return refuse(tr("This function is implemented only for single layer morphing."));
    int const from = morph.from, to = l.morphEnd(index);
    double const t = morph.progress(double(frame - from) / double(to - from));
    Drawing made = l.shownAt(frame);
    made.set = l.drawings[l.drawingAt(from)].set;
    made.name.clear();
    // The hints go on through the new key drawing: to it where their points have got to by then, and on from there.
    Morph first = morph, second = morph;
    second.from = frame;
    second.velocity.clear();
    first.velocity.clear();
    first.easeIn = 0.0;
    second.easeOut = 0.0;
    for (int k = 0; k < morph.hints.size(); ++k) {
        QPointF const mid = morph.hints[k].pointAt(t);
        first.hints[k].setP2(mid);
        second.hints[k].setP1(mid);
    }
    apply(tr("Insert Morphing Key Drawing"), [&](Scene& s) {
        Layer& into = s.layers[layer];
        into.drawings.push_back(made);
        int const drawing = int(into.drawings.size()) - 1;
        for (int f = frame; f < to; ++f)
            into.cells[f] = drawing;
        into.morphs[index] = first;
        into.morphs.insert(index + 1, second);
    });
    return true;
}

bool Document::setMorphLength(int layer, int frame, int length)
{
    if (!validCell(m_scene, layer, frame) || length < 2)
        return false;
    Layer const& l = m_scene.layers[layer];
    int const index = l.morphIndexAt(frame);
    if (index < 0)
        return false;
    int const from = l.morphs[index].from, to = l.morphEnd(index);
    int const delta = length - (to - from);
    if (delta == 0)
        return false;
    apply(tr("Replace Morphing Length"), [&](Scene& s) {
        Layer& into = s.layers[layer];
        if (delta > 0)
            into.cells.insert(to, delta, into.cells[from]);
        else
            into.cells.remove(to + delta, -delta);
        into.shiftKeyExposures(delta > 0 ? to : to + delta, delta);
        for (Morph& m : into.morphs)
            if (m.from >= to)
                m.from += delta;
        int last = int(into.cells.size()) - 1;
        while (last >= s.frameCount() && into.cells[last] == Layer::kNoDrawing)
            --last;
        if (last >= s.frameCount())
            s.setFrameCount(last + 1);
        for (Layer& each : s.layers)
            each.cells.resize(s.frameCount(), Layer::kNoDrawing);
    });
    return true;
}

int Document::morphFrame(int layer, int frame, int which) const
{
    // Go to First / Previous / Next / Last Frame of the morphing `frame` is in (or starts or ends on).
    if (!validCell(m_scene, layer, frame))
        return -1;
    Layer const& l = m_scene.layers[layer];
    int index = l.morphIndexAt(frame);
    if (index < 0 && frame > 0)
        for (int i = 0; i < l.morphs.size(); ++i)
            if (l.morphEnd(i) == frame)
                index = i;
    if (index < 0)
        return -1;
    int const from = l.morphs[index].from, to = l.morphEnd(index);
    switch (which) {
    case 0: return from;
    case 1: return std::max(from, frame - 1);
    case 2: return std::min(to, frame + 1);
    default: return to;
    }
}
