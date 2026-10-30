// Eclipse - the Layer Properties view: drawing settings (3D, alignment, flips, clipping), attributes published on a group,
// attributes of the user's own, and the commands of an attribute's button (offset, publish, information, bindings).
#include "ui/panels/LayerPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "actions/theme/InterfaceColours.h"
#include "model/document/Commands.h"
#include "ui/panels/ColourSpaceBox.h"

QString LayerPanel::valueName(int value)
{
    static char const* const kNames[] = {QT_TR_NOOP("Position X"), QT_TR_NOOP("Position Y"), QT_TR_NOOP("Scale X"), QT_TR_NOOP("Scale Y"),
                                         QT_TR_NOOP("Angle"), QT_TR_NOOP("Opacity"), QT_TR_NOOP("Depth (Z)"), QT_TR_NOOP("Skew"),
                                         QT_TR_NOOP("Field of View")};
    return value >= 0 && value < Driver::ValueCount ? tr(kNames[value]) : QString();
}

int LayerPanel::groupOf(int layer) const
{
    Scene const& scene = m_document->scene();
    for (int up = scene.parentIndex(layer), guard = 0; up >= 0 && guard < 256; up = scene.parentIndex(up), ++guard)
        if (scene.layers[up].kind == Layer::Group)
            return up;
    return -1;
}

void LayerPanel::togglePublished(int layer, int value)
{
    Scene const& scene = m_document->scene();
    int const group = groupOf(layer);
    if (group < 0) {
        emit message(tr("Only a value of a layer in a group can be published on the group."));
        return;
    }
    PublishedAttribute const wanted{scene.layers[layer].id, value};
    bool const on = !scene.layers[group].published.contains(wanted);
    m_document->editLayers({group}, on ? tr("Publish Attribute") : tr("Clear Published Attribute"), [wanted, on](Layer& g) {
        if (on)
            g.published.push_back(wanted);
        else
            g.published.removeAll(wanted);
    });
    emit message(on ? tr("%1 of %2 is now shown on the group %3.").arg(valueName(value), scene.layers[layer].name, scene.layers[group].name)
                    : tr("%1 of %2 is no longer shown on the group.").arg(valueName(value), scene.layers[layer].name));
}

void LayerPanel::addAttributeCommands(QMenu* menu, int value)
{
    Scene const& scene = m_document->scene();
    int const layer = m_layer;
    menu->addSeparator();
    QAction* const offset = menu->addAction(tr("Offset Attribute Value..."));
    offset->setObjectName(QStringLiteral("AttributeOffset"));
    connect(offset, &QAction::triggered, this, [this, layer, value] {
        bool okay = false;
        double const by = QInputDialog::getDouble(this, tr("Offset Attribute Value"), tr("Add to %1 in every keyframe:").arg(valueName(value)), 0.0, -100000.0, 100000.0,
                                                  3, &okay);
        if (okay && by != 0.0 && !m_document->offsetValue(layer, value, value == Driver::Opacity ? by / 100.0 : by))
            emit message(tr("%1 comes from a function column or another layer: offset it there.").arg(valueName(value)));
    });
    int const group = groupOf(layer);
    if (group >= 0) {
        QAction* const publish = menu->addAction(tr("Publish Attribute on %1").arg(scene.layers[group].name));
        publish->setObjectName(QStringLiteral("AttributePublish"));
        publish->setCheckable(true);
        publish->setChecked(scene.layers[group].published.contains(PublishedAttribute{scene.layers[layer].id, value}));
        connect(publish, &QAction::triggered, this, [this, layer, value] { togglePublished(layer, value); });
    }
    bool bound = false;
    for (Driver const& d : scene.layers[layer].drivers)
        bound = bound || d.value == value;
    if (bound) {
        QAction* const unbind = menu->addAction(tr("Remove Attribute Binding"));
        unbind->setObjectName(QStringLiteral("AttributeUnbind"));
        connect(unbind, &QAction::triggered, this, [this, layer, value] {
            m_document->editLayers({layer}, tr("Remove Attribute Binding"), [value](Layer& l) {
                l.drivers.erase(std::remove_if(l.drivers.begin(), l.drivers.end(), [value](Driver const& d) { return d.value == value; }), l.drivers.end());
            });
        });
    }
    connect(menu->addAction(tr("Attribute Information...")), &QAction::triggered, this, [this, layer] { emit attributeInformationWanted(layer); });
}

