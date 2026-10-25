// Eclipse - remapping column identifiers and expression references when scenes are combined.
#include "model/scene/TemplateFunctions.h"
#include <algorithm>

namespace {
QString expression(QString const& text, QHash<QString, QString> const& names, int offset, int frames)
{
    QString out;
    for (qsizetype at = 0; at < text.size(); ) {
        QChar const c = text[at];
        if (c == QLatin1Char('"')) {
            qsizetype const end = text.indexOf(QLatin1Char('"'), at + 1);
            if (end < 0)
                return text;
            QString const name = text.mid(at + 1, end - at - 1);
            out += QLatin1Char('"') + names.value(name.toLower(), name) + QLatin1Char('"');
            at = end + 1;
        } else if (c.isLetter() || c == QLatin1Char('_')) {
            qsizetype const start = at++;
            while (at < text.size() && (text[at].isLetterOrNumber() || text[at] == QLatin1Char('_')))
                ++at;
            QString const token = text.mid(start, at - start), lower = token.toLower();
            qsizetype after = at;
            while (after < text.size() && text[after].isSpace())
                ++after;
            if (after < text.size() && text[after] == QLatin1Char('('))
                out += token;
            else if (lower == QLatin1String("frame") || lower == QLatin1String("currentframe"))
                out += QStringLiteral("(frame-%1)").arg(offset);
            else if (lower == QLatin1String("numframes"))
                out += QString::number(frames);
            else if (names.contains(lower))
                out += QLatin1Char('"') + names[lower] + QLatin1Char('"');
            else
                out += token;
        } else {
            out += c;
            ++at;
        }
    }
    return out;
}
}

QHash<int, int> TemplateFunctions::import(Scene& target, Scene const& source, int offset)
{
    QHash<int, int> ids;
    QHash<QString, QString> names;
    int const begin = int(target.functions.size());
    for (FunctionColumn column : source.functions) {
        for (FunctionColumn const& existing : std::as_const(target.functions))
            target.nextFunctionId = std::max(target.nextFunctionId, existing.id + 1);
        int const old = column.id;
        column.id = target.nextFunctionId++;
        QString const name = column.name;
        column.name = target.uniqueFunctionName(name);
        ids.insert(old, column.id);
        names.insert(name.toLower(), column.name);
        for (Layer* curve : {&column.curve, &column.velocity})
            for (Keyframe& key : curve->keys)
                key.frame += offset;
        for (FunctionColumn::PathPoint& point : column.path)
            if (point.frame >= 0)
                point.frame = std::max(0, point.frame + offset);
        target.functions << column;
    }
    for (int i = begin; i < target.functions.size(); ++i)
        target.functions[i].expression = expression(target.functions[i].expression, names, offset, source.frameCount());
    return ids;
}

void TemplateFunctions::relink(Layer& layer, QHash<int, int> const& functions)
{
    for (ValueLink& link : layer.links)
        link.function = functions.value(link.function, 0);
    layer.links.removeIf([](ValueLink const& link) { return link.function <= 0; });
}

void TemplateFunctions::crop(Layer& curve, int first, int last)
{
    if (curve.keys.isEmpty())
        return;
    auto insert = [&](int frame) {
        if (curve.keyIndex(frame) >= 0)
            return;
        QVector<int> const through = curve.keysOf(Driver::X);
        Keyframe key;
        key.frame = frame;
        key.parts = Keyframe::PartX;
        key.pose.x = curve.curveValue(Driver::X, through, frame);
        int right = 0;
        while (right < curve.keys.size() && curve.keys[right].frame < frame)
            ++right;
        if (right > 0 && right < curve.keys.size()) {
            Keyframe& a = curve.keys[right - 1];
            Keyframe& b = curve.keys[right];
            ValueCurve left = curve.curveAt(Driver::X, right - 1), middle = left, end = curve.curveAt(Driver::X, right);
            key.tween = a.tween;
            if (a.tween && left.after == ValueCurve::Curved) {
                QPointF out, in;
                curve.segmentHandles(Driver::X, right - 1, right, out, in);
                QPointF const p0(a.frame, a.pose.x), p3(b.frame, b.pose.x);
                QPointF const p1(std::clamp(a.frame + out.x(), double(a.frame), double(b.frame)), a.pose.x + out.y());
                QPointF const p2(std::clamp(b.frame + in.x(), double(a.frame), double(b.frame)), b.pose.x + in.y());
                auto mix = [](QPointF p, QPointF q, double t) { return p * (1.0 - t) + q * t; };
                double low = 0, high = 1;
                for (int i = 0; i < 48; ++i) {
                    double const t = (low + high) / 2;
                    QPointF const q = mix(mix(mix(p0, p1, t), mix(p1, p2, t), t), mix(mix(p1, p2, t), mix(p2, p3, t), t), t);
                    if (q.x() < frame) low = t; else high = t;
                }
                double const t = (low + high) / 2;
                QPointF const a0 = mix(p0, p1, t), a1 = mix(p1, p2, t), a2 = mix(p2, p3, t);
                QPointF const b0 = mix(a0, a1, t), b1 = mix(a1, a2, t), point = mix(b0, b1, t);
                left.out = a0 - p0;
                middle.in = b0 - point;
                middle.out = b1 - point;
                end.in = a2 - p3;
                a.setCurve(left);
                b.setCurve(end);
            }
            key.setCurve(middle);
        }
        curve.keys.insert(right, key);
    };
    insert(first);
    insert(last);
    curve.keys.removeIf([&](Keyframe const& key) { return key.frame < first || key.frame > last; });
}
