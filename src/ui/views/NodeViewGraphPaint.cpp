// Eclipse - the Node View's graph: a node's settings edited, and the graph painted.
#include "ui/views/NodeView.h"
#include "actions/theme/InterfaceColours.h"

#include <QCheckBox>
#include "ui/panels/ColourEditor.h"
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSet>
#include <QSpinBox>

#include <algorithm>
#include <cmath>
#include <functional>

#include "model/document/Commands.h"
#include "render/effects/Effects.h"
#include "ui/panels/EffectsPanel.h"
#include "ui/panels/EffectParamsEditor.h"
#include "colour/luts/Luts.h"
#include "ui/panels/ColourSpaceBox.h"

#include <QCoreApplication>
#include <QHeaderView>
#include <QTableWidget>

namespace {

// Which effects take a colour, and which can be turned inside out.
bool takesColour(Effect::Type t)
{
    return t == Effect::Tint || t == Effect::Glow || t == Effect::Shadow || t == Effect::ColourScale || t == Effect::Tone || t == Effect::Highlight
           || t == Effect::FieldChart
           || t == Effect::MatteGenerator || t == Effect::Gradient || t == Effect::ChromaKey;
}

bool turnsInsideOut(Effect::Type t)
{
    return GraphNode::takesMatte(t) || t == Effect::Gradient || t == Effect::MatteResize || t == Effect::ChromaKey || t == Effect::DifferenceKeyer
           || t == Effect::LuminanceThreshold;
}

}  // namespace

