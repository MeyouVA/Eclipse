// Eclipse - the document: layers - adding, removing, arranging, attaching, their switches.
#include "model/document/Commands.h"

#include "model/document/CommandsPrivate.h"

int Document::addCamera()
{
    // The first camera is the one the scene is seen through; a further one waits to be chosen.
    int at = -1;
    apply(tr("Add Camera"), [&](Scene& s) { at = s.addLayer(s.uniqueLayerName(QStringLiteral("Camera")), -1, Layer::Camera); });
    return at;
}

int Document::addSound(QString const& file, int startFrame)
{
    int at = -1;
    apply(tr("Import Sound"), [&](Scene& s) {
        QString name = file.section(QLatin1Char('/'), -1).section(QLatin1Char('\\'), -1);
        if (name.contains(QLatin1Char('.')))
            name = name.section(QLatin1Char('.'), 0, -2);
        at = s.addLayer(name.isEmpty() ? QStringLiteral("Sound") : name, -1, Layer::Sound);
        s.layers[at].soundFile = file;
        s.layers[at].soundStart = startFrame;
    });
    return at;
}

void Document::setSoundStart(int layer, int frame)
{
    if (layer < 0 || layer >= m_scene.layers.size() || m_scene.layers[layer].kind != Layer::Sound || m_scene.layers[layer].soundStart == frame)
        return;
    apply(tr("Move Sound"), [&](Scene& s) { s.layers[layer].soundStart = frame; });
}

void Document::setEffects(int layer, QVector<Effect> const& effects, QString const& text)
{
    if (layer < 0 || layer >= m_scene.layers.size() || !m_scene.layers[layer].holdsDrawings() || m_scene.layers[layer].effects == effects)
        return;
    apply(text, [&](Scene& s) { s.layers[layer].effects = effects; });
}

void Document::removeEffect(int layer, int index)
{
    if (layer < 0 || layer >= m_scene.layers.size() || index < 0 || index >= m_scene.layers[layer].effects.size())
        return;
    apply(tr("Remove Effect"), [&](Scene& s) {
        Layer& l = s.layers[layer];
        l.effects.removeAt(index);
        // The amounts in the keyframes go by position: close the gap.
        for (Keyframe& k : l.keys)
            if (index < k.pose.fx.size())
                k.pose.fx.removeAt(index);
    });
}

void Document::setNodePos(int layer, QPointF pos)
{
    if (layer < 0 || layer >= m_scene.layers.size() || (m_scene.layers[layer].nodePlaced && m_scene.layers[layer].nodePos == pos))
        return;
    apply(tr("Move Node"), [&](Scene& s) {
        s.layers[layer].nodePos = pos;
        s.layers[layer].nodePlaced = true;
    });
}

void Document::setActiveCamera(int layer)
{
    if (layer < 0 || layer >= m_scene.layers.size() || m_scene.layers[layer].kind != Layer::Camera || m_scene.cameraIndex() == layer)
        return;
    apply(tr("Choose Camera"), [&](Scene& s) { s.activeCamera = s.layers[layer].id; });
}

int Document::addPeg(int above)
{
    int at = -1;
    apply(tr("Add Peg"), [&](Scene& s) { at = s.addLayer(QStringLiteral("Peg"), above + 1, Layer::Peg); });
    return at;
}

int Document::addColourCard()
{
    int at = -1;
    apply(tr("Add Colour-Card"), [&](Scene& s) { at = s.addLayer(QStringLiteral("Colour-Card"), 0, Layer::ColourCard); });
    return at;
}

int Document::addGroup(int above)
{
    int at = -1;
    apply(tr("Add Group"), [&](Scene& s) { at = s.addLayer(QStringLiteral("Group"), above + 1, Layer::Group); });
    return at;
}

int Document::groupLayer(int layer)
{
    if (layer < 0 || layer >= m_scene.layers.size())
        return -1;
    int at = -1;
    apply(tr("Group Selection"), [&](Scene& s) {
        int const up = s.parentIndex(layer);
        at = s.addLayer(s.layers[layer].name + QStringLiteral("-G"), layer + 1, Layer::Group);
        // The group takes the place of the layer under what it was attached to.
        s.layers[at].parent = up >= 0 ? s.layers[up].id : 0;
        s.layers[at].parentBone = s.layers[layer].parentBone;
        s.layers[layer].parent = s.layers[at].id;
        s.layers[layer].parentBone = -1;
    });
    return at;
}

