// Eclipse - what the parts of the document (Commands*.cpp) have in common and nobody else needs.
#pragma once

#include "model/document/Commands.h"
#include "model/geometry/Deform.h"
#include <QUndoCommand>

// Swaps the whole scene.  Scenes are small enough for now that this is simpler and safer
// than a command per kind of edit.
class SceneStateCommand : public QUndoCommand {
public:
    SceneStateCommand(Document* doc, Scene before, Scene after, QString const& text)
        : QUndoCommand(text), m_doc(doc), m_before(std::move(before)), m_after(std::move(after))
    {
    }
    void undo() override { m_doc->setScene(m_before); }
    void redo() override { m_doc->setScene(m_after); }

private:
    Document* m_doc;
    Scene m_before;
    Scene m_after;
};

template <class Edit> void Document::apply(QString const& text, Edit edit)
{
    Scene after = m_scene;
    edit(after);
    // (Synced layers keep the same timing.)
    after.followSyncedTiming(m_scene);
    // (Key exposures that mark nothing any more - their drawing gone or changed - are not kept.)
    for (Layer& layer : after.layers)
        layer.tidyKeyExposures();
    m_undo.push(new SceneStateCommand(this, m_scene, std::move(after), text));
}

inline bool validCell(Scene const& scene, int layer, int frame)
{
    return layer >= 0 && layer < scene.layers.size() && frame >= 0 && frame < scene.frameCount();
}

// The drawing shown before the empty frames that lead up to `frame` is held over them.
inline void holdPreviousUpTo(Layer& layer, int frame)
{
    int before = frame - 1;
    while (before >= 0 && layer.cells[before] == Layer::kNoDrawing)
        --before;
    if (before < 0)
        return;
    for (int f = before + 1; f < frame; ++f)
        layer.cells[f] = layer.cells[before];
}

// Gives `frame` a drawing of its own: from there to the end of the exposure it was part of.
inline void startDrawingAt(Layer& layer, int frame, Drawing const& drawing)
{
    int const old = layer.cells[frame];
    int const index = int(layer.drawings.size());
    layer.drawings.push_back(drawing);
    int end = frame + 1;
    if (old != Layer::kNoDrawing) {
        while (end < layer.cells.size() && layer.cells[end] == old)
            ++end;
    }
    for (int f = frame; f < end; ++f)
        layer.cells[f] = index;
    layer.compact();
}
