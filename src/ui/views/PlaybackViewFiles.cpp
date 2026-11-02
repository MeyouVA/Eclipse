#include "ui/views/PlaybackView.h"
#include "ui/views/PlaybackSurface.h"
#include "ui/views/PlaybackMovie.h"
#include "render/images/ImageFiles.h"
#include <QColorSpace>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QApplication>
#include <QScopedValueRollback>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>

bool PlaybackView::reloadFrames(int first,int last,QString* error)
{
    if(error) error->clear();
    if(m_loading || first<0 || last<first || last>=frameCount()) {
        if(error) *error=tr("Select a valid loaded frame range."); return false;
    }
    stop(); QImage current;
    {
        QScopedValueRollback<bool> loading(m_loading,true);
        QProgressDialog progress(tr("Checking selected frames..."),tr("Cancel"),0,last-first+1,this); progress.setMinimumDuration(500); progress.setWindowModality(Qt::WindowModal);
        for(int frame=first;frame<=last;++frame) {
            progress.setValue(frame-first); QApplication::processEvents();
            if(progress.wasCanceled()) { if(error) *error=tr("Frame reload cancelled."); return false; }
            QString why; QImage const image=previewLoaded()?m_preview[frame]:ImageFiles::load(m_files[frame],&why);
            if(image.isNull()) { if(error) *error=tr("Cannot reload %1: %2").arg(QFileInfo(m_files[frame]).fileName(),why); return false; }
            if(frame==m_frame) current=image;
        }
        progress.setValue(last-first+1);
    }
    for(int frame=first;frame<=last;++frame) { m_cache.remove(frame); m_unloaded.remove(frame); }
    if(!current.isNull()) {
        if(m_resolution!=100) current=current.scaled(std::max(1,current.width()*m_resolution/100),std::max(1,current.height()*m_resolution/100),Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
        m_cache.insert(m_frame,new QImage(current),std::max(1,int((current.sizeInBytes()+1023)/1024)));
        m_surface->setImage(current); m_surface->setBlank(false);
    }
    updateInfo(); return true;
}

bool PlaybackView::unloadFrames(int first,int last)
{
    if(m_loading || first<0 || last<first || last>=frameCount()) return false;
    stop();
    for(int frame=first;frame<=last;++frame) { m_cache.remove(frame); m_unloaded.insert(frame); }
    if(m_unloaded.contains(m_frame)) { m_surface->setImage({}); m_surface->setBlank(true); }
    updateInfo(); return true;
}

void PlaybackView::chooseFrameOperation(bool reloadSelected)
{
    if(m_loading || m_files.isEmpty()) { report(tr("Load a frame folder, movie or preview first.")); return; }
    stop(); QDialog dialog(this); dialog.setObjectName(QStringLiteral("PlaybackFrameRange"));
    dialog.setWindowTitle(reloadSelected?tr("Reload Frames"):tr("Unload Frames"));
    auto* layout=new QVBoxLayout(&dialog);
    auto* summary=new QLabel(tr("%1 of %2 frames loaded. Frame positions and sound timing are retained.").arg(frameCount()-m_unloaded.size()).arg(frameCount()),&dialog);
    summary->setWordWrap(true); layout->addWidget(summary);
    auto* form=new QFormLayout; layout->addLayout(form);
    auto spin=[&](char const* name,QString const& text,int value) { auto* box=new QSpinBox(&dialog); box->setObjectName(QLatin1String(name)); box->setRange(1,frameCount()); box->setValue(value); form->addRow(text,box); return box; };
    auto* first=spin("PlaybackSelectedFirst",tr("First frame"),m_first->value());
    auto* last=spin("PlaybackSelectedLast",tr("Last frame"),m_last->value());
    connect(first,&QSpinBox::valueChanged,&dialog,[last](int value) { if(last->value()<value) last->setValue(value); });
    connect(last,&QSpinBox::valueChanged,&dialog,[first](int value) { if(first->value()>value) first->setValue(value); });
    auto* list=new QListWidget(&dialog); list->setObjectName(QStringLiteral("PlaybackRangeFiles"));
    for(int frame=0;frame<frameCount();++frame) list->addItem(tr("%1 — %2 — %3").arg(frame+1).arg(QFileInfo(m_files[frame]).fileName(),frameLoaded(frame)?tr("Loaded"):tr("Unloaded")));
    list->setCurrentRow(m_frame); layout->addWidget(list);
    connect(list,&QListWidget::currentRowChanged,&dialog,[first,last](int row) { first->setValue(row+1); last->setValue(row+1); });
    auto* explanation=new QLabel(previewLoaded()?tr("Snapshot originals are retained for reload until the entire clip is unloaded."):m_movie?tr("Reload restores prepared movie frames. Use Reload Entire Source to refresh a changed movie and its sound."):tr("Reload reads the selected source pictures again. Unload releases their cached and displayed pixels."),&dialog);
    explanation->setWordWrap(true); layout->addWidget(explanation);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog); layout->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept); connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    dialog.resize(520,420);
    if(dialog.exec()!=QDialog::Accepted) return;
    QString error;
    if(reloadSelected) { if(!reloadFrames(first->value()-1,last->value()-1,&error)) report(error); }
    else unloadFrames(first->value()-1,last->value()-1);
}

