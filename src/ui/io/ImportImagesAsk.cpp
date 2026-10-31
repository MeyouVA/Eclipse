// Eclipse - importing images: the options asked for, and the images brought in.
#include "ui/window/MainWindow.h"
#include "ui/panels/ColourSpaceBox.h"

#include <functional>

#include <algorithm>
#include <cmath>

#include <QApplication>
#include <QCheckBox>
#include "ui/panels/ColourEditor.h"
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QElapsedTimer>
#include <QProgressDialog>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QSettings>
#include <QStandardPaths>
#include <QVBoxLayout>

#include "audio/sound/Sound.h"
#include "model/document/Commands.h"
#include "ui/io/Scanner.h"
#include "model/geometry/Model3D.h"
#include "model/geometry/Vectorize.h"
#include "canvas/Canvas.h"
#include "ui/panels/LibraryView.h"
#include "ui/io/MovieExport.h"
#include "ui/panels/PalettePanel.h"
#include "ui/timeline/Timeline.h"
#include "ui/io/ImportExportPrivate.h"
#include "formats/flash/SwfImport.h"
#include "formats/flash/FlaImport.h"

void MainWindow::askImportImages(bool linked, QStringList const& given)
{
    QDialog dialog(this);
    dialog.setWindowTitle(linked ? tr("Link to Images") : tr("Import Images"));
    dialog.setObjectName(QStringLiteral("ImportImagesDialog"));

    QGroupBox* const filesBox = new QGroupBox(tr("Files"), &dialog);
    QListWidget* const files = new QListWidget(filesBox);
    files->setMinimumSize(380, 110);
    QPushButton* const browse = new QPushButton(tr("Browse..."), filesBox);
    QVBoxLayout* const filesLayout = new QVBoxLayout(filesBox);
    filesLayout->addWidget(files);
    filesLayout->addWidget(browse, 0, Qt::AlignRight);

    QGroupBox* const layerBox = new QGroupBox(tr("Layer"), &dialog);
    QRadioButton* const newLayer = new QRadioButton(tr("Create Single Layer Named:"), layerBox);
    QLineEdit* const layerName = new QLineEdit(layerBox);
    layerName->setPlaceholderText(tr("the name of the first file"));
    QRadioButton* const byNames = new QRadioButton(tr("Create Layer(s) Based on Filenames"), layerBox);
    byNames->setObjectName(QStringLiteral("ImportByFilenames"));
    byNames->setToolTip(tr("Files named alike but for a number at the end (walk-1, walk-2, run-1) each go onto a layer of that name (walk, run)."));
    QRadioButton* const currentLayer = new QRadioButton(tr("Add to the Current Layer, from the Current Frame"), layerBox);
    currentLayer->setEnabled(m_document->scene().layers[layer()].holdsDrawings());
    newLayer->setChecked(true);
    QSpinBox* const hold = new QSpinBox(layerBox);
    hold->setRange(1, 1000);
    QHBoxLayout* const nameRow = new QHBoxLayout;
    nameRow->addWidget(newLayer);
    nameRow->addWidget(layerName, 1);
    QFormLayout* const layerLayout = new QFormLayout(layerBox);
    layerLayout->addRow(nameRow);
    layerLayout->addRow(byNames);
    layerLayout->addRow(currentLayer);
    layerLayout->addRow(tr("Frames for each image"), hold);

    QGroupBox* const alignBox = new QGroupBox(tr("Alignment"), &dialog);
    QComboBox* const alignment = new QComboBox(alignBox);
    alignment->setObjectName(QStringLiteral("ImportAlignment"));
    alignment->addItems({tr("Fit"), tr("Pan"), tr("Project Resolution (Actual Size)")});
    alignment->setToolTip(tr("Fit: the whole picture inside the frame.\nPan: the picture fills the frame; what does not fit hangs over the edges, for the camera to pan across.\n"
                             "Project Resolution: a pixel of the picture is a pixel of the scene."));
    alignment->setCurrentIndex(std::clamp(QSettings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse")).value(QStringLiteral("prefs/importAlignment"), 0).toInt(), 0, 2));
    QFormLayout* const alignLayout = new QFormLayout(alignBox);
    alignLayout->addRow(tr("Rule"), alignment);

    QGroupBox* const asBox = new QGroupBox(tr("Import As"), &dialog);
    QRadioButton* const bitmap = new QRadioButton(tr("Keep As Original Bitmap"), asBox);
    QRadioButton* const vector = new QRadioButton(tr("Vector Drawing (Black and White)"), asBox);
    QRadioButton* const centre = new QRadioButton(tr("Vector Drawing (Centre Lines)"), asBox);
    centre->setObjectName(QStringLiteral("ImportCentreLines"));
    centre->setToolTip(tr("Each stroke of the picture becomes a pencil line down its middle, as thick as the stroke was - lines you can reshape,\n"
                          "not shapes round them. For clean line drawings."));
    QRadioButton* const colourVector = new QRadioButton(tr("Vector Drawing (Colour)"), asBox);
    QSpinBox* const colourCount = new QSpinBox(asBox);
    colourCount->setRange(2, 64);
    colourCount->setValue(8);
    colourCount->setEnabled(false);
    connect(colourVector, &QRadioButton::toggled, colourCount, &QSpinBox::setEnabled);
    bitmap->setChecked(true);
    vector->setEnabled(!linked);    // a traced picture is artwork of the scene, not a file to read again
    colourVector->setEnabled(!linked);
    centre->setEnabled(!linked);
    QSpinBox* const threshold = new QSpinBox(asBox);
    threshold->setRange(1, 255);
    threshold->setValue(128);
    threshold->setEnabled(false);
    QSpinBox* const colourArt = new QSpinBox(asBox);
    colourArt->setObjectName(QStringLiteral("ImportColourArtThreshold"));
    colourArt->setRange(0, 100);
    colourArt->setSpecialValueText(tr("None"));
    colourArt->setToolTip(tr("Tones lighter than the line but darker than this much of white are traced onto the Colour Art, in their colour."));
    colourArt->setEnabled(false);
    QSpinBox* const smoothing = new QSpinBox(asBox);
    smoothing->setObjectName(QStringLiteral("ImportSmoothPasses"));
    smoothing->setRange(0, 20);
    smoothing->setToolTip(tr("How many times the traced line art is smoothed."));
    smoothing->setEnabled(false);
    auto thresholdUsed = [vector, centre, threshold, colourArt, smoothing] {
        bool const on = vector->isChecked() || centre->isChecked();
        for (QSpinBox* box : {threshold, colourArt, smoothing})
            box->setEnabled(on);
    };
    connect(vector, &QRadioButton::toggled, threshold, thresholdUsed);
    connect(centre, &QRadioButton::toggled, threshold, thresholdUsed);
    QFormLayout* const asLayout = new QFormLayout(asBox);
    asLayout->addRow(bitmap);
    // Photoshop files: each of their layers a layer of its own (onto new layers, kept as bitmaps).
    QCheckBox* const psdLayers = new QCheckBox(tr("Photoshop Files: Import Each Layer as a Layer"), asBox);
    psdLayers->setObjectName(QStringLiteral("ImportPsdLayers"));
    psdLayers->setToolTip(tr("Each layer of a PSD or PSB file becomes a layer of its own, with its name, place, opacity, blend mode, masks and groups,\n"
                             "all in a group named after the file. Off: the file's flattened picture."));
    psdLayers->setChecked(QSettings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse")).value(QStringLiteral("prefs/importPsdLayers"), true).toBool());
    psdLayers->setEnabled(!linked);
    auto psdUsable = [psdLayers, bitmap, newLayer, linked] { psdLayers->setEnabled(!linked && bitmap->isChecked() && newLayer->isChecked()); };
    connect(bitmap, &QRadioButton::toggled, psdLayers, psdUsable);
    connect(newLayer, &QRadioButton::toggled, psdLayers, psdUsable);
    connect(psdLayers, &QCheckBox::toggled, psdLayers, [](bool on) { QSettings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse")).setValue(QStringLiteral("prefs/importPsdLayers"), on); });
    asLayout->addRow(psdLayers);
    asLayout->addRow(vector);
    asLayout->addRow(centre);
    asLayout->addRow(tr("Threshold (darker than this is line)"), threshold);
    asLayout->addRow(tr("Colour Art Threshold (%)"), colourArt);
    asLayout->addRow(tr("Smooth passes for line art"), smoothing);
    asLayout->addRow(colourVector);
    asLayout->addRow(tr("Colours at most"), colourCount);
    // The colour of the paper: left out, so that what is traced is the drawing and not the sheet it is on.
    QCheckBox* const leavePaper = new QCheckBox(tr("Leave out the paper, of this colour:"), asBox);
    leavePaper->setObjectName(QStringLiteral("ImportLeavePaper"));
    leavePaper->setChecked(true);
    leavePaper->setEnabled(false);
    QPushButton* const paper = new QPushButton(asBox);
    paper->setObjectName(QStringLiteral("ImportPaper"));
    paper->setEnabled(false);
    auto showPaper = [paper](QColor const& colour) {
        paper->setProperty("colour", colour);
        paper->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid #202020; min-height: 16px; min-width: 60px;").arg(colour.name()));
    };
    showPaper(QColor(255, 255, 255));
    connect(paper, &QPushButton::clicked, &dialog, [paper, showPaper, &dialog] {
        QColor const chosen = ColourEditor::getColour(paper->property("colour").value<QColor>(), &dialog, tr("Paper"));
        if (chosen.isValid())
            showPaper(chosen);
    });
    connect(colourVector, &QRadioButton::toggled, leavePaper, &QCheckBox::setEnabled);
    connect(colourVector, &QRadioButton::toggled, paper, &QPushButton::setEnabled);
    asLayout->addRow(leavePaper, paper);
    // Colour management: the colour space the pictures are read in.
    QSettings const prefs(QStringLiteral("Eclipse"), QStringLiteral("Eclipse"));
    QCheckBox* const convert = new QCheckBox(tr("Convert Image Colour Space on Import"), asBox);
    convert->setObjectName(QStringLiteral("ImportConvertColour"));
    convert->setChecked(prefs.value(QStringLiteral("prefs/convertOnImport"), false).toBool());
    convert->setToolTip(tr("The pictures are read in their source colour space, then put in the scene's working colour space."));
    QComboBox* const source = ColourSpaceBox::make(asBox, QStringLiteral("ImportSourceSpace"), prefs.value(QStringLiteral("prefs/importSourceSpace")).toString(),
                                                   tr("From the File (its own profile, else sRGB)"));
    source->setEnabled(convert->isChecked());
    connect(convert, &QCheckBox::toggled, source, &QComboBox::setEnabled);
    asLayout->addRow(convert);
    asLayout->addRow(tr("Source Colour Space:"), source);

    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QString const startFolder = m_path.isEmpty() ? QString() : QFileInfo(m_path).path();
    auto choose = [&] {
        QStringList chosen = QFileDialog::getOpenFileNames(&dialog, dialog.windowTitle(), startFolder, kImageFilter);
        if (chosen.isEmpty())
            return;
        chosen.sort(Qt::CaseInsensitive);   // a numbered run of files in its order
        files->clear();
        files->addItems(chosen);
        buttons->button(QDialogButtonBox::Ok)->setEnabled(true);
    };
    connect(browse, &QPushButton::clicked, &dialog, choose);

    QVBoxLayout* const layout = new QVBoxLayout(&dialog);
    layout->addWidget(filesBox);
    layout->addWidget(layerBox);
    layout->addWidget(alignBox);
    layout->addWidget(asBox);
    layout->addWidget(buttons);
    if (given.isEmpty()) {
        choose();   // straight to the files, as the first thing asked
    } else {
        files->addItems(given);
        layerName->setText(tr("Scan"));
        buttons->button(QDialogButtonBox::Ok)->setEnabled(true);
    }
    if (files->count() == 0 || dialog.exec() != QDialog::Accepted)
        return;

    ImageImport options;
    for (int i = 0; i < files->count(); ++i)
        options.files << files->item(i)->text();
    options.newLayer = newLayer->isChecked() || byNames->isChecked();
    options.byFilenames = byNames->isChecked();
    options.layerName = layerName->text();
    options.fit = alignment->currentIndex() != 2;
    options.pan = alignment->currentIndex() == 1;
    options.paper = leavePaper->isChecked() ? paper->property("colour").value<QColor>() : QColor();
    options.vectorize = vector->isChecked() || colourVector->isChecked() || centre->isChecked();
    options.centreLines = centre->isChecked();
    options.colours = colourVector->isChecked() ? colourCount->value() : 0;
    options.threshold = threshold->value();
    options.psdLayers = psdLayers->isChecked();
    options.colourArtThreshold = colourArt->value();
    options.smoothPasses = smoothing->value();
    options.linked = linked;
    options.hold = hold->value();
    options.convertColour = convert->isChecked();
    options.sourceSpace = ColourSpaceBox::chosen(source);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString error;
    int const at = importImages(options, &error);
    QApplication::restoreOverrideCursor();
    if (at < 0)
        QMessageBox::critical(this, tr("Eclipse"), error);
    else
        statusBar()->showMessage(tr("%n image(s) imported.", nullptr, int(options.files.size())), 5000);
}
