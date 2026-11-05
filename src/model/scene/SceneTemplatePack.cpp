// Eclipse - templates and the files they use: those outside the template's file listed, copied beside it, pointed at again.
#include "model/scene/Scene.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>
#include <QUrl>

#include "model/geometry/Model3D.h"

namespace {

// Each place a scene names a file outside itself, given to `use` to read or change.
template <class Use> void eachFile(Scene& scene, Use use)
{
    auto inLayers = [&](QVector<Layer>& layers) {
        for (Layer& layer : layers) {
            use(layer.soundFile);
            for (SoundClip& clip : layer.soundClips)
                use(clip.file);
            for (Drawing& d : layer.drawings) {
                for (Picture& p : d.pictures)
                    if (p.linked)
                        use(p.file);
                use(d.model);
            }
        }
    };
    inLayers(scene.layers);
    for (Scene::Symbol& symbol : scene.symbols)
        inLayers(symbol.layers);
    for (QString& reference : scene.references)
        use(reference);
}

// The files an OBJ model reads beside it: its material files and the pictures they name (as written in them).
QStringList modelParts(QString const& obj)
{
    QStringList parts;
    auto read = [](QString const& file, QRegularExpression const& line) {
        QStringList found;
        QFile in(file);
        if (!in.open(QIODevice::ReadOnly | QIODevice::Text))
            return found;
        QTextStream text(&in);
        while (!text.atEnd()) {
            QRegularExpressionMatch const m = line.match(text.readLine().trimmed());
            if (m.hasMatch())
                found << m.captured(1).trimmed();
        }
        return found;
    };
    static QRegularExpression const mtl(QStringLiteral("^mtllib\\s+(.+)$"));
    // (A map line ends with the file; options such as -s 1 1 1 come before it.)
    static QRegularExpression const map(QStringLiteral("^(?:map_\\w+|bump|disp|decal|refl)\\s+(?:.*\\s)?(\\S+)$"));
    QDir const beside = QFileInfo(obj).absoluteDir();
    if (QFileInfo(obj).suffix().compare(QStringLiteral("gltf"), Qt::CaseInsensitive) == 0) {
        // glTF: the buffers and pictures it names beside it (those inside it, as data, need nothing more).
        QFile in(obj);
        if (!in.open(QIODevice::ReadOnly))
            return parts;
        QJsonObject const root = QJsonDocument::fromJson(in.readAll()).object();
        for (char const* list : {"buffers", "images"})
            for (QJsonValue const& v : root[QLatin1String(list)].toArray())
                if (QString const uri = v.toObject()["uri"].toString(); !uri.isEmpty() && !uri.startsWith(QStringLiteral("data:")) && !parts.contains(uri))
                    parts << QUrl::fromPercentEncoding(uri.toUtf8());
        return parts;
    }
    for (QString const& library : read(obj, mtl)) {
        parts << library;
        for (QString const& picture : read(beside.absoluteFilePath(library), map))
            if (!parts.contains(picture))
                parts << picture;
    }
    return parts;
}

}  // namespace

QStringList Scene::externalFiles() const
{
    QStringList files;
    Scene& self = const_cast<Scene&>(*this);  // (eachFile only reads here)
    eachFile(self, [&](QString& file) {
        if (!file.isEmpty() && !files.contains(file))
            files << file;
    });
    return files;
}

int Scene::repointFiles(QHash<QString, QString> const& moved)
{
    int changed = 0;
    eachFile(*this, [&](QString& file) {
        auto const to = moved.constFind(file);
        if (to != moved.constEnd() && to.value() != file) {
            file = to.value();
            ++changed;
        }
    });
    return changed;
}

int Scene::repointFolder(QString const& from, QString const& to)
{
    QString const old = QDir::cleanPath(QDir(from).absolutePath()) + QLatin1Char('/');
    QString const now = QDir::cleanPath(QDir(to).absolutePath()) + QLatin1Char('/');
    int changed = 0;
    eachFile(*this, [&](QString& file) {
        QString const clean = QDir::cleanPath(file);
        if (clean.startsWith(old, Qt::CaseInsensitive)) {
            file = now + clean.mid(old.size());
            ++changed;
        }
    });
    return changed;
}

