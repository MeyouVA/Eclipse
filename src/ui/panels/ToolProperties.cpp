// Eclipse - Tool Properties: how the form is put together, which rows a tool shows, and the sizes to pick from.
#include "ui/panels/ToolProperties.h"
#include "actions/theme/InterfaceColours.h"

#include <algorithm>
#include <cmath>

#include <QAction>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>

#include "actions/manager/ActionManager.h"
#include "model/document/Commands.h"
#include "canvas/Canvas.h"
#include "ui/window/MainWindowPrivate.h"
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QComboBox>
#include <QSettings>
#include <QMenu>

// A stroke of a given thickness on a light card: what the tool in hand will draw.
QPixmap ToolProperties::strokePicture(QSize size, double thickness, bool taper)
{
    QPixmap pixmap(size);
    pixmap.fill(InterfaceColours::get("brushPreview"));
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);
    // A lazy S across the card.
    QPainterPath wave;
    double const w = size.width(), h = size.height();
    wave.moveTo(w * 0.07, h * 0.62);
    wave.cubicTo(w * 0.33, h * 0.12, w * 0.60, h * 0.95, w * 0.93, h * 0.40);
    double const shown = std::clamp(thickness, 1.0, h * 0.7);
    if (!taper) {
        p.setPen(QPen(QColor(20, 20, 20), shown, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(wave);
    } else {
        // A brush swells in the middle and thins at its ends.
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(20, 20, 20));
        for (int i = 0; i <= 120; ++i) {
            double const t = i / 120.0;
            double const r = std::max(0.5, shown / 2.0 * std::sin(t * 3.14159265358979323846));
            p.drawEllipse(wave.pointAtPercent(t), r, r);
        }
    }
    return pixmap;
}

// The picture at the top of Tool Properties: the tip of the tool, and a stroke of it.
class StrokePreview : public QWidget {
public:
    explicit StrokePreview(QWidget* parent) : QWidget(parent) { setFixedHeight(56); }
    void set(double thickness, bool taper)
    {
        m_thickness = thickness;
        m_taper = taper;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        QRect const tip(0, 0, 56, height()), stroke(62, 0, width() - 62, height());
        p.fillRect(tip, InterfaceColours::get("brushPreview"));
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(20, 20, 20));
        double const r = std::clamp(m_thickness / 2.0, 0.8, height() * 0.42);
        p.drawEllipse(QRectF(tip).center(), r, r);
        p.drawPixmap(stroke.topLeft(), ToolProperties::strokePicture(stroke.size(), m_thickness, m_taper));
    }

private:
    double m_thickness = 6.0;
    bool m_taper = true;
};

