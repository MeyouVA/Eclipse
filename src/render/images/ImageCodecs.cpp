#include "render/images/ImageFiles.h"
#include "render/images/ImageCodecProcess.h"
#include "render/images/ImageApng.h"
#include "render/images/ImageFfmpeg.h"
#include "render/images/ImageJng.h"
#include "render/images/ImageSixel.h"
#include "platform/ThirdParty.h"
#include "model/scene/PsdRead.h"
#include "model/media/IconRead.h"
#include "model/media/RawPixels.h"
#include "colour/icc/Icc.h"

#include <QColorSpace>
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QtEndian>
#include <cstring>
#include <cmath>
#include <limits>

namespace {

using ImageCodec::run;
using ImageCodec::runProgram;

QColorSpace exrSpace(QString const& path)
{
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly)) return {};
    QByteArray const header=file.read(8);
    if(header.size()!=8 || qFromLittleEndian<quint32>(header.constData())!=20000630u) return {};
    auto word=[&]() {
        QByteArray value; char byte=0;
        while(value.size()<256 && file.getChar(&byte)) { if(!byte) return value; value.append(byte); }
        return QByteArray();
    };
    while(file.pos()<16*1024*1024 && !file.atEnd()) {
        QByteArray const name=word(); if(name.isEmpty()) return {};
        QByteArray const type=word(),lengthBytes=file.read(4);
        if(type.isEmpty() || lengthBytes.size()!=4) return {};
        quint32 const length=qFromLittleEndian<quint32>(lengthBytes.constData());
        if(length>quint64(file.size()-file.pos())) return {};
        if(name=="chromaticities" && type=="chromaticities" && length==32) {
            QByteArray const data=file.read(32); float values[8];
            if(data.size()!=32) return {};
            for(int i=0;i<8;++i) { quint32 const bits=qFromLittleEndian<quint32>(data.constData()+i*4); std::memcpy(&values[i],&bits,4); if(!std::isfinite(values[i])) return {}; }
            QColorSpace const space(QPointF(values[6],values[7]),QPointF(values[0],values[1]),QPointF(values[2],values[3]),QPointF(values[4],values[5]),QColorSpace::TransferFunction::Linear);
            if(space.isValid()) return space;
            Cms::Primaries const primaries{values[0],values[1],values[2],values[3],values[4],values[5],values[6],values[7]};
            return QColorSpace::fromIccProfile(Cms::Icc::write(primaries,Cms::Curve::Linear,QStringLiteral("Linear EXR")));
        }
        if(!file.seek(file.pos()+length)) return {};
    }
    return {};
}

bool writeBytes(QString const& file, QByteArray const& bytes)
{
    QFile out(file);
    return out.open(QIODevice::WriteOnly) && out.write(bytes) == bytes.size();
}

QByteArray box(QByteArray const& type, QByteArray const& data)
{
    QByteArray result(8, '\0');
    qToBigEndian(quint32(data.size() + 8), result.data());
    std::memcpy(result.data() + 4, type.constData(), 4);
    return result + data;
}

QByteArray icns(QImage const& image, QString* error)
{
    int const size = image.width();
    QList<int> const sizes{16,32,64,128,256,512,1024};
    QList<QByteArray> const codes{"icp4","icp5","icp6","ic07","ic08","ic09","ic10"};
    int const index = int(sizes.indexOf(size));
    if (image.height() != size || index < 0) {
        if (error) *error = QStringLiteral("ICNS requires a square image of 16, 32, 64, 128, 256, 512 or 1024 pixels.");
        return {};
    }
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG")) { if (error) *error = QStringLiteral("Cannot encode the ICNS image."); return {}; }
    QByteArray chunk = box(codes[index], png);
    chunk.replace(0,8,codes[index] + chunk.left(4));
    QByteArray result = box("icns",chunk);
    result.replace(0,8,QByteArray("icns") + result.left(4));
    return result;
}

