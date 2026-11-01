#include "ui/io/RawMediaImport.h"
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>

bool RawMediaImport::video(QWidget* parent,QString const& path,double fps,RawVideo::Options& options,QString* error)
{
    QDialog dialog(parent); dialog.setObjectName(QStringLiteral("RawVideoSettings")); dialog.setWindowTitle(QObject::tr("Raw Video Settings")); QFormLayout form(&dialog);
    QLabel description(QObject::tr("Choose the recorded frame size, pixel format and frame rate. Rates may be written as 30000/1001.")); description.setWordWrap(true); form.addRow(&description);
    QComboBox format; format.setObjectName(QStringLiteral("RawVideoFormat")); for(auto const& item : RawVideo::formats()) format.addItem(item.name,item.id);
    QString const suffix=QFileInfo(path).suffix().toLower();
    QString inferred=suffix==QLatin1String("rgb")?QStringLiteral("rgb24"):suffix==QLatin1String("bgr")?QStringLiteral("bgr24"):suffix==QLatin1String("uyvy")?QStringLiteral("uyvy422"):suffix==QLatin1String("yuyv")?QStringLiteral("yuyv422"):suffix;
    format.setCurrentIndex(std::max(0,format.findData(RawVideo::format(inferred)?inferred:QStringLiteral("yuv420p"))));
    QSpinBox width,height; width.setRange(1,32768); height.setRange(1,32768); width.setValue(1920); height.setValue(1080);
    width.setObjectName(QStringLiteral("RawVideoWidth")); height.setObjectName(QStringLiteral("RawVideoHeight"));
    QLineEdit rate(QString::number(fps,'g',12)),skip(QStringLiteral("0")); rate.setObjectName(QStringLiteral("RawVideoRate")); skip.setObjectName(QStringLiteral("RawVideoSkipBytes"));
    QComboBox matrix,range; matrix.addItem(QObject::tr("BT.601"),QStringLiteral("smpte170m")); matrix.addItem(QObject::tr("BT.709"),QStringLiteral("bt709")); matrix.addItem(QObject::tr("BT.2020"),QStringLiteral("bt2020nc")); range.addItems({QObject::tr("Limited range"),QObject::tr("Full range")});
    QLabel validation; validation.setWordWrap(true); QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
    QObject::connect(&buttons,&QDialogButtonBox::accepted,&dialog,[&] {
        bool valid=false; options.skipBytes=skip.text().toLongLong(&valid); options.format=format.currentData().toString(); options.width=width.value(); options.height=height.value(); options.frameRate=rate.text().trimmed(); options.matrix=matrix.currentData().toString(); options.fullRange=range.currentIndex()==1;
        QString why; QStringList const args=RawVideo::inputArguments(path,options,&why);
        if(!valid || args.isEmpty()) { validation.setText(why.isEmpty()?QObject::tr("Enter a valid byte offset."):why); return; }
        qint64 const frames=(QFileInfo(path).size()-options.skipBytes)/RawVideo::frameBytes(options);
        if(double(frames)*fps/RawVideo::frameRate(options.frameRate)>100000) { validation.setText(QObject::tr("This import would produce more than 100000 scene frames. Choose a smaller source or a higher recording frame rate.")); return; }
        dialog.accept();
    });
    QObject::connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    auto update=[&] { auto const* item=RawVideo::format(format.currentData().toString()); bool const yuv=item && (!item->components || item->id==QLatin1String("uyvy422") || item->id==QLatin1String("yuyv422")); matrix.setEnabled(yuv); range.setEnabled(yuv); };
    QObject::connect(&format,&QComboBox::currentIndexChanged,&dialog,update); update();
    form.addRow(QObject::tr("Pixel format"),&format); form.addRow(QObject::tr("Width"),&width); form.addRow(QObject::tr("Height"),&height); form.addRow(QObject::tr("Recording frame rate"),&rate); form.addRow(QObject::tr("YUV colour matrix"),&matrix); form.addRow(QObject::tr("YUV sample range"),&range); form.addRow(QObject::tr("Skip bytes at start"),&skip); form.addRow(&validation); form.addRow(&buttons);
    if(dialog.exec()!=QDialog::Accepted) { if(error) error->clear(); return false; }
    return true;
}
