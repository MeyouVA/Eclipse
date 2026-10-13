// Eclipse - Tool Properties: the settings of the tools that draw - size, smoothing, the brush's
// options, the Rigging tool's mode, the ends and corners of lines, the thickness of a point.
#include "ui/panels/ToolProperties.h"

#include <algorithm>

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>

#include "ui/canvas/Canvas.h"
#include "ui/window/MainWindowPrivate.h"
#include <QFormLayout>

void ToolProperties::addSizeRows()
{
    m_size = new QSlider(Qt::Horizontal, this);
    m_size->setRange(1, 200);
    m_size->setValue(6);
    QSpinBox* const sizeBox = new QSpinBox(this);
    sizeBox->setRange(1, 200);
    sizeBox->setValue(6);
    connect(m_size, &QSlider::valueChanged, sizeBox, &QSpinBox::setValue);
    connect(sizeBox, &QSpinBox::valueChanged, m_size, &QSlider::setValue);
    connect(m_size, &QSlider::valueChanged, this, [this](int v) { m_canvas->setBrushWidth(v); });
    sizeBox->setObjectName(QStringLiteral("ToolSize"));
    QHBoxLayout* const sizeRow = new QHBoxLayout;
    sizeRow->addWidget(m_size, 1);
    sizeRow->addWidget(sizeBox);
    m_form->addRow(tr("Size"), sizeRow);
    m_sizeRow = sizeRow;
    // The Brush with a pen: how thin its line gets where the pen barely touches.
    {
        int const kept = std::clamp(settings().value(QStringLiteral("tools/brushMinSize"), 0).toInt(), 0, 100);
        m_canvas->setMinimumBrushSize(kept);
        m_minSizeRow = addSetting(tr("Minimum Size"), tr("With a pen tablet: how thick the brush line is where the pen barely touches, as a percentage of its size.\n"
                                                       "100: the same thickness however hard you press."),
                0, 100, kept, QStringLiteral(" %"), [this](int v) {
                    m_canvas->setMinimumBrushSize(v);
                    settings().setValue(QStringLiteral("tools/brushMinSize"), v);
                });
    }
    // The paint tools: gaps in the lines round an area, up to this wide, are closed of themselves.
    {
        int const kept = std::clamp(settings().value(QStringLiteral("tools/closeGap"), 2).toInt(), 0, 40);
        m_canvas->setCloseGapSize(kept);
        m_gapRow = addSetting(tr("Close Gap"), tr("A gap this wide or less in the lines round an area does not let the paint out. 0: only areas that are closed are painted."),
                0, 40, kept, QStringLiteral(" px"), [this](int v) {
                    m_canvas->setCloseGapSize(v);
                    settings().setValue(QStringLiteral("tools/closeGap"), v);
                });
    }
    // The Envelope tool: how many points there are to pull.
    m_envToolRow = addSetting(tr("Points"), tr("How many points the outline round the selection has."), 4, 32, m_canvas->envelopeToolPoints(), QString(),
                              [this](int v) { m_canvas->setEnvelopeToolPoints(v); });
    // How drawn lines are tidied when the pen lifts.
    m_smoothingRow = addSetting(tr("Smoothing"), tr("How much a line drawn with the brush or pencil is smoothed when the pen lifts."), 0, 100, 25,
            QStringLiteral(" %"), [this](int v) { m_canvas->setSmoothing(v); });
    m_cornersRow = addSetting(tr("Corners"), tr("A turn sharper than this many degrees stays a sharp corner; gentler turns are rounded. 180 rounds everything."),
            0, 180, 50, QStringLiteral(" deg"), [this](int v) { m_canvas->setCornerAngle(v); });
}

