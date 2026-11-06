// Eclipse - an editing program's timeline, read and written (see EditTimeline.h).
#include "formats/edit/EditTimeline.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>

namespace EditTimeline {

// ---- the sequence ----

int Sequence::length() const
{
    if (duration > 0)
        return duration;
    int end = 0;
    for (Clip const& clip : clips)
        end = std::max(end, clip.end);
    for (Marker const& marker : markers)
        end = std::max(end, marker.frame + std::max(1, marker.length));
    return end;
}

int Sequence::timebase() const
{
    return std::max(1, int(std::lround(fps)));
}

static QVector<Clip> clipsOf(QVector<Clip> const& clips, bool audio)
{
    QVector<Clip> out;
    for (Clip const& clip : clips)
        if (clip.audio == audio)
            out << clip;
    std::stable_sort(out.begin(), out.end(), [](Clip const& a, Clip const& b) {
        return a.start != b.start ? a.start < b.start : a.track < b.track;
    });
    return out;
}

QVector<Clip> Sequence::videoClips() const
{
    return clipsOf(clips, false);
}

QVector<Clip> Sequence::audioClips() const
{
    return clipsOf(clips, true);
}

int Sequence::tracks(bool audio) const
{
    int count = 0;
    for (Clip const& clip : clips)
        if (clip.audio == audio)
            count = std::max(count, clip.track + 1);
    return count;
}

Clip const* Sequence::clipNamed(QString const& wanted) const
{
    for (Clip const& clip : clips)
        if (!clip.audio && clip.name.compare(wanted, Qt::CaseInsensitive) == 0)
            return &clip;
    // (A clip named after its file: "sc010.mov" for "sc010".)
    for (Clip const& clip : clips)
        if (!clip.audio && QFileInfo(clip.name).completeBaseName().compare(wanted, Qt::CaseInsensitive) == 0)
            return &clip;
    return nullptr;
}

QStringList Sequence::files() const
{
    QStringList out;
    for (Clip const& clip : clips)
        if (!clip.file.isEmpty() && !out.contains(clip.file, Qt::CaseInsensitive))
            out << clip.file;
    return out;
}

// ---- time codes ----

static int droppedPerMinute(int timebase)
{
    return timebase == 30 ? 2 : timebase == 60 ? 4 : 0;
}

QString timecode(qint64 frame, double fps, bool dropFrame)
{
    int const tb = std::max(1, int(std::lround(fps)));
    int const drop = dropFrame ? droppedPerMinute(tb) : 0;
    bool const negative = frame < 0;
    frame = std::llabs(frame);
    if (drop > 0) {
        qint64 const perTen = qint64(tb) * 600 - drop * 9, perMinute = qint64(tb) * 60 - drop;
        qint64 const tens = frame / perTen, rest = frame % perTen;
        frame += qint64(drop) * 9 * tens + (rest > drop ? drop * ((rest - drop) / perMinute) : 0);
    }
    qint64 const ff = frame % tb, seconds = frame / tb;
    return QStringLiteral("%1%2:%3:%4%5%6")
        .arg(negative ? QStringLiteral("-") : QString())
        .arg(seconds / 3600, 2, 10, QChar(u'0'))
        .arg(seconds / 60 % 60, 2, 10, QChar(u'0'))
        .arg(seconds % 60, 2, 10, QChar(u'0'))
        .arg(QChar(drop > 0 ? u';' : u':'))
        .arg(ff, 2, 10, QChar(u'0'));
}

qint64 frames(QString const& text, double fps, bool dropFrame)
{
    static QRegularExpression const form(QStringLiteral("^\\s*(\\d+)[:;.](\\d+)[:;.](\\d+)[:;.](\\d+)\\s*$"));
    QRegularExpressionMatch const m = form.match(text);
    if (!m.hasMatch())
        return -1;
    int const tb = std::max(1, int(std::lround(fps)));
    qint64 const hh = m.captured(1).toLongLong(), mm = m.captured(2).toLongLong(), ss = m.captured(3).toLongLong(), ff = m.captured(4).toLongLong();
    qint64 total = ((hh * 60 + mm) * 60 + ss) * tb + ff;
    // (A semicolon says the time code counts dropped frames.)
    int const drop = dropFrame || text.contains(u';') ? droppedPerMinute(tb) : 0;
    if (drop > 0) {
        qint64 const minutes = hh * 60 + mm;
        total -= drop * (minutes - minutes / 10);
    }
    return total;
}

QString fileFromUrl(QString const& url)
{
    if (!url.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive))
        return url;
    QString rest = url.mid(5);
    if (rest.startsWith(QStringLiteral("//")))
        rest = rest.mid(2);
    if (rest.startsWith(QStringLiteral("localhost"), Qt::CaseInsensitive))
        rest = rest.mid(9);
    rest = QUrl::fromPercentEncoding(rest.toUtf8());
    static QRegularExpression const drive(QStringLiteral("^/[A-Za-z]:"));
    if (drive.match(rest).hasMatch())
        rest = rest.mid(1);
    return rest;
}

QString urlFromFile(QString const& file)
{
    QString path = QDir::fromNativeSeparators(file);
    if (!path.startsWith(u'/'))
        path.prepend(u'/');
    return QStringLiteral("file://localhost") + QString::fromUtf8(QUrl::toPercentEncoding(path, "/:"));
}

