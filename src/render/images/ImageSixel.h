#pragma once
#include <QImage>
#include <QSaveFile>
#include <array>

namespace ImageCodec {
inline bool writeSixel(QImage const& source,QString const& path,QString* error)
{
    QImage const image=source.convertToFormat(QImage::Format_RGB32).convertToFormat(QImage::Format_Indexed8,Qt::DiffuseDither|Qt::ColorOnly);
    QByteArray data="\x1bP0;0;0q\"1;1;"+QByteArray::number(image.width())+';'+QByteArray::number(image.height());
    for(int colour=0;colour<image.colorCount();++colour) {
        QRgb const rgb=image.color(colour);
        data+='#'+QByteArray::number(colour)+";2;"+QByteArray::number((qRed(rgb)*100+127)/255)+';'+QByteArray::number((qGreen(rgb)*100+127)/255)+';'+QByteArray::number((qBlue(rgb)*100+127)/255);
    }
    for(int y=0;y<image.height();y+=6) {
        std::array<bool,256> used{};
        for(int dy=0;dy<6 && y+dy<image.height();++dy)
            for(int x=0;x<image.width();++x) used[image.constScanLine(y+dy)[x]]=true;
        for(int colour=0;colour<image.colorCount();++colour) if(used[size_t(colour)]) {
            data+='#'+QByteArray::number(colour);
            QByteArray row(image.width(),'?');
            for(int x=0;x<image.width();++x) {
                int bits=0;
                for(int dy=0;dy<6 && y+dy<image.height();++dy) if(image.constScanLine(y+dy)[x]==colour) bits|=1<<dy;
                row[x]=char(63+bits);
            }
            while(row.endsWith('?')) row.chop(1);
            for(qsizetype at=0;at<row.size();) {
                qsizetype end=at+1; while(end<row.size() && row[end]==row[at]) ++end;
                if(end-at>3) data+='!'+QByteArray::number(end-at)+row[at]; else data+=row.mid(at,end-at);
                at=end;
            }
            data+='$';
        }
        if(y+6<image.height()) data+='-';
    }
    data+="\x1b\\";
    QSaveFile file(path);
    bool const okay=file.open(QIODevice::WriteOnly) && file.write(data)==data.size() && file.commit();
    if(!okay && error) *error=file.errorString();
    return okay;
}
}