int Document::groupLayers(QVector<int> const& layers)
{
    QVector<int> in;
    for (int l : layers)
        if (l >= 0 && l < m_scene.layers.size() && !in.contains(l))
            in.push_back(l);
    if (in.isEmpty())
        return -1;
    if (in.size() == 1)
        return groupLayer(in.first());
    std::sort(in.begin(), in.end());
    int at = -1;
    apply(tr("Group Selection"), [&](Scene& s) {
        int const front = in.last();
        int const up = s.parentIndex(front);
        at = s.addLayer(s.layers[front].name + QStringLiteral("-G"), front + 1, Layer::Group);
        s.layers[at].parent = up >= 0 && !in.contains(up) ? s.layers[up].id : 0;
        for (int l : in) {
            // A layer attached to another of the selection stays attached to it.
            if (in.contains(s.parentIndex(l)))
                continue;
            s.layers[l].parent = s.layers[at].id;
            s.layers[l].parentBone = -1;
        }
    });
    return at;
}

int Document::insertTemplate(Scene const& piece, int frame)
{
    if (piece.layers.isEmpty() || frame < 0)
        return -1;
    int at = -1;
    apply(tr("Insert Template"), [&](Scene& s) { at = s.insert(piece, frame); });
    return at;
}

int Document::pasteTemplateOnto(Scene const& piece, int frame)
{
    if (piece.layers.isEmpty() || frame < 0)
        return -1;
    Scene trial = m_scene;
    if (trial.pasteOnto(piece, frame) < 0)
        return -1;      // not every layer of it is here: nothing is done
    int at = -1;
    apply(tr("Paste Template"), [&](Scene& s) { at = s.pasteOnto(piece, frame); });
    return at;
}

int Document::mergeLayers(QVector<int> const& layers)
{
    QVector<int> in;
    for (int l : layers)
        if (l >= 0 && l < m_scene.layers.size() && m_scene.layers[l].holdsDrawings() && !in.contains(l))
            in.push_back(l);
    if (in.size() < 2)
        return -1;
    std::sort(in.begin(), in.end());    // back to front: the front layer is painted last
    int at = -1;
    apply(tr("Merge Layers"), [&](Scene& s) {
        QVector<Drawing> drawings;
        QVector<QVector<double>> made;  // what each of them was made from
        QVector<int> cells(s.frameCount(), Layer::kNoDrawing);
        for (int f = 0; f < s.frameCount(); ++f) {
            Drawing all;
            QVector<double> from;
            bool any = false;
            for (int l : in) {
                int const index = s.layers[l].drawingAt(f);
                QTransform const t = s.worldTransform(l, f);
                from << index << t.m11() << t.m12() << t.m13() << t.m21() << t.m22() << t.m23() << t.m31() << t.m32() << t.m33();
                if (index == Layer::kNoDrawing)
                    continue;
                any = true;
                Drawing part = s.layers[l].drawings[index];
                for (Picture& picture : part.pictures)
                    picture.transform(t);
                all.pictures += part.pictures;
                for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
                    for (Stroke& stroke : part.strokesOf(sheet))
                        stroke.transform(t);
                    for (Fill& fill : part.fillsOf(sheet))
                        fill.transform(t);
                    all.strokesOf(sheet) += part.strokesOf(sheet);
                    all.fillsOf(sheet) += part.fillsOf(sheet);
                }
            }
            if (!any)
                continue;
            // The same drawings standing the same way: the same merged drawing, held.
            int found = int(made.indexOf(from));
            if (found < 0) {
                found = int(drawings.size());
                drawings.push_back(all);
                made.push_back(from);
            }
            cells[f] = found;
        }
        at = s.addLayer(s.layers[in.last()].name + QStringLiteral("_Merged"), in.last() + 1, Layer::DrawingLayer);
        s.layers[at].drawings = drawings;
        s.layers[at].cells = cells;
        for (int i = int(in.size()) - 1; i >= 0; --i)
            s.removeLayer(in[i]);
        at -= int(in.size());
    });
    return at;
}

