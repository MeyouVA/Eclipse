// Eclipse - the document: palette operations - owners, order, clones, merging, recolouring, tints, overrides.
#include "model/document/Commands.h"

#include "model/document/CommandsPrivate.h"

#include <algorithm>

namespace {

// The colour it is a clone of at the root (a clone of a clone stands in for the first one).
int rootOf(Scene const& s, int id)
{
    for (int guard = 0; guard < 64; ++guard) {
        Swatch const* const found = s.findSwatch(id);
        if (!found || found->cloneOf == 0)
            return id;
        id = found->cloneOf;
    }
    return id;
}

// A swatch's values given to another: its colour, gradient and texture (not its name, palette or id).
void giveValues(Swatch& to, Swatch const& from)
{
    to.color = from.color;
    to.kind = from.kind;
    to.stops = from.stops;
    to.texture = from.texture;
}

QColor tinted(QColor const& c, QColor const& tint, double amount, bool offset)
{
    auto mix = [&](float a, float t) {
        float const v = offset ? a + float(amount) * t : a + float(amount) * (t - a);
        return std::clamp(v, 0.0f, 1.0f);
    };
    return QColor::fromRgbF(mix(c.redF(), tint.redF()), mix(c.greenF(), tint.greenF()), mix(c.blueF(), tint.blueF()), c.alphaF());
}

}  // namespace

bool Document::setPaletteOwner(int palette, int owner)
{
    if (palette < 0 || palette >= m_scene.paletteNames.size() || m_scene.paletteOwner(palette) == owner)
        return false;
    apply(owner == 0 ? tr("Make Scene Palette") : tr("Make Element Palette"), [&](Scene& s) {
        while (s.paletteOwners.size() < s.paletteNames.size())
            s.paletteOwners.push_back(0);
        s.paletteOwners[palette] = owner;
    });
    return true;
}

bool Document::movePalette(int palette, int by)
{
    int const to = palette + by;
    if (palette < 0 || palette >= m_scene.paletteNames.size() || to < 0 || to >= m_scene.paletteNames.size() || by == 0)
        return false;
    apply(by < 0 ? tr("Move Palette Up") : tr("Move Palette Down"), [&](Scene& s) { s.movePalette(palette, to); });
    return true;
}

int Document::clonePalette(int palette, QString const& name)
{
    if (palette < 0 || palette >= m_scene.paletteNames.size())
        return -1;
    int at = -1;
    apply(tr("Clone Palette"), [&](Scene& s) {
        QString unique = name.trimmed().isEmpty() ? tr("%1 Clone").arg(s.paletteNames[palette]) : name.trimmed();
        for (int n = 2; s.paletteNames.contains(unique); ++n)
            unique = QStringLiteral("%1 %2").arg(name.trimmed().isEmpty() ? tr("%1 Clone").arg(s.paletteNames[palette]) : name.trimmed()).arg(n);
        s.paletteNames.push_back(unique);
        at = int(s.paletteNames.size()) - 1;
        while (s.paletteOwners.size() < at)
            s.paletteOwners.push_back(0);
        s.paletteOwners.push_back(s.paletteOwner(palette));
        for (int id : s.paletteSwatches(palette)) {
            Swatch const original = *s.findSwatch(id);
            int const made = s.addSwatch(original.name, original.color, at);
            for (Swatch& mine : s.palette)
                if (mine.id == made) {
                    giveValues(mine, original);
                    mine.isProtected = original.isProtected;
                    mine.cloneOf = rootOf(s, id);
                }
        }
    });
    return at;
}

bool Document::mergePalette(int from, int into)
{
    int const count = int(m_scene.paletteNames.size());
    if (from < 0 || from >= count || into < 0 || into >= count || from == into)
        return false;
    apply(tr("Merge Palettes"), [&](Scene& s) {
        for (int id : s.paletteSwatches(from)) {
            Swatch const theirs = *s.findSwatch(id);
            int same = Scene::kNoSwatch;
            for (Swatch const& mine : s.palette)
                if (mine.palette == into && mine.name == theirs.name)
                    same = mine.id;
            if (same != Scene::kNoSwatch) {
                s.recolour(id, same);
                continue;
            }
            for (Swatch& moved : s.palette)
                if (moved.id == id)
                    moved.palette = into;
        }
        std::stable_sort(s.palette.begin(), s.palette.end(), [](Swatch const& a, Swatch const& b) { return a.palette < b.palette; });
        s.dropPalette(from);
    });
    return true;
}

