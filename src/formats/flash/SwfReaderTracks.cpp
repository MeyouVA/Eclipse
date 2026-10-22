// Eclipse - the Flash movie (.swf) reader, laid out in tracks: a track a depth, its drawings apart from where they are put.
#include "formats/flash/SwfReader.h"

#include <QMap>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace swf {

namespace {

// Flash's blend modes by their numbers, under the names its documents (.fla) give them.
QString blendName(int mode)
{
    static char const* const names[] = {"", "", "layer", "multiply", "screen", "lighten", "darken", "difference", "add",
                                        "subtract", "invert", "alpha", "erase", "overlay", "hardlight"};
    return mode >= 0 && mode < int(std::size(names)) ? QString::fromLatin1(names[mode]) : QString();
}

}  // namespace

QString Reader::trackName(Placed const& placed, int depth) const
{
    if (!placed.name.isEmpty())
        return placed.name;
    for (auto it = classes.cbegin(); it != classes.cend(); ++it)
        if (it.value() == placed.character && !it.key().isEmpty())
            return it.key().section(QLatin1Char('.'), -1);
    return tr("Depth %1").arg(depth);
}

bool Reader::drawTracks(QTransform const& toPixels)
{
    int const frames = int(mainFrames.size());
    // A track for each depth anything is ever put on, from the back up.
    QMap<int, int> trackOf;
    for (DisplayList const& shown : std::as_const(mainFrames))
        for (auto it = shown.cbegin(); it != shown.cend(); ++it)
            trackOf.insert(it.key(), 0);
    for (auto it = trackOf.begin(); it != trackOf.end(); ++it) {
        it.value() = int(out.tracks.size());
        Track t;
        t.depth = it.key();
        t.cells.fill(-1, frames);
        t.places.fill(QTransform(), frames);
        t.opacity.fill(1.0, frames);
        out.tracks.push_back(t);
    }
    QVector<QHash<QByteArray, int>> drawingOf(out.tracks.size());
    // Each track drawn as big as it first shows (so its peg starts at about 100 %, and thin lines stay thin).
    QVector<double> unitOf(out.tracks.size(), 0.0);
    for (int f = 0; f < frames; ++f) {
        if (!told(400 + qint64(600) * f / std::max(1, frames))) {
            why = tr("The import was stopped.");
            return false;
        }
        for (auto it = mainFrames[f].cbegin(); it != mainFrames[f].cend(); ++it) {
            Placed const& placed = *it;
            int const index = trackOf.value(it.key());
            Track& track = out.tracks[index];
            if (track.name.isEmpty())
                track.name = trackName(placed, it.key());
            if (placed.clipDepth > 0) {
                track.mask = true;
                for (auto under = trackOf.upperBound(it.key()); under != trackOf.end() && under.key() <= placed.clipDepth; ++under)
                    if (!out.tracks[under.value()].maskedBy.contains(index))
                        out.tracks[under.value()].maskedBy.push_back(index);
            }
            if (track.blend.isEmpty() && placed.blend > 1)
                track.blend = blendName(placed.blend);
            if (track.filters.isEmpty())
                track.filters = placed.filters;
            if (placed.hidden)
                continue;
            // A colour effect that only fades becomes how opaque the track is; any other is drawn into the drawing.
            ColourFx fx = ColourFx::of(placed);
            bool const fades = fx.mult[0] == 256 && fx.mult[1] == 256 && fx.mult[2] == 256 && fx.add[0] == 0 && fx.add[1] == 0
                               && fx.add[2] == 0 && fx.add[3] == 0;
            if (fades) {
                track.opacity[f] = std::clamp(fx.mult[3] / 256.0, 0.0, 1.0);
                fx.mult[3] = 256;
            }
            // Which drawing of it: a movie clip's frame, a morph's step, a video's frame.
            int step = 0;
            if (auto const sprite = sprites.constFind(placed.character); sprite != sprites.cend() && !sprite->isEmpty())
                step = ((f - placed.since) % int(sprite->size()) + int(sprite->size())) % int(sprite->size());
            else if (morphs.contains(placed.character) || streams.contains(placed.character))
                step = placed.ratio;
            QByteArray const key = QByteArray::number(placed.character) + ':' + QByteArray::number(step) + '/' + fx.key();
            double const placedScale = std::sqrt(std::abs(placed.matrix.determinant()));
            double& unit = unitOf[index];
            if (unit <= 0.0)
                unit = placedScale > 1e-6 ? placedScale : 1.0;
            auto found = drawingOf[index].constFind(key);
            if (found == drawingOf[index].cend()) {
                // Drawn where it stands in its own coordinates, with its colour effect; then into pixels.
                Placed alone = placed;
                alone.matrix = QTransform();
                for (int k = 0; k < 4; ++k) {
                    alone.mult[k] = fx.mult[k];
                    alone.add[k] = fx.add[k];
                }
                Drawing own;
                int order = 0;
                drawPlaced(alone, f, own, order, 0);
                Drawing drawing;
                int placedOrder = 0;
                placeInto(own, QTransform::fromScale(unit / 20.0, unit / 20.0), ColourFx(), drawing, placedOrder);
                // A line that keeps its width: as wide as it is where this drawing is first put (the peg scales it after).
                for (Stroke& s : drawing.strokes) {
                    if (s.texture == kKeepWidthMark) {
                        if (placedScale > 1e-9)
                            s.width = float(s.width * unit / placedScale);
                        s.texture = 0;
                    }
                    s.width = std::max(0.5f, s.width);
                }
                track.drawings.push_back(drawing);
                found = drawingOf[index].insert(key, int(track.drawings.size()) - 1);
            }
            track.cells[f] = *found;
            track.places[f] = QTransform::fromScale(20.0 / unit, 20.0 / unit) * placed.matrix * toPixels;
        }
    }
    return true;
}

}  // namespace swf
