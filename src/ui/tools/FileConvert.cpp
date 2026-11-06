// Eclipse - the file converters (see FileConvert.h).
#include "ui/tools/FileConvert.h"

#include "actions/manager/ActionManager.h"
#include "canvas/FrameRender.h"
#include "formats/archive/Zip.h"
#include "formats/flash/SwfImport.h"
#include "model/geometry/VectorizeOptions.h"
#include "model/scene/Scene.h"
#include "model/scene/SceneProject.h"
#include "ui/tools/CommandLine.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <algorithm>
#include <cmath>

namespace FileConvert {

namespace {

QString tr(char const* text, char const* note = nullptr, int n = -1) { return QCoreApplication::translate("FileConvert", text, note, n); }

bool fail(QString* error, QString const& why)
{
    if (error)
        *error = why;
    return false;
}

bool readBytes(QString const& path, QByteArray& out, QString* error)
{
    if (QString const why = CommandLine::readProblem(path); !why.isEmpty())
        return fail(error, QStringLiteral("%1: %2").arg(QDir::toNativeSeparators(path), why));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(error, QStringLiteral("%1: %2").arg(QDir::toNativeSeparators(path), file.errorString()));
    out = file.readAll();
    return true;
}

bool writeBytes(QString const& path, QByteArray const& bytes, QString* error)
{
    if (QString const why = CommandLine::writeProblem(path); !why.isEmpty())
        return fail(error, QStringLiteral("%1: %2").arg(QDir::toNativeSeparators(path), why));
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return fail(error, QStringLiteral("%1: %2").arg(QDir::toNativeSeparators(path), file.errorString()));
    return true;
}

// The JSON object of an Eclipse file (packed or plain); `packed` says which it was.
bool readObject(QByteArray bytes, QJsonObject& out, bool* packed = nullptr)
{
    bool const wasPacked = bytes.startsWith("ECLZ");
    if (wasPacked)
        bytes = qUncompress(bytes.mid(4));
    if (packed)
        *packed = wasPacked;
    QJsonDocument const doc = QJsonDocument::fromJson(bytes);
    out = doc.object();
    return doc.isObject();
}

QByteArray objectBytes(QJsonObject const& object, bool pack, bool indented = false)
{
    QByteArray const bytes = QJsonDocument(object).toJson(indented ? QJsonDocument::Indented : QJsonDocument::Compact);
    return pack ? QByteArray("ECLZ") + qCompress(bytes, 9) : bytes;
}

Scene oneDrawing(Drawing const& drawing, QSize size = QSize(640, 480))
{
    Scene scene;
    scene.width = size.width();
    scene.height = size.height();
    scene.background = Qt::transparent;
    scene.layers.clear();
    int const l = scene.addLayer(QStringLiteral("Drawing"), -1, Layer::DrawingLayer);
    scene.layers[l].drawings = {drawing};
    scene.layers[l].cells = QVector<int>(1, 0);
    scene.setFrameCount(1);
    return scene;
}

bool isImageFile(QString const& path)
{
    QString const suffix = QFileInfo(path).suffix().toLower().toLatin1();
    return !suffix.isEmpty() && QImageReader::supportedImageFormats().contains(suffix.toLatin1());
}

int parseSide(QString const& text, bool horizontal)
{
    QString const t = text.toLower();
    if (t == QLatin1String("left") || t == QLatin1String("top"))
        return -1;
    if (t == QLatin1String("right") || t == QLatin1String("bottom"))
        return 1;
    Q_UNUSED(horizontal);
    return 0;
}

QString sideName(int side, bool horizontal)
{
    return side < 0 ? (horizontal ? QStringLiteral("left") : QStringLiteral("top")) : side > 0 ? (horizontal ? QStringLiteral("right") : QStringLiteral("bottom"))
                                                                                               : QStringLiteral("centre");
}

QString const kFits[] = {QStringLiteral("none"), QStringLiteral("fit"), QStringLiteral("fill"), QStringLiteral("width"), QStringLiteral("height")};

// Every string in a JSON value, changed by `change` (which returns whether it changed it).
void walkStrings(QJsonValue& value, std::function<bool(QString&)> const& change, int& changed)
{
    if (value.isString()) {
        QString text = value.toString();
        if (change(text)) {
            value = text;
            ++changed;
        }
    } else if (value.isArray()) {
        QJsonArray array = value.toArray();
        for (qsizetype i = 0; i < array.size(); ++i) {
            QJsonValue item = array[i];
            walkStrings(item, change, changed);
            array[i] = item;
        }
        value = array;
    } else if (value.isObject()) {
        QJsonObject object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            QJsonValue item = it.value();
            walkStrings(item, change, changed);
            it.value() = item;
        }
        value = object;
    }
}

void eachString(QJsonValue const& value, std::function<void(QString const&)> const& see)
{
    if (value.isString())
        see(value.toString());
    else if (value.isArray())
        for (QJsonValue const& item : value.toArray())
            eachString(item, see);
    else if (value.isObject())
        for (QJsonValue const& item : value.toObject())
            eachString(item, see);
}

// Whether a string of a scene looks like a path of a file outside it.
bool looksLikePath(QString const& text)
{
    static QRegularExpression const path(QStringLiteral("^([A-Za-z]:[\\\\/]|\\\\\\\\|/)[^\\n\"<>|]*\\.[A-Za-z0-9]{1,5}$"));
    return text.size() < 1024 && path.match(text).hasMatch();
}

QString normal(QString path)
{
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return path;
}

// The drawing files of a scene's folder.
QStringList drawingFilesOf(QString const& folder)
{
    QStringList files;
    QDirIterator it(QDir(folder).filePath(QLatin1String(SceneProject::kElements)), {QStringLiteral("*.") + QLatin1String(SceneProject::kDrawingSuffix)},
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
        files << it.next();
    return files;
}

}  // namespace

// ---- drawing files ----

bool readDrawing(QString const& path, Drawing& out, QString* error)
{
    QByteArray bytes;
    if (!readBytes(path, bytes, error))
        return false;
    QJsonObject object;
    if (!readObject(bytes, object) || !object.contains(QStringLiteral("eclipseDrawing")))
        return fail(error, tr("Formatting error: %1 is not a drawing file.").arg(QDir::toNativeSeparators(path)));
    QString const folder = QFileInfo(path).absolutePath();
    object.remove(QStringLiteral("eclipseDrawing"));
    object.remove(QStringLiteral("source"));
    QJsonObject images;
    for (QJsonValue const& p : object[QStringLiteral("pictures")].toArray()) {
        QString const key = p.toObject()[QStringLiteral("image")].toString();
        if (key.isEmpty() || images.contains(key))
            continue;
        QFile file(QDir(folder).filePath(key));
        if (file.open(QIODevice::ReadOnly))
            images[key] = QString::fromLatin1(file.readAll().toBase64());
    }
    QJsonObject scene = oneDrawing(Drawing()).toJson();
    QJsonArray layers = scene[QStringLiteral("layers")].toArray();
    QJsonObject layer = layers[0].toObject();
    layer[QStringLiteral("drawings")] = QJsonArray{object};
    layers[0] = layer;
    scene[QStringLiteral("layers")] = layers;
    scene[QStringLiteral("images")] = images;
    Scene read;
    QString why;
    if (!Scene::fromJson(scene, read, &why, folder) || read.layers.isEmpty() || read.layers[0].drawings.isEmpty())
        return fail(error, tr("Formatting error: %1 could not be read as a drawing (%2).").arg(QDir::toNativeSeparators(path), why));
    out = read.layers[0].drawings[0];
    return true;
}

bool writeDrawing(Drawing const& drawing, QString const& path, bool pack, QString* error, QString const& source)
{
    QJsonObject scene = oneDrawing(drawing).toJson();
    SceneProject::Split const split = SceneProject::split(scene, QStringLiteral("drawing"), QString(), false);
    QJsonObject object;
    for (auto it = split.files.constBegin(); it != split.files.constEnd(); ++it)
        if (it.key().endsWith(QLatin1Char('.') + QLatin1String(SceneProject::kDrawingSuffix))) {
            readObject(it.value(), object);
            break;
        }
    if (object.isEmpty())
        return fail(error, tr("The drawing could not be written."));
    // Its pictures beside it, named after it.
    QString const base = QFileInfo(path).completeBaseName();
    QJsonArray pictures = object[QStringLiteral("pictures")].toArray();
    QHash<QString, QString> renamed;
    for (qsizetype i = 0; i < pictures.size(); ++i) {
        QJsonObject picture = pictures[i].toObject();
        QString const key = picture[QStringLiteral("image")].toString();
        if (key.isEmpty() || !split.files.contains(key))
            continue;
        if (!renamed.contains(key)) {
            QString const name = QStringLiteral("%1-%2.%3").arg(base).arg(renamed.size() + 1).arg(QFileInfo(key).suffix());
            if (!writeBytes(QFileInfo(path).absoluteDir().filePath(name), split.files.value(key), error))
                return false;
            renamed.insert(key, name);
        }
        picture[QStringLiteral("image")] = renamed.value(key);
        pictures[i] = picture;
    }
    if (!pictures.isEmpty())
        object[QStringLiteral("pictures")] = pictures;
    if (!source.isEmpty())
        object[QStringLiteral("source")] = source;
    return writeBytes(path, objectBytes(object, pack), error);
}

QString drawingSource(QString const& path)
{
    QFile file(path);
    QJsonObject object;
    if (!file.open(QIODevice::ReadOnly) || !readObject(file.readAll(), object))
        return QString();
    return object[QStringLiteral("source")].toString();
}

bool drawingToText(QString const& in, QString const& out, QString* error)
{
    QByteArray bytes;
    if (!readBytes(in, bytes, error))
        return false;
    QJsonObject object;
    if (!readObject(bytes, object) || !object.contains(QStringLiteral("eclipseDrawing")))
        return fail(error, tr("Could not read input file.") + QLatin1Char(' ') + tr("Formatting error: %1 is not a drawing file.").arg(QDir::toNativeSeparators(in)));
    // The pictures it names go with it.
    QDir const from = QFileInfo(in).absoluteDir(), to = QFileInfo(out).absoluteDir();
    if (from != to)
        for (QJsonValue const& p : object[QStringLiteral("pictures")].toArray()) {
            QString const key = p.toObject()[QStringLiteral("image")].toString();
            if (!key.isEmpty() && QFileInfo::exists(from.filePath(key)) && !QFileInfo::exists(to.filePath(key))) {
                QDir().mkpath(QFileInfo(to.filePath(key)).absolutePath());
                QFile::copy(from.filePath(key), to.filePath(key));
            }
        }
    if (!writeBytes(out, objectBytes(object, false, true), error)) {
        if (error)
            *error = tr("Could not write output file.") + QLatin1Char(' ') + *error;
        return false;
    }
    return true;
}

bool textToDrawing(QString const& in, QString const& out, bool pack, QString* error)
{
    QByteArray bytes;
    if (!readBytes(in, bytes, error))
        return false;
    QJsonParseError parse;
    QJsonDocument const doc = QJsonDocument::fromJson(bytes, &parse);
    if (!doc.isObject())
        return fail(error, tr("Formatting error: %1, at character %2: %3").arg(QDir::toNativeSeparators(in)).arg(parse.offset).arg(parse.errorString()));
    QJsonObject object = doc.object();
    if (!object.contains(QStringLiteral("eclipseDrawing")))
        return fail(error, tr("Formatting error: %1 is not the text of a drawing (it has no \"eclipseDrawing\").").arg(QDir::toNativeSeparators(in)));
    if (!writeBytes(out, objectBytes(object, pack), error))
        return false;
    // Read back, so that what was written is known to be a drawing.
    Drawing check;
    return readDrawing(out, check, error);
}

// ---- Flash movies ----

int swfToDrawings(QString const& swf, QString const& folder, QString const& prefix, bool pack, QStringList* written, QString* error)
{
    QByteArray bytes;
    if (!readBytes(swf, bytes, error))
        return -1;
    if (!(bytes.startsWith("FWS") || bytes.startsWith("CWS") || bytes.startsWith("ZWS"))) {
        fail(error, tr("Formatting error: %1 is not a Flash movie.").arg(QDir::toNativeSeparators(swf)));
        return -1;
    }
    SwfImport::Movie movie;
    QString why;
    if (!SwfImport::read(bytes, movie, &why, QFileInfo(swf).absolutePath())) {
        fail(error, tr("Could not read %1: %2").arg(QDir::toNativeSeparators(swf), why));
        return -1;
    }
    if (QString const problem = CommandLine::folderProblem(folder); !problem.isEmpty()) {
        fail(error, QStringLiteral("%1: %2").arg(QDir::toNativeSeparators(folder), problem));
        return -1;
    }
    QDir().mkpath(folder);
    QString const name = prefix.isEmpty() ? QFileInfo(swf).completeBaseName() : prefix;
    QString const source = tr("Generated by eclipse-convert from %1").arg(QFileInfo(swf).fileName());
    int digits = 4;
    while (movie.frames.size() >= std::pow(10, digits))
        ++digits;
    int made = 0;
    for (qsizetype i = 0; i < movie.frames.size(); ++i) {
        QString const file = QDir(folder).filePath(QStringLiteral("%1-%2.%3").arg(name).arg(i + 1, digits, 10, QLatin1Char('0')).arg(QLatin1String(SceneProject::kDrawingSuffix)));
        if (!writeDrawing(movie.frames[i], file, pack, error, source))
            return -1;
        if (written)
            *written << file;
        ++made;
    }
    return made;
}

// ---- alignments ----

QVector<Alignment> alignments(QString const& file)
{
    QVector<Alignment> list{{QStringLiteral("Fit"), Alignment::Fit, 0, 0, 1.0},
                            {QStringLiteral("Fill"), Alignment::Fill, 0, 0, 1.0},
                            {QStringLiteral("Fit Width"), Alignment::Width, 0, 0, 1.0},
                            {QStringLiteral("Fit Height"), Alignment::Height, 0, 0, 1.0},
                            {QStringLiteral("Centre"), Alignment::None, 0, 0, 1.0},
                            {QStringLiteral("Top Left"), Alignment::None, -1, -1, 1.0}};
    QFile f(file.isEmpty() ? ActionManager::defaultDirectory() + QStringLiteral("/alignments.xml") : file);
    if (!f.open(QIODevice::ReadOnly))
        return list;
    QXmlStreamReader xml(&f);
    while (!xml.atEnd()) {
        if (xml.readNext() != QXmlStreamReader::StartElement || xml.name() != QLatin1String("alignment"))
            continue;
        QXmlStreamAttributes const a = xml.attributes();
        Alignment al;
        al.name = a.value(QStringLiteral("name")).toString();
        if (al.name.isEmpty())
            continue;
        QString const fit = a.value(QStringLiteral("fit")).toString().toLower();
        for (int i = 0; i < 5; ++i)
            if (fit == kFits[i])
                al.fit = i;
        al.horizontal = parseSide(a.value(QStringLiteral("horizontal")).toString(), true);
        al.vertical = parseSide(a.value(QStringLiteral("vertical")).toString(), false);
        bool ok = false;
        double const scale = a.value(QStringLiteral("scale")).toDouble(&ok);
        al.scale = ok && scale > 0 ? scale : 1.0;
        auto same = std::find_if(list.begin(), list.end(), [&](Alignment const& x) { return x.name.compare(al.name, Qt::CaseInsensitive) == 0; });
        if (same != list.end())
            *same = al;
        else
            list << al;
    }
    return list;
}

bool writeAlignments(QString const& file, QVector<Alignment> const& list, QString* error)
{
    QByteArray bytes;
    QXmlStreamWriter xml(&bytes);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    xml.writeComment(QStringLiteral(" How a picture is put in a frame when it is made a drawing (eclipse-convert -alignment <name>). "
                                    "fit: none, fit, fill, width or height; horizontal: left, centre or right; vertical: top, centre or bottom; "
                                    "scale: after fitting. "));
    xml.writeStartElement(QStringLiteral("alignments"));
    for (Alignment const& al : list) {
        xml.writeStartElement(QStringLiteral("alignment"));
        xml.writeAttribute(QStringLiteral("name"), al.name);
        xml.writeAttribute(QStringLiteral("fit"), kFits[std::clamp(al.fit, 0, 4)]);
        xml.writeAttribute(QStringLiteral("horizontal"), sideName(al.horizontal, true));
        xml.writeAttribute(QStringLiteral("vertical"), sideName(al.vertical, false));
        xml.writeAttribute(QStringLiteral("scale"), QString::number(al.scale));
        xml.writeEndElement();
    }
    xml.writeEndElement();
    xml.writeEndDocument();
    return writeBytes(file, bytes, error);
}

QTransform placement(QSize picture, QSize frame, Alignment const& alignment)
{
    if (picture.isEmpty() || frame.isEmpty())
        return QTransform();
    double const across = double(frame.width()) / picture.width(), down = double(frame.height()) / picture.height();
    double scale = alignment.fit == Alignment::Fit ? std::min(across, down)
                   : alignment.fit == Alignment::Fill ? std::max(across, down)
                   : alignment.fit == Alignment::Width ? across
                   : alignment.fit == Alignment::Height ? down
                                                        : 1.0;
    scale *= alignment.scale;
    double const w = picture.width() * scale, h = picture.height() * scale;
    double const x = alignment.horizontal < 0 ? 0.0 : alignment.horizontal > 0 ? frame.width() - w : (frame.width() - w) / 2.0;
    double const y = alignment.vertical < 0 ? 0.0 : alignment.vertical > 0 ? frame.height() - h : (frame.height() - h) / 2.0;
    return QTransform::fromScale(scale, scale) * QTransform::fromTranslate(x, y);
}

// ---- pictures and drawings ----

Drawing pictureDrawing(QImage const& image, QSize frame, Alignment const& alignment)
{
    Drawing drawing;
    Picture picture;
    picture.image = image;
    picture.size = image.size();
    picture.placement = placement(image.size(), frame.isValid() ? frame : image.size(), alignment);
    drawing.pictures.push_back(picture);
    return drawing;
}

namespace {

QVector<QPair<QString, QString>> styleFile(QString const& file)
{
    QVector<QPair<QString, QString>> styles;
    QFile f(file.isEmpty() ? ActionManager::defaultDirectory() + QStringLiteral("/vectorize.conf") : file);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return styles;
    for (QString line : QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'))) {
        line = line.trimmed();
        qsizetype const eq = line.indexOf(QLatin1Char('='));
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char('[')) || eq <= 0)
            continue;
        styles.push_back({line.left(eq).trimmed(), line.mid(eq + 1).trimmed()});
    }
    return styles;
}

}  // namespace

