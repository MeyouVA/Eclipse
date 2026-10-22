// Eclipse - the Camera view: how the onion skin is shown (own colours, other layers) and the drawings it shows, for Apply to Onion Skin Range.
#include "canvas/Canvas.h"

#include "model/document/Commands.h"

void Canvas::setOnionOriginal(bool on)
{
    if (m_onionOriginal == on)
        return;
    m_onionOriginal = on;
    emit onionChanged();
    update();
}

void Canvas::setOnionOthers(bool on)
{
    if (m_onionOthers == on)
        return;
    m_onionOthers = on;
    emit onionChanged();
    update();
}

void Canvas::setApplyToOnionRange(bool on)
{
    if (m_applyOnion == on)
        return;
    m_applyOnion = on;
    emit onionChanged();
}

QSet<int> Canvas::onionDrawings() const
{
    // The same drawings the onion skin is painted with: so many before and after, by drawing or by frame, of the mark asked for.
    QSet<int> shown;
    Scene const& scene = m_document->scene();
    if (!m_onionSkin || m_layer < 0 || m_layer >= scene.layers.size())
        return shown;
    Layer const& layer = scene.layers[m_layer];
    for (bool after : {false, true}) {
        int const count = after ? m_onionNext : m_onionPrev;
        for (int i = 0, at = m_frame; i < count; ++i) {
            at = after ? (m_onionByFrame ? at + 1 : layer.nextDrawingFrame(at)) : (m_onionByFrame ? at - 1 : layer.previousDrawingFrame(at));
            if (at < 0 || at >= scene.frameCount())
                break;
            if (layer.drawingAt(at) == Layer::kNoDrawing)
                continue;
            int const index = layer.cells[at];
            if (m_onionMark != Drawing::NoMark && layer.drawings[index].mark != m_onionMark) {
                --i;
                continue;
            }
            if (m_onionStrength[after ? 1 : 0][i] > 0)
                shown.insert(index);
        }
    }
    return shown;
}
