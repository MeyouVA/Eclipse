// Eclipse - the scene: palettes moved, taken out, overridden; colours replaced and recovered.
#include "model/scene/Scene.h"

#include <algorithm>

#include <QSet>

namespace {

QColor s_replacement;
int s_pasteColours = Scene::ReuseColours;

// Every line and area of a drawing, on every sheet.
template <typename Fn>
void eachArt(Drawing& d, Fn&& fn)
{
    for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
        for (Stroke& stroke : d.strokesOf(sheet))
            fn(stroke.swatch, stroke.color);
        for (Fill& fill : d.fillsOf(sheet))
            fn(fill.swatch, fill.color);
    }
}

// A palette's place after one at `gone` is taken out (-1: it was that one).
int afterDrop(int place, int gone)
{
    return place == gone ? -1 : place > gone ? place - 1 : place;
}

}  // namespace

void Scene::setReplacementColour(QColor const& colour)
{
    s_replacement = colour;
}

QColor Scene::replacementColour()
{
    return s_replacement;
}

QVector<int> Scene::paletteSwatches(int which) const
{
    QVector<int> ids;
    for (Swatch const& s : palette)
        if (s.palette == which)
            ids.push_back(s.id);
    return ids;
}

void Scene::movePalette(int from, int to)
{
    int const count = int(paletteNames.size());
    if (from < 0 || from >= count || to < 0 || to >= count || from == to)
        return;
    while (paletteLinks.size() < count)
        paletteLinks.push_back(QString());
    while (paletteOwners.size() < count)
        paletteOwners.push_back(0);
    // Where each place goes.
    QVector<int> order(count);
    for (int i = 0; i < count; ++i)
        order[i] = i;
    order.move(from, to);
    QVector<int> placeOf(count);
    for (int i = 0; i < count; ++i)
        placeOf[order[i]] = i;
    paletteNames.move(from, to);
    paletteLinks.move(from, to);
    paletteOwners.move(from, to);
    for (Swatch& s : palette)
        s.palette = placeOf.value(s.palette, s.palette);
    for (Layer& layer : layers)
        for (int& p : layer.paletteOverrides)
            p = placeOf.value(p, p);
    // The colours of a palette stay together, the palettes in their order.
    std::stable_sort(palette.begin(), palette.end(), [](Swatch const& a, Swatch const& b) { return a.palette < b.palette; });
}

void Scene::dropPalette(int which)
{
    if (which < 0 || which >= paletteNames.size() || paletteNames.size() < 2)
        return;
    QHash<int, QColor> gone;
    for (int i = int(palette.size()) - 1; i >= 0; --i) {
        if (palette[i].palette == which) {
            gone.insert(palette[i].id, palette[i].color);
            palette.removeAt(i);
        } else {
            palette[i].palette = afterDrop(palette[i].palette, which);
        }
    }
    // What was drawn with its colours keeps their values; clones of them are clones of nothing now.
    for (Layer& layer : layers)
        for (Drawing& d : layer.drawings)
            eachArt(d, [&](int& swatch, QColor& colour) {
                auto found = gone.constFind(swatch);
                if (found == gone.constEnd())
                    return;
                colour = found.value();
                swatch = kNoSwatch;
            });
    for (Swatch& s : palette)
        if (gone.contains(s.cloneOf))
            s.cloneOf = 0;
    for (Layer& layer : layers) {
        QVector<int> kept;
        for (int p : std::as_const(layer.paletteOverrides))
            if (int const now = afterDrop(p, which); now >= 0)
                kept.push_back(now);
        layer.paletteOverrides = kept;
    }
    paletteNames.removeAt(which);
    if (which < paletteLinks.size())
        paletteLinks.removeAt(which);
    if (which < paletteOwners.size())
        paletteOwners.removeAt(which);
}

QSet<int> Scene::usedSwatches() const
{
    QSet<int> used;
    for (Layer const& layer : layers)
        for (Drawing d : layer.drawings)
            eachArt(d, [&](int& swatch, QColor&) {
                if (swatch != kNoSwatch)
                    used.insert(swatch);
            });
    return used;
}

int Scene::recolour(int from, int to, QVector<int> const& only)
{
    Swatch const* const target = findSwatch(to);
    if (!target || from == to)
        return 0;
    QColor const value = target->color;
    int count = 0;
    for (int l = 0; l < layers.size(); ++l) {
        if (!only.isEmpty() && !only.contains(l))
            continue;
        for (Drawing& d : layers[l].drawings)
            eachArt(d, [&](int& swatch, QColor& colour) {
                if (swatch != from)
                    return;
                swatch = to;
                colour = value;
                ++count;
            });
    }
    return count;
}

QHash<int, int> Scene::overridesOf(int layer) const
{
    QHash<int, int> map;
    if (layer < 0 || layer >= layers.size())
        return map;
    // The first override in the list wins.
    QVector<int> const& list = layers[layer].paletteOverrides;
    for (int i = int(list.size()) - 1; i >= 0; --i)
        for (Swatch const& s : palette)
            if (s.palette == list[i] && s.cloneOf != 0)
                map.insert(s.cloneOf, s.id);
    return map;
}