QString Scene::templateFilesFolder(QString const& templatePath)
{
    QFileInfo const info(templatePath);
    return info.absoluteDir().filePath(info.completeBaseName() + QStringLiteral(".files"));
}

int Scene::packFiles(QString const& folder, QString* error)
{
    QStringList const files = externalFiles();
    QString const into = QDir::cleanPath(QDir(folder).absolutePath());
    QHash<QString, QString> moved;
    QSet<QString> taken;
    int copied = 0;
    auto fail = [&](QString const& why) {
        if (error)
            *error = why;
        return -1;
    };
    // A name in the folder for a file of the scene (the same name again for another file: a number added).
    auto place = [&](QString const& name) {
        QFileInfo const info(name);
        QString chosen = name;
        for (int n = 2; taken.contains(chosen.toLower()); ++n)
            chosen = info.completeBaseName() + QStringLiteral("_%1").arg(n) + (info.suffix().isEmpty() ? QString() : QLatin1Char('.') + info.suffix());
        taken.insert(chosen.toLower());
        return chosen;
    };
    auto copy = [&](QString const& from, QString const& to) {
        if (QFileInfo(from).absoluteFilePath().compare(QFileInfo(to).absoluteFilePath(), Qt::CaseInsensitive) == 0)
            return true;    // packed already
        if (!QDir().mkpath(QFileInfo(to).absolutePath()))
            return false;
        QFile::remove(to);
        return QFile::copy(from, to) && QFileInfo(to).size() == QFileInfo(from).size();
    };
    for (QString const& file : files) {
        QFileInfo const source(file);
        if (!source.isFile())
            continue;       // missing: nothing to take along (the template keeps naming it)
        if (QDir::cleanPath(source.absoluteFilePath()).startsWith(into + QLatin1Char('/'), Qt::CaseInsensitive))
            continue;       // already in the folder
        bool const model = Model3D::isModelFile(source.fileName());
        // A model goes into a folder of its own, its materials and their pictures where it looks for them.
        QString const name = place(source.fileName());
        QString const target = model ? QDir(into).filePath(QFileInfo(name).completeBaseName() + QStringLiteral("/") + source.fileName())
                                     : QDir(into).filePath(name);
        if (!copy(source.absoluteFilePath(), target))
            return fail(QObject::tr("Could not copy %1 into %2.").arg(QDir::toNativeSeparators(file), QDir::toNativeSeparators(into)));
        ++copied;
        if (model)
            for (QString const& part : modelParts(source.absoluteFilePath())) {
                QString const relative = QDir::cleanPath(part);
                if (relative.startsWith(QStringLiteral("..")) || QDir::isAbsolutePath(relative))
                    continue;   // (outside the model's folder: left where it is)
                QString const from = source.absoluteDir().filePath(relative);
                if (QFileInfo(from).isFile() && copy(from, QFileInfo(target).absoluteDir().filePath(relative)))
                    ++copied;
            }
        moved.insert(file, target);
    }
    repointFiles(moved);
    return copied;
}

int Scene::scaleModels(double factor)
{
    if (!(factor > 0.0))
        return 0;
    int scaled = 0;
    auto inLayers = [&](QVector<Layer>& all) {
        for (Layer& layer : all)
            for (Drawing& d : layer.drawings)
                if (!d.model.isEmpty()) {
                    d.modelScale *= factor;
                    ++scaled;
                }
    };
    inLayers(layers);
    for (Symbol& symbol : symbols)
        inLayers(symbol.layers);
    return scaled;
}

bool Scene::hasModels() const
{
    auto in = [](QVector<Layer> const& all) {
        for (Layer const& layer : all)
            for (Drawing const& d : layer.drawings)
                if (!d.model.isEmpty())
                    return true;
        return false;
    };
    if (in(layers))
        return true;
    for (Symbol const& symbol : symbols)
        if (in(symbol.layers))
            return true;
    return false;
}
