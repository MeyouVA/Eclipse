// Eclipse - the Colour Editor.
#include "ui/panels/ColourEditor.h"
#include "ui/panels/ColourWheel.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "actions/theme/AppSettings.h"
#include "model/document/Commands.h"

ColourEditor::ColourEditor(Document* document, QWidget* parent) : QDialog(parent), m_document(document)
{
    setObjectName(QStringLiteral("ColourEditor"));
    setWindowTitle(document ? tr("Colour Editor") : tr("Colour Picker"));
    m_hsv = settings().value(QStringLiteral("colour/hsvSliders"), true).toBool();
    m_settle = new QTimer(this);
    m_settle->setSingleShot(true);
    m_settle->setInterval(600);
    connect(m_settle, &QTimer::timeout, this, &ColourEditor::finishEdit);
    build();
    setMultiwheel(settings().value(QStringLiteral("colour/multiwheel"), false).toBool());
    setHsvSliders(m_hsv);
    if (m_document) {
        // Undone or changed elsewhere: the editor shows what the colour is now.
        connect(m_document, &Document::changed, this, [this] {
            if (m_editOpen || m_swatch < 0)
                return;
            if (Swatch const* const s = m_document->scene().findSwatch(m_swatch)) {
                m_colour = s->color;
                if (!m_name->hasFocus())
                    m_name->setText(s->name);
                showColour();
            } else {
                setSwatch(-1);
            }
        });
    }
    showColour();
}

ColourEditor::~ColourEditor()
{
    finishEdit();
}

