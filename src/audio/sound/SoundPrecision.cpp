#include "audio/sound/Sound.h"
#include <QDataStream>
#include <QSaveFile>
#include <limits>

bool Sound::writePreciseWav(QString const& path, QVector<double> const& stereo, int rate, QString* error)
{
    auto fail=[&](QString const& why) { if(error) *error=why; return false; };
    if(rate<8000 || rate>192000 || stereo.size()%2 || quint64(stereo.size())> (std::numeric_limits<quint32>::max()-36u)/sizeof(double))
        return fail(QStringLiteral("The audio rate, channel data or WAV size is invalid."));
    QSaveFile file(path);
    if(!file.open(QIODevice::WriteOnly)) return fail(file.errorString());
    quint32 const bytes=quint32(stereo.size()*sizeof(double));
    QDataStream stream(&file); stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData("RIFF",4); stream<<quint32(36+bytes); stream.writeRawData("WAVEfmt ",8);
    stream<<quint32(16)<<quint16(3)<<quint16(2)<<quint32(rate)<<quint32(rate*16)<<quint16(16)<<quint16(64);
    stream.writeRawData("data",4); stream<<bytes;
    if(file.write(reinterpret_cast<char const*>(stereo.constData()),bytes)!=bytes || stream.status()!=QDataStream::Ok || !file.commit()) return fail(file.errorString());
    return true;
}