QString PlaybackView::fileProperties(int frame) const
{
    if(frame<0 || frame>=frameCount()) return tr("No frame selected.");
    QString error; QImage const image=previewLoaded()?m_preview[frame]:ImageFiles::load(m_files[frame],&error);
    QFileInfo const file(m_files[frame]);
    QString const origin=previewLoaded()?tr("Rendered snapshot"):m_movie?m_movie->source:file.absoluteFilePath();
    QString const format=previewLoaded()?tr("In-memory image"):file.suffix().toUpper();
    QString result=tr("Frame: %1\nState: %2\nSource: %3\nFormat: %4\n").arg(frame+1).arg(frameLoaded(frame)?tr("Loaded"):tr("Unloaded"),origin,format);
    if(m_movie) result+=tr("Prepared picture: %1\n").arg(file.absoluteFilePath());
    if(image.isNull()) return result+tr("Image unavailable: %1").arg(error);
    return result+tr("Dimensions: %1 x %2\nPixel depth: %3 bits\nAlpha channel: %4\nColour profile: %5\nSource bytes: %6\n")
        .arg(image.width()).arg(image.height()).arg(image.depth()).arg(image.hasAlphaChannel()?tr("Yes"):tr("No"))
        .arg(image.colorSpace().isValid()?image.colorSpace().description():tr("Unspecified")).arg(previewLoaded()?image.sizeInBytes():file.size());
}

void PlaybackView::showFileProperties()
{
    if(m_loading) return;
    stop(); QDialog dialog(this); dialog.setObjectName(QStringLiteral("PlaybackFileProperties")); dialog.setWindowTitle(tr("Loaded Files Properties"));
    auto* layout=new QVBoxLayout(&dialog); auto* list=new QListWidget(&dialog); list->setObjectName(QStringLiteral("PlaybackPropertyFiles"));
    for(int frame=0;frame<frameCount();++frame) list->addItem(tr("%1 — %2").arg(frame+1).arg(QFileInfo(m_files[frame]).fileName()));
    layout->addWidget(list); auto* detail=new QPlainTextEdit(&dialog); detail->setObjectName(QStringLiteral("PlaybackFileDetail")); detail->setReadOnly(true); layout->addWidget(detail);
    connect(list,&QListWidget::currentRowChanged,&dialog,[this,detail](int row) { detail->setPlainText(fileProperties(row)); });
    if(!m_files.isEmpty()) list->setCurrentRow(m_frame); else detail->setPlainText(tr("No frames loaded."));
    auto* summary=new QPlainTextEdit(properties(),&dialog); summary->setReadOnly(true); summary->setMaximumHeight(120); layout->addWidget(summary);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Close,&dialog); layout->addWidget(buttons); connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    dialog.resize(660,620); dialog.exec();
}