int Document::paletteFromDrawings(QVector<int> const& layers, QString const& name, int owner)
{
    // The colours used, in the order first met: linked ones by their swatch, the others by value.
    QVector<QPair<int, QRgb>> found;
    auto meet = [&](int swatch, QColor const& colour) {
        QPair<int, QRgb> const key(swatch, swatch == Scene::kNoSwatch ? colour.rgba() : 0);
        if (!found.contains(key))
            found.push_back(key);
    };
    for (int l : layers) {
        if (l < 0 || l >= m_scene.layers.size())
            continue;
        for (Drawing const& d : m_scene.layers[l].drawings)
            for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
                for (Stroke const& stroke : d.strokesOf(sheet))
                    meet(stroke.swatch, stroke.color);
                for (Fill const& fill : d.fillsOf(sheet))
                    meet(fill.swatch, fill.color);
            }
    }
    if (found.isEmpty())
        return -1;
    int at = -1;
    apply(tr("New Palette from Selected Drawings"), [&](Scene& s) {
        QString unique = name.trimmed().isEmpty() ? tr("Palette") : name.trimmed();
        for (int n = 2; s.paletteNames.contains(unique); ++n)
            unique = QStringLiteral("%1 %2").arg(name.trimmed().isEmpty() ? tr("Palette") : name.trimmed()).arg(n);
        s.paletteNames.push_back(unique);
        at = int(s.paletteNames.size()) - 1;
        while (s.paletteOwners.size() < at)
            s.paletteOwners.push_back(0);
        s.paletteOwners.push_back(owner);
        QHash<QPair<int, QRgb>, int> made;
        int n = 1;
        for (QPair<int, QRgb> const& key : std::as_const(found)) {
            Swatch const* const old = key.first == Scene::kNoSwatch ? nullptr : s.findSwatch(key.first);
            QColor const value = old ? old->color : QColor::fromRgba(key.second);
            int const id = s.addSwatch(old ? old->name : tr("Colour %1").arg(n), value, at);
            ++n;
            if (old)
                for (Swatch& mine : s.palette)
                    if (mine.id == id)
                        giveValues(mine, *old);
            made.insert(key, id);
        }
        for (int l : layers) {
            if (l < 0 || l >= s.layers.size())
                continue;
            for (Drawing& d : s.layers[l].drawings)
                for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
                    auto relink = [&](int& swatch, QColor const& colour) {
                        QPair<int, QRgb> const key(swatch, swatch == Scene::kNoSwatch ? colour.rgba() : 0);
                        swatch = made.value(key, swatch);
                    };
                    for (Stroke& stroke : d.strokesOf(sheet))
                        relink(stroke.swatch, stroke.color);
                    for (Fill& fill : d.fillsOf(sheet))
                        relink(fill.swatch, fill.color);
                }
        }
    });
    return at;
}

bool Document::removeColours(QVector<int> const& swatches, QVector<int> const& palettes, QString const& text)
{
    if (swatches.isEmpty() && palettes.isEmpty())
        return false;
    apply(text, [&](Scene& s) {
        for (int id : swatches) {
            Swatch const* const found = s.findSwatch(id);
            if (!found)
                continue;
            if (s.paletteSwatches(found->palette).size() <= 1 && !palettes.contains(found->palette))
                continue;   // a palette keeps one colour at least
            QColor const value = found->color;
            for (Layer& layer : s.layers)
                for (Drawing& d : layer.drawings)
                    for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
                        for (Stroke& stroke : d.strokesOf(sheet))
                            if (stroke.swatch == id) {
                                stroke.swatch = Scene::kNoSwatch;
                                stroke.color = value;
                            }
                        for (Fill& fill : d.fillsOf(sheet))
                            if (fill.swatch == id) {
                                fill.swatch = Scene::kNoSwatch;
                                fill.color = value;
                            }
                    }
            for (Swatch& other : s.palette)
                if (other.cloneOf == id)
                    other.cloneOf = 0;
            s.palette.removeIf([id](Swatch const& sw) { return sw.id == id; });
        }
        // From the last place to the first, so that the places of the others hold.
        QVector<int> going = palettes;
        std::sort(going.begin(), going.end(), std::greater<int>());
        going.erase(std::unique(going.begin(), going.end()), going.end());
        for (int p : std::as_const(going))
            s.dropPalette(p);
    });
    return true;
}

int Document::recolour(int from, int to, QVector<int> const& layers)
{
    Scene tried = m_scene;
    int const count = tried.recolour(from, to, layers);
    if (count > 0)
        apply(tr("Recolour Drawings"), [&](Scene& s) { s.recolour(from, to, layers); });
    return count;
}