// ---- Final Cut Pro XML ----

namespace {

struct Node {
    QString name, text;
    QHash<QString, QString> attributes;
    QVector<std::shared_ptr<Node>> children;
    Node const* child(QString const& tag) const
    {
        for (auto const& c : children)
            if (c->name == tag)
                return c.get();
        return nullptr;
    }
    QVector<Node const*> all(QString const& tag) const
    {
        QVector<Node const*> out;
        for (auto const& c : children)
            if (c->name == tag)
                out << c.get();
        return out;
    }
    QString value(QString const& tag) const
    {
        Node const* c = child(tag);
        return c ? c->text.trimmed() : QString();
    }
    int number(QString const& tag, int fallback) const
    {
        bool ok = false;
        int const v = int(std::lround(value(tag).toDouble(&ok)));
        return ok ? v : fallback;
    }
};

std::shared_ptr<Node> parseXml(QByteArray const& data, QString* error)
{
    QXmlStreamReader xml(data);
    auto root = std::make_shared<Node>();
    QVector<Node*> stack{root.get()};
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            auto node = std::make_shared<Node>();
            node->name = xml.name().toString();
            for (QXmlStreamAttribute const& a : xml.attributes())
                node->attributes.insert(a.name().toString(), a.value().toString());
            stack.last()->children << node;
            stack << node.get();
        } else if (xml.isEndElement()) {
            if (stack.size() > 1)
                stack.removeLast();
        } else if (xml.isCharacters()) {
            stack.last()->text += xml.text();
        }
    }
    if (xml.hasError()) {
        if (error)
            *error = QStringLiteral("The XML cannot be read (line %1): %2").arg(xml.lineNumber()).arg(xml.errorString());
        return nullptr;
    }
    return root;
}

void readRate(Node const* owner, Sequence& sequence)
{
    if (Node const* rate = owner->child(QStringLiteral("rate"))) {
        int const tb = rate->number(QStringLiteral("timebase"), 24);
        sequence.ntsc = rate->value(QStringLiteral("ntsc")).compare(QStringLiteral("TRUE"), Qt::CaseInsensitive) == 0;
        sequence.fps = sequence.ntsc ? tb * 1000.0 / 1001.0 : double(tb);
    }
}

Marker readMarker(Node const* m)
{
    Marker marker;
    marker.name = m->value(QStringLiteral("name"));
    marker.comment = m->value(QStringLiteral("comment"));
    marker.frame = m->number(QStringLiteral("in"), 0);
    int const out = m->number(QStringLiteral("out"), -1);
    marker.length = out > marker.frame ? out - marker.frame : 0;
    return marker;
}

struct FcpReader {
    QVector<Sequence>* out = nullptr;
    QHash<QString, QString> files;      // file id -> path
    QHash<QString, QString> sequences;  // sequence id -> name
    QSet<Node const*> done;

    QString readSequence(Node const* node)
    {
        QString const id = node->attributes.value(QStringLiteral("id"));
        if (node->children.isEmpty() || done.contains(node))
            return sequences.value(id);
        done.insert(node);
        qsizetype const place = out->size();
        out->push_back(Sequence());
        Sequence sequence;
        sequence.name = node->value(QStringLiteral("name"));
        if (!id.isEmpty())
            sequences.insert(id, sequence.name);
        readRate(node, sequence);
        sequence.duration = std::max(0, node->number(QStringLiteral("duration"), 0));
        if (Node const* tc = node->child(QStringLiteral("timecode")))
            sequence.dropFrame = tc->value(QStringLiteral("displayformat")).compare(QStringLiteral("DF"), Qt::CaseInsensitive) == 0;
        for (Node const* m : node->all(QStringLiteral("marker")))
            sequence.markers << readMarker(m);
        if (Node const* media = node->child(QStringLiteral("media"))) {
            for (int audio = 0; audio < 2; ++audio) {
                Node const* kind = media->child(audio ? QStringLiteral("audio") : QStringLiteral("video"));
                if (!kind)
                    continue;
                if (!audio)
                    if (Node const* format = kind->child(QStringLiteral("format")))
                        if (Node const* sc = format->child(QStringLiteral("samplecharacteristics"))) {
                            sequence.width = sc->number(QStringLiteral("width"), 0);
                            sequence.height = sc->number(QStringLiteral("height"), 0);
                        }
                int trackNumber = 0;
                for (Node const* track : kind->all(QStringLiteral("track")))
                    readTrack(track, trackNumber++, audio != 0, sequence);
            }
        }
        (*out)[place] = sequence;
        return sequence.name;
    }

