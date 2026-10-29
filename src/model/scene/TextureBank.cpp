// Eclipse - textures of your own: the bank of them.
#include "model/scene/TextureBank.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QHash>
#include <QMutex>

#include <algorithm>
#include <cmath>

namespace {

QHash<int, TextureBank::Entry>& bank()
{
    static QHash<int, TextureBank::Entry> entries;
    return entries;
}

// Renders on several threads read the bank while the window may add to it.
QMutex& bankLock()
{
    static QMutex lock;
    return lock;
}

}

QImage TextureBank::normalised(QImage const& grain)
{
    if (grain.isNull())
        return QImage();
    QImage grey = grain.convertToFormat(QImage::Format_Grayscale8);
    if (grey.height() != kStripHeight || grey.width() < kStripHeight || grey.width() > 1024) {
        int const wide = std::clamp(int(std::lround(double(grey.width()) * kStripHeight / std::max(1, grey.height()))), kStripHeight, 1024);
        grey = grey.scaled(wide, kStripHeight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_Grayscale8);
    }
    return grey;
}

int TextureBank::add(QString const& name, QImage const& grain, double edge)
{
    QMutexLocker const locked(&bankLock());
    QImage const grey = normalised(grain);
    if (grey.isNull())
        return 0;
    edge = std::clamp(std::round(edge * 100.0) / 100.0, 0.0, 1.0);
    // Its number: from the grain itself, row by row, and how its edge is.
    QCryptographicHash hash(QCryptographicHash::Sha1);
    for (int y = 0; y < grey.height(); ++y)
        hash.addData(QByteArrayView(reinterpret_cast<char const*>(grey.constScanLine(y)), grey.width()));
    hash.addData(QByteArray::number(grey.width()) + ' ' + QByteArray::number(int(std::lround(edge * 100.0))));
    QByteArray const digest = hash.result();
    quint32 number = 0;
    for (int i = 0; i < 4; ++i)
        number = (number << 8) | quint8(digest[i]);
    int const id = kFirst + int(number % 2000000000u);
    if (!bank().contains(id)) {
        Entry entry;
        entry.id = id;
        entry.name = name.trimmed().isEmpty() ? QStringLiteral("Texture") : name.trimmed();
        entry.grain = grey;
        entry.edge = edge;
        bank().insert(id, entry);
    }
    return id;
}

void TextureBank::remove(int id)
{
    QMutexLocker const locked(&bankLock());
    bank().remove(id);
}

void TextureBank::rename(int id, QString const& name)
{
    QMutexLocker const locked(&bankLock());
    auto found = bank().find(id);
    if (found != bank().end() && !name.trimmed().isEmpty())
        found->name = name.trimmed();
}

bool TextureBank::contains(int id)
{
    QMutexLocker const locked(&bankLock());
    return bank().contains(id);
}

TextureBank::Entry TextureBank::entry(int id)
{
    QMutexLocker const locked(&bankLock());
    return bank().value(id);
}

QVector<TextureBank::Entry> TextureBank::all()
{
    QMutexLocker const locked(&bankLock());
    QVector<Entry> entries;
    for (Entry const& entry : std::as_const(bank()))
        entries.push_back(entry);
    std::sort(entries.begin(), entries.end(), [](Entry const& a, Entry const& b) {
        int const order = a.name.compare(b.name, Qt::CaseInsensitive);
        return order != 0 ? order < 0 : a.id < b.id;
    });
    return entries;
}

QString TextureBank::grainToText(QImage const& grain)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    grain.save(&buffer, "PNG");
    return QString::fromLatin1(bytes.toBase64());
}

QImage TextureBank::grainFromText(QString const& text)
{
    QImage image;
    image.loadFromData(QByteArray::fromBase64(text.toLatin1()), "PNG");
    return image;
}
