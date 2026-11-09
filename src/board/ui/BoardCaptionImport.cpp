#include "board/ui/BoardWindow.h"
#include "board/ui/BoardPlayback.h"

#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QStatusBar>

void BoardWindow::updateCaptionsCsv()
{
    if (m_document.path().isEmpty())
        return;
    m_playback->stop();
    QString const title = tr("Update Captions from CSV");
    QString const path = QFileDialog::getOpenFileName(this, title, QFileInfo(m_document.path()).absolutePath(), tr("CSV (*.csv)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, title, file.errorString());
        return;
    }
    QByteArray const bytes = file.read(BoardDocument::captionCsvLimit + 1);
    if (file.error() != QFileDevice::NoError) {
        QMessageBox::warning(this, title, file.errorString());
        return;
    }
    QString error;
    int const changed = m_document.updateCaptionsFromCsv(bytes, &error);
    if (changed < 0)
        QMessageBox::warning(this, title, error);
    else
        statusBar()->showMessage(changed == 0 ? tr("Captions already match the CSV.") : tr("Updated %n caption(s).", nullptr, changed), 6000);
}