int Document::addBone(int current)
{
    int at = -1;
    bool const chained = current >= 0 && current < m_scene.layers.size() && m_scene.layers[current].kind == Layer::Bone;
    apply(tr("Add Bone"), [&](Scene& s) {
        at = s.addLayer(QStringLiteral("Bone"), current + 1, Layer::Bone);
        if (chained) {
            // It hangs on the bone it was added to, and begins where that one ends.
            s.layers[at].parent = s.layers[current].id;
            s.layers[at].pivot = s.layers[current].pivot + QPointF(s.layers[current].boneLength, 0.0);
            s.layers[at].boneLength = s.layers[current].boneLength;
        }
    });
    return at;
}

int Document::addQuadmap(int above)
{
    int at = -1;
    apply(tr("Add Quadmap"), [&](Scene& s) { at = s.addLayer(QStringLiteral("Quadmap"), above + 1, Layer::Quadmap); });
    return at;
}

void Document::setCardColor(int layer, QColor const& color)
{
    if (layer < 0 || layer >= m_scene.layers.size() || !color.isValid() || m_scene.layers[layer].cardColor == color)
        return;
    apply(tr("Change Colour-Card"), [&](Scene& s) { s.layers[layer].cardColor = color; });
}

void Document::setIkConstraints(int layer, bool excluded, bool limited, double minimum, double maximum)
{
    if (layer < 0 || layer >= m_scene.layers.size())
        return;
    if (minimum > maximum)
        std::swap(minimum, maximum);
    Layer const& now = m_scene.layers[layer];
    if (now.ikExcluded == excluded && now.ikLimited == limited && now.ikMin == minimum && now.ikMax == maximum)
        return;
    apply(tr("IK Constraints"), [&](Scene& s) {
        s.layers[layer].ikExcluded = excluded;
        s.layers[layer].ikLimited = limited;
        s.layers[layer].ikMin = minimum;
        s.layers[layer].ikMax = maximum;
    });
}

void Document::setBoneLength(int layer, double length)
{
    length = std::max(1.0, length);
    if (layer < 0 || layer >= m_scene.layers.size() || m_scene.layers[layer].boneLength == length)
        return;
    apply(tr("Change Bone Length"), [&](Scene& s) {
        // The bones that begin at its tip stay at its tip.
        double const delta = length - s.layers[layer].boneLength;
        for (Layer& child : s.layers)
            if (child.kind == Layer::Bone && child.parent == s.layers[layer].id)
                child.pivot += QPointF(delta, 0.0);
        s.layers[layer].boneLength = length;
    });
}

int Document::addParentPeg(int layer)
{
    if (layer < 0 || layer >= m_scene.layers.size())
        return -1;
    int at = -1;
    apply(tr("Add Parent Peg"), [&](Scene& s) {
        int const up = s.parentIndex(layer);
        int const upId = up >= 0 ? s.layers[up].id : 0;
        at = s.addLayer(s.layers[layer].name + QStringLiteral("-P"), layer + 1, Layer::Peg);
        s.layers[at].parent = upId;
        s.layers[at].pivot = s.layers[layer].pivot;
        s.layers[layer].parent = s.layers[at].id;
    });
    return at;
}

bool Document::setParent(int layer, int parent)
{
    Scene probe = m_scene;
    if (!probe.setParent(layer, parent))
        return false;
    if (probe.layers[layer].parent != m_scene.layers[layer].parent || probe.layers[layer].parentBone != m_scene.layers[layer].parentBone)
        apply(parent >= 0 ? tr("Attach Layer") : tr("Detach Layer"), [&](Scene& s) { s.setParent(layer, parent); });
    return true;
}

int Document::moveLayerBeside(int layer, int target, bool behind)
{
    int const count = int(m_scene.layers.size());
    if (layer < 0 || layer >= count || target < 0 || target >= count || layer == target)
        return layer;
    // Taking the layer out shifts everything in front of it down by one.
    int const to = behind ? (layer < target ? target - 1 : target) : (layer < target ? target : target + 1);
    if (to != layer)
        apply(tr("Move Layer"), [&](Scene& s) { s.layers.move(layer, to); });
    return to;
}

int Document::addLayer(int above)
{
    int at = -1;
    apply(tr("Add Layer"), [&](Scene& s) { at = s.addLayer(QStringLiteral("Drawing"), above + 1); });
    return at;
}

