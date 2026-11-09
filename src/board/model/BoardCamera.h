#pragma once

#include "EclipseExport.h"

#include <QPointF>
#include <QVector>

struct BoardCameraPose {
    QPointF offset;
    double zoom = 1.0;
    double rotation = 0.0;
    bool operator==(BoardCameraPose const&) const = default;
};

struct BoardCameraKey {
    int frame;
    BoardCameraPose pose;
};

namespace BoardCamera {
inline constexpr double offsetLimit = 1000000.0;
inline constexpr double minZoom = 0.01;
inline constexpr double maxZoom = 64.0;
inline constexpr double rotationLimit = 36000.0;

ECLIPSE_BOARD_MODEL_API bool valid(BoardCameraPose const& pose);
ECLIPSE_BOARD_MODEL_API qsizetype lowerKey(QVector<BoardCameraKey> const& keys, int frame);
ECLIPSE_BOARD_MODEL_API BoardCameraPose poseAt(QVector<BoardCameraKey> const& keys, int frame);
QVector<BoardCameraKey> slice(QVector<BoardCameraKey> const& keys, int first, int length);
}