    void readTrack(Node const* track, int number, bool audio, Sequence& sequence)
    {
        // The items in order: transitions say where a clip that leaves its start or end open (-1) begins or ends.
        struct Item { bool transition; int start, end; };
        QVector<Item> items;
        QVector<int> clipAt;   // index in sequence.clips of each clip item, or -1
        for (auto const& child : track->children) {
            Node const* item = child.get();
            if (item->name == QStringLiteral("transitionitem")) {
                Transition t;
                t.track = number;
                t.audio = audio;
                t.start = item->number(QStringLiteral("start"), 0);
                t.end = item->number(QStringLiteral("end"), t.start);
                if (Node const* effect = item->child(QStringLiteral("effect")))
                    t.name = effect->value(QStringLiteral("name"));
                if (t.name.isEmpty())
                    t.name = QStringLiteral("Cross Dissolve");
                sequence.transitions << t;
                items << Item{true, t.start, t.end};
                clipAt << -1;
            } else if (item->name == QStringLiteral("clipitem") || item->name == QStringLiteral("generatoritem")) {
                Clip clip;
                clip.track = number;
                clip.audio = audio;
                clip.name = item->value(QStringLiteral("name"));
                clip.enabled = item->value(QStringLiteral("enabled")).compare(QStringLiteral("FALSE"), Qt::CaseInsensitive) != 0;
                clip.start = item->number(QStringLiteral("start"), 0);
                clip.end = item->number(QStringLiteral("end"), clip.start);
                clip.in = std::max(0, item->number(QStringLiteral("in"), 0));
                if (Node const* file = item->child(QStringLiteral("file"))) {
                    QString const fid = file->attributes.value(QStringLiteral("id"));
                    QString const url = file->value(QStringLiteral("pathurl"));
                    if (!url.isEmpty()) {
                        clip.file = fileFromUrl(url);
                        if (!fid.isEmpty())
                            files.insert(fid, clip.file);
                    } else {
                        clip.file = files.value(fid);
                    }
                    clip.id = fid;
                    if (clip.name.isEmpty())
                        clip.name = file->value(QStringLiteral("name"));
                }
                if (Node const* nested = item->child(QStringLiteral("sequence")))
                    clip.nested = readSequence(nested);
                for (Node const* m : item->all(QStringLiteral("marker")))
                    clip.markers << readMarker(m);
                items << Item{false, clip.start, clip.end};
                clipAt << int(sequence.clips.size());
                sequence.clips << clip;
            }
        }
        for (int i = 0; i < items.size(); ++i) {
            if (clipAt[i] < 0)
                continue;
            Clip& clip = sequence.clips[clipAt[i]];
            if (clip.start < 0)
                clip.start = i > 0 && items[i - 1].transition ? items[i - 1].start : i > 0 ? items[i - 1].end : 0;
            if (clip.end < 0)
                clip.end = i + 1 < items.size() && items[i + 1].transition ? items[i + 1].end : clip.start + 1;
        }
    }

    void walk(Node const* node)
    {
        for (auto const& child : node->children) {
            if (child->name == QStringLiteral("sequence"))
                readSequence(child.get());
            else
                walk(child.get());
        }
    }
};

}   // namespace

QVector<Sequence> readFcpXml(QByteArray const& data, QString* error)
{
    std::shared_ptr<Node> const root = parseXml(data, error);
    if (!root)
        return {};
    Node const* top = root->children.isEmpty() ? nullptr : root->children.first().get();
    if (!top || top->name != QStringLiteral("xmeml")) {
        if (error)
            *error = QStringLiteral("This is not a Final Cut Pro XML file (it has no xmeml element).");
        return {};
    }
    QVector<Sequence> out;
    FcpReader reader;
    reader.out = &out;
    reader.walk(top);
    if (out.isEmpty() && error)
        *error = QStringLiteral("The file holds no sequence.");
    return out;
}

static void writeRate(QXmlStreamWriter& xml, Sequence const& sequence)
{
    xml.writeStartElement(QStringLiteral("rate"));
    xml.writeTextElement(QStringLiteral("timebase"), QString::number(sequence.timebase()));
    xml.writeTextElement(QStringLiteral("ntsc"), sequence.ntsc ? QStringLiteral("TRUE") : QStringLiteral("FALSE"));
    xml.writeEndElement();
}

static void writeMarker(QXmlStreamWriter& xml, Marker const& marker)
{
    xml.writeStartElement(QStringLiteral("marker"));
    xml.writeTextElement(QStringLiteral("name"), marker.name);
    xml.writeTextElement(QStringLiteral("comment"), marker.comment);
    xml.writeTextElement(QStringLiteral("in"), QString::number(marker.frame));
    xml.writeTextElement(QStringLiteral("out"), QString::number(marker.length > 0 ? marker.frame + marker.length : -1));
    xml.writeEndElement();
}