void ToolProperties::addMorphRows()
{
    m_morphLayers = new QListWidget(this);
    m_morphLayers->setObjectName(QStringLiteral("MorphLayers"));
    m_morphLayers->setMaximumHeight(120);
    m_morphLayers->setToolTip(tr("The morphing layers of the morphing here: shapes put in one change only into shapes of the same one. "
                                 "Choose one, then Ctrl+click shapes on the first and the last drawing to put them in it (or take them out). "
                                 "Double-click to rename."));
    m_form->addRow(tr("Morphing Layers"), m_morphLayers);
    m_morphButtons = new QWidget(this);
    QHBoxLayout* const row = new QHBoxLayout(m_morphButtons);
    row->setContentsMargins(0, 0, 0, 0);
    // A change to the morphing layers of the morphing here, as one step.
    auto change = [this](QString const& text, std::function<void(QVector<Morph::Group>&, int&)> edit) {
        int const morph = m_canvas->morphHere();
        int const layer = m_canvas->currentLayer();
        Scene const& scene = m_document->scene();
        if (morph < 0 || layer < 0 || layer >= scene.layers.size())
            return;
        QVector<Morph::Group> groups = scene.layers[layer].morphs[morph].groups;
        int current = m_morphLayers->currentRow();
        edit(groups, current);
        m_document->setMorphGroups(layer, morph, groups, text);
        m_canvas->setMorphGroup(current);
        refreshMorphLayers();
    };
    auto button = [&](QString const& text, QString const& name, QString const& tip) {
        QPushButton* const b = new QPushButton(text, m_morphButtons);
        b->setObjectName(name);
        b->setToolTip(tip);
        row->addWidget(b);
        return b;
    };
    connect(button(tr("Add"), QStringLiteral("MorphLayerAdd"), tr("A new morphing layer")), &QPushButton::clicked, this, [change] {
        change(QObject::tr("Add Morphing Layer"), [](QVector<Morph::Group>& groups, int& current) {
            groups.push_back({QObject::tr("Morphing Layer %1").arg(groups.size() + 1), {}, {}});
            current = int(groups.size()) - 1;
        });
    });
    connect(button(tr("Remove"), QStringLiteral("MorphLayerRemove"), tr("Take the chosen morphing layer away (its shapes pair up with the rest)")),
            &QPushButton::clicked, this, [change] {
                change(QObject::tr("Remove Morphing Layer"), [](QVector<Morph::Group>& groups, int& current) {
                    if (current < 0 || current >= groups.size())
                        return;
                    groups.removeAt(current);
                    current = std::min(current, int(groups.size()) - 1);
                });
            });
    connect(button(QStringLiteral("\u2191"), QStringLiteral("MorphLayerUp"), tr("Move the chosen morphing layer up")), &QPushButton::clicked, this, [change] {
        change(QObject::tr("Move Morphing Layer Up"), [](QVector<Morph::Group>& groups, int& current) {
            if (current > 0 && current < groups.size()) {
                groups.swapItemsAt(current, current - 1);
                --current;
            }
        });
    });
    connect(button(QStringLiteral("\u2193"), QStringLiteral("MorphLayerDown"), tr("Move the chosen morphing layer down")), &QPushButton::clicked, this, [change] {
        change(QObject::tr("Move Morphing Layer Down"), [](QVector<Morph::Group>& groups, int& current) {
            if (current >= 0 && current + 1 < groups.size()) {
                groups.swapItemsAt(current, current + 1);
                ++current;
            }
        });
    });
    m_form->addRow(QString(), m_morphButtons);
    connect(m_morphLayers, &QListWidget::currentRowChanged, this, [this](int row) {
        if (!m_fillingMorphs)
            m_canvas->setMorphGroup(row);
    });
    connect(m_morphLayers, &QListWidget::itemChanged, this, [this, change](QListWidgetItem* item) {
        if (m_fillingMorphs)
            return;
        int const row = m_morphLayers->row(item);
        QString const name = item->text().trimmed();
        change(QObject::tr("Rename Morphing Layer"), [row, name](QVector<Morph::Group>& groups, int& current) {
            if (row >= 0 && row < groups.size() && !name.isEmpty())
                groups[row].name = name;
            current = row;
        });
    });
    connect(m_document, &Document::changed, this, [this] {
        if (m_canvas->tool() == Canvas::Morphing)
            refreshMorphLayers();
    });
    connect(m_canvas, &Canvas::morphHereChanged, this, [this] {
        if (m_canvas->tool() == Canvas::Morphing)
            refreshMorphLayers();
    });
}

void ToolProperties::refreshMorphLayers()
{
    if (!m_morphLayers)
        return;
    m_fillingMorphs = true;
    int const keep = m_canvas->morphGroup();
    m_morphLayers->clear();
    int const morph = m_canvas->morphHere();
    int const layer = m_canvas->currentLayer();
    Scene const& scene = m_document->scene();
    bool const here = morph >= 0 && layer >= 0 && layer < scene.layers.size() && morph < scene.layers[layer].morphs.size();
    if (here)
        for (Morph::Group const& g : scene.layers[layer].morphs[morph].groups) {
            QListWidgetItem* const item = new QListWidgetItem(tr("%1  (%2 / %3)").arg(g.name).arg(g.first.size()).arg(g.second.size()));
            item->setData(Qt::EditRole, g.name);
            item->setFlags(item->flags() | Qt::ItemIsEditable);
            m_morphLayers->addItem(item);
        }
    m_morphLayers->setEnabled(here);
    m_morphButtons->setEnabled(here);
    int const row = std::min(keep, m_morphLayers->count() - 1);
    m_morphLayers->setCurrentRow(row);
    m_fillingMorphs = false;
    if (row != keep)
        m_canvas->setMorphGroup(row);
}

