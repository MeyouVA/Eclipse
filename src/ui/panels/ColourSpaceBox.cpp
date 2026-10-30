// Eclipse - a list of the colour spaces of the configuration in use.
#include "ui/panels/ColourSpaceBox.h"

#include "colour/spaces/ColourConfig.h"

#include <QCoreApplication>
#include <QSignalBlocker>

namespace ColourSpaceBox {

QStringList names()
{
    return Cms::config()->spaceNames();
}

void fill(QComboBox* box, QString const& current, QString const& first)
{
    QSignalBlocker const quiet(box);
    box->clear();
    if (!first.isEmpty())
        box->addItem(first, QString());
    std::shared_ptr<Cms::Config const> const c = Cms::config();
    for (QString const& name : c->spaceNames()) {
        Cms::Space const* const s = c->space(name);
        box->addItem(name, name);
        if (s && !s->description.isEmpty())
            box->setItemData(box->count() - 1, s->description, Qt::ToolTipRole);
    }
    int at = current.isEmpty() ? 0 : box->findData(current);
    if (at < 0 && !current.isEmpty()) {
        // A name the configuration does not have: kept, and said to be missing.
        Cms::Space const* const s = c->space(current);
        at = s ? box->findData(s->name) : -1;
        if (at < 0) {
            box->addItem(QCoreApplication::translate("Colour", "%1 (Colour Space Unavailable)").arg(current), current);
            at = box->count() - 1;
        }
    }
    box->setCurrentIndex(std::max(at, 0));
}

QComboBox* make(QWidget* parent, QString const& objectName, QString const& current, QString const& first)
{
    QComboBox* const box = new QComboBox(parent);
    box->setObjectName(objectName);
    box->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    box->setMinimumContentsLength(14);
    fill(box, current, first);
    return box;
}

QString chosen(QComboBox const* box)
{
    return box->currentData().toString();
}

}  // namespace ColourSpaceBox
