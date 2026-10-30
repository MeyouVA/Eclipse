// Eclipse - rendering frames of the scene, several at once on the computer's cores.
#pragma once

#include "EclipseExport.h"

#include <QColor>
#include <QImage>
#include <QString>
#include <QVector>

#include <functional>

#include "model/scene/Scene.h"

namespace FrameRender {

// Preferences > Render: how many frames are rendered at once (0: one for each core of the computer).
void setThreads(int count);
int threads();
// The count a render uses: the setting, or the cores when it is 0.
int threadsUsed();

// The size of a frame rendered at `scale` of the scene's size.
QSize sizeAt(Scene const& scene, double scale);
// One frame as it is rendered, `size` big, on `under`; with `quick` its edges are not smoothed.
QImage frame(Scene const& scene, int frame, QSize size, bool quick = false, QColor const& under = Qt::transparent);

// A rendered frame put in the colour space it is written in (`space` from Scene::outputSpace; empty: as it is); with `floats`, kept in floats.
QImage inSpace(Scene const& scene, QImage const& image, QString const& space, bool floats);

// Renders `frames` on threadsUsed() threads; `keep` gets each on its thread (returns an error or nothing), `progress` the count done (false stops).
bool render(Scene const& scene, QVector<int> const& frames, QSize size, bool quick,
            std::function<QString(int frame, QImage const& image)> const& keep, std::function<bool(int done)> const& progress,
            QString* error = nullptr, QColor const& under = Qt::transparent);

}  // namespace FrameRender
