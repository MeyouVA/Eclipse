// Eclipse - remapping column identifiers and expression references when scenes are combined.
#include "model/scene/TemplateFunctions.h"
#include <algorithm>

namespace {
QString expression(QString const& text, QHash<QString, QString> const& names)
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
            else if (lower == QLatin1String("frame") || lower == QLatin1String("currentframe") || lower == QLatin1String("numframes") || lower == QLatin1String("pi"))
                out += token;
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
        column.extendedTiming = true;
        if (column.kind == FunctionColumn::Expression) {
            column.expressionOffset += offset;
            if (column.expressionFrames <= 0)
                column.expressionFrames = source.frameCount();
        }
        if (column.kind == FunctionColumn::Path3D) {
            if (!column.velocityRangeSet) {
                column.velocityStart = 0;
                column.velocityEnd = source.frameCount() - 1;
                column.velocityRangeSet = true;
            }
            column.velocityStart += offset;
            column.velocityEnd += offset;
        }
        for (Layer* curve : {&column.curve, &column.velocity})
            for (Keyframe& key : curve->keys)
                key.frame += offset;
        for (FunctionColumn::PathPoint& point : column.path)
            if (point.timeLocked()) {
                point.frame += offset;
                point.locked = true;
            }
        target.functions << column;
    }
    for (int i = begin; i < target.functions.size(); ++i)
        target.functions[i].expression = expression(target.functions[i].expression, names);
    return ids;
}

void TemplateFunctions::relink(Layer& layer, QHash<int, int> const& functions)
{
    for (ValueLink& link : layer.links)
        link.function = functions.value(link.function, 0);
    layer.links.removeIf([](ValueLink const& link) { return link.function <= 0; });
}