QStringList vectorizeStyles(QString const& file)
{
    QStringList names = Vectorize::styleNames();
    for (auto const& [name, line] : styleFile(file))
        if (!names.contains(name, Qt::CaseInsensitive))
            names << name;
    return names;
}

QString vectorizeLine(QString const& style, QString const& file)
{
    for (auto const& [name, line] : styleFile(file))
        if (name.compare(style, Qt::CaseInsensitive) == 0)
            return line;
    for (QString const& name : Vectorize::styleNames())
        if (name.compare(style, Qt::CaseInsensitive) == 0)
            return Vectorize::styleLine(name);
    return style.startsWith(QLatin1Char('-')) || style.isEmpty() ? style : QString();
}

bool vectorizePicture(QImage const& image, QString const& options, QSize frame, Alignment const& alignment, Drawing& out, QString* error)
{
    Vectorize::Options settings;
    QString why;
    if (!Vectorize::parse(options, settings, &why))
        return fail(error, tr("Wrong vectorization options: %1").arg(why));
    QImage flat(image.size(), QImage::Format_ARGB32);
    flat.fill(Qt::white);
    {
        QPainter p(&flat);
        p.drawImage(0, 0, image);
    }
    Vectorize::Result const made = Vectorize::run(flat, settings);
    if (!made.ok)
        return fail(error, tr("The picture could not be vectorised: %1").arg(made.notes.join(QStringLiteral("; "))));
    QTransform const placed = placement(image.size(), frame.isValid() ? frame : image.size(), alignment);
    out = Drawing();
    for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
        for (Fill f : made.drawing.fillsOf(sheet)) {
            f.transform(placed);
            f.giveContours(std::max(0.75, placed.m11()));
            out.fillsOf(sheet).push_back(f);
        }
        for (Stroke s : made.drawing.strokesOf(sheet)) {
            s.transform(placed);
            s.width = float(s.width * std::sqrt(std::abs(placed.determinant())));
            out.strokesOf(sheet).push_back(s);
        }
    }
    return true;
}

