// Eclipse - the Camera view: Set Pencil Template - the Pencil tool's thickness stencil given to pencil lines already drawn.
#include "canvas/Canvas.h"
#include "model/document/Commands.h"

QVector<float> Canvas::pencilTemplateProfile() const
{
    // The Pencil's stencil (Tool Properties) as the thickness at 32 places from the start of a line to its end.
    int const stencil = thicknessStencilOf(Pencil);
    if (stencil == StencilCount)
        return m_customStencils[stencilSlot(Pencil)];
    QVector<float> profile;
    for (int i = 0; i < 32; ++i)
        profile << stencilThickness(stencil, i / 31.0);
    return profile;
}

void Canvas::givePencilTemplate(Stroke& line, QVector<float> const& profile)
{
    QVector<QPair<double, ThicknessPoint>> byLength;
    for (int k = 0; k < profile.size(); ++k) {
        float const share = profile[k];
        byLength.push_back({profile.size() > 1 ? double(k) / (profile.size() - 1) : 0.0, {0.0, share, share}});
    }
    line.setThicknessByLength(byLength);
    line.simplifyThickness(0.005);
}

void Canvas::onActionSetPencilTemplate()
{
    Drawing const* const d = currentDrawing();
    if (!d || m_selStrokes.isEmpty()) {
        emit message(tr("Select the pencil lines to give the Pencil's thickness template to."));
        return;
    }
    QVector<float> const profile = pencilTemplateProfile();
    if (profile.isEmpty()) {
        emit message(tr("The Pencil's own stencil is empty: make one in Tool Properties first."));
        return;
    }
    Drawing edited = *d;
    for (int i : std::as_const(m_selStrokes))
        if (i >= 0 && i < edited.strokes.size() && edited.strokes[i].width > 0.0f)
            givePencilTemplate(edited.strokes[i], profile);
    commitDrawing(edited, tr("Set Pencil Template"));
    update();
}

void Canvas::onActionSetPencilTemplateAll()
{
    if (!canEditDrawing())
        return;
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size() || !scene.layers[m_layer].holdsDrawings())
        return;
    QVector<float> const profile = pencilTemplateProfile();
    if (profile.isEmpty()) {
        emit message(tr("The Pencil's own stencil is empty: make one in Tool Properties first."));
        return;
    }
    // Every pencil line of every drawing of the layer: one step.
    m_document->editLayers({m_layer}, tr("Set Pencil Template On All Drawings"), [&](Layer& l) {
        for (Drawing& d : l.drawings)
            for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet)
                for (Stroke& s : d.strokesOf(sheet))
                    if (s.width > 0.0f)
                        givePencilTemplate(s, profile);
    });
    update();
}
