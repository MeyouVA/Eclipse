// Eclipse - the storyboard of a scene (see Storyboard.h).
#include "model/scene/Storyboard.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>

QStringList Storyboard::defaultCaptionFields()
{
    return {QStringLiteral("Action Notes"), QStringLiteral("Dialog"), QStringLiteral("Slugging"), QStringLiteral("Notes")};
}

int Storyboard::Shot::length() const
{
    int total = 0;
    for (Panel const& panel : panels)
        total += std::max(1, panel.length);
    return total;
}

int Storyboard::length() const
{
    int total = 0;
    for (Shot const& shot : shots)
        total += shot.length();
    return total;
}

int Storyboard::panelCount() const
{
    int total = 0;
    for (Shot const& shot : shots)
        total += int(shot.panels.size());
    return total;
}

int Storyboard::shotStart(int shot) const
{
    int at = start;
    for (int s = 0; s < shot && s < shots.size(); ++s)
        at += shots[s].length();
    return at;
}

int Storyboard::panelStart(int shot, int panel) const
{
    int at = shotStart(shot);
    if (shot < 0 || shot >= shots.size())
        return at;
    for (int p = 0; p < panel && p < shots[shot].panels.size(); ++p)
        at += std::max(1, shots[shot].panels[p].length);
    return at;
}

int Storyboard::shotAt(int frame) const
{
    return panelAt(frame).first;
}

QPair<int, int> Storyboard::panelAt(int frame) const
{
    int at = start;
    if (frame < at)
        return {-1, -1};
    for (int s = 0; s < shots.size(); ++s)
        for (int p = 0; p < shots[s].panels.size(); ++p) {
            int const length = std::max(1, shots[s].panels[p].length);
            if (frame < at + length)
                return {s, p};
            at += length;
        }
    return {-1, -1};
}

QString Storyboard::panelName(int shot, int panel, Naming const& naming) const
{
    if (shot < 0 || shot >= shots.size() || panel < 0 || panel >= shots[shot].panels.size())
        return QString();
    Panel const& p = shots[shot].panels[panel];
    if (naming.customPanelNames && !p.name.trimmed().isEmpty())
        return p.name.trimmed();
    QString name;
    if (naming.panelRule == Naming::Letters) {
        // A .. Z, then AA, AB ...
        int n = panel;
        do {
            name.prepend(QChar(u'A' + n % 26));
            n = n / 26 - 1;
        } while (n >= 0);
    } else if (naming.panelRule == Naming::ShotAndNumber) {
        name = QStringLiteral("%1-%2").arg(shots[shot].name).arg(panel + 1);
    } else {
        name = QString::number(panel + 1);
    }
    if (naming.showTotal && naming.panelRule != Naming::ShotAndNumber)
        name += QStringLiteral("/%1").arg(shots[shot].panels.size());
    return name;
}

QString Storyboard::shotName(QString const& typed, Naming const& naming)
{
    QString const name = typed.trimmed();
    if (!naming.shotLeadingZeros)
        return name;
    int digits = 0;
    while (digits < name.size() && name[digits].isDigit())
        ++digits;
    if (digits == 0 || digits >= naming.shotZeros)
        return name;
    return QString(naming.shotZeros - digits, u'0') + name;
}

void Storyboard::renumberShots(Naming const& naming, int first, int step)
{
    for (int s = 0; s < shots.size(); ++s)
        shots[s].name = shotName(QString::number(first + s * step), naming);
}

QStringList Storyboard::sequences() const
{
    QStringList names;
    for (Shot const& shot : shots)
        if (!shot.sequence.isEmpty() && !names.contains(shot.sequence))
            names << shot.sequence;
    return names;
}

QStringList Storyboard::acts() const
{
    QStringList names;
    for (Shot const& shot : shots)
        if (!shot.act.isEmpty() && !names.contains(shot.act))
            names << shot.act;
    return names;
}

int Storyboard::addShot(int at, int frames, QString const& name)
{
    Shot shot;
    shot.name = name;
    Panel panel;
    panel.length = std::max(1, frames);
    shot.panels << panel;
    if (at < 0 || at > shots.size())
        at = int(shots.size());
    if (shot.name.isEmpty())
        shot.name = QString::number(at + 1);
    // (A new shot belongs where its neighbour does.)
    if (at > 0) {
        shot.sequence = shots[at - 1].sequence;
        shot.act = shots[at - 1].act;
    }
    shots.insert(at, shot);
    return at;
}