QImage drawingPicture(Drawing const& drawing, QSize size, QColor const& background)
{
    if (!size.isValid() || size.isEmpty()) {
        QRectF const bounds = drawing.bounds();
        size = QSize(std::max(1, int(std::ceil(bounds.right()))), std::max(1, int(std::ceil(bounds.bottom()))));
    }
    Scene scene = oneDrawing(drawing, size);
    return FrameRender::frame(scene, 0, size, false, background);
}

// ---- pictures transformed ----

bool parseTransform(QStringList const& words, Transform& out, QString* error)
{
    for (qsizetype i = 0; i < words.size(); ++i) {
        QString const word = words[i];
        auto value = [&](QString& to) {
            if (i + 1 >= words.size())
                return fail(error, tr("Wrong command line arguments.") + QLatin1Char(' ') + tr("%1 needs a value.").arg(word));
            to = words[++i];
            return true;
        };
        auto wrong = [&](QString const& text) { return fail(error, tr("Wrong value specified for %1: %2").arg(word, text)); };
        QString text;
        if (word == QLatin1String("-resize")) {
            if (!value(text))
                return false;
            QStringList const parts = text.toLower().split(QLatin1Char('x'));
            int const w = parts.value(0).toInt(), h = parts.value(1).toInt();
            if (parts.size() != 2 || w <= 0 || h <= 0 || w > 32768 || h > 32768)
                return wrong(text);
            out.size = QSize(w, h);
        } else if (word == QLatin1String("-scale")) {
            if (!value(text))
                return false;
            bool ok = false;
            out.percent = text.toInt(&ok);
            if (!ok || out.percent <= 0 || out.percent > 10000)
                return wrong(text);
        } else if (word == QLatin1String("-flip")) {
            if (!value(text))
                return false;
            if (text == QLatin1String("h") || text == QLatin1String("horizontal"))
                out.flipHorizontal = true;
            else if (text == QLatin1String("v") || text == QLatin1String("vertical"))
                out.flipVertical = true;
            else
                return wrong(text);
        } else if (word == QLatin1String("-rotate")) {
            if (!value(text))
                return false;
            int const r = text.toInt();
            if (r != 0 && r != 90 && r != 180 && r != 270 && r != -90)
                return wrong(text);
            out.rotate = (r + 360) % 360;
        } else if (word == QLatin1String("-premultiply")) {
            if (!value(text))
                return false;
            QString const t = text.toLower();
            if (t == QLatin1String("on") || t == QLatin1String("yes") || t == QLatin1String("1") || t == QLatin1String("true"))
                out.premultiply = 1;
            else if (t == QLatin1String("off") || t == QLatin1String("no") || t == QLatin1String("0") || t == QLatin1String("false"))
                out.premultiply = 0;
            else
                return wrong(text);
        } else if (word == QLatin1String("-remap")) {
            if (!value(out.remapFile))
                return false;
        } else if (word == QLatin1String("-grey") || word == QLatin1String("-gray")) {
            out.grey = true;
        } else if (word == QLatin1String("-flatten")) {
            if (!value(text))
                return false;
            out.flatten = QColor(text);
            if (!out.flatten.isValid())
                return wrong(text);
        } else if (word == QLatin1String("-format")) {
            if (!value(text))
                return false;
            out.format = text.toLower();
            if (!QImageWriter::supportedImageFormats().contains(out.format.toLatin1()))
                return wrong(text);
        } else if (word == QLatin1String("-quality")) {
            if (!value(text))
                return false;
            bool ok = false;
            out.quality = text.toInt(&ok);
            if (!ok || out.quality < 0 || out.quality > 100)
                return wrong(text);
        } else {
            return fail(error, tr("Unknown command-line argument: %1").arg(word));
        }
    }
    return true;
}

