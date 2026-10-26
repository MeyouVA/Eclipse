#include "model/scene/Layer.h"
#include <atomic>
#include <QSet>

namespace {

bool needsPointIdentities(Drawing const& drawing)
{
    QSet<quint64> seen;
    auto missing = [&](Stroke const& line) {
        for (StrokePoint const& point : line.points) {
            if (!point.editIdentity || seen.contains(point.editIdentity))
                return true;
            seen.insert(point.editIdentity);
        }
        return false;
    };
    for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
        for (Stroke const& line : drawing.strokesOf(sheet))
            if (missing(line))
                return true;
        for (Fill const& fill : drawing.fillsOf(sheet))
            for (Stroke const& contour : fill.contours)
                if (missing(contour))
                    return true;
    }
    return false;
}

} // namespace

quint64 nextDrawingIdentity()
{
    static std::atomic<quint64> identity{1};
    return identity.fetch_add(1, std::memory_order_relaxed);
}

void Layer::keepDrawingIdentitiesUnique()
{
    QSet<quint64> seen;
    for (int drawing = 0; drawing < drawings.size(); ++drawing) {
        quint64 identity = std::as_const(drawings)[drawing].editIdentity;
        if (!identity || seen.contains(identity)) {
            identity = nextDrawingIdentity();
            drawings[drawing].editIdentity = identity;
        }
        seen.insert(identity);
        if (needsPointIdentities(std::as_const(drawings)[drawing]))
            drawings[drawing].keepPointIdentitiesUnique();
    }
}