ToolProperties::ToolProperties(Document* document, Canvas* canvas, ActionManager* actions, int envelopePoints, QWidget* parent)
    : QWidget(parent), m_document(document), m_canvas(canvas), m_actions(actions), m_envelopeCount(envelopePoints)
{
    setObjectName(QStringLiteral("ToolProperties"));
    m_form = new QFormLayout(this);
    m_toolTitle = new QLabel(this);
    m_toolTitle->setObjectName(QStringLiteral("ToolTitle"));
    m_preview = new StrokePreview(this);
    m_preview->setObjectName(QStringLiteral("StrokePreview"));
    m_noOptions = new QLabel(this);
    m_noOptions->setObjectName(QStringLiteral("NoOptions"));
    m_form->addRow(m_toolTitle);
    m_form->addRow(m_preview);
    m_form->addRow(m_noOptions);

    addSizeRows();
    addBrushRows();
    addRiggingRows();
    addSelectRows();
    addAutoFlattenRow();
    addLineShapeRows();
    addThicknessRow();
    addTextRows();
    addSelectionRows();
    addMorphRows();
    addPaintRows();
    addZoomRows();
    addTransformRows();
    addPresetRows();

    connect(m_canvas, &Canvas::toolChanged, this, [this](int tool) { showFor(tool); });
    connect(m_size, &QSlider::valueChanged, this, [this](int v) { m_preview->set(v, m_canvas->tool() == Canvas::Brush); });
    showFor(int(m_canvas->tool()));
}

void ToolProperties::refresh()
{
    showFor(int(m_canvas->tool()));
}

QLayout* ToolProperties::addSetting(QString const& label, QString const& tip, int min, int max, int value, QString const& suffix, std::function<void(int)> apply)
{
    QSlider* const slider = new QSlider(Qt::Horizontal, this);
    slider->setRange(min, max);
    slider->setValue(value);
    QSpinBox* const box = new QSpinBox(this);
    box->setObjectName(label);
    box->setRange(min, max);
    box->setValue(value);
    box->setSuffix(suffix);
    slider->setToolTip(tip);
    box->setToolTip(tip);
    connect(slider, &QSlider::valueChanged, box, &QSpinBox::setValue);
    connect(box, &QSpinBox::valueChanged, slider, &QSlider::setValue);
    connect(slider, &QSlider::valueChanged, this, apply);
    QHBoxLayout* const row = new QHBoxLayout;
    row->addWidget(slider, 1);
    row->addWidget(box);
    m_form->addRow(label, row);
    return row;
}

