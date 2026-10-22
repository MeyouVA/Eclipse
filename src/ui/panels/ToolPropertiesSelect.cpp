// Eclipse - Tool Properties: rigging rows, select rows, auto flatten, line ends and joints, thickness and line options.
// options, the Rigging tool's mode, the ends and corners of lines, the thickness of a point.
#include "ui/panels/ToolProperties.h"

#include <algorithm>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDialog>
#include <QBuffer>
#include <QFileInfo>
#include <QFileDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QInputDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QToolButton>
#include <QPainter>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>

#include "actions/manager/ActionManager.h"
#include "canvas/Canvas.h"
#include "canvas/PencilTexture.h"
#include "ui/window/MainWindowPrivate.h"
#include <QFormLayout>

// What the Rigging tool lays down.
void ToolProperties::addRiggingRows()
{
    m_rigMode = new QComboBox(this);
    m_rigMode->setObjectName(QStringLiteral("RigMode"));
    m_rigMode->addItems({tr("Bone"), tr("Curve"), tr("Envelope"), tr("Weights")});
    m_rigMode->setToolTip(tr("Bone: a chain of bones the drawing bends at.\n"
                             "Curve: a curve the drawing bends along.\n"
                             "Envelope: an outline round the drawing that pulls it about.\n"
                             "Weights: paint where a bone of the chain pulls on the drawing more (Alt: less).\n"
                             "Click to lay down points; pose them with the Transform tool."));
    m_form->addRow(tr("Mode"), m_rigMode);
    m_weightBone = new QSpinBox(this);
    m_weightBone->setObjectName(QStringLiteral("WeightBone"));
    m_weightBone->setRange(1, 64);
    m_weightBone->setToolTip(tr("The bone whose pull is painted: 1 is the first of the chain."));
    connect(m_weightBone, &QSpinBox::valueChanged, this, [this](int bone) { m_canvas->setWeightBone(bone - 1); });
    m_form->addRow(tr("Bone"), m_weightBone);
    m_weightSize = new QDoubleSpinBox(this);
    m_weightSize->setObjectName(QStringLiteral("WeightSize"));
    m_weightSize->setRange(1.0, 1000.0);
    m_weightSize->setDecimals(0);
    m_weightSize->setValue(m_canvas->weightRadius());
    m_weightSize->setToolTip(tr("How far round the pointer each dab of weight reaches (scene units)."));
    connect(m_weightSize, &QDoubleSpinBox::valueChanged, m_canvas, &Canvas::setWeightRadius);
    m_form->addRow(tr("Brush size"), m_weightSize);
    m_weightClear = new QPushButton(tr("Clear Painted Weights"), this);
    m_weightClear->setObjectName(QStringLiteral("WeightClear"));
    m_weightClear->setToolTip(tr("Takes away all the weights painted on the current layer: nearness alone says again how the bones pull."));
    connect(m_weightClear, &QPushButton::clicked, this, [this] { emit clearWeightsRequested(); });
    m_form->addRow(m_weightClear);
    auto weightsShown = [this](int index) {
        bool const weights = index == Canvas::RigWeights && m_canvas->tool() == Canvas::Rigging;
        m_form->setRowVisible(m_weightBone, weights);
        m_form->setRowVisible(m_weightSize, weights);
        m_form->setRowVisible(m_weightClear, weights);
    };
    connect(m_rigMode, &QComboBox::currentIndexChanged, this, [this, weightsShown](int index) {
        m_canvas->setRigMode(Canvas::RigMode(index));
        weightsShown(index);
    });
    weightsShown(m_rigMode->currentIndex());
    m_envelopePoints = new QSpinBox(this);
    m_envelopePoints->setObjectName(QStringLiteral("EnvelopePoints"));
    m_envelopePoints->setRange(4, 32);
    m_envelopePoints->setValue(m_envelopeCount);
    m_envelopePoints->setToolTip(tr("How many points Animation > Deformation > Create Envelope puts round a drawing."));
    connect(m_envelopePoints, &QSpinBox::valueChanged, this, [this](int count) {
        m_envelopeCount = count;
        emit envelopePointsChanged(count);
    });
    m_form->addRow(tr("Envelope points"), m_envelopePoints);
    m_envelopeOutline = new QCheckBox(tr("Envelope follows the outline"), this);
    m_envelopeOutline->setObjectName(QStringLiteral("EnvelopeOutline"));
    m_envelopeOutline->setToolTip(tr("On: Create Envelope goes round the outline of the artwork itself, into its hollows.\nOff: round the hull about it."));
    m_envelopeOutline->setChecked(true);
    connect(m_envelopeOutline, &QCheckBox::toggled, this, &ToolProperties::envelopeOutlineChanged);
    m_form->addRow(m_envelopeOutline);
}

