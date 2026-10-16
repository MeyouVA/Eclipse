// Eclipse - Tool Properties: how the form is put together, which rows a tool shows, and the sizes to pick from.
#include "ui/panels/ToolProperties.h"

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
#include "ui/canvas/Canvas.h"
#include "ui/window/MainWindowPrivate.h"
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QComboBox>
#include <QSettings>

// A stroke of a given thickness on a light card: what the tool in hand will draw.
QPixmap ToolProperties::strokePicture(QSize size, double thickness, bool taper)
{
    QPixmap pixmap(size);
    pixmap.fill(QColor(228, 228, 226));
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
        p.fillRect(tip, QColor(228, 228, 226));
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
    // What the tool in hand is called, a picture of what it draws, and - under the
    // settings - sizes to pick from.  Only the settings the tool has are shown.
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
    // Room for a handful of them: when the view is too small for that, the view scrolls -
    // the list is not squeezed down to a row or two.
    m_presets->setMinimumHeight(150);
    m_form->addRow(m_presetsTitle);
    m_form->addRow(m_presets);
    connect(m_presets, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        m_size->setValue(item->data(Qt::UserRole).toInt());
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
                                      "ToolEditGradient", "ToolReposition", "ToolStamp", "ToolPencilEditor", "ToolPerspective", "ToolInk", "ToolSmoothEditor", "ToolEnvelope", "ToolMorphing", "ToolRepaintBrush", "ToolSplineOffset", "ToolTurn3D", "ToolPencilRetouch"};
    QAction* const action = m_actions->action(QLatin1String(ids[tool]));
    QString const name = action ? action->text().remove(QLatin1Char('&')) : tr("Tool");
    bool const any = draws || lines || text || tool == T::Morphing || tool == T::Select || tool == T::Cutter || tool == T::Rigging || tool == T::Dropper || tool == T::Stamp || tool == T::Envelope || tool == T::Eraser || Canvas::isPaintTool(Canvas::Tool(tool));
    m_form->setRowVisible(m_rigMode, tool == T::Rigging);
    m_form->setRowVisible(m_morphLayers, tool == T::Morphing);
    m_form->setRowVisible(m_morphButtons, tool == T::Morphing);
    if (tool == T::Morphing)
        refreshMorphLayers();
    for (QWidget* const row : {static_cast<QWidget*>(m_weightBone), static_cast<QWidget*>(m_weightSize), static_cast<QWidget*>(m_weightClear)})
        m_form->setRowVisible(row, tool == T::Rigging && m_rigMode->currentIndex() == Canvas::RigWeights);
    m_form->setRowVisible(m_envelopePoints, tool == T::Rigging);
    m_form->setRowVisible(m_envelopeOutline, tool == T::Rigging);
    // What the settings are headed with: the tools that have a tip to set up are headed
    // "Properties", the others "Options".
    QString const heading = brush ? tr("Vector Brush Properties") : eraser ? tr("Vector Eraser Properties") : pencil ? tr("Pencil Properties")
                            : shape ? tr("%1 Tool Properties").arg(name) : Canvas::isPaintTool(Canvas::Tool(tool)) ? tr("Paint Tool Options")
                            : contour ? tr("Contour Editor Options") : tr("%1 Tool Options").arg(name.endsWith(tr(" Tool")) ? name.chopped(tr(" Tool").size()) : name);
    m_toolTitle->setText(any ? heading : QString());
    m_noOptions->setText(tr("No options for %1.").arg(name));
    m_form->setRowVisible(m_toolTitle, any);
    m_form->setRowVisible(m_noOptions, !any);
    m_form->setRowVisible(m_preview, draws);
    m_form->setRowVisible(m_sizeRow, draws);
    m_form->setRowVisible(m_smoothingRow, brush || pencil);
    m_form->setRowVisible(m_stabiliserRow, brush || pencil);
    m_form->setRowVisible(m_lineModeRow, tool == T::Line);
    m_form->setRowVisible(m_stencilRow, pencil);
    m_form->setRowVisible(m_retouchRow, tool == T::PencilRetouch);
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
    m_form->setRowVisible(m_dropperAll, tool == T::Dropper);
    m_form->setRowVisible(m_byColour, tool == T::Select);
    m_form->setRowVisible(m_paintAll, tool == T::Eraser || Canvas::isPaintTool(Canvas::Tool(tool)));
    m_form->setRowVisible(m_paintLayers, Canvas::isPaintTool(Canvas::Tool(tool)));
    m_form->setRowVisible(m_permanent, tool == T::Select);
    m_form->setRowVisible(m_bothArts, tool == T::Eraser);
    m_form->setRowVisible(m_selectBothArts, tool == T::Select || tool == T::Cutter);
    m_form->setRowVisible(m_snapAlign, tool == T::Select);
    m_form->setRowVisible(m_stampScale, tool == T::Stamp);
    m_form->setRowVisible(m_stampRow, tool == T::Stamp);
    m_form->setRowVisible(m_stampRandom, tool == T::Stamp);
    m_form->setRowVisible(m_presetsTitle, draws);
    m_form->setRowVisible(m_presets, draws);
    m_preview->set(m_size->value(), brush);
    listPresets(brush, eraser, draws);
}

void ToolProperties::listPresets(bool brush, bool eraser, bool draws)
{
    // The sizes of this tool.
    struct Preset { char const* name; int size; };
    static Preset const kBrush[] = {{QT_TR_NOOP("Solid Brush 1"), 5}, {QT_TR_NOOP("Solid Brush 2"), 10}, {QT_TR_NOOP("Solid Brush 3"), 15},
                                    {QT_TR_NOOP("Solid Brush 4"), 20}, {QT_TR_NOOP("Solid Brush 5"), 30}, {QT_TR_NOOP("Solid Brush 6"), 45}};
    static Preset const kPencil[] = {{QT_TR_NOOP("Pencil 1"), 3}, {QT_TR_NOOP("Pencil 2"), 6}, {QT_TR_NOOP("Pencil 3"), 10},
                                     {QT_TR_NOOP("Pencil 4"), 16}, {QT_TR_NOOP("Pencil 5"), 24}};
    static Preset const kEraser[] = {{QT_TR_NOOP("Small Eraser"), 20}, {QT_TR_NOOP("Medium Eraser"), 50}, {QT_TR_NOOP("Large Eraser"), 100},
                                     {QT_TR_NOOP("Very Large Eraser"), 160}};
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
        for (QString const& entry : own) {
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
        }
    }
}
