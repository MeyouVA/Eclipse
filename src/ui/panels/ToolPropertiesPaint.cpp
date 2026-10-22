// Eclipse - Tool Properties: the options of the Paint tools, the Ink tool and the Dropper.
#include "ui/panels/ToolProperties.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QSpinBox>

#include "actions/theme/AppSettings.h"

void ToolProperties::addPaintRows()
{
    // A tick box kept between runs (tools/<key>), set on the canvas when changed.
    auto box = [this](QVector<QWidget*>& rows, char const* name, QString const& text, QString const& tip, QString const& key, bool usual,
                      std::function<void(bool)> apply) {
        QCheckBox* const check = new QCheckBox(text, this);
        check->setObjectName(QLatin1String(name));
        check->setToolTip(tip);
        bool const on = settings().value(QStringLiteral("tools/") + key, usual).toBool();
        check->setChecked(on);
        apply(on);
        connect(check, &QCheckBox::toggled, this, [apply, key](bool now) {
            apply(now);
            settings().setValue(QStringLiteral("tools/") + key, now);
        });
        m_form->addRow(check);
        rows.push_back(check);
    };
    box(m_paintRows, "PaintSelectNew", tr("Select Newly Painted Zones"), tr("Keeps a selection round the latest painted zone."), QStringLiteral("paintSelectNew"), false,
        [this](bool on) { m_canvas->setPaintSelectNew(on); });
    box(m_paintRows, "PaintOverTexture", tr("Paint and Remove Texture"), tr("Painting a textured zone makes it one plain colour."), QStringLiteral("paintOverTexture"), false,
        [this](bool on) { m_canvas->setPaintOverTexture(on); });
    box(m_paintRows, "PaintStoredGradient", tr("Use Stored Colour Gradient"),
        tr("A gradient colour is laid as the gradient last placed with Edit Gradient, so every new zone has the same gradient position."),
        QStringLiteral("paintStoredGradient"), false, [this](bool on) { m_canvas->setUseStoredGradient(on); });

    // The bucket on bitmap art.
    QLabel* const heading = new QLabel(QStringLiteral("<b>%1</b>").arg(tr("Bitmap Options")), this);
    m_form->addRow(heading);
    m_paintRows.push_back(heading);
    QComboBox* const source = new QComboBox(this);
    source->setObjectName(QStringLiteral("BitmapFillSource"));
    source->addItems({tr("This art layer"), tr("Every art layer")});
    source->setToolTip(tr("What the bucket looks at to find where to fill on bitmap art: the art layer painted, or every art layer of the drawing."));
    source->setCurrentIndex(std::clamp(settings().value(QStringLiteral("tools/bitmapSource"), 0).toInt(), 0, 1));
    m_canvas->setBitmapSource(source->currentIndex());
    connect(source, &QComboBox::currentIndexChanged, this, [this](int at) {
        m_canvas->setBitmapSource(at);
        settings().setValue(QStringLiteral("tools/bitmapSource"), at);
    });
    auto spin = [this](char const* name, QString const& label, QString const& tip, QString const& key, int low, int high, int usual, std::function<void(int)> apply) {
        QSpinBox* const box = new QSpinBox(this);
        box->setObjectName(QLatin1String(name));
        box->setRange(low, high);
        box->setToolTip(tip);
        box->setValue(std::clamp(settings().value(QStringLiteral("tools/") + key, usual).toInt(), low, high));
        apply(box->value());
        connect(box, &QSpinBox::valueChanged, this, [apply, key](int now) {
            apply(now);
            settings().setValue(QStringLiteral("tools/") + key, now);
        });
        QLabel* const text = new QLabel(label, this);
        m_form->addRow(text, box);
        m_paintRows.push_back(text);
        m_paintRows.push_back(box);
    };
    QLabel* const sourceText = new QLabel(tr("Source"), this);
    m_form->addRow(sourceText, source);
    m_paintRows.push_back(sourceText);
    m_paintRows.push_back(source);
    spin("BitmapTolerance", tr("Colour Tolerance"), tr("How far a pixel's colour may be from the one clicked and still be filled (0: the same colour only)."),
         QStringLiteral("bitmapTolerance"), 0, 255, 40, [this](int t) { m_canvas->setBitmapTolerance(t); });
    spin("BitmapOverlap", tr("Maximum Overlap"), tr("How many pixels the fill reaches under the edge of what bounds it, so no pale rim is left."),
         QStringLiteral("bitmapOverlap"), 0, 16, 0, [this](int pixels) { m_canvas->setBitmapOverlap(pixels); });

    box(m_inkRows, "InkShowInkable", tr("Show Inkable Lines"), tr("The lines the Ink tool would change are traced over."), QStringLiteral("inkShowInkable"), false,
        [this](bool on) { m_canvas->setInkShowInkable(on); });
    box(m_inkRows, "InkSelectNew", tr("Select Newly Inked Lines"), tr("Keeps the lines just inked selected."), QStringLiteral("inkSelectNew"), false,
        [this](bool on) { m_canvas->setInkSelectNew(on); });
    box(m_inkRows, "InkSmart", tr("Be Smart on Connecting Lines"), tr("Lines joined end to end with the one inked are inked with it."), QStringLiteral("inkSmart"), false,
        [this](bool on) { m_canvas->setInkSmart(on); });
    box(m_inkRows, "InkSelectionMode", tr("Use Selection Mode"), tr("Ink the selected lines instead of those under the pen. Press Ctrl to toggle."),
        QStringLiteral("inkSelectionMode"), false, [this](bool on) { m_canvas->setInkSelectionMode(on); });
    box(m_inkRows, "InkJoinChange", tr("Change Inked Lines Join Type"), tr("Inked lines take the join chosen for new lines."), QStringLiteral("inkJoinChange"), false,
        [this](bool on) { m_canvas->setInkJoinChange(on); });
    QComboBox* const order = new QComboBox(this);
    order->setObjectName(QStringLiteral("InkOrder"));
    order->addItems({tr("Ink lines where they are"), tr("Bring ink lines on top"), tr("Send ink lines under")});
    order->setToolTip(tr("Where inked lines go among the others. Press Alt to toggle on top and under."));
    order->setCurrentIndex(std::clamp(settings().value(QStringLiteral("tools/inkOrder"), 0).toInt(), 0, 2));
    m_canvas->setInkOrder(order->currentIndex());
    connect(order, &QComboBox::currentIndexChanged, this, [this](int at) {
        m_canvas->setInkOrder(at);
        settings().setValue(QStringLiteral("tools/inkOrder"), at);
    });
    m_form->addRow(order);
    m_inkRows.push_back(order);

    box(m_dropperRows, "DropperNoTransparency", tr("Do Not Pick Transparency"), tr("Lines and zones that cannot be seen are passed over."),
        QStringLiteral("dropperNoTransparency"), false, [this](bool on) { m_canvas->setDropperNoTransparency(on); });
}
