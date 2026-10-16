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
        if (!fits(true))
            return;     // (never so: the scene is the one the stroke was drawn on)
        m_doc->m_scene.layers[m_layer].drawings[m_drawing].strokes.removeLast();
        emit m_doc->changed();
    }
    void redo() override
    {
        if (!fits(false))
            return;
        m_doc->m_scene.layers[m_layer].drawings[m_drawing].strokes.push_back(m_stroke);
        emit m_doc->changed();
    }

private:
    bool fits(bool taking) const
    {
        Scene const& s = m_doc->m_scene;
        return m_layer >= 0 && m_layer < s.layers.size() && m_drawing >= 0 && m_drawing < s.layers[m_layer].drawings.size()
               && (!taking || !s.layers[m_layer].drawings[m_drawing].strokes.isEmpty());
    }
    Document* m_doc;
    int m_layer;
    int m_drawing;
    Stroke m_stroke;
};

// Going into a symbol or out of one: the scene worked on, and the scenes around it, before and after.
// A step of undo like any other, so that what was done before is still there to undo.
class SymbolLevelCommand : public QUndoCommand {
public:
    SymbolLevelCommand(Document* doc, Scene before, QVector<Document::Around> aroundBefore, Scene after, QVector<Document::Around> aroundAfter,
                       QString const& text)
        : QUndoCommand(text), m_doc(doc), m_before(std::move(before)), m_after(std::move(after)), m_aroundBefore(std::move(aroundBefore)),
          m_aroundAfter(std::move(aroundAfter))
    {
    }
    static constexpr int kId = 0x5359;    // ("SY": tells these steps from the others)
    int id() const override { return kId; }
    void undo() override { put(m_before, m_aroundBefore); }
    void redo() override { put(m_after, m_aroundAfter); }

private:
    void put(Scene const& scene, QVector<Document::Around> const& around)
    {
        m_doc->m_scene = scene;
        m_doc->m_around = around;
        emit m_doc->changed();
        emit m_doc->levelChanged();
    }
    Document* m_doc;
    Scene m_before, m_after;
    QVector<Document::Around> m_aroundBefore, m_aroundAfter;
};

Document::Document(QObject* parent) : QObject(parent) {}

bool Document::unsaved() const
{
    if (m_unsavedInside)
        return true;
    if (m_undo.isClean())
        return false;
    int const clean = m_undo.cleanIndex();
    if (clean < 0)
        return true;
    for (int i = std::min(clean, m_undo.index()); i < std::max(clean, m_undo.index()); ++i)
        if (m_undo.command(i)->id() != SymbolLevelCommand::kId)
            return true;
    return false;
}

void Document::setUndoLevels(int levels)
{
    m_undoLevels = std::max(0, levels);
    if (m_undo.count() == 0)
        m_undo.setUndoLimit(m_undoLevels);
}

bool Document::enterSymbol(int symbolId)
{
    Scene::Symbol const* const symbol = m_scene.findSymbol(symbolId);
    if (!symbol || m_around.size() >= Scene::kSymbolDepth)
        return false;
    Scene inner = m_scene.symbolScene(*symbol);
    inner.refreshSymbolInstances();
    QVector<Around> around = m_around;
    around.push_back({m_scene, symbolId});
    m_undo.push(new SymbolLevelCommand(this, m_scene, m_around, inner, around, tr("Edit Symbol")));
    return true;
}

bool Document::leaveSymbol()
{
    if (m_around.isEmpty())
        return false;
    QVector<Around> around = m_around;
    Around const left = around.takeLast();
    Scene outer = left.scene;
    outer.takeSymbol(left.symbolId, m_scene);
    m_undo.push(new SymbolLevelCommand(this, m_scene, m_around, outer, around, tr("Leave Symbol")));
    return true;
}

QStringList Document::symbolTrail() const
{
    QStringList names;
    for (Around const& around : m_around)
        if (Scene::Symbol const* const symbol = around.scene.findSymbol(around.symbolId))
            names << symbol->name;
    return names;
}

Scene Document::wholeScene() const
{
    Scene whole = m_scene;
    for (int i = int(m_around.size()) - 1; i >= 0; --i) {
        Scene outer = m_around[i].scene;
        outer.takeSymbol(m_around[i].symbolId, whole);
        whole = outer;
    }
    return whole;
}

void Document::setSaved()
{
    m_unsavedInside = false;
    m_undo.setClean();
}

void Document::reset(Scene const& scene)
{
    m_around.clear();
    m_unsavedInside = false;
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
    if (!validCell(m_scene, layer, frame) || stroke.points.isEmpty() || !m_scene.layers[layer].holdsDrawings() || m_scene.layers[layer].showsSymbol())
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
    // On top of all that is in the drawing already (a brush shape drawn over the lines among it).
    Drawing const& d = m_scene.layers[layer].drawings[m_scene.layers[layer].cells[frame]];
    Stroke placed = stroke;
    placed.order = Drawing::topOrder(d.fills, d.strokes);
    m_undo.push(new AddStrokeCommand(this, layer, m_scene.layers[layer].cells[frame], placed));
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
    if (!validCell(m_scene, layer, frame) || !m_scene.layers[layer].holdsDrawings() || m_scene.layers[layer].showsSymbol())
        return;         // (what a layer that shows a symbol has on it is the symbol's: changed there)
    apply(text, [&](Scene& s) {
        int const index = s.layers[layer].cells[frame];
        if (index == Layer::kNoDrawing) {
            if (m_extendPrevious)
                holdPreviousUpTo(s.layers[layer], frame);
            Drawing made = drawing;
            if (m_nameByFrame && made.name.isEmpty())
                made.name = QString::number(frame + 1);
            startDrawingAt(s.layers[layer], frame, made);
        } else {
            // Joints kept: a point of one that was moved takes the others with it.
            Drawing kept = drawing;
            kept.keepJoints(s.layers[layer].drawings[index]);
            s.layers[layer].drawings[index] = kept;
        }
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

void Document::setSceneSettings(int width, int height, double fps, double fov)
{
    apply(tr("Scene Settings"), [&](Scene& s) {
        s.width = std::max(1, width);
        s.height = std::max(1, height);
        s.fps = std::clamp(fps, 1.0, 240.0);
        if (fov > 0.0 && fov != s.fov) {
            fov = std::clamp(fov, 1.0, 170.0);
            for (Layer& l : s.layers) {
                bool keyed = false;
                for (Keyframe const& k : l.keys)
                    keyed = keyed || k.pose.fov > 0.0;
                if (l.kind == Layer::Camera && !keyed && std::abs(l.fov - s.fov) < 1e-9)
                    l.fov = fov;
            }
            s.fov = fov;
        }
    });
}
