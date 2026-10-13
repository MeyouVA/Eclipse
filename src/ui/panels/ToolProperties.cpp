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
#include "ui/canvas/Canvas.h"
#include "ui/window/MainWindowPrivate.h"
#include <QCheckBox>
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
    bool const draws = brush || pencil || eraser || shape || tool == T::SmoothEditor;      // (the Smooth Editor has a size too)
    bool const lines = pencil || shape || tool == T::Select || tool == T::ContourEditor;
    bool const text = tool == T::Text, contour = tool == T::ContourEditor;
    static char const* const ids[] = {"ToolSelect", "ToolTransform", "ToolBrush",  "ToolEraser",    "ToolPaint",  "ToolHand",
                                      "ToolPencil", "ToolLine",      "ToolRectangle", "ToolEllipse", "ToolContourEditor",
                                      "ToolRigging", "ToolIK", "ToolDropper", "ToolZoom", "ToolPolyline",
                                      "ToolPaintUnpainted", "ToolRepaint", "ToolUnpaint", "ToolText", "ToolRotateView", "ToolCutter", "ToolStroke", "ToolCloseGap",
                                      "ToolEditGradient", "ToolReposition", "ToolStamp", "ToolPencilEditor", "ToolPerspective", "ToolInk", "ToolSmoothEditor", "ToolEnvelope"};
    QAction* const action = m_actions->action(QLatin1String(ids[tool]));
    QString const name = action ? action->text().remove(QLatin1Char('&')) : tr("Tool");
    bool const any = draws || lines || text || tool == T::Select || tool == T::Cutter || tool == T::Rigging || tool == T::Dropper || tool == T::Stamp || tool == T::Envelope || Canvas::isPaintTool(Canvas::Tool(tool));
    m_form->setRowVisible(m_rigMode, tool == T::Rigging);
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
    m_form->setRowVisible(m_minSizeRow, brush);
    m_form->setRowVisible(m_envToolRow, tool == T::Envelope);
    m_form->setRowVisible(m_gapRow, tool == T::Paint || tool == T::PaintUnpainted);
    m_form->setRowVisible(m_cornersRow, brush || pencil);
    m_form->setRowVisible(m_brushShape, brush);
    m_form->setRowVisible(m_autoFlatten, brush);
    m_form->setRowVisible(m_drawBehind, brush);
    m_drawBehind->setChecked(m_canvas->drawBehind());
    m_autoFlatten->setChecked(m_canvas->autoFlatten());
    m_autoFlatten->setEnabled(m_canvas->brushShapes());
    m_brushShape->setChecked(m_canvas->brushShapes());
    m_form->setRowVisible(static_cast<QLayout*>(m_shapeRow), lines);
    m_form->setRowVisible(m_thicknessRow, contour);
    m_form->setRowVisible(m_selection, tool == T::Select || tool == T::Cutter || Canvas::isPaintTool(Canvas::Tool(tool)));
    m_form->setRowVisible(m_fontRow, text);
    m_form->setRowVisible(m_textRow, text);
    m_form->setRowVisible(m_paraRow, text);
    m_form->setRowVisible(m_applyAll, tool == T::Select);
    m_form->setRowVisible(m_dropperAll, tool == T::Dropper);
    m_form->setRowVisible(m_byColour, tool == T::Select);
    m_form->setRowVisible(m_paintAll, Canvas::isPaintTool(Canvas::Tool(tool)));
    m_form->setRowVisible(m_permanent, tool == T::Select);
    m_form->setRowVisible(m_snapAlign, tool == T::Select);
    m_form->setRowVisible(m_stampScale, tool == T::Stamp);
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
