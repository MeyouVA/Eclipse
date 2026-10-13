// Eclipse - Tool Properties: what the Text tool writes in - font, size, how the lines stand.
#include "ui/panels/ToolProperties.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFontComboBox>
#include <QHBoxLayout>
#include <QSpinBox>

#include "ui/canvas/Canvas.h"
#include <QFormLayout>

void ToolProperties::addTextRows()
{
    QFontComboBox* const family = new QFontComboBox(this);
    family->setObjectName(QStringLiteral("TextFont"));
    QSpinBox* const size = new QSpinBox(this);
    size->setObjectName(QStringLiteral("TextSize"));
    size->setRange(4, 2000);
    size->setValue(72);
    QCheckBox* const bold = new QCheckBox(tr("Bold"), this);
    bold->setObjectName(QStringLiteral("TextBold"));
    QCheckBox* const italic = new QCheckBox(tr("Italic"), this);
    italic->setObjectName(QStringLiteral("TextItalic"));
    auto apply = [this, family, size, bold, italic] {
        QFont font = family->currentFont();
        font.setPixelSize(size->value());
        font.setBold(bold->isChecked());
        font.setItalic(italic->isChecked());
        m_canvas->setTextFont(font);
    };
    connect(family, &QFontComboBox::currentFontChanged, this, apply);
    connect(size, &QSpinBox::valueChanged, this, apply);
    connect(bold, &QCheckBox::toggled, this, apply);
    connect(italic, &QCheckBox::toggled, this, apply);
    apply();
    // Words taken up again with the Text tool: their font is the one shown, to change it from.
    connect(m_canvas, &Canvas::textFontPicked, this, [family, size, bold, italic](QFont const& font) {
        family->setCurrentFont(QFont(font.family()));
        size->setValue(font.pixelSize());
        bold->setChecked(font.bold());
        italic->setChecked(font.italic());
    });
    QHBoxLayout* const row = new QHBoxLayout;
    row->addWidget(size);
    row->addWidget(bold);
    row->addWidget(italic);
    row->addStretch(1);
    m_form->addRow(tr("Font"), family);
    m_form->addRow(tr("Text"), row);
    m_fontRow = family;
    m_textRow = row;
    // How the lines stand, and how far apart.
    QComboBox* const align = new QComboBox(this);
    align->setObjectName(QStringLiteral("TextAlign"));
    align->addItems({tr("Left"), tr("Centre"), tr("Right"), tr("Justify")});
    align->setToolTip(tr("Whether each line of words starts where you clicked, is centred on it, or ends there.\nIn the entry, Shift+Enter starts a new line."));
    QSpinBox* const spacing = new QSpinBox(this);
    spacing->setObjectName(QStringLiteral("TextLineSpacing"));
    spacing->setRange(20, 1000);
    spacing->setValue(100);
    spacing->setSuffix(QStringLiteral(" %"));
    spacing->setToolTip(tr("How far apart the lines of words are: 100 % is what the font gives."));
    connect(align, &QComboBox::currentIndexChanged, this, [this](int index) { m_canvas->setTextAlign(index); });
    connect(spacing, &QSpinBox::valueChanged, this, [this](int percent) { m_canvas->setTextLineSpacing(percent); });
    QSpinBox* const kerning = new QSpinBox(this);
    kerning->setObjectName(QStringLiteral("TextKerning"));
    kerning->setRange(50, 400);
    kerning->setValue(100);
    kerning->setSuffix(QStringLiteral(" %"));
    kerning->setToolTip(tr("How far apart the letters stand: 100 % is what the font gives."));
    connect(kerning, &QSpinBox::valueChanged, this, [this](int percent) { m_canvas->setTextKerning(percent); });
    connect(m_canvas, &Canvas::textLayoutPicked, this, [align, spacing, kerning](int a, int percent, int letters) {
        align->setCurrentIndex(a);
        spacing->setValue(percent);
        kerning->setValue(letters);
    });
    QHBoxLayout* const para = new QHBoxLayout;
    para->addWidget(align);
    para->addWidget(spacing);
    para->addWidget(kerning);
    para->addStretch(1);
    m_form->addRow(tr("Lines"), para);
    m_paraRow = para;
}
