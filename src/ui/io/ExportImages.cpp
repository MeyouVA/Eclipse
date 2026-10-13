// Eclipse - writing the scene out: frames as pictures, an animated GIF, the sound.
#include "ui/window/MainWindow.h"

#include <functional>

#include <algorithm>
#include <cmath>

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>

#include "audio/sound/Sound.h"
#include "model/document/Commands.h"
#include "ui/canvas/Canvas.h"
#include "ui/panels/LibraryView.h"
#include "ui/io/MovieExport.h"
#include "ui/timeline/Timeline.h"
#include "ui/io/ImportExportPrivate.h"

int MainWindow::exportImages(ImageExport const& options, QString* error)
{
    Scene const scene = m_document->scene();
    int const first = std::clamp(options.first, 0, scene.frameCount() - 1);
    int const last = options.last < 0 ? scene.frameCount() - 1 : std::clamp(options.last, first, scene.frameCount() - 1);
    double const scale = std::clamp(options.percent, 1, 400) / 100.0;
    QSize const size(std::max(1, qRound(scene.width * scale)), std::max(1, qRound(scene.height * scale)));
    QString const format = options.format.toLower();
    if (!QDir().mkpath(options.folder)) {
        if (error)
            *error = tr("Could not make the folder %1.").arg(QDir::toNativeSeparators(options.folder));
        return 0;
    }
    int written = 0;
    for (int f = first; f <= last; ++f) {
        QImage image(size, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        {
            QPainter painter(&image);
            painter.setRenderHint(QPainter::Antialiasing, !options.preview);
            painter.scale(scale, scale);
            Canvas::paintFrame(painter, scene, f);
        }
        QString const file = QDir(options.folder).filePath(QStringLiteral("%1_%2.%3").arg(options.name).arg(f + 1, 4, 10, QLatin1Char('0')).arg(format));
        // A JPEG has no transparency and wants the plain layout.
        bool const ok = format == QLatin1String("jpg") ? image.convertToFormat(QImage::Format_RGB32).save(file, "JPG", 92) : image.save(file);
        if (!ok) {
            if (error)
                *error = tr("Could not write %1.").arg(QDir::toNativeSeparators(file));
            break;
        }
        ++written;
        if (written % 8 == 0)
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }
    return written;
}

void MainWindow::askExportImages(bool preview)
{
    Scene const& scene = m_document->scene();
    QDialog dialog(this);
    dialog.setWindowTitle(preview ? tr("Export OpenGL Frames") : tr("Export Images"));
    QLineEdit* const folder = new QLineEdit(m_path.isEmpty() ? QDir::homePath() : QFileInfo(m_path).path() + QStringLiteral("/frames"), &dialog);
    folder->setMinimumWidth(320);
    QPushButton* const browse = new QPushButton(tr("Browse..."), &dialog);
    connect(browse, &QPushButton::clicked, &dialog, [&] {
        QString const chosen = QFileDialog::getExistingDirectory(&dialog, tr("Export To"), folder->text());
        if (!chosen.isEmpty())
            folder->setText(chosen);
    });
    QHBoxLayout* const folderRow = new QHBoxLayout;
    folderRow->addWidget(folder, 1);
    folderRow->addWidget(browse);
    QLineEdit* const name = new QLineEdit(m_path.isEmpty() ? QStringLiteral("frame") : QFileInfo(m_path).completeBaseName(), &dialog);
    QComboBox* const format = new QComboBox(&dialog);
    format->addItem(tr("PNG (with transparency)"), QStringLiteral("png"));
    format->addItem(tr("JPEG"), QStringLiteral("jpg"));
    format->addItem(tr("TIFF"), QStringLiteral("tif"));
    format->addItem(tr("BMP"), QStringLiteral("bmp"));
    QSpinBox* const from = new QSpinBox(&dialog);
    from->setRange(1, scene.frameCount());
    from->setValue(playStart() + 1);
    QSpinBox* const to = new QSpinBox(&dialog);
    to->setRange(1, scene.frameCount());
    to->setValue(playStop() + 1);
    from->setObjectName(QStringLiteral("ExportFrom"));
    to->setObjectName(QStringLiteral("ExportTo"));
    // The whole scene, or only the frame one is on, at a press.
    QPushButton* const allFrames = new QPushButton(tr("All Frames"), &dialog);
    allFrames->setObjectName(QStringLiteral("ExportAllFrames"));
    QPushButton* const thisFrame = new QPushButton(tr("Current Frame"), &dialog);
    thisFrame->setObjectName(QStringLiteral("ExportCurrentFrame"));
    int const frames = scene.frameCount(), here = frame() + 1;
    connect(allFrames, &QPushButton::clicked, &dialog, [from, to, frames] {
        from->setValue(1);
        to->setValue(frames);
    });
    connect(thisFrame, &QPushButton::clicked, &dialog, [from, to, here] {
        from->setValue(here);
        to->setValue(here);
    });
    QHBoxLayout* const rangeRow = new QHBoxLayout;
    rangeRow->addWidget(allFrames);
    rangeRow->addWidget(thisFrame);
    rangeRow->addStretch(1);
    QComboBox* const resolution = new QComboBox(&dialog);
    for (int percent : {100, 75, 50, 25, 200})
        resolution->addItem(tr("%1%  (%2 x %3)").arg(percent).arg(qRound(scene.width * percent / 100.0)).arg(qRound(scene.height * percent / 100.0)), percent);
    resolution->setCurrentIndex(preview ? 2 : 0);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QFormLayout* const form = new QFormLayout(&dialog);
    form->addRow(tr("Folder"), folderRow);
    form->addRow(tr("File name"), name);
    form->addRow(tr("Format"), format);
    form->addRow(tr("From frame"), from);
    form->addRow(tr("To frame"), to);
    form->addRow(QString(), rangeRow);
    form->addRow(tr("Resolution"), resolution);
    if (preview)
        form->addRow(new QLabel(tr("Quick frames for checking the animation: edges are not smoothed."), &dialog));
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;

    ImageExport options;
    options.folder = folder->text();
    options.name = name->text().trimmed().isEmpty() ? QStringLiteral("frame") : name->text().trimmed();
    options.format = format->currentData().toString();
    options.first = std::min(from->value(), to->value()) - 1;
    options.last = std::max(from->value(), to->value()) - 1;
    options.percent = resolution->currentData().toInt();
    options.preview = preview;
    stop();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString error;
    int const written = exportImages(options, &error);
    QApplication::restoreOverrideCursor();
    int const wanted = options.last - options.first + 1;
    if (written == wanted)
        statusBar()->showMessage(tr("Exported %1 frames to %2").arg(written).arg(QDir::toNativeSeparators(options.folder)), 6000);
    else
        QMessageBox::critical(this, tr("Eclipse"), tr("Export stopped after %1 of %2 frames.\n%3").arg(written).arg(wanted).arg(error));
}

void MainWindow::onActionExportFrames()
{
    askExportImages(false);
}

void MainWindow::onActionExportGif()
{
    QString const start = m_path.isEmpty() ? QStringLiteral("scene.gif") : QFileInfo(m_path).path() + QLatin1Char('/') + QFileInfo(m_path).completeBaseName() + QStringLiteral(".gif");
    QString path = QFileDialog::getSaveFileName(this, tr("Export Animated GIF"), start, tr("Animated GIF (*.gif)"));
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().toLower() != QLatin1String("gif"))
        path += QStringLiteral(".gif");
    exportMovieTo(path);
}

bool MainWindow::exportAudio(QString const& path, QString* error)
{
    QVector<qint16> const sound = Sound::mix(m_document->scene());
    if (sound.isEmpty()) {
        if (error)
            *error = tr("The scene has no sound to export: add a sound layer first (File > Import > Sound), and check that it is enabled.");
        return false;
    }
    return Sound::writeWav(path, sound, error);
}

void MainWindow::onActionExportAudio()
{
    QString const start = m_path.isEmpty() ? QStringLiteral("scene.wav") : QFileInfo(m_path).path() + QLatin1Char('/') + QFileInfo(m_path).completeBaseName() + QStringLiteral(".wav");
    QString path = QFileDialog::getSaveFileName(this, tr("Export Audio"), start, tr("WAV Sound (*.wav)"));
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().isEmpty())
        path += QStringLiteral(".wav");
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString error;
    bool const ok = exportAudio(path, &error);
    QApplication::restoreOverrideCursor();
    if (ok)
        statusBar()->showMessage(tr("Exported %1").arg(QDir::toNativeSeparators(path)), 6000);
    else
        QMessageBox::critical(this, tr("Eclipse"), tr("The sound could not be written.\n%1").arg(error));
}
