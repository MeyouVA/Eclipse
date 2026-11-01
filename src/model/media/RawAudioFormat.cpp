#include "model/media/RawAudioFormat.h"
#include <QFileInfo>

QVector<RawAudio::Format> const& RawAudio::formats()
{
    static QVector<Format> const list={
        {QStringLiteral("s8"),QStringLiteral("Signed 8-bit PCM"),QStringLiteral("s8"),{},1,{QStringLiteral("sb")}},
        {QStringLiteral("u8"),QStringLiteral("Unsigned 8-bit PCM"),QStringLiteral("u8"),{},1,{QStringLiteral("ub")}},
        {QStringLiteral("s16le"),QStringLiteral("Signed 16-bit PCM, little endian"),QStringLiteral("s16le"),{},2,{QStringLiteral("sw")}},
        {QStringLiteral("s16be"),QStringLiteral("Signed 16-bit PCM, big endian"),QStringLiteral("s16be"),{},2,{}},
        {QStringLiteral("u16le"),QStringLiteral("Unsigned 16-bit PCM, little endian"),QStringLiteral("u16le"),{},2,{QStringLiteral("uw")}},
        {QStringLiteral("u16be"),QStringLiteral("Unsigned 16-bit PCM, big endian"),QStringLiteral("u16be"),{},2,{}},
        {QStringLiteral("s24le"),QStringLiteral("Signed 24-bit PCM, little endian"),QStringLiteral("s24le"),{},3,{}},
        {QStringLiteral("s24be"),QStringLiteral("Signed 24-bit PCM, big endian"),QStringLiteral("s24be"),{},3,{}},
        {QStringLiteral("u24le"),QStringLiteral("Unsigned 24-bit PCM, little endian"),QStringLiteral("u24le"),{},3,{}},
        {QStringLiteral("u24be"),QStringLiteral("Unsigned 24-bit PCM, big endian"),QStringLiteral("u24be"),{},3,{}},
        {QStringLiteral("s32le"),QStringLiteral("Signed 32-bit PCM, little endian"),QStringLiteral("s32le"),{},4,{}},
        {QStringLiteral("s32be"),QStringLiteral("Signed 32-bit PCM, big endian"),QStringLiteral("s32be"),{},4,{}},
        {QStringLiteral("u32le"),QStringLiteral("Unsigned 32-bit PCM, little endian"),QStringLiteral("u32le"),{},4,{}},
        {QStringLiteral("u32be"),QStringLiteral("Unsigned 32-bit PCM, big endian"),QStringLiteral("u32be"),{},4,{}},
        {QStringLiteral("f32le"),QStringLiteral("32-bit float, little endian"),QStringLiteral("f32le"),{},4,{}},
        {QStringLiteral("f32be"),QStringLiteral("32-bit float, big endian"),QStringLiteral("f32be"),{},4,{}},
        {QStringLiteral("f64le"),QStringLiteral("64-bit float, little endian"),QStringLiteral("f64le"),{},8,{}},
        {QStringLiteral("f64be"),QStringLiteral("64-bit float, big endian"),QStringLiteral("f64be"),{},8,{}},
        {QStringLiteral("alaw"),QStringLiteral("G.711 A-law"),QStringLiteral("alaw"),{},1,{QStringLiteral("al")}},
        {QStringLiteral("mulaw"),QStringLiteral("G.711 mu-law"),QStringLiteral("mulaw"),{},1,{QStringLiteral("ul")}},
        {QStringLiteral("vidc"),QStringLiteral("VIDC PCM"),QStringLiteral("u8"),QStringLiteral("pcm_vidc"),1,{}},
        {QStringLiteral("g726"),QStringLiteral("G.726, big-endian codes"),QStringLiteral("g726"),{},0,{},true},
        {QStringLiteral("g726le"),QStringLiteral("G.726, little-endian codes"),QStringLiteral("g726le"),{},0,{},true}
    };
    return list;
}

RawAudio::Format const* RawAudio::format(QString const& idOrSuffix)
{
    QString const key=idOrSuffix.toLower();
    for(auto const& item : formats()) if(item.id==key || item.aliases.contains(key)) return &item;
    return nullptr;
}

QStringList RawAudio::suffixes()
{
    QStringList result{QStringLiteral("raw"),QStringLiteral("pcm")};
    for(auto const& item : formats()) { result << item.id; result+=item.aliases; }
    result.removeDuplicates();
    return result;
}

bool RawAudio::isRaw(QString const& path)
{
    return suffixes().contains(QFileInfo(path).suffix().toLower());
}