QByteArray writeFcpXml(Sequence const& sequence)
{
    QByteArray data;
    QXmlStreamWriter xml(&data);
    xml.setAutoFormatting(true);
    xml.setAutoFormattingIndent(1);
    xml.writeStartDocument();
    xml.writeDTD(QStringLiteral("<!DOCTYPE xmeml>"));
    xml.writeStartElement(QStringLiteral("xmeml"));
    xml.writeAttribute(QStringLiteral("version"), QStringLiteral("5"));
    xml.writeStartElement(QStringLiteral("sequence"));
    xml.writeAttribute(QStringLiteral("id"), QStringLiteral("sequence-1"));
    xml.writeTextElement(QStringLiteral("name"), sequence.name);
    xml.writeTextElement(QStringLiteral("duration"), QString::number(sequence.length()));
    writeRate(xml, sequence);
    xml.writeStartElement(QStringLiteral("timecode"));
    writeRate(xml, sequence);
    xml.writeTextElement(QStringLiteral("string"), timecode(0, sequence.fps, sequence.dropFrame));
    xml.writeTextElement(QStringLiteral("frame"), QStringLiteral("0"));
    xml.writeTextElement(QStringLiteral("displayformat"), sequence.dropFrame ? QStringLiteral("DF") : QStringLiteral("NDF"));
    xml.writeEndElement();
    for (Marker const& marker : sequence.markers)
        writeMarker(xml, marker);
    xml.writeStartElement(QStringLiteral("media"));
    QHash<QString, QString> fileIds;
    int nextClip = 1;
    for (int audio = 0; audio < 2; ++audio) {
        xml.writeStartElement(audio ? QStringLiteral("audio") : QStringLiteral("video"));
        if (!audio && sequence.width > 0) {
            xml.writeStartElement(QStringLiteral("format"));
            xml.writeStartElement(QStringLiteral("samplecharacteristics"));
            writeRate(xml, sequence);
            xml.writeTextElement(QStringLiteral("width"), QString::number(sequence.width));
            xml.writeTextElement(QStringLiteral("height"), QString::number(sequence.height));
            xml.writeEndElement();
            xml.writeEndElement();
        }
        int const tracks = std::max(audio ? 0 : 1, sequence.tracks(audio != 0));
        for (int t = 0; t < tracks; ++t) {
            xml.writeStartElement(QStringLiteral("track"));
            // Clips and transitions in order along the track.
            struct Item { int start; Clip const* clip; Transition const* transition; };
            QVector<Item> items;
            for (Clip const& clip : sequence.clips)
                if (clip.audio == (audio != 0) && clip.track == t)
                    items << Item{clip.start, &clip, nullptr};
            for (Transition const& tr : sequence.transitions)
                if (tr.audio == (audio != 0) && tr.track == t)
                    items << Item{tr.start, nullptr, &tr};
            std::stable_sort(items.begin(), items.end(), [](Item const& a, Item const& b) {
                return a.start != b.start ? a.start < b.start : a.transition != nullptr && b.transition == nullptr;
            });
            for (Item const& item : items) {
                if (item.transition) {
                    xml.writeStartElement(QStringLiteral("transitionitem"));
                    xml.writeTextElement(QStringLiteral("start"), QString::number(item.transition->start));
                    xml.writeTextElement(QStringLiteral("end"), QString::number(item.transition->end));
                    xml.writeTextElement(QStringLiteral("alignment"), QStringLiteral("center"));
                    writeRate(xml, sequence);
                    xml.writeStartElement(QStringLiteral("effect"));
                    xml.writeTextElement(QStringLiteral("name"), item.transition->name);
                    xml.writeTextElement(QStringLiteral("effectid"), item.transition->name);
                    xml.writeTextElement(QStringLiteral("effecttype"), QStringLiteral("transition"));
                    xml.writeTextElement(QStringLiteral("mediatype"), audio ? QStringLiteral("audio") : QStringLiteral("video"));
                    xml.writeEndElement();
                    xml.writeEndElement();
                    continue;
                }
                Clip const& clip = *item.clip;
                xml.writeStartElement(QStringLiteral("clipitem"));
                xml.writeAttribute(QStringLiteral("id"), QStringLiteral("clipitem-%1").arg(nextClip++));
                xml.writeTextElement(QStringLiteral("name"), clip.name);
                xml.writeTextElement(QStringLiteral("enabled"), clip.enabled ? QStringLiteral("TRUE") : QStringLiteral("FALSE"));
                xml.writeTextElement(QStringLiteral("duration"), QString::number(clip.in + clip.length()));
                writeRate(xml, sequence);
                xml.writeTextElement(QStringLiteral("start"), QString::number(clip.start));
                xml.writeTextElement(QStringLiteral("end"), QString::number(clip.end));
                xml.writeTextElement(QStringLiteral("in"), QString::number(clip.in));
                xml.writeTextElement(QStringLiteral("out"), QString::number(clip.in + clip.length()));
                if (!clip.file.isEmpty()) {
                    QString const key = QDir::cleanPath(clip.file).toLower();
                    xml.writeStartElement(QStringLiteral("file"));
                    if (fileIds.contains(key)) {
                        xml.writeAttribute(QStringLiteral("id"), fileIds[key]);
                    } else {
                        QString const id = QStringLiteral("file-%1").arg(fileIds.size() + 1);
                        fileIds.insert(key, id);
                        xml.writeAttribute(QStringLiteral("id"), id);
                        xml.writeTextElement(QStringLiteral("name"), QFileInfo(clip.file).fileName());
                        xml.writeTextElement(QStringLiteral("pathurl"), urlFromFile(clip.file));
                        writeRate(xml, sequence);
                        xml.writeStartElement(QStringLiteral("media"));
                        xml.writeEmptyElement(audio ? QStringLiteral("audio") : QStringLiteral("video"));
                        xml.writeEndElement();
                    }
                    xml.writeEndElement();
                }
                for (Marker const& marker : clip.markers)
                    writeMarker(xml, marker);
                xml.writeEndElement();
            }
            xml.writeEndElement();
        }
        xml.writeEndElement();
    }
    xml.writeEndElement();  // media
    xml.writeEndElement();  // sequence
    xml.writeEndElement();  // xmeml
    xml.writeEndDocument();
    return data;
}

