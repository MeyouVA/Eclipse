// Eclipse - the document: palettes and their colours.
#include "model/document/Commands.h"

#include "model/document/CommandsPrivate.h"

int Document::addSwatch(QString const& name, QColor const& color, int palette)
{
    int id = Scene::kNoSwatch;
    apply(tr("Add Colour"), [&](Scene& s) { id = s.addSwatch(name, color, palette); });
    return id;
}

// What was drawn with the swatch keeps its colour and is linked to no swatch.
static void unlinkSwatch(Scene& s, int id, QColor const& color)
{
    auto strokes = [&](QVector<Stroke>& list) {
        for (Stroke& stroke : list)
            if (stroke.swatch == id) {
                stroke.swatch = Scene::kNoSwatch;
                stroke.color = color;
            }
    };
    auto fills = [&](QVector<Fill>& list) {
        for (Fill& fill : list)
            if (fill.swatch == id) {
                fill.swatch = Scene::kNoSwatch;
                fill.color = color;
            }
    };
    for (Layer& layer : s.layers)
        for (Drawing& d : layer.drawings) {
            for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
                strokes(d.strokesOf(sheet));
                fills(d.fillsOf(sheet));
            }
        }
}

int Document::addPalette(QString const& name)
{
    int at = -1;
    apply(tr("New Palette"), [&](Scene& s) {
        QString unique = name.trimmed().isEmpty() ? tr("Palette") : name.trimmed();
        for (int n = 2; s.paletteNames.contains(unique); ++n)
            unique = QStringLiteral("%1 %2").arg(name.trimmed().isEmpty() ? tr("Palette") : name.trimmed()).arg(n);
        s.paletteNames.push_back(unique);
        at = int(s.paletteNames.size()) - 1;
        s.addSwatch(tr("Black"), QColor(0, 0, 0), at);
    });
    return at;
}

void Document::renamePalette(int palette, QString const& name)
{
    QString const wanted = name.trimmed();
    if (palette < 0 || palette >= m_scene.paletteNames.size() || wanted.isEmpty() || m_scene.paletteNames.contains(wanted))
        return;
    apply(tr("Rename Palette"), [&](Scene& s) { s.paletteNames[palette] = wanted; });
}

void Document::removePalette(int palette)
{
    if (palette < 0 || palette >= m_scene.paletteNames.size() || m_scene.paletteNames.size() < 2)
        return;
    apply(tr("Remove Palette"), [&](Scene& s) {
        for (int i = int(s.palette.size()) - 1; i >= 0; --i) {
            if (s.palette[i].palette == palette) {
                unlinkSwatch(s, s.palette[i].id, s.palette[i].color);
                s.palette.removeAt(i);
            } else if (s.palette[i].palette > palette) {
                --s.palette[i].palette;
            }
        }
        s.paletteNames.removeAt(palette);
    });
}

void Document::editSwatch(int id, QString const& name, QColor const& color)
{
    if (!m_scene.findSwatch(id))
        return;
    apply(tr("Edit Colour"), [&](Scene& s) {
        for (Swatch& swatch : s.palette) {
            if (swatch.id == id) {
                swatch.name = name;
                swatch.color = color;
            }
        }
    });
}

void Document::setSwatchGradient(int id, Swatch::Kind kind, QVector<GradientStop> const& stops)
{
    Swatch const* const now = m_scene.findSwatch(id);
    if (!now)
        return;
    QVector<GradientStop> ordered;
    if (kind != Swatch::Solid) {
        for (GradientStop stop : stops) {
            if (!stop.color.isValid())
                continue;
            stop.at = std::clamp(stop.at, 0.0, 1.0);
            ordered.push_back(stop);
        }
        std::stable_sort(ordered.begin(), ordered.end(), [](GradientStop const& a, GradientStop const& b) { return a.at < b.at; });
        if (ordered.size() < 2)
            return;     // one colour is no gradient
    }
    if (now->kind == kind && now->stops == ordered)
        return;
    apply(tr("Edit Colour"), [&](Scene& s) {
        for (Swatch& swatch : s.palette) {
            if (swatch.id != id)
                continue;
            swatch.kind = kind;
            swatch.stops = ordered;
            if (!ordered.isEmpty())
                swatch.color = ordered.first().color;
        }
    });
}

