// Eclipse - scanning drawings in.
#include "animation/ui/io/Scanning.h"

#include "model/geometry/VectorizeOptions.h"
#include "animation/ui/io/Scanner.h"
#include "animation/ui/io/VectorizeDialog.h"
#include "animation/ui/panels/ColourEditor.h"
#include "animation/ui/window/MainWindow.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QVBoxLayout>

#include <cmath>

ScanSettings ScanSettings::load()
{
    QSettings s(QStringLiteral("Eclipse"), QStringLiteral("Eclipse"));
    ScanSettings o;
    o.dpi = s.value(QStringLiteral("scan/dpi"), o.dpi).toInt();
    o.colour = s.value(QStringLiteral("scan/colour"), o.colour).toInt();
    o.fields = s.value(QStringLiteral("scan/fields"), o.fields).toInt();
    o.pegSide = s.value(QStringLiteral("scan/pegSide"), o.pegSide).toInt();
    o.whitePoint = s.value(QStringLiteral("scan/whitePoint"), o.whitePoint).toInt();
    o.blackPoint = s.value(QStringLiteral("scan/blackPoint"), o.blackPoint).toInt();
    o.gamma = s.value(QStringLiteral("scan/gamma"), o.gamma).toDouble();
    o.dropColour = QColor(s.value(QStringLiteral("scan/dropColour")).toString());
    o.threshold = s.value(QStringLiteral("scan/threshold"), o.threshold).toInt();
    o.opticalRegistration = s.value(QStringLiteral("scan/opticalRegistration"), o.opticalRegistration).toBool();
    o.type = s.value(QStringLiteral("scan/type"), o.type).toInt();
    o.vectorizeLine = s.value(QStringLiteral("scan/vectorizeLine")).toString();
    o.flip = s.value(QStringLiteral("scan/flip"), o.flip).toBool();
    o.scanAndAdvance = s.value(QStringLiteral("scan/scanAndAdvance"), o.scanAndAdvance).toBool();
    o.overwrite = s.value(QStringLiteral("scan/overwrite"), o.overwrite).toBool();
    o.hold = std::max(1, s.value(QStringLiteral("scan/hold"), o.hold).toInt());
    o.pagePanning = s.value(QStringLiteral("scan/pagePanning"), o.pagePanning).toBool();
    o.scannerName = s.value(QStringLiteral("scan/scannerName")).toString();
    return o;
}

void ScanSettings::save() const
{
    QSettings s(QStringLiteral("Eclipse"), QStringLiteral("Eclipse"));
    s.setValue(QStringLiteral("scan/dpi"), dpi);
    s.setValue(QStringLiteral("scan/colour"), colour);
    s.setValue(QStringLiteral("scan/fields"), fields);
    s.setValue(QStringLiteral("scan/pegSide"), pegSide);
    s.setValue(QStringLiteral("scan/whitePoint"), whitePoint);
    s.setValue(QStringLiteral("scan/blackPoint"), blackPoint);
    s.setValue(QStringLiteral("scan/gamma"), gamma);
    s.setValue(QStringLiteral("scan/dropColour"), dropColour.isValid() ? dropColour.name() : QString());
    s.setValue(QStringLiteral("scan/threshold"), threshold);
    s.setValue(QStringLiteral("scan/opticalRegistration"), opticalRegistration);
    s.setValue(QStringLiteral("scan/type"), type);
    s.setValue(QStringLiteral("scan/vectorizeLine"), vectorizeLine);
    s.setValue(QStringLiteral("scan/flip"), flip);
    s.setValue(QStringLiteral("scan/scanAndAdvance"), scanAndAdvance);
    s.setValue(QStringLiteral("scan/overwrite"), overwrite);
    s.setValue(QStringLiteral("scan/hold"), hold);
    s.setValue(QStringLiteral("scan/pagePanning"), pagePanning);
    s.setValue(QStringLiteral("scan/scannerName"), scannerName);
}