QByteArray profileJp2(QByteArray const& data, QByteArray const& profile)
{
    if (profile.isEmpty()) return data;
    QByteArray result;
    for (qsizetype at = 0; at + 8 <= data.size();) {
        quint32 const size = qFromBigEndian<quint32>(data.constData() + at);
        if (size < 8 || size > quint64(data.size() - at)) return data;
        QByteArray const type = data.mid(at + 4, 4);
        if (type != "jp2h") { result += data.mid(at, size); at += size; continue; }
        QByteArray header;
        for (qsizetype child = at + 8; child + 8 <= at + size;) {
            quint32 const length = qFromBigEndian<quint32>(data.constData() + child);
            if (length < 8 || length > quint64(at + size - child)) return data;
            if (data.mid(child + 4, 4) == "colr") header += box("colr", QByteArray("\x02\x00\x00",3) + profile);
            else header += data.mid(child,length);
            child += length;
        }
        result += box(type,header);
        at += size;
    }
    return result;
}

QByteArray psdProfile(QFile& file)
{
    if (!file.seek(26)) return {};
    QByteArray sizeBytes=file.read(4);
    if(sizeBytes.size()!=4) return {};
    quint32 const colourBytes=qFromBigEndian<quint32>(sizeBytes.constData());
    if(!file.seek(30+qint64(colourBytes))) return {};
    sizeBytes=file.read(4);
    if(sizeBytes.size()!=4) return {};
    quint32 const size=qFromBigEndian<quint32>(sizeBytes.constData());
    if(size>16*1024*1024) return {};
    QByteArray const resources=file.read(size);
    for(qsizetype at=0;at+12<=resources.size();) {
        if(resources.mid(at,4)!="8BIM") break;
        quint16 const id=qFromBigEndian<quint16>(resources.constData()+at+4);
        qsizetype const name=(quint8(resources[at+6])+2)&~qsizetype(1);
        qsizetype const lengthAt=at+6+name;
        if(lengthAt+4>resources.size()) break;
        quint32 const length=qFromBigEndian<quint32>(resources.constData()+lengthAt);
        qsizetype const begin=lengthAt+4;
        if(length>quint64(resources.size()-begin)) break;
        if(id==1039) return resources.mid(begin,length);
        at=begin+length+(length&1);
    }
    return {};
}

}

