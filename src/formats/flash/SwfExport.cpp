// Eclipse - the Flash movie writer.
#include "formats/flash/SwfExport.h"

#include <QFile>
#include <QHash>
#include <QPolygonF>
#include <QTransform>

#include <algorithm>
#include <climits>
#include <cmath>

#include "model/geometry/Deform.h"
#include "model/scene/Scene.h"

namespace {

// Bits written one after another, most significant first, as the file's shape records want them.
class Bits {
public:
    void put(quint32 value, int count)
    {
        for (int i = count - 1; i >= 0; --i) {
            m_byte = quint8((m_byte << 1) | ((value >> i) & 1u));
            if (++m_used == 8)
                flush();
        }
    }
    void putSigned(qint32 value, int count) { put(quint32(value) & ((count >= 32) ? 0xffffffffu : ((1u << count) - 1u)), count); }
    // The rest of the last byte filled with noughts.
    QByteArray bytes()
    {
        if (m_used > 0) {
            m_byte = quint8(m_byte << (8 - m_used));
            flush();
        }
        return m_out;
    }

private:
    void flush()
    {
        m_out.append(char(m_byte));
        m_byte = 0;
        m_used = 0;
    }
    QByteArray m_out;
    quint8 m_byte = 0;
    int m_used = 0;
};

// How many bits a signed number takes (with its sign).
int signedBits(qint32 v)
{
    int n = 1;
    while (n < 32 && (v < -(qint32(1) << (n - 1)) || v > (qint32(1) << (n - 1)) - 1))
        ++n;
    return n;
}

void u16(QByteArray& out, quint16 v)
{
    out.append(char(v & 0xff));
    out.append(char(v >> 8));
}

void u32(QByteArray& out, quint32 v)
{
    for (int i = 0; i < 4; ++i)
        out.append(char((v >> (8 * i)) & 0xff));
}

QByteArray rect(qint32 xmin, qint32 xmax, qint32 ymin, qint32 ymax)
{
    int const n = std::max({signedBits(xmin), signedBits(xmax), signedBits(ymin), signedBits(ymax)});
    Bits bits;
    bits.put(quint32(n), 5);
    for (qint32 v : {xmin, xmax, ymin, ymax})
        bits.putSigned(v, n);
    return bits.bytes();
}

QByteArray tag(int code, QByteArray const& body)
{
    QByteArray out;
    if (body.size() < 63) {
        u16(out, quint16((code << 6) | int(body.size())));
    } else {
        u16(out, quint16((code << 6) | 0x3f));
        u32(out, quint32(body.size()));
    }
    return out + body;
}

// One painted thing of a frame: outlines filled odd-even in one colour, in the frame's twips.
struct Piece {
    QVector<QVector<QPoint>> rings;
    QColor colour;
};

// A shape (DefineShape3) of one piece, its rings turned so that what lies inside an even number of them is filled.
QByteArray shapeBody(int id, Piece const& piece)
{
    QByteArray body;
    u16(body, quint16(id));
    int left = INT_MAX, top = INT_MAX, right = INT_MIN, bottom = INT_MIN;
    for (auto const& ring : piece.rings)
        for (QPoint const& p : ring) {
            left = std::min(left, p.x());
            right = std::max(right, p.x());
            top = std::min(top, p.y());
            bottom = std::max(bottom, p.y());
        }
    body += rect(left, right, top, bottom);
    // One fill style, solid, with its opacity; no line styles.
    body.append(char(1));
    body.append(char(0x00));
    body.append(char(piece.colour.red()));
    body.append(char(piece.colour.green()));
    body.append(char(piece.colour.blue()));
    body.append(char(piece.colour.alpha()));
    body.append(char(0));
    Bits bits;
    bits.put(1, 4);     // fill style bits
    bits.put(0, 4);     // line style bits
    // Which rings are holes: those inside an odd number of the others.
    QVector<QPolygonF> polygons;
    for (auto const& ring : piece.rings) {
        QPolygonF p;
        for (QPoint const& q : ring)
            p << QPointF(q);
        polygons.push_back(p);
    }
    for (int r = 0; r < piece.rings.size(); ++r) {
        QVector<QPoint> ring = piece.rings[r];
        if (ring.size() < 3)
            continue;
        int inside = 0;
        for (int o = 0; o < polygons.size(); ++o)
            if (o != r && polygons[o].containsPoint(polygons[r].first(), Qt::OddEvenFill))
                ++inside;
        // The fill on the right of each edge (y down): an outer ring goes clockwise on screen, a hole the other way.
        double twice = 0.0;
        for (int i = 0; i < ring.size(); ++i)
            twice += double(ring[i].x()) * ring[(i + 1) % ring.size()].y() - double(ring[(i + 1) % ring.size()].x()) * ring[i].y();
        bool const clockwise = twice > 0.0;
        if (clockwise != (inside % 2 == 0))
            std::reverse(ring.begin(), ring.end());
        // Move to the first point, with the fill on the right (fill style 1) and none on the left.
        bits.put(0, 1);         // a style change
        bits.put(0, 1);         // no new styles
        bits.put(0, 1);         // line style: unchanged
        bits.put(1, 1);         // fill style 1
        bits.put(1, 1);         // fill style 0
        bits.put(1, 1);         // move to
        int const moveBits = std::max(signedBits(ring.first().x()), signedBits(ring.first().y()));
        bits.put(quint32(moveBits), 5);
        bits.putSigned(ring.first().x(), moveBits);
        bits.putSigned(ring.first().y(), moveBits);
        bits.put(0, 1);         // fill style 0: none
        bits.put(1, 1);         // fill style 1: the colour
        QPoint at = ring.first();
        for (int i = 1; i <= ring.size(); ++i) {
            QPoint const to = ring[i % ring.size()];
            // (An edge no longer than a straight edge can say: long ones in pieces.)
            int const steps = std::max(1, int(std::ceil(std::max(std::abs(to.x() - at.x()), std::abs(to.y() - at.y())) / 60000.0)));
            QPoint const from = at;
            for (int s = 1; s <= steps; ++s) {
                QPoint const next = from + (to - from) * s / steps;
                int const dx = next.x() - at.x(), dy = next.y() - at.y();
                if (dx == 0 && dy == 0)
                    continue;
                int const n = std::max({signedBits(dx), signedBits(dy), 2});
                bits.put(1, 1);                     // an edge
                bits.put(1, 1);                     // straight
                bits.put(quint32(n - 2), 4);
                bits.put(1, 1);                     // across and down both
                bits.putSigned(dx, n);
                bits.putSigned(dy, n);
                at = next;
            }
        }
    }
    bits.put(0, 6);     // the end of the shape
    return body + bits.bytes();
}

struct Collector {
    Scene const& scene;
    QVector<Piece> pieces;
    SwfExport::Report* report = nullptr;

