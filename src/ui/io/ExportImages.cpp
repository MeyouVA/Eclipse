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
#include <QHeaderView>
#include <QStatusBar>
#include <QTableWidget>
#include <QVBoxLayout>

#include "audio/sound/Sound.h"
#include "model/document/Commands.h"
#include "ui/canvas/Canvas.h"
#include "ui/panels/LibraryView.h"
#include "ui/io/MovieExport.h"
#include "ui/io/SwfExport.h"
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

void MainWindow::onActionExportSwf()
{
    QString const start = m_path.isEmpty() ? QStringLiteral("scene.swf") : QFileInfo(m_path).path() + QLatin1Char('/') + QFileInfo(m_path).completeBaseName() + QStringLiteral(".swf");
    QString path = QFileDialog::getSaveFileName(this, tr("Export SWF"), start, tr("Flash Movie (*.swf)"));
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().toLower() != QLatin1String("swf"))
        path += QStringLiteral(".swf");
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString error;
    SwfExport::Report report;
    bool const done = SwfExport::write(m_document->scene(), path, &error, &report);
    QApplication::restoreOverrideCursor();
    if (!done) {
        QMessageBox::warning(this, tr("Export SWF"), tr("Could not write %1:\n%2").arg(QDir::toNativeSeparators(path), error));
        return;
    }
    QStringList left;
    if (report.skippedEffects)
        left << tr("effects");
    if (report.skippedPictures)
        left << tr("bitmap pictures");
    statusBar()->showMessage(tr("%1: %2 frames, %3 shapes.").arg(QFileInfo(path).fileName()).arg(report.frames).arg(report.shapes)
                                 + (left.isEmpty() ? QString() : tr(" Left out (not vector shapes): %1.").arg(left.join(tr(" and ")))),
                             10000);
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

int MainWindow::renderWrites(QString* error)
{
    Scene const scene = m_document->scene();
    QDir const home = m_path.isEmpty() ? QDir::current() : QFileInfo(m_path).absoluteDir();
    int made = 0;
    QStringList problems;
    for (Scene::Write const& w : scene.writes) {
        if (!w.enabled || w.path.trimmed().isEmpty())
            continue;
        QString const where = QDir::cleanPath(home.absoluteFilePath(w.path.trimmed()));
        QString why;
        bool ok = false;
        if (w.movie) {
            MovieExport::Options options;
            options.scale = std::clamp(w.percent, 1, 1000) / 100.0;
            QDir().mkpath(QFileInfo(where).absolutePath());
            ok = MovieExport::write(scene, where, nullptr, &why, options);
        } else {
            ImageExport images;
            images.folder = where;
            images.name = w.name.trimmed().isEmpty() ? QStringLiteral("frame") : w.name.trimmed();
            images.format = w.format;
            images.percent = std::clamp(w.percent, 1, 1000);
            QDir().mkpath(where);
            ok = exportImages(images, &why) == scene.frameCount();
        }
        if (ok)
            ++made;
        else
            problems << QStringLiteral("%1: %2").arg(w.name, why);
    }
    if (error)
        *error = problems.join(QLatin1Char('\n'));
    return made;
}

void MainWindow::onActionRenderWrites()
{
    if (m_document->scene().writes.isEmpty()) {
        statusBar()->showMessage(tr("The scene has no write outputs: add them with Scene > Write Outputs."), 7000);
        onActionWriteOutputs();
        return;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString error;
    int const made = renderWrites(&error);
    QApplication::restoreOverrideCursor();
    if (!error.isEmpty())
        QMessageBox::warning(this, tr("Render Write Nodes"), tr("%n output(s) made. These could not be:", nullptr, made) + QLatin1Char('\n') + error);
    else
        statusBar()->showMessage(tr("%n write output(s) made.", nullptr, made), 6000);
}

void MainWindow::onActionWriteOutputs()
{
    QVector<Scene::Write> writes = m_document->scene().writes;
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("WriteOutputsDialog"));
    dialog.setWindowTitle(tr("Write Outputs"));
    QTableWidget* const table = new QTableWidget(0, 6, &dialog);
    table->setObjectName(QStringLiteral("WriteOutputsTable"));
    table->setHorizontalHeaderLabels({tr("On"), tr("Name"), tr("Kind"), tr("Folder or file"), tr("Images as"), tr("Size %")});
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    table->verticalHeader()->setVisible(false);
    auto addRow = [table](Scene::Write const& w) {
        int const row = table->rowCount();
        table->insertRow(row);
        QTableWidgetItem* const on = new QTableWidgetItem;
        on->setCheckState(w.enabled ? Qt::Checked : Qt::Unchecked);
        table->setItem(row, 0, on);
        table->setItem(row, 1, new QTableWidgetItem(w.name));
        QComboBox* const kind = new QComboBox(table);
        kind->addItems({QObject::tr("Images"), QObject::tr("Movie")});
        kind->setCurrentIndex(w.movie ? 1 : 0);
        table->setCellWidget(row, 2, kind);
        table->setItem(row, 3, new QTableWidgetItem(w.path));
        QComboBox* const format = new QComboBox(table);
        format->addItems({QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("tga"), QStringLiteral("tif"), QStringLiteral("bmp")});
        format->setCurrentText(w.format);
        table->setCellWidget(row, 4, format);
        QSpinBox* const size = new QSpinBox(table);
        size->setRange(1, 1000);
        size->setValue(w.percent);
        table->setCellWidget(row, 5, size);
    };
    for (Scene::Write const& w : std::as_const(writes))
        addRow(w);
    QPushButton* const add = new QPushButton(tr("Add"), &dialog);
    add->setObjectName(QStringLiteral("WriteOutputsAdd"));
    QPushButton* const remove = new QPushButton(tr("Remove"), &dialog);
    remove->setObjectName(QStringLiteral("WriteOutputsRemove"));
    connect(add, &QPushButton::clicked, &dialog, [table, addRow] {
        Scene::Write w;
        w.name = QObject::tr("Write%1").arg(table->rowCount() + 1);
        w.path = QStringLiteral("frames/") + w.name;
        addRow(w);
    });
    connect(remove, &QPushButton::clicked, &dialog, [table] {
        if (table->currentRow() >= 0)
            table->removeRow(table->currentRow());
    });
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QHBoxLayout* const row = new QHBoxLayout;
    row->addWidget(add);
    row->addWidget(remove);
    row->addStretch(1);
    QVBoxLayout* const layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(tr("Renders kept with the scene: File > Export > Render Write Nodes makes all of them that are on. "
                                    "A folder or file not written in full is taken from the scene file's folder."), &dialog));
    layout->addWidget(table, 1);
    layout->addLayout(row);
    layout->addWidget(buttons);
    dialog.resize(760, 320);
    if (dialog.exec() != QDialog::Accepted)
        return;
    QVector<Scene::Write> made;
    for (int r = 0; r < table->rowCount(); ++r) {
        Scene::Write w;
        w.enabled = table->item(r, 0)->checkState() == Qt::Checked;
        w.name = table->item(r, 1)->text().trimmed();
        w.movie = static_cast<QComboBox*>(table->cellWidget(r, 2))->currentIndex() == 1;
        w.path = table->item(r, 3)->text().trimmed();
        w.format = static_cast<QComboBox*>(table->cellWidget(r, 4))->currentText();
        w.percent = static_cast<QSpinBox*>(table->cellWidget(r, 5))->value();
        made.push_back(w);
    }
    m_document->setWrites(made);
}