namespace ImageFiles {
QVector<Format> const& formats()
{
    static QVector<Format> const list = {
        {QStringLiteral("png"), QStringLiteral("PNG"), true, false, true, true, false, {},8,16},
        {QStringLiteral("tga"), QStringLiteral("Targa (TGA)"), true, false},
        {QStringLiteral("tif"), QStringLiteral("TIFF"), true, false, true, true, false, {QStringLiteral("tiff")},8,32,true},
        {QStringLiteral("psd"), QStringLiteral("Photoshop (PSD)"), true, true,true,true,false,{QStringLiteral("psdt")},8,32,true},
        {QStringLiteral("exr"), QStringLiteral("OpenEXR (linear, half/float)"), true, false,true,true,false,{},8,32,true},
        {QStringLiteral("sgi"), QStringLiteral("Silicon Graphics (SGI)"), true, false,true,true,false,{QStringLiteral("rgb"),QStringLiteral("rgba"),QStringLiteral("bw")},8,16},
        {QStringLiteral("dpx"), QStringLiteral("DPX (10-bit)"), false, false,true,true,false,{},0,10},
        {QStringLiteral("jpg"), QStringLiteral("JPEG"), false, false,true,true,false,{QStringLiteral("jpeg"),QStringLiteral("jpe"),QStringLiteral("pjpeg"),QStringLiteral("jps")}},
        {QStringLiteral("bmp"), QStringLiteral("BMP"), true, false,true,true,false,{QStringLiteral("bmp2"),QStringLiteral("bmp3"),QStringLiteral("dib")}},
        {QStringLiteral("gif"), QStringLiteral("GIF"), true, false, true, true, true, {}, 1},
        {QStringLiteral("webp"), QStringLiteral("WebP"), true, false, true, true, true},
        {QStringLiteral("apng"), QStringLiteral("Animated PNG"), true, false, true, true, true,{},8,16},
        {QStringLiteral("heic"), QStringLiteral("HEIC / HEIF"), true, false, true, true, false, {QStringLiteral("heif")},8,12},
        {QStringLiteral("avif"), QStringLiteral("AVIF"), true, false, true, true, true,{},8,12},
        {QStringLiteral("jp2"), QStringLiteral("JPEG 2000"), true, false, true, true, false, {QStringLiteral("j2k"), QStringLiteral("j2c"), QStringLiteral("jpc")},8,16},
        {QStringLiteral("jxl"), QStringLiteral("JPEG XL"), true, false, true, true, true,{},8,32,true},
        {QStringLiteral("ico"), QStringLiteral("Windows Icon"), true, false,true,true,false,{QStringLiteral("icon"),QStringLiteral("cur")}},
        {QStringLiteral("icns"), QStringLiteral("Apple Icon"), true, false},
        {QStringLiteral("qoi"), QStringLiteral("QOI"), true, false},
        {QStringLiteral("dds"), QStringLiteral("DDS"), true, false},
        {QStringLiteral("pbm"), QStringLiteral("Portable Bitmap"), false, false},
        {QStringLiteral("pgm"), QStringLiteral("Portable Greymap"), false, false,true,true,false,{},0,16},
        {QStringLiteral("ppm"), QStringLiteral("Portable Pixmap"), false, false,true,true,false,{},0,16},
        {QStringLiteral("pfm"), QStringLiteral("Portable Float Map"), false, false,true,true,false,{},0,32,true},
        {QStringLiteral("hdr"), QStringLiteral("Radiance HDR"), false, false, true, true, false, {QStringLiteral("rgbe")},0,32,true},
        {QStringLiteral("pcx"), QStringLiteral("PCX"), false, false},
        {QStringLiteral("xpm"), QStringLiteral("XPM"), true, false, true, true, false, {}, 1},
        {QStringLiteral("xbm"), QStringLiteral("XBM"), false, false},
        {QStringLiteral("wbmp"), QStringLiteral("Wireless Bitmap"), false, false},
        {QStringLiteral("svg"), QStringLiteral("SVG"), true, false},
        {QStringLiteral("pam"), QStringLiteral("Portable Arbitrary Map"), true,false,true,true,false,{},8,16},
        {QStringLiteral("pnm"), QStringLiteral("Portable Anymap"), false,false,true,true,false,{},0,16},
        {QStringLiteral("farbfeld"), QStringLiteral("Farbfeld"), true,false,true,true,false,{QStringLiteral("ff")},16,16},
        {QStringLiteral("miff"), QStringLiteral("Magick Image File"), true,false,true,true,false,{},16,32,true},
        {QStringLiteral("mng"), QStringLiteral("Multiple-image Network Graphics"), true,false,true,true,true,{},8,16},
        {QStringLiteral("jng"), QStringLiteral("JPEG Network Graphics"), true,false},
        {QStringLiteral("jls"), QStringLiteral("JPEG-LS"), false,false,true,true,false,{},0,16},
        {QStringLiteral("vbn"), QStringLiteral("VBN texture"), true,false},
        {QStringLiteral("xwd"), QStringLiteral("X Window Dump"), false,false},
        {QStringLiteral("xface"), QStringLiteral("X-Face (48 by 48)"), false,false},
        {QStringLiteral("ras"), QStringLiteral("Sun Raster"), false,false,true,true,false,{QStringLiteral("sun")}},
        {QStringLiteral("viff"), QStringLiteral("Khoros VIFF"), true,false,true,true,false,{QStringLiteral("xv")}},
        {QStringLiteral("pict"), QStringLiteral("PICT"), false,false,true,true,false,{QStringLiteral("pct")}},
        {QStringLiteral("dcx"), QStringLiteral("Multi-page PCX"), false,false},
        {QStringLiteral("fits"), QStringLiteral("Flexible Image Transport"), false,false,true,true,false,{QStringLiteral("fit"),QStringLiteral("fts")},0,32,true},
        {QStringLiteral("vicar"), QStringLiteral("VICAR"), false,false},
        {QStringLiteral("cin"), QStringLiteral("Cineon"), false,false,true,true,false,{},0,10},
        {QStringLiteral("mat"), QStringLiteral("MATLAB image"), false,false,true,true,false,{},0,16},
        {QStringLiteral("palm"), QStringLiteral("Palm Pixmap"), false,false},
        {QStringLiteral("otb"), QStringLiteral("On-the-air Bitmap"), false,false},
        {QStringLiteral("hrz"), QStringLiteral("HRZ (256 by 240)"), false,false},
        {QStringLiteral("mtv"), QStringLiteral("MTV Raytracer"), false,false},
        {QStringLiteral("ipl"), QStringLiteral("IPL image"), false,false},
        {QStringLiteral("cals"), QStringLiteral("CALS monochrome"), false,false,true,true,false,{QStringLiteral("cal")}},
        {QStringLiteral("sixel"), QStringLiteral("SIXEL"), false,false,true,true,false,{QStringLiteral("six")}},
        {QStringLiteral("pcd"), QStringLiteral("Photo CD"), false,false,true,false,false,{QStringLiteral("pcds")}},
        {QStringLiteral("wpg"), QStringLiteral("WordPerfect Graphics"), false,false,true,false},
        {QStringLiteral("dcm"), QStringLiteral("DICOM image"), false,false,true,false},
        {QStringLiteral("svgz"), QStringLiteral("Compressed SVG"), true,false,true,false},
        {QStringLiteral("raw-pixels"),QStringLiteral("Raw pixels (settings required)"),true,false,true,false,false,RawPixels::suffixes()},
        {QStringLiteral("psb"), QStringLiteral("Photoshop Large Document"), true, true, true, false},
        {QStringLiteral("raw"), QStringLiteral("Camera RAW"), false, false, true, false, false,
         {QStringLiteral("cr2"), QStringLiteral("cr3"), QStringLiteral("nef"), QStringLiteral("nrw"), QStringLiteral("arw"), QStringLiteral("dng"), QStringLiteral("raf"), QStringLiteral("orf"), QStringLiteral("rw2"), QStringLiteral("pef"), QStringLiteral("srw"),QStringLiteral("3fr"),QStringLiteral("crw"),QStringLiteral("dcr"),QStringLiteral("erf"),QStringLiteral("fff"),QStringLiteral("iiq"),QStringLiteral("kdc"),QStringLiteral("mdc"),QStringLiteral("mos"),QStringLiteral("mrw"),QStringLiteral("rwl"),QStringLiteral("sr2"),QStringLiteral("srf"),QStringLiteral("sti")}},
    };
    return list;
}

Format const* find(QString const& suffix)
{
    QString const s = suffix.toLower();
    for (Format const& f : formats())
        if (f.suffix == s || f.aliases.contains(s))
            return &f;
    return nullptr;
}

}