QImage ScanSettings::process(QImage const& page, QString* notes) const
{
    QImage image = page.convertToFormat(QImage::Format_ARGB32);
    // Levels: black point to black, white point to white, the tones between by the gamma.
    int const low = std::clamp(blackPoint, 0, 254), high = std::clamp(whitePoint, low + 1, 255);
    unsigned char table[256];
    for (int v = 0; v < 256; ++v) {
        double const t = std::clamp(double(v - low) / double(high - low), 0.0, 1.0);
        table[v] = static_cast<unsigned char>(std::lround(255.0 * std::pow(t, 1.0 / std::max(0.1, gamma))));
    }
    for (int y = 0; y < image.height(); ++y) {
        QRgb* const row = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            QRgb c = row[x];
            // Drop Colour: near that colour, paper.
            if (dropColour.isValid() && std::abs(qRed(c) - dropColour.red()) + std::abs(qGreen(c) - dropColour.green()) + std::abs(qBlue(c) - dropColour.blue()) < 90) {
                row[x] = qRgba(255, 255, 255, qAlpha(c));
                continue;
            }
            c = qRgba(table[qRed(c)], table[qGreen(c)], table[qBlue(c)], qAlpha(c));
            if (colour == Grey) {
                int const g = qGray(c);
                c = qRgba(g, g, g, qAlpha(c));
            } else if (colour == BlackAndWhite) {
                int const g = qGray(c) < std::clamp(threshold, 1, 255) ? 0 : 255;
                c = qRgba(g, g, g, qAlpha(c));
            }
            row[x] = c;
        }
    }
    if (flip)
        image = image.transformed(QTransform().rotate(180.0));
    image.setDotsPerMeterX(int(std::lround(dpi / 0.0254)));
    image.setDotsPerMeterY(int(std::lround(dpi / 0.0254)));
    if (opticalRegistration) {
        Vectorize::Options o;
        o.dpi = dpi;
        o.pegSide = pegSide;
        QTransform move;
        QString why;
        if (Vectorize::registration(image, o, move, &why)) {
            QImage moved(image.size(), QImage::Format_ARGB32);
            moved.fill(Qt::white);
            moved.setDotsPerMeterX(image.dotsPerMeterX());
            moved.setDotsPerMeterY(image.dotsPerMeterY());
            QPainter p(&moved);
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            p.setTransform(move);
            p.drawImage(0, 0, image);
            p.end();
            image = moved;
        } else if (notes) {
            *notes = QObject::tr("Optical Registration Failed: %1").arg(why);
        }
    }
    return image;
}

QString ScanSettings::info(QImage const& page, QString const& file) const
{
    static char const* const colours[] = {QT_TRANSLATE_NOOP("ScanSettings", "Black and White"), QT_TRANSLATE_NOOP("ScanSettings", "Grey"),
                                          QT_TRANSLATE_NOOP("ScanSettings", "Colour")};
    return QObject::tr("Scanned %1 from %2: %3 x %4 pixels, %5 dpi, %6, %7 fields%8")
        .arg(QDateTime::currentDateTime().toString(Qt::ISODate), scannerName.isEmpty() ? QFileInfo(file).fileName() : scannerName)
        .arg(page.width())
        .arg(page.height())
        .arg(dpi)
        .arg(QCoreApplication::translate("ScanSettings", colours[std::clamp(colour, 0, 2)]))
        .arg(fields)
        .arg(opticalRegistration ? QObject::tr(", optically registered") : QString());
}

// ---- Scanning Parameters -----------------------------------------------------------------------

