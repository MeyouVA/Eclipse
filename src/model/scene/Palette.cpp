// Eclipse - the scene: the palette - colours and gradients, and what an area or a line is painted with.
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "model/scene/Scene.h"

#include <QLinearGradient>
#include <QRadialGradient>

#include <QJsonArray>
#include <QLineF>

#include <algorithm>
#include <cmath>
#include <cstdio>

Swatch const* Scene::findSwatch(int id) const
{
    for (Swatch const& s : palette)
        if (s.id == id)
            return &s;
    return nullptr;
}

QJsonObject Scene::paletteJson(int inPalette) const
{
    QJsonArray colours;
    for (Swatch const& swatch : palette) {
        if (swatch.palette != inPalette)
            continue;
        QJsonObject colour;
        colour[QStringLiteral("name")] = swatch.name;
        colour[QStringLiteral("color")] = swatch.color.name(QColor::HexArgb);
        if (swatch.isGradient()) {
            colour[QStringLiteral("kind")] = int(swatch.kind);
            colour[QStringLiteral("stops")] = swatch.stopsJson();
        }
        if (swatch.isProtected)
            colour[QStringLiteral("protected")] = true;
        colours.push_back(colour);
    }
    QJsonObject root;
    root[QStringLiteral("eclipsePalette")] = 1;
    root[QStringLiteral("name")] = paletteNames.value(inPalette);
    root[QStringLiteral("colours")] = colours;
    return root;
}

bool Scene::takePaletteJson(int inPalette, QJsonObject const& root)
{
    QJsonArray const colours = root[QStringLiteral("colours")].toArray();
    if (!root.contains(QStringLiteral("eclipsePalette")) || colours.isEmpty() || inPalette < 0 || inPalette >= paletteNames.size())
        return false;
    for (QJsonValue const& value : colours) {
        QJsonObject const colour = value.toObject();
        QString const name = colour[QStringLiteral("name")].toString();
        QColor const c(colour[QStringLiteral("color")].toString());
        int id = kNoSwatch;
        for (Swatch const& swatch : std::as_const(palette))
            if (swatch.palette == inPalette && swatch.name == name)
                id = swatch.id;
        if (id == kNoSwatch)
            id = addSwatch(name, c.isValid() ? c : QColor(Qt::gray), inPalette);
        for (Swatch& swatch : palette) {
            if (swatch.id != id)
                continue;
            if (c.isValid())
                swatch.color = c;
            swatch.setStopsJson(colour[QStringLiteral("stops")].toArray());
            swatch.kind = swatch.stops.size() >= 2 ? Swatch::Kind(std::clamp(colour[QStringLiteral("kind")].toInt(1), 1, 2)) : Swatch::Solid;
            swatch.isProtected = colour[QStringLiteral("protected")].toBool(false);
        }
    }
    return true;
}

bool Scene::writePaletteFile(int inPalette, QString const& path) const
{
    QFile file(path);
    return inPalette >= 0 && inPalette < paletteNames.size() && file.open(QIODevice::WriteOnly | QIODevice::Truncate)
           && file.write(QJsonDocument(paletteJson(inPalette)).toJson()) > 0;
}

bool Scene::readPaletteFile(int inPalette, QString const& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) && takePaletteJson(inPalette, QJsonDocument::fromJson(file.readAll()).object());
}

bool Scene::protects(int swatch) const
{
    Swatch const* const found = findSwatch(swatch);
    return found && found->isProtected;
}

QColor Swatch::colourAt(QVector<GradientStop> const& stops, double at)
{
    if (stops.isEmpty())
        return QColor(Qt::black);
    if (at <= stops.first().at)
        return stops.first().color;
    for (int i = 0; i + 1 < stops.size(); ++i) {
        GradientStop const& a = stops[i];
        GradientStop const& b = stops[i + 1];
        if (at > b.at)
            continue;
        double const t = b.at - a.at > 1e-9 ? (at - a.at) / (b.at - a.at) : 0.0;
        return QColor::fromRgbF(float(a.color.redF() + (b.color.redF() - a.color.redF()) * t), float(a.color.greenF() + (b.color.greenF() - a.color.greenF()) * t),
                                float(a.color.blueF() + (b.color.blueF() - a.color.blueF()) * t), float(a.color.alphaF() + (b.color.alphaF() - a.color.alphaF()) * t));
    }
    return stops.last().color;
}

