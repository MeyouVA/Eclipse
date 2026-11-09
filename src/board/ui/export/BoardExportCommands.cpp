#include "board/ui/BoardWindow.h"
#include "board/ui/BoardPlayback.h"
#include "board/ui/BoardTimeline.h"
#include "board/ui/export/BoardExport.h"
#include "canvas/export/StoryboardPdf.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <QProgressDialog>
#include <QSaveFile>
#include <QScopedValueRollback>
#include <QStatusBar>

void BoardWindow::exportBoard(QString const& operation)
{
    if (m_document.path().isEmpty() || m_exporting)
        return;
    QScopedValueRollback<bool> const exporting(m_exporting, true);
    m_playback->stop();
    BoardProject const project = m_document.project();
    QString const projectFile = m_document.path();
    QDir const directory = QFileInfo(projectFile).dir();
    QString output;
    QString error;
    bool ok = false;
    auto appendSuffix = [this](QString& path, QString const& suffix) {
        path += suffix;
        return !QFileInfo::exists(path) || QMessageBox::question(this, tr("Export"),
            tr("Replace the existing file “%1”?").arg(QFileInfo(path).fileName())) == QMessageBox::Yes;
    };
    if (operation == QLatin1String("pdf")) {
        auto const profiles = StoryboardPdf::profiles();
        QStringList names;
        for (auto const& profile : profiles)
            names.append(profile.name());
        bool accepted = false;
        QString const profile = QInputDialog::getItem(this, tr("Export PDF"), tr("Page Layout"), names, 0, false, &accepted);
        if (!accepted)
            return;
        output = QFileDialog::getSaveFileName(this, tr("Export PDF"), directory.filePath(project.name + QStringLiteral(".pdf")), tr("PDF (*.pdf)"));
        if (output.isEmpty())
            return;
        if (!output.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive) && !appendSuffix(output, QStringLiteral(".pdf")))
            return;
        ok = BoardExport::pdf(project, projectFile, output, profiles[names.indexOf(profile)], &error);
    } else if (operation == QLatin1String("csv")) {
        output = QFileDialog::getSaveFileName(this, tr("Export Captions CSV"), directory.filePath(project.name + QStringLiteral(".csv")),
            tr("CSV (*.csv)"));
        if (output.isEmpty())
            return;
        if (!output.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive) && !appendSuffix(output, QStringLiteral(".csv")))
            return;
        Storyboard::Naming naming;
        naming.customPanelNames = true;
        naming.showTotal = false;
        QByteArray const csv = project.timeline().captionsCsv(project.fps, false, naming);
        QSaveFile file(output);
        ok = file.open(QIODevice::WriteOnly) && file.write(csv) == csv.size() && file.commit();
        if (!ok)
            error = file.errorString();
    } else if (operation == QLatin1String("animation")) {
        QString const parent = QFileDialog::getExistingDirectory(this, tr("Choose a Folder for Animation Export"), directory.absolutePath());
        if (parent.isEmpty())
            return;
        output = QDir(parent).filePath(project.name + tr(" Animation"));
        ok = BoardExport::animation(project, projectFile, output, &error);
    } else {
        bool const movie = operation == QLatin1String("movie");
        bool transparent = false;
        if (movie) {
            QString const mp4 = tr("MP4 Video (*.mp4)");
            QString const mov = tr("QuickTime MOV with Alpha (*.mov)");
            QString selected = mp4;
            output = QFileDialog::getSaveFileName(this, tr("Export Animatic"), directory.filePath(project.name),
                                                   mp4 + QStringLiteral(";;") + mov, &selected);
            if (output.isEmpty())
                return;
            if (QFileInfo(output).suffix().isEmpty()
                && !appendSuffix(output, selected == mov ? QStringLiteral(".mov") : QStringLiteral(".mp4")))
                return;
            transparent = QFileInfo(output).suffix().compare(QLatin1String("mov"), Qt::CaseInsensitive) == 0;
        } else {
            QString const parent = QFileDialog::getExistingDirectory(this, tr("Choose a Folder for the PNG Sequence"), directory.absolutePath());
            if (parent.isEmpty())
                return;
            auto const background = QMessageBox::question(this, tr("Export PNG Sequence"), tr("Use a transparent background?"),
                QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::No);
            if (background == QMessageBox::Cancel)
                return;
            transparent = background == QMessageBox::Yes;
            output = QDir(parent).filePath(project.name + tr(" Frames"));
        }
        QProgressDialog progress(tr("Exporting frames..."), tr("Cancel"), 0, m_timeline->length(), this);
        progress.setWindowModality(Qt::WindowModal);
        progress.setMinimumDuration(250);
        auto report = [&](int frame) {
            progress.setValue(frame);
            return !progress.wasCanceled();
        };
        ok = movie ? BoardExport::movie(project, projectFile, output, transparent, report, &error)
                   : BoardExport::images(project, output, transparent, report, &error);
        if (progress.wasCanceled())
            return;
    }
    if (ok)
        statusBar()->showMessage(tr("Exported to %1").arg(QDir::toNativeSeparators(output)), 6000);
    else
        QMessageBox::warning(this, tr("Export"), error);
}
