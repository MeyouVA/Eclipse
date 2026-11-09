#pragma once

#include <QPixmap>

struct BoardProject;
struct BoardLayer;

QPixmap boardLayerPreview(BoardProject const& project, BoardLayer const& layer, QSize const& size);