// ---- CMX 3600 ----

static QList<int> edlChannels(QString const& track, bool* video)
{
    QString const t = track.toUpper();
    *video = t.contains(u'V') || t == QStringLiteral("B");
    QList<int> audio;
    static QRegularExpression const numbered(QStringLiteral("A(\\d+)"));
    if (t == QStringLiteral("B") || t.startsWith(QStringLiteral("AA"))) {
        audio = {0, 1};
    } else {
        auto it = numbered.globalMatch(t);
        while (it.hasNext())
            audio << std::max(0, it.next().captured(1).toInt() - 1);
        if (audio.isEmpty() && t.startsWith(u'A'))
            audio << 0;
    }
    return audio;
}

Sequence readEdl(QByteArray const& data, double fps, QString* error)
{
    Sequence sequence;
    QString const text = QString::fromUtf8(data);
    bool drop = text.contains(QStringLiteral("FCM: DROP FRAME"), Qt::CaseInsensitive);
    sequence.dropFrame = drop;
    sequence.fps = fps > 0.0 ? fps : drop ? 30000.0 / 1001.0 : 24.0;
    sequence.ntsc = std::abs(sequence.fps - std::round(sequence.fps)) > 0.01;
    struct Event { QString reel; bool video; QList<int> audio; QString type; int length; qint64 srcIn, srcOut, recIn, recOut; QString name, file; };
    QVector<Event> events;
    struct Loc { qint64 at; QString name; };
    QVector<Loc> locs;
    static QRegularExpression const space(QStringLiteral("\\s+"));
    static QRegularExpression const eventStart(QStringLiteral("^\\d{3,6}\\s"));
    for (QString line : text.split(u'\n')) {
        line = line.trimmed();
        if (line.isEmpty())
            continue;
        if (line.startsWith(QStringLiteral("TITLE:"), Qt::CaseInsensitive)) {
            sequence.name = line.mid(6).trimmed();
            continue;
        }
        if (line.startsWith(u'*')) {
            QString const body = line.mid(1).trimmed();
            auto after = [&](QString const& key) { return body.mid(key.size()).trimmed(); };
            if (events.isEmpty() && !body.startsWith(QStringLiteral("LOC:"), Qt::CaseInsensitive))
                continue;
            if (body.startsWith(QStringLiteral("FROM CLIP NAME:"), Qt::CaseInsensitive) && events.last().name.isEmpty())
                events.last().name = after(QStringLiteral("FROM CLIP NAME:"));
            else if (body.startsWith(QStringLiteral("TO CLIP NAME:"), Qt::CaseInsensitive))
                events.last().name = after(QStringLiteral("TO CLIP NAME:"));
            else if (body.startsWith(QStringLiteral("SOURCE FILE:"), Qt::CaseInsensitive))
                events.last().file = after(QStringLiteral("SOURCE FILE:"));
            else if (body.startsWith(QStringLiteral("LOC:"), Qt::CaseInsensitive)) {
                QStringList const parts = after(QStringLiteral("LOC:")).split(space);
                if (!parts.isEmpty()) {
                    qint64 const at = frames(parts[0], sequence.fps, drop);
                    // (After the time code: a colour, then the name.)
                    if (at >= 0)
                        locs << Loc{at, parts.mid(2).join(u' ')};
                }
            }
            continue;
        }
        if (!eventStart.match(line).hasMatch())
            continue;
        QStringList const t = line.split(space);
        if (t.size() < 8)
            continue;
        Event e;
        e.reel = t[1];
        e.audio = edlChannels(t[2], &e.video);
        e.type = t[3].toUpper();
        int at = 4;
        e.length = 0;
        if (t.size() >= 9) {
            e.length = t[4].toInt();
            at = 5;
        }
        e.srcIn = frames(t[at], sequence.fps, drop);
        e.srcOut = frames(t[at + 1], sequence.fps, drop);
        e.recIn = frames(t[at + 2], sequence.fps, drop);
        e.recOut = frames(t[at + 3], sequence.fps, drop);
        if (e.srcIn < 0 || e.recIn < 0 || e.recOut < 0) {
            if (error)
                *error = QStringLiteral("This line of the edit decision list cannot be read: %1").arg(line);
            return {};
        }
        events << e;
    }
    if (events.isEmpty()) {
        if (error)
            *error = QStringLiteral("The edit decision list holds no events.");
        return {};
    }
    qint64 origin = events.first().recIn;
    for (Event const& e : events)
        origin = std::min(origin, e.recIn);
    for (Event const& e : events) {
        if (e.recOut <= e.recIn)
            continue;           // (the outgoing half of a transition)
        bool const black = e.reel.compare(QStringLiteral("BL"), Qt::CaseInsensitive) == 0 || e.reel.compare(QStringLiteral("BLACK"), Qt::CaseInsensitive) == 0;
        auto add = [&](bool audio, int track) {
            if (e.type != QStringLiteral("C") && e.length > 0) {
                Transition tr;
                tr.audio = audio;
                tr.track = track;
                tr.start = int(e.recIn - origin);
                tr.end = tr.start + e.length;
                tr.name = e.type.startsWith(u'W') ? QStringLiteral("Wipe") : e.type.startsWith(u'K') ? QStringLiteral("Key") : QStringLiteral("Cross Dissolve");
                sequence.transitions << tr;
            }
            if (black)
                return;
            Clip clip;
            clip.audio = audio;
            clip.track = track;
            clip.start = int(e.recIn - origin);
            clip.end = int(e.recOut - origin);
            clip.in = int(e.srcIn);
            clip.name = e.name.isEmpty() ? e.reel : e.name;
            clip.file = e.file;
            sequence.clips << clip;
        };
        if (e.video)
            add(false, 0);
        for (int channel : e.audio)
            add(true, channel);
    }
    for (Loc const& loc : locs)
        sequence.markers << Marker{int(loc.at - origin), 0, loc.name, QString()};
    return sequence;
}

