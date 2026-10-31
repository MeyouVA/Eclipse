#include "render/images/ImageApng.h"
#include <QBuffer>
#include <QColorSpace>
#include <QFile>
#include <QImageReader>
#include <QPainter>
#include <QSaveFile>
#include <QtEndian>
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

namespace {

QByteArray const signature("\x89PNG\r\n\x1a\n",8);

quint32 crc(QByteArray const& bytes)
{
    static auto const table=[] {
        std::array<quint32,256> values{};
        for(quint32 i=0;i<256;++i) {
            quint32 c=i;
            for(int bit=0;bit<8;++bit) c=(c>>1)^((c&1)?0xedb88320u:0u);
            values[i]=c;
        }
        return values;
    }();
    quint32 value=0xffffffffu;
    for(char byte : bytes) value=table[(value^quint8(byte))&255]^(value>>8);
    return value^0xffffffffu;
}

QByteArray number(quint32 value)
{
    QByteArray bytes(4,'\0'); qToBigEndian(value,bytes.data()); return bytes;
}

QByteArray chunk(QByteArray const& type,QByteArray const& data)
{
    QByteArray const body=type+data;
    return number(quint32(data.size()))+body+number(crc(body));
}

struct Chunk { QByteArray type,data; };

QVector<Chunk> chunks(QByteArray const& bytes,QString* error)
{
    QVector<Chunk> result;
    if(!bytes.startsWith(signature)) { if(error) *error=QStringLiteral("Invalid PNG signature."); return {}; }
    for(qsizetype at=8;at+12<=bytes.size();) {
        quint32 const length=qFromBigEndian<quint32>(bytes.constData()+at);
        if(length>quint64(bytes.size()-at-12)) break;
        QByteArray const type=bytes.mid(at+4,4),data=bytes.mid(at+8,length);
        quint32 const expected=qFromBigEndian<quint32>(bytes.constData()+at+8+length);
        if(crc(type+data)!=expected) break;
        result.push_back({type,data});
        at+=12+length;
        if(type=="IEND") return result;
    }
    if(error) *error=QStringLiteral("The PNG has an incomplete chunk or invalid checksum.");
    return {};
}

}

bool ImageCodec::isApng(QString const& path)
{
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly) || file.read(8)!=signature) return false;
    while(!file.atEnd()) {
        QByteArray const header=file.read(8);
        if(header.size()!=8) return false;
        quint32 const length=qFromBigEndian<quint32>(header.constData());
        if(qint64(length)+4>file.size()-file.pos()) return false;
        QByteArray const type=header.mid(4,4);
        if(type=="acTL") return length==8;
        if(type=="IDAT" || type=="IEND") return false;
        if(!file.seek(file.pos()+qint64(length)+4)) return false;
    }
    return false;
}

bool ImageCodec::writeApng(QVector<ImageFiles::Frame> const& frames,QString const& path,QString* error,int loops)
{
    if(frames.isEmpty()) { if(error) *error=QStringLiteral("There are no animation frames."); return false; }
    QSaveFile file(path);
    if(!file.open(QIODevice::WriteOnly)) { if(error) *error=file.errorString(); return false; }
    auto put=[&](QByteArray const& bytes) { return file.write(bytes)==bytes.size(); };
    if(!put(signature)) return false;
    bool const deep=std::any_of(frames.cbegin(),frames.cend(),[](auto const& f) { return f.image.depth()>32; });
    QSize const size=frames.first().image.size();
    QColorSpace const space=frames.first().image.colorSpace();
    quint32 sequence=0;
    for(qsizetype i=0;i<frames.size();++i) {
        QImage image=frames[i].image;
        if(image.isNull() || image.size()!=size) { if(error) *error=QStringLiteral("Animation frame dimensions must match."); return false; }
        if(space.isValid() && image.colorSpace().isValid() && image.colorSpace()!=space) image.convertToColorSpace(space);
        image=image.convertToFormat(deep?QImage::Format_RGBA64:QImage::Format_RGBA8888);
        QByteArray png; QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly);
        if(!image.save(&buffer,"PNG")) { if(error) *error=QStringLiteral("Cannot encode an APNG frame."); return false; }
        auto const parts=chunks(png,error);
        if(parts.isEmpty()) return false;
        if(i==0) {
            if(!put(chunk("IHDR",parts.first().data))) return false;
            for(auto const& part : parts)
                if(part.type!="IHDR" && part.type!="IDAT" && part.type!="IEND")
                    if(!put(chunk(part.type,part.data))) return false;
            if(!put(chunk("acTL",number(quint32(frames.size()))+number(quint32(std::max(0,loops)))))) return false;
        }
        int delay=std::max(0,frames[i].delayMs),denominator=1000;
        int const divisor=std::gcd(delay,denominator); delay/=divisor; denominator/=divisor;
        if(delay>65535) { if(error) *error=QStringLiteral("An APNG frame delay is too long."); return false; }
        QByteArray control=number(sequence++)+number(quint32(size.width()))+number(quint32(size.height()))+number(0)+number(0);
        QByteArray timing(6,'\0'); qToBigEndian(quint16(delay),timing.data()); qToBigEndian(quint16(denominator),timing.data()+2);
        if(!put(chunk("fcTL",control+timing))) return false;
        for(auto const& part : parts)
            if(part.type=="IDAT") {
                if(!put(i==0?chunk("IDAT",part.data):chunk("fdAT",number(sequence++)+part.data))) return false;
            }
    }
    if(!put(chunk("IEND",{})) || !file.commit()) { if(error) *error=file.errorString(); return false; }
    return true;
}