bool Document::moveSwatch(int id, int by)
{
    int at = -1;
    for (int i = 0; i < m_scene.palette.size(); ++i)
        if (m_scene.palette[i].id == id)
            at = i;
    if (at < 0 || (by != -1 && by != 1))
        return false;
    // The next colour of the same palette that way.
    int other = at + by;
    while (other >= 0 && other < m_scene.palette.size() && m_scene.palette[other].palette != m_scene.palette[at].palette)
        other += by;
    if (other < 0 || other >= m_scene.palette.size())
        return false;
    apply(tr("Move Colour"), [&](Scene& s) { s.palette.swapItemsAt(at, other); });
    return true;
}

bool Document::setSwatchPalette(int id, int palette)
{
    Swatch const* const found = m_scene.findSwatch(id);
    if (!found || palette < 0 || palette >= m_scene.paletteNames.size() || found->palette == palette)
        return false;
    // A palette keeps one colour at least.
    int left = 0;
    for (Swatch const& swatch : m_scene.palette)
        left += swatch.palette == found->palette && swatch.id != id;
    if (left == 0)
        return false;
    apply(tr("Move Colour to Palette"), [&](Scene& s) {
        for (int i = 0; i < s.palette.size(); ++i) {
            if (s.palette[i].id == id) {
                Swatch swatch = s.palette.takeAt(i);
                swatch.palette = palette;
                s.palette.push_back(swatch);
                break;
            }
        }
    });
    return true;
}

bool Document::setSwatchProtected(int id, bool on)
{
    Swatch const* const found = m_scene.findSwatch(id);
    if (!found || found->isProtected == on)
        return false;
    apply(on ? tr("Protect Colour") : tr("Unprotect Colour"), [&](Scene& s) {
        for (Swatch& swatch : s.palette)
            if (swatch.id == id)
                swatch.isProtected = on;
    });
    return true;
}

void Document::removeSwatch(int id)
{
    Swatch const* const found = m_scene.findSwatch(id);
    if (!found)
        return;
    // A palette keeps one colour at least.
    int inPalette = 0;
    for (Swatch const& swatch : m_scene.palette)
        inPalette += swatch.palette == found->palette;
    if (inPalette <= 1)
        return;
    QColor const color = found->color;
    apply(tr("Delete Colour"), [&](Scene& s) {
        // Unlink what used it, keeping the colour.
        for (Layer& layer : s.layers) {
            for (Drawing& d : layer.drawings) {
                for (Stroke& stroke : d.strokes) {
                    if (stroke.swatch == id) {
                        stroke.swatch = Scene::kNoSwatch;
                        stroke.color = color;
                    }
                }
                for (Fill& fill : d.fills) {
                    if (fill.swatch == id) {
                        fill.swatch = Scene::kNoSwatch;
                        fill.color = color;
                    }
                }
                for (Stroke& stroke : d.colourStrokes) {
                    if (stroke.swatch == id) {
                        stroke.swatch = Scene::kNoSwatch;
                        stroke.color = color;
                    }
                }
                for (Fill& fill : d.colourFills) {
                    if (fill.swatch == id) {
                        fill.swatch = Scene::kNoSwatch;
                        fill.color = color;
                    }
                }
                for (int sheet : {int(Drawing::UnderlaySheet), int(Drawing::OverlaySheet)}) {
                    for (Stroke& stroke : d.strokesOf(sheet)) {
                        if (stroke.swatch == id) {
                            stroke.swatch = Scene::kNoSwatch;
                            stroke.color = color;
                        }
                    }
                    for (Fill& fill : d.fillsOf(sheet)) {
                        if (fill.swatch == id) {
                            fill.swatch = Scene::kNoSwatch;
                            fill.color = color;
                        }
                    }
                }
            }
        }
        for (int i = 0; i < s.palette.size(); ++i) {
            if (s.palette[i].id == id) {
                s.palette.removeAt(i);
                break;
            }
        }
    });
}