// The Select tool: every drawing of the layer at once.
void ToolProperties::addSelectRows()
{
    m_applyAll = new QCheckBox(tr("Apply to All Drawings"), this);
    m_applyAll->setObjectName(QStringLiteral("SelectAllDrawings"));
    m_applyAll->setToolTip(tr("What you move, scale, turn or delete is moved, scaled, turned or deleted in every drawing of the layer:\n"
                              "in the other drawings, whatever lies within the place the selection had."));
    connect(m_applyAll, &QCheckBox::toggled, m_canvas, &Canvas::setApplyToAllDrawings);
    m_form->addRow(m_applyAll);
    // The Stamp tool: how big the stamp comes out.
    m_canvas->loadStamp();
    m_stampScale = new QSpinBox(this);
    m_stampScale->setObjectName(QStringLiteral("StampScale"));
    m_stampScale->setRange(10, 1000);
    m_stampScale->setSingleStep(10);
    m_stampScale->setSuffix(QStringLiteral(" %"));
    m_stampScale->setValue(m_canvas->stampScale());
    m_stampScale->setToolTip(tr("How big the stamp comes out. To stamp artwork of your own: select it, then Drawing > Create Stamp from Selection."));
    connect(m_stampScale, &QSpinBox::valueChanged, m_canvas, &Canvas::setStampScale);
    m_form->addRow(tr("Scale"), m_stampScale);
    m_stampRandomSize = new QSpinBox(this);
    m_stampRandomSize->setObjectName(QStringLiteral("StampRandomSize"));
    m_stampRandomSize->setRange(0, 90);
    m_stampRandomSize->setSuffix(QStringLiteral(" %"));
    m_stampRandomSize->setToolTip(tr("Each stamp is made smaller by up to this much, at random: 0, all of them as big as the scale says."));
    m_stampRandomSize->setValue(std::clamp(settings().value(QStringLiteral("tools/stampRandomSize"), 0).toInt(), 0, 90));
    m_canvas->setStampRandomSize(m_stampRandomSize->value());
    connect(m_stampRandomSize, &QSpinBox::valueChanged, this, [this](int v) {
        m_canvas->setStampRandomSize(v);
        settings().setValue(QStringLiteral("tools/stampRandomSize"), v);
    });
    m_form->addRow(tr("Random Size"), m_stampRandomSize);
    {
        // Which stamp: the star, or one of those made with Create Stamp from Selection.
        m_stampRow = new QWidget(this);
        auto* const row = new QHBoxLayout(m_stampRow);
        row->setContentsMargins(0, 0, 0, 0);
        m_stampList = new QComboBox(m_stampRow);
        m_stampList->setObjectName(QStringLiteral("StampList"));
        m_stampList->setToolTip(tr("The stamp to use: the star, or artwork of your own (select it, then Drawing > Create Stamp from Selection)."));
        auto* const remove = new QPushButton(tr("Remove"), m_stampRow);
        remove->setObjectName(QStringLiteral("StampRemove"));
        remove->setToolTip(tr("Takes the chosen stamp off the list."));
        row->addWidget(m_stampList, 1);
        row->addWidget(remove);
        auto fill = [this, remove] {
            QSignalBlocker const quiet(m_stampList);
            m_stampList->clear();
            m_stampList->addItem(tr("Star"));
            m_stampList->addItems(m_canvas->stampNames());
            m_stampList->setCurrentIndex(m_canvas->currentStamp() + 1);
            remove->setEnabled(m_canvas->currentStamp() >= 0);
        };
        fill();
        connect(m_canvas, &Canvas::stampsChanged, this, fill);
        connect(m_stampList, &QComboBox::activated, this, [this](int index) { m_canvas->chooseStamp(index - 1); });
        connect(remove, &QPushButton::clicked, this, [this] { m_canvas->removeStamp(m_stampList->currentIndex() - 1); });
        m_form->addRow(tr("Stamp"), m_stampRow);
        m_stampRandom = new QCheckBox(tr("Random drawing"), this);
        m_stampRandom->setObjectName(QStringLiteral("StampRandom"));
        m_stampRandom->setToolTip(tr("A stamp made of several drawings (Drawing > Create Stamp from Selected Drawings) puts them down one after another;\n"
                                     "ticked, each click puts down any one of them."));
        connect(m_stampRandom, &QCheckBox::toggled, m_canvas, &Canvas::setStampRandom);
        m_form->addRow(m_stampRandom);
    }
    m_byColour = new QCheckBox(tr("Select by Colour"), this);
    m_byColour->setObjectName(QStringLiteral("SelectByColour"));
    m_byColour->setToolTip(tr("A click on a line or on paint selects everything of that colour in the drawing."));
    connect(m_byColour, &QCheckBox::toggled, m_canvas, &Canvas::setSelectByColour);
    m_form->addRow(m_byColour);
    m_bothArts = new QCheckBox(tr("Apply to Line and Colour Art"), this);
    m_bothArts->setObjectName(QStringLiteral("EraserBothArts"));
    m_bothArts->setToolTip(tr("The eraser rubs out of the line art and the colour art at once, whichever of the two is being worked on."));
    connect(m_bothArts, &QCheckBox::toggled, m_canvas, &Canvas::setEraserBothArts);
    m_form->addRow(m_bothArts);
    m_selectBothArts = new QCheckBox(tr("Apply to Line and Colour Art"), this);
    m_selectBothArts->setObjectName(QStringLiteral("SelectBothArts"));
    m_selectBothArts->setToolTip(tr("The tool takes the lines and the paint under them together: what is in the line art and what is in the colour art, as if they were one."));
    connect(m_selectBothArts, &QCheckBox::toggled, m_canvas, &Canvas::setSelectBothArts);
    connect(m_canvas, &Canvas::selectBothArtsChanged, m_selectBothArts, &QCheckBox::setChecked);
    m_form->addRow(m_selectBothArts);
    m_paintAll = new QCheckBox(tr("Apply to All Drawings"), this);
    m_paintAll->setObjectName(QStringLiteral("PaintAllDrawings"));
    m_paintAll->setToolTip(tr("What you paint, repaint, unpaint or erase is painted, repainted, unpainted or erased in the same place in every drawing of the layer."));
    connect(m_paintAll, &QCheckBox::toggled, m_canvas, &Canvas::setPaintAllDrawings);
    m_form->addRow(m_paintAll);
    m_paintLayers = new QCheckBox(tr("Apply to All Layers"), this);
    m_paintLayers->setObjectName(QStringLiteral("PaintAllLayers"));
    m_paintLayers->setToolTip(tr("What you paint, repaint or unpaint is painted, repainted or unpainted in the same place of the picture on every drawing layer\n"
                                 "that is shown and not locked, as one step."));
    connect(m_paintLayers, &QCheckBox::toggled, m_canvas, &Canvas::setPaintAllLayers);
    m_form->addRow(m_paintLayers);
    m_permanent = new QCheckBox(tr("Permanent Selection"), this);
    m_permanent->setObjectName(QStringLiteral("SelectPermanent"));
    m_permanent->setToolTip(tr("What is selected stays selected when you go to another frame: whatever lies in the same place in the drawing there."));
    connect(m_permanent, &QCheckBox::toggled, m_canvas, &Canvas::setPermanentSelection);
    m_form->addRow(m_permanent);
    m_snapAlign = new QCheckBox(tr("Snap and Align"), this);
    m_snapAlign->setObjectName(QStringLiteral("SelectSnapAlign"));
    m_snapAlign->setToolTip(tr("What you drag goes edge to edge, or middle to middle, with the other artwork of the drawing when it comes near."));
    connect(m_snapAlign, &QCheckBox::toggled, m_canvas, &Canvas::setSnapAlign);
    connect(m_snapAlign, &QCheckBox::toggled, this, [this](bool on) {
        if (QAction* const action = m_actions->action(QStringLiteral("SnapAndAlign")))
            action->setChecked(on);
    });
    // A command that changes a setting shown here (Snap and Align, Auto Fill): shown again.
    connect(m_canvas, &Canvas::toolSettingsChanged, this, [this] {
        QSignalBlocker const quiet1(m_snapAlign), quiet2(m_lineFill);
        m_snapAlign->setChecked(m_canvas->snapsAndAligns());
        m_scaleThickness->setChecked(m_canvas->scaleThickness());
        if (QAction* const action = m_actions->action(QStringLiteral("ScalePencilThickness")))
            action->setChecked(m_canvas->scaleThickness());
        if (m_ceControls)
            m_ceControls->setChecked(m_canvas->contourControls());
        if (m_lineFill)
            m_lineFill->setChecked(m_canvas->lineAutoFill());
        settings().setValue(QStringLiteral("tools/lineAutoFill"), m_canvas->lineAutoFill());
    });
    m_form->addRow(m_snapAlign);
    addSnapRows();
    addModeRows();
    addStylusRows();
    // The Dropper: the colour of whatever is seen there, or only of what is on the layer worked on.
    m_dropperAll = new QCheckBox(tr("Sample All Layers"), this);
    m_dropperAll->setObjectName(QStringLiteral("DropperAllLayers"));
    m_dropperAll->setToolTip(tr("On: the dropper takes the colour of whatever is in front where you click, on any layer.\n"
                               "Off: it looks only at the layer you are working on."));
    m_canvas->setDropperAllLayers(settings().value(QStringLiteral("tools/dropperAllLayers"), true).toBool());
    m_dropperAll->setChecked(m_canvas->dropperAllLayers());
    connect(m_dropperAll, &QCheckBox::toggled, m_canvas, &Canvas::setDropperAllLayers);
    connect(m_dropperAll, &QCheckBox::clicked, this, [](bool on) { settings().setValue(QStringLiteral("tools/dropperAllLayers"), on); });
    m_form->addRow(m_dropperAll);
}

