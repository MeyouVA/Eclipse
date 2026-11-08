#include "animation/ui/tools/CommandLine.h"

#include "actions/manager/ActionManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStorageInfo>

#include <cstdio>
#include <limits>

namespace CommandLine {

QStringList splitWords(QString const& text)
{
    QStringList words;
    QString word;
    bool inWord = false;
    QChar quote;
    for (qsizetype i = 0; i < text.size(); ++i) {
        QChar const c = text[i];
        if (!quote.isNull()) {
            if (c == quote)
                quote = QChar();
            else
                word += c;
            continue;
        }
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            quote = c;
            inWord = true;
        } else if (c == QLatin1Char('#') && !inWord) {
            // The rest of the line is a note.
            while (i < text.size() && text[i] != QLatin1Char('\n'))
                ++i;
        } else if (c.isSpace()) {
            if (inWord)
                words << word;
            word.clear();
            inWord = false;
        } else {
            word += c;
            inWord = true;
        }
    }
    if (inWord)
        words << word;
    return words;
}

namespace {

bool expandInto(QStringList const& arguments, QStringList& out, QStringList& open, QStringList* used, QString* error)
{
    for (qsizetype i = 0; i < arguments.size(); ++i) {
        QString file;
        if (arguments[i] == QLatin1String("-options") || arguments[i] == QLatin1String("--options")) {
            if (i + 1 >= arguments.size()) {
                if (error)
                    *error = QObject::tr("Wrong command line arguments.") + QLatin1Char(' ') + QObject::tr("-options needs the name of a file.");
                return false;
            }
            file = arguments[++i];
        } else if (arguments[i].startsWith(QLatin1Char('@')) && arguments[i].size() > 1) {
            file = arguments[i].mid(1);
        } else {
            out << arguments[i];
            continue;
        }
        QString const whole = QFileInfo(file).absoluteFilePath();
        if (open.contains(whole, Qt::CaseInsensitive)) {
            if (error)
                *error = QObject::tr("The options file %1 names itself.").arg(QDir::toNativeSeparators(file));
            return false;
        }
        if (QString const why = readProblem(file); !why.isEmpty()) {
            if (error)
                *error = QStringLiteral("%1: %2").arg(QDir::toNativeSeparators(file), why);
            return false;
        }
        QFile f(file);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            if (error)
                *error = QStringLiteral("%1: %2").arg(QDir::toNativeSeparators(file), f.errorString());
            return false;
        }
        if (used)
            *used << whole;
        open << whole;
        bool const ok = expandInto(splitWords(QString::fromUtf8(f.readAll())), out, open, used, error);
        open.removeLast();
        if (!ok)
            return false;
    }
    return true;
}

// The parts of a path, each at most so long, and the whole at most so long (as Windows has them).
bool tooLong(QString const& path)
{
    QString const whole = QFileInfo(path).absoluteFilePath();
    if (whole.size() > 32000)
        return true;
    for (QString const& part : whole.split(QLatin1Char('/'), Qt::SkipEmptyParts))
        if (part.size() > 255)
            return true;
    return false;
}

// The nearest folder above the path that is there.
QString existingParent(QString const& path)
{
    QFileInfo info(QFileInfo(path).absoluteFilePath());
    QString at = info.absolutePath();
    while (!at.isEmpty() && !QFileInfo(at).exists()) {
        QString const up = QFileInfo(at).absolutePath();
        if (up == at)
            break;
        at = up;
    }
    return at;
}

}  // namespace

bool expand(QStringList const& arguments, QStringList& out, QStringList* used, QString* error)
{
    QStringList open;
    out.clear();
    return expandInto(arguments, out, open, used, error);
}

QString readProblem(QString const& path)
{
    if (path.isEmpty())
        return QObject::tr("No such file or directory");
    if (tooLong(path))
        return QObject::tr("Filename too long");
    QFileInfo const info(path);
    if (!info.exists()) {
        // A file where a folder of its path is to be.
        QString const parent = existingParent(path);
        if (!parent.isEmpty() && QFileInfo(parent).isFile())
            return QObject::tr("Not a directory");
        return QObject::tr("No such file or directory");
    }
    if (info.isDir())
        return QObject::tr("Is a directory");
    if (!info.isReadable())
        return QObject::tr("Permission denied");
    if (info.size() > qint64(std::numeric_limits<int>::max()))
        return QObject::tr("File too large");
    return QString();
}

QString folderProblem(QString const& folder)
{
    if (folder.isEmpty())
        return QObject::tr("No such file or directory");
    if (tooLong(folder))
        return QObject::tr("Filename too long");
    QFileInfo const info(folder);
    QString at = folder;
    if (info.exists()) {
        if (!info.isDir())
            return QObject::tr("Not a directory");
    } else {
        at = existingParent(folder);
        if (at.isEmpty())
            return QObject::tr("No such file or directory");
        if (!QFileInfo(at).isDir())
            return QObject::tr("Not a directory");
    }
    QStorageInfo const storage(at);
    if (storage.isValid() && storage.isReadOnly())
        return QObject::tr("Read-only file system");
    if (!QFileInfo(at).isWritable())
        return QObject::tr("Permission denied");
    if (storage.isValid() && storage.bytesAvailable() == 0)
        return QObject::tr("No space left on device");
    return QString();
}

QString writeProblem(QString const& path)
{
    if (path.isEmpty())
        return QObject::tr("No such file or directory");
    if (tooLong(path))
        return QObject::tr("Filename too long");
    QFileInfo const info(path);
    if (info.isDir())
        return QObject::tr("Is a directory");
    if (QString const why = folderProblem(info.absolutePath()); !why.isEmpty())
        return why;
    if (info.exists() && !info.isWritable())
        return QObject::tr("Permission denied");
    return QString();
}

void print(QString const& text)
{
    QByteArray const bytes = text.toUtf8();
    std::fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

void printError(QString const& text)
{
    QByteArray const bytes = text.toUtf8();
    std::fwrite(bytes.constData(), 1, size_t(bytes.size()), stderr);
    std::fputc('\n', stderr);
    std::fflush(stderr);
}

QString dataFolder()
{
    QString const set = qEnvironmentVariable("ECLIPSE_TOOLS_FOLDER");
    QString const folder = !set.isEmpty() ? set : QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/Eclipse");
    QDir().mkpath(folder);
    return folder;
}

QString versionFile()
{
    return ActionManager::defaultDirectory() + QStringLiteral("/version.txt");
}

QString versionLine()
{
    QString product = QStringLiteral("Eclipse"), version = QStringLiteral("0.3.0"), build;
    QFile file(versionFile());
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        for (QString const& line : QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'))) {
            qsizetype const eq = line.indexOf(QLatin1Char('='));
            if (line.trimmed().startsWith(QLatin1Char('#')) || eq < 0)
                continue;
            QString const key = line.left(eq).trimmed().toLower(), value = line.mid(eq + 1).trimmed();
            if (key == QLatin1String("product"))
                product = value;
            else if (key == QLatin1String("version"))
                version = value;
            else if (key == QLatin1String("build"))
                build = value;
        }
    }
    return build.isEmpty() ? QStringLiteral("%1 %2").arg(product, version) : QStringLiteral("%1 %2 (%3)").arg(product, version, build);
}

}  // namespace CommandLine
