#include "ui/io/RawSoundImport.h"
#include "audio/sound/RawAudio.h"
#include "audio/sound/Sound.h"
#include "model/scene/SceneProject.h"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressDialog>
#include <QSpinBox>
#include <QTemporaryDir>

QString RawSoundImport::prepare(QWidget* parent,QString const& path,QString const& scenePath,QString* error)
{
    if(!RawAudio::isRaw(path)) return path;
    if(scenePath.isEmpty()) { if(error) *error=QObject::tr("Save the scene before importing raw audio. Converted sounds are kept in its audio folder."); return {}; }
    QDialog dialog(parent); dialog.setWindowTitle(QObject::tr("Raw Audio Settings")); dialog.setObjectName(QStringLiteral("RawAudioSettings"));
    QFormLayout form(&dialog);
    QLabel label(QObject::tr("%1 has no audio header. Choose the settings used to record it.").arg(QFileInfo(path).fileName())); label.setWordWrap(true); form.addRow(&label);
    QComboBox encoding; encoding.setObjectName(QStringLiteral("RawAudioEncoding"));
    for(auto const& item : RawAudio::formats()) encoding.addItem(item.name,item.id);
    auto const* inferred=RawAudio::format(QFileInfo(path).suffix()); encoding.setCurrentIndex(encoding.findData(inferred?inferred->id:QStringLiteral("s16le")));
    QSpinBox rate; rate.setObjectName(QStringLiteral("RawAudioRate")); rate.setRange(1000,384000); rate.setValue(44100); rate.setSuffix(QObject::tr(" Hz"));
    QSpinBox channels; channels.setObjectName(QStringLiteral("RawAudioChannels")); channels.setRange(1,32); channels.setValue(2);
    QLineEdit skip(QStringLiteral("0")); skip.setObjectName(QStringLiteral("RawAudioSkipBytes"));
    QSpinBox codes; codes.setObjectName(QStringLiteral("RawAudioCodeBits")); codes.setRange(2,5); codes.setValue(4);
    QLabel validation; validation.setWordWrap(true);
    QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
    auto update=[&] {
        auto const* item=RawAudio::format(encoding.currentData().toString());
        bool const compressed=item && item->codes;
        codes.setEnabled(compressed); channels.setEnabled(!compressed);
        if(compressed) { channels.setValue(1); rate.setValue(8000); }
    };
    QObject::connect(&encoding,&QComboBox::currentIndexChanged,&dialog,update); update();
    QObject::connect(&buttons,&QDialogButtonBox::accepted,&dialog,[&] {
        bool valid=false; qint64 const offset=skip.text().toLongLong(&valid);
        if(!valid || offset<0 || offset>=QFileInfo(path).size()) { validation.setText(QObject::tr("Skip bytes must be a whole number smaller than the file size.")); return; }
        dialog.accept();
    });
    QObject::connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    form.addRow(QObject::tr("Encoding and byte order"),&encoding); form.addRow(QObject::tr("Sample rate"),&rate); form.addRow(QObject::tr("Channels"),&channels);
    form.addRow(QObject::tr("Skip bytes at start"),&skip); form.addRow(QObject::tr("Bits per G.726 code"),&codes); form.addRow(&validation); form.addRow(&buttons);
    if(dialog.exec()!=QDialog::Accepted) return {};
    QDir audio(QFileInfo(scenePath).dir().filePath(QLatin1String(SceneProject::kAudio)));
    if(!audio.mkpath(QStringLiteral("."))) { if(error) *error=QObject::tr("Cannot create the scene audio folder."); return {}; }
    QTemporaryDir converted(audio.filePath(QStringLiteral("raw_XXXXXX")));
    if(!converted.isValid()) { if(error) *error=QObject::tr("Cannot create the imported sound folder."); return {}; }
    QString const output=converted.filePath(SceneProject::fileName(QFileInfo(path).completeBaseName(),QStringLiteral("sound"))+QStringLiteral(".wav"));
    RawAudio::Options options; options.format=encoding.currentData().toString(); options.rate=rate.value(); options.channels=channels.value(); options.skipBytes=skip.text().toLongLong(); options.codeBits=codes.value();
    QProgressDialog progress(QObject::tr("Importing raw audio..."),QObject::tr("Cancel"),0,0,parent); progress.setWindowModality(Qt::WindowModal); progress.setMinimumDuration(0);
    bool const okay=RawAudio::convert(path,output,options,error,[&] { QApplication::processEvents(); return !progress.wasCanceled(); });
    if(progress.wasCanceled()) { if(error) error->clear(); return {}; }
    if(!okay || !Sound::load(output,error)) return {};
    converted.setAutoRemove(false);
    return output;
}
