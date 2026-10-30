// Eclipse - a scene kept as a folder: the scene file, a folder of elements (a file for each drawing, its pictures beside it), the palette library and frames.
#include "model/scene/SceneProject.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace SceneProject {

namespace {

QByteArray packed(QJsonObject const& object, bool pack)
{
    QByteArray const bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    return pack ? QByteArray("ECLZ") + qCompress(bytes, 9) : bytes;
}

QByteArray readFile(QString const& path, bool* ok)
{
    QFile file(path);
    *ok = file.open(QIODevice::ReadOnly);
    return *ok ? file.readAll() : QByteArray();
}

QJsonObject readObject(QString const& path, bool* ok)
{
    QByteArray bytes = readFile(path, ok);
    if (*ok && bytes.startsWith("ECLZ"))
        bytes = qUncompress(bytes.mid(4));
    QJsonDocument const doc = QJsonDocument::fromJson(bytes);
    *ok = *ok && doc.isObject();
    return doc.object();
}

// A name not yet in `taken` (the same, else with _2, _3 ... after it), noted as taken.
QString unique(QString const& name, QSet<QString>& taken)
{
    QString made = name;
    for (int n = 2; taken.contains(made.toLower()); ++n)
        made = QStringLiteral("%1_%2").arg(name).arg(n);
    taken.insert(made.toLower());
    return made;
}

// The drawings of a scene's layers (and of its symbols' layers, in folders of their own) into the elements folder.
void splitLayers(QJsonObject& scene, QString const& elements, QString const& owner, bool pack, Split& out, QJsonArray& table)
{
    QJsonObject images = scene.take(QStringLiteral("images")).toObject();
    QJsonObject imageFiles;
    QHash<QString, QString> moved;      // an image's key: the file it went to
    QSet<QString> elementNames;
    QJsonArray layers = scene[QStringLiteral("layers")].toArray();
    for (qsizetype l = 0; l < layers.size(); ++l) {
        QJsonObject layer = layers[l].toObject();
        QJsonArray drawings = layer[QStringLiteral("drawings")].toArray();
        QJsonArray notes = layer[QStringLiteral("pictures")].toArray();
        if (drawings.isEmpty() && notes.isEmpty())
            continue;
        QString const element = unique(fileName(layer[QStringLiteral("name")].toString(), QStringLiteral("Layer")), elementNames);
        QString const folder = elements + QLatin1Char('/') + element;
        QSet<QString> drawingNames;
        QJsonArray listed;
        for (qsizetype d = 0; d < drawings.size(); ++d) {
            QJsonObject drawing = drawings[d].toObject();
            QString const name = unique(fileName(drawing[QStringLiteral("name")].toString(), QString::number(d + 1)), drawingNames);
            QString const base = QStringLiteral("%1/%2-%3%4").arg(folder, element, name, owner);
            QJsonArray pictures = drawing[QStringLiteral("pictures")].toArray();
            for (qsizetype p = 0; p < pictures.size(); ++p) {
                QJsonObject picture = pictures[p].toObject();
                QString const key = picture[QStringLiteral("image")].toString();
                if (key.isEmpty() || !images.contains(key))
                    continue;
                if (!moved.contains(key)) {
                    QString const file = QStringLiteral("%1-%2.png").arg(base).arg(p + 1);
                    out.files.insert(file, QByteArray::fromBase64(images[key].toString().toLatin1()));
                    moved.insert(key, file);
                    imageFiles[file] = file;
                }
                picture[QStringLiteral("image")] = moved.value(key);
                pictures[p] = picture;
            }
            if (!pictures.isEmpty())
                drawing[QStringLiteral("pictures")] = pictures;
            drawing[QStringLiteral("eclipseDrawing")] = 1;
            QString const file = base + QLatin1Char('.') + QLatin1String(kDrawingSuffix);
            out.files.insert(file, packed(drawing, pack));
            drawings[d] = QJsonObject{{QStringLiteral("file"), file}};
            listed.append(name);
        }
        // The pictures put on an annotation layer.
        for (qsizetype n = 0; n < notes.size(); ++n) {
            QJsonObject note = notes[n].toObject();
            if (!note.contains(QStringLiteral("png")))
                continue;
            QString const file = QStringLiteral("%1/%2-note-%3%4.png").arg(folder, element).arg(n + 1).arg(owner);
            out.files.insert(file, QByteArray::fromBase64(note.take(QStringLiteral("png")).toString().toLatin1()));
            note[QStringLiteral("pngFile")] = file;
            notes[n] = note;
        }
        if (!drawings.isEmpty())
            layer[QStringLiteral("drawings")] = drawings;
        if (!notes.isEmpty())
            layer[QStringLiteral("pictures")] = notes;
        layer[QStringLiteral("element")] = folder;
        layers[l] = layer;
        table.append(QJsonObject{{QStringLiteral("name"), element}, {QStringLiteral("layer"), layer[QStringLiteral("id")]}, {QStringLiteral("folder"), folder},
                                 {QStringLiteral("drawings"), listed}});
    }
    scene[QStringLiteral("layers")] = layers;
    // Pictures named from elsewhere than a drawing: in a folder of their own.
    for (auto it = images.constBegin(); it != images.constEnd(); ++it)
        if (!moved.contains(it.key())) {
            QString const file = QStringLiteral("%1/pictures/picture-%2%3.png").arg(elements, fileName(it.key(), QStringLiteral("0")), owner);
            out.files.insert(file, QByteArray::fromBase64(it.value().toString().toLatin1()));
            imageFiles[it.key()] = file;
        }
    if (!imageFiles.isEmpty())
        scene[QStringLiteral("imageFiles")] = imageFiles;
    // Symbols: their layers' drawings in a folder for each symbol.
    QJsonArray symbols = scene[QStringLiteral("symbols")].toArray();
    QSet<QString> symbolNames;
    for (qsizetype s = 0; s < symbols.size(); ++s) {
        QJsonObject symbol = symbols[s].toObject();
        QJsonObject inner = symbol[QStringLiteral("scene")].toObject();
        QString const name = unique(fileName(symbol[QStringLiteral("name")].toString(), QStringLiteral("Symbol")), symbolNames);
        splitLayers(inner, QStringLiteral("%1/%2-symbol").arg(elements, name), owner, pack, out, table);
        symbol[QStringLiteral("scene")] = inner;
        symbols[s] = symbol;
    }
    if (!symbols.isEmpty())
        scene[QStringLiteral("symbols")] = symbols;
}

void joinLayers(QJsonObject& scene, QDir const& folder, QStringList* missing)
{
    auto lost = [missing](QString const& file) {
        if (missing && !missing->contains(file))
            missing->push_back(file);
    };
    QJsonArray layers = scene[QStringLiteral("layers")].toArray();
    for (qsizetype l = 0; l < layers.size(); ++l) {
        QJsonObject layer = layers[l].toObject();
        layer.remove(QStringLiteral("element"));
        QJsonArray drawings = layer[QStringLiteral("drawings")].toArray();
        for (qsizetype d = 0; d < drawings.size(); ++d) {
            QJsonObject const entry = drawings[d].toObject();
            QString const file = entry[QStringLiteral("file")].toString();
            if (file.isEmpty())
                continue;
            bool ok = false;
            QJsonObject drawing = readObject(folder.filePath(file), &ok);
            if (!ok)
                lost(file);
            drawing.remove(QStringLiteral("eclipseDrawing"));
            drawings[d] = drawing;
        }
        if (!drawings.isEmpty())
            layer[QStringLiteral("drawings")] = drawings;
        QJsonArray notes = layer[QStringLiteral("pictures")].toArray();
        for (qsizetype n = 0; n < notes.size(); ++n) {
            QJsonObject note = notes[n].toObject();
            QString const file = note.take(QStringLiteral("pngFile")).toString();
            if (file.isEmpty())
                continue;
            bool ok = false;
            QByteArray const bytes = readFile(folder.filePath(file), &ok);
            if (!ok)
                lost(file);
            note[QStringLiteral("png")] = QString::fromLatin1(bytes.toBase64());
            notes[n] = note;
        }
        if (!notes.isEmpty())
            layer[QStringLiteral("pictures")] = notes;
        layers[l] = layer;
    }
    scene[QStringLiteral("layers")] = layers;
    QJsonObject const files = scene.take(QStringLiteral("imageFiles")).toObject();
    if (!files.isEmpty()) {
        QJsonObject images = scene[QStringLiteral("images")].toObject();
        for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
            bool ok = false;
            QByteArray const bytes = readFile(folder.filePath(it.value().toString()), &ok);
            if (!ok)
                lost(it.value().toString());
            images[it.key()] = QString::fromLatin1(bytes.toBase64());
        }
        scene[QStringLiteral("images")] = images;
    }
    QJsonArray symbols = scene[QStringLiteral("symbols")].toArray();
    for (qsizetype s = 0; s < symbols.size(); ++s) {
        QJsonObject symbol = symbols[s].toObject();
        QJsonObject inner = symbol[QStringLiteral("scene")].toObject();
        joinLayers(inner, folder, missing);
        symbol[QStringLiteral("scene")] = inner;
        symbols[s] = symbol;
    }
    if (!symbols.isEmpty())
        scene[QStringLiteral("symbols")] = symbols;
}

}

