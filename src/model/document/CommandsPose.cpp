// Eclipse - animation tools writing a visible pose into local values and linked curves.
#include "model/document/Commands.h"

#include <cmath>

bool Document::animatePose(int layer, int frame, Pose const& from, Pose const& to, QString const& text,
                           int parts, bool offset, int first, int last)
{
    if (layer < 0 || layer >= m_scene.layers.size() || frame < 0 || frame >= m_scene.frameCount())
        return false;
    Layer const before = m_scene.layers[layer];
    Pose const local = before.poseAt(frame);
    QMap<int, QVector<ValueChange>> columns;
    QMap<int, double> scales;
    Pose localFrom = from, localTo = to;
    int localParts = parts;
    for (ValueLink const& link : before.links) {
        int const index = m_scene.functionIndex(link.function);
        if (index < 0)
            continue;
        localParts &= ~(1 << link.value);
        double const was = Driver::read(from, link.value), now = Driver::read(to, link.value);
        Driver::write(localFrom, link.value, Driver::read(local, link.value));
        Driver::write(localTo, link.value, Driver::read(local, link.value));
        if (!(parts & (1 << link.value)) || std::abs(now - was) < 1e-9)
            continue;
        FunctionColumn const& column = m_scene.functions[index];
        if (column.kind != FunctionColumn::Bezier && column.kind != FunctionColumn::Ease)
            return false;
        QVector<ValueChange> changes;
        bool const scale = link.value == Driver::ScaleX || link.value == Driver::ScaleY;
        double const factor = scale && std::abs(was) > 1e-9 ? now / was : 1.0;
        if (offset) {
            for (Keyframe const& k : column.curve.keys) {
                if (first >= 0 && last >= first && (k.frame < first || k.frame > last))
                    continue;
                double const v = scale && std::abs(was) > 1e-9 ? k.pose.x * factor : k.pose.x + now - was;
                changes.push_back({k.frame, Driver::X, v});
            }
        }
        if (changes.isEmpty())
            changes.push_back({frame, Driver::X, now});
        if (columns.contains(index)) {
            QVector<ValueChange> const& other = columns[index];
            if (other.size() != changes.size())
                return false;
            for (int i = 0; i < other.size(); ++i)
                if (other[i].frame != changes[i].frame || std::abs(other[i].to - changes[i].to) > 1e-9)
                    return false;
        }
        columns[index] = changes;
        scales[index] = factor;
    }
    beginMacro(text);
    for (auto it = columns.cbegin(); it != columns.cend(); ++it) {
        setKeyValues(-1 - it.key(), it.value(), text);
        if (offset && scales.value(it.key(), 1.0) != 1.0) {
            Layer const curve = m_scene.functions[it.key()].curve;
            QVector<CurveChange> handles;
            for (ValueChange const& c : it.value()) {
                int const key = curve.keyIndex(c.frame);
                if (key < 0 || !curve.keys[key].curveOf(Driver::X))
                    continue;
                ValueCurve shaped = *curve.keys[key].curveOf(Driver::X);
                shaped.in.ry() *= scales[it.key()];
                shaped.out.ry() *= scales[it.key()];
                handles.push_back({c.frame, shaped, false});
            }
            setValueCurves(-1 - it.key(), handles, text);
        }
    }
    if (localParts != 0) {
        if (offset)
            shiftPoses(layer, frame, localFrom, localTo, text, first, last);
        else if (localParts == Keyframe::AllParts)
            setKeyframe(layer, frame, localTo, text);
        else
            setPartKeyframe(layer, frame, localParts, localTo, text);
    }
    endMacro();
    return true;
}
