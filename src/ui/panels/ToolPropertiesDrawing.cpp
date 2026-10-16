// Eclipse - Tool Properties: the settings of the tools that draw - size, smoothing, the brush's
// options, the Rigging tool's mode, the ends and corners of lines, the thickness of a point.
#include "ui/panels/ToolProperties.h"

#include <algorithm>

#include <QCheckBox>
#include <QComboBox>
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
#include "ui/canvas/Canvas.h"
#include "ui/canvas/PencilTexture.h"
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
    // The Brush on a bitmap layer: how hard its edge is, and its flow.
    {
        int const hard = std::clamp(settings().value(QStringLiteral("tools/bitmapHardness"), 100).toInt(), 0, 100);
        int const flow = std::clamp(settings().value(QStringLiteral("tools/bitmapFlow"), 100).toInt(), 1, 100);
        m_canvas->setBitmapHardness(hard);
        m_canvas->setBitmapFlow(flow);
        m_hardnessRow = addSetting(tr("Bitmap Hardness"), tr("On a bitmap layer: how hard the brush's edge is. 100: crisp; less: the paint fades out towards the edge."),
                0, 100, hard, QStringLiteral(" %"), [this](int v) {
                    m_canvas->setBitmapHardness(v);
                    settings().setValue(QStringLiteral("tools/bitmapHardness"), v);
                });
        {
            QComboBox* const tip = new QComboBox(this);
            tip->setObjectName(QStringLiteral("BitmapTip"));
            tip->setToolTip(tr("On a bitmap layer: the shape of the brush's tip, pressed down along the stroke."));
            tip->addItems({tr("Round"), tr("Square"), tr("Chisel"), tr("Spray")});
            int const kept = std::clamp(settings().value(QStringLiteral("tools/bitmapTip"), 0).toInt(), 0, 3);
            tip->setCurrentIndex(kept);
            m_canvas->setBitmapTip(Canvas::BitmapTip(kept));
            connect(tip, &QComboBox::currentIndexChanged, this, [this](int i) {
                m_canvas->setBitmapTip(Canvas::BitmapTip(std::clamp(i, 0, 3)));
                settings().setValue(QStringLiteral("tools/bitmapTip"), i);
            });
            QHBoxLayout* const row = new QHBoxLayout;
            row->addWidget(tip, 1);
            m_form->addRow(tr("Bitmap Tip"), row);
            m_tipRow = row;
        }
        m_flowRow = addSetting(tr("Bitmap Flow"), tr("On a bitmap layer: how much paint a stroke of the brush puts down; strokes over each other build it up."),
                1, 100, flow, QStringLiteral(" %"), [this](int v) {
                    m_canvas->setBitmapFlow(v);
                    settings().setValue(QStringLiteral("tools/bitmapFlow"), v);
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
    // The pen stabiliser: its way, and how far behind the pen the line keeps.
    {
        QComboBox* const mode = new QComboBox(this);
        mode->setObjectName(QStringLiteral("Stabiliser"));
        mode->addItems({tr("Stabilizer Off"), tr("Catch Up"), tr("No Catch Up")});
        mode->setToolTip(tr("The pen stabilizer: the line follows the pen at a distance, taking the shake out of it.\n"
                            "Catch Up: when the pen lifts, the line goes on to where it lifted.\nNo Catch Up: the line ends where it had got to."));
        QSpinBox* const strength = new QSpinBox(this);
        strength->setObjectName(QStringLiteral("StabiliserStrength"));
        strength->setRange(0, 100);
        strength->setSuffix(QStringLiteral(" %"));
        strength->setToolTip(tr("How far behind the pen the line keeps: the more, the steadier the line."));
        int const keptMode = std::clamp(settings().value(QStringLiteral("tools/stabiliser"), 0).toInt(), 0, 2);
        int const keptStrength = std::clamp(settings().value(QStringLiteral("tools/stabiliserStrength"), 50).toInt(), 0, 100);
        mode->setCurrentIndex(keptMode);
        strength->setValue(keptStrength);
        strength->setEnabled(keptMode != 0);
        m_canvas->setStabiliser(keptMode);
        m_canvas->setStabiliserStrength(keptStrength);
        connect(mode, &QComboBox::currentIndexChanged, this, [this, strength](int i) {
            m_canvas->setStabiliser(i);
            strength->setEnabled(i != 0);
            settings().setValue(QStringLiteral("tools/stabiliser"), i);
            if (i != 0)
                settings().setValue(QStringLiteral("tools/stabiliserLast"), i);
        });
        connect(strength, &QSpinBox::valueChanged, this, [this](int v) {
            m_canvas->setStabiliserStrength(v);
            settings().setValue(QStringLiteral("tools/stabiliserStrength"), v);
        });
        QHBoxLayout* const row = new QHBoxLayout;
        row->addWidget(mode, 1);
        row->addWidget(strength);
        m_form->addRow(tr("Stabilizer"), row);
        m_stabiliserRow = row;
    }
    // The Line tool's way: straight, or bent into a curve after the drag.
    {
        QComboBox* const mode = new QComboBox(this);
        mode->setObjectName(QStringLiteral("LineMode"));
        mode->addItems({tr("Straight Line"), tr("Curve Line"), tr("S-Curve Line")});
        mode->setToolTip(tr("Straight: drag from one end to the other.\nCurve: drag the line, then move the pointer to bend it and click to put it down.\n"
                            "S-Curve: the same, bending the line one way at its start and the other way at its end."));
        int const kept = std::clamp(settings().value(QStringLiteral("tools/lineMode"), 0).toInt(), 0, 2);
        mode->setCurrentIndex(kept);
        m_canvas->setLineMode(kept);
        connect(mode, &QComboBox::currentIndexChanged, this, [this](int i) {
            m_canvas->setLineMode(i);
            settings().setValue(QStringLiteral("tools/lineMode"), i);
        });
        QHBoxLayout* const row = new QHBoxLayout;
        row->addWidget(mode, 1);
        m_form->addRow(tr("Mode"), row);
        m_lineModeRow = row;
    }
    // The Pencil's thickness stencil: how thick a new line is along its length.
    {
        QComboBox* const stencil = new QComboBox(this);
        stencil->setObjectName(QStringLiteral("ThicknessStencil"));
        stencil->setToolTip(tr("The thickness stencil: how thick a new pencil line is from its start to its end."));
        fillStencils(stencil);
        // Which one: one of the usual ones by number, or one of your own by name.
        auto choose = [this, stencil](int i) {
            if (i < 0)
                return;
            QVariant const own = stencil->itemData(i);
            if (own.isValid()) {
                QVector<float> profile;
                for (QString const& share : settings().value(QStringLiteral("tools/stencils/") + own.toString()).toString().split(QLatin1Char(','), Qt::SkipEmptyParts))
                    profile.push_back(share.toFloat());
                m_canvas->setCustomStencil(profile);
                m_canvas->setThicknessStencil(Canvas::StencilCount);
                settings().setValue(QStringLiteral("tools/thicknessStencilName"), own.toString());
            } else {
                m_canvas->setThicknessStencil(i);
                settings().remove(QStringLiteral("tools/thicknessStencilName"));
            }
            settings().setValue(QStringLiteral("tools/thicknessStencil"), i);
        };
        connect(stencil, &QComboBox::currentIndexChanged, this, choose);
        QString const keptName = settings().value(QStringLiteral("tools/thicknessStencilName")).toString();
        int const kept = keptName.isEmpty() ? std::clamp(settings().value(QStringLiteral("tools/thicknessStencil"), 0).toInt(), 0, int(Canvas::StencilCount) - 1)
                                            : std::max(0, stencil->findData(keptName));
        stencil->setCurrentIndex(kept);
        choose(kept);
        // Stencils of your own: made from the thickness of the line selected; renamed; deleted.
        QToolButton* const more = new QToolButton(this);
        more->setObjectName(QStringLiteral("StencilMenu"));
        more->setText(QStringLiteral("..."));
        more->setPopupMode(QToolButton::InstantPopup);
        QMenu* const menu = new QMenu(more);
        QAction* const create = menu->addAction(tr("New Thickness Stencil"));
        create->setObjectName(QStringLiteral("NewThicknessStencil"));
        create->setToolTip(tr("A stencil of your own, from the thickness of the pencil line selected (select it with the Select tool first)."));
        QAction* const rename = menu->addAction(tr("Rename Thickness Stencil"));
        QAction* const remove = menu->addAction(tr("Delete Thickness Stencil"));
        more->setMenu(menu);
        connect(create, &QAction::triggered, this, [this] {
            if (m_canvas->selectedLineProfile().isEmpty()) {
                QMessageBox::information(this, tr("New Thickness Stencil"), tr("Select a pencil line first (the Select tool): the stencil is made from its thickness."));
                return;
            }
            QString const name = QInputDialog::getText(this, tr("New Thickness Stencil"), tr("Thickness Stencil Name:"), QLineEdit::Normal, tr("Thickness Stencil 1"));
            if (!name.trimmed().isEmpty())
                makeStencil(name.trimmed());
        });
        connect(rename, &QAction::triggered, this, [this, stencil] {
            QVariant const own = stencil->currentData();
            if (!own.isValid())
                return;
            QString const name = QInputDialog::getText(this, tr("Rename Thickness Stencil"), tr("Thickness Stencil Name:"), QLineEdit::Normal, own.toString());
            if (name.trimmed().isEmpty() || name.trimmed() == own.toString())
                return;
            QVariant const shares = settings().value(QStringLiteral("tools/stencils/") + own.toString());
            settings().remove(QStringLiteral("tools/stencils/") + own.toString());
            settings().setValue(QStringLiteral("tools/stencils/") + name.trimmed(), shares);
            fillStencils(stencil);
            stencil->setCurrentIndex(std::max(0, stencil->findData(name.trimmed())));
        });
        connect(remove, &QAction::triggered, this, [this, stencil] {
            QVariant const own = stencil->currentData();
            if (!own.isValid())
                return;
            settings().remove(QStringLiteral("tools/stencils/") + own.toString());
            fillStencils(stencil);
            stencil->setCurrentIndex(0);
        });
        QHBoxLayout* const row = new QHBoxLayout;
        row->addWidget(stencil, 1);
        row->addWidget(more);
        m_form->addRow(tr("Thickness Stencil"), row);
        m_stencilRow = row;
    }
    // The Brush and the Pencil: a line whose ends meet is painted inside.
    m_lineFill = new QCheckBox(tr("Auto Fill"), this);
    m_lineFill->setObjectName(QStringLiteral("LineAutoFill"));
    m_lineFill->setToolTip(tr("Automatically fills closed areas with the selected colour: a line whose ends meet is painted inside."));
    m_lineFill->setChecked(settings().value(QStringLiteral("tools/lineAutoFill"), false).toBool());
    m_canvas->setLineAutoFill(m_lineFill->isChecked());
    connect(m_lineFill, &QCheckBox::toggled, this, [this](bool on) {
        m_canvas->setLineAutoFill(on);
        settings().setValue(QStringLiteral("tools/lineAutoFill"), on);
        if (QAction* const action = m_actions->action(QStringLiteral("AutoFillInside")))
            action->setChecked(on);
    });
    m_form->addRow(m_lineFill);
    // The Select tool: lines scaled get thicker or thinner with it.
    m_scaleThickness = new QCheckBox(tr("Scale Pencil Line Thickness"), this);
    m_scaleThickness->setObjectName(QStringLiteral("SelectScaleThickness"));
    m_scaleThickness->setToolTip(tr("When enabled, the width of pencil lines will be adjusted when scaling them with the Select tool."));
    m_scaleThickness->setChecked(settings().value(QStringLiteral("tools/scaleThickness"), false).toBool());
    m_canvas->setScaleThickness(m_scaleThickness->isChecked());
    connect(m_scaleThickness, &QCheckBox::toggled, this, [this](bool on) {
        m_canvas->setScaleThickness(on);
        settings().setValue(QStringLiteral("tools/scaleThickness"), on);
    });
    m_form->addRow(m_scaleThickness);
    addLineOptionRows();
    // The Pencil Retouch tool: thicker or thinner where it is rubbed.
    {
        QComboBox* const mode = new QComboBox(this);
        mode->setObjectName(QStringLiteral("RetouchMode"));
        mode->addItems({tr("Increase Thickness"), tr("Decrease Thickness")});
        mode->setToolTip(tr("Rubbed along pencil lines, the Pencil Retouch tool makes them thicker or thinner there. Alt does the other."));
        int const kept = std::clamp(settings().value(QStringLiteral("tools/retouchMode"), 0).toInt(), 0, 1);
        mode->setCurrentIndex(kept);
        m_canvas->setRetouchMode(kept);
        connect(mode, &QComboBox::currentIndexChanged, this, [this](int i) {
            m_canvas->setRetouchMode(i);
            settings().setValue(QStringLiteral("tools/retouchMode"), i);
        });
        QHBoxLayout* const row = new QHBoxLayout;
        row->addWidget(mode, 1);
        m_form->addRow(tr("Retouch"), row);
        m_retouchRow = row;
    }
    // The Rectangle and the Ellipse: painted inside as they are drawn.
    m_shapeFill = new QCheckBox(tr("Auto Fill"), this);
    m_shapeFill->setObjectName(QStringLiteral("ShapeAutoFill"));
    m_shapeFill->setToolTip(tr("Automatically fills your shape with the selected colour."));
    m_shapeFill->setChecked(settings().value(QStringLiteral("tools/shapeAutoFill"), false).toBool());
    m_canvas->setShapeAutoFill(m_shapeFill->isChecked());
    connect(m_shapeFill, &QCheckBox::toggled, this, [this](bool on) {
        m_canvas->setShapeAutoFill(on);
        settings().setValue(QStringLiteral("tools/shapeAutoFill"), on);
    });
    m_form->addRow(m_shapeFill);
    m_cornersRow = addSetting(tr("Corners"), tr("A turn sharper than this many degrees stays a sharp corner; gentler turns are rounded. 180 rounds everything."),
            0, 180, 50, QStringLiteral(" deg"), [this](int v) { m_canvas->setCornerAngle(v); });
}

// The ramp of a texture: two numbers - grain fainter than the first is no ink at all, grain denser than the
// second is solid ink.  For lines, or for the brush.
QWidget* ToolProperties::rampRow(QString const& name, bool brush)
{
    QWidget* const row = new QWidget(this);
    QHBoxLayout* const layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    QSpinBox* const low = new QSpinBox(row);
    QSpinBox* const high = new QSpinBox(row);
    low->setObjectName(name + QStringLiteral("Low"));
    high->setObjectName(name + QStringLiteral("High"));
    for (QSpinBox* const box : {low, high}) {
        box->setRange(0, 100);
        box->setSingleStep(5);
        box->setSuffix(QStringLiteral(" %"));
    }
    low->setToolTip(tr("Clear below: grain fainter than this is no ink at all - raise it to clear away the faint grey parts of a texture."));
    high->setToolTip(tr("Solid above: grain denser than this is solid ink. With both numbers alike, the texture is ink or nothing."));
    QString const key = QStringLiteral("tools/") + QLatin1String(brush ? "brushTextureRamp" : "lineTextureRamp");
    int const keptLow = std::clamp(settings().value(key + QStringLiteral("Low"), 0).toInt(), 0, 100);
    int const keptHigh = std::clamp(settings().value(key + QStringLiteral("High"), 100).toInt(), keptLow, 100);
    low->setValue(keptLow);
    high->setValue(keptHigh);
    auto apply = [this, low, high, brush, key] {
        // (The one never passes the other.)
        if (low->value() > high->value()) {
            QSignalBlocker const block(sender() == low ? high : low);
            (sender() == low ? high : low)->setValue(sender() == low ? low->value() : high->value());
        }
        if (brush)
            m_canvas->setBrushTextureRamp(low->value(), high->value());
        else
            m_canvas->setLineTextureRamp(low->value(), high->value());
        settings().setValue(key + QStringLiteral("Low"), low->value());
        settings().setValue(key + QStringLiteral("High"), high->value());
    };
    if (brush)
        m_canvas->setBrushTextureRamp(keptLow, keptHigh);
    else
        m_canvas->setLineTextureRamp(keptLow, keptHigh);
    connect(low, &QSpinBox::valueChanged, this, apply);
    connect(high, &QSpinBox::valueChanged, this, apply);
    layout->addWidget(new QLabel(tr("Ramp: clear below"), row));
    layout->addWidget(low);
    layout->addWidget(new QLabel(tr("solid above"), row));
    layout->addWidget(high);
    layout->addStretch();
    return row;
}

// The textures there are, in a list; `current` is the one shown as chosen.
void ToolProperties::fillTextureBox(QComboBox* box, int current)
{
    box->clear();
    for (int kind : PencilTexture::kinds())
        box->addItem(PencilTexture::icon(kind), PencilTexture::name(kind), kind);
    box->setCurrentIndex(std::max(0, box->findData(current)));
}

// The button beside a list of textures: one of your own from a picture, one put together, the one in hand deleted.
QToolButton* ToolProperties::textureButton(QString const& name)
{
    QToolButton* const button = new QToolButton(this);
    button->setObjectName(name);
    button->setText(QStringLiteral("..."));
    button->setToolTip(tr("Textures of your own: bring one in from a picture, make one, or delete the one chosen."));
    button->setPopupMode(QToolButton::InstantPopup);
    QMenu* const menu = new QMenu(button);
    menu->addAction(tr("Import Texture from Picture..."), m_canvas, &Canvas::onActionImportTexture);
    menu->addAction(tr("New Texture..."), m_canvas, &Canvas::onActionNewTexture);
    menu->addSeparator();
    menu->addAction(tr("Rename Texture..."), m_canvas, &Canvas::onActionRenameTexture);
    menu->addAction(tr("Export Texture..."), m_canvas, &Canvas::onActionExportTexture);
    menu->addAction(tr("Delete Texture"), m_canvas, &Canvas::onActionDeleteTexture);
    button->setMenu(menu);
    return button;
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

    // The paper texture: the grain a brush stroke shows in place of solid paint, and how much of it.
    m_paperRow = new QHBoxLayout;
    QComboBox* const paper = new QComboBox(this);
    paper->setObjectName(QStringLiteral("BrushTexture"));
    paper->setIconSize(QSize(72, 20));
    paper->setToolTip(tr("The paper texture: the grain a brush stroke shows in place of solid paint. Applies to new strokes and to selected ones."));
    QSpinBox* const shows = new QSpinBox(this);
    shows->setObjectName(QStringLiteral("BrushTextureAmount"));
    shows->setRange(0, 100);
    shows->setSingleStep(5);
    shows->setSuffix(QStringLiteral(" %"));
    shows->setToolTip(tr("How much of the texture shows: 100 the grain as it is, 0 solid paint after all."));
    {
        int const kept = settings().value(QStringLiteral("tools/brushTexture"), 0).toInt();
        int const keptAmount = std::clamp(settings().value(QStringLiteral("tools/brushTextureAmount"), 100).toInt(), 0, 100);
        shows->setValue(keptAmount);
        m_canvas->setBrushTextureAmount(keptAmount);
        m_canvas->setBrushTexture(kept);
    }
    auto list = [this, paper, shows] {
        QSignalBlocker const block(paper);
        fillTextureBox(paper, m_canvas->brushTexture());
        shows->setEnabled(m_canvas->brushTexture() != 0);
    };
    list();
    connect(m_canvas, &Canvas::texturesChanged, this, list);
    connect(paper, &QComboBox::currentIndexChanged, this, [this, paper, shows](int) {
        int const kind = paper->currentData().toInt();
        m_canvas->setBrushTexture(kind);
        shows->setEnabled(kind != 0);
        settings().setValue(QStringLiteral("tools/brushTexture"), kind);
    });
    connect(shows, &QSpinBox::valueChanged, this, [this](int v) {
        m_canvas->setBrushTextureAmount(v);
        settings().setValue(QStringLiteral("tools/brushTextureAmount"), v);
    });
    m_paperRow->addWidget(new QLabel(tr("Paper Texture"), this));
    m_paperRow->addWidget(paper, 1);
    m_paperRow->addWidget(shows);
    m_paperRow->addWidget(textureButton(QStringLiteral("BrushTextureMenu")));
    m_form->addRow(m_paperRow);
    m_paperCrisp = new QCheckBox(tr("Crisp texture edge"), this);
    m_paperCrisp->setObjectName(QStringLiteral("BrushTextureCrisp"));
    m_paperCrisp->setToolTip(tr("A crisp edge: the grain frays at the edge but does not fade there, and is not blurred - every bit of it is ink or nothing. Applies to new strokes and to selected ones."));
    m_paperCrisp->setChecked(settings().value(QStringLiteral("tools/brushTextureCrisp"), false).toBool());
    m_canvas->setBrushTextureCrisp(m_paperCrisp->isChecked());
    connect(m_paperCrisp, &QCheckBox::toggled, this, [this](bool on) {
        m_canvas->setBrushTextureCrisp(on);
        settings().setValue(QStringLiteral("tools/brushTextureCrisp"), on);
    });
    m_form->addRow(m_paperCrisp);
    m_paperRamp = rampRow(QStringLiteral("BrushTextureRamp"), true);
    m_form->addRow(m_paperRamp);
}

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
        if (m_lineFill)
            m_lineFill->setChecked(m_canvas->lineAutoFill());
        settings().setValue(QStringLiteral("tools/lineAutoFill"), m_canvas->lineAutoFill());
    });
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
    number(tr("Speed Sensitivity"), tr("The faster the pen moves, the thinner the Brush's line (with Invert: the thicker)."), "speed", 100);
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

void ToolProperties::fillStencils(QComboBox* box)
{
    QSignalBlocker const quiet(box);
    box->clear();
    box->addItems({tr("Uniform"), tr("Tapered"), tr("Tapered Beginning"), tr("Tapered End"), tr("Medium Tapered"), tr("Pointy"), tr("Fat Ends")});
    QSettings s = settings();
    s.beginGroup(QStringLiteral("tools/stencils"));
    QStringList names = s.childKeys();
    s.endGroup();
    names.sort(Qt::CaseInsensitive);
    for (QString const& name : std::as_const(names))
        box->addItem(name, name);
}

bool ToolProperties::makeStencil(QString const& name)
{
    QVector<float> const profile = m_canvas->selectedLineProfile();
    QComboBox* const box = findChild<QComboBox*>(QStringLiteral("ThicknessStencil"));
    if (profile.isEmpty() || name.isEmpty() || !box)
        return false;
    QStringList shares;
    for (float share : profile)
        shares << QString::number(share, 'g', 4);
    settings().setValue(QStringLiteral("tools/stencils/") + name, shares.join(QLatin1Char(',')));
    fillStencils(box);
    box->setCurrentIndex(std::max(0, box->findData(name)));
    return true;
}