QString ImageFiles::fileFilter(bool writing)
{
    QStringList patterns;
    for (Format const& format : formats()) {
        if (writing ? !format.writable : !format.readable) continue;
        patterns << QStringLiteral("*.") + format.suffix;
        for (QString const& alias : format.aliases) patterns << QStringLiteral("*.") + alias;
    }
    patterns.removeDuplicates();
    return QStringLiteral("Images (%1);;All Files (*)").arg(patterns.join(QLatin1Char(' ')));
}

QImage ImageFiles::load(QString const& path, QString* error, int frame)
{
    if (frame < 0) { if (error) *error = QStringLiteral("Invalid image frame."); return {}; }
    if(QFileInfo(path).suffix().compare(QLatin1String("jng"),Qt::CaseInsensitive)==0 && frame==0) return ImageCodec::readJng(path,error);
    if(frame>0)
        if(Format const* known=find(QFileInfo(path).suffix());(known && known->animated) || ImageCodec::isApng(path)) {
            QVector<Frame> const frames=readSequence(path,error);
            if(frame<frames.size()) return frames[frame].image;
            if(error) *error=QStringLiteral("The animation does not contain the requested frame.");
            return {};
        }
    if (Psd::isPsd(path)) {
        QFile file(path);
        if(file.open(QIODevice::ReadOnly)) {
            QByteArray const header=file.read(26);
            bool const native=header.size()==26 && qFromBigEndian<quint16>(header.constData()+22)<=8;
            if(native || ThirdParty::executable(QStringLiteral("imagemagick"),QStringLiteral("magick")).isEmpty()) {
                QImage image=frame==0 ? Psd::read(path,error) : QImage();
                QByteArray const profile=psdProfile(file);
                if(!profile.isEmpty()) image.setColorSpace(QColorSpace::fromIccProfile(profile));
                return image;
            }
        }
    }
    if (QFileInfo(path).suffix().compare(QLatin1String("icns"),Qt::CaseInsensitive)==0) {
        return IconRead::read(path,error);
    }
    QImageReader reader(path);
    reader.setAutoTransform(true);
    if (frame == 0 || reader.jumpToImage(frame)) {
        QImage image = reader.read();
        if (!image.isNull()) {
            if(QFileInfo(path).suffix().compare(QLatin1String("exr"),Qt::CaseInsensitive)==0) {
                QColorSpace const space=exrSpace(path); if(space.isValid()) image.setColorSpace(space);
            }
            if(QFileInfo(path).suffix().compare(QLatin1String("bmp"),Qt::CaseInsensitive)==0) {
                QFile file(path);
                if(file.open(QIODevice::ReadOnly)) {
                    QByteArray const header=file.read(138);
                    if(header.size()==138 && header.startsWith("BM") && qFromLittleEndian<quint32>(header.constData()+14)>=124 && qFromLittleEndian<quint32>(header.constData()+70)==0x4d424544u) {
                        quint32 const offset=qFromLittleEndian<quint32>(header.constData()+126),size=qFromLittleEndian<quint32>(header.constData()+130);
                        if(size>0 && size<16*1024*1024 && qint64(offset)+14+size<=file.size() && file.seek(qint64(offset)+14))
                            image.setColorSpace(QColorSpace::fromIccProfile(file.read(size)));
                    }
                }
            }
            return image;
        }
    }
    QTemporaryDir temp;
    if (!temp.isValid()) { if (error) *error = QStringLiteral("Cannot create an image conversion folder."); return {}; }
    if(!ImageCodec::imageEncoder(QFileInfo(path).suffix().toLower()).isEmpty()) return ImageCodec::readFfmpeg(path,frame,error);
    QString inputPath=QFileInfo(path).absoluteFilePath();
    QString const inputSuffix=QFileInfo(path).suffix().toLower();
    if(inputSuffix==QLatin1String("sixel") || inputSuffix==QLatin1String("six")) {
        QFile input(inputPath);
        if(input.open(QIODevice::ReadOnly) && input.size()>0 && input.size()<2048) {
            QByteArray bytes=input.readAll(); bytes.append(QByteArray(2048-bytes.size(),'\n'));
            inputPath=temp.filePath(QStringLiteral("padded.sixel"));
            if(!writeBytes(inputPath,bytes)) { if(error) *error=QStringLiteral("Cannot prepare the SIXEL decoder input."); return {}; }
        }
    }
    QString const source = inputPath + QStringLiteral("[%1]").arg(frame);
    QString const metadata = temp.filePath(QStringLiteral("metadata.txt"));
    QString const pixels = temp.filePath(QStringLiteral("pixels.raw"));
    if (!run({source, QStringLiteral("-auto-orient"), QStringLiteral("-format"),
              QStringLiteral("%w\n%h\n%z\n%[colorspace]\n%[profiles]\n"), QStringLiteral("info:") + metadata}, error)) return {};
    QFile info(metadata);
    if (!info.open(QIODevice::ReadOnly)) { if (error) *error = info.errorString(); return {}; }
    QList<QByteArray> const fields = info.readAll().split('\n');
    int const width = fields.value(0).toInt(), height = fields.value(1).toInt(), depth = fields.value(2).toInt();
    QString const suffix = QFileInfo(path).suffix().toLower();
    bool const floating = suffix == QLatin1String("exr") || suffix == QLatin1String("pfm") || suffix == QLatin1String("hdr") || suffix == QLatin1String("rgbe") || depth > 16;
    int const bits=floating?32:depth>8?16:8;
    qint64 const limit = qint64(QImageReader::allocationLimit()) * 1024 * 1024;
    qint64 const maximum=limit>0?limit:std::numeric_limits<qint64>::max();
    if (width <= 0 || height <= 0 || qint64(width)>maximum/(bits/2)/height) {
        if (error) *error = QStringLiteral("Image dimensions exceed the image allocation limit.");
        return {};
    }
    qint64 const bytes = qint64(width) * height * (bits/2);
    if (!run({source, QStringLiteral("-auto-orient"),QStringLiteral("-alpha"),QStringLiteral("on"),
              QStringLiteral("-define"),floating?QStringLiteral("quantum:format=floating-point"):QStringLiteral("quantum:format=unsigned"),
              QStringLiteral("-depth"),QString::number(bits),QStringLiteral("-endian"),QStringLiteral("LSB"),QStringLiteral("rgba:")+pixels},error)) return {};
    QImage image(width, height, floating?QImage::Format_RGBA32FPx4:bits==16?QImage::Format_RGBA64:QImage::Format_RGBA8888);
    QFile raw(pixels);
    if (image.isNull() || !raw.open(QIODevice::ReadOnly) || raw.size() != bytes || raw.read(reinterpret_cast<char*>(image.bits()), bytes) != bytes) {
        if (error) *error = QStringLiteral("The image decoder returned incomplete pixels.");
        return {};
    }
    if (fields.value(4).contains("icc")) {
        QString const profile = temp.filePath(QStringLiteral("profile.icc"));
        if (!run({source, QStringLiteral("icc:") + profile}, error)) return {};
        QFile file(profile);
        if (file.open(QIODevice::ReadOnly)) image.setColorSpace(QColorSpace::fromIccProfile(file.readAll()));
    } else {
        image.setColorSpace(fields.value(3).trimmed() == "RGB" ? QColorSpace(QColorSpace::SRgbLinear) : QColorSpace(QColorSpace::SRgb));
    }
    if(suffix==QLatin1String("exr")) {
        QColorSpace const space=exrSpace(path); if(space.isValid()) image.setColorSpace(space);
        image.reinterpretAsFormat(QImage::Format_RGBA32FPx4_Premultiplied);
        image=image.convertToFormat(QImage::Format_RGBA32FPx4);
    }
    return image;
}