int Document::unpaintColour(int id, QVector<int> const& layers)
{
    int count = 0;
    for (int l = 0; l < m_scene.layers.size(); ++l)
        if (layers.isEmpty() || layers.contains(l))
            for (Drawing const& d : m_scene.layers[l].drawings)
                for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
                    for (Stroke const& stroke : d.strokesOf(sheet))
                        count += stroke.swatch == id && stroke.width > 0.0f;
                    for (Fill const& fill : d.fillsOf(sheet))
                        count += fill.swatch == id;
                }
    if (count == 0)
        return 0;
    apply(tr("Unpaint Regions"), [&](Scene& s) {
        for (int l = 0; l < s.layers.size(); ++l)
            if (layers.isEmpty() || layers.contains(l))
                for (Drawing& d : s.layers[l].drawings)
                    for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
                        for (Stroke& stroke : d.strokesOf(sheet))
                            if (stroke.swatch == id)
                                stroke.width = 0.0f;
                        d.fillsOf(sheet).removeIf([id](Fill const& f) { return f.swatch == id; });
                    }
    });
    return count;
}

bool Document::tintColours(QVector<int> const& swatches, QColor const& tint, double amount, bool offset)
{
    if (swatches.isEmpty() || !tint.isValid() || amount == 0.0)
        return false;
    apply(tr("Tint Colours"), [&](Scene& s) {
        for (Swatch& swatch : s.palette) {
            if (!swatches.contains(swatch.id))
                continue;
            swatch.color = tinted(swatch.color, tint, amount, offset);
            for (GradientStop& stop : swatch.stops)
                stop.color = tinted(stop.color, tint, amount, offset);
        }
    });
    return true;
}

QVector<int> Document::addColours(QVector<Swatch> const& colours, int palette, bool clones)
{
    QVector<int> ids;
    if (colours.isEmpty() || palette < 0 || palette >= m_scene.paletteNames.size())
        return ids;
    apply(clones ? tr("Paste As Clones") : tr("Paste As New Colours"), [&](Scene& s) {
        for (Swatch const& theirs : colours) {
            int const id = s.addSwatch(theirs.name, theirs.color, palette);
            for (Swatch& mine : s.palette)
                if (mine.id == id) {
                    giveValues(mine, theirs);
                    mine.isProtected = theirs.isProtected;
                    // A clone of a colour this scene has (or of what that one clones).
                    mine.cloneOf = clones && s.findSwatch(theirs.id) ? rootOf(s, theirs.id) : 0;
                }
            ids.push_back(id);
        }
    });
    return ids;
}

bool Document::setColourValues(QVector<int> const& targets, QVector<Swatch> const& values)
{
    if (targets.isEmpty() || values.isEmpty())
        return false;
    apply(tr("Paste Colour Values"), [&](Scene& s) {
        for (int i = 0; i < targets.size(); ++i)
            for (Swatch& mine : s.palette)
                if (mine.id == targets[i])
                    giveValues(mine, values[std::min(i, int(values.size()) - 1)]);
    });
    return true;
}

bool Document::setPaletteOverrides(int layer, QVector<int> const& palettes)
{
    if (layer < 0 || layer >= m_scene.layers.size() || m_scene.layers[layer].paletteOverrides == palettes)
        return false;
    apply(tr("Palette Overrides"), [&](Scene& s) { s.layers[layer].paletteOverrides = palettes; });
    return true;
}

int Document::recoverColours()
{
    Scene tried = m_scene;
    int const count = tried.recoverColours();
    if (count > 0)
        apply(tr("Recover Colours"), [&](Scene& s) { s.recoverColours(); });
    return count;
}

QHash<int, int> Document::bringColours(QVector<Swatch> const& colours)
{
    QHash<int, int> map;
    if (colours.isEmpty())
        return map;
    apply(tr("Recover Colours"), [&](Scene& s) {
        int recovered = -1;
        for (Swatch const& theirs : colours) {
            int mine = Scene::kNoSwatch;
            for (Swatch const& have : std::as_const(s.palette))
                if (have.name == theirs.name && have.color == theirs.color)
                    mine = have.id;
            if (mine == Scene::kNoSwatch) {
                if (recovered < 0) {
                    recovered = int(s.paletteNames.indexOf(QStringLiteral("Recovered Palette")));
                    if (recovered < 0) {
                        s.paletteNames.push_back(QStringLiteral("Recovered Palette"));
                        recovered = int(s.paletteNames.size()) - 1;
                    }
                }
                mine = s.addSwatch(theirs.name, theirs.color, recovered);
                for (Swatch& made : s.palette)
                    if (made.id == mine)
                        giveValues(made, theirs);
            }
            map.insert(theirs.id, mine);
        }
    });
    return map;
}