bool readRemap(QString const& path, QHash<QRgb, QRgb>& out, QString* error)
{
    out.clear();
    QFile file(path);
    if (path.isEmpty() || !file.open(QIODevice::ReadOnly | QIODevice::Text))
        return fail(error, tr("Empty or non existent remap color file: %1").arg(QDir::toNativeSeparators(path)));
    int lineNumber = 0;
    for (QString line : QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'))) {
        ++lineNumber;
        line = line.trimmed();
        // Notes: a line of them (# and a space, or //), or one after the colours (a # with spaces round it).
        if (line.isEmpty() || line.startsWith(QLatin1String("//")) || (line.startsWith(QLatin1Char('#')) && (line.size() < 2 || line[1].isSpace())))
            continue;
        static QRegularExpression const note(QStringLiteral("\\s#(\\s|$)"));
        if (QRegularExpressionMatch const m = note.match(line); m.hasMatch())
            line = line.left(m.capturedStart()).trimmed();
        QStringList const parts = line.split(QRegularExpression(QStringLiteral("[\\s,]+")), Qt::SkipEmptyParts);
        QColor from, to;
        if (parts.size() == 2) {
            from = QColor(parts[0]);
            to = QColor(parts[1]);
        } else if (parts.size() == 6) {
            from = QColor(parts[0].toInt(), parts[1].toInt(), parts[2].toInt());
            to = QColor(parts[3].toInt(), parts[4].toInt(), parts[5].toInt());
        }
        if (!from.isValid() || !to.isValid())
            return fail(error, tr("Formatting error: line %1 of %2 is not a pair of colours.").arg(lineNumber).arg(QDir::toNativeSeparators(path)));
        out.insert(from.rgb() & 0xffffffu, to.rgb() & 0xffffffu);
    }
    if (out.isEmpty())
        return fail(error, tr("Empty or non existent remap color file: %1").arg(QDir::toNativeSeparators(path)));
    return true;
}

