// Eclipse - Preferences > Node View: how the graph is shown and worked, its magnifier and backdrops, and how pictures read in are placed.
#include "ui/window/MainWindow.h"
#include "ui/io/PreferenceWidgets.h"

#include <algorithm>

#include "ui/views/NodeView.h"

namespace {

QSpinBox* spin(QWidget* parent, char const* name, int low, int high, int value, QString const& suffix = QString())
{
    QSpinBox* const box = new QSpinBox(parent);
    box->setObjectName(QLatin1String(name));
    box->setRange(low, high);
    box->setValue(std::clamp(value, low, high));
    box->setSuffix(suffix);
    return box;
}

QDoubleSpinBox* number(QWidget* parent, char const* name, double low, double high, double value)
{
    QDoubleSpinBox* const box = new QDoubleSpinBox(parent);
    box->setObjectName(QLatin1String(name));
    box->setRange(low, high);
    box->setDecimals(1);
    box->setSingleStep(0.1);
    box->setValue(std::clamp(value, low, high));
    return box;
}

QCheckBox* check(QWidget* parent, char const* name, QString const& text, bool value)
{
    QCheckBox* const box = new QCheckBox(text, parent);
    box->setObjectName(QLatin1String(name));
    box->setChecked(value);
    return box;
}

QComboBox* choice(QWidget* parent, char const* name, QStringList const& items, int value)
{
    QComboBox* const box = new QComboBox(parent);
    box->setObjectName(QLatin1String(name));
    box->addItems(items);
    box->setCurrentIndex(std::clamp(value, 0, int(items.size()) - 1));
    return box;
}

// The cable type: as set, or (from before there was a choice) straight or curved.
int cableType(QSettings const& s)
{
    return s.contains(QStringLiteral("prefs/nodeCableType")) ? std::clamp(s.value(QStringLiteral("prefs/nodeCableType")).toInt(), 0, 2)
                                                             : (s.value(QStringLiteral("prefs/nodeStraight"), false).toBool() ? 1 : 0);
}

}  // namespace

