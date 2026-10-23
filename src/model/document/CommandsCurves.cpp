// Eclipse - the document: the Function view's edits - values' own curves, values set, moved and taken out of keyframes.
#include "model/document/Commands.h"

#include "model/document/CommandsPrivate.h"

void Document::setValueCurves(int layer, QVector<CurveChange> const& changes, QString const& text)
{
    if (!hasCurve(m_scene, layer) || changes.isEmpty())
        return;
    Layer const& l = curveLayer(m_scene, layer);
    bool differs = false;
    for (CurveChange const& c : changes) {
        int const k = l.keyIndex(c.frame);
        if (k < 0 || c.curve.value < 0 || c.curve.value >= Driver::ValueCount)
            return;
        ValueCurve const* now = l.keys[k].curveOf(c.curve.value);
        differs = differs || (c.drop ? now != nullptr : !now || !(*now == c.curve));
    }
    if (!differs)
        return;
    apply(text, [&](Scene& s) {
        Layer& to = curveLayer(s, layer);
        for (CurveChange const& c : changes) {
            Keyframe& key = to.keys[to.keyIndex(c.frame)];
            if (c.drop) {
                key.dropCurve(c.curve.value);
                continue;
            }
            ValueCurve curve = c.curve;
            curve.in.setX(std::min(0.0, curve.in.x()));
            curve.out.setX(std::max(0.0, curve.out.x()));
            key.setCurve(curve);
        }
    });
}

void Document::setKeyValues(int layer, QVector<ValueChange> const& changes, QString const& text)
{
    if (!hasCurve(m_scene, layer) || changes.isEmpty())
        return;
    Layer const& l = curveLayer(m_scene, layer);
    bool differs = false;
    for (ValueChange const& c : changes) {
        if (c.frame < 0 || c.frame >= m_scene.frameCount() || c.value < 0 || c.value >= Driver::ValueCount)
            return;
        int const k = l.keyIndex(c.frame);
        differs = differs || k < 0 || Driver::read(l.keys[k].pose, c.value) != c.to || !l.keys[k].sets(1 << c.value);
    }
    if (!differs)
        return;
    // The poses of new keyframes: as the layer is on their frames now.
    QMap<int, Pose> made;
    for (ValueChange const& c : changes)
        if (l.keyIndex(c.frame) < 0 && !made.contains(c.frame))
            made.insert(c.frame, l.poseAt(c.frame));
    apply(text, [&](Scene& s) {
        Layer& to = curveLayer(s, layer);
        for (auto it = made.cbegin(); it != made.cend(); ++it) {
            int bits = 0;
            for (ValueChange const& c : changes)
                if (c.frame == it.key())
                    bits |= 1 << c.value;
            keyOn(to, it.key(), it.value());
            to.keys[to.keyIndex(it.key())].parts = bits;
        }
        for (ValueChange const& c : changes) {
            Keyframe& key = to.keys[to.keyIndex(c.frame)];
            double v = c.to;
            if (c.value == Driver::Opacity)
                v = std::clamp(v, 0.0, 1.0);
            Driver::write(key.pose, c.value, v);
            key.parts |= 1 << c.value;
        }
    });
}

bool Document::moveValuePoints(int layer, QVector<QPair<int, int>> const& points, int by, QString const& text)
{
    if (!hasCurve(m_scene, layer) || points.isEmpty() || by == 0)
        return false;
    Layer trial = curveLayer(m_scene, layer);
    if (!trial.moveValues(points, by, m_scene.frameCount() - 1))
        return false;
    apply(text, [&](Scene& s) { curveLayer(s, layer).moveValues(points, by, m_scene.frameCount() - 1); });
    return true;
}

void Document::removeKeyValues(int layer, QVector<QPair<int, int>> const& points, QString const& text)
{
    if (!hasCurve(m_scene, layer) || points.isEmpty())
        return;
    Layer trial = curveLayer(m_scene, layer);
    trial.removeValues(points);
    QVector<Keyframe> const& now = curveLayer(m_scene, layer).keys;
    bool same = trial.keys.size() == now.size();
    for (int k = 0; same && k < now.size(); ++k)
        same = trial.keys[k].parts == now[k].parts && trial.keys[k].curves == now[k].curves;
    if (same)
        return;
    apply(text, [&](Scene& s) { curveLayer(s, layer).removeValues(points); });
}

void Document::setSegmentEases(QVector<SegmentEase> const& eases, QString const& text)
{
    QVector<SegmentEase> valid;
    for (SegmentEase const& e : eases) {
        if (!hasCurve(m_scene, e.layer) || e.value < 0 || e.value >= Driver::ValueCount)
            continue;
        Layer const& l = curveLayer(m_scene, e.layer);
        if (l.keyIndex(e.from) >= 0 && l.keyIndex(e.to) > l.keyIndex(e.from))
            valid.push_back(e);
    }
    if (valid.isEmpty())
        return;
    apply(text, [&](Scene& s) {
        for (SegmentEase const& e : valid) {
            Layer& l = curveLayer(s, e.layer);
            l.shapeSegment(e.value, l.keyIndex(e.from), l.keyIndex(e.to), e.leave, e.arrive);
        }
    });
}
