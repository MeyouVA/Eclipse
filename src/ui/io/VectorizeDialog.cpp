// Eclipse - Vectorization Parameters.
#include "ui/io/VectorizeDialog.h"

#include "canvas/Canvas.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

QDoubleSpinBox* number(double low, double high, double step, int decimals, QWidget* parent)
{
    QDoubleSpinBox* const box = new QDoubleSpinBox(parent);
    box->setRange(low, high);
    box->setSingleStep(step);
    box->setDecimals(decimals);
    return box;
}

QPixmap shown(QImage const& image)
{
    return image.isNull() ? QPixmap() : QPixmap::fromImage(image.scaled(220, 165, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

}  // namespace

VectorizeDialog::VectorizeDialog(Scene const& scene, QString const& line, QImage const& preview, QWidget* parent)
    : QDialog(parent), m_scene(scene), m_image(preview)
{
    setObjectName(QStringLiteral("VectorizeDialog"));
    setWindowTitle(tr("Vectorization Parameters"));
    QVBoxLayout* const top = new QVBoxLayout(this);
    QFormLayout* const head = new QFormLayout;
    m_style = new QComboBox(this);
    m_style->setObjectName(QStringLiteral("VectorizeStyle"));
    m_style->addItem(tr("Custom"));
    m_style->addItems(Vectorize::styleNames());
    head->addRow(tr("Vectorization Style:"), m_style);
    top->addLayout(head);

    QHBoxLayout* const columns = new QHBoxLayout;
    top->addLayout(columns);
    QGroupBox* const settings = new QGroupBox(tr("Settings"), this);
    QFormLayout* const form = new QFormLayout(settings);
    QWidget* const modes = new QWidget(settings);
    QHBoxLayout* const modeRow = new QHBoxLayout(modes);
    modeRow->setContentsMargins(0, 0, 0, 0);
    m_blackWhite = new QRadioButton(tr("Black and White"), modes);
    m_centre = new QRadioButton(tr("Centre Lines"), modes);
    m_colour = new QRadioButton(tr("Colour"), modes);
    m_colour->setToolTip(tr("Perform a color vectorization: a painted shape for the areas of each colour."));
    m_blackWhite->setObjectName(QStringLiteral("VectorizeBlackWhite"));
    m_centre->setObjectName(QStringLiteral("VectorizeCentre"));
    m_colour->setObjectName(QStringLiteral("VectorizeColour"));
    modeRow->addWidget(m_blackWhite);
    modeRow->addWidget(m_centre);
    modeRow->addWidget(m_colour);
    form->addRow(tr("Vectorize as"), modes);
    m_twoPass = new QCheckBox(tr("Colour art too (a second pass for the tones)"), settings);
    m_twoPass->setObjectName(QStringLiteral("VectorizeTwoPass"));
    form->addRow(QString(), m_twoPass);
    m_threshLine = number(0.0, 1.0, 0.05, 2, settings);
    m_threshLine->setObjectName(QStringLiteral("VectorizeThreshold"));
    m_threshLine->setToolTip(tr("Line art is darker than this much of white (from the darkest line of the picture to its paper)."));
    form->addRow(tr("Threshold"), m_threshLine);
    m_threshColour = number(0.0, 1.0, 0.05, 2, settings);
    m_threshColour->setToolTip(tr("Colour art: tones darker than white by more than this."));
    form->addRow(tr("Colour Art Threshold"), m_threshColour);
    m_holes = number(0.0, 100000.0, 1.0, 1, settings);
    m_holes->setToolTip(tr("Holes smaller than this many pixels are filled."));
    form->addRow(tr("Remove holes smaller than"), m_holes);
    m_dirt = number(0.0, 100000.0, 1.0, 1, settings);
    m_dirt->setObjectName(QStringLiteral("VectorizeDirt"));
    m_dirt->setToolTip(tr("Specks smaller than this many pixels are left out."));
    form->addRow(tr("Remove dirt smaller than"), m_dirt);
    m_fit = number(0.0, 50.0, 0.25, 2, settings);
    m_fit->setToolTip(tr("How far (pixels) the outlines may stray from the picture to have fewer points."));
    form->addRow(tr("Fitting error"), m_fit);
    m_smooth = new QSpinBox(settings);
    m_smooth->setRange(0, 20);
    form->addRow(tr("Smooth passes"), m_smooth);
    QWidget* const leave = new QWidget(settings);
    QHBoxLayout* const leaveRow = new QHBoxLayout(leave);
    leaveRow->setContentsMargins(0, 0, 0, 0);
    m_noRed = new QCheckBox(tr("Red"), leave);
    m_noGreen = new QCheckBox(tr("Green"), leave);
    m_noBlue = new QCheckBox(tr("Blue"), leave);
    m_noBlue->setObjectName(QStringLiteral("VectorizeNoBlue"));
    leaveRow->addWidget(m_noRed);
    leaveRow->addWidget(m_noGreen);
    leaveRow->addWidget(m_noBlue);
    leave->setToolTip(tr("Lines of these colours (construction lines) are left out."));
    form->addRow(tr("Ignore lines of colour"), leave);
    m_rgb = new QCheckBox(tr("Red, green and blue lines each a shape of their colour"), settings);
    form->addRow(QString(), m_rgb);
    m_gap = number(0.0, 500.0, 1.0, 1, settings);
    m_gap->setSpecialValueText(tr("Do not close"));
    m_gap->setToolTip(tr("Gaps in the line art up to this many pixels are closed round the colour art."));
    form->addRow(tr("Close gaps up to"), m_gap);
    m_frame = new QCheckBox(tr("A frame round the colour art"), settings);
    form->addRow(QString(), m_frame);
    m_flatten = new QCheckBox(tr("Flatten the shapes of one colour"), settings);
    form->addRow(QString(), m_flatten);
    QWidget* const reg = new QWidget(settings);
    QHBoxLayout* const regRow = new QHBoxLayout(reg);
    regRow->setContentsMargins(0, 0, 0, 0);
    m_register = new QCheckBox(tr("Optical Registration"), reg);
    m_register->setObjectName(QStringLiteral("VectorizeRegister"));
    m_register->setToolTip(tr("The sheet put straight and in place by its peg holes."));
    m_pegSide = new QComboBox(reg);
    m_pegSide->addItems({tr("Top"), tr("Bottom"), tr("Left"), tr("Right")});
    m_dpi = new QSpinBox(reg);
    m_dpi->setRange(-1, 2400);
    m_dpi->setSpecialValueText(tr("Picture's own"));
    m_dpi->setSuffix(tr(" dpi"));
    regRow->addWidget(m_register);
    regRow->addWidget(m_pegSide);
    regRow->addWidget(m_dpi);
    form->addRow(tr("Pegs"), reg);
    columns->addWidget(settings, 1);

    // The preview: the picture, and what it becomes.
    QGroupBox* const previewBox = new QGroupBox(tr("Preview"), this);
    QVBoxLayout* const previewLayout = new QVBoxLayout(previewBox);
    m_before = new QLabel(previewBox);
    m_before->setMinimumSize(220, 165);
    m_before->setAlignment(Qt::AlignCenter);
    m_after = new QLabel(previewBox);
    m_after->setObjectName(QStringLiteral("VectorizePreview"));
    m_after->setMinimumSize(220, 165);
    m_after->setAlignment(Qt::AlignCenter);
    previewLayout->addWidget(m_before);
    previewLayout->addWidget(m_after);
    QHBoxLayout* const previewButtons = new QHBoxLayout;
    QPushButton* const load = new QPushButton(tr("Load Image..."), previewBox);
    QPushButton* const run = new QPushButton(tr("Vectorize Image"), previewBox);
    run->setObjectName(QStringLiteral("VectorizeRun"));
    run->setToolTip(tr("Vectorizes the visible portion of the preview image with your current settings."));
    previewButtons->addWidget(load);
    previewButtons->addWidget(run);
    previewLayout->addLayout(previewButtons);
    columns->addWidget(previewBox);

    QFormLayout* const bottom = new QFormLayout;
    m_line = new QLineEdit(this);
    m_line->setObjectName(QStringLiteral("VectorizeLine"));
    QHBoxLayout* const lineRow = new QHBoxLayout;
    lineRow->addWidget(m_line, 1);
    QPushButton* const helpButton = new QPushButton(tr("Options..."), this);
    lineRow->addWidget(helpButton);
    bottom->addRow(tr("Command Line Options"), lineRow);
    m_problem = new QLabel(this);
    m_problem->setObjectName(QStringLiteral("VectorizeProblem"));
    m_problem->setStyleSheet(QStringLiteral("color: #c0392b"));
    bottom->addRow(m_problem);
    top->addLayout(bottom);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    top->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (fromLine())
            accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // Settings and the line kept as one.
    for (QDoubleSpinBox* const box : {m_threshLine, m_threshColour, m_holes, m_dirt, m_fit, m_gap})
        connect(box, &QDoubleSpinBox::valueChanged, this, &VectorizeDialog::fromWidgets);
    connect(m_smooth, &QSpinBox::valueChanged, this, &VectorizeDialog::fromWidgets);
    connect(m_dpi, &QSpinBox::valueChanged, this, &VectorizeDialog::fromWidgets);
    connect(m_pegSide, &QComboBox::currentIndexChanged, this, &VectorizeDialog::fromWidgets);
    for (QAbstractButton* const box : std::initializer_list<QAbstractButton*>{m_blackWhite, m_centre, m_colour, m_twoPass, m_noRed, m_noGreen, m_noBlue, m_rgb, m_frame, m_flatten, m_register})
        connect(box, &QAbstractButton::toggled, this, &VectorizeDialog::fromWidgets);
    connect(m_line, &QLineEdit::editingFinished, this, &VectorizeDialog::fromLine);
    connect(m_style, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (m_filling || index <= 0)
            return;
        m_options = Vectorize::style(m_style->currentText());
        toWidgets();
    });
    connect(helpButton, &QPushButton::clicked, this, [this] { QMessageBox::information(this, tr("Vectorization Parameters"), Vectorize::help()); });
    connect(load, &QPushButton::clicked, this, [this] {
        QString const file = QFileDialog::getOpenFileName(this, tr("Load Image"), QString(), tr("Images (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.tga *.webp)"));
        if (!file.isEmpty())
            setPreviewImage(QImage(file));
    });
    connect(run, &QPushButton::clicked, this, [this] { runPreview(); });

    QString error;
    if (!Vectorize::parse(line, m_options, &error))
        m_problem->setText(error);
    for (QString const& name : Vectorize::styleNames())
        if (Vectorize::style(name) == m_options) {
            m_filling = true;
            m_style->setCurrentText(name);
            m_filling = false;
            break;
        }
    toWidgets();
    setPreviewImage(preview);
}

void VectorizeDialog::setPreviewImage(QImage const& image)
{
    m_image = image;
    m_before->setPixmap(shown(image));
    m_before->setText(image.isNull() ? tr("Please select a file before editing the vectorization settings.") : QString());
    m_before->setWordWrap(true);
    m_after->clear();
}

bool VectorizeDialog::runPreview()
{
    if (!fromLine())
        return false;
    if (m_image.isNull()) {
        m_problem->setText(tr("You must get a preview of an image before editing the vectorization settings."));
        return false;
    }
    Vectorize::Result const made = Vectorize::run(m_image, m_options);
    m_previewShapes = 0;
    for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet)
        m_previewShapes += int(made.drawing.fillsOf(sheet).size() + made.drawing.strokesOf(sheet).size());
    if (!made.ok) {
        m_problem->setText(tr("Vectorization failed.") + QLatin1Char(' ') + made.notes.join(QLatin1Char(' ')));
        return false;
    }
    QImage picture(m_image.size(), QImage::Format_ARGB32_Premultiplied);
    picture.fill(Qt::white);
    QPainter p(&picture);
    p.setRenderHint(QPainter::Antialiasing);
    Canvas::paintDrawing(p, m_scene, made.drawing, 1.0);
    p.end();
    m_after->setPixmap(shown(picture));
    if (!made.notes.isEmpty())
        m_problem->setText(made.notes.join(QLatin1Char('\n')));
    return true;
}