void ColourEditor::build()
{
    QVBoxLayout* const column = new QVBoxLayout(this);
    // The name and the colour as it was beside the colour as it is.
    QHBoxLayout* const top = new QHBoxLayout;
    m_name = new QLineEdit(this);
    m_name->setObjectName(QStringLiteral("ColourEditorName"));
    m_name->setPlaceholderText(tr("Colour name"));
    m_name->setVisible(m_document != nullptr);
    connect(m_name, &QLineEdit::editingFinished, this, [this] {
        Swatch const* const s = m_document ? m_document->scene().findSwatch(m_swatch) : nullptr;
        QString const name = m_name->text().trimmed();
        if (s && !name.isEmpty() && name != s->name)
            m_document->editSwatch(m_swatch, name, s->color);
    });
    m_preview = new QLabel(this);
    m_preview->setObjectName(QStringLiteral("ColourEditorPreview"));
    m_preview->setFixedSize(64, 24);
    m_preview->setToolTip(tr("Left: the colour as it was. Right: the colour as it is."));
    top->addWidget(m_name, 1);
    top->addWidget(m_preview);
    column->addLayout(top);

    // The wheel, and beside it the value (for the disc, which has no place for it).
    QHBoxLayout* const wheelRow = new QHBoxLayout;
    m_wheel = new ColourWheel(this);
    m_value = new QSlider(Qt::Vertical, this);
    m_value->setObjectName(QStringLiteral("ColourEditorValue"));
    m_value->setRange(0, 255);
    m_value->setToolTip(tr("Value: how light the colour is"));
    wheelRow->addWidget(m_wheel, 1);
    wheelRow->addWidget(m_value);
    column->addLayout(wheelRow, 1);
    connect(m_wheel, &ColourWheel::colourChanged, this, [this](QColor const& c) {
        QColor kept = c;
        kept.setAlpha(m_colour.alpha());
        give(kept);
    });
    connect(m_wheel, &ColourWheel::editingFinished, this, &ColourEditor::finishEdit);
    connect(m_value, &QSlider::valueChanged, this, [this](int v) {
        if (m_showing)
            return;
        float h = 0, s = 0, old = 0, a = 1;
        m_colour.getHsvF(&h, &s, &old, &a);
        give(QColor::fromHsvF(h < 0 ? 0 : h, s, v / 255.0f, a));
    });
    connect(m_value, &QSlider::sliderReleased, this, &ColourEditor::finishEdit);

    // The ways to look at it.
    QHBoxLayout* const modes = new QHBoxLayout;
    m_multiButton = new QToolButton(this);
    m_multiButton->setObjectName(QStringLiteral("ColourEditorMultiwheel"));
    m_multiButton->setText(tr("Multiwheel"));
    m_multiButton->setCheckable(true);
    m_multiButton->setToolTip(tr("A ring of hues round a square of saturation and value, instead of the disc"));
    connect(m_multiButton, &QToolButton::toggled, this, [this](bool on) {
        setMultiwheel(on);
        settings().setValue(QStringLiteral("colour/multiwheel"), on);
    });
    m_hsvButton = new QToolButton(this);
    m_hsvButton->setObjectName(QStringLiteral("ColourEditorHsv"));
    m_hsvButton->setText(tr("HSV"));
    m_hsvButton->setCheckable(true);
    m_hsvButton->setToolTip(tr("Sliders for hue, saturation and value (off: red, green and blue)"));
    connect(m_hsvButton, &QToolButton::toggled, this, [this](bool on) {
        setHsvSliders(on);
        settings().setValue(QStringLiteral("colour/hsvSliders"), on);
    });
    modes->addWidget(m_multiButton);
    modes->addWidget(m_hsvButton);
    modes->addStretch();
    column->addLayout(modes);

    // Three sliders (RGB or HSV) and the alpha, each with its number.
    QGridLayout* const grid = new QGridLayout;
    char const* const names[4] = {"ColourEditorSlider0", "ColourEditorSlider1", "ColourEditorSlider2", "ColourEditorAlpha"};
    for (int i = 0; i < 4; ++i) {
        QLabel* const label = new QLabel(i == 3 ? tr("Alpha") : QString(), this);
        if (i < 3)
            m_names[i] = label;
        m_sliders[i] = new QSlider(Qt::Horizontal, this);
        m_sliders[i]->setObjectName(QLatin1String(names[i]));
        m_spins[i] = new QSpinBox(this);
        m_spins[i]->setObjectName(QLatin1String(names[i]) + QStringLiteral("Number"));
        grid->addWidget(label, i, 0);
        grid->addWidget(m_sliders[i], i, 1);
        grid->addWidget(m_spins[i], i, 2);
        connect(m_sliders[i], &QSlider::valueChanged, this, [this, i](int v) {
            if (m_showing)
                return;
            QSignalBlocker const block(m_spins[i]);
            m_spins[i]->setValue(v);
            fromSliders();
        });
        connect(m_sliders[i], &QSlider::sliderReleased, this, &ColourEditor::finishEdit);
        connect(m_spins[i], &QSpinBox::valueChanged, this, [this, i](int v) {
            if (m_showing)
                return;
            QSignalBlocker const block(m_sliders[i]);
            m_sliders[i]->setValue(v);
            fromSliders();
        });
    }
    column->addLayout(grid);

    // The colour as text: #RRGGBB, or #AARRGGBB.
    QHBoxLayout* const hexRow = new QHBoxLayout;
    hexRow->addWidget(new QLabel(tr("Hex"), this));
    m_hex = new QLineEdit(this);
    m_hex->setObjectName(QStringLiteral("ColourEditorHex"));
    m_hex->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("#?([0-9A-Fa-f]{6}|[0-9A-Fa-f]{8})")), m_hex));
    connect(m_hex, &QLineEdit::editingFinished, this, [this] {
        QString text = m_hex->text().trimmed();
        if (!text.startsWith(QLatin1Char('#')))
            text.prepend(QLatin1Char('#'));
        QColor const c(text);
        if (c.isValid() && c != m_colour) {
            give(c);
            finishEdit();
        }
    });
    hexRow->addWidget(m_hex, 1);
    column->addLayout(hexRow);

    // Tint (a colour of a palette only): the colour mixed toward another, by blending or by an offset.
    m_tintBox = new QWidget(this);
    QHBoxLayout* const tint = new QHBoxLayout(m_tintBox);
    tint->setContentsMargins(0, 0, 0, 0);
    tint->addWidget(new QLabel(tr("Tint"), m_tintBox));
    QPushButton* const tintColour = new QPushButton(m_tintBox);
    tintColour->setObjectName(QStringLiteral("ColourEditorTintColour"));
    tintColour->setFixedWidth(40);
    tintColour->setProperty("tint", QColor(Qt::white));
    tintColour->setStyleSheet(QStringLiteral("background: #ffffff;"));
    connect(tintColour, &QPushButton::clicked, this, [this, tintColour] {
        QColor const chosen = getColour(tintColour->property("tint").value<QColor>(), this, tr("Tint Colour"));
        if (chosen.isValid()) {
            tintColour->setProperty("tint", chosen);
            tintColour->setStyleSheet(QStringLiteral("background: %1;").arg(chosen.name()));
        }
    });
    QComboBox* const how = new QComboBox(m_tintBox);
    how->setObjectName(QStringLiteral("ColourEditorTintMode"));
    how->addItems({tr("Blend"), tr("Offset")});
    QSpinBox* const amount = new QSpinBox(m_tintBox);
    amount->setObjectName(QStringLiteral("ColourEditorTintAmount"));
    amount->setRange(-100, 100);
    amount->setValue(20);
    amount->setSuffix(QStringLiteral(" %"));
    QPushButton* const apply = new QPushButton(tr("Apply"), m_tintBox);
    apply->setObjectName(QStringLiteral("ColourEditorTintApply"));
    QPushButton* const reset = new QPushButton(tr("Reset"), m_tintBox);
    reset->setObjectName(QStringLiteral("ColourEditorReset"));
    reset->setToolTip(tr("The colour back as it was when the editor took it"));
    connect(apply, &QPushButton::clicked, this, [this, tintColour, how, amount] {
        finishEdit();
        if (m_document && m_swatch >= 0)
            m_document->tintColours({m_swatch}, tintColour->property("tint").value<QColor>(), amount->value() / 100.0, how->currentIndex() == 1);
    });
    connect(reset, &QPushButton::clicked, this, [this] {
        if (m_colour != m_was) {
            give(m_was);
            finishEdit();
        }
    });
    tint->addWidget(tintColour);
    tint->addWidget(how);
    tint->addWidget(amount);
    tint->addWidget(apply);
    tint->addWidget(reset);
    m_tintBox->setVisible(m_document != nullptr);
    column->addWidget(m_tintBox);

    m_note = new QLabel(this);
    m_note->setObjectName(QStringLiteral("ColourEditorNote"));
    m_note->setWordWrap(true);
    m_note->hide();
    column->addWidget(m_note);

    QDialogButtonBox* const buttons = new QDialogButtonBox(m_document ? QDialogButtonBox::Close : QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    column->addWidget(buttons);
    resize(300, 520);
}

