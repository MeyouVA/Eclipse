#include "model/scene/Layer.h"
#include <atomic>
#include <QSet>

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
    }
}
