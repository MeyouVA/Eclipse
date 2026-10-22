// Eclipse - the scene: palettes moved, taken out, overridden; colours replaced and recovered.
#include "model/scene/Scene.h"

#include <algorithm>

namespace {

QColor s_replacement;

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