QByteArray writeEdl(Sequence const& sequence, bool audio)
{
    qint64 const origin = qint64(sequence.timebase()) * 3600;   // 01:00:00:00
    double const fps = sequence.fps;
    bool const drop = sequence.dropFrame;
    QString out;
    out += QStringLiteral("TITLE: %1\n").arg(sequence.name.isEmpty() ? QStringLiteral("Untitled") : sequence.name);
    out += drop ? QStringLiteral("FCM: DROP FRAME\n\n") : QStringLiteral("FCM: NON-DROP FRAME\n\n");
    int number = 1;
    auto reelOf = [](Clip const& clip) {
        QString reel = QFileInfo(clip.file.isEmpty() ? clip.name : clip.file).completeBaseName().toUpper();
        reel.remove(QRegularExpression(QStringLiteral("[^A-Z0-9_]")));
        return reel.isEmpty() ? QStringLiteral("AX") : reel.left(8);
    };
    auto line = [&](QString const& reel, QString const& track, QString const& type, qint64 srcIn, qint64 srcOut, qint64 recIn, qint64 recOut) {
        out += QStringLiteral("%1  %2 %3 %4 %5 %6 %7 %8\n")
                   .arg(number, 3, 10, QChar(u'0'))
                   .arg(reel, -8)
                   .arg(track, -5)
                   .arg(type, -8)
                   .arg(timecode(srcIn, fps, drop), timecode(srcOut, fps, drop), timecode(recIn + origin, fps, drop), timecode(recOut + origin, fps, drop));
    };
    for (int kind = 0; kind < (audio ? 2 : 1); ++kind) {
        QVector<Clip> const clips = kind ? sequence.audioClips() : sequence.videoClips();
        for (int c = 0; c < clips.size(); ++c) {
            Clip const& clip = clips[c];
            if (!clip.enabled || clip.length() <= 0)
                continue;
            QString const track = kind ? (clip.track == 0 ? QStringLiteral("A") : QStringLiteral("A%1").arg(clip.track + 1)) : QStringLiteral("V");
            // A transition into it: the clip before, held at its last frame, then this one coming in over it.
            Transition const* into = nullptr;
            for (Transition const& tr : sequence.transitions)
                if (tr.audio == (kind != 0) && tr.track == clip.track && tr.start == clip.start && tr.end > tr.start)
                    into = &tr;
            Clip const* before = nullptr;
            for (int b = c - 1; b >= 0 && into; --b)
                if (clips[b].track == clip.track && clips[b].end >= clip.start) {
                    before = &clips[b];
                    break;
                }
            if (into && before) {
                qint64 const held = before->in + (clip.start - before->start);
                line(reelOf(*before), track, QStringLiteral("C"), held, held, clip.start, clip.start);
                QString const type = (into->name.contains(QStringLiteral("wipe"), Qt::CaseInsensitive) ? QStringLiteral("W001 ") : QStringLiteral("D    "))
                                     + QStringLiteral("%1").arg(into->end - into->start, 3, 10, QChar(u'0'));
                line(reelOf(clip), track, type, clip.in, clip.in + clip.length(), clip.start, clip.end);
                out += QStringLiteral("* FROM CLIP NAME: %1\n").arg(before->name);
                out += QStringLiteral("* TO CLIP NAME: %1\n").arg(clip.name);
            } else {
                line(reelOf(clip), track, QStringLiteral("C"), clip.in, clip.in + clip.length(), clip.start, clip.end);
                out += QStringLiteral("* FROM CLIP NAME: %1\n").arg(clip.name);
            }
            if (!clip.file.isEmpty())
                out += QStringLiteral("* SOURCE FILE: %1\n").arg(QDir::toNativeSeparators(clip.file));
            out += u'\n';
            ++number;
        }
    }
    for (Marker const& marker : sequence.markers)
        out += QStringLiteral("* LOC: %1 RED     %2\n").arg(timecode(marker.frame + origin, fps, drop), marker.name);
    return out.toUtf8();
}

// ---- OpenTimelineIO ----

static QJsonObject rationalTime(double value, double rate)
{
    return QJsonObject{{"OTIO_SCHEMA", "RationalTime.1"}, {"rate", rate}, {"value", value}};
}

