#include "render/images/ImageFiles.h"
#include "render/images/ImageCodecProcess.h"
#include "render/images/ImageApng.h"
#include "platform/ThirdParty.h"
#include "model/scene/PsdRead.h"

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

namespace {

using ImageCodec::run;
using ImageCodec::runProgram;

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
        {QStringLiteral("sgi"), QStringLiteral("Silicon Graphics (SGI)"), true, false,true,true,false,{},8,16},
        {QStringLiteral("dpx"), QStringLiteral("DPX (10-bit)"), false, false,true,true,false,{},0,10},
        {QStringLiteral("jpg"), QStringLiteral("JPEG"), false, false,true,true,false,{QStringLiteral("jpeg")}},
        {QStringLiteral("bmp"), QStringLiteral("BMP"), true, false},
        {QStringLiteral("gif"), QStringLiteral("GIF"), true, false, true, true, true, {}, 1},
        {QStringLiteral("webp"), QStringLiteral("WebP"), true, false, true, true, true},
        {QStringLiteral("apng"), QStringLiteral("Animated PNG"), true, false, true, true, true,{},8,16},
        {QStringLiteral("heic"), QStringLiteral("HEIC / HEIF"), true, false, true, true, false, {QStringLiteral("heif")},8,12},
        {QStringLiteral("avif"), QStringLiteral("AVIF"), true, false, true, true, true,{},8,12},
        {QStringLiteral("jp2"), QStringLiteral("JPEG 2000"), true, false, true, true, false, {QStringLiteral("j2k"), QStringLiteral("j2c"), QStringLiteral("jpc")},8,16},
        {QStringLiteral("jxl"), QStringLiteral("JPEG XL"), true, false, true, true, true,{},8,32,true},
        {QStringLiteral("ico"), QStringLiteral("Windows Icon"), true, false},
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
        {QStringLiteral("psb"), QStringLiteral("Photoshop Large Document"), true, true, true, false},
        {QStringLiteral("raw"), QStringLiteral("Camera RAW"), false, false, true, false, false,
         {QStringLiteral("cr2"), QStringLiteral("cr3"), QStringLiteral("nef"), QStringLiteral("nrw"), QStringLiteral("arw"), QStringLiteral("dng"), QStringLiteral("raf"), QStringLiteral("orf"), QStringLiteral("rw2"), QStringLiteral("pef"), QStringLiteral("srw")}},
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
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            QByteArray const data=file.readAll(); QImage best;
            if (data.startsWith("icns"))
                for(qsizetype at=8;at+8<=data.size();) {
                    quint32 const size=qFromBigEndian<quint32>(data.constData()+at+4);
                    if(size<8 || size>quint64(data.size()-at)) break;
                    QImage const decoded=QImage::fromData(data.mid(at+8,size-8),"PNG");
                    if(decoded.width()>best.width()) best=decoded;
                    at+=size;
                }
            if(!best.isNull()) return best;
        }
    }
    QImageReader reader(path);
    reader.setAutoTransform(true);
    if (frame == 0 || reader.jumpToImage(frame)) {
        QImage image = reader.read();
        if (!image.isNull()) {
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
    QString const source = QFileInfo(path).absoluteFilePath() + QStringLiteral("[%1]").arg(frame);
    QString const metadata = temp.filePath(QStringLiteral("metadata.txt"));
    QString const pixels = temp.filePath(QStringLiteral("pixels.raw"));
    if (!run({source, QStringLiteral("-auto-orient"), QStringLiteral("-alpha"), QStringLiteral("on"), QStringLiteral("-format"),
              QStringLiteral("%w\n%h\n%z\n%[colorspace]\n%[profiles]\n"), QStringLiteral("-write"), QStringLiteral("info:") + metadata,
              QStringLiteral("-define"), QStringLiteral("quantum:format=floating-point"), QStringLiteral("-depth"), QStringLiteral("32"),
              QStringLiteral("-endian"), QStringLiteral("LSB"), QStringLiteral("rgba:") + pixels}, error)) return {};
    QFile info(metadata);
    if (!info.open(QIODevice::ReadOnly)) { if (error) *error = info.errorString(); return {}; }
    QList<QByteArray> const fields = info.readAll().split('\n');
    int const width = fields.value(0).toInt(), height = fields.value(1).toInt(), depth = fields.value(2).toInt();
    qint64 const bytes = qint64(width) * height * 16;
    qint64 const limit = qint64(QImageReader::allocationLimit()) * 1024 * 1024;
    if (width <= 0 || height <= 0 || bytes <= 0 || (limit > 0 && bytes > limit)) {
        if (error) *error = QStringLiteral("Image dimensions exceed the image allocation limit.");
        return {};
    }
    QImage image(width, height, QImage::Format_RGBA32FPx4);
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
    QString const suffix = QFileInfo(path).suffix().toLower();
    if(suffix==QLatin1String("exr")) {
        image.reinterpretAsFormat(QImage::Format_RGBA32FPx4_Premultiplied);
        image=image.convertToFormat(QImage::Format_RGBA32FPx4);
    }
    bool const floating = suffix == QLatin1String("exr") || suffix == QLatin1String("pfm") || suffix == QLatin1String("hdr") || suffix == QLatin1String("rgbe") || depth > 16;
    return floating ? image : image.convertToFormat(depth > 8 ? QImage::Format_RGBA64 : QImage::Format_RGBA8888);
}

bool ImageFiles::canRead(QString const& path)
{
    return !load(path).isNull();
}

bool ImageFiles::saveWithCodec(QImage const& image, QString const& path, QString const& format, QString* error, int quality)
{
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
    if (format == QLatin1String("jp2") && !icc.isEmpty()) {
        QByteArray const profiled = profileJp2(result.readAll(),icc);
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
