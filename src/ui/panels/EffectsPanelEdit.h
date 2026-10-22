// Eclipse - the effects panel's files: changing the effect picked (a template, so its body is here for each of them).
#pragma once

#include <QListWidget>

#include "model/document/Commands.h"
#include "ui/panels/EffectsPanel.h"

template <class Change> inline void EffectsPanel::edit(Change change)
{
    Scene const& scene = m_document->scene();
    int const row = m_list->currentRow();
    if (m_layer < 0 || m_layer >= scene.layers.size() || row < 0 || row >= scene.layers[m_layer].effects.size())
        return;
    QVector<Effect> effects = scene.layers[m_layer].effects;
    change(effects[row]);
    m_document->setEffects(m_layer, effects, tr("Change Effect"));
    m_list->setCurrentRow(row);
}