bool ColourEditor::multiwheel() const
{
    return m_wheel->multiwheel();
}

void ColourEditor::setMultiwheel(bool on)
{
    m_wheel->setMultiwheel(on);
    m_value->setVisible(!on);
    QSignalBlocker const block(m_multiButton);
    m_multiButton->setChecked(on);
}

void ColourEditor::setHsvSliders(bool on)
{
    m_hsv = on;
    QSignalBlocker const block(m_hsvButton);
    m_hsvButton->setChecked(on);
    QStringList const labels = on ? QStringList{tr("Hue"), tr("Saturation"), tr("Value")} : QStringList{tr("Red"), tr("Green"), tr("Blue")};
    for (int i = 0; i < 3; ++i) {
        m_names[i]->setText(labels[i]);
        int const top = on && i == 0 ? 359 : 255;
        m_sliders[i]->setRange(0, top);
        m_spins[i]->setRange(0, top);
    }
    m_sliders[3]->setRange(0, 255);
    m_spins[3]->setRange(0, 255);
    showColour();
}

void ColourEditor::setSwatch(int id)
{
    finishEdit();
    Swatch const* const s = m_document ? m_document->scene().findSwatch(id) : nullptr;
    m_swatch = s ? id : -1;
    m_tintBox->setEnabled(s != nullptr);
    m_name->setEnabled(s != nullptr);
    m_name->setText(s ? s->name : QString());
    if (s) {
        m_colour = s->color;
        m_was = s->color;
    }
    bool const plain = !s || (!s->isGradient() && !s->isTexture());
    m_note->setVisible(!s || !plain);
    m_note->setText(!s ? tr("Current Colour. This colour is not stored in a palette.")
                       : s->isGradient() ? tr("A gradient: what is set here is its first colour. Colours > Edit Gradient... changes the gradient.")
                                         : tr("A texture: what is set here is the colour it stands for. Colours > Edit Texture changes its picture."));
    showColour();
}

