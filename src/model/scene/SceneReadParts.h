// Eclipse - the parts of a scene as they are read from its file: lines, paint, effects, drawings, layers.
#pragma once

#include "model/scene/TextureBank.h"
#include "model/scene/Scene.h"
#include <QFileInfo>
#include <QDir>
#include "model/geometry/Deform.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace scene_read {

// A texture as a file names it: none, one of the program's, or one of your own.
int textureRead(int texture);

Fill fillFromJson(QJsonObject const& o);

// Strokes are stored compactly: one flat array [x, y, pressure, x, y, pressure, ...].
Stroke strokeFromJson(QJsonObject const& o);

QString found(QString const& folder, QString const& file, QString const& relative);

QVector<QPair<QString, QString>> pairsFromJson(QJsonArray const& array);

// An effect from the scene file (false: one of a kind this version does not know).
bool effectFromJson(QJsonObject const& eo, Effect& e);

// A drawing as the file has it (version 1: a bare array of strokes).
Drawing drawingFromJson(QJsonValue const& dv, QString const& folder, QHash<QString, QImage> const& images);

// A layer as the file has it: its drawings, exposure, keys, bones and the rest; `frameCount` frames long, in a scene `width` by `height`.
Layer layerFromJson(QJsonObject const& lo, QString const& folder, QHash<QString, QImage> const& images, int frameCount, double width, double height);

// Keyframes from a file into a layer (or a function column's points).
void keysFromJson(QJsonArray const& array, Layer& layer, int frameCount);
}  // namespace scene_read