// Sizes to pick from, each with a picture of its stroke.
void ToolProperties::addPresetRows()
{
    m_presetsTitle = new QLabel(tr("Presets"), this);
    m_presets = new QListWidget(this);
    m_presets->setObjectName(QStringLiteral("Presets"));
    m_presets->setIconSize(QSize(120, 26));
    m_presets->setUniformItemSizes(true);
    m_presets->setMinimumHeight(150);
    m_form->addRow(m_presetsTitle);
    m_form->addRow(m_presets);
    setPresetView(std::clamp(settings().value(QStringLiteral("tools/presetView"), 0).toInt(), 0, 4));
    // Right-click: how the list is shown, and the user's own preset updated to the settings now in hand.
    m_presets->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_presets, &QWidget::customContextMenuRequested, this, [this](QPoint const& at) {
        QMenu menu(this);
        QListWidgetItem* const item = m_presets->itemAt(at);
        QAction* const update = menu.addAction(tr("Update Preset"));
        update->setEnabled(item && item->data(Qt::UserRole + 4).isValid());
        menu.addSeparator();
        QAction* const views[5] = {menu.addAction(tr("Small Thumbnail")), menu.addAction(tr("Large Thumbnail")), menu.addAction(tr("Detail View")),
                                   menu.addAction(tr("Swatch Mode")), menu.addAction(tr("Swatch Mode (Small)"))};
        int const now = settings().value(QStringLiteral("tools/presetView"), 0).toInt();
        for (int v = 0; v < 5; ++v) {
            views[v]->setCheckable(true);
            views[v]->setChecked(v == now);
        }
        QAction* const chosen = menu.exec(m_presets->viewport()->mapToGlobal(at));
        if (chosen == update && item)
            updatePreset(item->data(Qt::UserRole + 4).toInt());
        for (int v = 0; v < 5; ++v)
            if (chosen == views[v])
                setPresetView(v);
    });
    connect(m_presets, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        m_size->setValue(item->data(Qt::UserRole).toInt());
        // A preset that sets more: the thinnest the line gets, its taper.
        int const least = item->data(Qt::UserRole + 2).isValid() ? item->data(Qt::UserRole + 2).toInt() : -1;
        int const taper = item->data(Qt::UserRole + 3).isValid() ? item->data(Qt::UserRole + 3).toInt() : -1;
        if (QSpinBox* const box = findChild<QSpinBox*>(tr("Minimum Size")); box && least >= 0)
            box->setValue(least);
        for (QString const& name : {tr("Start Taper"), tr("End Taper")})
            if (QSpinBox* const box = findChild<QSpinBox*>(name); box && taper >= 0)
                box->setValue(taper);
        // An eraser preset: its tip too.
        if (int const square = item->data(Qt::UserRole + 5).isValid() ? item->data(Qt::UserRole + 5).toInt() : -1; square >= 0)
            if (QComboBox* const tip = findChild<QComboBox*>(QStringLiteral("EraserTip")))
                tip->setCurrentIndex(square);
        // A preset of your own keeps the smoothing as well.
        QVariant const smoothing = item->data(Qt::UserRole + 1);
        if (QSpinBox* const box = findChild<QSpinBox*>(tr("Smoothing")); box && smoothing.isValid())
            box->setValue(smoothing.toInt());
    });
    connect(m_presets, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
        if (item)
            m_size->setValue(item->data(Qt::UserRole).toInt());
    });
}