void Document::removeLayer(int layer)
{
    if (layer < 0 || layer >= m_scene.layers.size() || m_scene.layers.size() == 1)
        return;
    apply(tr("Delete Layer"), [&](Scene& s) { s.removeLayer(layer); });
}

void Document::moveLayer(int layer, int delta)
{
    int const to = layer + delta;
    if (layer < 0 || layer >= m_scene.layers.size() || to < 0 || to >= m_scene.layers.size() || delta == 0)
        return;
    apply(tr("Move Layer"), [&](Scene& s) { s.layers.move(layer, to); });
}

void Document::renameLayer(int layer, QString const& name)
{
    if (layer < 0 || layer >= m_scene.layers.size() || name.trimmed().isEmpty() || m_scene.layers[layer].name == name)
        return;
    apply(tr("Rename Layer"), [&](Scene& s) {
        s.layers[layer].name.clear();  // so that its own name does not count as taken
        s.layers[layer].name = s.uniqueLayerName(name.trimmed());
    });
}

void Document::setLayerVisible(int layer, bool visible)
{
    if (layer < 0 || layer >= m_scene.layers.size() || m_scene.layers[layer].visible == visible)
        return;
    apply(visible ? tr("Show Layer") : tr("Hide Layer"), [&](Scene& s) { s.layers[layer].visible = visible; });
}

void Document::setLayerOnion(int layer, bool onion)
{
    if (layer < 0 || layer >= m_scene.layers.size() || m_scene.layers[layer].onion == onion)
        return;
    apply(onion ? tr("Onion Skin On") : tr("Onion Skin Off"), [&](Scene& s) { s.layers[layer].onion = onion; });
}

void Document::editLayers(QVector<int> const& layers, QString const& text, std::function<void(Layer&)> const& change)
{
    if (layers.isEmpty())
        return;
    apply(text, [&](Scene& s) {
        for (int l : layers)
            if (l >= 0 && l < s.layers.size())
                change(s.layers[l]);
    });
}

int Document::createSymbol(QVector<int> const& layers, QString const& name)
{
    {
        Scene trial = m_scene;
        if (trial.makeSymbol(layers, name) < 0)
            return -1;
    }
    int at = -1;
    apply(tr("Create Symbol"), [&](Scene& s) { at = s.makeSymbol(layers, name); });
    return at;
}

int Document::insertSymbol(int symbolId, int above)
{
    if (!m_scene.findSymbol(symbolId))
        return -1;
    int at = -1;
    apply(tr("Insert Symbol"), [&](Scene& s) { at = s.addSymbolInstance(symbolId, above >= 0 && above < s.layers.size() ? above + 1 : -1); });
    return at;
}

int Document::insertEmptySymbol(QString const& name, int above)
{
    int at = -1;
    apply(tr("Create Empty Symbol"), [&](Scene& s) {
        int const id = s.addEmptySymbol(name);
        at = s.addSymbolInstance(id, above >= 0 && above < s.layers.size() ? above + 1 : -1);
    });
    return at;
}

bool Document::duplicateSymbol(int layer)
{
    if (layer < 0 || layer >= m_scene.layers.size() || !m_scene.layers[layer].showsSymbol() || !m_scene.findSymbol(m_scene.layers[layer].symbolId))
        return false;
    apply(tr("Duplicate Symbol"), [&](Scene& s) { s.duplicateSymbol(layer); });
    return true;
}

int Document::addCloneLayer(int layer)
{
    if (layer < 0 || layer >= m_scene.layers.size() || !m_scene.layers[layer].holdsDrawings() || m_scene.layers[layer].showsSymbol())
        return -1;
    int at = -1;
    apply(tr("Clone Layer"), [&](Scene& s) { at = s.addCloneLayer(layer); });
    return at;
}

void Document::makeCloneIndependent(int layer)
{
    if (layer < 0 || layer >= m_scene.layers.size() || m_scene.layers[layer].cloneGroup == 0)
        return;
    apply(tr("Make Clone Independent"), [&](Scene& s) { s.makeCloneIndependent(layer); });
}

int Document::insertControlPoint(int layer, int frame)
{
    Scene probe = m_scene;
    if (probe.insertControlPoint(layer, frame) < 0)
        return -1;
    int at = -1;
    apply(tr("Insert Control Point"), [&](Scene& s) { at = s.insertControlPoint(layer, frame); });
    return at;
}

