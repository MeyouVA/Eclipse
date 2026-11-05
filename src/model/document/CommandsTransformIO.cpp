// Eclipse - the document: Import / Export Transformations - the animated transformations (keyframes) of layers written
// to a file of their own and read back onto layers, of this scene or another.
#include "model/document/Commands.h"

#include "model/document/CommandsPrivate.h"
#include "model/scene/SceneReadParts.h"
#include "model/scene/SceneWriteParts.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {
char const* const kFormat = "eclipse-transformations";
}

QByteArray Document::exportTransformations(QVector<int> const& layers) const
{
    QJsonArray out;
    for (int l : layers) {
        if (l < 0 || l >= m_scene.layers.size())
            continue;
        QJsonArray keys;
        for (Keyframe const& k : m_scene.layers[l].keys)
            keys.append(scene_write::keyToJson(k));
        out.append(QJsonObject{{"name", m_scene.layers[l].name}, {"keys", keys}});
    }
    return QJsonDocument(QJsonObject{{"format", kFormat}, {"version", 1}, {"fps", m_scene.fps}, {"layers", out}}).toJson();
}

int Document::importTransformations(QByteArray const& file, QVector<int> const& layers, int frame, QString* error)
{
    auto fail = [&](QString const& why) {
        if (error)
            *error = why;
        return 0;
    };
    QJsonParseError parse;
    QJsonObject const root = QJsonDocument::fromJson(file, &parse).object();
    if (parse.error != QJsonParseError::NoError || root["format"].toString() != QLatin1String(kFormat))
        return fail(tr("Unable to find the given import file."));
    QJsonArray const given = root["layers"].toArray();
    if (given.isEmpty())
        return fail(tr("The file has no transformations."));
    // Onto the layers chosen: as many as the file has, in order; else each onto the chosen layer of its name.
    QVector<QPair<int, QJsonArray>> onto;
    if (layers.size() == given.size()) {
        for (int i = 0; i < layers.size(); ++i)
            onto.push_back({layers[i], given[i].toObject()["keys"].toArray()});
    } else {
        for (QJsonValue const& g : given) {
            QString const name = g.toObject()["name"].toString();
            for (int l : layers)
                if (l >= 0 && l < m_scene.layers.size() && m_scene.layers[l].name == name)
                    onto.push_back({l, g.toObject()["keys"].toArray()});
        }
        if (onto.isEmpty() && layers.size() == 1 && given.size() >= 1)
            onto.push_back({layers.first(), given[0].toObject()["keys"].toArray()});
    }
    if (onto.isEmpty())
        return fail(tr("None of the layers chosen is named as a layer of the file: choose as many layers as the file has."));
    int moved = 0;
    apply(tr("Import Transformations"), [&](Scene& s) {
        for (auto const& [l, keys] : onto) {
            if (l < 0 || l >= s.layers.size())
                continue;
            Layer read;
            scene_read::keysFromJson(keys, read, s.frameCount(), true);
            Layer& into = s.layers[l];
            // From `frame` on: what the file has, its first keyframe there.
            int const first = read.keys.isEmpty() ? 0 : read.keys.first().frame;
            for (Keyframe k : read.keys) {
                k.frame = k.frame - first + frame;
                if (k.frame < 0)
                    continue;
                if (k.frame >= s.frameCount())
                    s.setFrameCount(k.frame + 1);
                int at = 0;
                while (at < into.keys.size() && into.keys[at].frame < k.frame)
                    ++at;
                if (at < into.keys.size() && into.keys[at].frame == k.frame)
                    into.keys[at] = k;
                else
                    into.keys.insert(at, k);
            }
            ++moved;
        }
        for (Layer& l : s.layers)
            l.cells.resize(s.frameCount(), Layer::kNoDrawing);
    });
    return moved;
}