bool Storyboard::splitPanel(int frame)
{
    QPair<int, int> const where = panelAt(frame);
    if (where.first < 0)
        return false;
    int const first = panelStart(where.first, where.second);
    if (frame == first)
        return false;
    Panel& panel = shots[where.first].panels[where.second];
    Panel second;
    second.length = first + panel.length - frame;
    panel.length = frame - first;
    shots[where.first].panels.insert(where.second + 1, second);
    return true;
}

bool Storyboard::splitShot(int frame)
{
    QPair<int, int> const where = panelAt(frame);
    if (where.first < 0 || where.second == 0 || panelStart(where.first, where.second) != frame)
        return false;
    Shot& shot = shots[where.first];
    Shot after;
    after.sequence = shot.sequence;
    after.act = shot.act;
    after.name = shot.name + QStringLiteral("B");
    after.panels = shot.panels.mid(where.second);
    shot.panels.resize(where.second);
    shots.insert(where.first + 1, after);
    return true;
}

bool Storyboard::joinShot(int shot)
{
    if (shot <= 0 || shot >= shots.size())
        return false;
    shots[shot - 1].panels += shots[shot].panels;
    shots.removeAt(shot);
    return true;
}

void Storyboard::removeShot(int shot)
{
    if (shot >= 0 && shot < shots.size())
        shots.removeAt(shot);
}

void Storyboard::removePanel(int shot, int panel)
{
    if (shot < 0 || shot >= shots.size() || panel < 0 || panel >= shots[shot].panels.size())
        return;
    shots[shot].panels.removeAt(panel);
    if (shots[shot].panels.isEmpty())
        shots.removeAt(shot);
}

void Storyboard::setPanelLength(int shot, int panel, int frames)
{
    if (shot >= 0 && shot < shots.size() && panel >= 0 && panel < shots[shot].panels.size())
        shots[shot].panels[panel].length = std::max(1, frames);
}

QString Storyboard::caption(int shot, int panel, QString const& field) const
{
    if (shot < 0 || shot >= shots.size() || panel < 0 || panel >= shots[shot].panels.size())
        return QString();
    for (Caption const& c : shots[shot].panels[panel].captions)
        if (c.field.compare(field, Qt::CaseInsensitive) == 0)
            return c.text;
    return QString();
}

void Storyboard::setCaption(int shot, int panel, QString const& field, QString const& text)
{
    if (shot < 0 || shot >= shots.size() || panel < 0 || panel >= shots[shot].panels.size())
        return;
    QVector<Caption>& captions = shots[shot].panels[panel].captions;
    for (int c = 0; c < captions.size(); ++c)
        if (captions[c].field.compare(field, Qt::CaseInsensitive) == 0) {
            if (text.isEmpty())
                captions.removeAt(c);
            else
                captions[c].text = text;
            return;
        }
    if (!text.isEmpty())
        captions << Caption{field, text};
    if (!captionFields.contains(field, Qt::CaseInsensitive))
        captionFields << field;
}

void Storyboard::fitTo(int frames)
{
    frames = std::max(1, frames);
    int at = 0;
    for (int s = 0; s < shots.size(); ++s)
        for (int p = 0; p < shots[s].panels.size(); ++p) {
            if (at >= frames) {
                // Past the end: dropped.
                shots[s].panels.resize(p);
                shots.resize(s + (p > 0 ? 1 : 0));
                s = int(shots.size());
                break;
            }
            at += std::max(1, shots[s].panels[p].length);
        }
    if (shots.isEmpty())
        return;
    // The last panel made to end on the last frame.
    Panel& last = shots.last().panels.last();
    last.length = std::max(1, last.length + (frames - length()));
}

QString Storyboard::captionsText(QStringList const& fields, Naming const& naming) const
{
    QString text;
    for (int s = 0; s < shots.size(); ++s)
        for (int p = 0; p < shots[s].panels.size(); ++p) {
            QStringList lines;
            for (Caption const& c : shots[s].panels[p].captions)
                if (fields.isEmpty() || fields.contains(c.field, Qt::CaseInsensitive))
                    lines << QStringLiteral("%1: %2").arg(c.field, c.text);
            if (lines.isEmpty())
                continue;
            text += QStringLiteral("Shot %1, Panel %2\n").arg(shots[s].name, panelName(s, p, naming));
            text += lines.join(u'\n') + QStringLiteral("\n\n");
        }
    return text;
}

static QString csvCell(QString const& value)
{
    if (!value.contains(u',') && !value.contains(u'"') && !value.contains(u'\n'))
        return value;
    QString quoted = value;
    quoted.replace(u'"', QStringLiteral("\"\""));
    return u'"' + quoted + u'"';
}