void NodeView::editGraphNode(int id)
{
    NodeGraph g = m_document->scene().graph;
    int const i = g.nodeIndex(id);
    if (i >= 0 && g.nodes[i].kind == GraphNode::Composite) {
        editComposite(id);
        return;
    }
    if (i < 0 || g.nodes[i].kind != GraphNode::EffectNode)
        return;
    Effect e = g.nodes[i].effect;
    QDialog dialog(this);
    dialog.setWindowTitle(graphNodeName(g.nodes[i]));
    QFormLayout* const form = new QFormLayout(&dialog);
    QCheckBox* const on = new QCheckBox(&dialog);
    on->setChecked(e.enabled);
    form->addRow(tr("On"), on);
    QDoubleSpinBox* const amount = new QDoubleSpinBox(&dialog);
    amount->setObjectName(QStringLiteral("GraphEffectAmount"));
    amount->setRange(0.0, 1000.0);
    amount->setDecimals(2);
    amount->setValue(g.nodes[i].amountAt(m_frame));
    Effect::Type const type = e.type;
    bool const keyer = type == Effect::ChromaKey || type == Effect::DifferenceKeyer || type == Effect::LuminanceThreshold;
    if (type == Effect::ImageSwitch)
        amount->setDecimals(0);
    Effect::Amount const newer = Effect::amountOf(type);
    if (newer.shown >= 0)
        amount->setRange(0.0, newer.most);
    form->addRow(newer.shown >= 0 && newer.label ? QCoreApplication::translate("Effect", newer.label)
                 : type == Effect::GenericComposite ? tr("Opacity") : keyer ? tr("Threshold") : type == Effect::ImageSwitch ? tr("Port Index") : tr("Amount"),
                 amount);
    // Animated: the amount a keyframe of this frame (the frames between go straight from one keyframe to the next).
    QCheckBox* const keyed = new QCheckBox(tr("Keyframe on frame %1").arg(m_frame + 1), &dialog);
    keyed->setObjectName(QStringLiteral("GraphEffectKeyed"));
    keyed->setChecked(!g.nodes[i].amountKeys.isEmpty());
    form->addRow(QString(), keyed);
    form->setRowVisible(amount, newer.shown != 0);
    form->setRowVisible(keyed, newer.shown != 0);
    QPushButton* colour = nullptr;
    if (takesColour(e.type)) {
        colour = new QPushButton(e.color.name(), &dialog);
        connect(colour, &QPushButton::clicked, &dialog, [&] {
            QColor const picked = ColourEditor::getColour(e.color, &dialog, tr("Colour"));
            if (picked.isValid()) {
                e.color = picked;
                colour->setText(picked.name());
            }
        });
        form->addRow(tr("Colour"), colour);
    }
    QCheckBox* invert = nullptr;
    if (turnsInsideOut(e.type)) {
        invert = new QCheckBox(&dialog);
        invert->setChecked(e.invert);
        form->addRow(GraphNode::takesMatte(e.type) ? tr("Outside the matte") : tr("Inverted"), invert);
    }
    QComboBox* channel = nullptr;
    if (e.type == Effect::ChannelSelector) {
        channel = new QComboBox(&dialog);
        channel->addItems({tr("Red"), tr("Green"), tr("Blue"), tr("Opacity")});
        channel->setCurrentIndex(std::clamp(e.channel, 0, 3));
        form->addRow(tr("Channel"), channel);
    }
    QPlainTextEdit* text = nullptr;
    if (e.type == Effect::Script || e.type == Effect::External || e.type == Effect::BurnIn) {
        text = new QPlainTextEdit(&dialog);
        text->setObjectName(QStringLiteral("GraphEffectText"));
        text->setLineWrapMode(QPlainTextEdit::NoWrap);
        text->setPlainText(e.text);
        text->setMinimumSize(360, 120);
        if (e.type == Effect::BurnIn) {
            text->setMinimumSize(300, 50);
            text->setToolTip(tr("%f the frame, %s the scene's name, %t the time code, %% a percent sign."));
        }
        form->addRow(e.type == Effect::Script ? tr("Script") : e.type == Effect::BurnIn ? tr("Text") : tr("Command Line"), text);
    }
    QComboBox* peg = nullptr;
    if (e.type == Effect::ApplyPeg) {
        // The layer whose motion moves the picture.
        Scene const& scene = m_document->scene();
        peg = new QComboBox(&dialog);
        peg->setObjectName(QStringLiteral("GraphEffectPeg"));
        peg->addItem(tr("(none)"), 0);
        for (int l = int(scene.layers.size()) - 1; l >= 0; --l)
            peg->addItem(scene.layers[l].name, scene.layers[l].id);
        peg->setCurrentIndex(std::max(0, peg->findData(e.matte)));
        form->addRow(tr("Peg"), peg);
    }
    QComboBox* camera = nullptr;
    QCheckBox* viewpoint = nullptr;
    QCheckBox* flatten = nullptr;
    if (e.type == Effect::ApplyCamera) {
        Scene const& scene = m_document->scene();
        camera = new QComboBox(&dialog);
        camera->setObjectName(QStringLiteral("GraphEffectCamera"));
        camera->addItem(tr("(none)"), 0);
        for (int l : scene.cameras())
            camera->addItem(scene.layers[l].name, scene.layers[l].id);
        camera->setCurrentIndex(std::max(0, camera->findData(e.matte)));
        form->addRow(tr("Camera"), camera);
        viewpoint = new QCheckBox(tr("Apply Camera Viewpoint"), &dialog);
        viewpoint->setObjectName(QStringLiteral("GraphEffectViewpoint"));
        viewpoint->setToolTip(tr("Seen through the camera. Off, the picture moves with the camera instead, as with a peg."));
        viewpoint->setChecked(e.cameraViewpoint);
        form->addRow(viewpoint);
        flatten = new QCheckBox(tr("Flatten Depth Buffer"), &dialog);
        flatten->setObjectName(QStringLiteral("GraphEffectFlatten"));
        flatten->setToolTip(tr("What comes in is seen as one flat picture. Off, each of its layers is seen from the camera in its own depth."));
        flatten->setChecked(e.flattenDepth);
        form->addRow(flatten);
    }
    QCheckBox* truck = nullptr;
    if (Effect::trucks(e.type)) {
        truck = new QCheckBox(tr("Truck Factor"), &dialog);
        truck->setObjectName(QStringLiteral("GraphEffectTruckFactor"));
        truck->setToolTip(tr("The blur grows as the layer that comes in nears the camera and shrinks as it goes further away."));
        truck->setChecked(e.truckFactor);
        form->addRow(truck);
    }
    QComboBox* mode = nullptr;
    if (e.type == Effect::Blending) {
        mode = new QComboBox(&dialog);
        mode->setObjectName(QStringLiteral("GraphEffectBlend"));
        mode->addItems({tr("Normal"), tr("Multiply"), tr("Screen"), tr("Overlay"), tr("Darken"), tr("Lighten"), tr("Colour Dodge"), tr("Colour Burn"),
                        tr("Hard Light"), tr("Soft Light"), tr("Difference"), tr("Exclusion"), tr("Add")});
        mode->setCurrentIndex(std::clamp(e.blendMode, 0, int(Layer::BlendCount) - 1));
        form->addRow(tr("Over the second input"), mode);
    }
    // Colour Curves: the four curves, each to drag.
    QVector<QPointF> curves[4];
    for (int c = 0; c < 4; ++c)
        curves[c] = e.curves[c];
    if (e.type == Effect::ColourCurves) {
        QComboBox* const which = new QComboBox(&dialog);
        which->addItems({tr("RGB"), tr("Red"), tr("Green"), tr("Blue")});
        form->addRow(tr("Curve"), which);
        ColourCurveEdit* const curve = new ColourCurveEdit(&dialog);
        curve->setPoints(curves[0]);
        form->addRow(curve);
        connect(which, &QComboBox::currentIndexChanged, curve, [curve, &curves](int c) { curve->setPoints(curves[std::clamp(c, 0, 3)]); });
        connect(curve, &ColourCurveEdit::edited, which, [which, &curves](QVector<QPointF> const& points) {
            bool const straight = points.size() == 2 && points[0] == QPointF(0, 0) && points[1] == QPointF(1, 1);
            curves[std::clamp(which->currentIndex(), 0, 3)] = straight ? QVector<QPointF>() : points;
        });
    }
    QDoubleSpinBox* area[4] = {};
    if (e.type == Effect::Crop) {
        double const now[4] = {e.area.x(), e.area.y(), e.area.width(), e.area.height()};
        QString const names[4] = {tr("Left"), tr("Top"), tr("Width"), tr("Height")};
        for (int k = 0; k < 4; ++k) {
            area[k] = new QDoubleSpinBox(&dialog);
            area[k]->setRange(k < 2 ? -10000.0 : 0.0, 10000.0);
            area[k]->setValue(now[k]);
            form->addRow(names[k], area[k]);
        }
    }
    auto fraction = [&](char const* name, double value, double most = 1.0) {
        QDoubleSpinBox* const box = new QDoubleSpinBox(&dialog);
        box->setObjectName(QLatin1String(name));
        box->setRange(0.0, most);
        box->setSingleStep(0.05);
        box->setDecimals(2);
        box->setValue(value);
        return box;
    };
    QComboBox* colourOperation = nullptr;
    QComboBox* alphaOperation = nullptr;
    QDoubleSpinBox* intensity[3] = {};
    if (type == Effect::GenericComposite) {
        colourOperation = new QComboBox(&dialog);
        colourOperation->setObjectName(QStringLiteral("GraphEffectColourOperation"));
        colourOperation->addItems(Effects::colourOperations());
        colourOperation->setCurrentIndex(std::clamp(e.colourOperation, 0, int(colourOperation->count()) - 1));
        form->addRow(tr("Colour Operation"), colourOperation);
        alphaOperation = new QComboBox(&dialog);
        alphaOperation->setObjectName(QStringLiteral("GraphEffectAlphaOperation"));
        alphaOperation->addItems(Effects::alphaOperations());
        alphaOperation->setCurrentIndex(std::clamp(e.alphaOperation, 0, int(alphaOperation->count()) - 1));
        form->addRow(tr("Alpha Operation"), alphaOperation);
        QString const names[3] = {tr("Intensity Red"), tr("Intensity Green"), tr("Intensity Blue")};
        for (int c = 0; c < 3; ++c) {
            intensity[c] = fraction("GraphEffectIntensity", e.intensity[c], 10.0);
            form->addRow(names[c], intensity[c]);
        }
    }
    QCheckBox* arts[Drawing::SheetCount] = {};
    if (type == Effect::LayerSelector) {
        QString const names[Drawing::SheetCount] = {tr("Underlay"), tr("Colour Art"), tr("Line Art"), tr("Overlay")};
        for (int sheet = Drawing::SheetCount - 1; sheet >= 0; --sheet) {
            arts[sheet] = new QCheckBox(names[sheet], &dialog);
            arts[sheet]->setObjectName(QStringLiteral("GraphEffectArt%1").arg(sheet));
            arts[sheet]->setChecked(e.sheets & (1 << sheet));
            form->addRow(arts[sheet]);
        }
    }
    QDoubleSpinBox* softness = nullptr;
    QDoubleSpinBox* clipBlack = nullptr;
    QDoubleSpinBox* clipWhite = nullptr;
    QDoubleSpinBox* spill = nullptr;
    if (keyer) {
        softness = fraction("GraphEffectSoftness", e.softness);
        form->addRow(tr("Softness"), softness);
        clipBlack = fraction("GraphEffectClipBlack", e.clipBlack);
        form->addRow(tr("Clip Black"), clipBlack);
        clipWhite = fraction("GraphEffectClipWhite", e.clipWhite);
        form->addRow(tr("Clip White"), clipWhite);
        if (type == Effect::ChromaKey) {
            spill = fraction("GraphEffectSpill", e.spill);
            form->addRow(tr("Spill Reduction"), spill);
        }
    }
    QSpinBox* switchInputs = nullptr;
    if (type == Effect::ImageSwitch) {
        switchInputs = new QSpinBox(&dialog);
        switchInputs->setObjectName(QStringLiteral("GraphEffectInputs"));
        switchInputs->setRange(1, 64);
        switchInputs->setValue(g.nodes[i].inputs());
        form->addRow(tr("Inputs"), switchInputs);
    }
    QCheckBox* inViews = nullptr;
    QCheckBox* inRender = nullptr;
    if (type == Effect::Visibility) {
        inViews = new QCheckBox(tr("Display in Camera View"), &dialog);
        inViews->setObjectName(QStringLiteral("GraphEffectInViews"));
        inViews->setChecked(e.showInViews);
        form->addRow(inViews);
        inRender = new QCheckBox(tr("Soft Render"), &dialog);
        inRender->setObjectName(QStringLiteral("GraphEffectInRender"));
        inRender->setToolTip(tr("Shown when the scene is rendered (exported, previewed or printed)."));
        inRender->setChecked(e.showInRender);
        form->addRow(inRender);
    }
    // Colour-Override: each colour of the palettes, ticked to be shown in another.
    QTableWidget* overrides = nullptr;
    if (type == Effect::ColourOverride) {
        Scene const& scene = m_document->scene();
        overrides = new QTableWidget(int(scene.palette.size()), 3, &dialog);
        overrides->setObjectName(QStringLiteral("GraphEffectOverrides"));
        overrides->setHorizontalHeaderLabels({tr("Colour"), tr("Shown in"), tr("Picture Sequence")});
        overrides->verticalHeader()->hide();
        overrides->horizontalHeader()->setStretchLastSection(true);
        overrides->setEditTriggers(QAbstractItemView::NoEditTriggers);
        overrides->setMinimumSize(320, 160);
        for (int row = 0; row < scene.palette.size(); ++row) {
            Swatch const& swatch = scene.palette[row];
            QColor instead = swatch.color;
            bool listed = false;
            for (QPair<int, QColor> const& o : std::as_const(e.overrides))
                if (o.first == swatch.id) {
                    instead = o.second;
                    listed = true;
                }
            QPixmap chip(14, 14);
            chip.fill(swatch.color);
            QTableWidgetItem* const name = new QTableWidgetItem(QIcon(chip), swatch.name);
            name->setData(Qt::UserRole, swatch.id);
            name->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
            name->setCheckState(listed ? Qt::Checked : Qt::Unchecked);
            overrides->setItem(row, 0, name);
            QTableWidgetItem* const shown = new QTableWidgetItem(instead.name(QColor::HexArgb));
            shown->setData(Qt::UserRole, instead);
            shown->setBackground(instead);
            shown->setForeground(instead.lightnessF() > 0.5 ? Qt::black : Qt::white);
            overrides->setItem(row, 1, shown);
            QString const sequence = e.overrideImages.value(swatch.id);
            QTableWidgetItem* const images = new QTableWidgetItem(sequence.isEmpty() ? tr("(none)") : QFileInfo(sequence).fileName());
            images->setData(Qt::UserRole, sequence);
            images->setToolTip(sequence.isEmpty() ? tr("Click to show this colour as a picture sequence: its first picture, the numbered ones after it the frames after.")
                                                  : sequence);
            overrides->setItem(row, 2, images);
        }
        // The third column: a picture sequence the colour is shown as (chosen again with nothing chosen: none).
        connect(overrides, &QTableWidget::cellClicked, &dialog, [overrides, &dialog](int row, int column) {
            if (column != 2)
                return;
            QTableWidgetItem* const images = overrides->item(row, 2);
            QString const was = images->data(Qt::UserRole).toString();
            QString const chosen = QFileDialog::getOpenFileName(&dialog, tr("Picture Sequence (its first picture)"), was,
                                                                tr("Pictures (*.png *.jpg *.jpeg *.tga *.tif *.tiff *.bmp *.psd *.exr);;All Files (*)"));
            if (chosen.isEmpty() && !was.isEmpty()
                && QMessageBox::question(&dialog, tr("Picture Sequence"), tr("Show this colour without its picture sequence?")) != QMessageBox::Yes)
                return;
            images->setData(Qt::UserRole, chosen);
            images->setText(chosen.isEmpty() ? tr("(none)") : QFileInfo(chosen).fileName());
            images->setToolTip(chosen);
            if (!chosen.isEmpty())
                overrides->item(row, 0)->setCheckState(Qt::Checked);
        });
        // A click on the second column picks the colour it is shown in instead (and ticks it).
        connect(overrides, &QTableWidget::cellClicked, &dialog, [overrides, &dialog](int row, int column) {
            if (column != 1)
                return;
            QTableWidgetItem* const shown = overrides->item(row, 1);
            QColor const picked = ColourEditor::getColour(shown->data(Qt::UserRole).value<QColor>(), &dialog, tr("Shown in"));
            if (!picked.isValid())
                return;
            shown->setData(Qt::UserRole, picked);
            shown->setText(picked.name(QColor::HexArgb));
            shown->setBackground(picked);
            shown->setForeground(picked.lightnessF() > 0.5 ? Qt::black : Qt::white);
            overrides->item(row, 0)->setCheckState(Qt::Checked);
        });
        form->addRow(overrides);
    }
    // Colour Space: from and to; Look-Up Table: its file.
    QComboBox* fromSpace = nullptr;
    QComboBox* toSpace = nullptr;
    if (type == Effect::ColourSpace) {
        fromSpace = ColourSpaceBox::make(&dialog, QStringLiteral("GraphEffectFromSpace"), e.fromSpace, tr("Scene Working Colour Space"));
        form->addRow(tr("From"), fromSpace);
        toSpace = ColourSpaceBox::make(&dialog, QStringLiteral("GraphEffectToSpace"), e.toSpace, tr("Scene Working Colour Space"));
        form->addRow(tr("To"), toSpace);
    }
    QLineEdit* lutFile = nullptr;
    if (type == Effect::LookUpTable) {
        QWidget* const row = new QWidget(&dialog);
        QHBoxLayout* const line = new QHBoxLayout(row);
        line->setContentsMargins(0, 0, 0, 0);
        lutFile = new QLineEdit(QDir::toNativeSeparators(e.file), row);
        lutFile->setObjectName(QStringLiteral("GraphEffectLutFile"));
        QPushButton* const browse = new QPushButton(tr("Browse..."), row);
        line->addWidget(lutFile, 1);
        line->addWidget(browse);
        connect(browse, &QPushButton::clicked, &dialog, [lutFile, &dialog] {
            QString const file = QFileDialog::getOpenFileName(&dialog, tr("Look-Up Table"), lutFile->text(), Cms::Luts::filter());
            if (!file.isEmpty())
                lutFile->setText(QDir::toNativeSeparators(file));
        });
        form->addRow(tr("LUT File"), row);
    }
    // The named settings of the kinds that have them.
    EffectParamsEditor* params = nullptr;
    if (!Effect::params(type).isEmpty()) {
        params = new EffectParamsEditor(&dialog);
        params->setEffect(e);
        form->addRow(params);
    }
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    if (params)
        params->takeInto(e);
    if (overrides) {
        e.overrides.clear();
        for (int row = 0; row < overrides->rowCount(); ++row)
            if (overrides->item(row, 0)->checkState() == Qt::Checked)
                e.overrides.push_back(qMakePair(overrides->item(row, 0)->data(Qt::UserRole).toInt(), overrides->item(row, 1)->data(Qt::UserRole).value<QColor>()));
        e.overrideImages.clear();
        for (int row = 0; row < overrides->rowCount(); ++row)
            if (QString const sequence = overrides->item(row, 2)->data(Qt::UserRole).toString();
                overrides->item(row, 0)->checkState() == Qt::Checked && !sequence.isEmpty())
                e.overrideImages.insert(overrides->item(row, 0)->data(Qt::UserRole).toInt(), sequence);
    }
    if (colourOperation) {
        e.colourOperation = colourOperation->currentIndex();
        e.alphaOperation = alphaOperation->currentIndex();
        for (int c = 0; c < 3; ++c)
            e.intensity[c] = intensity[c]->value();
    }
    if (arts[0]) {
        e.sheets = 0;
        for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet)
            if (arts[sheet]->isChecked())
                e.sheets |= 1 << sheet;
    }
    if (softness) {
        e.softness = softness->value();
        e.clipBlack = clipBlack->value();
        e.clipWhite = clipWhite->value();
    }
    if (spill)
        e.spill = spill->value();
    if (switchInputs)
        g.nodes[i].ports = switchInputs->value();
    if (inViews) {
        e.showInViews = inViews->isChecked();
        e.showInRender = inRender->isChecked();
    }
    if (channel)
        e.channel = channel->currentIndex();
    if (mode)
        e.blendMode = mode->currentIndex();
    if (peg)
        e.matte = peg->currentData().toInt();
    if (camera)
        e.matte = camera->currentData().toInt();
    if (viewpoint)
        e.cameraViewpoint = viewpoint->isChecked();
    if (flatten)
        e.flattenDepth = flatten->isChecked();
    if (truck)
        e.truckFactor = truck->isChecked();
    if (text)
        e.text = text->toPlainText();
    if (fromSpace) {
        e.fromSpace = ColourSpaceBox::chosen(fromSpace);
        e.toSpace = ColourSpaceBox::chosen(toSpace);
    }
    if (lutFile)
        e.file = QDir::fromNativeSeparators(lutFile->text().trimmed());
    if (area[0])
        e.area = QRectF(area[0]->value(), area[1]->value(), area[2]->value(), area[3]->value());
    for (int c = 0; c < 4; ++c)
        e.curves[c] = curves[c];
    e.enabled = on->isChecked();
    if (keyed->isChecked()) {
        g.nodes[i].amountKeys.insert(m_frame, amount->value());
    } else {
        g.nodes[i].amountKeys.clear();
        e.amount = amount->value();
    }
    if (invert)
        e.invert = invert->isChecked();
    g.nodes[i].effect = e;
    m_document->setGraph(g, tr("Edit Effect Node"));
}

