#include "ui/window/MainWindow.h"
#include "model/document/Commands.h"
#include "ui/timeline/Timeline.h"
#include <QSet>

QVector<int> MainWindow::exposureLayers() const
{
    Scene const& scene = m_document->scene();
    QVector<int> selected;
    QSet<int> seen;
    for (int layer : m_timeline->selectedLayers()) {
        if (layer < 0 || layer >= scene.layers.size() || seen.contains(layer) || !scene.layers[layer].holdsDrawings() || scene.layers[layer].locked)
            continue;
        selected << layer;
        QVector<int> connected{layer};
        seen.insert(layer);
        for (int at = 0; at < connected.size(); ++at) {
            Layer const& source = scene.layers[connected[at]];
            for (int other = 0; other < scene.layers.size(); ++other) {
                Layer const& target = scene.layers[other];
                bool const synced = source.syncGroup && source.syncGroup == target.syncGroup;
                bool const cloned = source.cloneGroup && source.cloneGroup == target.cloneGroup && (source.cloneTiming || target.cloneTiming);
                if (!seen.contains(other) && (synced || cloned)) {
                    seen.insert(other);
                    connected << other;
                }
            }
        }
    }
    return selected;
}