QByteArray Storyboard::captionsCsv(double fps, bool timecode, Naming const& naming) const
{
    QStringList header = {QStringLiteral("Act"), QStringLiteral("Sequence"), QStringLiteral("Shot"), QStringLiteral("Panel"),
                          QStringLiteral("Start"), QStringLiteral("Duration")};
    header += captionFields;
    QString out;
    QStringList cells;
    for (QString const& h : std::as_const(header))
        cells << csvCell(h);
    out += cells.join(u',') + u'\n';
    for (int s = 0; s < shots.size(); ++s)
        for (int p = 0; p < shots[s].panels.size(); ++p) {
            cells = {csvCell(shots[s].act), csvCell(shots[s].sequence), csvCell(shots[s].name), csvCell(panelName(s, p, naming)),
                     duration(panelStart(s, p), fps, timecode), duration(shots[s].panels[p].length, fps, timecode)};
            for (QString const& field : captionFields)
                cells << csvCell(caption(s, p, field));
            out += cells.join(u',') + u'\n';
        }
    return out.toUtf8();
}

QString Storyboard::duration(int frames, double fps, bool timecode)
{
    if (!timecode)
        return QString::number(frames);
    int const rate = std::max(1, int(std::lround(fps)));
    int const f = frames % rate, totalSeconds = frames / rate;
    return QStringLiteral("%1:%2:%3:%4")
        .arg(totalSeconds / 3600, 2, 10, QChar(u'0'))
        .arg(totalSeconds / 60 % 60, 2, 10, QChar(u'0'))
        .arg(totalSeconds % 60, 2, 10, QChar(u'0'))
        .arg(f, 2, 10, QChar(u'0'));
}

QJsonObject Storyboard::toJson() const
{
    QJsonArray shotArray;
    for (Shot const& shot : shots) {
        QJsonArray panelArray;
        for (Panel const& panel : shot.panels) {
            QJsonObject p{{"length", panel.length}};
            if (!panel.name.isEmpty())
                p["name"] = panel.name;
            if (!panel.image.isEmpty())
                p["image"] = panel.image;
            QJsonArray captions;
            for (Caption const& c : panel.captions)
                captions.append(QJsonObject{{"field", c.field}, {"text", c.text}});
            if (!captions.isEmpty())
                p["captions"] = captions;
            panelArray.append(p);
        }
        QJsonObject s{{"name", shot.name}, {"panels", panelArray}};
        if (!shot.sequence.isEmpty())
            s["sequence"] = shot.sequence;
        if (!shot.act.isEmpty())
            s["act"] = shot.act;
        if (!shot.transition.isEmpty()) {
            s["transition"] = shot.transition;
            s["transitionLength"] = shot.transitionLength;
        }
        if (!shot.clip.isEmpty())
            s["clip"] = shot.clip;
        shotArray.append(s);
    }
    return QJsonObject{{"shots", shotArray}, {"start", start}, {"title", title}, {"subtitle", subtitle},
                       {"captionFields", QJsonArray::fromStringList(captionFields)}};
}

Storyboard Storyboard::fromJson(QJsonObject const& json)
{
    Storyboard board;
    board.start = std::max(0, json["start"].toInt(0));
    board.title = json["title"].toString();
    board.subtitle = json["subtitle"].toString();
    if (json.contains("captionFields")) {
        board.captionFields.clear();
        for (QJsonValue const& f : json["captionFields"].toArray())
            board.captionFields << f.toString();
    }
    for (QJsonValue const& sv : json["shots"].toArray()) {
        QJsonObject const so = sv.toObject();
        Shot shot;
        shot.name = so["name"].toString();
        shot.sequence = so["sequence"].toString();
        shot.act = so["act"].toString();
        shot.transition = so["transition"].toString();
        shot.transitionLength = std::max(0, so["transitionLength"].toInt(0));
        shot.clip = so["clip"].toString();
        for (QJsonValue const& pv : so["panels"].toArray()) {
            QJsonObject const po = pv.toObject();
            Panel panel;
            panel.name = po["name"].toString();
            panel.length = std::max(1, po["length"].toInt(24));
            panel.image = po["image"].toString();
            for (QJsonValue const& cv : po["captions"].toArray())
                panel.captions << Caption{cv.toObject()["field"].toString(), cv.toObject()["text"].toString()};
            shot.panels << panel;
        }
        if (!shot.panels.isEmpty())
            board.shots << shot;
    }
    return board;
}