void ToolProperties::addAutoFlattenRow()
{
    m_autoFlatten = new QCheckBox(tr("Auto Flatten"), this);
    m_autoFlatten->setObjectName(QStringLiteral("BrushAutoFlatten"));
    m_autoFlatten->setToolTip(tr("A stroke joins the shapes of its own colour that it overlaps, and is cut out of the shapes of other colours under it."));
    m_autoFlatten->setChecked(m_canvas->autoFlatten());
    connect(m_autoFlatten, &QCheckBox::toggled, m_canvas, &Canvas::setAutoFlatten);
    connect(m_autoFlatten, &QCheckBox::clicked, this, [](bool on) { settings().setValue(QStringLiteral("tools/brushAutoFlatten"), on); });
    m_autoFlatten->setEnabled(m_canvas->brushShapes());
    connect(m_brushShape, &QCheckBox::toggled, m_autoFlatten, &QCheckBox::setEnabled);
    m_form->addRow(m_autoFlatten);
}

// kind 0: the start of a line, 1: its end, 2: a corner.  A thick white line on dark.
static QIcon endPicture(int kind, int which)
{
    QPixmap pixmap(26, 20);
    pixmap.fill(QColor(34, 35, 38));
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(QColor(225, 227, 230), 9.0);
    if (kind == 2) {
        pen.setCapStyle(Qt::FlatCap);
        pen.setJoinStyle(which == 1 ? Qt::MiterJoin : which == 2 ? Qt::BevelJoin : Qt::RoundJoin);
        p.setPen(pen);
        p.drawPolyline(QPolygonF({QPointF(26, 13.5), QPointF(9.5, 13.5), QPointF(9.5, 0)}));
    } else {
        pen.setCapStyle(which == 1 ? Qt::FlatCap : Qt::RoundCap);
        p.setPen(pen);
        // The line runs off the far side; its tip is the near one.
        if (kind == 0)
            p.drawLine(QPointF(8, 10), QPointF(40, 10));
        else
            p.drawLine(QPointF(-14, 10), QPointF(18, 10));
    }
    // Where the line itself runs, thin, as the centre of it.
    p.setPen(QPen(QColor(232, 92, 60), 1.0));
    if (kind == 2)
        p.drawPolyline(QPolygonF({QPointF(26, 13.5), QPointF(9.5, 13.5), QPointF(9.5, 0)}));
    else if (kind == 0)
        p.drawLine(QPointF(8, 10), QPointF(26, 10));
    else
        p.drawLine(QPointF(0, 10), QPointF(18, 10));
    return QIcon(pixmap);
}

