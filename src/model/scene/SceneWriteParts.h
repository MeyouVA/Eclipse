// Eclipse - the parts of a scene as they are written to its file: lines, paint, effects, drawings, keys, layers, the node graph.
#pragma once

#include "model/scene/TextureBank.h"
#include "model/scene/Scene.h"
#include <QBuffer>
#include <QFileInfo>
#include <QDir>
#include "model/geometry/Deform.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace scene_write {

// Strokes are stored compactly: one flat array [x, y, pressure, x, y, pressure, ...].
QJsonObject strokeToJson(Stroke const& s);

QJsonObject fillToJson(Fill const& f);

// The path of a file of the scene from the folder of the scene; empty without a folder.
QString pathFrom(QString const& folder, QString const& file);

QJsonArray pairsJson(QVector<QPair<QString, QString>> const& pairs);

// An effect, as the scene file keeps it (on a layer, or on an effect node of the node graph).
QJsonObject effectToJson(Effect const& e);

// A drawing as it is written (the pictures it shows put into `images`, once each).
QJsonObject drawingToJson(Drawing const& d, QString const& folder, QJsonObject& images, QHash<qint64, QString>& imageKeys);

// A keyframe as it is written.
QJsonObject keyToJson(Keyframe const& k);

// A layer as it is written: its drawings (with the pictures they show put into `images` once each), keys, bones and the rest.
QJsonObject layerToJson(Layer const& layer, QString const& folder, QJsonObject& images, QHash<qint64, QString>& imageKeys);

// The node graph as it is written: its nodes and cables.
QJsonObject graphToJson(NodeGraph const& graph);

}  // namespace scene_write