void MainWindow::preferencesNodeView(PreferenceWidgets& w, QSettings& s, QDialog& dialog)
{
    w.nodePage = new QGroupBox(tr("Node View"), &dialog);
    QWidget* const page = w.nodePage;
    w.nodeLayout = new QFormLayout(page);
    // The navigator (the small map of the graph in a corner) when Eclipse starts.
    w.navigator = check(page, "PrefNodeNavigator", tr("Show Navigator"), s.value(QStringLiteral("prefs/nodeNavigator"), true).toBool());
    w.nodeLayout->addRow(w.navigator);
    w.nodeNavigatorCorner = choice(page, "PrefNodeNavigatorCorner", {tr("Top Left"), tr("Top Right"), tr("Bottom Right"), tr("Bottom Left")},
                                   s.value(QStringLiteral("prefs/nodeNavigatorCorner"), 2).toInt());
    w.nodeLayout->addRow(tr("Navigator Starting Corner"), w.nodeNavigatorCorner);
    w.nodeCableType = choice(page, "PrefNodeCableType", {tr("Bezier"), tr("Line"), tr("Straight")}, cableType(s));
    w.nodeLayout->addRow(tr("Cable Type"), w.nodeCableType);
    w.nodeWideCables = check(page, "PrefNodeWideCables", tr("Two Colour Cables for Pass Through Connections"),
                             s.value(QStringLiteral("prefs/nodeWideCables"), true).toBool());
    w.nodeWideCables->setToolTip(tr("Cables out of a pass-through composite carry several pictures: they are drawn wider, in two colours."));
    w.nodeLayout->addRow(w.nodeWideCables);
    w.nodePortOrder = choice(page, "PrefNodePortOrder", {tr("Left to Right"), tr("Right to Left")}, s.value(QStringLiteral("prefs/nodePortOrder"), 0).toInt());
    w.nodePortOrder->setToolTip(tr("Which end of a composite its first input (the front picture) is at."));
    w.nodeLayout->addRow(tr("Port Input Ordering"), w.nodePortOrder);
    w.nodeDoubleClick = choice(page, "PrefNodeDoubleClick", {tr("Renames the layer"), tr("Opens Layer Properties")},
                               s.value(QStringLiteral("prefs/nodeDoubleClick"), 0).toInt());
    w.nodeLayout->addRow(tr("A double-click on a layer's node"), w.nodeDoubleClick);
    w.nodePanMiddle = check(page, "PrefNodePanMiddle", tr("Middle Mouse Button Pans the View"), s.value(QStringLiteral("prefs/nodePanMiddle"), true).toBool());
    w.nodePanMiddle->setToolTip(tr("Off: a middle drag moves the nodes picked instead."));
    w.nodeLayout->addRow(w.nodePanMiddle);
    w.nodeReadColour = check(page, "PrefNodeReadColour", tr("Read Colour"), s.value(QStringLiteral("prefs/nodeReadColour"), true).toBool());
    w.nodeReadColour->setToolTip(tr("A drawing layer's node takes the layer's colour, when it has one."));
    w.nodeLayout->addRow(w.nodeReadColour);
    w.nodeFollowParenting = check(page, "PrefNodeFollowParenting", tr("Follow Node View Parenting for Node Navigation"),
                                  s.value(QStringLiteral("prefs/nodeFollowParenting"), false).toBool());
    w.nodeFollowParenting->setToolTip(tr("In a graph wired by hand, Select Parent and Select Child go along the Node View's cables instead of the Timeline's parenting."));
    w.nodeLayout->addRow(w.nodeFollowParenting);
    // The magnifier (Z in the Node View).
    w.nodeMagnifierWidth = number(page, "PrefNodeMagnifierWidth", 0.1, 10.0, s.value(QStringLiteral("prefs/nodeMagnifierWidth"), 6.0).toDouble());
    w.nodeMagnifierWidth->setToolTip(tr("The width of the magnifier in tenths of the Node View's width (0.1 to 10)."));
    w.nodeLayout->addRow(tr("Magnifier Width Multiple"), w.nodeMagnifierWidth);
    w.nodeMagnifierAspect = number(page, "PrefNodeMagnifierAspect", 0.1, 10.0, s.value(QStringLiteral("prefs/nodeMagnifierAspect"), 1.5).toDouble());
    w.nodeLayout->addRow(tr("Magnifier Aspect Ratio"), w.nodeMagnifierAspect);
    w.nodeMagnifierOpacity = spin(page, "PrefNodeMagnifierOpacity", 0, 100, s.value(QStringLiteral("prefs/nodeMagnifierOpacity"), 80).toInt(), tr(" %"));
    w.nodeLayout->addRow(tr("Magnifier Opacity"), w.nodeMagnifierOpacity);
    w.nodeMagnifierScale = number(page, "PrefNodeMagnifierScale", 1.0, 100.0, s.value(QStringLiteral("prefs/nodeMagnifierScale"), 5.0).toDouble());
    w.nodeLayout->addRow(tr("Magnifier Zoom Factor"), w.nodeMagnifierScale);
    // Backdrops.
    w.nodeBackdropSelected = spin(page, "PrefBackdropSelected", 0, 255, s.value(QStringLiteral("prefs/backdropSelected"), 220).toInt());
    w.nodeLayout->addRow(tr("Backdrop Opacity, Selected"), w.nodeBackdropSelected);
    w.nodeBackdropUnselected = spin(page, "PrefBackdropUnselected", 0, 255, s.value(QStringLiteral("prefs/backdropUnselected"), 170).toInt());
    w.nodeLayout->addRow(tr("Backdrop Opacity, Not Selected"), w.nodeBackdropUnselected);
    w.nodeBackdropTitle = spin(page, "PrefBackdropTitleSize", 6, 72, s.value(QStringLiteral("prefs/backdropTitleSize"), 14).toInt());
    w.nodeLayout->addRow(tr("Backdrop Title Size"), w.nodeBackdropTitle);
    w.nodeBackdropColours = spin(page, "PrefBackdropColours", 1, 17, s.value(QStringLiteral("prefs/backdropColours"), 17).toInt());
    w.nodeLayout->addRow(tr("Backdrop Colours Offered"), w.nodeBackdropColours);
    // Pictures read in.
    w.importAlignment = choice(page, "PrefImportAlignment", {tr("Fit"), tr("Pan"), tr("Project Resolution (Actual Size)")},
                               s.value(QStringLiteral("prefs/importAlignment"), 0).toInt());
    w.importAlignment->setToolTip(tr("How an imported picture is placed in the frame, unless chosen otherwise when importing."));
    w.nodeLayout->addRow(tr("Alignment Rule"), w.importAlignment);
    w.importTransparency = choice(page, "PrefImportTransparency",
                                  {tr("Straight"), tr("Premultiplied with Black"), tr("Premultiplied with White"), tr("Clamp Colour to Alpha")},
                                  s.value(QStringLiteral("prefs/importTransparency"), 0).toInt());
    w.importTransparency->setToolTip(tr("How the colours of a picture's see-through parts are stored in its file."));
    w.nodeLayout->addRow(tr("Transparency of Imported Pictures"), w.importTransparency);
    // Attribute bindings.
    w.bindSharedColumns = check(page, "PrefBindSharedColumns", tr("Create new bindings on all attributes sharing the column"),
                                s.value(QStringLiteral("prefs/bindSharedColumns"), true).toBool());
    w.bindSharedColumns->setToolTip(tr("A binding made in Attribute Controls on a value linked to a function column is made on every value linked to that column."));
    w.nodeLayout->addRow(w.bindSharedColumns);
    // Composites.
    w.compositePassThrough = check(page, "PrefCompositePassThrough", tr("Default Pass Through Composite"),
                                   s.value(QStringLiteral("prefs/compositePassThrough"), false).toBool());
    w.compositePassThrough->setToolTip(tr("New composites pass their inputs on separately into the composite they feed (Pass Through mode)."));
    w.nodeLayout->addRow(w.compositePassThrough);
}

