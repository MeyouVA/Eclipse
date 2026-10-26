#include "model/scene/Layer.h"
#include "model/scene/PointIdentity.h"
#include "model/geometry/PointEdit.h"
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
        for (int line = 0; line < view.strokesOf(sheet).size(); ++line) {
            if (renew || !view.strokesOf(sheet)[line].sourceIdentity)
                drawing.strokesOf(sheet)[line].sourceIdentity = nextPointIdentity();
            assign(view.strokesOf(sheet)[line], [&](int p) -> StrokePoint& { return drawing.strokesOf(sheet)[line].points[p]; });
        }
        for (int fill = 0; fill < view.fillsOf(sheet).size(); ++fill) {
            if (renew || !view.fillsOf(sheet)[fill].sourceIdentity)
                drawing.fillsOf(sheet)[fill].sourceIdentity = nextPointIdentity();
            for (int contour = 0; contour < view.fillsOf(sheet)[fill].contours.size(); ++contour) {
                if (renew || !view.fillsOf(sheet)[fill].contours[contour].sourceIdentity)
                    drawing.fillsOf(sheet)[fill].contours[contour].sourceIdentity = nextPointIdentity();
                assign(view.fillsOf(sheet)[fill].contours[contour], [&](int p) -> StrokePoint& {
                    return drawing.fillsOf(sheet)[fill].contours[contour].points[p];
                });
            }
        }
    }
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
    DrawingPoints::retarget(*this, drawing, before, after);
}