static QJsonObject timeRange(double start, double duration, double rate)
{
    return QJsonObject{{"OTIO_SCHEMA", "TimeRange.1"}, {"start_time", rationalTime(start, rate)}, {"duration", rationalTime(duration, rate)}};
}

static int framesOf(QJsonValue const& time, double fps)
{
    QJsonObject const t = time.toObject();
    double const rate = t["rate"].toDouble(fps);
    return int(std::lround(t["value"].toDouble() * fps / (rate > 0.0 ? rate : fps)));
}

static QVector<Marker> otioMarkers(QJsonArray const& array, double fps, int offset)
{
    QVector<Marker> out;
    for (QJsonValue const& mv : array) {
        QJsonObject const m = mv.toObject();
        QJsonObject const range = m["marked_range"].toObject();
        Marker marker;
        marker.name = m["name"].toString();
        marker.comment = m["comment"].toString();
        if (marker.comment.isEmpty())
            marker.comment = m["metadata"].toObject()["comment"].toString();
        marker.frame = framesOf(range["start_time"], fps) + offset;
        marker.length = framesOf(range["duration"], fps);
        out << marker;
    }
    return out;
}

Sequence readOtio(QByteArray const& data, QString* error)
{
    QJsonParseError parse;
    QJsonDocument const doc = QJsonDocument::fromJson(data, &parse);
    QJsonObject timeline = doc.object();
    if (doc.isNull() || !timeline["OTIO_SCHEMA"].toString().startsWith(QStringLiteral("Timeline"))) {
        // (A collection of timelines: the first.)
        for (QJsonValue const& c : timeline["children"].toArray())
            if (c.toObject()["OTIO_SCHEMA"].toString().startsWith(QStringLiteral("Timeline"))) {
                timeline = c.toObject();
                break;
            }
        if (!timeline["OTIO_SCHEMA"].toString().startsWith(QStringLiteral("Timeline"))) {
            if (error)
                *error = doc.isNull() ? parse.errorString() : QStringLiteral("This is not an OpenTimelineIO timeline.");
            return {};
        }
    }
    Sequence sequence;
    sequence.name = timeline["name"].toString();
    QJsonObject const stack = timeline["tracks"].toObject();
    // The rate: the first one met.
    double fps = 0.0;
    std::function<void(QJsonValue const&)> findRate = [&](QJsonValue const& v) {
        if (fps > 0.0)
            return;
        if (v.isObject()) {
            QJsonObject const o = v.toObject();
            if (o["OTIO_SCHEMA"].toString().startsWith(QStringLiteral("RationalTime")) && o["rate"].toDouble() > 0.0) {
                fps = o["rate"].toDouble();
                return;
            }
            for (QJsonValue const& c : o)
                findRate(c);
        } else if (v.isArray()) {
            for (QJsonValue const& c : v.toArray())
                findRate(c);
        }
    };
    findRate(stack);
    sequence.fps = fps > 0.0 ? fps : 24.0;
    sequence.ntsc = std::abs(sequence.fps - std::round(sequence.fps)) > 0.01;
    int tracks[2] = {0, 0};
    for (QJsonValue const& tv : stack["children"].toArray()) {
        QJsonObject const track = tv.toObject();
        bool const audio = track["kind"].toString().compare(QStringLiteral("Audio"), Qt::CaseInsensitive) == 0;
        int const number = tracks[audio]++;
        int at = 0;
        QJsonArray const children = track["children"].toArray();
        for (int i = 0; i < children.size(); ++i) {
            QJsonObject const item = children[i].toObject();
            QString const schema = item["OTIO_SCHEMA"].toString();
            QJsonObject const range = item["source_range"].toObject();
            int const length = framesOf(range["duration"], sequence.fps);
            if (schema.startsWith(QStringLiteral("Transition"))) {
                Transition tr;
                tr.audio = audio;
                tr.track = number;
                tr.start = at - framesOf(item["in_offset"], sequence.fps);
                tr.end = at + framesOf(item["out_offset"], sequence.fps);
                QString const type = item["transition_type"].toString();
                tr.name = type.contains(QStringLiteral("Dissolve")) || type.isEmpty() ? QStringLiteral("Cross Dissolve") : type;
                sequence.transitions << tr;
            } else if (schema.startsWith(QStringLiteral("Clip")) || schema.startsWith(QStringLiteral("Stack")) || schema.startsWith(QStringLiteral("Track"))) {
                Clip clip;
                clip.audio = audio;
                clip.track = number;
                clip.name = item["name"].toString();
                clip.start = at;
                clip.end = at + length;
                clip.in = framesOf(range["start_time"], sequence.fps);
                clip.enabled = item["enabled"].toBool(true);
                QJsonObject const media = item["media_reference"].toObject();
                QString const url = media["target_url"].toString();
                if (!url.isEmpty())
                    clip.file = fileFromUrl(url);
                if (!schema.startsWith(QStringLiteral("Clip")))
                    clip.nested = clip.name;
                // (A clip's markers are on its source's frames: put on the sequence's.)
                clip.markers = otioMarkers(item["markers"].toArray(), sequence.fps, 0);
                sequence.clips << clip;
                at += length;
            } else {
                at += length;   // a gap
            }
        }
    }
    sequence.markers = otioMarkers(stack["markers"].toArray(), sequence.fps, 0);
    sequence.markers += otioMarkers(timeline["markers"].toArray(), sequence.fps, 0);
    return sequence;
}

