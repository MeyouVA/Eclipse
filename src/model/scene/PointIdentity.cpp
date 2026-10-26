#include "model/scene/Layer.h"
#include "model/scene/PointIdentity.h"
#include <QHash>
#include <QSet>
#include <atomic>
#include <algorithm>

namespace {

quint64 nextPointIdentity()
{
    static std::atomic<quint64> identity{1};
    return identity.fetch_add(1, std::memory_order_relaxed);
}

void assignIdentities(Drawing& drawing, bool renew)
{
    QSet<quint64> seen;
    auto assign = [&](Stroke const& line, auto writable) {
        for (int p = 0; p < line.points.size(); ++p) {
            quint64 identity = line.points[p].editIdentity;
            if (renew || !identity || seen.contains(identity)) {
                identity = nextPointIdentity();
                writable(p).editIdentity = identity;
            }
            seen.insert(identity);
        }
    };
    Drawing const& view = drawing;
    for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
        for (int line = 0; line < view.strokesOf(sheet).size(); ++line)
            assign(view.strokesOf(sheet)[line], [&](int p) -> StrokePoint& { return drawing.strokesOf(sheet)[line].points[p]; });
        for (int fill = 0; fill < view.fillsOf(sheet).size(); ++fill)
            for (int contour = 0; contour < view.fillsOf(sheet)[fill].contours.size(); ++contour)
                assign(view.fillsOf(sheet)[fill].contours[contour], [&](int p) -> StrokePoint& {
                    return drawing.fillsOf(sheet)[fill].contours[contour].points[p];
                });
    }
}

Stroke const* lineOf(Drawing const& drawing, PointMove const& move)
{
    if (move.sheet < 0 || move.sheet >= Drawing::SheetCount || move.line < 0)
        return nullptr;
    if (move.fill < 0) {
        auto const& lines = drawing.strokesOf(move.sheet);
        return move.line < lines.size() ? &lines[move.line] : nullptr;
    }
    auto const& fills = drawing.fillsOf(move.sheet);
    if (move.fill >= fills.size())
        return nullptr;
    auto const& contours = fills[move.fill].contours;
    return move.line < contours.size() ? &contours[move.line] : nullptr;
}

bool reversed(Stroke const& before, Stroke const& after)
{
    int const count = int(before.points.size());
    if (count < 2 || after.points.size() != count)
        return false;
    for (int p = 0; p < count; ++p)
        if (before.points[p].editIdentity != after.points[count - 1 - p].editIdentity)
            return false;
    return true;
}

} // namespace

void Drawing::keepPointIdentitiesUnique() { assignIdentities(*this, false); }
void Drawing::renewPointIdentities() { assignIdentities(*this, true); }

void DrawingPoints::inheritCorresponding(Drawing& drawing, Drawing const& before)
{
    auto corresponds = [](Stroke const& now, Stroke const& was) {
        if (now.points.size() != was.points.size() || now.closed != was.closed)
            return false;
        for (StrokePoint const& point : now.points)
            if (point.editIdentity)
                return false;
        return true;
    };
    Drawing const& view = drawing;
    for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
        if (view.strokesOf(sheet).size() != before.strokesOf(sheet).size() || view.fillsOf(sheet).size() != before.fillsOf(sheet).size())
            return;
        for (int line = 0; line < view.strokesOf(sheet).size(); ++line)
            if (!corresponds(view.strokesOf(sheet)[line], before.strokesOf(sheet)[line]))
                return;
        for (int fill = 0; fill < view.fillsOf(sheet).size(); ++fill) {
            auto const& now = view.fillsOf(sheet)[fill].contours;
            auto const& was = before.fillsOf(sheet)[fill].contours;
            if (now.size() != was.size())
                return;
            for (int line = 0; line < now.size(); ++line)
                if (!corresponds(now[line], was[line]))
                    return;
        }
    }
    auto inherit = [](Stroke& now, Stroke const& was) {
        for (int p = 0; p < now.points.size(); ++p)
            now.points[p].editIdentity = was.points[p].editIdentity;
    };
    for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
        for (int line = 0; line < view.strokesOf(sheet).size(); ++line)
            inherit(drawing.strokesOf(sheet)[line], before.strokesOf(sheet)[line]);
        for (int fill = 0; fill < view.fillsOf(sheet).size(); ++fill)
            for (int line = 0; line < view.fillsOf(sheet)[fill].contours.size(); ++line)
                inherit(drawing.fillsOf(sheet)[fill].contours[line], before.fillsOf(sheet)[fill].contours[line]);
    }
}

void Layer::remapPointReferences(int drawing, Drawing const& before, Drawing const& after)
{
    bool any = false;
    for (Keyframe const& key : keys)
        for (PointMove const& move : key.pose.points)
            any = any || move.drawing == drawing;
    if (!any)
        return;
    QHash<quint64, PointMove> destinations;
    auto remember = [&](Stroke const& line, PointMove address) {
        for (int p = 0; p < line.points.size(); ++p) {
            address.point = p;
            if (line.points[p].editIdentity)
                destinations.insert(line.points[p].editIdentity, address);
        }
    };
    for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
        PointMove address;
        address.drawing = drawing;
        address.sheet = sheet;
        for (int line = 0; line < after.strokesOf(sheet).size(); ++line) {
            address.line = line;
            remember(after.strokesOf(sheet)[line], address);
        }
        for (int fill = 0; fill < after.fillsOf(sheet).size(); ++fill) {
            address.fill = fill;
            for (int contour = 0; contour < after.fillsOf(sheet)[fill].contours.size(); ++contour) {
                address.line = contour;
                remember(after.fillsOf(sheet)[fill].contours[contour], address);
            }
        }
    }
    for (Keyframe& key : keys) {
        QVector<PointMove> kept;
        for (PointMove move : key.pose.points) {
            if (move.drawing == drawing) {
                Stroke const* const line = lineOf(before, move);
                if (!line || move.point < 0 || move.point >= line->points.size())
                    continue;
                auto const found = destinations.constFind(line->points[move.point].editIdentity);
                if (found == destinations.constEnd())
                    continue;
                if (Stroke const* const target = lineOf(after, *found); target && reversed(*line, *target))
                    std::swap(move.in, move.out);
                move.sheet = found->sheet;
                move.fill = found->fill;
                move.line = found->line;
                move.point = found->point;
            }
            kept.push_back(move);
        }
        std::sort(kept.begin(), kept.end(), [](PointMove const& a, PointMove const& b) { return a.comesBefore(b); });
        key.pose.points = kept;
    }
}
