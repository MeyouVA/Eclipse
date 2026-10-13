// Eclipse - the document: the scene in hand, drawing on it, and what belongs to the scene as a whole.
#include "model/document/Commands.h"

#include <QUndoCommand>
#include "model/document/CommandsPrivate.h"

// Appends one stroke to an existing drawing.
class AddStrokeCommand : public QUndoCommand {
public:
    AddStrokeCommand(Document* doc, int layer, int drawing, Stroke stroke)
        : QUndoCommand(QObject::tr("Draw Stroke")), m_doc(doc), m_layer(layer), m_drawing(drawing), m_stroke(std::move(stroke))
    {
    }
    void undo() override
    {
        m_doc->m_scene.layers[m_layer].drawings[m_drawing].strokes.removeLast();
        emit m_doc->changed();
    }
    void redo() override
    {
        m_doc->m_scene.layers[m_layer].drawings[m_drawing].strokes.push_back(m_stroke);
        emit m_doc->changed();
    }

private:
    Document* m_doc;
    int m_layer;
    int m_drawing;
    Stroke m_stroke;
};

Document::Document(QObject* parent) : QObject(parent) {}

void Document::setUndoLevels(int levels)
{
    m_undoLevels = std::max(0, levels);
    if (m_undo.count() == 0)
        m_undo.setUndoLimit(m_undoLevels);
}

void Document::reset(Scene const& scene)
{
    m_undo.clear();
    m_undo.setUndoLimit(m_undoLevels);
    m_scene = scene;
    emit changed();
}

void Document::setScene(Scene const& scene)
{
    m_scene = scene;
    emit changed();
}

void Document::addStroke(int layer, int frame, Stroke const& stroke)
{
    if (!validCell(m_scene, layer, frame) || stroke.points.isEmpty() || !m_scene.layers[layer].holdsDrawings())
        return;
    if (m_scene.layers[layer].cells[frame] == Layer::kNoDrawing) {
        // Drawing on an empty cell creates the drawing: one undo step for both.
        apply(tr("Draw Stroke"), [&](Scene& s) {
            Drawing d;
            d.strokes.push_back(stroke);
            if (m_nameByFrame)
                d.name = QString::number(frame + 1);
            if (m_extendPrevious)
                holdPreviousUpTo(s.layers[layer], frame);
            startDrawingAt(s.layers[layer], frame, d);
        });
        return;
    }
    m_undo.push(new AddStrokeCommand(this, layer, m_scene.layers[layer].cells[frame], stroke));
}

int Document::eraseAt(int layer, int frame, QPointF p, double radius)
{
    if (!validCell(m_scene, layer, frame))
        return 0;
    int const index = m_scene.layers[layer].cells[frame];
    if (index == Layer::kNoDrawing)
        return 0;
    QVector<Stroke> const& strokes = m_scene.layers[layer].drawings[index].strokes;
    QVector<Stroke> kept;
    for (Stroke const& s : strokes)
        if (!s.hit(p, radius))
            kept.push_back(s);
    // Brush strokes are contour shapes: they go as lines do.  (Paint from the bucket stays.)
    QVector<Fill> const& fills = m_scene.layers[layer].drawings[index].fills;
    QVector<Fill> keptFills;
    for (Fill const& f : fills)
        if (f.contours.isEmpty() || !f.hit(p, radius))
            keptFills.push_back(f);
    int const removed = int(strokes.size() - kept.size()) + int(fills.size() - keptFills.size());
    if (removed)
        apply(tr("Erase"), [&](Scene& s) {
            s.layers[layer].drawings[index].strokes = kept;
            s.layers[layer].drawings[index].fills = keptFills;
        });
    return removed;
}

void Document::setDrawing(int layer, int frame, Drawing const& drawing, QString const& text)
{
    if (!validCell(m_scene, layer, frame) || !m_scene.layers[layer].holdsDrawings())
        return;
    apply(text, [&](Scene& s) {
        int const index = s.layers[layer].cells[frame];
        if (index == Layer::kNoDrawing) {
            if (m_extendPrevious)
                holdPreviousUpTo(s.layers[layer], frame);
            Drawing made = drawing;
            if (m_nameByFrame && made.name.isEmpty())
                made.name = QString::number(frame + 1);
            startDrawingAt(s.layers[layer], frame, made);
        } else
            s.layers[layer].drawings[index] = drawing;
    });
}

void Document::setGuides(QVector<Scene::Guide> const& guides, QString const& text)
{
    if (m_scene.guides != guides)
        apply(text, [&](Scene& s) { s.guides = guides; });
}

void Document::setMetadata(int layer, QVector<QPair<QString, QString>> const& pairs, QString const& text)
{
    if (layer >= m_scene.layers.size())
        return;
    // Nothing without a name; and of two with one name, the first.
    QVector<QPair<QString, QString>> kept;
    for (QPair<QString, QString> const& pair : pairs) {
        QString const name = pair.first.trimmed();
        bool taken = name.isEmpty();
        for (QPair<QString, QString> const& other : kept)
            taken = taken || other.first == name;
        if (!taken)
            kept.push_back({name, pair.second});
    }
    if ((layer < 0 ? m_scene.metadata : m_scene.layers[layer].metadata) == kept)
        return;
    apply(text, [&](Scene& s) { (layer < 0 ? s.metadata : s.layers[layer].metadata) = kept; });
}

void Document::setReferences(QStringList const& files, QString const& text)
{
    if (m_scene.references != files)
        apply(text, [&](Scene& s) { s.references = files; });
}

void Document::setFrameCount(int count)
{
    if (count < 1 || count == m_scene.frameCount())
        return;
    apply(tr("Change Scene Length"), [&](Scene& s) { s.setFrameCount(count); });
}

void Document::setSceneSettings(int width, int height, double fps)
{
    apply(tr("Scene Settings"), [&](Scene& s) {
        s.width = std::max(1, width);
        s.height = std::max(1, height);
        s.fps = std::clamp(fps, 1.0, 240.0);
    });
}