    // Outlines from the layer's own coordinates into the frame's twips.
    QVector<QVector<QPoint>> twips(QVector<QPolygonF> const& outlines, QTransform const& toFrame) const
    {
        QVector<QVector<QPoint>> rings;
        for (QPolygonF const& outline : outlines) {
            QVector<QPoint> ring;
            for (QPointF const& p : outline) {
                QPointF const q = toFrame.map(p);
                QPoint const t(int(std::lround(q.x() * 20.0)), int(std::lround(q.y() * 20.0)));
                if (ring.isEmpty() || ring.last() != t)
                    ring.push_back(t);
            }
            if (ring.size() > 1 && ring.first() == ring.last())
                ring.removeLast();
            if (ring.size() >= 3)
                rings.push_back(ring);
        }
        return rings;
    }

    void add(QVector<QPolygonF> const& outlines, QColor colour, double opacity, QTransform const& toFrame)
    {
        colour.setAlphaF(float(std::clamp(colour.alphaF() * opacity, 0.0, 1.0)));
        if (colour.alpha() == 0)
            return;
        Piece piece;
        piece.rings = twips(outlines, toFrame);
        piece.colour = colour;
        if (!piece.rings.isEmpty())
            pieces.push_back(piece);
    }

    void drawing(Drawing const& d, double opacity, QTransform const& toFrame)
    {
        if (!d.pictures.isEmpty() && report)
            report->skippedPictures = true;
        for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
            struct Item {
                int order;
                bool line;
                int index;
            };
            QVector<Item> items;
            for (int i = 0; i < d.fillsOf(sheet).size(); ++i)
                items.push_back({d.fillsOf(sheet)[i].order, false, i});
            for (int i = 0; i < d.strokesOf(sheet).size(); ++i)
                items.push_back({d.strokesOf(sheet)[i].order, true, i});
            std::stable_sort(items.begin(), items.end(), [](Item const& a, Item const& b) { return a.order != b.order ? a.order < b.order : (!a.line && b.line); });
            for (Item const& item : std::as_const(items)) {
                if (item.line) {
                    Stroke const& s = d.strokesOf(sheet)[item.index];
                    add(s.outlinePolygons(), scene.resolve(s.swatch, s.color), opacity, toFrame);
                } else {
                    Fill const& f = d.fillsOf(sheet)[item.index];
                    add(f.outlines, scene.resolve(f.swatch, f.color), opacity, toFrame);
                }
            }
        }
    }

