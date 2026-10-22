// Eclipse - pencil lines: what their files share.
#pragma once

#include "ui/canvas/Canvas.h"
#include <QSet>
#include <QRandomGenerator>
#include <QLineF>
#include <algorithm>

#include <QInputDialog>
#include <QPainterPath>
#include <QSettings>

#include <cmath>
#include <numeric>

#include "model/document/Commands.h"
#include "render/effects/Effects.h"
#include "ui/window/MainWindowPrivate.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include "ui/canvas/CanvasTidyShared.h"

using namespace canvas_tidy_shared;

namespace canvas_pencil_lines_shared {

// How far along a line each of its points lies, from the first (straight from point to point).
inline QVector<double> distancesAlong(QVector<StrokePoint> const& points)
{
    QVector<double> along(points.size(), 0.0);
    for (int i = 1; i < points.size(); ++i)
        along[i] = along[i - 1] + QLineF(points[i - 1].pos, points[i].pos).length();
    return along;
}

}  // namespace canvas_pencil_lines_shared