void MainWindow::saveNodePreferences(PreferenceWidgets& w, QSettings& s)
{
    s.setValue(QStringLiteral("prefs/nodeNavigator"), w.navigator->isChecked());
    s.setValue(QStringLiteral("prefs/nodeNavigatorCorner"), w.nodeNavigatorCorner->currentIndex());
    s.setValue(QStringLiteral("prefs/nodeCableType"), w.nodeCableType->currentIndex());
    s.setValue(QStringLiteral("prefs/nodeStraight"), w.nodeCableType->currentIndex() == 1);
    s.setValue(QStringLiteral("prefs/nodeWideCables"), w.nodeWideCables->isChecked());
    s.setValue(QStringLiteral("prefs/nodePortOrder"), w.nodePortOrder->currentIndex());
    s.setValue(QStringLiteral("prefs/nodeDoubleClick"), w.nodeDoubleClick->currentIndex());
    s.setValue(QStringLiteral("prefs/nodePanMiddle"), w.nodePanMiddle->isChecked());
    s.setValue(QStringLiteral("prefs/nodeReadColour"), w.nodeReadColour->isChecked());
    s.setValue(QStringLiteral("prefs/nodeFollowParenting"), w.nodeFollowParenting->isChecked());
    s.setValue(QStringLiteral("prefs/nodeMagnifierWidth"), w.nodeMagnifierWidth->value());
    s.setValue(QStringLiteral("prefs/nodeMagnifierAspect"), w.nodeMagnifierAspect->value());
    s.setValue(QStringLiteral("prefs/nodeMagnifierOpacity"), w.nodeMagnifierOpacity->value());
    s.setValue(QStringLiteral("prefs/nodeMagnifierScale"), w.nodeMagnifierScale->value());
    s.setValue(QStringLiteral("prefs/backdropSelected"), w.nodeBackdropSelected->value());
    s.setValue(QStringLiteral("prefs/backdropUnselected"), w.nodeBackdropUnselected->value());
    s.setValue(QStringLiteral("prefs/backdropTitleSize"), w.nodeBackdropTitle->value());
    s.setValue(QStringLiteral("prefs/backdropColours"), w.nodeBackdropColours->value());
    s.setValue(QStringLiteral("prefs/importAlignment"), w.importAlignment->currentIndex());
    s.setValue(QStringLiteral("prefs/importTransparency"), w.importTransparency->currentIndex());
    s.setValue(QStringLiteral("prefs/bindSharedColumns"), w.bindSharedColumns->isChecked());
    s.setValue(QStringLiteral("prefs/compositePassThrough"), w.compositePassThrough->isChecked());
}

void MainWindow::applyNodePreferences(QSettings const& s)
{
    NodeView::Options o;
    o.cableType = cableType(s);
    o.wideCables = s.value(QStringLiteral("prefs/nodeWideCables"), true).toBool();
    o.portsRightToLeft = s.value(QStringLiteral("prefs/nodePortOrder"), 0).toInt() == 1;
    o.magnifierWidth = std::clamp(s.value(QStringLiteral("prefs/nodeMagnifierWidth"), 6.0).toDouble(), 0.1, 10.0);
    o.magnifierAspect = std::clamp(s.value(QStringLiteral("prefs/nodeMagnifierAspect"), 1.5).toDouble(), 0.1, 10.0);
    o.magnifierOpacity = std::clamp(s.value(QStringLiteral("prefs/nodeMagnifierOpacity"), 80).toInt(), 0, 100);
    o.magnifierScale = std::clamp(s.value(QStringLiteral("prefs/nodeMagnifierScale"), 5.0).toDouble(), 1.0, 100.0);
    o.panMiddle = s.value(QStringLiteral("prefs/nodePanMiddle"), true).toBool();
    o.readColour = s.value(QStringLiteral("prefs/nodeReadColour"), true).toBool();
    o.backdropSelected = std::clamp(s.value(QStringLiteral("prefs/backdropSelected"), 220).toInt(), 0, 255);
    o.backdropUnselected = std::clamp(s.value(QStringLiteral("prefs/backdropUnselected"), 170).toInt(), 0, 255);
    o.backdropTitleSize = std::clamp(s.value(QStringLiteral("prefs/backdropTitleSize"), 14).toInt(), 6, 72);
    o.backdropColours = std::clamp(s.value(QStringLiteral("prefs/backdropColours"), 17).toInt(), 1, 17);
    o.compositePassThrough = s.value(QStringLiteral("prefs/compositePassThrough"), false).toBool();
    for (NodeView* const view : findChildren<NodeView*>()) {
        view->setOptions(o);
        view->setNavigatorShown(s.value(QStringLiteral("prefs/nodeNavigator"), true).toBool());
        if (s.contains(QStringLiteral("prefs/nodeNavigatorCorner")))
            view->setNavigatorCorner(s.value(QStringLiteral("prefs/nodeNavigatorCorner")).toInt());
        view->setDoubleClickOpensEditor(s.value(QStringLiteral("prefs/nodeDoubleClick"), 0).toInt() == 1);
    }
    m_followNodeParenting = s.value(QStringLiteral("prefs/nodeFollowParenting"), false).toBool();
}