void ToolProperties::addShapeBox(QString const& label, QString const& tip, int kind, QStringList const& names, std::function<void(int)> apply)
{
    QComboBox* const box = new QComboBox(this);
    box->setObjectName(label);
    box->setIconSize(QSize(26, 20));
    for (int i = 0; i < names.size(); ++i) {
        box->addItem(endPicture(kind, i), QString());
        box->setItemData(i, names[i], Qt::ToolTipRole);
        box->setItemData(i, names[i], Qt::AccessibleTextRole);
    }
    box->setToolTip(tip);
    box->setFixedWidth(58);
    connect(box, &QComboBox::currentIndexChanged, this, apply);
    m_shapeRow->addWidget(new QLabel(label, this));
    m_shapeRow->addWidget(box);
}

void ToolProperties::addLineShapeRows()
{
    m_shapeRow = new QHBoxLayout;
    QString const applies = tr(" Applies to new lines and to selected ones; shows on lines of one thickness.");
    addShapeBox(tr("Start"), tr("The start of a line: round, or cut off square.") + applies, 0, {tr("Round"), tr("Flat")},
                [this](int index) { m_canvas->setStartCap(index); });
    addShapeBox(tr("End"), tr("The end of a line: round, or cut off square.") + applies, 1, {tr("Round"), tr("Flat")},
                [this](int index) { m_canvas->setEndCap(index); });
    addShapeBox(tr("Join"), tr("Corners of a line: rounded, carried on to a point (mitre), or cut flat (bevel).") + applies, 2,
                {tr("Round"), tr("Mitre"), tr("Bevel")}, [this](int index) { m_canvas->setJoinStyle(index); });
    m_shapeRow->addStretch();
    m_form->addRow(m_shapeRow);

    // The pencil texture: the grain a line is drawn with, and how much of it shows.
    m_textureRow = new QHBoxLayout;
    QComboBox* const texture = new QComboBox(this);
    texture->setObjectName(QStringLiteral("LineTexture"));
    texture->setIconSize(QSize(72, 20));
    texture->setToolTip(tr("The pencil texture: the grain the line is drawn with in place of solid ink.") + applies);
    QSpinBox* const amount = new QSpinBox(this);
    amount->setObjectName(QStringLiteral("LineTextureAmount"));
    amount->setRange(0, 100);
    amount->setSingleStep(5);
    amount->setSuffix(QStringLiteral(" %"));
    amount->setToolTip(tr("How much of the texture shows: 100 the grain as it is, 0 a solid line after all.") + applies);
    {
        int const kept = settings().value(QStringLiteral("tools/lineTexture"), 0).toInt();
        int const keptAmount = std::clamp(settings().value(QStringLiteral("tools/lineTextureAmount"), 100).toInt(), 0, 100);
        amount->setValue(keptAmount);
        m_canvas->setLineTextureAmount(keptAmount);
        m_canvas->setLineTexture(kept);
    }
    auto list = [this, texture, amount] {
        QSignalBlocker const block(texture);
        fillTextureBox(texture, m_canvas->lineTexture());
        amount->setEnabled(m_canvas->lineTexture() != 0);
    };
    list();
    connect(m_canvas, &Canvas::texturesChanged, this, list);
    connect(m_canvas, &Canvas::lineTextureTaken, this, [this, list, amount] {
        list();
        QSignalBlocker const block(amount);
        amount->setValue(m_canvas->lineTextureAmount());
        settings().setValue(QStringLiteral("tools/lineTexture"), m_canvas->lineTexture());
    });
    connect(texture, &QComboBox::currentIndexChanged, this, [this, texture, amount](int) {
        int const kind = texture->currentData().toInt();
        m_canvas->setLineTexture(kind);
        amount->setEnabled(kind != 0);
        settings().setValue(QStringLiteral("tools/lineTexture"), kind);
    });
    connect(amount, &QSpinBox::valueChanged, this, [this](int v) {
        m_canvas->setLineTextureAmount(v);
        settings().setValue(QStringLiteral("tools/lineTextureAmount"), v);
    });
    m_textureRow->addWidget(new QLabel(tr("Texture"), this));
    m_textureRow->addWidget(texture, 1);
    m_textureRow->addWidget(amount);
    m_textureRow->addWidget(textureButton(QStringLiteral("LineTextureMenu")));
    m_form->addRow(m_textureRow);
    m_lineCrisp = new QCheckBox(tr("Crisp texture edge"), this);
    m_lineCrisp->setObjectName(QStringLiteral("LineTextureCrisp"));
    m_lineCrisp->setToolTip(tr("A crisp edge: the grain frays at the edge but does not fade there, and is not blurred - every bit of it is ink or nothing.") + applies);
    m_lineCrisp->setChecked(settings().value(QStringLiteral("tools/lineTextureCrisp"), false).toBool());
    m_canvas->setLineTextureCrisp(m_lineCrisp->isChecked());
    connect(m_lineCrisp, &QCheckBox::toggled, this, [this](bool on) {
        m_canvas->setLineTextureCrisp(on);
        settings().setValue(QStringLiteral("tools/lineTextureCrisp"), on);
    });
    m_form->addRow(m_lineCrisp);
    m_lineRamp = rampRow(QStringLiteral("LineTextureRamp"), false);
    m_form->addRow(m_lineRamp);
}