QString NodeView::graphPortName(GraphNode const& n, int port, bool input) const
{
    if (!input)
        return n.kind == GraphNode::GroupIn ? tr("Into the group %1").arg(port + 1) : n.outputs() > 1 ? tr("Output %1").arg(port + 1) : tr("Output image");
    if (n.kind == GraphNode::EffectNode) {
        if (port == 0)
            return tr("Input image");
        Effect::Type const t = n.effect.type;
        if (t == Effect::ImageSwitch)
            return tr("Input %1").arg(port + 1);
        if (t == Effect::DifferenceKeyer)
            return tr("Clean background");
        if (t == Effect::Refract)
            return tr("Height");
        return t == Effect::Blending || t == Effect::Glue || t == Effect::GenericComposite ? tr("Under") : tr("Matte");
    }
    if (n.kind == GraphNode::Display)
        return tr("Image shown");
    return tr("Input %1").arg(port + 1);
}

void NodeView::paintGraphMode(QPainter& p)
{
    Scene const& scene = m_document->scene();
    NodeGraph const& g = scene.graph;
    QColor const imageCable = InterfaceColours::get("nodeImageCable"), picked(255, 220, 90);
    auto cable = [&](QPointF from, QPointF to, QColor const& color, double width) {
        QVector<QPointF> const pts = cablePoints(from, to);
        p.setPen(QPen(color, width));
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(pts.constData(), int(pts.size()));
    };
    // A node being dragged is drawn where the pointer has it, cables and all.
    QPointF const dragBy = m_drag == DragGraphNode && m_moved ? nodeDragDelta(m_dragNow) * m_zoom : QPointF();
    auto shift = [&](int id) { return id == m_graphNode || m_graphPicked.contains(id) ? dragBy : QPointF(); };
    // ---- cables ----
    for (int c = 0; c < g.cables.size(); ++c) {
        GraphCable const& k = g.cables[c];
        if (c == m_unplugged || graphNodeRect(k.from).isNull() || graphNodeRect(k.to).isNull())
            continue;
        QPointF const from = graphOutPort(k.from, k.fromPort) + shift(k.from), to = graphInPort(k.to, k.toPort) + shift(k.to);
        GraphNode const* const source = g.node(k.from);
        if (m_options.wideCables && source && source->kind == GraphNode::Composite && source->passThrough && c != m_graphCable) {
            // Out of a pass-through composite: several pictures in one cable, wide and in two colours.
            cable(from, to, imageCable, 5.0);
            cable(from, to, InterfaceColours::get("nodeWideCableInner"), 1.6);
            continue;
        }
        cable(from, to, c == m_graphCable ? picked : imageCable, c == m_graphCable ? 2.6 : 1.8);
    }
    if (m_drag == DragWire) {
        if (m_wireReverse)
            cable(m_dragNow, graphInPort(m_wireFrom, m_wireFromPort), picked, 1.8);
        else
            cable(graphOutPort(m_wireFrom, m_wireFromPort), m_dragNow, picked, 1.8);
    }
    // ---- the parenting of the layers: from a node's right end to the left end of each node that hangs from it ----
    {
        QPen pegPen(InterfaceColours::get("nodePegCable"), 1.6, Qt::DashLine);
        p.setPen(pegPen);
        p.setBrush(Qt::NoBrush);
        for (GraphNode const& n : g.nodes) {
            if (n.kind != GraphNode::LayerNode || n.group != m_graphGroup || n.id == m_pegChild)
                continue;
            int const l = scene.layerIndex(n.layer);
            int const parent = l >= 0 ? scene.parentIndex(l) : -1;
            int const from = parent >= 0 ? g.layerNode(scene.layers[parent].id) : 0;
            if (!from || graphNodeRect(from).isNull())
                continue;
            QPointF const a = graphTransformOut(from) + shift(from), b = graphTransformIn(n.id) + shift(n.id);
            QPainterPath path(a);
            double const bow = std::max(30.0 * m_zoom, std::abs(b.x() - a.x()) / 2.0);
            path.cubicTo(a + QPointF(bow, 0), b - QPointF(bow, 0), b);
            p.drawPath(path);
        }
        if (m_drag == DragPeg) {
            p.drawLine(graphTransformOut(m_pegFrom), m_dragNow);
        }
    }
    // ---- nodes ----
    QFont font = this->font();
    font.setPointSizeF(std::max(5.0, font.pointSizeF() * m_zoom));
    p.setFont(font);
    for (GraphNode const& n : g.nodes) {
        if (n.group != m_graphGroup)
            continue;
        QRectF const r = graphNodeRect(n.id).translated(shift(n.id));
        if (n.kind == GraphNode::Note) {
            // A note: what it says, on a pale card.
            QRectF const card(r.topLeft(), QSizeF(std::max(r.width(), 180.0 * m_zoom), std::max(r.height(), 60.0 * m_zoom)));
            p.setPen(QPen(n.id == m_graphNode || m_graphPicked.contains(n.id) ? QColor(255, 255, 255) : QColor(120, 110, 60), 1.2));
            p.setBrush(QColor(240, 225, 140));
            p.drawRect(card);
            p.setPen(QColor(40, 36, 20));
            p.drawText(card.adjusted(6 * m_zoom, 4 * m_zoom, -6 * m_zoom, -4 * m_zoom), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, graphNodeName(n));
            continue;
        }
        int const shownLayer = n.kind == GraphNode::LayerNode ? m_document->scene().layerIndex(n.layer) : -1;
        bool const chosen = n.id == m_graphNode || m_graphPicked.contains(n.id);
        if (n.kind == GraphNode::Waypoint) {
            // A waypoint: a dot that cables pass through, its name beside it.
            p.setPen(QPen(chosen ? InterfaceColours::get("nodeSelected") : InterfaceColours::get("nodeDark"), chosen ? 2.0 : 1.0));
            p.setBrush(n.enabled ? InterfaceColours::get("nodeLight") : InterfaceColours::get("nodeLight").darker(190));
            p.drawEllipse(r);
            if (!n.name.isEmpty()) {
                p.setPen(QColor(235, 235, 235));
                p.drawText(QRectF(r.right() + 4 * m_zoom, r.top() - 4 * m_zoom, 200 * m_zoom, r.height() + 8 * m_zoom), Qt::AlignLeft | Qt::AlignVCenter, n.name);
            }
            continue;
        }
        QColor const color = shownLayer >= 0 ? layerNodeColour(shownLayer)
                             : n.kind == GraphNode::Composite ? InterfaceColours::get(n.passThrough ? "nodeNoFlatten" : "nodeComposite") : n.kind == GraphNode::EffectNode ? InterfaceColours::get("nodeEffect")
                             : n.kind == GraphNode::Display ? InterfaceColours::get("nodeDisplay")
                             : n.kind == GraphNode::GroupIn || n.kind == GraphNode::GroupOut ? InterfaceColours::get("nodeProxyPort")
                             : n.kind == GraphNode::Group ? InterfaceColours::get(effectGroup(n.id) ? "nodeGroupEffect" : "nodeGroup")
                             : InterfaceColours::get("nodeLight");
        bool const off = !nodeEnabled(n.id);
        QColor const border = nodeBorder(&n, shownLayer);
        p.setPen(QPen(chosen ? InterfaceColours::get("nodeSelected") : border, chosen ? 2.0 : 1.0));
        p.setBrush(off ? color.darker(190) : color);
        {
            // Its shadow, a little below and to the right.
            QPen const pen = p.pen();
            QBrush const brush = p.brush();
            p.setPen(Qt::NoPen);
            p.setBrush(InterfaceColours::get("nodeShadow"));
            p.drawRoundedRect(r.translated(3 * m_zoom, 3 * m_zoom), 4 * m_zoom, 4 * m_zoom);
            p.setPen(pen);
            p.setBrush(brush);
        }
        p.drawRoundedRect(r, 4 * m_zoom, 4 * m_zoom);
        p.setPen(off ? QColor(150, 150, 150) : QColor(245, 245, 245));
        bool const editor = showsEditor(shownLayer, &n);
        QRectF const text = r.adjusted(8 * m_zoom, 0, -(n.kind == GraphNode::Group ? 18 : editor ? 20 : 6) * m_zoom, 0);
        p.drawText(text, Qt::AlignVCenter | Qt::AlignLeft, p.fontMetrics().elidedText(graphNodeName(n), Qt::ElideRight, int(text.width())));
        if (editor)
            paintEditorButton(p, r);
        if (n.kind == GraphNode::Group) {
            // A group can be gone into: a small arrow at its right end says so.
            QPointF const tip(r.right() - 10 * m_zoom, r.center().y() + 4 * m_zoom);
            p.setPen(QPen(QColor(235, 235, 235), 1.4));
            p.drawLine(tip + QPointF(0, -8 * m_zoom), tip);
            p.drawLine(tip + QPointF(-3.5 * m_zoom, -3.5 * m_zoom), tip);
            p.drawLine(tip + QPointF(3.5 * m_zoom, -3.5 * m_zoom), tip);
        }
        // Ports: in along the top (filled when something comes in), out along the bottom; picked ones ringed, named ones labelled.
        QFont small = p.font();
        small.setPointSizeF(std::max(4.5, small.pointSizeF() * 0.8));
        for (int i = 0; i < n.inputs(); ++i) {
            QPointF const at = graphInPort(n.id, i) + shift(n.id);
            p.setPen(QPen(InterfaceColours::get("nodePortImageDark"), 1));
            p.setBrush(g.into(n.id, i) ? InterfaceColours::get("nodePortImage") : InterfaceColours::get("nodePortImage").darker(240));
            p.drawEllipse(at, 3.5 * m_zoom, 3.5 * m_zoom);
            if (m_portsPicked.contains(PortRef{n.id, i, false})) {
                p.setPen(QPen(picked, 1.6));
                p.setBrush(Qt::NoBrush);
                p.drawEllipse(at, 6.5 * m_zoom, 6.5 * m_zoom);
            }
            if (m_showFilters && !n.portFilters.value(i).isEmpty()) {
                QFont const was = p.font();
                p.setFont(small);
                p.setPen(QColor(250, 210, 120));
                p.drawText(QRectF(at.x() - 60 * m_zoom, at.y() - 22 * m_zoom, 120 * m_zoom, 14 * m_zoom), Qt::AlignCenter, QStringLiteral("[") + n.portFilters.value(i) + QStringLiteral("]"));
                p.setFont(was);
            }
        }
        for (int o = 0; o < n.outputs() && !pictureless(n); ++o) {
            QPointF const at = graphOutPort(n.id, o) + shift(n.id);
            p.setPen(QPen(InterfaceColours::get("nodePortImageDark"), 1));
            p.setBrush(InterfaceColours::get("nodePortImage"));
            p.drawEllipse(at, 4.5 * m_zoom, 4.5 * m_zoom);
            if (m_portsPicked.contains(PortRef{n.id, o, true})) {
                p.setPen(QPen(picked, 1.6));
                p.setBrush(Qt::NoBrush);
                p.drawEllipse(at, 7.5 * m_zoom, 7.5 * m_zoom);
            }
            if (!n.portNames.value(o).isEmpty()) {
                QFont const was = p.font();
                p.setFont(small);
                p.setPen(QColor(170, 230, 250));
                p.drawText(QRectF(at.x() - 60 * m_zoom, at.y() + 6 * m_zoom, 120 * m_zoom, 14 * m_zoom), Qt::AlignCenter, n.portNames.value(o));
                p.setFont(was);
            }
        }
        if (n.kind == GraphNode::LayerNode) {
            // Its transformation ports: small squares at its two ends.
            double const s = 3.5 * m_zoom;
            p.setBrush(QColor(230, 160, 60));
            p.drawRect(QRectF(graphTransformIn(n.id) + shift(n.id) - QPointF(s, s), QSizeF(2 * s, 2 * s)));
            p.drawRect(QRectF(graphTransformOut(n.id) + shift(n.id) - QPointF(s, s), QSizeF(2 * s, 2 * s)));
        }
    }
    paintThumbnails(p);
    if (m_printing || m_inLens)
        return;
    // The map in the corner: the nodes of this level as small blocks, and what the view shows as a frame.
    if (m_navigator && width() > 320 && height() > 200) {
        QRectF const map = navigatorRect();
        p.setFont(this->font());
        p.setPen(QPen(QColor(24, 24, 24), 1));
        p.setBrush(QColor(44, 44, 46, 230));
        p.drawRect(map);
        p.setClipRect(map.adjusted(1, 1, -1, -1));
        QRectF const all = graphLevelBounds().adjusted(-20, -20, 20, 20), inner = map.adjusted(5, 5, -5, -5);
        double const k = std::min(inner.width() / std::max(all.width(), 1.0), inner.height() / std::max(all.height(), 1.0));
        auto onMap = [&](QPointF graph) { return inner.center() + (graph - all.center()) * k; };
        p.setPen(Qt::NoPen);
        for (GraphNode const& n : g.nodes) {
            if (n.group != m_graphGroup)
                continue;
            p.setBrush(n.id == m_graphNode ? QColor(72, 190, 200) : QColor(150, 152, 158));
            p.drawRect(QRectF(onMap(n.at), QSizeF(std::max(3.0, graphNodeWidth(n) * k), std::max(2.0, kNodeHeight * k))));
        }
        p.setPen(QPen(QColor(235, 235, 235), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(onMap(toGraph(QPointF(0, 0))), onMap(toGraph(QPointF(width(), height())))));
        p.setClipping(false);
    }
    paintBoxSelection(p);
    // Inside a group: the way here along the top; a click on it goes out one level.
    if (QRectF const path = graphPathRect(); !path.isNull()) {
        QStringList names;
        for (GraphNode const* at = g.node(m_graphGroup); at; at = g.node(at->group))
            names.prepend(graphNodeName(*at));
        p.setFont(this->font());
        p.setPen(QPen(QColor(24, 24, 24), 1));
        p.setBrush(QColor(40, 118, 122));
        p.drawRoundedRect(path, 4, 4);
        p.setPen(QColor(245, 245, 245));
        p.drawText(path.adjusted(8, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("\u2191  ") + tr("Top") + QStringLiteral(" / ") + names.join(QStringLiteral(" / ")));
    }
}