QImage transformed(QImage const& image, Transform const& transform, QHash<QRgb, QRgb> const& remap)
{
    QImage out = image.convertToFormat(QImage::Format_ARGB32);
    if (!remap.isEmpty() || transform.premultiply >= 0 || transform.grey) {
        for (int y = 0; y < out.height(); ++y) {
            QRgb* const line = reinterpret_cast<QRgb*>(out.scanLine(y));
            for (int x = 0; x < out.width(); ++x) {
                QRgb c = line[x];
                int const a = qAlpha(c);
                if (!remap.isEmpty())
                    if (auto it = remap.constFind(c & 0xffffffu); it != remap.constEnd())
                        c = (QRgb(a) << 24) | it.value();
                int r = qRed(c), g = qGreen(c), b = qBlue(c);
                if (transform.grey)
                    r = g = b = qGray(r, g, b);
                if (transform.premultiply == 1) {
                    // The colours made premultiplied by the alpha in the file itself.
                    r = r * a / 255;
                    g = g * a / 255;
                    b = b * a / 255;
                } else if (transform.premultiply == 0 && a > 0 && a < 255) {
                    // Premultiplied colours in the file made straight again.
                    r = std::min(255, r * 255 / a);
                    g = std::min(255, g * 255 / a);
                    b = std::min(255, b * 255 / a);
                }
                line[x] = qRgba(r, g, b, a);
            }
        }
    }
    if (transform.flipHorizontal || transform.flipVertical)
        out = out.mirrored(transform.flipHorizontal, transform.flipVertical);
    if (transform.rotate != 0)
        out = out.transformed(QTransform().rotate(transform.rotate));
    if (transform.size.isValid())
        out = out.scaled(transform.size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    else if (transform.percent != 100)
        out = out.scaled(std::max(1, out.width() * transform.percent / 100), std::max(1, out.height() * transform.percent / 100), Qt::IgnoreAspectRatio,
                         Qt::SmoothTransformation);
    if (transform.flatten.isValid()) {
        QImage flat(out.size(), QImage::Format_RGB32);
        flat.fill(transform.flatten);
        QPainter p(&flat);
        p.drawImage(0, 0, out);
        p.end();
        out = flat;
    }
    return out;
}

bool transformFile(QString const& in, QString const& out, Transform const& transform, QString* error)
{
    if (QString const why = CommandLine::readProblem(in); !why.isEmpty())
        return fail(error, tr("Unable to read: %1 (%2)").arg(QDir::toNativeSeparators(in), why));
    QImageReader reader(in);
    QImage const image = reader.read();
    if (image.isNull())
        return fail(error, tr("Unable to read: %1 (%2)").arg(QDir::toNativeSeparators(in), reader.errorString()));
    QHash<QRgb, QRgb> remap;
    if (!transform.remapFile.isEmpty() && !readRemap(transform.remapFile, remap, error))
        return false;
    if (QString const why = CommandLine::writeProblem(out); !why.isEmpty())
        return fail(error, QStringLiteral("%1: %2").arg(QDir::toNativeSeparators(out), why));
    QImage result = transformed(image, transform, remap);
    QString const format = !transform.format.isEmpty() ? transform.format : QFileInfo(out).suffix().toLower();
    if (format == QLatin1String("jpg") || format == QLatin1String("jpeg") || format == QLatin1String("bmp"))
        result = result.convertToFormat(QImage::Format_RGB32);
    QImageWriter writer(out, format.toLatin1());
    if (transform.quality >= 0)
        writer.setQuality(transform.quality);
    if (!writer.write(result))
        return fail(error, tr("Could not write output file.") + QStringLiteral(" %1: %2").arg(QDir::toNativeSeparators(out), writer.errorString()));
    return true;
}

// ---- relinking ----

bool readRelink(QString const& file, Relink& out, QString* error)
{
    out.clear();
    QByteArray bytes;
    if (!readBytes(file, bytes, error))
        return false;
    QXmlStreamReader xml(bytes);
    while (!xml.atEnd()) {
        if (xml.readNext() == QXmlStreamReader::StartElement && xml.name() == QLatin1String("path")) {
            QString const from = xml.attributes().value(QStringLiteral("from")).toString();
            QString const to = xml.attributes().value(QStringLiteral("to")).toString();
            if (!from.isEmpty())
                out.push_back({from, to});
        }
    }
    if (xml.hasError())
        return fail(error, tr("Formatting error: %1, line %2: %3").arg(QDir::toNativeSeparators(file)).arg(xml.lineNumber()).arg(xml.errorString()));
    if (out.isEmpty())
        return fail(error, tr("%1 has no <path from=\"...\" to=\"...\"/> in it.").arg(QDir::toNativeSeparators(file)));
    return true;
}

bool writeRelink(QString const& file, Relink const& rules, QString* error)
{
    QByteArray bytes;
    QXmlStreamWriter xml(&bytes);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    xml.writeComment(QStringLiteral(" Where a scene's media went (eclipse-convert -relink relink.xml scene.ecl): each path that starts with "
                                    "`from` is made to start with `to`; the first rule that fits is used. "));
    xml.writeStartElement(QStringLiteral("relink"));
    for (auto const& [from, to] : rules) {
        xml.writeStartElement(QStringLiteral("path"));
        xml.writeAttribute(QStringLiteral("from"), from);
        xml.writeAttribute(QStringLiteral("to"), to);
        xml.writeEndElement();
    }
    xml.writeEndElement();
    xml.writeEndDocument();
    return writeBytes(file, bytes, error);
}

int relinkScene(QString const& scene, Relink const& rules, bool dryRun, QStringList* changed, QString* error)
{
    QStringList files{scene};
    files << drawingFilesOf(QFileInfo(scene).absolutePath());
    auto change = [&](QString& text) {
        if (!looksLikePath(text) && !text.contains(QLatin1Char('/')) && !text.contains(QLatin1Char('\\')))
            return false;
        QString const n = normal(text);
        for (auto const& [from, to] : rules) {
            QString const f = normal(from);
            if (n.startsWith(f, Qt::CaseInsensitive) && (n.size() == f.size() || f.endsWith(QLatin1Char('/')) || n[f.size()] == QLatin1Char('/'))) {
                QString const made = normal(to) + n.mid(f.size());
                if (changed)
                    *changed << QStringLiteral("%1 -> %2").arg(QDir::toNativeSeparators(text), QDir::toNativeSeparators(made));
                text = made;
                return true;
            }
        }
        return false;
    };
    int total = 0;
    for (QString const& file : files) {
        QByteArray bytes;
        if (!readBytes(file, bytes, error))
            return -1;
        QJsonObject object;
        bool packed = false;
        if (!readObject(bytes, object, &packed)) {
            if (file == scene) {
                fail(error, tr("Formatting error: %1 is not a scene file.").arg(QDir::toNativeSeparators(file)));
                return -1;
            }
            continue;
        }
        QJsonValue value(object);
        int here = 0;
        walkStrings(value, change, here);
        total += here;
        if (here > 0 && !dryRun && !writeBytes(file, objectBytes(value.toObject(), packed), error))
            return -1;
    }
    return total;
}

QStringList scenePaths(QString const& scene, QStringList* missing)
{
    QStringList paths;
    QStringList files{scene};
    files << drawingFilesOf(QFileInfo(scene).absolutePath());
    for (QString const& file : files) {
        QFile f(file);
        QJsonObject object;
        if (!f.open(QIODevice::ReadOnly) || !readObject(f.readAll(), object))
            continue;
        eachString(object, [&](QString const& text) {
            if (looksLikePath(text) && !paths.contains(text, Qt::CaseInsensitive))
                paths << text;
        });
    }
    if (missing) {
        missing->clear();
        for (QString const& p : paths)
            if (!QFileInfo::exists(p))
                *missing << p;
    }
    return paths;
}

// ---- compacting ----

int compactScene(QString const& scene, bool dryRun, QStringList* unused, QString* error)
{
    QByteArray bytes;
    if (!readBytes(scene, bytes, error))
        return -1;
    QJsonObject object;
    if (!readObject(bytes, object) || object[QStringLiteral("format")].toString() != QLatin1String("eclipse-scene")) {
        fail(error, tr("Formatting error: %1 is not a scene file.").arg(QDir::toNativeSeparators(scene)));
        return -1;
    }
    QDir const folder = QFileInfo(scene).absoluteDir();
    // What every scene file of the folder names (other versions of the scene share its files).
    QSet<QString> named;
    auto note = [&](QString const& text) {
        if (text.isEmpty() || text.size() > 1024)
            return;
        QString n = normal(text);
        if (QDir::isAbsolutePath(n))
            n = folder.relativeFilePath(n);
        named.insert(n.toLower());
    };
    QStringList scenes;
    for (QString const& name : folder.entryList({QStringLiteral("*.ecl"), QStringLiteral("*.") + QLatin1String(SceneProject::kTableSuffix)}, QDir::Files))
        scenes << folder.filePath(name);
    QStringList pending;
    for (QString const& s : scenes) {
        QFile f(s);
        QJsonObject o;
        if (f.open(QIODevice::ReadOnly) && readObject(f.readAll(), o) && o[QStringLiteral("format")].toString() == QLatin1String("eclipse-scene"))
            eachString(o, note);
    }
    // The drawing files named name their pictures in turn.
    for (QString const& d : drawingFilesOf(folder.path())) {
        QString const relative = folder.relativeFilePath(d).toLower();
        if (!named.contains(relative))
            continue;
        QFile f(d);
        QJsonObject o;
        if (f.open(QIODevice::ReadOnly) && readObject(f.readAll(), o))
            for (QJsonValue const& p : o[QStringLiteral("pictures")].toArray()) {
                QString const key = p.toObject()[QStringLiteral("image")].toString();
                if (!key.isEmpty())
                    note(key);
            }
    }
    QStringList found;
    for (char const* sub : {SceneProject::kElements, SceneProject::kPalettes, SceneProject::kAudio}) {
        QDirIterator it(folder.filePath(QLatin1String(sub)), QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QString const file = it.next();
            if (!named.contains(folder.relativeFilePath(file).toLower()))
                found << folder.relativeFilePath(file);
        }
    }
    std::sort(found.begin(), found.end());
    if (unused)
        *unused = found;
    if (dryRun)
        return int(found.size());
    QString const trash = folder.filePath(QStringLiteral(".trash/") + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    for (QString const& file : std::as_const(found)) {
        QString const to = QDir(trash).filePath(file);
        QDir().mkpath(QFileInfo(to).absolutePath());
        if (!QFile::rename(folder.filePath(file), to)) {
            fail(error, tr("Could not move %1 to %2").arg(QDir::toNativeSeparators(file), QDir::toNativeSeparators(trash)));
            return -1;
        }
    }
    return int(found.size());
}

// ---- zip archives ----

int zipFiles(QStringList const& paths, QString const& archive, QString* error)
{
    QList<QPair<QString, QByteArray>> files;
    for (QString const& path : paths) {
        QFileInfo const info(path);
        if (info.isDir()) {
            QDir const base = QDir(info.absoluteFilePath());
            QString const top = base.dirName();
            QDirIterator it(base.path(), QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                QString const file = it.next();
                QByteArray bytes;
                if (!readBytes(file, bytes, error))
                    return -1;
                files.push_back({top + QLatin1Char('/') + base.relativeFilePath(file), bytes});
            }
        } else {
            QByteArray bytes;
            if (!readBytes(path, bytes, error))
                return -1;
            files.push_back({info.fileName(), bytes});
        }
    }
    if (files.isEmpty()) {
        fail(error, tr("There is nothing to put in %1.").arg(QDir::toNativeSeparators(archive)));
        return -1;
    }
    if (!writeBytes(archive, Zip::write(files), error))
        return -1;
    return int(files.size());
}

int unzipFiles(QString const& archive, QString const& folder, QStringList* written, QString* error)
{
    QByteArray bytes;
    if (!readBytes(archive, bytes, error))
        return -1;
    QHash<QString, QByteArray> const files = Zip::read(bytes);
    if (files.isEmpty()) {
        fail(error, tr("Formatting error: %1 is not a zip archive (or it is empty or damaged).").arg(QDir::toNativeSeparators(archive)));
        return -1;
    }
    if (QString const why = CommandLine::folderProblem(folder); !why.isEmpty()) {
        fail(error, QStringLiteral("%1: %2").arg(QDir::toNativeSeparators(folder), why));
        return -1;
    }
    QStringList names = files.keys();
    std::sort(names.begin(), names.end());
    int made = 0;
    QString const root = QDir(folder).absolutePath();
    for (QString const& name : std::as_const(names)) {
        // Nothing written outside the folder.
        QString const to = QDir::cleanPath(QDir(root).filePath(name));
        if (!to.startsWith(root + QLatin1Char('/')) || name.contains(QLatin1String("..")))
            continue;
        if (name.endsWith(QLatin1Char('/'))) {
            QDir().mkpath(to);
            continue;
        }
        if (!writeBytes(to, files.value(name), error))
            return -1;
        if (written)
            *written << to;
        ++made;
    }
    return made;
}

// ---- the program ----

QString usage()
{
    return tr("Usage: eclipse-convert <in> <out> [options]   (what is done goes by the files' kinds)\n"
              "\n"
              "  drawing.ecd drawing.json      a drawing file as readable text\n"
              "  drawing.json drawing.ecd      and back  (-plain: not packed)\n"
              "  movie.swf <folder>            each frame of a Flash movie as a drawing file (-prefix <name>)\n"
              "  picture.png drawing.ecd       a picture as a drawing: -frame WxH, -alignment <name>,\n"
              "                                -vectorize to make it lines and shapes (-style <name>, or -style \"<options>\")\n"
              "  drawing.ecd picture.png       a drawing as a picture: -frame WxH, -background #rrggbb\n"
              "\n"
              "  -transform <in> <out> [-resize WxH] [-scale %] [-flip h|v] [-rotate 90|180|270]\n"
              "                [-premultiply on|off] [-remap <colours file>] [-grey] [-flatten #rrggbb]\n"
              "                [-format <png|jpg|tif...>] [-quality 0-100]\n"
              "  -relink <relink.xml> <scene.ecl> [-dryrun]   the paths of a scene's media changed\n"
              "  -paths <scene.ecl>            the files a scene uses from outside its folder (missing ones marked)\n"
              "  -compact <scene.ecl> [-dryrun]   files of the scene's folder no longer used, put in its .trash\n"
              "  -zip <archive.zip> <files or folders...>\n"
              "  -unzip <archive.zip> <folder>\n"
              "  -alignments [<file>]          the alignments a picture can be put in a frame with\n"
              "  -styles [<file>]              the vectorization styles\n"
              "  -options <file>, @file        more arguments from a file\n"
              "  -version, -help\n"
              "\n"
              "Exit codes: 0 done, 1 failed, 2 wrong arguments.");
}

int run(QStringList const& arguments, Say const& say)
{
    QStringList words, used;
    QString error;
    if (!CommandLine::expand(arguments, words, &used, &error)) {
        say(error, true);
        return 2;
    }
    for (QString const& file : used)
        say(tr("Options file used: %1").arg(QDir::toNativeSeparators(file)), false);
    auto wrong = [&](QString const& why) {
        say(why.isEmpty() ? tr("Wrong command line arguments.") : why, true);
        say(tr("eclipse-convert -help shows how it is used."), true);
        return 2;
    };
    auto failed = [&](QString const& why) {
        say(why, true);
        return 1;
    };
    if (words.isEmpty())
        return wrong(QString());
    QString const command = words[0];
    if (command == QLatin1String("-help") || command == QLatin1String("--help") || command == QLatin1String("-h") || command == QLatin1String("/?")) {
        say(usage(), false);
        return 0;
    }
    if (command == QLatin1String("-version") || command == QLatin1String("--version")) {
        say(CommandLine::versionLine(), false);
        return 0;
    }
    bool const dryRun = words.contains(QStringLiteral("-dryrun"));
    if (command == QLatin1String("-transform")) {
        if (words.size() < 3)
            return wrong(tr("Wrong command line arguments.") + QLatin1Char(' ') + tr("-transform needs a picture to read and one to write."));
        Transform t;
        if (!parseTransform(words.mid(3), t, &error))
            return wrong(error);
        if (!transformFile(words[1], words[2], t, &error))
            return failed(error);
        say(tr("Wrote %1").arg(QDir::toNativeSeparators(words[2])), false);
        return 0;
    }
    if (command == QLatin1String("-relink")) {
        if (words.size() < 3)
            return wrong(QString());
        Relink rules;
        if (!readRelink(words[1], rules, &error))
            return failed(error);
        QStringList changed;
        int const n = relinkScene(words[2], rules, dryRun, &changed, &error);
        if (n < 0)
            return failed(error);
        for (QString const& c : changed)
            say(c, false);
        say(dryRun ? tr("%n path(s) would be relinked.", nullptr, n) : tr("%n path(s) relinked.", nullptr, n), false);
        return 0;
    }
    if (command == QLatin1String("-paths")) {
        if (words.size() < 2)
            return wrong(QString());
        if (QString const why = CommandLine::readProblem(words[1]); !why.isEmpty())
            return failed(QStringLiteral("%1: %2").arg(QDir::toNativeSeparators(words[1]), why));
        QStringList missing;
        for (QString const& p : scenePaths(words[1], &missing))
            say(missing.contains(p) ? tr("%1 (missing)").arg(QDir::toNativeSeparators(p)) : QDir::toNativeSeparators(p), false);
        return missing.isEmpty() ? 0 : 1;
    }
    if (command == QLatin1String("-compact")) {
        if (words.size() < 2)
            return wrong(QString());
        QStringList unused;
        int const n = compactScene(words[1], dryRun, &unused, &error);
        if (n < 0)
            return failed(error);
        for (QString const& u : unused)
            say(QDir::toNativeSeparators(u), false);
        say(dryRun ? tr("%n file(s) are no longer used.", nullptr, n) : tr("%n file(s) no longer used were put in the folder's .trash.", nullptr, n), false);
        return 0;
    }
    if (command == QLatin1String("-zip")) {
        if (words.size() < 3)
            return wrong(QString());
        int const n = zipFiles(words.mid(2), words[1], &error);
        if (n < 0)
            return failed(error);
        say(tr("%n file(s) put in %1", nullptr, n).arg(QDir::toNativeSeparators(words[1])), false);
        return 0;
    }
    if (command == QLatin1String("-unzip")) {
        if (words.size() < 3)
            return wrong(QString());
        int const n = unzipFiles(words[1], words[2], nullptr, &error);
        if (n < 0)
            return failed(error);
        say(tr("%n file(s) taken out to %1", nullptr, n).arg(QDir::toNativeSeparators(words[2])), false);
        return 0;
    }
    if (command == QLatin1String("-alignments")) {
        for (Alignment const& al : alignments(words.value(1)))
            say(QStringLiteral("%1: fit %2, %3 %4, scale %5").arg(al.name, kFits[std::clamp(al.fit, 0, 4)], sideName(al.horizontal, true), sideName(al.vertical, false))
                    .arg(al.scale),
                false);
        return 0;
    }
    if (command == QLatin1String("-styles")) {
        for (QString const& style : vectorizeStyles(words.value(1)))
            say(QStringLiteral("%1 = %2").arg(style, vectorizeLine(style, words.value(1))), false);
        return 0;
    }

    // <in> <out> [options]
    QStringList files;
    QString prefix, alignmentName = QStringLiteral("Fit"), vectorize;
    bool plain = false, vectorizing = false;
    QSize frame;
    QColor background = Qt::transparent;
    for (qsizetype i = 0; i < words.size(); ++i) {
        QString const w = words[i];
        auto value = [&](QString& to) {
            if (i + 1 >= words.size())
                return false;
            to = words[++i];
            return true;
        };
        QString text;
        if (w == QLatin1String("-plain")) {
            plain = true;
        } else if (w == QLatin1String("-prefix")) {
            if (!value(prefix))
                return wrong(QString());
        } else if (w == QLatin1String("-alignment")) {
            if (!value(alignmentName))
                return wrong(QString());
        } else if (w == QLatin1String("-frame")) {
            if (!value(text))
                return wrong(QString());
            QStringList const parts = text.toLower().split(QLatin1Char('x'));
            frame = QSize(parts.value(0).toInt(), parts.value(1).toInt());
            if (parts.size() != 2 || frame.isEmpty())
                return wrong(tr("Wrong value specified for -frame: %1").arg(text));
        } else if (w == QLatin1String("-background")) {
            if (!value(text) || !QColor(text).isValid())
                return wrong(tr("Wrong value specified for -background: %1").arg(text));
            background = QColor(text);
        } else if (w == QLatin1String("-vectorize")) {
            vectorizing = true;
        } else if (w == QLatin1String("-style")) {
            if (!value(vectorize))
                return wrong(QString());
            vectorizing = true;
        } else if (w.startsWith(QLatin1Char('-')) && w.size() > 1 && w != QLatin1String("-dryrun")) {
            return wrong(tr("Unknown command-line argument: %1").arg(w));
        } else if (w != QLatin1String("-dryrun")) {
            files << w;
        }
    }
    if (files.size() != 2)
        return wrong(tr("Wrong command line arguments.") + QLatin1Char(' ') + tr("Give a file to read and one to write (eclipse-convert -help)."));
    QString const in = files[0], out = files[1];
    QString const inSuffix = QFileInfo(in).suffix().toLower(), outSuffix = QFileInfo(out).suffix().toLower();
    QString const drawingSuffix = QLatin1String(SceneProject::kDrawingSuffix);
    if (QString const why = CommandLine::readProblem(in); !why.isEmpty())
        return failed(tr("Could not read input file.") + QStringLiteral(" %1: %2").arg(QDir::toNativeSeparators(in), why));

    if (inSuffix == drawingSuffix && outSuffix == QLatin1String("json")) {
        if (!drawingToText(in, out, &error))
            return failed(error);
    } else if (inSuffix == QLatin1String("json") && outSuffix == drawingSuffix) {
        if (!textToDrawing(in, out, !plain, &error))
            return failed(error);
    } else if (inSuffix == QLatin1String("swf")) {
        QStringList written;
        int const n = swfToDrawings(in, out, prefix, !plain, &written, &error);
        if (n < 0)
            return failed(error);
        say(tr("%n drawing(s) written to %1", nullptr, n).arg(QDir::toNativeSeparators(out)), false);
        return 0;
    } else if (isImageFile(in) && outSuffix == drawingSuffix) {
        QImageReader reader(in);
        QImage const image = reader.read();
        if (image.isNull())
            return failed(tr("Unable to read: %1 (%2)").arg(QDir::toNativeSeparators(in), reader.errorString()));
        QVector<Alignment> const all = alignments();
        auto al = std::find_if(all.begin(), all.end(), [&](Alignment const& a) { return a.name.compare(alignmentName, Qt::CaseInsensitive) == 0; });
        if (al == all.end())
            return wrong(tr("There is no alignment called %1 (eclipse-convert -alignments lists them).").arg(alignmentName));
        Drawing drawing;
        if (vectorizing) {
            QString const line = vectorizeLine(vectorize.isEmpty() ? Vectorize::styleNames().value(0) : vectorize);
            if (line.isEmpty() && !vectorize.isEmpty())
                return wrong(tr("There is no vectorization style called %1 (eclipse-convert -styles lists them).").arg(vectorize));
            if (!vectorizePicture(image, line, frame, *al, drawing, &error))
                return failed(error);
        } else {
            drawing = pictureDrawing(image, frame, *al);
        }
        if (!writeDrawing(drawing, out, !plain, &error, tr("Generated by eclipse-convert from %1").arg(QFileInfo(in).fileName())))
            return failed(error);
    } else if (inSuffix == drawingSuffix && isImageFile(out)) {
        Drawing drawing;
        if (!readDrawing(in, drawing, &error))
            return failed(error);
        QImage image = drawingPicture(drawing, frame, background);
        if (outSuffix == QLatin1String("jpg") || outSuffix == QLatin1String("jpeg") || outSuffix == QLatin1String("bmp"))
            image = image.convertToFormat(QImage::Format_RGB32);
        if (QString const why = CommandLine::writeProblem(out); !why.isEmpty())
            return failed(tr("Could not write output file.") + QStringLiteral(" %1: %2").arg(QDir::toNativeSeparators(out), why));
        if (!image.save(out))
            return failed(tr("Could not write output file.") + QStringLiteral(" %1").arg(QDir::toNativeSeparators(out)));
    } else if (isImageFile(in) && isImageFile(out)) {
        if (!transformFile(in, out, Transform(), &error))
            return failed(error);
    } else {
        return wrong(tr("There is no conversion from .%1 to .%2 (eclipse-convert -help lists them).").arg(inSuffix, outSuffix));
    }
    say(tr("Wrote %1").arg(QDir::toNativeSeparators(out)), false);
    return 0;
}

}  // namespace FileConvert
