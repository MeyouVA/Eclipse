// Eclipse - File > Export > SWF: where to, which frames, how, and what to leave out; then what was left out, told.
#include "ui/window/MainWindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QVBoxLayout>

#include "actions/theme/AppSettings.h"
#include "formats/flash/SwfExport.h"
#include "model/document/Commands.h"

void MainWindow::onActionExportSwf()
{
    Scene const& scene = m_document->scene();
    QSettings kept = settings();
    kept.beginGroup(QStringLiteral("swfExport"));
    QDialog ask(this);
    ask.setWindowTitle(tr("Export to Flash Movie (SWF)"));
    auto* column = new QVBoxLayout(&ask);

    auto* outputBox = new QGroupBox(tr("Output"), &ask);
    auto* outputRow = new QHBoxLayout(outputBox);
    auto* output = new QLineEdit(outputBox);
    output->setText(m_path.isEmpty() ? QDir::home().filePath(QStringLiteral("scene.swf"))
                                     : QFileInfo(m_path).path() + QLatin1Char('/') + QFileInfo(m_path).completeBaseName() + QStringLiteral(".swf"));
    auto* browse = new QPushButton(tr("Browse..."), outputBox);
    connect(browse, &QPushButton::clicked, &ask, [&] {
        QString const chosen = QFileDialog::getSaveFileName(&ask, tr("Export SWF"), output->text(), tr("Flash Movie (*.swf)"));
        if (!chosen.isEmpty())
            output->setText(chosen);
    });
    outputRow->addWidget(output, 1);
    outputRow->addWidget(browse);
    column->addWidget(outputBox);

    auto* rangeBox = new QGroupBox(tr("Export Range"), &ask);
    auto* rangeRow = new QHBoxLayout(rangeBox);
    auto* all = new QRadioButton(tr("&All"), rangeBox);
    auto* some = new QRadioButton(tr("Frames"), rangeBox);
    auto* from = new QSpinBox(rangeBox);
    auto* to = new QSpinBox(rangeBox);
    from->setRange(1, scene.frameCount());
    to->setRange(1, scene.frameCount());
    from->setValue(1);
    to->setValue(scene.frameCount());
    all->setChecked(true);
    rangeRow->addWidget(all);
    rangeRow->addWidget(some);
    rangeRow->addWidget(from);
    rangeRow->addWidget(new QLabel(tr("to"), rangeBox));
    rangeRow->addWidget(to);
    rangeRow->addStretch(1);
    auto rangeOn = [=](bool on) {
        from->setEnabled(on);
        to->setEnabled(on);
    };
    rangeOn(false);
    connect(some, &QRadioButton::toggled, &ask, rangeOn);
    column->addWidget(rangeBox);

    auto* optionsBox = new QGroupBox(tr("Options"), &ask);
    auto* form = new QFormLayout(optionsBox);
    auto* fps = new QDoubleSpinBox(optionsBox);
    fps->setRange(1.0, 120.0);
    fps->setDecimals(2);
    fps->setValue(scene.fps);
    auto* jpeg = new QSpinBox(optionsBox);
    jpeg->setRange(1, 100);
    jpeg->setValue(kept.value(QStringLiteral("jpegQuality"), 80).toInt());
    jpeg->setToolTip(tr("100: pictures kept as they are, without loss"));
    auto check = [&](QString const& text, QString const& key, bool byDefault) {
        auto* box = new QCheckBox(text, optionsBox);
        box->setChecked(kept.value(key, byDefault).toBool());
        box->setObjectName(key);
        return box;
    };
    QCheckBox* const protect = check(tr("Protect from Import"), QStringLiteral("protect"), false);
    QCheckBox* const compress = check(tr("Compress Movie"), QStringLiteral("compress"), true);
    QCheckBox* const sound = check(tr("Sound"), QStringLiteral("sound"), true);
    QCheckBox* const areas = check(tr("Convert Lines to Areas (as they look)"), QStringLiteral("linesAsAreas"), true);
    QCheckBox* const fixedWidth = check(tr("Lines Keep Their Thickness however the Movie is Scaled"), QStringLiteral("fixedLineWidth"), false);
    fixedWidth->setEnabled(!areas->isChecked());
    connect(areas, &QCheckBox::toggled, fixedWidth, [fixedWidth](bool on) { fixedWidth->setEnabled(!on); });
    form->addRow(tr("Frame rate (fps)"), fps);
    form->addRow(tr("JPEG quality"), jpeg);
    for (QCheckBox* box : {protect, compress, sound, areas, fixedWidth})
        form->addRow(box);
    column->addWidget(optionsBox);

    auto* leaveBox = new QGroupBox(tr("Do Not Export"), &ask);
    auto* grid = new QGridLayout(leaveBox);
    auto leave = [&](QString const& text, QString const& key) {
        auto* box = new QCheckBox(text, leaveBox);
        box->setChecked(kept.value(QStringLiteral("leave/") + key, false).toBool());
        box->setObjectName(key);
        grid->addWidget(box, grid->count() / 2, grid->count() % 2);
        return box;
    };
    QCheckBox* const noTransparency = leave(tr("Transparencies"), QStringLiteral("transparency"));
    QCheckBox* const noColour = leave(tr("Tints and Colour-Scales"), QStringLiteral("colourEffects"));
    QCheckBox* const noBlurs = leave(tr("Blurs"), QStringLiteral("blurs"));
    QCheckBox* const noGlows = leave(tr("Glows"), QStringLiteral("glows"));
    QCheckBox* const noShadows = leave(tr("Shadows"), QStringLiteral("shadows"));
    QCheckBox* const noBlends = leave(tr("Blend Modes"), QStringLiteral("blendModes"));
    QCheckBox* const noMasks = leave(tr("Cutters"), QStringLiteral("masks"));
    column->addWidget(leaveBox);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &ask);
    connect(buttons, &QDialogButtonBox::accepted, &ask, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &ask, &QDialog::reject);
    column->addWidget(buttons);
    if (ask.exec() != QDialog::Accepted)
        return;

    QString path = output->text().trimmed();
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().toLower() != QLatin1String("swf"))
        path += QStringLiteral(".swf");
    SwfExport::Options options;
    if (some->isChecked()) {
        options.first = std::min(from->value(), to->value()) - 1;
        options.last = std::max(from->value(), to->value()) - 1;
    }
    options.fps = fps->value();
    options.jpegQuality = jpeg->value();
    options.protect = protect->isChecked();
    options.compress = compress->isChecked();
    options.sound = sound->isChecked();
    options.linesAsAreas = areas->isChecked();
    options.fixedLineWidth = fixedWidth->isChecked();
    options.transparency = !noTransparency->isChecked();
    options.colourEffects = !noColour->isChecked();
    options.blurs = !noBlurs->isChecked();
    options.glows = !noGlows->isChecked();
    options.shadows = !noShadows->isChecked();
    options.blendModes = !noBlends->isChecked();
    options.masks = !noMasks->isChecked();
    kept.setValue(QStringLiteral("jpegQuality"), options.jpegQuality);
    for (QCheckBox* box : {protect, compress, sound, areas, fixedWidth})
        kept.setValue(box->objectName(), box->isChecked());
    for (QCheckBox* box : {noTransparency, noColour, noBlurs, noGlows, noShadows, noBlends, noMasks})
        kept.setValue(QStringLiteral("leave/") + box->objectName(), box->isChecked());

    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString error;
    SwfExport::Report report;
    bool const done = SwfExport::write(scene, path, options, &error, &report);
    QApplication::restoreOverrideCursor();
    if (!done) {
        QMessageBox::warning(this, tr("Export SWF"), tr("Could not write %1:\n%2").arg(QDir::toNativeSeparators(path), error));
        return;
    }
    QString const summary = tr("%1: %2 frames, %3 shapes, %4 pictures%5.")
                                .arg(QFileInfo(path).fileName())
                                .arg(report.frames)
                                .arg(report.shapes)
                                .arg(report.bitmaps)
                                .arg(report.sound ? tr(", sound") : QString());
    statusBar()->showMessage(summary, 10000);
    if (!report.leftOut.isEmpty())
        QMessageBox::information(this, tr("SWF Export Report"), summary + QStringLiteral("\n\n") + tr("Not exported:") + QStringLiteral("\n- ") + report.leftOut.join(QStringLiteral("\n- ")));
}
