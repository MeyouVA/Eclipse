// Eclipse - the zip reader, with a deflate unpacker of its own.
#include "ui/io/Zip.h"

#include <array>
#include <vector>

namespace {

struct Input {
    QByteArray const& data;
    qsizetype pos = 0;
    quint32 bits = 0;
    int count = 0;
    bool broken = false;
    int bit()
    {
        if (count == 0) {
            if (pos >= data.size()) {
                broken = true;
                return 0;
            }
            bits = quint8(data[pos++]);
            count = 8;
        }
        int const b = int(bits & 1u);
        bits >>= 1;
        --count;
        return b;
    }
    // So many bits, the first the lowest.
    int take(int n)
    {
        int v = 0;
        for (int i = 0; i < n; ++i)
            v |= bit() << i;
        return v;
    }
};

// A canonical Huffman code, from the length of each symbol's code.
struct Huffman {
    std::array<int, 16> counts{};
    std::vector<int> symbols;
    void build(int const* lengths, int n)
    {
        counts.fill(0);
        for (int i = 0; i < n; ++i)
            ++counts[lengths[i]];
        counts[0] = 0;
        std::array<int, 16> offsets{};
        for (int i = 1; i < 16; ++i)
            offsets[i] = offsets[i - 1] + counts[i - 1];
        symbols.assign(n, 0);
        for (int i = 0; i < n; ++i)
            if (lengths[i] != 0)
                symbols[offsets[lengths[i]]++] = i;
    }
    int decode(Input& in) const
    {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len < 16; ++len) {
            code |= in.bit();
            int const count = counts[len];
            if (code - count < first)
                return symbols[index + (code - first)];
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
            if (in.broken)
                return -1;
        }
        return -1;
    }
};

int const kLengthBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
int const kLengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
int const kDistanceBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
int const kDistanceExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

bool block(Input& in, QByteArray& out, Huffman const& lengths, Huffman const& distances)
{
    for (;;) {
        int const symbol = lengths.decode(in);
        if (symbol < 0 || in.broken)
            return false;
        if (symbol < 256) {
            out.append(char(symbol));
        } else if (symbol == 256) {
            return true;
        } else {
            int const l = symbol - 257;
            if (l >= 29)
                return false;
            int const length = kLengthBase[l] + in.take(kLengthExtra[l]);
            int const d = distances.decode(in);
            if (d < 0 || d >= 30)
                return false;
            int const distance = kDistanceBase[d] + in.take(kDistanceExtra[d]);
            if (distance > out.size())
                return false;
            qsizetype const from = out.size() - distance;
            for (int i = 0; i < length; ++i)
                out.append(out.at(from + i));
        }
    }
}

quint16 le16(QByteArray const& b, qsizetype at)
{
    return at + 1 < b.size() ? quint16(quint8(b[at]) | (quint8(b[at + 1]) << 8)) : 0;
}

quint32 le32(QByteArray const& b, qsizetype at)
{
    return at + 3 < b.size() ? quint32(le16(b, at)) | (quint32(le16(b, at + 2)) << 16) : 0;
}

}  // namespace

namespace Zip {

QByteArray inflate(QByteArray const& data, bool* ok)
{
    Input in{data};
    QByteArray out;
    bool last = false;
    bool good = true;
    while (!last && good) {
        last = in.bit();
        int const type = in.take(2);
        if (type == 0) {
            // Stored: from the next whole byte, its length and the length's complement.
            in.count = 0;
            if (in.pos + 4 > data.size()) {
                good = false;
                break;
            }
            int const length = le16(data, in.pos);
            in.pos += 4;
            if (in.pos + length > data.size()) {
                good = false;
                break;
            }
            out.append(data.mid(in.pos, length));
            in.pos += length;
        } else if (type == 1) {
            static Huffman fixedLengths, fixedDistances;
            static bool made = false;
            if (!made) {
                int lengths[288];
                for (int i = 0; i < 144; ++i) lengths[i] = 8;
                for (int i = 144; i < 256; ++i) lengths[i] = 9;
                for (int i = 256; i < 280; ++i) lengths[i] = 7;
                for (int i = 280; i < 288; ++i) lengths[i] = 8;
                fixedLengths.build(lengths, 288);
                int distances[30];
                for (int& d : distances)
                    d = 5;
                fixedDistances.build(distances, 30);
                made = true;
            }
            good = block(in, out, fixedLengths, fixedDistances);
        } else if (type == 2) {
            int const hlit = in.take(5) + 257, hdist = in.take(5) + 1, hclen = in.take(4) + 4;
            static int const order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
            int codeLengths[19] = {};
            for (int i = 0; i < hclen; ++i)
                codeLengths[order[i]] = in.take(3);
            Huffman lengthCode;
            lengthCode.build(codeLengths, 19);
            int lengths[320] = {};
            for (int i = 0; i < hlit + hdist && good;) {
                int const symbol = lengthCode.decode(in);
                if (symbol < 0) {
                    good = false;
                } else if (symbol < 16) {
                    lengths[i++] = symbol;
                } else {
                    int repeat = 0, value = 0;
                    if (symbol == 16) {
                        if (i == 0) {
                            good = false;
                            break;
                        }
                        value = lengths[i - 1];
                        repeat = 3 + in.take(2);
                    } else if (symbol == 17) {
                        repeat = 3 + in.take(3);
                    } else {
                        repeat = 11 + in.take(7);
                    }
                    for (int k = 0; k < repeat && i < hlit + hdist; ++k)
                        lengths[i++] = value;
                }
            }
            if (!good)
                break;
            Huffman literal, distance;
            literal.build(lengths, hlit);
            distance.build(lengths + hlit, hdist);
            good = block(in, out, literal, distance);
        } else {
            good = false;
        }
        if (in.broken)
            good = false;
    }
    if (ok)
        *ok = good;
    return good ? out : QByteArray();
}

QHash<QString, QByteArray> read(QByteArray const& archive)
{
    QHash<QString, QByteArray> files;
    qsizetype p = 0;
    while (p + 30 <= archive.size() && le32(archive, p) == 0x04034b50u) {
        quint16 const flags = le16(archive, p + 6), method = le16(archive, p + 8);
        quint32 compressed = le32(archive, p + 18);
        quint16 const nameLength = le16(archive, p + 26), extraLength = le16(archive, p + 28);
        QString const name = QString::fromUtf8(archive.mid(p + 30, nameLength));
        qsizetype const start = p + 30 + nameLength + extraLength;
        qsizetype end;
        if (flags & 0x08) {
            // The sizes come after the data: it runs to the next entry (or to the directory), less what is said about it.
            qsizetype next = archive.indexOf("PK\x03\x04", start);
            qsizetype const directory = archive.indexOf("PK\x01\x02", start);
            if (next < 0 || (directory >= 0 && directory < next))
                next = directory;
            if (next < 0)
                next = archive.size();
            end = next;
            qsizetype dataEnd = end;
            if (dataEnd - 16 >= start && le32(archive, dataEnd - 16) == 0x08074b50u)
                dataEnd -= 16;
            else if (dataEnd - 12 >= start)
                dataEnd -= 12;
            compressed = quint32(dataEnd - start);
        } else {
            end = start + compressed;
        }
        if (start + qsizetype(compressed) > archive.size())
            break;
        QByteArray const raw = archive.mid(start, compressed);
        if (method == 0) {
            files.insert(name, raw);
        } else if (method == 8) {
            bool ok = false;
            QByteArray const body = inflate(raw, &ok);
            if (ok)
                files.insert(name, body);
        }
        p = end;
    }
    return files;
}

}
