#include "ui/window/MainWindow.h"
#include "model/document/Commands.h"
#include "model/media/MediaFormats.h"
#include "audio/sound/Sound.h"
#include "audio/sound/AudioFiles.h"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QMessageBox>
#include <QRegularExpression>
#include <QSpinBox>
#include <QStatusBar>
#include <algorithm>
#include <cmath>

bool MainWindow::exportAudio(QString const& path,QString* error)
{
    QVector<qint16> const sound=Sound::mix(m_document->scene());
    if(sound.isEmpty()) { if(error) *error=tr("The scene has no sound to export: add a sound layer first and check that it is enabled."); return false; }
    AudioFiles::Options options;
    if(QFileInfo(path).suffix().compare(QLatin1String("opus"),Qt::CaseInsensitive)==0) options.rate=48000;
    return AudioFiles::write(path,sound,options,error);
}

void MainWindow::onActionExportAudio()
{
    Scene const scene=m_document->scene();
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("ExportAudioDialog"));
    dialog.setWindowTitle(tr("Export Audio"));
    QFormLayout* form=new QFormLayout(&dialog);
    QComboBox* format=new QComboBox(&dialog); format->setObjectName(QStringLiteral("AudioFormat"));
    for(auto const& entry : MediaFormats::audioFormats()) format->addItem(entry.name,entry.id);
    QComboBox* range=new QComboBox(&dialog); range->setObjectName(QStringLiteral("AudioRange")); range->addItems({tr("All"),tr("Frames")});
    QSpinBox* first=new QSpinBox(&dialog); first->setObjectName(QStringLiteral("AudioFirst")); first->setRange(1,scene.frameCount());
    QSpinBox* last=new QSpinBox(&dialog); last->setObjectName(QStringLiteral("AudioLast")); last->setRange(1,scene.frameCount()); last->setValue(scene.frameCount());
    connect(first,&QSpinBox::valueChanged,last,[last](int value) { last->setMinimum(value); });
    auto rangeChanged=[range,first,last] { first->setEnabled(range->currentIndex()==1); last->setEnabled(range->currentIndex()==1); };
    connect(range,&QComboBox::currentIndexChanged,&dialog,rangeChanged); rangeChanged();
    QComboBox* rate=new QComboBox(&dialog); rate->setObjectName(QStringLiteral("AudioRate"));
    QComboBox* bits=new QComboBox(&dialog); bits->setObjectName(QStringLiteral("AudioBits"));
    QComboBox* channels=new QComboBox(&dialog); channels->setObjectName(QStringLiteral("AudioChannels")); channels->addItem(tr("Mono"),1); channels->addItem(tr("Stereo"),2); channels->setCurrentIndex(1);
    QComboBox* tracks=new QComboBox(&dialog); tracks->setObjectName(QStringLiteral("AudioTracks")); tracks->addItems({tr("Merge all Audio Tracks"),tr("Process Audio Tracks Individually")});
    auto formatChanged=[format,rate,bits] {
        auto const* entry=MediaFormats::audio(format->currentData().toString());
        if(!entry) return;
        int previous=rate->currentData().toInt(); if(!previous) previous=44100;
        rate->clear();
        for(int hz : {8000,11025,12000,16000,22050,24000,32000,44100,48000}) {
            if(entry->id==QLatin1String("opus") && hz!=8000 && hz!=12000 && hz!=16000 && hz!=24000 && hz!=48000) continue;
            if(entry->id==QLatin1String("ac3") && hz!=32000 && hz!=44100 && hz!=48000) continue;
            rate->addItem(tr("%1 Hz").arg(hz),hz);
        }
        int const chosen=rate->findData(previous); rate->setCurrentIndex(chosen<0?rate->findData(48000):chosen);
        bits->clear();
        for(int depth : {8,16,24,32}) if((entry->pcm || depth>=16) && depth<=entry->maxBits) bits->addItem(tr("%1-bit integer").arg(depth),depth);
        if(entry->floating) { bits->addItem(tr("32-bit float"),32); bits->setItemData(bits->count()-1,true,Qt::UserRole+1); }
        bits->setCurrentIndex(bits->findData(16)); bits->setEnabled(entry->lossless);
    };
    connect(format,&QComboBox::currentIndexChanged,&dialog,formatChanged); formatChanged();
    form->addRow(tr("Format"),format); form->addRow(tr("Export Range"),range); form->addRow(tr("From frame"),first); form->addRow(tr("To frame"),last);
    form->addRow(tr("Sample rate"),rate); form->addRow(tr("Bit depth"),bits); form->addRow(tr("Channels"),channels); form->addRow(tr("Tracks"),tracks);
    QDialogButtonBox* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept); connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject); form->addRow(buttons);
    if(dialog.exec()!=QDialog::Accepted) return;
    auto const* chosen=MediaFormats::audio(format->currentData().toString());
    if(!chosen) return;
    QString const name=m_path.isEmpty()?QStringLiteral("scene"):QFileInfo(m_path).completeBaseName();
    QString const initial=(m_path.isEmpty()?QDir::homePath():QFileInfo(m_path).path())+QLatin1Char('/')+name+QLatin1Char('.')+chosen->suffix;
    QString path=QFileDialog::getSaveFileName(this,tr("Export Audio"),initial,chosen->name+QStringLiteral(" (*.%1)").arg(chosen->suffix));
    if(path.isEmpty()) return;
    if(QFileInfo(path).suffix().compare(chosen->suffix,Qt::CaseInsensitive)!=0) path+=QLatin1Char('.')+chosen->suffix;
    AudioFiles::Options options; options.format=chosen->id; options.rate=rate->currentData().toInt(); options.bits=bits->currentData().toInt(); options.floating=bits->currentData(Qt::UserRole+1).toBool(); options.channels=channels->currentData().toInt();
    int const from=range->currentIndex()==0?0:first->value()-1,through=range->currentIndex()==0?scene.frameCount()-1:last->value()-1;
    QString error; int written=0;
    auto keep=[&](Scene const& source,QString const& target) {
        QVector<qint16> const samples=Sound::mix(source,from,through);
        if(samples.isEmpty()) return true;
        if(!AudioFiles::write(target,samples,options,&error)) return false;
        ++written; return true;
    };
    QApplication::setOverrideCursor(Qt::WaitCursor);
    if(tracks->currentIndex()==0) keep(scene,path);
    else for(int l=0;l<scene.layers.size();++l) if(scene.layers[l].kind==Layer::Sound && scene.enabled(l)) {
        Scene one=scene; Layer sound=scene.layers[l]; sound.parent=0; one.layers={sound};
        QString clean=sound.name; clean.replace(QRegularExpression(QStringLiteral(R"([\/:*?"<>|])")),QStringLiteral("_"));
        QFileInfo const file(path);
        if(!keep(one,file.dir().filePath(file.completeBaseName()+QStringLiteral("_%1_%2.").arg(l+1).arg(clean)+chosen->suffix))) break;
    }
    QApplication::restoreOverrideCursor();
    if(error.isEmpty() && written==0) error=tr("No sound to export in the selected range.");
    if(!error.isEmpty()) QMessageBox::critical(this,tr("Export Audio"),error);
    else statusBar()->showMessage(tr("Exported %n audio file(s)",nullptr,written),6000);
}