QString fileName(QString const& name, QString const& fallback)
{
    static QRegularExpression const unfit(QStringLiteral("[\\\\/:*?\"<>|\\x00-\\x1f]"));
    QString made = name;
    made.replace(unfit, QStringLiteral("_"));
    made = made.trimmed();
    while (made.endsWith(QLatin1Char('.')))
        made.chop(1);
    return made.isEmpty() ? fallback : made.left(80);
}

bool usesFolder(QJsonObject const& scene)
{
    return scene.contains(QStringLiteral("paletteFiles")) || scene.contains(QStringLiteral("imageFiles"))
           || scene.contains(QStringLiteral("textureFiles")) || scene.contains(QStringLiteral("folderLayout"));
}

Split split(QJsonObject& scene, QString const& sceneName, QString const& owner, bool pack)
{
    Split out;
    QJsonArray elements;
    splitLayers(scene, QLatin1String(kElements), owner, pack, out, elements);

    // The palette library: a file for each palette of the scene, its textures as pictures beside them.
    QString const library = QLatin1String(kPalettes);
    QString const textures = QStringLiteral("%1/%2%3_textures").arg(library, fileName(sceneName, QStringLiteral("scene")), owner);
    QJsonArray const names = scene[QStringLiteral("paletteNames")].toArray();
    QJsonArray const swatches = scene.take(QStringLiteral("palette")).toArray();
    QVector<QJsonArray> byPalette(std::max<qsizetype>(1, names.size()));
    for (qsizetype i = 0; i < swatches.size(); ++i) {
        QJsonObject swatch = swatches[i].toObject();
        swatch[QStringLiteral("at")] = int(i);
        if (QString const texture = swatch.take(QStringLiteral("texture")).toString(); !texture.isEmpty()) {
            QString const file = QStringLiteral("%1/swatch-%2.png").arg(textures).arg(swatch[QStringLiteral("id")].toInt());
            out.files.insert(file, QByteArray::fromBase64(texture.toLatin1()));
            swatch[QStringLiteral("textureFile")] = file;
        }
        int const palette = std::clamp(swatch[QStringLiteral("palette")].toInt(), 0, int(byPalette.size()) - 1);
        byPalette[palette].append(swatch);
    }
    QJsonArray paletteFiles;
    QSet<QString> paletteNames;
    for (qsizetype p = 0; p < byPalette.size(); ++p) {
        QString const name = names.at(p).toString();
        QString const file = QStringLiteral("%1/%2%3.%4").arg(library, unique(fileName(name, QStringLiteral("palette")), paletteNames), owner,
                                                             QLatin1String(kPaletteSuffix));
        out.files.insert(file, packed(QJsonObject{{QStringLiteral("eclipseScenePalette"), 1}, {QStringLiteral("name"), name},
                                                  {QStringLiteral("swatches"), byPalette[p]}},
                                      false));
        paletteFiles.append(file);
    }
    scene[QStringLiteral("paletteFiles")] = paletteFiles;
    // The textures of the scene's own that it is drawn with.
    QJsonArray bank = scene[QStringLiteral("textures")].toArray();
    QSet<QString> textureNames;
    for (qsizetype t = 0; t < bank.size(); ++t) {
        QJsonObject texture = bank[t].toObject();
        QString const png = texture.take(QStringLiteral("png")).toString();
        QString const file = QStringLiteral("%1/%2.png").arg(textures, unique(fileName(texture[QStringLiteral("name")].toString(), QStringLiteral("texture")), textureNames));
        out.files.insert(file, QByteArray::fromBase64(png.toLatin1()));
        texture[QStringLiteral("pngFile")] = file;
        bank[t] = texture;
    }
    if (!bank.isEmpty())
        scene[QStringLiteral("textures")] = bank;
    scene[QStringLiteral("folderLayout")] = 1;

    QJsonArray files;
    for (auto it = out.files.constBegin(); it != out.files.constEnd(); ++it)
        files.append(it.key());
    out.table = QJsonObject{{QStringLiteral("eclipseElements"), 1}, {QStringLiteral("scene"), sceneName}, {QStringLiteral("elements"), elements},
                            {QStringLiteral("palettes"), paletteFiles}, {QStringLiteral("files"), files}};
    return out;
}