static bool hasControl(Scene const& scene, int layer, int key, int control)
{
    return layer >= 0 && layer < scene.layers.size() && key >= 0 && key < scene.layers[layer].keys.size() && control >= 0
           && control < scene.layers[layer].keys[key].controls.size();
}

void Document::moveControlPoint(int layer, int key, int control, QPointF to)
{
    if (!hasControl(m_scene, layer, key, control) || m_scene.layers[layer].keys[key].controls[control] == to)
        return;
    apply(tr("Move Control Point"), [&](Scene& s) { s.layers[layer].keys[key].controls[control] = to; });
}

void Document::removeControlPoint(int layer, int key, int control)
{
    if (!hasControl(m_scene, layer, key, control))
        return;
    apply(tr("Remove Control Point"), [&](Scene& s) { s.layers[layer].keys[key].controls.removeAt(control); });
}

int Document::lockControlPointInTime(int layer, int key, int control)
{
    if (!hasControl(m_scene, layer, key, control) || m_scene.controlPointFrame(layer, key, control) < 0)
        return -1;
    int frame = -1;
    apply(tr("Lock in Time"), [&](Scene& s) { frame = s.lockControlPointInTime(layer, key, control); });
    return frame;
}

void Document::setPathShape(int layer, int key, double tension, double continuity, double bias)
{
    if (layer < 0 || layer >= m_scene.layers.size() || key < 0 || key >= m_scene.layers[layer].keys.size())
        return;
    Keyframe const& k = m_scene.layers[layer].keys[key];
    tension = std::clamp(tension, -1.0, 1.0);
    continuity = std::clamp(continuity, -1.0, 1.0);
    bias = std::clamp(bias, -1.0, 1.0);
    if (k.tension == tension && k.continuity == continuity && k.bias == bias)
        return;
    apply(tr("Shape Path"), [&](Scene& s) {
        Keyframe& into = s.layers[layer].keys[key];
        into.tension = tension;
        into.continuity = continuity;
        into.bias = bias;
    });
}

int Document::addSyncedLayer(int layer)
{
    if (layer < 0 || layer >= m_scene.layers.size() || !m_scene.layers[layer].holdsDrawings() || m_scene.layers[layer].showsSymbol())
        return -1;
    int at = -1;
    apply(tr("Add Synced Drawing Layer"), [&](Scene& s) { at = s.addSyncedLayer(layer); });
    return at;
}

void Document::syncLayerWith(int layer, int other)
{
    if (layer < 0 || layer >= m_scene.layers.size() || !m_scene.layers[layer].holdsDrawings() || m_scene.layers[layer].showsSymbol())
        return;
    if (other < 0 && m_scene.layers[layer].syncGroup == 0)
        return;
    apply(other < 0 ? tr("Remove Sync") : tr("Sync Layer"), [&](Scene& s) { s.syncLayerWith(layer, other); });
}

int Document::cloneLayer(int layer)
{
    if (layer < 0 || layer >= m_scene.layers.size() || !m_scene.layers[layer].holdsDrawings())
        return -1;
    int at = -1;
    apply(tr("Clone Layer"), [&](Scene& s) { at = s.cloneLayer(layer); });
    return at;
}

int Document::expandSymbol(int layer)
{
    if (layer < 0 || layer >= m_scene.layers.size() || !m_scene.layers[layer].showsSymbol() || !m_scene.findSymbol(m_scene.layers[layer].symbolId))
        return -1;
    int at = -1;
    apply(tr("Expand Symbol"), [&](Scene& s) { at = s.expandSymbol(layer); });
    return at;
}

void Document::removeLayers(QVector<int> const& layers)
{
    QVector<int> gone;
    for (int l : layers)
        if (l >= 0 && l < m_scene.layers.size() && !gone.contains(l))
            gone.push_back(l);
    if (gone.isEmpty() || gone.size() >= m_scene.layers.size())
        return;  // a scene keeps one layer at least
    std::sort(gone.begin(), gone.end());
    apply(gone.size() == 1 ? tr("Delete Layer") : tr("Delete Layers"), [&](Scene& s) {
        for (int i = int(gone.size()) - 1; i >= 0; --i)
            s.removeLayer(gone[i]);
    });
}