QByteArray writeOtio(Sequence const& sequence)
{
    double const rate = sequence.fps;
    auto markersJson = [&](QVector<Marker> const& markers) {
        QJsonArray array;
        for (Marker const& m : markers)
            array.append(QJsonObject{{"OTIO_SCHEMA", "Marker.2"}, {"name", m.name}, {"comment", m.comment}, {"color", "RED"},
                                     {"marked_range", timeRange(m.frame, m.length, rate)}, {"metadata", QJsonObject()}});
        return array;
    };
    QJsonArray trackArray;
    for (int audio = 0; audio < 2; ++audio) {
        int const count = std::max(audio ? 0 : 1, sequence.tracks(audio != 0));
        for (int t = 0; t < count; ++t) {
            QJsonArray children;
            int at = 0;
            QVector<Clip> clips;
            for (Clip const& clip : sequence.clips)
                if (clip.audio == (audio != 0) && clip.track == t)
                    clips << clip;
            std::stable_sort(clips.begin(), clips.end(), [](Clip const& a, Clip const& b) { return a.start < b.start; });
            for (Clip const& clip : clips) {
                if (clip.start < at)
                    continue;   // (overlapping: a track holds one clip at a time)
                for (Transition const& tr : sequence.transitions)
                    if (tr.audio == (audio != 0) && tr.track == t && tr.start <= clip.start && tr.end > clip.start && at == clip.start && !children.isEmpty())
                        children.append(QJsonObject{{"OTIO_SCHEMA", "Transition.1"}, {"name", tr.name}, {"transition_type", "SMPTE_Dissolve"},
                                                    {"in_offset", rationalTime(clip.start - tr.start, rate)}, {"out_offset", rationalTime(tr.end - clip.start, rate)},
                                                    {"metadata", QJsonObject()}});
                if (clip.start > at)
                    children.append(QJsonObject{{"OTIO_SCHEMA", "Gap.1"}, {"name", ""}, {"source_range", timeRange(0, clip.start - at, rate)}});
                QJsonObject item{{"OTIO_SCHEMA", "Clip.1"}, {"name", clip.name}, {"enabled", clip.enabled},
                                 {"source_range", timeRange(clip.in, clip.length(), rate)}, {"markers", markersJson(clip.markers)}, {"metadata", QJsonObject()}};
                item["media_reference"] = clip.file.isEmpty()
                                              ? QJsonObject{{"OTIO_SCHEMA", "MissingReference.1"}, {"name", ""}}
                                              : QJsonObject{{"OTIO_SCHEMA", "ExternalReference.1"}, {"name", QFileInfo(clip.file).fileName()}, {"target_url", urlFromFile(clip.file)}};
                children.append(item);
                at = clip.end;
            }
            trackArray.append(QJsonObject{{"OTIO_SCHEMA", "Track.1"}, {"name", audio ? QStringLiteral("A%1").arg(t + 1) : QStringLiteral("V%1").arg(t + 1)},
                                          {"kind", audio ? "Audio" : "Video"}, {"children", children}, {"markers", QJsonArray()}, {"metadata", QJsonObject()}});
        }
    }
    QJsonObject const stack{{"OTIO_SCHEMA", "Stack.1"}, {"name", "tracks"}, {"children", trackArray}, {"markers", markersJson(sequence.markers)}, {"metadata", QJsonObject()}};
    QJsonObject const timeline{{"OTIO_SCHEMA", "Timeline.1"}, {"name", sequence.name}, {"global_start_time", QJsonValue()}, {"tracks", stack}, {"metadata", QJsonObject()}};
    return QJsonDocument(timeline).toJson(QJsonDocument::Indented);
}

// ---- any of them ----

QString fileFilter()
{
    return QStringLiteral("Editing Timelines (*.xml *.fcpxml *.edl *.otio);;Final Cut Pro XML (*.xml);;Edit Decision Lists (*.edl);;OpenTimelineIO (*.otio)");
}

QVector<Sequence> read(QString const& path, double fps, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("%1 cannot be opened.").arg(QDir::toNativeSeparators(path));
        return {};
    }
    QByteArray const data = file.readAll();
    QVector<Sequence> sequences;
    QByteArray const head = data.left(512).trimmed();
    if (head.startsWith('{'))
        sequences << readOtio(data, error);
    else if (head.startsWith('<'))
        sequences = readFcpXml(data, error);
    else
        sequences << readEdl(data, fps, error);
    sequences.removeIf([](Sequence const& s) { return s.clips.isEmpty() && s.markers.isEmpty(); });
    // Media named relative to the file: found beside it.
    QDir const folder = QFileInfo(path).absoluteDir();
    for (Sequence& s : sequences) {
        if (s.name.isEmpty())
            s.name = QFileInfo(path).completeBaseName();
        for (Clip& clip : s.clips)
            if (!clip.file.isEmpty() && QFileInfo(clip.file).isRelative())
                clip.file = folder.absoluteFilePath(clip.file);
    }
    return sequences;
}

}
