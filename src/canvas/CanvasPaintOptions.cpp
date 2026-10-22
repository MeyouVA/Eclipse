// Eclipse - the Camera view: Auto Gap Closing, Paint and Remove Line Texture, and pencil textures set on lines or taken from one.
#include "canvas/Canvas.h"

#include <QAction>
#include <QMenu>

#include "canvas/PencilTexture.h"
#include "model/document/Commands.h"

namespace {

// The three sizes of gap closed by the paint tools, in drawing units.
double gapOf(QString const& size)
{
    return size == QLatin1String("small") ? 3.0 : size == QLatin1String("medium") ? 8.0 : size == QLatin1String("large") ? 16.0 : 0.0;
}

}  // namespace

void Canvas::onActionAutoGap(QString const& size)
{
    setCloseGapSize(gapOf(size));
    emit closeGapChanged(m_gapSize);
}

void Canvas::onShowAutoGapMenu(QMenu* menu)
{
    for (QAction* const a : menu->actions()) {
        if (!a->isCheckable())
            continue;
        QString const id = a->objectName();
        QString const size = id == QLatin1String("AutoGapSmall") ? QStringLiteral("small") : id == QLatin1String("AutoGapMedium") ? QStringLiteral("medium")
                             : id == QLatin1String("AutoGapLarge") ? QStringLiteral("large") : QStringLiteral("off");
        a->setChecked(std::abs(gapOf(size) - m_gapSize) < 1e-9);
    }
}

void Canvas::onActionPaintOverTexture(bool on)
{
    m_paintOverTexture = on;
}

void Canvas::onActionSetPencilTexture()
{
    // The current pencil texture given to the selected lines (as choosing it in Tool Properties does).
    if (m_selStrokes.isEmpty()) {
        emit message(tr("Select the pencil lines to give the texture to."));
        return;
    }
    setLineTexture(m_lineTexture);
}

void Canvas::onActionSetPencilTextureAll()
{
    // Every pencil line of every drawing of the layer takes the current texture: one step.
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size() || !scene.layers[m_layer].holdsDrawings())
        return;
    int const layer = m_layer;
    int const texture = m_lineTexture;
    float const amount = float(m_lineTextureAmount) / 100.0f;
    bool const crisp = m_lineTextureCrisp;
    float const low = float(m_lineRampLow) / 100.0f, high = float(m_lineRampHigh) / 100.0f;
    m_document->editLayers({layer}, tr("Set Pencil Texture On All Drawings"), [&](Layer& l) {
        for (Drawing& d : l.drawings)
            for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet)
                for (Stroke& s : d.strokesOf(sheet)) {
                    if (s.width <= 0.0f)
                        continue;
                    s.texture = texture;
                    s.textureAmount = amount;
                    s.textureCrisp = crisp;
                    s.textureLow = low;
                    s.textureHigh = high;
                }
    });
}

void Canvas::onActionPencilTextureNone()
{
    m_lineTexture = 0;
    emit lineTextureTaken(0);
}

void Canvas::onActionPencilTextureEyedropper()
{
    // The Dropper, taking the texture of the pencil line clicked rather than its colour (until another tool is chosen).
    setTool(Dropper);
    m_dropperTexture = true;
    updateCursor();
    emit message(tr("Pencil Texture Eyedropper: click a textured pencil line to take its texture."));
}

bool Canvas::takeTextureAt(QPointF widgetPos)
{
    Scene const& scene = m_document->scene();
    QPointF const at = toScene(widgetPos);
    QVector<int> const order = scene.drawOrder(m_frame);
    for (int place = int(order.size()) - 1; place >= 0; --place) {
        int const l = order[place];
        Layer const& layer = scene.layers[l];
        int const index = layer.drawingAt(m_frame);
        if (!scene.shows(l) || !layer.holdsDrawings() || index == Layer::kNoDrawing || (!m_dropperAllLayers && l != m_layer))
            continue;
        QPointF const p = scene.worldTransform(l, m_frame).inverted().map(at);
        Drawing const& drawing = layer.drawings[index];
        for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet)
            for (int i = int(drawing.strokesOf(sheet).size()) - 1; i >= 0; --i) {
                Stroke const& s = drawing.strokesOf(sheet)[i];
                if (!s.hit(p, 2.0 / m_zoom))
                    continue;
                m_lineTexture = PencilTexture::known(s.texture) ? s.texture : 0;
                m_lineTextureAmount = int(std::lround(s.textureAmount * 100.0f));
                m_lineTextureCrisp = s.textureCrisp;
                m_lineRampLow = int(std::lround(s.textureLow * 100.0f));
                m_lineRampHigh = int(std::lround(s.textureHigh * 100.0f));
                emit lineTextureTaken(m_lineTexture);
                emit message(m_lineTexture == 0 ? tr("That line has no texture: the pencil now draws without one.") : tr("Pencil texture taken."));
                return true;
            }
    }
    emit message(tr("There is no pencil line there to take a texture from."));
    return false;
}