QVector<ImageFiles::Frame> ImageCodec::readApng(QString const& path,QString* error)
{
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly)) { if(error) *error=file.errorString(); return {}; }
    auto const parts=chunks(file.readAll(),error);
    if(parts.isEmpty()) return {};
    auto fail=[&](char const* text) { if(error) *error=QString::fromLatin1(text); return QVector<ImageFiles::Frame>(); };
    QByteArray header,metadata,encoded;
    QImage canvas;
    QRect area;
    int delay=0,dispose=0,blend=0;
    bool controlled=false,seenData=false;
    quint32 expectedSequence=0,announced=0;
    QVector<ImageFiles::Frame> frames;
    auto finish=[&]() {
        if(!controlled) return true;
        QByteArray local=header; qToBigEndian(quint32(area.width()),local.data()); qToBigEndian(quint32(area.height()),local.data()+4);
        QImage const decoded=QImage::fromData(signature+chunk("IHDR",local)+metadata+chunk("IDAT",encoded)+chunk("IEND",{}),"PNG");
        if(decoded.isNull()) return false;
        if(canvas.isNull()) {
            canvas=QImage(int(qFromBigEndian<quint32>(header.constData())),int(qFromBigEndian<quint32>(header.constData()+4)),decoded.depth()>32?QImage::Format_RGBA64_Premultiplied:QImage::Format_ARGB32_Premultiplied);
            canvas.fill(Qt::transparent); canvas.setColorSpace(decoded.colorSpace());
        }
        if(canvas.isNull()) return false;
        QImage const before=canvas;
        { QPainter painter(&canvas); painter.setCompositionMode(blend==0?QPainter::CompositionMode_Source:QPainter::CompositionMode_SourceOver); painter.drawImage(area.topLeft(),decoded); }
        frames.push_back({canvas,delay});
        if(dispose==1) { QPainter painter(&canvas); painter.setCompositionMode(QPainter::CompositionMode_Source); painter.fillRect(area,Qt::transparent); }
        else if(dispose==2) canvas=before;
        return true;
    };
    for(auto const& part : parts) {
        if(part.type=="IHDR") { if(part.data.size()!=13 || !header.isEmpty()) return fail("Invalid PNG image header."); header=part.data; }
        else if(part.type=="acTL") {
            if(part.data.size()!=8) return fail("Invalid APNG animation control.");
            announced=qFromBigEndian<quint32>(part.data.constData());
            if(announced<1 || announced>10000) return fail("Invalid or excessive APNG frame count.");
        } else if(part.type=="fcTL") {
            if(header.size()!=13 || part.data.size()!=26 || !announced || !finish()) return fail("Invalid APNG frame control.");
            if(qFromBigEndian<quint32>(part.data.constData())!=expectedSequence++) return fail("APNG frame sequence is incomplete.");
            quint32 const w=qFromBigEndian<quint32>(part.data.constData()+4),h=qFromBigEndian<quint32>(part.data.constData()+8),x=qFromBigEndian<quint32>(part.data.constData()+12),y=qFromBigEndian<quint32>(part.data.constData()+16);
            quint32 const fullW=qFromBigEndian<quint32>(header.constData()),fullH=qFromBigEndian<quint32>(header.constData()+4);
            qint64 const limit=qint64(QImageReader::allocationLimit())*1024*1024;
            if(!w || !h || quint64(x)+w>fullW || quint64(y)+h>fullH || fullW>32768 || fullH>32768 || (limit>0 && quint64(fullW)*fullH*8>quint64(limit))) return fail("APNG dimensions exceed the image allocation limit.");
            area=QRect(int(x),int(y),int(w),int(h));
            quint16 const numerator=qFromBigEndian<quint16>(part.data.constData()+20),denominator=qFromBigEndian<quint16>(part.data.constData()+22);
            delay=int(std::lround(double(numerator)*1000/(denominator?denominator:100)));
            dispose=quint8(part.data[24]); blend=quint8(part.data[25]);
            if(dispose>2 || blend>1) return fail("Invalid APNG blend or disposal mode.");
            controlled=true; encoded.clear();
        } else if(part.type=="IDAT") { seenData=true; if(controlled) encoded+=part.data; }
        else if(part.type=="fdAT") {
            seenData=true;
            if(!controlled || part.data.size()<4 || qFromBigEndian<quint32>(part.data.constData())!=expectedSequence++) return fail("Invalid APNG frame data.");
            encoded+=part.data.mid(4);
        } else if(part.type=="IEND") { if(!finish()) return fail("Cannot decode an APNG frame."); }
        else if(!seenData) metadata+=chunk(part.type,part.data);
    }
    if(announced && frames.size()!=announced) return fail("The APNG has missing animation frames.");
    if(!announced) { QImage const image=ImageFiles::load(path,error); return image.isNull()?QVector<ImageFiles::Frame>():QVector<ImageFiles::Frame>{{image,0}}; }
    return frames;
}