void LayerPanel::setPublishMode(bool on)
{
    m_publishMode = on;
    for (QToolButton* button : m_functionButtons)
        if (button)
            button->setProperty("publishing", on);
    emit message(on ? tr("Publish Attribute Mode: a click on a value's button shows it on the group the layer is in (again: no longer).") : QString());
}

void LayerPanel::buildAttributeSections()
{
    // ---- a drawing layer's settings ----
    m_drawingBox = new QWidget(this);
    QFormLayout* const drawing = new QFormLayout(m_drawingBox);
    drawing->setContentsMargins(0, 0, 0, 0);
    m_enable3d = new QCheckBox(tr("Enable 3D"), m_drawingBox);
    m_enable3d->setObjectName(QStringLiteral("LayerEnable3D"));
    m_enable3d->setToolTip(tr("Off: the layer stays flat - Turn in Space is left out, keyed or not."));
    connect(m_enable3d, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_refreshing)
            m_document->editLayers({m_layer}, on ? tr("Enable 3D") : tr("Disable 3D"), [on](Layer& l) { l.enable3d = on; });
    });
    drawing->addRow(m_enable3d);
    m_alignment = new QComboBox(m_drawingBox);
    m_alignment->setObjectName(QStringLiteral("LayerAlignmentRule"));
    m_alignment->addItems({tr("As Placed"), tr("Fit"), tr("Pan"), tr("Project Resolution (Actual Size)")});
    m_alignment->setToolTip(tr("The pictures of the layer's drawings fitted to the frame again: inside it, filling it, or a pixel for a pixel."));
    connect(m_alignment, &QComboBox::activated, this, [this](int rule) {
        if (m_refreshing)
            return;
        Scene const& scene = m_document->scene();
        double const width = scene.width, height = scene.height;
        m_document->editLayers({m_layer}, tr("Alignment Rule"), [rule, width, height](Layer& l) {
            l.alignment = rule;
            if (rule == 0)
                return;
            for (Drawing& d : l.drawings)
                for (Picture& picture : d.pictures) {
                    QSizeF const size = picture.size.isEmpty() ? QSizeF(picture.image.size()) : QSizeF(picture.size);
                    if (size.isEmpty())
                        continue;
                    double const across = width / size.width(), down = height / size.height();
                    double const scale = rule == 3 ? 1.0 : rule == 2 ? std::max(across, down) : std::min(across, down);
                    picture.placement = QTransform::fromScale(scale, scale)
                                        * QTransform::fromTranslate((width - size.width() * scale) / 2.0, (height - size.height() * scale) / 2.0);
                }
        });
    });
    drawing->addRow(tr("Alignment Rule"), m_alignment);
    QHBoxLayout* const flips = new QHBoxLayout;
    struct Switch {
        QCheckBox** box;
        char const* text;
        char const* name;
        bool Layer::*field;
    };
    Switch const switches[] = {{&m_turnBefore, QT_TR_NOOP("Turn Before Alignment"), "LayerTurnBeforeAlignment", &Layer::turnBeforeAlignment},
                               {&m_flipH, QT_TR_NOOP("Flip Horizontal"), "LayerFlipHorizontal", &Layer::flipHorizontal},
                               {&m_flipV, QT_TR_NOOP("Flip Vertical"), "LayerFlipVertical", &Layer::flipVertical}};
    for (Switch const& s : switches) {
        *s.box = new QCheckBox(tr(s.text), m_drawingBox);
        (*s.box)->setObjectName(QLatin1String(s.name));
        bool Layer::*const field = s.field;
        QString const text = tr(s.text);
        connect(*s.box, &QCheckBox::toggled, this, [this, field, text](bool on) {
            if (!m_refreshing)
                m_document->editLayers({m_layer}, text, [field, on](Layer& l) { l.*field = on; });
        });
        flips->addWidget(*s.box);
    }
    m_turnBefore->setToolTip(tr("What the layer shows is turned a quarter about the middle of the frame."));
    drawing->addRow(flips);
    QHBoxLayout* const clip = new QHBoxLayout;
    m_clipping = new QCheckBox(tr("Clip"), m_drawingBox);
    m_clipping->setObjectName(QStringLiteral("LayerClipping"));
    m_clipping->setToolTip(tr("Only what lies in the frame shows (made larger or smaller by the factors), in the layer's own space."));
    m_clipX = number(0.01, 100.0, 0.1, 2);
    m_clipX->setObjectName(QStringLiteral("LayerClipX"));
    m_clipY = number(0.01, 100.0, 0.1, 2);
    m_clipY->setObjectName(QStringLiteral("LayerClipY"));
    clip->addWidget(m_clipping);
    clip->addWidget(new QLabel(tr("X"), m_drawingBox));
    clip->addWidget(m_clipX, 1);
    clip->addWidget(new QLabel(tr("Y"), m_drawingBox));
    clip->addWidget(m_clipY, 1);
    auto clipped = [this] {
        if (m_refreshing)
            return;
        bool const on = m_clipping->isChecked();
        double const x = m_clipX->value(), y = m_clipY->value();
        m_document->editLayers({m_layer}, tr("Clipping"), [on, x, y](Layer& l) {
            l.clipping = on;
            l.clipX = x;
            l.clipY = y;
        });
    };
    connect(m_clipping, &QCheckBox::toggled, this, clipped);
    connect(m_clipX, &QDoubleSpinBox::valueChanged, this, clipped);
    connect(m_clipY, &QDoubleSpinBox::valueChanged, this, clipped);
    drawing->addRow(tr("Clipping"), clip);
    // Vector render options: how smooth its edges are, how hard their soft edge is, how its pictures are scaled.
    QHBoxLayout* const smooth = new QHBoxLayout;
    m_antialiasing = new QComboBox(m_drawingBox);
    m_antialiasing->setObjectName(QStringLiteral("LayerAntialiasing"));
    m_antialiasing->addItems({tr("Low"), tr("Medium"), tr("High"), tr("Very High")});
    m_antialiasing->setToolTip(tr("Antialiasing Quality: Low draws hard edges; Very High works the edges out at twice the size (slower)."));
    m_antialiasExponent = number(0.1, 10.0, 0.1, 2);
    m_antialiasExponent->setObjectName(QStringLiteral("LayerAntialiasExponent"));
    m_antialiasExponent->setToolTip(tr("Antialiasing Exponent: above 1 the soft edge of lines and paint is made harder, below 1 softer."));
    m_textureFilter = new QComboBox(m_drawingBox);
    m_textureFilter->setObjectName(QStringLiteral("LayerTextureFilter"));
    m_textureFilter->addItems({tr("Smooth"), tr("Nearest")});
    m_textureFilter->setToolTip(tr("Texture Filter: how its pictures are scaled - smoothly, or each pixel kept square (Nearest)."));
    smooth->addWidget(m_antialiasing, 1);
    smooth->addWidget(new QLabel(tr("Exponent"), m_drawingBox));
    smooth->addWidget(m_antialiasExponent, 1);
    smooth->addWidget(new QLabel(tr("Pictures"), m_drawingBox));
    smooth->addWidget(m_textureFilter, 1);
    auto smoothed = [this] {
        if (m_refreshing)
            return;
        int const quality = m_antialiasing->currentIndex(), filter = m_textureFilter->currentIndex();
        double const exponent = m_antialiasExponent->value();
        m_document->editLayers({m_layer}, tr("Antialiasing"), [quality, exponent, filter](Layer& l) {
            l.antialiasing = quality;
            l.antialiasExponent = exponent;
            l.textureFilter = filter;
        });
    };
    connect(m_antialiasing, &QComboBox::currentIndexChanged, this, smoothed);
    connect(m_antialiasExponent, &QDoubleSpinBox::valueChanged, this, smoothed);
    connect(m_textureFilter, &QComboBox::currentIndexChanged, this, smoothed);
    drawing->addRow(tr("Antialiasing"), smooth);
    // The colour space its pictures are read in, put in the scene's working space (Scene Settings: Colour).
    m_colourSpace = ColourSpaceBox::make(m_drawingBox, QStringLiteral("LayerColourSpace"), {}, tr("Scene Working Colour Space"));
    m_colourSpace->setToolTip(tr("The colour space the pictures of this layer are read in, before they are put in the working colour space."));
    connect(m_colourSpace, &QComboBox::activated, this, [this] {
        if (m_refreshing)
            return;
        QString const space = ColourSpaceBox::chosen(m_colourSpace);
        m_document->editLayers({m_layer}, tr("Colour Space"), [space](Layer& l) { l.colourSpace = space; });
    });
    drawing->addRow(tr("Colour Space"), m_colourSpace);
    // Line Thickness: its vector lines' thickness multiplied, added to, and kept between a least and a most (0: no limit).
    QHBoxLayout* const lines = new QHBoxLayout;
    m_lineMultiply = number(0.0, 100.0, 0.1, 2);
    m_lineMultiply->setObjectName(QStringLiteral("LayerLineMultiply"));
    m_lineAdd = number(-1000.0, 1000.0, 0.5, 2);
    m_lineAdd->setObjectName(QStringLiteral("LayerLineAdd"));
    m_lineMin = number(0.0, 1000.0, 0.5, 2);
    m_lineMin->setObjectName(QStringLiteral("LayerLineMin"));
    m_lineMax = number(0.0, 1000.0, 0.5, 2);
    m_lineMax->setObjectName(QStringLiteral("LayerLineMax"));
    m_lineZoom = new QCheckBox(tr("Zoom Independent"), m_drawingBox);
    m_lineZoom->setObjectName(QStringLiteral("LayerLineZoomIndependent"));
    m_lineZoom->setToolTip(tr("Its lines keep their thickness however much the layer is scaled."));
    lines->addWidget(new QLabel(tr("Multiply"), m_drawingBox));
    lines->addWidget(m_lineMultiply, 1);
    lines->addWidget(new QLabel(tr("Add"), m_drawingBox));
    lines->addWidget(m_lineAdd, 1);
    QHBoxLayout* const limits = new QHBoxLayout;
    limits->addWidget(new QLabel(tr("Min"), m_drawingBox));
    limits->addWidget(m_lineMin, 1);
    limits->addWidget(new QLabel(tr("Max"), m_drawingBox));
    limits->addWidget(m_lineMax, 1);
    limits->addWidget(m_lineZoom);
    auto thickness = [this] {
        if (m_refreshing)
            return;
        double const mult = m_lineMultiply->value(), add = m_lineAdd->value(), least = m_lineMin->value(), most = m_lineMax->value();
        bool const zoom = m_lineZoom->isChecked();
        m_document->editLayers({m_layer}, tr("Line Thickness"), [mult, add, least, most, zoom](Layer& l) {
            l.lineMultiply = mult;
            l.lineAdd = add;
            l.lineMin = least;
            l.lineMax = most;
            l.lineZoomIndependent = zoom;
        });
    };
    for (QDoubleSpinBox* box : {m_lineMultiply, m_lineAdd, m_lineMin, m_lineMax})
        connect(box, &QDoubleSpinBox::valueChanged, this, thickness);
    connect(m_lineZoom, &QCheckBox::toggled, this, thickness);
    drawing->addRow(tr("Line Thickness"), lines);
    drawing->addRow(QString(), limits);
    // Bitmap file options: its pictures' colours read or only their shape, and the pictures repeated across or down.
    QHBoxLayout* const pictureRow = new QHBoxLayout;
    Switch const pictureSwitches[] = {{&m_readColour, QT_TR_NOOP("Read Colour"), "LayerReadColour", &Layer::readColour},
                                      {&m_tileH, QT_TR_NOOP("Tile Horizontally"), "LayerTileHorizontal", &Layer::tileHorizontal},
                                      {&m_tileV, QT_TR_NOOP("Tile Vertically"), "LayerTileVertical", &Layer::tileVertical}};
    for (Switch const& s : pictureSwitches) {
        *s.box = new QCheckBox(tr(s.text), m_drawingBox);
        (*s.box)->setObjectName(QLatin1String(s.name));
        bool Layer::*const field = s.field;
        QString const text = tr(s.text);
        connect(*s.box, &QCheckBox::toggled, this, [this, field, text](bool on) {
            if (!m_refreshing)
                m_document->editLayers({m_layer}, text, [field, on](Layer& l) { l.*field = on; });
        });
        pictureRow->addWidget(*s.box);
    }
    m_readColour->setToolTip(tr("Off: its pictures show only their shape, in black."));
    m_bitmapMultiplier = number(0.0, 1.0, 0.05, 2);
    m_bitmapMultiplier->setObjectName(QStringLiteral("LayerBitmapMultiplier"));
    m_bitmapMultiplier->setToolTip(tr("Bitmap Options: Multiplier - how solid its pictures are, 1 as they are."));
    connect(m_bitmapMultiplier, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (!m_refreshing)
            m_document->editLayers({m_layer}, tr("Bitmap Multiplier"), [v](Layer& l) { l.bitmapMultiplier = v; });
    });
    drawing->addRow(tr("Pictures"), pictureRow);
    drawing->addRow(tr("Multiplier"), m_bitmapMultiplier);
    m_form->addRow(tr("Drawing"), m_drawingBox);

    // ---- a group: the values published on it ----
    m_publishedBox = new QWidget(this);
    m_publishedBox->setObjectName(QStringLiteral("LayerPublished"));
    new QFormLayout(m_publishedBox);
    m_form->addRow(tr("Published"), m_publishedBox);

    // ---- attributes of the user's own ----
    m_customBox = new QWidget(this);
    m_customBox->setObjectName(QStringLiteral("LayerCustomAttributes"));
    QVBoxLayout* const custom = new QVBoxLayout(m_customBox);
    custom->setContentsMargins(0, 0, 0, 0);
    QWidget* const rows = new QWidget(m_customBox);
    rows->setObjectName(QStringLiteral("LayerCustomRows"));
    new QFormLayout(rows);
    custom->addWidget(rows);
    QPushButton* const create = new QPushButton(tr("Create Attribute..."), m_customBox);
    create->setObjectName(QStringLiteral("LayerCreateAttribute"));
    create->setToolTip(tr("A number of your own on the layer, which can follow a function column; scripts read it by its name."));
    connect(create, &QPushButton::clicked, this, [this] {
        bool okay = false;
        QString const name = QInputDialog::getText(this, tr("Create Dynamic Attribute"), tr("Name:"), QLineEdit::Normal, QString(), &okay).trimmed();
        if (!okay)
            return;
        if (!addCustomAttribute(name))
            QMessageBox::information(this, tr("Create Dynamic Attribute"), name.isEmpty() ? tr("Attribute must have a non-empty name.")
                                                                                         : tr("The layer has an attribute of that name already."));
    });
    custom->addWidget(create, 0, Qt::AlignLeft);
    m_form->addRow(tr("Custom"), m_customBox);
}