ScanConfigDialog::ScanConfigDialog(ScanSettings const& settings, QWidget* parent) : QDialog(parent), m_settings(settings)
{
    setObjectName(QStringLiteral("ScanConfigDialog"));
    setWindowTitle(tr("Scanning Parameters"));
    QVBoxLayout* const top = new QVBoxLayout(this);
    QGroupBox* const box = new QGroupBox(tr("Scanning parameters"), this);
    QFormLayout* const form = new QFormLayout(box);
    QComboBox* const colour = new QComboBox(box);
    colour->addItems({tr("Black and White"), tr("Grey"), tr("Colour")});
    colour->setCurrentIndex(settings.colour);
    form->addRow(tr("Color:"), colour);
    QSpinBox* const dpi = new QSpinBox(box);
    dpi->setObjectName(QStringLiteral("ScanDpi"));
    dpi->setRange(50, 2400);
    dpi->setSuffix(tr(" dpi"));
    dpi->setValue(settings.dpi);
    form->addRow(tr("Resolution:"), dpi);
    QComboBox* const side = new QComboBox(box);
    side->addItems({tr("Top"), tr("Bottom"), tr("Left"), tr("Right")});
    side->setCurrentIndex(settings.pegSide);
    form->addRow(tr("Peg Side:"), side);
    QSpinBox* const fields = new QSpinBox(box);
    fields->setRange(4, 40);
    fields->setSuffix(tr(" fields"));
    fields->setValue(settings.fields);
    form->addRow(tr("Size:"), fields);
    QPushButton* const drop = new QPushButton(box);
    auto showDrop = [drop](QColor const& c) {
        drop->setText(c.isValid() ? c.name() : tr("None"));
        drop->setProperty("colour", c);
    };
    showDrop(settings.dropColour);
    connect(drop, &QPushButton::clicked, this, [this, drop, showDrop] {
        QColor const chosen = ColourEditor::getColour(drop->property("colour").value<QColor>(), this, tr("Drop Colour"));
        showDrop(chosen);
    });
    QPushButton* const noDrop = new QPushButton(tr("None"), box);
    connect(noDrop, &QPushButton::clicked, this, [showDrop] { showDrop(QColor()); });
    QHBoxLayout* const dropRow = new QHBoxLayout;
    dropRow->addWidget(drop);
    dropRow->addWidget(noDrop);
    form->addRow(tr("Drop Color:"), dropRow);
    QSpinBox* const white = new QSpinBox(box);
    white->setRange(1, 255);
    white->setValue(settings.whitePoint);
    form->addRow(tr("White Point"), white);
    QSpinBox* const black = new QSpinBox(box);
    black->setRange(1, 255);
    black->setValue(settings.blackPoint);
    form->addRow(tr("Black Point"), black);
    QDoubleSpinBox* const gamma = new QDoubleSpinBox(box);
    gamma->setRange(0.1, 3.0);
    gamma->setSingleStep(0.05);
    gamma->setValue(settings.gamma);
    form->addRow(tr("Gamma"), gamma);
    QSpinBox* const threshold = new QSpinBox(box);
    threshold->setRange(1, 255);
    threshold->setValue(settings.threshold);
    form->addRow(tr("Threshold"), threshold);
    QCheckBox* const registration = new QCheckBox(tr("Optical Registration"), box);
    registration->setChecked(settings.opticalRegistration);
    form->addRow(QString(), registration);
    QComboBox* const type = new QComboBox(box);
    type->setObjectName(QStringLiteral("ScanType"));
    type->addItems({tr("Bitmap"), tr("Vector (Line Art)"), tr("Vector (Colour Art too)")});
    type->setCurrentIndex(settings.type);
    form->addRow(tr("Scanning Mode:"), type);
    QPushButton* const vectorize = new QPushButton(tr("Vectorization..."), box);
    auto line = std::make_shared<QString>(settings.vectorizeLine);
    connect(vectorize, &QPushButton::clicked, this, [this, line] {
        Scene const blank;
        VectorizeDialog ask(blank, line->isEmpty() ? MainWindow::vectorizeOptionsLine() : *line, QImage(), this);
        if (ask.exec() == QDialog::Accepted)
            *line = ask.line();
    });
    form->addRow(tr("Vectorization Style:"), vectorize);
    top->addWidget(box);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    top->addWidget(buttons);
    m_read = [=](ScanSettings& s) {
        s.colour = colour->currentIndex();
        s.dpi = dpi->value();
        s.pegSide = side->currentIndex();
        s.fields = fields->value();
        s.dropColour = drop->property("colour").value<QColor>();
        s.whitePoint = white->value();
        s.blackPoint = black->value();
        s.gamma = gamma->value();
        s.threshold = threshold->value();
        s.opticalRegistration = registration->isChecked();
        s.type = type->currentIndex();
        s.vectorizeLine = *line;
    };
}

ScanSettings ScanConfigDialog::settings() const
{
    ScanSettings s = m_settings;
    m_read(s);
    return s;
}

// ---- Scan Drawing ------------------------------------------------------------------------------

