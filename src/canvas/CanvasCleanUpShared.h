// Eclipse - clean-up tools: what their files share.
#pragma once

// not), in this drawing or in every drawing of the layer; closing gaps; strokes made pencil lines.
#include "canvas/Canvas.h"

#include <cmath>

#include <QRandomGenerator>
#include <functional>

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QSettings>

#include "model/document/Commands.h"
#include "model/geometry/Deform.h"
#include "actions/theme/AppSettings.h"

namespace canvas_clean_up_shared {


enum What { UnpaintArt, RepaintArt, RemoveArt };

// Does `what` to the lines and paint that `chosen` says yes to; true when anything changed.
inline bool changeArt(QVector<Stroke>& strokes, QVector<Fill>& fills, std::function<bool(bool line, int index)> const& chosen, What what,
               std::function<bool(int swatch)> const& kept, int swatch, QColor const& colour)
{
    bool changed = false;
    for (int i = int(strokes.size()) - 1; i >= 0; --i) {
        if (!chosen(true, i))
            continue;
        Stroke& stroke = strokes[i];
        if (what == RemoveArt) {
            strokes.removeAt(i);
            changed = true;
        } else if (kept(stroke.swatch)) {
            continue;       // a protected colour is left as it is
        } else if (what == UnpaintArt && stroke.width > 0.0f) {
            stroke.width = 0.0f;        // there still, to bound paint, but not seen
            changed = true;
        } else if (what == RepaintArt && stroke.width > 0.0f && (stroke.swatch != swatch || stroke.color != colour)) {
            stroke.swatch = swatch;
            stroke.color = colour;
            changed = true;
        }
    }
    for (int i = int(fills.size()) - 1; i >= 0; --i) {
        if (!chosen(false, i))
            continue;
        Fill& fill = fills[i];
        if (what == RemoveArt) {
            fills.removeAt(i);
            changed = true;
        } else if (kept(fill.swatch)) {
            continue;
        } else if (what == UnpaintArt) {
            fills.removeAt(i);
            changed = true;
        } else if (fill.swatch != swatch || fill.color != colour) {
            fill.swatch = swatch;
            fill.color = colour;
            changed = true;
        }
    }
    return changed;
}


}  // namespace canvas_clean_up_shared