void ToolProperties::showFor(int tool)
{
    using T = Canvas::Tool;
    bool const brush = tool == T::Brush, pencil = tool == T::Pencil, eraser = tool == T::Eraser;
    bool const shape = tool == T::Line || tool == T::Rectangle || tool == T::Ellipse || tool == T::Polyline;
    bool const draws = brush || pencil || eraser || shape || tool == T::SmoothEditor || tool == T::RepaintBrush || tool == T::PencilRetouch;   // (these have a size too)
    bool const lines = pencil || shape || tool == T::Select || tool == T::ContourEditor;
    bool const text = tool == T::Text, contour = tool == T::ContourEditor;
    static char const* const ids[] = {"ToolSelect", "ToolTransform", "ToolBrush",  "ToolEraser",    "ToolPaint",  "ToolHand",
                                      "ToolPencil", "ToolLine",      "ToolRectangle", "ToolEllipse", "ToolContourEditor",
                                      "ToolRigging", "ToolIK", "ToolDropper", "ToolZoom", "ToolPolyline",
                                      "ToolPaintUnpainted", "ToolRepaint", "ToolUnpaint", "ToolText", "ToolRotateView", "ToolCutter", "ToolStroke", "ToolCloseGap",
                                      "ToolEditGradient", "ToolReposition", "ToolStamp", "ToolPencilEditor", "ToolPerspective", "ToolInk", "ToolSmoothEditor", "ToolEnvelope", "ToolMorphing", "ToolRepaintBrush", "ToolSplineOffset", "ToolTurn3D", "ToolPencilRetouch", "ToolShiftTrace", "ToolDrawingPivot", "ToolStaticCamera"};
    QAction* const action = m_actions->action(QLatin1String(ids[tool]));
    QString const name = action ? action->text().remove(QLatin1Char('&')) : tr("Tool");
    bool const any = tool == T::Transform || tool == T::DrawingPivot || tool == T::Ink || draws || lines || text || tool == T::Morphing || tool == T::Select || tool == T::Cutter || tool == T::Rigging || tool == T::Dropper || tool == T::Stamp || tool == T::Envelope || tool == T::Reposition || tool == T::Eraser || Canvas::isPaintTool(Canvas::Tool(tool));
    m_form->setRowVisible(m_rigMode, tool == T::Rigging);
    m_form->setRowVisible(m_morphLayers, tool == T::Morphing);
    m_form->setRowVisible(m_morphButtons, tool == T::Morphing);
    if (tool == T::Morphing)
        refreshMorphLayers();
    for (QWidget* const row : {static_cast<QWidget*>(m_weightBone), static_cast<QWidget*>(m_weightSize), static_cast<QWidget*>(m_weightClear)})
        m_form->setRowVisible(row, tool == T::Rigging && m_rigMode->currentIndex() == Canvas::RigWeights);
    m_form->setRowVisible(m_envelopePoints, tool == T::Rigging);
    m_form->setRowVisible(m_envelopeOutline, tool == T::Rigging);
    QString const heading = brush ? tr("Vector Brush Properties") : eraser ? tr("Vector Eraser Properties") : pencil ? tr("Pencil Properties")
                            : shape ? tr("%1 Tool Properties").arg(name) : Canvas::isPaintTool(Canvas::Tool(tool)) ? tr("Paint Tool Options")
                            : contour ? tr("Contour Editor Options") : tool == T::PencilRetouch ? tr("Pencil Retouch Options") : tr("%1 Tool Options").arg(name.endsWith(tr(" Tool")) ? name.chopped(tr(" Tool").size()) : name);
    m_toolTitle->setText(any ? heading : QString());
    m_noOptions->setText(tr("No options for %1.").arg(name));
    m_form->setRowVisible(m_toolTitle, any);
    m_form->setRowVisible(m_noOptions, !any);
    m_form->setRowVisible(m_preview, draws);
    m_form->setRowVisible(m_sizeRow, draws);
    m_form->setRowVisible(m_smoothingRow, brush || pencil);
    m_form->setRowVisible(m_stabiliserRow, brush || pencil);
    m_form->setRowVisible(m_lineModeRow, tool == T::Line);
    m_form->setRowVisible(m_stencilRow, pencil || tool == T::Line || tool == T::Polyline);
    if (m_showStencil && (pencil || tool == T::Line || tool == T::Polyline))
        m_showStencil(tool);
    m_form->setRowVisible(m_retouchRow, tool == T::PencilRetouch);
    m_form->setRowVisible(m_eraserTipRow, eraser);
    m_form->setRowVisible(m_lineFill, brush || pencil);
    // The line options: which tool has which.
    for (QLayout* const row : std::as_const(m_brushLineRows))
        m_form->setRowVisible(row, brush);
    for (QWidget* const check : std::as_const(m_lineOptionBoxes)) {
        QString const option = check->objectName();
        bool const shown = option == QStringLiteral("LinespeedInvert") ? brush
                           : option == QStringLiteral("Linetrim") || option == QStringLiteral("Lineconnect") ? (pencil || tool == T::StrokeTool)
                           : option == QStringLiteral("LinecloseGap") || option == QStringLiteral("Linebuilding") ? (brush || pencil)
                           : option == QStringLiteral("Linestraight") ? tool == T::StrokeTool : false;
        m_form->setRowVisible(check, shown);
    }
    m_form->setRowVisible(m_scaleThickness, tool == T::Select);
    m_form->setRowVisible(m_tipToCamera, brush || pencil);
    {
        QSignalBlocker const quiet(m_tipToCamera);
        m_tipToCamera->setChecked(m_canvas->tipToCamera(tool));
    }
    m_form->setRowVisible(m_shapeFill, tool == T::Rectangle || tool == T::Ellipse);
    m_form->setRowVisible(m_minSizeRow, brush);
    m_form->setRowVisible(m_hardnessRow, brush);
    m_form->setRowVisible(m_flowRow, brush);
    m_form->setRowVisible(m_tipRow, brush);
    m_form->setRowVisible(m_envToolRow, tool == T::Envelope);
    m_form->setRowVisible(m_gapRow, tool == T::Paint || tool == T::PaintUnpainted);
    m_form->setRowVisible(m_cornersRow, brush || pencil);
    m_form->setRowVisible(m_brushShape, brush);
    m_form->setRowVisible(m_autoFlatten, brush);
    m_form->setRowVisible(m_drawBehind, brush);
    m_form->setRowVisible(static_cast<QLayout*>(m_paperRow), brush);
    m_form->setRowVisible(m_paperCrisp, brush);
    m_form->setRowVisible(m_paperRamp, brush);
    m_drawBehind->setChecked(m_canvas->drawBehind());
    m_autoFlatten->setChecked(m_canvas->autoFlatten());
    m_autoFlatten->setEnabled(m_canvas->brushShapes());
    m_brushShape->setChecked(m_canvas->brushShapes());
    m_form->setRowVisible(static_cast<QLayout*>(m_shapeRow), lines);
    m_form->setRowVisible(static_cast<QLayout*>(m_textureRow), lines && !brush);
    m_form->setRowVisible(m_lineCrisp, lines && !brush);
    m_form->setRowVisible(m_lineRamp, lines && !brush);
    m_form->setRowVisible(m_thicknessRow, contour);
    m_form->setRowVisible(m_selection, tool == T::Select || tool == T::Cutter || Canvas::isPaintTool(Canvas::Tool(tool)));
    m_form->setRowVisible(m_fontRow, text);
    m_form->setRowVisible(m_textRow, text);
    m_form->setRowVisible(m_paraRow, text);
    m_form->setRowVisible(m_applyAll, tool == T::Select);
    m_form->setRowVisible(m_applyOnion, tool == T::Select);
    m_form->setRowVisible(m_dropperAll, tool == T::Dropper);
    for (QWidget* const row : std::as_const(m_paintRows))
        m_form->setRowVisible(row, Canvas::isPaintTool(Canvas::Tool(tool)));
    for (QWidget* const row : std::as_const(m_inkRows))
        m_form->setRowVisible(row, tool == T::Ink);
    for (QWidget* const row : std::as_const(m_dropperRows))
        m_form->setRowVisible(row, tool == T::Dropper);
    for (QWidget* const row : std::as_const(m_zoomRows))
        m_form->setRowVisible(row, tool == T::Zoom);
    if (tool == T::Transform)
        fillTransformRows();
    for (QWidget* const row : std::as_const(m_transformRows))
        m_form->setRowVisible(row, tool == T::Transform);
    for (QWidget* const row : std::as_const(m_pivotRows))
        m_form->setRowVisible(row, tool == T::DrawingPivot);
    m_form->setRowVisible(m_byColour, tool == T::Select);
    m_form->setRowVisible(m_paintAll, tool == T::Eraser || Canvas::isPaintTool(Canvas::Tool(tool)));
    m_form->setRowVisible(m_paintLayers, Canvas::isPaintTool(Canvas::Tool(tool)));
    m_form->setRowVisible(m_permanent, tool == T::Select);
    m_form->setRowVisible(m_bothArts, tool == T::Eraser);
    m_form->setRowVisible(m_selectBothArts, tool == T::Select || tool == T::Cutter);
    m_form->setRowVisible(m_snapAlign, tool == T::Select);
    m_form->setRowVisible(m_snapMaster, tool == T::Select || contour || tool == T::Envelope || tool == T::Perspective);
    m_form->setRowVisible(m_hideManipulator, tool == T::Select);
    m_form->setRowVisible(m_ceControls, contour);
    m_form->setRowVisible(m_cePoints, contour);
    m_form->setRowVisible(m_envAdvanced, tool == T::Envelope);
    m_form->setRowVisible(m_cutterRow, tool == T::Cutter);
    m_form->setRowVisible(m_repositionRow, tool == T::Reposition);
    m_form->setRowVisible(m_pencilModeRow, pencil);
    m_form->setRowVisible(m_autoColour, brush || pencil || shape);
    m_form->setRowVisible(m_polyBehind, tool == T::Polyline);
    m_form->setRowVisible(m_eraserMinRow, eraser);
    m_form->setRowVisible(m_pencilMinRow, pencil);
    m_form->setRowVisible(m_pressureSmoothRow, brush || pencil);
    m_form->setRowVisible(m_pencilOpacityRow, pencil);
    m_form->setRowVisible(m_singleStrokeBox, tool == T::SmoothEditor || tool == T::PencilRetouch);
    m_form->setRowVisible(m_edgeType, shape);
    m_form->setRowVisible(m_overlayBox, brush || pencil);
    // The Pencil Retouch tool's size is the reach of its rub.
    if (QLabel* const label = qobject_cast<QLabel*>(m_form->labelForField(m_sizeRow)))
        label->setText(tool == T::PencilRetouch ? tr("Retouch Radius") : tr("Size"));
    m_form->setRowVisible(m_stampScale, tool == T::Stamp);
    m_form->setRowVisible(m_stampRandomSize, tool == T::Stamp);
    m_form->setRowVisible(m_stampRow, tool == T::Stamp);
    m_form->setRowVisible(m_stampRandom, tool == T::Stamp);
    m_form->setRowVisible(m_presetsTitle, draws);
    m_form->setRowVisible(m_presets, draws);
    m_preview->set(m_size->value(), brush);
    listPresets(brush, eraser, draws);
}