ScanDrawingDialog::ScanDrawingDialog(MainWindow* window, QWidget* parent) : QDialog(parent), m_window(window), m_settings(ScanSettings::load())
{
    setObjectName(QStringLiteral("ScanDrawingDialog"));
    setWindowTitle(tr("Scan Drawing"));
    QVBoxLayout* const top = new QVBoxLayout(this);
    QGroupBox* const box = new QGroupBox(tr("Scanning Options"), this);
    QFormLayout* const form = new QFormLayout(box);
    m_name = new QLineEdit(box);
    m_name->setObjectName(QStringLiteral("ScanDrawingName"));
    m_name->setPlaceholderText(tr("the next number"));
    form->addRow(tr("Drawing name:"), m_name);
    m_hold = new QSpinBox(box);
    m_hold->setRange(1, 999);
    m_hold->setValue(m_settings.hold);
    form->addRow(tr("Hold Exposure"), m_hold);
    m_advance = new QCheckBox(tr("Scan and Advance"), box);
    m_advance->setObjectName(QStringLiteral("ScanAdvance"));
    m_advance->setChecked(m_settings.scanAndAdvance);
    m_overwrite = new QCheckBox(tr("Overwrite Drawings"), box);
    m_overwrite->setChecked(m_settings.overwrite);
    m_flip = new QCheckBox(tr("Flip Drawings"), box);
    m_flip->setChecked(m_settings.flip);
    m_panning = new QCheckBox(tr("Page Panning"), box);
    m_panning->setToolTip(tr("The page kept at its own size (for a pan), not fitted to the frame."));
    m_panning->setChecked(m_settings.pagePanning);
    for (QCheckBox* const c : {m_advance, m_overwrite, m_flip, m_panning})
        form->addRow(QString(), c);
    top->addWidget(box);
    m_last = new QLabel(tr("Last Scanned: -"), this);
    m_last->setObjectName(QStringLiteral("ScanLast"));
    top->addWidget(m_last);
    QHBoxLayout* const buttons = new QHBoxLayout;
    QPushButton* const scan = new QPushButton(tr("Scan Drawing"), this);
    scan->setObjectName(QStringLiteral("ScanDrawingButton"));
    scan->setToolTip(tr("Scan a drawing: from the scanner into the current cell."));
    QPushButton* const parameters = new QPushButton(tr("Scanning Parameters..."), this);
    QPushButton* const stop = new QPushButton(tr("Stop Scanning"), this);
    buttons->addWidget(scan);
    buttons->addWidget(parameters);
    buttons->addStretch(1);
    buttons->addWidget(stop);
    top->addLayout(buttons);
    connect(scan, &QPushButton::clicked, this, [this] { scanOne(); });
    connect(parameters, &QPushButton::clicked, this, [this] {
        ScanConfigDialog ask(m_settings, this);
        if (ask.exec() == QDialog::Accepted) {
            m_settings = ask.settings();
            m_settings.save();
        }
    });
    connect(stop, &QPushButton::clicked, this, &QDialog::accept);
}

bool ScanDrawingDialog::scanOne()
{
    m_settings.scanAndAdvance = m_advance->isChecked();
    m_settings.overwrite = m_overwrite->isChecked();
    m_settings.flip = m_flip->isChecked();
    m_settings.pagePanning = m_panning->isChecked();
    m_settings.hold = m_hold->value();
    m_settings.save();
    QString const folder = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + QStringLiteral("/Eclipse Scans");
    QString error;
    QStringList const pages = Scanner::acquire(this, folder, &error);
    if (pages.isEmpty()) {
        if (!error.isEmpty())
            QMessageBox::information(this, tr("Scan Drawing"), error);
        return false;
    }
    int const made = m_window->scanPages(pages, m_settings, m_name->text().trimmed(), &error);
    if (made <= 0) {
        QMessageBox::warning(this, tr("Scan Drawing"), error.isEmpty() ? tr("Error while scanning") : error);
        return false;
    }
    m_last->setText(tr("Last Scanned: %1").arg(QFileInfo(pages.last()).fileName()));
    m_name->clear();
    if (!error.isEmpty())
        m_last->setText(m_last->text() + QLatin1String(" - ") + error);
    return true;
}
