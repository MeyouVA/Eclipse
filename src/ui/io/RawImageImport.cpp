#include "ui/io/RawMediaImport.h"
#include "render/images/ImageFiles.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>

RawMediaImport::Images RawMediaImport::images(QWidget* parent,QStringList const& files,QString const& scenePath,bool linked,QString* error,bool forceRaw)
{
    Images result;
    for(QString const& path : files) {
        bool raw=RawPixels::needsSettings(path);
        if(QFileInfo(path).suffix().compare(QLatin1String("raw"),Qt::CaseInsensitive)==0) {
            if(forceRaw) raw=true;
            else {
                bool okay=false; QStringList const choices{QObject::tr("Camera image"),QObject::tr("Raw pixel data")};
                QString const chosen=QInputDialog::getItem(parent,QObject::tr("Import RAW Image"),QObject::tr("Read this file as:"),choices,0,false,&okay);
                if(!okay) { result.ready=false; return result; }
                raw=chosen==choices.last();
            }
        }
        if(!raw) { result.files << path; continue; }
        if(scenePath.isEmpty()) { if(error) *error=QObject::tr("Save the scene before importing raw pixels. Decoded copies are kept with the scene."); result.ready=false; return result; }
        QDialog dialog(parent); dialog.setObjectName(QStringLiteral("RawImageSettings")); dialog.setWindowTitle(QObject::tr("Raw Image Settings")); QFormLayout form(&dialog);
        QLabel source(dialog.fontMetrics().elidedText(QFileInfo(path).fileName(),Qt::ElideMiddle,400)); source.setToolTip(path); form.addRow(QObject::tr("Source"),&source);
        QLabel description(QObject::tr("Read the first image using its recorded dimensions and sample layout. The source file is kept unchanged.")); description.setWordWrap(true); form.addRow(&description);
        if(linked) { auto* notice=new QLabel(QObject::tr("This link will track the decoded scene copy, rather than the original headerless data."),&dialog); notice->setWordWrap(true); form.addRow(notice); }
        QComboBox layout; layout.setObjectName(QStringLiteral("RawImageLayout")); for(auto const& item : RawPixels::formats()) layout.addItem(item.name,item.id);
        auto const* inferred=RawPixels::format(QFileInfo(path).suffix()); layout.setCurrentIndex(layout.findData(inferred?inferred->id:QStringLiteral("rgba")));
        QSpinBox width,height,palette; width.setRange(1,32768); height.setRange(1,32768); width.setValue(1920); height.setValue(1080); palette.setRange(1,256); palette.setValue(256);
        width.setObjectName(QStringLiteral("RawImageWidth")); height.setObjectName(QStringLiteral("RawImageHeight")); palette.setObjectName(QStringLiteral("RawImagePalette"));
        QComboBox depth; depth.setObjectName(QStringLiteral("RawImageDepth")); depth.addItem(QObject::tr("8-bit unsigned"),8); depth.addItem(QObject::tr("16-bit unsigned"),16); depth.addItem(QObject::tr("32-bit float"),32); depth.addItem(QObject::tr("1-bit monochrome"),1);
        QComboBox endian; endian.addItem(QObject::tr("Little endian"),false); endian.addItem(QObject::tr("Big endian"),true);
        QCheckBox planar(QObject::tr("Separate channel planes")),linear(QObject::tr("Linear RGB samples"));
        QLineEdit skip(QStringLiteral("0")),profile; skip.setObjectName(QStringLiteral("RawImageSkipBytes")); profile.setObjectName(QStringLiteral("RawImageProfile"));
        QPushButton browse(QObject::tr("Choose ICC profile..."));
        QObject::connect(&browse,&QPushButton::clicked,&dialog,[&] { QString const file=QFileDialog::getOpenFileName(&dialog,QObject::tr("Source Colour Profile"),profile.text(),QObject::tr("ICC profiles (*.icc *.icm);;All Files (*)")); if(!file.isEmpty()) profile.setText(file); });
        auto update=[&] {
            QString const id=layout.currentData().toString(); bool const mono=id==QLatin1String("mono") || id==QLatin1String("group4");
            bool const fixed=mono || id==QLatin1String("map") || id==QLatin1String("uyvy") || id==QLatin1String("yuv");
            if(fixed) depth.setCurrentIndex(depth.findData(mono?1:8)); depth.setEnabled(!fixed); palette.setEnabled(id==QLatin1String("map"));
            planar.setEnabled(!fixed); if(fixed) planar.setChecked(false);
            linear.setEnabled(!id.startsWith(QLatin1String("cmyk"))); if(!linear.isEnabled()) linear.setChecked(false);
        };
        QObject::connect(&layout,&QComboBox::currentIndexChanged,&dialog,update); update();
        QObject::connect(&depth,&QComboBox::currentIndexChanged,&dialog,[&] { linear.setChecked(depth.currentData().toInt()==32); });
        QLabel validation; validation.setWordWrap(true); QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
        QObject::connect(&buttons,&QDialogButtonBox::accepted,&dialog,[&] { bool valid=false; qint64 const offset=skip.text().toLongLong(&valid); if(!valid || offset<0 || offset>=QFileInfo(path).size()) validation.setText(QObject::tr("Enter a byte offset within the source file.")); else dialog.accept(); });
        QObject::connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        form.addRow(QObject::tr("Channel layout"),&layout); form.addRow(QObject::tr("Width"),&width); form.addRow(QObject::tr("Height"),&height); form.addRow(QObject::tr("Sample type"),&depth); form.addRow(QObject::tr("Byte order"),&endian);
        form.addRow(&planar); form.addRow(&linear); form.addRow(QObject::tr("Palette entries (MAP)"),&palette); form.addRow(QObject::tr("Skip bytes at start"),&skip); form.addRow(QObject::tr("Optional source ICC"),&profile); form.addRow(&browse); form.addRow(&validation); form.addRow(&buttons);
        if(dialog.exec()!=QDialog::Accepted) { result.ready=false; return result; }
        RawPixels::Options options; options.format=layout.currentData().toString(); options.width=width.value(); options.height=height.value(); options.bits=depth.currentData().toInt(); options.floating=options.bits==32;
        options.bigEndian=endian.currentData().toBool(); options.planar=planar.isChecked(); options.linear=linear.isChecked(); options.palette=palette.value(); options.skipBytes=skip.text().toLongLong(); options.profile=profile.text().trimmed();
        QImage const image=RawPixels::read(path,options,error);
        if(image.isNull()) { result.ready=false; return result; }
        if(!result.folder) {
            QDir media(QFileInfo(scenePath).dir().filePath(QFileInfo(scenePath).completeBaseName()+QStringLiteral("-media")));
            if(!media.mkpath(QStringLiteral("."))) { if(error) *error=QObject::tr("Cannot create the scene media folder."); result.ready=false; return result; }
            result.folder=std::make_unique<QTemporaryDir>(media.filePath(QStringLiteral("raw-images_XXXXXX")));
            if(!result.folder->isValid()) { if(error) *error=QObject::tr("Cannot create the raw image folder."); result.ready=false; return result; }
        }
        QByteArray const data=ImageFiles::encodeEmbedded(image);
        QDir destination(result.folder->path()); destination.mkpath(QString::number(result.files.size()));
        QString const output=destination.filePath(QString::number(result.files.size())+QLatin1Char('/')+QFileInfo(path).completeBaseName()+QLatin1Char('.')+ImageFiles::embeddedSuffix(data));
        QFile file(output);
        if(data.isEmpty() || !file.open(QIODevice::WriteOnly) || file.write(data)!=data.size()) { if(error) *error=QObject::tr("Cannot store the decoded image."); result.ready=false; return result; }
        file.close(); result.files << output;
    }
    return result;
}