void ToolProperties::addThicknessRow()
{
    QSlider* const slider = new QSlider(Qt::Horizontal, this);
    slider->setRange(0, 200);
    QSpinBox* const box = new QSpinBox(this);
    box->setObjectName(QStringLiteral("PointThickness"));
    box->setRange(0, 400);
    box->setSuffix(QStringLiteral(" %"));
    QString const tip = tr("The thickness of the line at the point picked with the Contour Editor, as a percentage of the line's width. 0 brings the line to a point there.");
    slider->setToolTip(tip);
    box->setToolTip(tip);
    slider->setEnabled(false);
    box->setEnabled(false);
    connect(slider, &QSlider::valueChanged, box, &QSpinBox::setValue);
    connect(box, &QSpinBox::valueChanged, this, [this, slider](int v) {
        QSignalBlocker const block(slider);
        slider->setValue(std::min(v, 200));
        m_canvas->setPointThickness(v);
    });
    connect(m_canvas, &Canvas::pointPicked, this, [slider, box](int percent) {
        QSignalBlocker const a(slider), b(box);
        slider->setEnabled(percent >= 0);
        box->setEnabled(percent >= 0);
        box->setValue(std::max(percent, 0));
        slider->setValue(std::clamp(percent, 0, 200));
    });
    QHBoxLayout* const row = new QHBoxLayout;
    row->addWidget(slider, 1);
    row->addWidget(box);
    m_form->addRow(tr("Thickness"), row);
    m_thicknessRow = row;
}