void ToolProperties::listPresets(bool brush, bool eraser, bool draws)
{
    struct Preset { char const* name; int size; int minSize = -1; int taper = -1; int square = -1; };
    static Preset const kBrush[] = {{QT_TR_NOOP("Solid Brush 1"), 5}, {QT_TR_NOOP("Solid Brush 2"), 10}, {QT_TR_NOOP("Solid Brush 3"), 15},
                                    {QT_TR_NOOP("Solid Brush 4"), 20}, {QT_TR_NOOP("Solid Brush 5"), 30}, {QT_TR_NOOP("Solid Brush 6"), 45},
                                    {QT_TR_NOOP("Fixed Width 1"), 6, 100, 0}, {QT_TR_NOOP("Fixed Width 2"), 14, 100, 0},
                                    {QT_TR_NOOP("Auto Taper"), 12, 0, 20}};
    static Preset const kPencil[] = {{QT_TR_NOOP("Pencil 1"), 3}, {QT_TR_NOOP("Pencil 2"), 6}, {QT_TR_NOOP("Pencil 3"), 10},
                                     {QT_TR_NOOP("Pencil 4"), 16}, {QT_TR_NOOP("Pencil 5"), 24},
                                     {QT_TR_NOOP("Fixed Thickness 1"), 2}, {QT_TR_NOOP("Fixed Thickness 2"), 4}};
    static Preset const kEraser[] = {{QT_TR_NOOP("Small Eraser"), 20, -1, -1, 0}, {QT_TR_NOOP("Medium Eraser"), 50, -1, -1, 0}, {QT_TR_NOOP("Large Eraser"), 100, -1, -1, 0},
                                     {QT_TR_NOOP("Very Large Eraser"), 160, -1, -1, 0}, {QT_TR_NOOP("Big Square Eraser"), 120, -1, -1, 1}};
    QSignalBlocker const block(m_presets);
    m_presets->clear();
    auto fill = [&](Preset const* list, int count) {
        for (int i = 0; i < count; ++i) {
            // The same picture when the row is chosen: the list would tint it otherwise.
            QPixmap const picture = strokePicture(QSize(120, 26), list[i].size, brush);
            QIcon icon;
            icon.addPixmap(picture, QIcon::Normal);
            icon.addPixmap(picture, QIcon::Selected);
            QListWidgetItem* const item = new QListWidgetItem(icon, tr("%1   (%2)").arg(tr(list[i].name)).arg(list[i].size), m_presets);
            item->setData(Qt::UserRole, list[i].size);
            item->setData(Qt::UserRole + 2, list[i].minSize);
            item->setData(Qt::UserRole + 3, list[i].taper);
            item->setData(Qt::UserRole + 5, list[i].square);
            if (list[i].size == m_size->value())
                m_presets->setCurrentItem(item);
        }
    };
    if (brush)
        fill(kBrush, int(std::size(kBrush)));
    else if (eraser)
        fill(kEraser, int(std::size(kEraser)));
    else if (draws)
        fill(kPencil, int(std::size(kPencil)));
    // Then the ones the user made for this kind of tool.
    if (draws) {
        QStringList const own = settings().value(QStringLiteral("presets/") + QLatin1String(brush ? "brush" : eraser ? "eraser" : "pencil")).toStringList();
        for (int index = 0; index < own.size(); ++index) {
            QString const& entry = own[index];
            QStringList const parts = entry.split(QLatin1Char('|'));
            int const width = parts.value(1).toInt();
            if (parts.size() < 2 || parts.size() > 3 || width < 1)
                continue;
            QPixmap const picture = strokePicture(QSize(120, 26), width, brush);
            QIcon icon;
            icon.addPixmap(picture, QIcon::Normal);
            icon.addPixmap(picture, QIcon::Selected);
            QListWidgetItem* const item = new QListWidgetItem(icon, tr("%1   (%2)").arg(parts[0]).arg(width), m_presets);
            item->setData(Qt::UserRole, width);
            if (parts.size() == 3)
                item->setData(Qt::UserRole + 1, parts[2].toInt());
            item->setData(Qt::UserRole + 4, index);       // one of the user's own: which
        }
    }
    setPresetView(settings().value(QStringLiteral("tools/presetView"), 0).toInt());
}