QString VectorizeDialog::line() const
{
    return Vectorize::toLine(m_options);
}

void VectorizeDialog::toWidgets()
{
    m_filling = true;
    Vectorize::Options const& o = m_options;
    m_blackWhite->setChecked(o.mode == Vectorize::Options::BlackAndWhite);
    m_centre->setChecked(o.mode == Vectorize::Options::CentreLines);
    m_colour->setChecked(o.mode == Vectorize::Options::Colour);
    m_twoPass->setChecked(o.twoPass);
    m_threshLine->setValue(o.threshLine);
    m_threshColour->setValue(o.threshColour);
    m_threshColour->setEnabled(o.twoPass);
    m_holes->setValue(o.holesLine);
    m_dirt->setValue(o.dirtLine);
    m_fit->setValue(o.fitLine);
    m_smooth->setValue(o.smoothLine);
    m_noRed->setChecked(o.noRed);
    m_noGreen->setChecked(o.noGreen);
    m_noBlue->setChecked(o.noBlue);
    m_rgb->setChecked(o.rgb);
    m_gap->setValue(o.closeGaps ? o.gap : 0.0);
    m_frame->setChecked(o.frame);
    m_flatten->setChecked(o.flatten);
    m_register->setChecked(o.registerPegs);
    m_pegSide->setCurrentIndex(o.pegSide);
    m_dpi->setValue(int(o.dpi));
    m_line->setText(Vectorize::toLine(o));
    m_filling = false;
}