void ColourEditor::setColour(QColor const& colour)
{
    if (colour.isValid())
        give(colour);
}

void ColourEditor::showColour()
{
    m_showing = true;
    m_wheel->setColour(m_colour);
    float h = 0, s = 0, v = 0, a = 1;
    m_colour.getHsvF(&h, &s, &v, &a);
    m_value->setValue(int(std::round(v * 255)));
    int const values[3] = {m_hsv ? (h < 0 ? 0 : int(std::round(h * 359))) : m_colour.red(), m_hsv ? int(std::round(s * 255)) : m_colour.green(),
                           m_hsv ? int(std::round(v * 255)) : m_colour.blue()};
    for (int i = 0; i < 3; ++i) {
        m_sliders[i]->setValue(values[i]);
        m_spins[i]->setValue(values[i]);
    }
    m_sliders[3]->setValue(m_colour.alpha());
    m_spins[3]->setValue(m_colour.alpha());
    if (!m_hex->hasFocus())
        m_hex->setText(m_colour.alpha() == 255 ? m_colour.name(QColor::HexRgb).toUpper() : m_colour.name(QColor::HexArgb).toUpper());
    m_preview->setStyleSheet(QStringLiteral("background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 %1, stop:0.5 %1, stop:0.5001 %2, stop:1 %2);"
                                            " border: 1px solid #101010;")
                                 .arg(m_was.name(QColor::HexArgb), m_colour.name(QColor::HexArgb)));
    m_showing = false;
}

void ColourEditor::fromSliders()
{
    int const a = m_sliders[3]->value();
    QColor const c = m_hsv ? QColor::fromHsv(std::clamp(m_sliders[0]->value(), 0, 359), m_sliders[1]->value(), m_sliders[2]->value(), a)
                           : QColor(m_sliders[0]->value(), m_sliders[1]->value(), m_sliders[2]->value(), a);
    give(c);
}

void ColourEditor::give(QColor const& colour)
{
    m_colour = colour;
    showColour();
    emit colourChanged(colour);
    Swatch const* const s = m_document ? m_document->scene().findSwatch(m_swatch) : nullptr;
    if (!s || s->color == colour)
        return;
    // A run of changes is one step to undo: it ends when the slider or wheel is let go, or after a pause.
    if (!m_editOpen) {
        m_document->beginMacro(tr("Edit Colour"));
        m_editOpen = true;
    }
    m_document->editSwatch(m_swatch, s->name, colour);
    m_settle->start();
}

void ColourEditor::finishEdit()
{
    m_settle->stop();
    if (!m_editOpen)
        return;
    m_editOpen = false;
    m_document->endMacro();
}

void ColourEditor::hideEvent(QHideEvent* event)
{
    finishEdit();
    QDialog::hideEvent(event);
}

QColor ColourEditor::getColour(QColor const& initial, QWidget* parent, QString const& title)
{
    ColourEditor editor(nullptr, parent);
    editor.setWindowTitle(title);
    editor.m_was = initial.isValid() ? initial : QColor(Qt::black);
    editor.m_colour = editor.m_was;
    editor.showColour();
    return editor.exec() == QDialog::Accepted ? editor.colour() : QColor();
}