void ToolProperties::addBrushRows()
{
    // What the Brush makes: a contour shape, or a centre line with a thickness.
    m_brushShape = new QCheckBox(tr("Draw as a contour shape"), this);
    m_brushShape->setObjectName(QStringLiteral("BrushShape"));
    m_brushShape->setToolTip(tr("On: a brush stroke is a filled shape with an outline of its own, which the Contour Editor can pull about.\n"
                                "Off: it is a centre line with a thickness, like a pencil line."));
    // The brush options are as they were left the last time the program ran.
    {
        QSettings const s = settings();
        m_canvas->setBrushShapes(s.value(QStringLiteral("tools/brushShapes"), true).toBool());
        m_canvas->setAutoFlatten(s.value(QStringLiteral("tools/brushAutoFlatten"), false).toBool());
        m_canvas->setDrawBehind(s.value(QStringLiteral("tools/brushDrawBehind"), false).toBool());
    }
    m_brushShape->setChecked(m_canvas->brushShapes());
    connect(m_brushShape, &QCheckBox::toggled, m_canvas, &Canvas::setBrushShapes);
    connect(m_brushShape, &QCheckBox::clicked, this, [](bool on) { settings().setValue(QStringLiteral("tools/brushShapes"), on); });
    m_form->addRow(m_brushShape);
    m_drawBehind = new QCheckBox(tr("Draw Behind"), this);
    m_drawBehind->setObjectName(QStringLiteral("BrushDrawBehind"));
    m_drawBehind->setToolTip(tr("What the brush draws goes under the artwork already in the drawing."));
    m_drawBehind->setChecked(m_canvas->drawBehind());
    connect(m_drawBehind, &QCheckBox::toggled, m_canvas, &Canvas::setDrawBehind);
    connect(m_drawBehind, &QCheckBox::clicked, this, [](bool on) { settings().setValue(QStringLiteral("tools/brushDrawBehind"), on); });
    m_form->addRow(m_drawBehind);
}

// What the Rigging tool lays down.
void ToolProperties::addRiggingRows()
{
    m_rigMode = new QComboBox(this);
    m_rigMode->setObjectName(QStringLiteral("RigMode"));
    m_rigMode->addItems({tr("Bone"), tr("Curve"), tr("Envelope")});
    m_rigMode->setToolTip(tr("Bone: a chain of bones the drawing bends at.\n"
                             "Curve: a curve the drawing bends along.\n"
                             "Envelope: an outline round the drawing that pulls it about.\n"
                             "Click to lay down points; pose them with the Transform tool."));
    connect(m_rigMode, &QComboBox::currentIndexChanged, this, [this](int index) { m_canvas->setRigMode(Canvas::RigMode(index)); });
    m_form->addRow(tr("Mode"), m_rigMode);
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
    m_byColour = new QCheckBox(tr("Select by Colour"), this);
    m_byColour->setObjectName(QStringLiteral("SelectByColour"));
    m_byColour->setToolTip(tr("A click on a line or on paint selects everything of that colour in the drawing."));
    connect(m_byColour, &QCheckBox::toggled, m_canvas, &Canvas::setSelectByColour);
    m_form->addRow(m_byColour);
    m_paintAll = new QCheckBox(tr("Apply to All Drawings"), this);
    m_paintAll->setObjectName(QStringLiteral("PaintAllDrawings"));
    m_paintAll->setToolTip(tr("What you paint, repaint or unpaint is painted, repainted or unpainted in the same place in every drawing of the layer."));
    connect(m_paintAll, &QCheckBox::toggled, m_canvas, &Canvas::setPaintAllDrawings);
    m_form->addRow(m_paintAll);
    m_permanent = new QCheckBox(tr("Permanent Selection"), this);
    m_permanent->setObjectName(QStringLiteral("SelectPermanent"));
    m_permanent->setToolTip(tr("What is selected stays selected when you go to another frame: whatever lies in the same place in the drawing there."));
    connect(m_permanent, &QCheckBox::toggled, m_canvas, &Canvas::setPermanentSelection);
    m_form->addRow(m_permanent);
    m_snapAlign = new QCheckBox(tr("Snap and Align"), this);
    m_snapAlign->setObjectName(QStringLiteral("SelectSnapAlign"));
    m_snapAlign->setToolTip(tr("What you drag goes edge to edge, or middle to middle, with the other artwork of the drawing when it comes near."));
    connect(m_snapAlign, &QCheckBox::toggled, m_canvas, &Canvas::setSnapAlign);
    m_form->addRow(m_snapAlign);
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

// How lines are finished at their two ends and at corners: three boxes of small
// pictures, side by side.
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
}

// The thickness of the line at the point picked with the Contour Editor: how a line
// drawn with the mouse is made to swell and taper.
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