QBrush Swatch::brush(QPointF from, QPointF to) const
{
    if (!isGradient())
        return QBrush(color);
    if (kind == Radial) {
        QRadialGradient gradient(from, std::max(1.0, QLineF(from, to).length()));
        for (GradientStop const& stop : stops)
            gradient.setColorAt(std::clamp(stop.at, 0.0, 1.0), stop.color);
        return QBrush(gradient);
    }
    // A line of no length has no direction: a hair's breadth across, so that there is one.
    QLinearGradient gradient(from, QLineF(from, to).length() < 1e-6 ? to + QPointF(1e-3, 0) : to);
    for (GradientStop const& stop : stops)
        gradient.setColorAt(std::clamp(stop.at, 0.0, 1.0), stop.color);
    return QBrush(gradient);
}

QJsonArray Swatch::stopsJson() const
{
    QJsonArray array;
    for (GradientStop const& stop : stops)
        array.append(QJsonArray{stop.at, stop.color.name(QColor::HexArgb)});
    return array;
}

void Swatch::setStopsJson(QJsonArray const& array)
{
    stops.clear();
    for (QJsonValue const& value : array) {
        QJsonArray const pair = value.toArray();
        QColor const c(pair.at(1).toString());
        if (pair.size() == 2 && c.isValid())
            stops.push_back({std::clamp(pair.at(0).toDouble(), 0.0, 1.0), c});
    }
    std::stable_sort(stops.begin(), stops.end(), [](GradientStop const& a, GradientStop const& b) { return a.at < b.at; });
}

void Scene::gradientLine(Fill const& fill, QPointF& from, QPointF& to) const
{
    if (fill.gradPlaced) {
        from = fill.gradFrom;
        to = fill.gradTo;
        return;
    }
    Swatch const* const s = findSwatch(fill.swatch);
    QRectF const box = fill.bounds();
    if (s && s->kind == Swatch::Radial) {
        // From the middle of the area out to its furthest side.
        from = box.center();
        to = from + QPointF(std::max(box.width(), box.height()) / 2.0, 0);
    } else {
        from = QPointF(box.left(), box.center().y());
        to = QPointF(box.right(), box.center().y());
    }
}

QBrush Scene::brushFor(Fill const& fill) const
{
    Swatch const* const s = findSwatch(fill.swatch);
    if (!s)
        return QBrush(fill.color);
    if (!s->isGradient())
        return QBrush(s->color);
    QPointF from, to;
    gradientLine(fill, from, to);
    return s->brush(from, to);
}

QBrush Scene::brushFor(Stroke const& stroke) const
{
    Swatch const* const s = findSwatch(stroke.swatch);
    if (!s)
        return QBrush(stroke.color);
    if (!s->isGradient())
        return QBrush(s->color);
    // Along a line the gradient goes from the one end of what it covers to the other.
    QRectF const box = stroke.bounds();
    if (s->kind == Swatch::Radial)
        return s->brush(box.center(), box.center() + QPointF(std::max(box.width(), box.height()) / 2.0, 0));
    return s->brush(QPointF(box.left(), box.center().y()), QPointF(box.right(), box.center().y()));
}

QColor Scene::resolve(int swatch, QColor const& fallback) const
{
    Swatch const* const s = findSwatch(swatch);
    return s ? s->color : fallback;
}

int Scene::addSwatch(QString const& name, QColor const& color, int inPalette)
{
    Swatch s;
    s.id = m_nextSwatchId++;
    s.name = name;
    s.color = color;
    s.palette = std::clamp(inPalette, 0, int(paletteNames.size()) - 1);
    // After the last colour of its palette, so that the colours of a palette stay together.
    int at = int(palette.size());
    for (int i = 0; i < palette.size(); ++i)
        if (palette[i].palette > s.palette) {
            at = i;
            break;
        }
    palette.insert(at, s);
    return s.id;
}