void ToolProperties::addLineOptionRows()
{
    // Each kept under tools/line/<key>; the canvas is told the whole set each time one changes.
    auto send = [this] {
        QSettings const s = settings();
        Canvas::LineOptions o;
        o.startTaper = s.value(QStringLiteral("tools/line/startTaper"), 0).toInt();
        o.endTaper = s.value(QStringLiteral("tools/line/endTaper"), 0).toInt();
        o.speed = s.value(QStringLiteral("tools/line/speed"), 0).toInt();
        o.speedInvert = s.value(QStringLiteral("tools/line/speedInvert"), false).toBool();
        o.trim = s.value(QStringLiteral("tools/line/trim"), false).toBool();
        o.connect = s.value(QStringLiteral("tools/line/connect"), false).toBool();
        o.closeGap = s.value(QStringLiteral("tools/line/closeGap"), false).toBool();
        o.building = s.value(QStringLiteral("tools/line/building"), false).toBool();
        o.straight = s.value(QStringLiteral("tools/line/straight"), false).toBool();
        o.fade = std::clamp(s.value(QStringLiteral("tools/line/fade"), 0).toInt(), 0, 5000);
        m_canvas->setLineOptions(o);
    };
    send();
    auto number = [&](QString const& label, QString const& tip, char const* key, int most) {
        int const kept = std::clamp(settings().value(QStringLiteral("tools/line/") + QLatin1String(key), 0).toInt(), 0, most);
        QString const name = QLatin1String(key);
        m_brushLineRows.push_back(addSetting(label, tip, 0, most, kept, QStringLiteral(" %"), [this, name, send](int v) {
            settings().setValue(QStringLiteral("tools/line/") + name, v);
            send();
        }));
    };
    number(tr("Start Taper"), tr("The Brush's line thins to a point at its start, over this share of its length."), "startTaper", 50);
    number(tr("End Taper"), tr("The Brush's line thins to a point at its end, over this share of its length."), "endTaper", 50);
    {
        int const kept = std::clamp(settings().value(QStringLiteral("tools/tiltSensitivity"), 0).toInt(), 0, 100);
        m_canvas->setTiltSensitivity(kept);
        m_brushLineRows.push_back(addSetting(tr("Tilt Sensitivity"), tr("With a pen that tells its tilt: the more the pen leans, the thicker the Brush's line."), 0, 100, kept,
                                             QStringLiteral(" %"), [this](int v) {
                                                 m_canvas->setTiltSensitivity(v);
                                                 settings().setValue(QStringLiteral("tools/tiltSensitivity"), v);
                                             }));
    }
    number(tr("Speed Sensitivity"), tr("The faster the pen moves, the thinner the Brush's line (with Invert: the thicker)."), "speed", 100);
    {
        // Fade Distance: the line thins away to nothing so far from where it began.
        int const kept = std::clamp(settings().value(QStringLiteral("tools/line/fade"), 0).toInt(), 0, 2000);
        m_brushLineRows.push_back(addSetting(tr("Fade Distance"), tr("The Brush's line thins away to nothing over this distance from where it began (0: it does not fade)."),
                                             0, 2000, kept, QString(), [this, send](int v) {
                                                 settings().setValue(QStringLiteral("tools/line/fade"), v);
                                                 send();
                                             }));
    }
    auto box = [&](QString const& label, QString const& tip, char const* key) {
        QCheckBox* const check = new QCheckBox(label, this);
        check->setObjectName(QStringLiteral("Line") + QLatin1String(key));
        check->setToolTip(tip);
        check->setChecked(settings().value(QStringLiteral("tools/line/") + QLatin1String(key), false).toBool());
        QString const name = QLatin1String(key);
        connect(check, &QCheckBox::toggled, this, [this, name, send](bool on) {
            settings().setValue(QStringLiteral("tools/line/") + name, on);
            send();
        });
        m_form->addRow(check);
        m_lineOptionBoxes.push_back(check);
    };
    box(tr("Invert Speed"), tr("Speed Sensitivity the other way: the faster the pen, the thicker the line."), "speedInvert");
    box(tr("Trim Extra Lines"), tr("An end that runs a little past a line it crosses is cut off where it crosses."), "trim");
    box(tr("Connect Line Ends"), tr("An end that stops a little short of a line is carried on to touch it."), "connect");
    box(tr("Auto Close Gap"), tr("Automatically close strokes with an invisible stroke: ends that nearly meet are joined, so paint stays in."), "closeGap");
    box(tr("Line Building Mode"), tr("In this mode, strokes drawn in small increments will be merged into a single line:\n"
                                     "a line begun or ended at the end of a line of its colour becomes one line with it."), "building");
    box(tr("Draw Stroke as Straight Lines"), tr("The Stroke tool draws straight pieces from point to point instead of curves."), "straight");
}
