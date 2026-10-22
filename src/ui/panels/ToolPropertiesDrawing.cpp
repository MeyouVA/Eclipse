// Eclipse - Tool Properties: the size rows of the drawing tools, pressure ramps, paper textures, brush rows.
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
            fillBitmapTips(tip);
            // One of the four, by number; or one of your own, by name.
            auto choose = [this, tip](int i) {
                if (i < 0)
                    return;
                QString const own = tip->itemData(i).toString();
                if (!own.isEmpty()) {
                    m_canvas->setTipImage(QImage::fromData(settings().value(QStringLiteral("tools/bitmapTips/") + own).toByteArray()));
                    m_canvas->setBitmapTip(Canvas::BitmapImage);
                    settings().setValue(QStringLiteral("tools/bitmapTipName"), own);
                } else {
                    m_canvas->setBitmapTip(Canvas::BitmapTip(std::clamp(i, 0, 3)));
                    settings().remove(QStringLiteral("tools/bitmapTipName"));
                }
                settings().setValue(QStringLiteral("tools/bitmapTip"), i);
            };
            connect(tip, &QComboBox::currentIndexChanged, this, choose);
            QString const keptName = settings().value(QStringLiteral("tools/bitmapTipName")).toString();
            int const kept = keptName.isEmpty() ? std::clamp(settings().value(QStringLiteral("tools/bitmapTip"), 0).toInt(), 0, 3) : std::max(0, tip->findData(keptName));
            tip->setCurrentIndex(kept);
            choose(kept);
            loadTipShape();
            QToolButton* const more = new QToolButton(this);
            more->setObjectName(QStringLiteral("BitmapTipMenu"));
            more->setText(QStringLiteral("..."));
            more->setPopupMode(QToolButton::InstantPopup);
            QMenu* const menu = new QMenu(more);
            connect(menu->addAction(tr("Edit Tip...")), &QAction::triggered, this, [this] { editTipShape(); });
            connect(menu->addAction(tr("Import Tip from Picture...")), &QAction::triggered, this, [this, tip] {
                QString const path = QFileDialog::getOpenFileName(this, tr("Import Tip from Picture"), QString(), tr("Pictures (*.png *.jpg *.jpeg *.bmp *.tif *.tiff)"));
                if (!path.isEmpty())
                    importBitmapTips(path, tip);
            });
            connect(menu->addAction(tr("Import Brushes to Bitmap Brush...")), &QAction::triggered, this, [this, tip] {
                QString const path = QFileDialog::getOpenFileName(this, tr("Import Brushes"), QString(), tr("Photoshop brushes (*.abr)"));
                if (!path.isEmpty())
                    importBitmapTips(path, tip);
            });
            connect(menu->addAction(tr("Delete Tip")), &QAction::triggered, this, [this, tip] {
                QString const own = tip->currentData().toString();
                if (own.isEmpty())
                    return;
                settings().remove(QStringLiteral("tools/bitmapTips/") + own);
                fillBitmapTips(tip);
                tip->setCurrentIndex(0);
            });
            more->setMenu(menu);
            QHBoxLayout* const row = new QHBoxLayout;
            row->addWidget(tip, 1);
            row->addWidget(more);
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
        // Each tool keeps its own: the Pencil (tools/thicknessStencil), the Line tool and the Polyline.
        auto keyOf = [](int tool) {
            return tool == Canvas::Line ? QStringLiteral("tools/lineStencil") : tool == Canvas::Polyline ? QStringLiteral("tools/polylineStencil")
                                                                                                         : QStringLiteral("tools/thicknessStencil");
        };
        // Which one, for a tool: one of the usual ones by number, or one of your own by name.
        auto chooseFor = [this, stencil, keyOf](int tool, int i) {
            if (i < 0)
                return;
            QString const key = keyOf(tool);
            QVariant const own = stencil->itemData(i);
            if (own.isValid()) {
                QVector<float> profile;
                for (QString const& share : settings().value(QStringLiteral("tools/stencils/") + own.toString()).toString().split(QLatin1Char(','), Qt::SkipEmptyParts))
                    profile.push_back(share.toFloat());
                m_canvas->setCustomStencilOf(tool, profile);
                m_canvas->setThicknessStencilOf(tool, Canvas::StencilCount);
                settings().setValue(key + QStringLiteral("Name"), own.toString());
            } else {
                m_canvas->setThicknessStencilOf(tool, i);
                settings().remove(key + QStringLiteral("Name"));
            }
            settings().setValue(key, i);
        };
        auto keptFor = [stencil, keyOf](int tool) {
            QString const key = keyOf(tool);
            QString const keptName = settings().value(key + QStringLiteral("Name")).toString();
            return keptName.isEmpty() ? std::clamp(settings().value(key, 0).toInt(), 0, int(Canvas::StencilCount) - 1) : std::max(0, stencil->findData(keptName));
        };
        connect(stencil, &QComboBox::currentIndexChanged, this, [this, chooseFor](int i) {
            int const tool = m_canvas->tool();
            chooseFor(tool == Canvas::Line || tool == Canvas::Polyline ? tool : int(Canvas::Pencil), i);
        });
        for (int tool : {int(Canvas::Pencil), int(Canvas::Line), int(Canvas::Polyline)})
            chooseFor(tool, keptFor(tool));
        // The box shows the stencil of the tool in hand.
        m_showStencil = [this, stencil, keptFor](int tool) {
            QSignalBlocker const quiet(stencil);
            stencil->setCurrentIndex(keptFor(tool == Canvas::Line || tool == Canvas::Polyline ? tool : int(Canvas::Pencil)));
        };
        m_showStencil(Canvas::Pencil);
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
    // (On or off to begin with as Preferences say: Scale Pencil Line Thickness by default.)
    m_scaleThickness->setChecked(settings().value(QStringLiteral("prefs/scalePencilThickness"), false).toBool());
    m_canvas->setScaleThickness(m_scaleThickness->isChecked());
    if (QAction* const action = m_actions->action(QStringLiteral("ScalePencilThickness")))
        action->setChecked(m_scaleThickness->isChecked());
    connect(m_scaleThickness, &QCheckBox::toggled, this, [this](bool on) {
        m_canvas->setScaleThickness(on);
        if (QAction* const action = m_actions->action(QStringLiteral("ScalePencilThickness")))
            action->setChecked(on);
    });
    m_form->addRow(m_scaleThickness);
    addLineOptionRows();
    // The Pencil Retouch tool: thicker or thinner where it is rubbed.
    {
        QComboBox* const mode = new QComboBox(this);
        mode->setObjectName(QStringLiteral("RetouchMode"));
        mode->addItems({tr("Increase Thickness"), tr("Decrease Thickness"), tr("Increase Opacity"), tr("Decrease Opacity")});
        mode->setToolTip(tr("Rubbed along pencil lines, the Pencil Retouch tool makes them thicker or thinner there - or more solid or fainter. Alt does the other of the pair."));
        int const kept = std::clamp(settings().value(QStringLiteral("tools/retouchMode"), 0).toInt(), 0, 3);
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
    // The Eraser's tip: round or square.
    {
        QComboBox* const tip = new QComboBox(this);
        tip->setObjectName(QStringLiteral("EraserTip"));
        tip->addItems({tr("Round Tip"), tr("Square Tip")});
        tip->setToolTip(tr("The shape of the Eraser's tip."));
        tip->setCurrentIndex(settings().value(QStringLiteral("tools/eraserSquare"), false).toBool() ? 1 : 0);
        m_canvas->setEraserSquare(tip->currentIndex() == 1);
        connect(tip, &QComboBox::currentIndexChanged, this, [this](int i) {
            m_canvas->setEraserSquare(i == 1);
            settings().setValue(QStringLiteral("tools/eraserSquare"), i == 1);
        });
        QHBoxLayout* const row = new QHBoxLayout;
        row->addWidget(tip, 1);
        m_form->addRow(tr("Tip"), row);
        m_eraserTipRow = row;
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
