#include "render/images/ImageFiles.h"
#include <QBuffer>
#include <QColorSpace>
#include <QDataStream>
#include <QFile>
#include <QTemporaryDir>

QByteArray ImageFiles::encodeEmbedded(QImage const& image)
{
    QByteArray bytes;
    if (image.pixelFormat().typeInterpretation()!=QPixelFormat::FloatingPoint) {
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer,"PNG");
        return bytes;
    }
    QImage const pixels=image.convertToFormat(QImage::Format_RGBA32FPx4);
    QByteArray const profile=image.colorSpace().iccProfile();
    quint32 const count=profile.isEmpty()?15:16;
    quint32 const bits=8+2+count*12+4, samples=bits+8, resolution=samples+8, data=resolution+16;
    quint32 const length=quint32(pixels.sizeInBytes()), profileAt=data+length;
    QDataStream out(&bytes,QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::LittleEndian);
    out.writeRawData("II",2);
    out << quint16(42) << quint32(8) << quint16(count);
    auto entry=[&](quint16 tag,quint16 type,quint32 n,quint32 value) { out << tag << type << n << value; };
    entry(256,4,1,quint32(image.width()));
    entry(257,4,1,quint32(image.height()));
    entry(258,3,4,bits);
    entry(259,3,1,1);
    entry(262,3,1,2);
    entry(273,4,1,data);
    entry(277,3,1,4);
    entry(278,4,1,quint32(image.height()));
    entry(279,4,1,length);
    entry(282,5,1,resolution);
    entry(283,5,1,resolution+8);
    entry(284,3,1,1);
    entry(296,3,1,2);
    entry(338,3,1,2);
    entry(339,3,4,samples);
    if(!profile.isEmpty()) entry(34675,7,quint32(profile.size()),profileAt);
    out << quint32(0);
    for(int i=0;i<4;++i) out << quint16(32);
    for(int i=0;i<4;++i) out << quint16(3);
    out << quint32(72) << quint32(1) << quint32(72) << quint32(1);
    bytes.append(reinterpret_cast<char const*>(pixels.constBits()),pixels.sizeInBytes());
    bytes.append(profile);
    return bytes;
}

QImage ImageFiles::decodeEmbedded(QByteArray const& bytes)
{
    QImage image=QImage::fromData(bytes);
    if(!image.isNull() || bytes.isEmpty()) return image;
    QTemporaryDir folder;
    if(!folder.isValid()) return {};
    QString const path=folder.filePath(QStringLiteral("picture.")+embeddedSuffix(bytes));
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size()) return {};
    file.close();
    return load(path);
}

QString ImageFiles::embeddedSuffix(QByteArray const& bytes)
{
    return bytes.startsWith(QByteArray("II*\0",4)) || bytes.startsWith(QByteArray("MM\0*",4)) ? QStringLiteral("tif") : QStringLiteral("png");
}