int Scene::recoverColours()
{
    QHash<QRgb, int> made;
    int recovered = -1;
    int count = 0;
    for (Layer& layer : layers)
        for (Drawing& d : layer.drawings)
            eachArt(d, [&](int& swatch, QColor& colour) {
                if (swatch == kNoSwatch || findSwatch(swatch))
                    return;
                if (recovered < 0) {
                    recovered = int(paletteNames.indexOf(QStringLiteral("Recovered Palette")));
                    if (recovered < 0) {
                        paletteNames.push_back(QStringLiteral("Recovered Palette"));
                        recovered = int(paletteNames.size()) - 1;
                    }
                }
                auto found = made.constFind(colour.rgba());
                if (found == made.constEnd()) {
                    found = made.insert(colour.rgba(), addSwatch(QStringLiteral("Recovered %1").arg(made.size() + 1), colour, recovered));
                    ++count;
                }
                swatch = found.value();
            });
    return count;
}

void Scene::setPasteColours(int mode)
{
    s_pasteColours = std::clamp(mode, int(ReuseColours), int(NewColours));
}

int Scene::pasteColours()
{
    return s_pasteColours;
}

int Scene::takeColour(Scene const& piece, int swatch, QColor const& colour, QHash<int, int>& map)
{
    Q_UNUSED(colour);
    if (swatch == kNoSwatch)
        return kNoSwatch;
    if (auto known = map.constFind(swatch); known != map.constEnd())
        return known.value();
    Swatch const* const theirs = piece.findSwatch(swatch);
    if (!theirs)
        return kNoSwatch;   // a colour the piece itself did not know: the artwork keeps its own
    // Where a new colour goes: the scene's first palette, or (copying palettes) one named as theirs.
    int place = 0;
    if (s_pasteColours == CopyPalettes) {
        QString const name = piece.paletteNames.value(theirs->palette, QStringLiteral("Palette"));
        place = int(paletteNames.indexOf(name));
        if (place < 0) {
            paletteNames.push_back(name);
            place = int(paletteNames.size()) - 1;
        }
    }
    int mine = kNoSwatch;
    for (Swatch const& have : std::as_const(palette)) {
        bool const same = s_pasteColours == UpdateColours ? have.name == theirs->name
                          : s_pasteColours == CopyPalettes ? have.palette == place && have.name == theirs->name && have.color == theirs->color
                          : s_pasteColours == ReuseColours ? have.name == theirs->name && have.color == theirs->color
                                                            : false;
        if (same) {
            mine = have.id;
            break;
        }
    }
    bool const fresh = mine == kNoSwatch;
    if (fresh)
        mine = addSwatch(theirs->name, theirs->color, place);
    if (fresh || s_pasteColours == UpdateColours)
        for (Swatch& s : palette)
            if (s.id == mine) {
                s.color = theirs->color;
                s.kind = theirs->kind;
                s.stops = theirs->stops;
                s.texture = theirs->texture;
                if (fresh)
                    s.isProtected = theirs->isProtected;
            }
    map.insert(swatch, mine);
    return mine;
}

QPair<int, int> Scene::optimizeForTemplate()
{
    int drawingsGone = 0, palettesGone = 0;
    // Drawings: each drawing layer keeps those its frames show (the ones kept in its bank stay kept).
    auto trim = [&](QVector<Layer>& all) {
        for (Layer& layer : all) {
            if (!layer.holdsDrawings() || layer.showsSymbol())
                continue;
            QSet<quint64> pinned;
            for (Drawing& d : layer.drawings) {
                if (d.pinned)
                    pinned.insert(d.editIdentity);
                d.pinned = false;
            }
            int const before = int(layer.drawings.size());
            layer.compact();
            drawingsGone += before - int(layer.drawings.size());
            for (Drawing& d : layer.drawings)
                d.pinned = pinned.contains(d.editIdentity);
        }
    };
    trim(layers);
    for (Symbol& symbol : symbols)
        trim(symbol.layers);
    refreshSymbolInstances();
    // Palettes: one goes only when nothing draws with its colours, no colour clones one of them, no layer overrides
    // with it, and no Colour-Override effect could name its colours.
    QSet<int> used;
    bool overrideEffects = false, nestedOverrides = false;
    auto look = [&](QVector<Layer>& all, bool nested) {
        for (Layer& layer : all) {
            for (Drawing& d : layer.drawings)
                eachArt(d, [&](int& swatch, QColor&) {
                    if (swatch != kNoSwatch)
                        used.insert(swatch);
                });
            for (Effect const& e : layer.effects)
                overrideEffects = overrideEffects || e.type == Effect::ColourOverride;
            nestedOverrides = nestedOverrides || (nested && !layer.paletteOverrides.isEmpty());
        }
    };
    look(layers, false);
    for (Symbol& symbol : symbols)
        look(symbol.layers, true);
    for (GraphNode const& node : graph.nodes)
        overrideEffects = overrideEffects || node.effect.type == Effect::ColourOverride;
    if (overrideEffects || nestedOverrides)
        return {drawingsGone, palettesGone};
    for (Swatch const& s : palette)
        if (s.cloneOf)
            used.insert(s.cloneOf);
    for (int p = int(paletteNames.size()) - 1; p >= 0 && paletteNames.size() > 1; --p) {
        bool needed = false;
        for (int id : paletteSwatches(p))
            needed = needed || used.contains(id);
        for (Layer const& layer : layers)
            needed = needed || layer.paletteOverrides.contains(p);
        if (needed)
            continue;
        dropPalette(p);
        ++palettesGone;
    }
    return {drawingsGone, palettesGone};
}