void join(QJsonObject& scene, QString const& folder, QStringList* missing)
{
    if (!usesFolder(scene))
        return;
    QDir const dir(folder);
    auto lost = [missing](QString const& file) {
        if (missing && !missing->contains(file))
            missing->push_back(file);
    };
    joinLayers(scene, dir, missing);
    // The palettes, in the order their colours had.
    QJsonArray const paletteFiles = scene.take(QStringLiteral("paletteFiles")).toArray();
    if (!paletteFiles.isEmpty()) {
        QVector<QJsonObject> swatches;
        for (QJsonValue const& f : paletteFiles) {
            bool ok = false;
            QJsonObject const palette = readObject(dir.filePath(f.toString()), &ok);
            if (!ok)
                lost(f.toString());
            for (QJsonValue const& s : palette[QStringLiteral("swatches")].toArray()) {
                QJsonObject swatch = s.toObject();
                if (QString const file = swatch.take(QStringLiteral("textureFile")).toString(); !file.isEmpty()) {
                    bool read = false;
                    QByteArray const bytes = readFile(dir.filePath(file), &read);
                    if (!read)
                        lost(file);
                    swatch[QStringLiteral("texture")] = QString::fromLatin1(bytes.toBase64());
                }
                swatches.push_back(swatch);
            }
        }
        std::stable_sort(swatches.begin(), swatches.end(), [](QJsonObject const& a, QJsonObject const& b) {
            return a[QStringLiteral("at")].toInt() < b[QStringLiteral("at")].toInt();
        });
        QJsonArray palette;
        for (QJsonObject swatch : std::as_const(swatches)) {
            swatch.remove(QStringLiteral("at"));
            palette.append(swatch);
        }
        scene[QStringLiteral("palette")] = palette;
    }
    QJsonArray bank = scene[QStringLiteral("textures")].toArray();
    for (qsizetype t = 0; t < bank.size(); ++t) {
        QJsonObject texture = bank[t].toObject();
        QString const file = texture.take(QStringLiteral("pngFile")).toString();
        if (file.isEmpty())
            continue;
        bool ok = false;
        QByteArray const bytes = readFile(dir.filePath(file), &ok);
        if (!ok)
            lost(file);
        texture[QStringLiteral("png")] = QString::fromLatin1(bytes.toBase64());
        bank[t] = texture;
    }
    if (!bank.isEmpty())
        scene[QStringLiteral("textures")] = bank;
    scene.remove(QStringLiteral("folderLayout"));
}

}
