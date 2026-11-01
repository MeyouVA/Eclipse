#pragma once
#include <QColorSpace>
#include <QFile>
#include <QHash>
#include <QImageReader>
#include <QtEndian>

namespace ImageCodec {
inline quint32 jngCrc(QByteArray const& bytes)
{
    quint32 crc=0xffffffffu;
    for(char byte : bytes) {
        crc^=quint8(byte);
        for(int bit=0;bit<8;++bit) crc=(crc>>1)^((crc&1)?0xedb88320u:0u);
    }
    return crc^0xffffffffu;
}
inline QByteArray jngChunk(QByteArray const& type,QByteArray const& data)
{
    QByteArray length(4,'\0'),checksum(4,'\0');
    qToBigEndian(quint32(data.size()),length.data()); qToBigEndian(jngCrc(type+data),checksum.data());
    return length+type+data+checksum;
}
inline QByteArray profileJng(QByteArray const& bytes,QByteArray const& profile)
{
    QByteArray result=bytes.left(8);
    for(qsizetype at=8;at+12<=bytes.size();) {
        quint32 const count=qFromBigEndian<quint32>(bytes.constData()+at);
        if(count>quint64(bytes.size()-at-12)) return {};
        QByteArray const type=bytes.mid(at+4,4);
        if(type!="iCCP" && type!="sRGB") result+=bytes.mid(at,qsizetype(count)+12);
        if(type=="JHDR") result+=jngChunk("iCCP",QByteArray("ICC\0\0",5)+qCompress(profile).mid(4));
        at+=qsizetype(count)+12;
    }
    return result;
}
inline QImage readJng(QString const& path,QString* error)
{
    auto fail=[&](QString const& reason) { if(error) *error=reason; return QImage(); };
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly) || file.size()>256*1024*1024) return fail(QStringLiteral("Cannot read the JNG file or its compressed data exceeds 256 MiB."));
    QByteArray const bytes=file.readAll();
    if(!bytes.startsWith(QByteArray("\x8bJNG\r\n\x1a\n",8))) return fail(QStringLiteral("Invalid JNG signature."));
    QHash<QByteArray,QByteArray> parts;
    bool ended=false,separated=false;
    for(qsizetype at=8;at+12<=bytes.size();) {
        quint32 const count=qFromBigEndian<quint32>(bytes.constData()+at);
        if(count>quint64(bytes.size()-at-12)) return fail(QStringLiteral("Truncated JNG chunk."));
        QByteArray const type=bytes.mid(at+4,4),data=bytes.mid(at+8,count);
        if(jngCrc(type+data)!=qFromBigEndian<quint32>(bytes.constData()+at+8+count)) return fail(QStringLiteral("Invalid JNG chunk checksum."));
        if(type=="JSEP") separated=true;
        if(!(type=="JDAT" && separated)) parts[type]+=data;
        at+=qsizetype(count)+12;
        if(type=="IEND") { ended=true; break; }
    }
    QByteArray const header=parts.value("JHDR");
    if(!ended || header.size()!=16 || parts.value("JDAT").isEmpty()) return fail(QStringLiteral("Incomplete JNG image."));
    quint32 const width=qFromBigEndian<quint32>(header.constData()),height=qFromBigEndian<quint32>(header.constData()+4);
    quint64 const limit=quint64(QImageReader::allocationLimit())*1024*1024;
    if(!width || !height || width>65535 || height>65535 || (limit && quint64(width)*height*8>limit)) return fail(QStringLiteral("JNG dimensions exceed the image allocation limit."));
    QImage image=QImage::fromData(parts.value("JDAT"),"JPEG").convertToFormat(QImage::Format_RGBA64);
    if(image.size()!=QSize(int(width),int(height))) return fail(QStringLiteral("Unsupported JNG JPEG data or mismatched dimensions."));
    int const colour=quint8(header[8]);
    if(colour!=8 && colour!=10 && colour!=12 && colour!=14) return fail(QStringLiteral("Unsupported JNG colour type."));
    if(colour==12 || colour==14) {
        QImage alpha;
        if(header[13]==0) {
            QByteArray ihdr=header.left(8); ihdr.append(header[12]); ihdr.append(char(0)); ihdr.append(header[13]); ihdr.append(header[14]); ihdr.append(header[15]);
            alpha=QImage::fromData(QByteArray("\x89PNG\r\n\x1a\n",8)+jngChunk("IHDR",ihdr)+jngChunk("IDAT",parts.value("IDAT"))+jngChunk("IEND",{}),"PNG");
        } else if(quint8(header[13])==8) alpha=QImage::fromData(parts.value("JDAA"),"JPEG");
        if(alpha.size()!=image.size()) return fail(QStringLiteral("Unsupported or incomplete JNG alpha data."));
        alpha=alpha.convertToFormat(QImage::Format_RGBA64);
        for(int y=0;y<image.height();++y) {
            auto* row=reinterpret_cast<QRgba64*>(image.scanLine(y));
            auto const* mask=reinterpret_cast<QRgba64 const*>(alpha.constScanLine(y));
            for(int x=0;x<image.width();++x) row[x].setAlpha(mask[x].red());
        }
    }
    QByteArray info(13,'\0'); qToBigEndian(quint32(1),info.data()); qToBigEndian(quint32(1),info.data()+4); info[8]=8; info[9]=2;
    QByteArray metadata;
    for(QByteArray const& type : {QByteArray("cHRM"),QByteArray("gAMA"),QByteArray("iCCP"),QByteArray("sRGB")})
        if(parts.contains(type)) metadata+=jngChunk(type,parts.value(type));
    if(!metadata.isEmpty()) {
        QImage const profile=QImage::fromData(QByteArray("\x89PNG\r\n\x1a\n",8)+jngChunk("IHDR",info)+metadata+jngChunk("IDAT",qCompress(QByteArray(4,'\0')).mid(4))+jngChunk("IEND",{}),"PNG");
        if(profile.colorSpace().isValid()) image.setColorSpace(profile.colorSpace());
    }
    return image;
}
}