void ToolProperties::setPresetView(int view)
{
    // 0: small pictures of the stroke; 1: large ones; 2: the names alone; 3 and 4: swatches, pictures alone in a grid (names in their tips).
    view = std::clamp(view, 0, 4);
    settings().setValue(QStringLiteral("tools/presetView"), view);
    bool const swatches = view >= 3;
    m_presets->setViewMode(swatches ? QListView::IconMode : QListView::ListMode);
    m_presets->setMovement(QListView::Static);
    m_presets->setResizeMode(QListView::Adjust);
    m_presets->setWrapping(swatches);
    QSize const icon = view == 0 ? QSize(120, 26) : view == 1 ? QSize(180, 40) : view == 3 ? QSize(60, 26) : view == 4 ? QSize(36, 16) : QSize(0, 0);
    m_presets->setIconSize(icon);
    m_presets->setGridSize(swatches ? icon + QSize(8, 8) : QSize());
    m_presets->setProperty("presetView", view);
    for (int row = 0; row < m_presets->count(); ++row) {
        QListWidgetItem* const item = m_presets->item(row);
        if (!item->data(Qt::UserRole + 6).isValid())
            item->setData(Qt::UserRole + 6, item->text());
        item->setText(swatches ? QString() : item->data(Qt::UserRole + 6).toString());
        item->setToolTip(item->data(Qt::UserRole + 6).toString());
    }
}

bool ToolProperties::updatePreset(int index)
{
    // One of the user's own presets of the tool in hand: its size and smoothing made those now in Tool Properties.
    using T = Canvas::Tool;
    int const tool = m_canvas->tool();
    char const* const kind = tool == T::Brush ? "brush" : tool == T::Eraser ? "eraser" : "pencil";
    QStringList own = settings().value(QStringLiteral("presets/") + QLatin1String(kind)).toStringList();
    if (index < 0 || index >= own.size())
        return false;
    QString const name = own[index].section(QLatin1Char('|'), 0, 0);
    QSpinBox const* const smoothing = findChild<QSpinBox*>(tr("Smoothing"));
    own[index] = tool == T::Eraser || !smoothing ? QStringLiteral("%1|%2").arg(name).arg(m_size->value())
                                                 : QStringLiteral("%1|%2|%3").arg(name).arg(m_size->value()).arg(smoothing->value());
    settings().setValue(QStringLiteral("presets/") + QLatin1String(kind), own);
    showFor(tool);
    return true;
}
