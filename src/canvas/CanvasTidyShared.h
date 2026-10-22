// Eclipse - tidying artwork: what its files share.
#pragma once

#include "canvas/Canvas.h"
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
#include "actions/theme/AppSettings.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>

namespace canvas_tidy_shared {

// Where the piece a-b crosses the piece c-d: how far along a-b (0 to 1), or -1.
inline double crossing(QPointF a, QPointF b, QPointF c, QPointF d)
{
    QPointF const r = b - a, s = d - c;
    double const across = r.x() * s.y() - r.y() * s.x();
    if (std::abs(across) < 1e-12)
        return -1.0;
    QPointF const ac = c - a;
    double const t = (ac.x() * s.y() - ac.y() * s.x()) / across, u = (ac.x() * r.y() - ac.y() * r.x()) / across;
    return t >= 0.0 && t <= 1.0 && u >= 0.0 && u <= 1.0 ? t : -1.0;
}

}  // namespace canvas_tidy_shared