bool LayerPanel::addCustomAttribute(QString const& name)
{
    Scene const& scene = m_document->scene();
    QString const wanted = name.trimmed();
    if (wanted.isEmpty() || m_layer < 0 || m_layer >= scene.layers.size() || wanted.contains(QLatin1Char('/')) || wanted.contains(QLatin1Char(':')))
        return false;
    for (CustomAttribute const& a : scene.layers[m_layer].customAttributes)
        if (a.name.compare(wanted, Qt::CaseInsensitive) == 0)
            return false;
    m_document->editLayers({m_layer}, tr("Create Dynamic Attribute"), [wanted](Layer& l) { l.customAttributes.push_back({wanted, 0.0, 0}); });
    return true;
}

void LayerPanel::refreshAttributeSections(Layer const& layer)
{
    Scene const& scene = m_document->scene();
    // Keyframe colours: a motion keyframe on this frame, or a stop-motion one.
    int const key = layer.keyIndex(m_frame);
    QColor const keyColour = key < 0 ? palette().color(QPalette::WindowText)
                             : InterfaceColours::get(layer.keys[key].tween ? "layerPropertiesMotionKey" : "layerPropertiesStopKey");
    m_key->setStyleSheet(key < 0 ? QString() : QStringLiteral("color: %1;").arg(keyColour.name()));

    bool const drawing = layer.holdsDrawings();
    m_drawingBox->setVisible(drawing || layer.kind == Layer::Peg);
    m_enable3d->setChecked(layer.enable3d);
    for (QWidget* w : std::initializer_list<QWidget*>{m_alignment, m_turnBefore, m_flipH, m_flipV, m_clipping, m_clipX, m_clipY, m_antialiasing,
                                                      m_antialiasExponent, m_textureFilter, m_lineMultiply, m_lineAdd, m_lineMin, m_lineMax, m_lineZoom,
                                                      m_readColour, m_tileH, m_tileV, m_bitmapMultiplier})
        w->setEnabled(drawing);
    m_alignment->setCurrentIndex(std::clamp(layer.alignment, 0, 3));
    m_turnBefore->setChecked(layer.turnBeforeAlignment);
    m_flipH->setChecked(layer.flipHorizontal);
    m_flipV->setChecked(layer.flipVertical);
    m_clipping->setChecked(layer.clipping);
    m_clipX->setValue(layer.clipX);
    m_clipY->setValue(layer.clipY);
    m_antialiasing->setCurrentIndex(std::clamp(layer.antialiasing, 0, 3));
    m_antialiasExponent->setValue(layer.antialiasExponent);
    m_textureFilter->setCurrentIndex(std::clamp(layer.textureFilter, 0, 1));
    ColourSpaceBox::fill(m_colourSpace, layer.colourSpace, tr("Scene Working Colour Space"));
    m_colourSpace->setEnabled(drawing);
    m_lineMultiply->setValue(layer.lineMultiply);
    m_lineAdd->setValue(layer.lineAdd);
    m_lineMin->setValue(layer.lineMin);
    m_lineMax->setValue(layer.lineMax);
    m_lineZoom->setChecked(layer.lineZoomIndependent);
    m_readColour->setChecked(layer.readColour);
    m_tileH->setChecked(layer.tileHorizontal);
    m_tileV->setChecked(layer.tileVertical);
    m_bitmapMultiplier->setValue(layer.bitmapMultiplier);
    if (QLayout* const turn = m_turnRow)
        for (int i = 0; i < turn->count(); ++i)
            if (QWidget* const w = turn->itemAt(i)->widget())
                w->setEnabled(layer.enable3d);

    // Published values: a row for each, set on the layer in the group on this frame.
    QFormLayout* const published = static_cast<QFormLayout*>(m_publishedBox->layout());
    while (published->rowCount() > 0)
        published->removeRow(0);
    m_form->setRowVisible(m_publishedBox, layer.kind == Layer::Group && !layer.published.isEmpty());
    for (PublishedAttribute const& p : layer.published) {
        int const inner = scene.layerIndex(p.layer);
        if (inner < 0)
            continue;
        QDoubleSpinBox* const box = number(-100000.0, 100000.0, p.value == Driver::Opacity ? 5.0 : 1.0, 2);
        box->setObjectName(QStringLiteral("Published_%1_%2").arg(p.layer).arg(p.value));
        double const shown = Driver::read(scene.poseOf(inner, m_frame), p.value);
        box->setValue(p.value == Driver::Opacity ? shown * 100.0 : shown);
        bool const linked = scene.layers[inner].linkOf(p.value) != 0;
        box->setEnabled(!linked);
        if (linked)
            box->setToolTip(tr("This value follows a function column: set it there."));
        int const layerId = p.layer, value = p.value;
        connect(box, &QDoubleSpinBox::valueChanged, this, [this, layerId, value](double v) {
            if (m_refreshing)
                return;
            int const at = m_document->scene().layerIndex(layerId);
            if (at < 0)
                return;
            Pose pose = m_document->scene().layers[at].poseAt(m_frame);
            Driver::write(pose, value, value == Driver::Opacity ? std::clamp(v / 100.0, 0.0, 1.0) : v);
            m_document->setKeyframe(at, m_frame, pose, tr("Set Published Attribute"));
        });
        published->addRow(scene.layers[inner].name + QStringLiteral(": ") + valueName(p.value), box);
    }

    // Attributes of the user's own.
    QWidget* const rows = m_customBox->findChild<QWidget*>(QStringLiteral("LayerCustomRows"));
    QFormLayout* const custom = static_cast<QFormLayout*>(rows->layout());
    while (custom->rowCount() > 0)
        custom->removeRow(0);
    for (int i = 0; i < layer.customAttributes.size(); ++i) {
        CustomAttribute const& a = layer.customAttributes[i];
        QHBoxLayout* const row = new QHBoxLayout;
        QDoubleSpinBox* const box = number(-1e9, 1e9, 1.0, 3);
        box->setObjectName(QStringLiteral("Custom_") + a.name);
        FunctionColumn const* const column = scene.function(a.function);
        box->setValue(column ? scene.customValue(m_layer, a.name, m_frame) : a.value);
        box->setEnabled(!column);
        int const index = i;
        connect(box, &QDoubleSpinBox::valueChanged, this, [this, index](double v) {
            if (!m_refreshing)
                m_document->editLayers({m_layer}, tr("Set Attribute Value"), [index, v](Layer& l) {
                    if (index < l.customAttributes.size())
                        l.customAttributes[index].value = v;
                });
        });
        QToolButton* const more = new QToolButton(rows);
        more->setObjectName(QStringLiteral("CustomMenu_") + a.name);
        more->setText(column ? column->name : QStringLiteral("..."));
        more->setPopupMode(QToolButton::InstantPopup);
        QMenu* const menu = new QMenu(more);
        connect(menu, &QMenu::aboutToShow, this, [this, menu, index] {
            menu->clear();
            Scene const& now = m_document->scene();
            if (m_layer < 0 || m_layer >= now.layers.size() || index >= now.layers[m_layer].customAttributes.size())
                return;
            int const linked = now.layers[m_layer].customAttributes[index].function;
            QMenu* const link = menu->addMenu(tr("Link Attribute"));
            for (FunctionColumn const& c : now.functions) {
                if (c.kind == FunctionColumn::Path3D)
                    continue;
                QAction* const to = link->addAction(c.name);
                to->setCheckable(true);
                to->setChecked(c.id == linked);
                int const id = c.id;
                connect(to, &QAction::triggered, this, [this, index, id] {
                    m_document->editLayers({m_layer}, tr("Link Attribute"), [index, id](Layer& l) { l.customAttributes[index].function = id; });
                });
            }
            link->setEnabled(!link->isEmpty());
            QAction* const unlink = menu->addAction(tr("Unlink Attribute"));
            unlink->setEnabled(linked != 0);
            connect(unlink, &QAction::triggered, this, [this, index] {
                m_document->editLayers({m_layer}, tr("Unlink Attribute"), [index](Layer& l) { l.customAttributes[index].function = 0; });
            });
            menu->addSeparator();
            connect(menu->addAction(tr("Remove Dynamic Attribute")), &QAction::triggered, this, [this, index] {
                m_document->editLayers({m_layer}, tr("Remove Dynamic Attribute"), [index](Layer& l) {
                    if (index < l.customAttributes.size())
                        l.customAttributes.removeAt(index);
                });
            });
        });
        more->setMenu(menu);
        row->addWidget(box, 1);
        row->addWidget(more);
        custom->addRow(a.name, row);
    }
}