void Document::setLayerLocked(int layer, bool locked)
{
    if (layer < 0 || layer >= m_scene.layers.size() || m_scene.layers[layer].locked == locked)
        return;
    apply(locked ? tr("Lock Layer") : tr("Unlock Layer"), [&](Scene& s) { s.layers[layer].locked = locked; });
}

void Document::setLayerSheetShown(int layer, int sheet, bool shown)
{
    if (layer < 0 || layer >= m_scene.layers.size() || sheet < 0 || sheet >= Drawing::SheetCount || m_scene.layers[layer].sheetShown(sheet) == shown)
        return;
    apply(shown ? tr("Show Art Layer") : tr("Hide Art Layer"), [&](Scene& s) {
        if (shown)
            s.layers[layer].hiddenSheets &= ~(1 << sheet);
        else
            s.layers[layer].hiddenSheets |= 1 << sheet;
    });
}

void Document::setLayerBitmap(int layer, bool bitmap)
{
    if (layer < 0 || layer >= m_scene.layers.size() || !m_scene.layers[layer].holdsDrawings() || m_scene.layers[layer].bitmap == bitmap)
        return;
    apply(bitmap ? tr("Make Bitmap Layer") : tr("Make Vector Layer"), [&](Scene& s) { s.layers[layer].bitmap = bitmap; });
}

void Document::setLayerSolo(int layer, bool solo)
{
    if (layer < 0 || layer >= m_scene.layers.size() || m_scene.layers[layer].solo == solo)
        return;
    apply(solo ? tr("Solo Layer") : tr("Solo Off"), [&](Scene& s) { s.layers[layer].solo = solo; });
}

void Document::setLayerCollapsed(int layer, bool collapsed)
{
    if (layer < 0 || layer >= m_scene.layers.size() || m_scene.layers[layer].collapsed == collapsed)
        return;
    apply(collapsed ? tr("Collapse") : tr("Expand"), [&](Scene& s) { s.layers[layer].collapsed = collapsed; });
}

void Document::setLayerColor(int layer, QColor const& color)
{
    if (layer < 0 || layer >= m_scene.layers.size() || m_scene.layers[layer].trackColor == color)
        return;
    apply(color.isValid() ? tr("Change Track Colour") : tr("Default Track Colour"), [&](Scene& s) { s.layers[layer].trackColor = color; });
}

void Document::setAllLocked(bool locked, int except)
{
    bool any = false;
    for (int l = 0; l < m_scene.layers.size(); ++l)
        any = any || (l != except && m_scene.layers[l].locked != locked);
    if (!any)
        return;
    apply(locked ? tr("Lock Layers") : tr("Unlock Layers"), [&](Scene& s) {
        for (int l = 0; l < s.layers.size(); ++l)
            if (l != except)
                s.layers[l].locked = locked;
    });
}

void Document::setAllVisible(bool visible, int except)
{
    bool any = false;
    for (int l = 0; l < m_scene.layers.size(); ++l)
        any = any || (l != except && m_scene.layers[l].visible != visible);
    if (!any)
        return;
    apply(visible ? tr("Enable Layers") : tr("Disable Layers"), [&](Scene& s) {
        for (int l = 0; l < s.layers.size(); ++l)
            if (l != except)
                s.layers[l].visible = visible;
    });
}

void Document::setAllCollapsed(bool collapsed)
{
    bool any = false;
    for (int l = 0; l < m_scene.layers.size(); ++l)
        any = any || (m_scene.hasChildren(l) && m_scene.layers[l].collapsed != collapsed);
    if (!any)
        return;
    apply(collapsed ? tr("Collapse All") : tr("Expand All"), [&](Scene& s) {
        for (int l = 0; l < s.layers.size(); ++l)
            s.layers[l].collapsed = collapsed && s.hasChildren(l);
    });
}

void Document::clearSolo()
{
    bool any = false;
    for (Layer const& l : m_scene.layers)
        any = any || l.solo;
    if (!any)
        return;
    apply(tr("Solo Off"), [&](Scene& s) {
        for (Layer& l : s.layers)
            l.solo = false;
    });
}

int Document::duplicateLayer(int layer)
{
    if (layer < 0 || layer >= m_scene.layers.size())
        return -1;
    int made = -1;
    apply(tr("Duplicate Layer"), [&](Scene& s) { made = s.duplicateLayer(layer); });
    return made;
}