bool ImageFiles::canRead(QString const& path)
{
    return !load(path).isNull();
}

bool ImageFiles::saveWithCodec(QImage const& image, QString const& path, QString const& format, QString* error, int quality)
{
    if(format==QLatin1String("sixel")) return ImageCodec::writeSixel(image,path,error);
    if(!ImageCodec::imageEncoder(format).isEmpty()) return ImageCodec::writeFfmpeg(image,path,format,error);
    if(format==QLatin1String("apng")) return ImageCodec::writeApng({{image,100}},path,error,0);
    if (format == QLatin1String("icns") || format == QLatin1String("svg")) {
        QByteArray bytes;
        if(format==QLatin1String("icns")) bytes=icns(image,error);
        else {
            QByteArray png; QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly);
            if(image.save(&buffer,"PNG")) bytes=QStringLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" width=\"%1\" height=\"%2\"><image width=\"%1\" height=\"%2\" xlink:href=\"data:image/png;base64,%3\"/></svg>").arg(image.width()).arg(image.height()).arg(QString::fromLatin1(png.toBase64())).toUtf8();
        }
        QSaveFile file(path);
        if (bytes.isEmpty()) return false;
        bool const ok = file.open(QIODevice::WriteOnly) && file.write(bytes)==bytes.size() && file.commit();
        if (!ok && error) *error = file.errorString();
        return ok;
    }
    QTemporaryDir temp;
    if (!temp.isValid()) { if (error) *error = QStringLiteral("Cannot create an image conversion folder."); return false; }
    QString const pixels = temp.filePath(QStringLiteral("pixels.raw"));
    QString const output = temp.filePath(QStringLiteral("result.")) + format;
    bool const heic = format==QLatin1String("heic");
    QImage const floats = image.convertToFormat(heic ? QImage::Format_RGBA64 : QImage::Format_RGBA32FPx4);
    QFile raw(pixels);
    if (!raw.open(QIODevice::WriteOnly) || raw.write(reinterpret_cast<char const*>(floats.constBits()), floats.sizeInBytes()) != floats.sizeInBytes()) {
        if (error) *error = QStringLiteral("Cannot write image conversion pixels.");
        return false;
    }
    raw.close();
    QStringList args{QStringLiteral("-size"), QStringLiteral("%1x%2").arg(image.width()).arg(image.height()),
        QStringLiteral("-define"), QStringLiteral("quantum:format=floating-point"), QStringLiteral("-depth"), QStringLiteral("32"),
        QStringLiteral("-endian"), QStringLiteral("LSB"), QStringLiteral("rgba:") + pixels,
        QStringLiteral("-set"), QStringLiteral("colorspace"), image.colorSpace() == QColorSpace(QColorSpace::SRgbLinear) ? QStringLiteral("RGB") : QStringLiteral("sRGB")};
    QByteArray const icc = image.colorSpace().iccProfile();
    if (!icc.isEmpty()) {
        QString const profile = temp.filePath(QStringLiteral("profile.icc"));
        if (!writeBytes(profile, icc)) { if (error) *error = QStringLiteral("Cannot write the image colour profile."); return false; }
        args << QStringLiteral("-profile") << profile;
    }
    Format const* const capability=find(format);
    bool const floatOutput = format == QLatin1String("pfm") || format == QLatin1String("hdr") || format == QLatin1String("exr")
        || (capability && capability->floating && image.pixelFormat().typeInterpretation()==QPixelFormat::FloatingPoint);
    args << QStringLiteral("-depth") << (floatOutput ? QStringLiteral("32") : image.depth() > 32 ? QStringLiteral("16") : QStringLiteral("8"));
    if (!floatOutput) args << QStringLiteral("-define") << QStringLiteral("quantum:format=unsigned");
    args << QStringLiteral("-quality") << QString::number(quality);
    if (format == QLatin1String("webp")) args << QStringLiteral("-define") << QStringLiteral("webp:lossless=true");
    if (format == QLatin1String("jxl")) args << QStringLiteral("-define") << QStringLiteral("jxl:distance=0");
    if (format == QLatin1String("dds")) args << QStringLiteral("-define") << QStringLiteral("dds:compression=none");
    args << format + QLatin1Char(':') + output;
    if(heic) {
        QString const profile = icc.isEmpty() ? QStringLiteral("-") : temp.filePath(QStringLiteral("profile.icc"));
        QString const program = ThirdParty::executable(QStringLiteral("heif"),QStringLiteral("eclipse-heif"));
        if(!runProgram(program,{QString::number(image.width()),QString::number(image.height()),image.depth()>32 ? QStringLiteral("12") : QStringLiteral("8"),pixels,output,profile,QString::number(quality)},error)) return false;
    } else if (!run(args, error)) return false;
    QFile result(output);
    QSaveFile destination(path);
    if (!result.open(QIODevice::ReadOnly) || !destination.open(QIODevice::WriteOnly)) { if (error) *error = QStringLiteral("Cannot open the converted image output."); return false; }
    if ((format == QLatin1String("jp2") || format==QLatin1String("jng")) && !icc.isEmpty()) {
        QByteArray const bytes=result.readAll();
        QByteArray const profiled = format==QLatin1String("jng")?ImageCodec::profileJng(bytes,icc):profileJp2(bytes,icc);
        if(profiled.isEmpty()) { if(error) *error=QStringLiteral("Cannot tag the converted image with its colour profile."); return false; }
        bool const ok = destination.write(profiled)==profiled.size() && destination.commit();
        if(!ok && error) *error=destination.errorString();
        return ok;
    }
    while (!result.atEnd()) {
        QByteArray const block = result.read(1024 * 1024);
        if (block.isEmpty() || destination.write(block) != block.size()) { if (error) *error = QStringLiteral("Cannot finish the converted image output."); return false; }
    }
    if (!destination.commit()) { if (error) *error = destination.errorString(); return false; }
    return true;
}