    void frame(int f)
    {
        pieces.clear();
        QTransform const camera = scene.cameraTransform(f);
        // The ground of the frame.
        add({QPolygonF(QRectF(0, 0, scene.width, scene.height))}, scene.background, 1.0, QTransform());
        for (int l : scene.drawOrder(f)) {
            Layer const& layer = scene.layers[l];
            if (!scene.enabled(l))
                continue;
            if (layer.kind == Layer::ColourCard) {
                add({QPolygonF(QRectF(0, 0, scene.width, scene.height))}, layer.cardColor, layer.opacity, QTransform());
                continue;
            }
            int const index = layer.drawingAt(f);
            if (!layer.holdsDrawings() || index == Layer::kNoDrawing)
                continue;
            for (Effect const& e : layer.effects)
                if (e.enabled && report)
                    report->skippedEffects = true;
            Pose const pose = scene.poseOf(l, f);
            Drawing const shown = layer.showsSymbol() ? scene.symbolPicture(layer.symbolId, index)
                                                      : Deform::ridden(scene, l, f, Deform::shown(layer.morphing(f) ? layer.shownAt(f) : layer.drawings[index], layer, pose));
            drawing(shown, layer.opacity * pose.opacity, scene.worldTransform(l, f) * camera);
        }
    }
};

}  // namespace

namespace SwfExport {

QByteArray encode(Scene const& scene, int first, int last, Report* report)
{
    Report local;
    Report& said = report ? *report : local;
    said = Report();
    int const count = scene.frameCount();
    first = std::clamp(first, 0, std::max(0, count - 1));
    last = last < 0 ? count - 1 : std::clamp(last, first, count - 1);

    QByteArray tags;
    {
        // The ground of the movie.
        QByteArray background;
        background.append(char(scene.background.red()));
        background.append(char(scene.background.green()));
        background.append(char(scene.background.blue()));
        tags += tag(9, background);
    }
    QHash<QByteArray, int> known;       // a shape already written, by what it is: its id
    QVector<int> placed;                // the shape at each depth (from 1) on the frame before
    int nextId = 1;
    Collector collect{scene};
    collect.report = &said;
    for (int f = first; f <= last; ++f) {
        collect.frame(f);
        QVector<int> now;
        for (Piece const& piece : std::as_const(collect.pieces)) {
            QByteArray const body = shapeBody(0, piece).mid(2);     // (what it is, without an id)
            auto found = known.constFind(body);
            int id;
            if (found != known.cend()) {
                id = *found;
            } else {
                id = nextId++;
                known.insert(body, id);
                QByteArray withId;
                u16(withId, quint16(id));
                tags += tag(32, withId + body);
                ++said.shapes;
            }
            now.push_back(id);
        }
        // Each depth shows its shape: put there, or swapped in for the one there was; depths no longer used emptied.
        for (int d = 0; d < now.size(); ++d) {
            if (d < placed.size() && placed[d] == now[d])
                continue;
            QByteArray place;
            bool const replace = d < placed.size();
            place.append(char(replace ? 0x03 : 0x02));      // has a character; (and moves the one at that depth)
            u16(place, quint16(d + 1));
            u16(place, quint16(now[d]));
            tags += tag(26, place);
        }
        for (int d = int(now.size()); d < placed.size(); ++d) {
            QByteArray remove;
            u16(remove, quint16(d + 1));
            tags += tag(28, remove);
        }
        placed = now;
        tags += tag(1, QByteArray());       // the frame shown
        ++said.frames;
    }
    tags += tag(0, QByteArray());           // the end

    QByteArray movie;
    movie += rect(0, scene.width * 20, 0, scene.height * 20);
    u16(movie, quint16(std::clamp(int(std::lround(scene.fps * 256.0)), 1, 65535)));
    u16(movie, quint16(said.frames));
    movie += tags;
    QByteArray file("FWS");
    file.append(char(8));
    u32(file, quint32(8 + movie.size()));
    return file + movie;
}

bool write(Scene const& scene, QString const& path, QString* error, Report* report)
{
    QByteArray const bytes = encode(scene, 0, -1, report);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

}