void VectorizeDialog::fromWidgets()
{
    if (m_filling)
        return;
    Vectorize::Options& o = m_options;
    o.mode = m_centre->isChecked() ? Vectorize::Options::CentreLines : m_colour->isChecked() ? Vectorize::Options::Colour : Vectorize::Options::BlackAndWhite;
    o.twoPass = m_twoPass->isChecked();
    o.threshLine = m_threshLine->value();
    o.threshColour = m_threshColour->value();
    // (One value for line and colour art here; the line of options can set them apart.)
    if (o.holesLine != m_holes->value())
        o.holesLine = o.holesColour = m_holes->value();
    if (o.dirtLine != m_dirt->value())
        o.dirtLine = o.dirtColour = m_dirt->value();
    if (o.fitLine != m_fit->value())
        o.fitLine = o.fitColour = m_fit->value();
    if (o.smoothLine != m_smooth->value())
        o.smoothLine = o.smoothColour = m_smooth->value();
    o.noRed = m_noRed->isChecked();
    o.noGreen = m_noGreen->isChecked();
    o.noBlue = m_noBlue->isChecked();
    o.rgb = m_rgb->isChecked();
    o.closeGaps = m_gap->value() > 0.0;
    if (o.closeGaps)
        o.gap = m_gap->value();
    o.frame = m_frame->isChecked();
    o.flatten = m_flatten->isChecked();
    o.registerPegs = m_register->isChecked();
    o.pegSide = m_pegSide->currentIndex();
    o.dpi = m_dpi->value();
    m_filling = true;
    m_style->setCurrentIndex(0);
    for (QString const& name : Vectorize::styleNames())
        if (Vectorize::style(name) == o)
            m_style->setCurrentText(name);
    m_threshColour->setEnabled(o.twoPass);
    m_line->setText(Vectorize::toLine(o));
    m_filling = false;
    m_problem->clear();
}

bool VectorizeDialog::fromLine()
{
    Vectorize::Options read;
    QString error;
    QStringList ignored;
    if (!Vectorize::parse(m_line->text(), read, &error, &ignored)) {
        m_problem->setText(error);
        m_problem->setStyleSheet(QStringLiteral("color: #c0392b"));
        return false;
    }
    m_options = read;
    m_problem->setText(ignored.isEmpty() ? QString() : tr("Not used in Eclipse: %1").arg(ignored.join(QLatin1Char(' '))));
    if (!ignored.isEmpty())
        m_problem->setStyleSheet(QStringLiteral("color: #7f6000"));
    QString const keep = m_problem->text();
    toWidgets();
    m_filling = true;
    m_style->setCurrentIndex(0);
    for (QString const& name : Vectorize::styleNames())
        if (Vectorize::style(name) == m_options)
            m_style->setCurrentText(name);
    m_filling = false;
    m_problem->setText(keep);
    if (keep.isEmpty())
        m_problem->setStyleSheet(QStringLiteral("color: #c0392b"));
    return true;
}
