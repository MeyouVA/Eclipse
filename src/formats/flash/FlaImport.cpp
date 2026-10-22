// Eclipse - the Flash document reader.
#include "formats/flash/FlaImport.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QHash>
#include <QMap>
#include <QPolygonF>
#include <QTransform>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QXmlStreamReader>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <functional>
#include <memory>

#include "model/geometry/Morph.h"
#include "model/scene/Scene.h"
#include "formats/flash/FlashCut.h"
#include "formats/flash/FlashVideo.h"
#include "formats/flash/SwfImport.h"
#include "formats/archive/Zip.h"
#include "formats/flash/FlaParts.h"
#include "formats/flash/FlaReader.h"

using namespace fla;

namespace {

// What a drawing is, to find another the same.
QByteArray fingerprint(Drawing const& d)
{
    QByteArray bytes;
    QDataStream s(&bytes, QIODevice::WriteOnly);
    for (Fill const& f : d.fills) {
        s << f.color.rgba() << f.order;
        for (QPolygonF const& o : f.outlines)
            s << o;
    }
    for (Stroke const& st : d.strokes) {
        s << st.color.rgba() << st.width << st.order;
        for (StrokePoint const& p : st.points)
            s << p.pos;
    }
    for (Picture const& p : d.pictures)
        s << p.image.cacheKey() << p.placement;
    return QCryptographicHash::hash(bytes, QCryptographicHash::Md5);
}

}  // namespace

namespace FlaImport {

bool read(QByteArray const& bytes, Document& out, QString* error, QString const& mediaFolder, Progress const& progress)
{
    auto fail = [&](QString const& why) {
        if (error)
            *error = why;
        return false;
    };
    if (!bytes.startsWith("PK"))
        return fail(tr("This .fla is not the kind saved by Flash CS5 or after (a zip of XML): it may be from an older Flash. "
                       "Open it in Flash or Animate and save it again, or export a .swf and import that."));
    QHash<QString, QByteArray> const files = Zip::read(bytes);
    if (!files.contains(QStringLiteral("DOMDocument.xml")))
        return fail(tr("The .fla could not be read (no DOMDocument.xml in it)."));
    Node document;
    if (!parse(files.value(QStringLiteral("DOMDocument.xml")), document))
        return fail(tr("The .fla's document could not be read."));
    out = Document();
    out.size = QSize(int(document.number(QStringLiteral("width"), 550)), int(document.number(QStringLiteral("height"), 400)));
    out.fps = document.number(QStringLiteral("frameRate"), 24.0);
    out.background = QColor(document.attr(QStringLiteral("backgroundColor"), QStringLiteral("#FFFFFF")));
    Reader reader;
    reader.out = &out;
    reader.files = &files;
    reader.mediaFolder = mediaFolder;
    QTemporaryDir passing;
    reader.passing = &passing;
    QHash<QString, Sound> sounds;
    if (Node const* media = document.child(QStringLiteral("media")))
        for (Node const& item : media->children) {
            if (item.name == QLatin1String("DOMBitmapItem")) {
                reader.bitmapFiles.insert(item.attr(QStringLiteral("name")), item.attr(QStringLiteral("bitmapDataHRef")));
            } else if (item.name == QLatin1String("DOMVideoItem")) {
                reader.videoItems.insert(item.attr(QStringLiteral("name")), item);
            } else if (item.name == QLatin1String("DOMSoundItem")) {
                QByteArray const data = files.value(QStringLiteral("bin/") + item.attr(QStringLiteral("soundDataHRef")));
                QString const format = item.attr(QStringLiteral("format"));
                int const rate = format.startsWith(QLatin1String("5kHz")) ? 5512
                                 : format.startsWith(QLatin1String("11kHz")) ? 11025
                                 : format.startsWith(QLatin1String("22kHz")) ? 22050
                                                                              : 44100;
                int const bits = format.contains(QLatin1String("8bit")) ? 8 : 16;
                int const channels = format.contains(QLatin1String("Stereo")) ? 2 : 1;
                qint64 const samples = qint64(item.number(QStringLiteral("sampleCount"), 0.0));
                Sound sound;
                sound.name = item.attr(QStringLiteral("name"));
                if (data.startsWith("ID3") || (data.size() > 2 && quint8(data[0]) == 0xff && (quint8(data[1]) & 0xe0) == 0xe0
                                               && data.size() != samples * channels * bits / 8)) {
                    sound.bytes = data;
                    sound.suffix = QStringLiteral("mp3");
                } else if (!data.isEmpty()) {
                    sound.bytes = SwfImport::wav(data, rate, bits, channels);
                    sound.suffix = QStringLiteral("wav");
                }
                if (!sound.bytes.isEmpty())
                    sounds.insert(sound.name, sound);
            }
        }
    // (In the order of their file names: two of one name, the same one kept every time.)
    QStringList names = files.keys();
    names.sort();
    for (QString const& file : std::as_const(names)) {
        if (!file.startsWith(QLatin1String("LIBRARY/")) || !file.endsWith(QLatin1String(".xml")))
            continue;
        Node symbol;
        if (parse(files.value(file), symbol) && symbol.name == QLatin1String("DOMSymbolItem"))
            reader.symbols.insert(symbol.attr(QStringLiteral("name")), symbol);
    }
    Node const* timelines = document.child(QStringLiteral("timelines"));
    Node const* main = timelines && !timelines->children.isEmpty() ? &timelines->children.first() : nullptr;
    Node const* layers = main ? main->child(QStringLiteral("layers")) : nullptr;
    if (!layers)
        return fail(tr("The .fla has no timeline."));
    out.frames = reader.length(*main);
    // Each layer of the main timeline, from the bottom up.
    QHash<int, int> made;       // the index of a layer in the document: its index in `out.layers`
    QHash<int, int> liftedFrom; // a layer of what had an effect: the layer it came from (in `out.layers`)
    qint64 const all = qint64(layers->children.size()) * std::max(1, out.frames);
    for (int l = int(layers->children.size()) - 1; l >= 0; --l) {
        Node const& layerNode = layers->children[l];
        qint64 const before = qint64(layers->children.size() - 1 - l) * std::max(1, out.frames);
        if (progress && !progress(before, all))
            return fail(tr("The import was stopped."));
        QString const type = layerNode.attr(QStringLiteral("layerType"));
        if (type == QLatin1String("guide") || type == QLatin1String("folder"))
            continue;
        Layer layer;
        layer.name = layerNode.attr(QStringLiteral("name"), QStringLiteral("Layer"));
        layer.visible = layerNode.attr(QStringLiteral("visible")) != QLatin1String("false");
        layer.locked = layerNode.attr(QStringLiteral("locked")) == QLatin1String("true");
        layer.colour = QColor(layerNode.attr(QStringLiteral("color")));
        layer.mask = type == QLatin1String("mask");
        layer.cells.fill(-1, out.frames);
        QHash<QByteArray, int> known;
        // What on this layer has an effect: a layer for each effect, filled in frame by frame as the layer is.
        QMap<QString, Layer> lifted;
        QHash<QString, QHash<QByteArray, int>> liftedKnown;
        Node const* frames = layerNode.child(QStringLiteral("frames"));
        for (int k = 0; frames && k < frames->children.size(); ++k) {
            Node const& f = frames->children[k];
            int const index = int(f.number(QStringLiteral("index"), 0)), duration = int(f.number(QStringLiteral("duration"), 1));
            // Its label; its script (kept as a note: it is not run); its sound.
            if (f.attributes.contains(QStringLiteral("name")) && type != QLatin1String("guide"))
                out.labels.push_back({index, std::max(1, duration), f.attr(QStringLiteral("name")), f.attr(QStringLiteral("labelType")) == QLatin1String("comment"),
                                      QString()});
            if (Node const* actions = f.child(QStringLiteral("Actionscript"))) {
                Node const* script = actions->child(QStringLiteral("script"));
                QString const code = script ? script->attr(QStringLiteral("#text")).trimmed() : QString();
                if (!code.isEmpty())
                    out.labels.push_back({index, 1, tr("Script"), true, code});
            }
            if (f.attributes.contains(QStringLiteral("soundName")) && f.attr(QStringLiteral("soundSync")) != QLatin1String("stop")) {
                QString const name = f.attr(QStringLiteral("soundName"));
                if (sounds.contains(name)) {
                    Sound sound = sounds.value(name);
                    sound.frame = index;
                    out.sounds.push_back(sound);
                } else {
                    ++out.skippedSounds;
                }
            }
            Node const* elements = f.child(QStringLiteral("elements"));
            if (!elements || elements->children.isEmpty())
                continue;
            for (int frame = index; frame < std::min(index + duration, out.frames); ++frame) {
                if (progress && !progress(before + frame, all))
                    return fail(tr("The import was stopped."));
                Drawing drawing;
                int order = 0;
                QMap<QString, Drawing> frameSides;
                reader.sides = &frameSides;
                reader.layer(layerNode, frame, QTransform(), Tint(), drawing, order, 0);
                reader.sides = nullptr;
                for (auto it = frameSides.cbegin(); it != frameSides.cend(); ++it) {
                    Drawing const& side = it.value();
                    if (side.fills.isEmpty() && side.strokes.isEmpty() && side.pictures.isEmpty())
                        continue;
                    Layer& extra = lifted[it.key()];
                    if (extra.cells.isEmpty()) {
                        extra.name = QStringLiteral("%1 (%2)").arg(layer.name, it.key());
                        extra.visible = layer.visible;
                        extra.locked = layer.locked;
                        extra.colour = layer.colour;
                        extra.cells.fill(-1, out.frames);
                        extra.blend = reader.effects.value(it.key()).blend;
                        extra.filters = reader.effects.value(it.key()).filters;
                    }
                    QHash<QByteArray, int>& seen = liftedKnown[it.key()];
                    QByteArray const print = fingerprint(side);
                    auto at = seen.constFind(print);
                    if (at == seen.cend()) {
                        at = seen.insert(print, int(extra.drawings.size()));
                        extra.drawings.push_back(side);
                    }
                    extra.cells[frame] = *at;
                }
                if (drawing.fills.isEmpty() && drawing.strokes.isEmpty() && drawing.pictures.isEmpty())
                    continue;
                QByteArray const print = fingerprint(drawing);
                auto found = known.constFind(print);
                if (found == known.cend()) {
                    found = known.insert(print, int(layer.drawings.size()));
                    layer.drawings.push_back(drawing);
                }
                layer.cells[frame] = *found;
            }
        }
        if (layer.mask && layer.cells.contains(-1)) {
            Drawing everything;
            Fill stage;
            stage.color = Qt::black;
            stage.outlines = {QPolygonF(QRectF(-4.0 * out.size.width(), -4.0 * out.size.height(), 9.0 * out.size.width(), 9.0 * out.size.height()))};
            everything.fills = {stage};
            int const whole = int(layer.drawings.size());
            layer.drawings.push_back(everything);
            for (int& cell : layer.cells)
                if (cell == -1)
                    cell = whole;
        }
        made.insert(l, int(out.layers.size()));
        out.layers.push_back(layer);
        for (Layer const& extra : std::as_const(lifted)) {
            liftedFrom.insert(int(out.layers.size()), made[l]);
            out.layers.push_back(extra);
        }
    }
    // Which layer masks which (a masked layer names the mask layer above it).
    for (int l = int(layers->children.size()) - 1; l >= 0; --l) {
        Node const& layerNode = layers->children[l];
        if (!made.contains(l) || !layerNode.attributes.contains(QStringLiteral("parentLayerIndex")))
            continue;
        int const parent = int(layerNode.number(QStringLiteral("parentLayerIndex"), -1));
        if (made.contains(parent) && out.layers[made[parent]].mask)
            out.layers[made[l]].maskedBy = made[parent];
    }
    // (What had an effect is masked as the layer it came from.)
    for (auto it = liftedFrom.cbegin(); it != liftedFrom.cend(); ++it)
        out.layers[it.key()].maskedBy = out.layers[it.value()].maskedBy;
    bool any = false;
    for (Layer const& layer : std::as_const(out.layers))
        any = any || !layer.drawings.isEmpty();
    if (!any)
        return fail(tr("Nothing Eclipse can read is drawn in this .fla (only video)."));
    std::stable_sort(out.labels.begin(), out.labels.end(), [](Label const& a, Label const& b) { return a.frame < b.frame; });
    // The document's swatches: its solid colours, each once.
    if (Node const* lists = document.child(QStringLiteral("swatchLists")))
        for (Node const& list : lists->children)
            if (Node const* swatches = list.child(QStringLiteral("swatches")))
                for (Node const& s : swatches->children) {
                    if (s.name == QLatin1String("LinearGradientSwatchItem") || s.name == QLatin1String("RadialGradientSwatchItem")) {
                        Swatch g;
                        g.kind = s.name.startsWith(QLatin1String("Linear")) ? Swatch::Linear : Swatch::Radial;
                        for (Node const& entry : s.children)
                            if (entry.name == QLatin1String("GradientEntry")) {
                                QColor c(entry.attr(QStringLiteral("color"), QStringLiteral("#000000")));
                                c.setAlphaF(float(entry.number(QStringLiteral("alpha"), 1.0)));
                                g.stops.push_back({std::clamp(entry.number(QStringLiteral("ratio"), 0.0), 0.0, 1.0), c});
                            }
                        if (g.stops.size() >= 2) {
                            g.color = g.stops.first().color;
                            g.name = tr("Flash gradient");
                            bool again = false;
                            for (Swatch const& known : std::as_const(out.swatchGradients))
                                again = again || (known.kind == g.kind && known.stops == g.stops);
                            if (!again)
                                out.swatchGradients.push_back(g);
                        }
                        continue;
                    }
                    if (s.name != QLatin1String("SolidSwatchItem"))
                        continue;
                    QColor c(s.attr(QStringLiteral("color"), QStringLiteral("#000000")));
                    c.setAlphaF(float(s.number(QStringLiteral("alpha"), 1.0)));
                    if (c.isValid() && !out.swatches.contains(c))
                        out.swatches.push_back(c);
                }
    return true;
}

}
