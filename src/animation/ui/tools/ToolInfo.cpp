// Eclipse - the information and status program (see ToolInfo.h).
#include "animation/ui/tools/ToolInfo.h"

#include "formats/archive/Zip.h"
#include "model/scene/Scene.h"
#include "model/scene/SceneDatabase.h"
#include "model/scene/SceneProject.h"
#include "animation/ui/io/RenderQueue.h"
#include "animation/ui/tools/CommandLine.h"
#include "actions/config/ConfigFile.h"
#include "animation/ui/tools/FileConvert.h"
#include "animation/ui/tools/ServiceLauncher.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QSettings>
#include <QStorageInfo>
#include <QSysInfo>
#include <QThread>
#include <QtGlobal>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#if defined(_M_X64) || defined(_M_IX86)
#include <intrin.h>
#endif
#endif

#include <algorithm>
#include <cstring>

namespace ToolInfo {

namespace {

QString tr(char const* text, char const* note = nullptr, int n = -1) { return QCoreApplication::translate("ToolInfo", text, note, n); }

QString bytesText(qint64 bytes) { return QLocale().formattedDataSize(bytes); }

quint32 be32(QByteArray const& b, int at) { return at + 4 <= b.size() ? (quint32(quint8(b[at])) << 24) | (quint32(quint8(b[at + 1])) << 16) | (quint32(quint8(b[at + 2])) << 8) | quint8(b[at + 3]) : 0; }

QString layerKindName(int kind)
{
    switch (kind) {
    case Layer::DrawingLayer: return tr("drawing");
    case Layer::Peg: return tr("peg");
    case Layer::Camera: return tr("camera");
    case Layer::Sound: return tr("sound");
    case Layer::ColourCard: return tr("colour card");
    case Layer::Group: return tr("group");
    case Layer::Bone: return tr("bone");
    case Layer::Quadmap: return tr("quadmap");
    case Layer::Annotation: return tr("annotation");
    default: return tr("other");
    }
}

void describeScene(QString const& path, QJsonObject const& json, bool packed, Description& d)
{
    d.kind = QStringLiteral("scene");
    d.format = tr("Eclipse scene, version %1").arg(json[QStringLiteral("version")].toInt(1));
    bool const folder = SceneProject::usesFolder(json);
    d.details << (folder ? tr("Saved as a folder: its drawings and palettes in files of their own")
                         : tr("Legacy format: an older scene kept all in one file"));
    d.details << (packed ? tr("Packed (zlib)") : tr("Plain text (JSON)"));
    QString const source = json[QStringLiteral("savedWith")].toString();
    d.details << (source.isEmpty() ? tr("Saved from an unknown Source") : tr("Saved with %1").arg(source));
    Scene scene;
    QString error;
    if (!Scene::load(path, scene, &error)) {
        d.details << tr("It could not be read: %1").arg(error);
        return;
    }
    d.details << tr("Size: %1 x %2, %3 frames a second").arg(scene.width).arg(scene.height).arg(scene.fps);
    d.details << tr("Frames: %1").arg(scene.frameCount());
    QMap<QString, int> kinds;
    int drawings = 0;
    QStringList sounds;
    for (Layer const& layer : scene.layers) {
        ++kinds[layerKindName(layer.kind)];
        drawings += int(layer.drawings.size());
        if (!layer.soundFile.isEmpty())
            sounds << layer.soundFile;
    }
    QStringList kindText;
    for (auto it = kinds.constBegin(); it != kinds.constEnd(); ++it)
        kindText << QStringLiteral("%1 %2").arg(it.value()).arg(it.key());
    d.details << tr("Layers: %1 (%2)").arg(scene.layers.size()).arg(kindText.join(QStringLiteral(", ")));
    d.details << tr("Drawings: %1").arg(drawings);
    d.details << tr("Palettes: %1, colours: %2").arg(scene.paletteNames.size()).arg(scene.palette.size());
    if (!scene.symbols.isEmpty())
        d.details << tr("Symbols: %1").arg(scene.symbols.size());
    if (!scene.storyboard.shots.isEmpty())
        d.details << tr("Storyboard: %1 shots").arg(scene.storyboard.shots.size());
    for (QString const& s : sounds)
        d.details << tr("Sound: %1%2").arg(QDir::toNativeSeparators(s), QFileInfo::exists(s) ? QString() : tr(" (missing)"));
    for (auto const& [key, value] : scene.metadata)
        d.details << tr("Metadata: %1 = %2").arg(key, value);
    if (!Scene::lastMissingFiles().isEmpty())
        d.details << tr("Missing from its folder: %1 file(s)").arg(Scene::lastMissingFiles().size());
}

void describeJson(QString const& path, QJsonObject const& json, bool packed, Description& d)
{
    QString const packing = packed ? tr("Packed (zlib)") : tr("Plain text (JSON)");
    if (json[QStringLiteral("format")].toString() == QLatin1String("eclipse-scene")) {
        describeScene(path, json, packed, d);
    } else if (json.contains(QStringLiteral("eclipseDrawing"))) {
        d.kind = QStringLiteral("drawing");
        d.format = tr("Eclipse drawing");
        d.details << packing;
        auto count = [&](char const* key) { return json[QLatin1String(key)].toArray().size(); };
        d.details << tr("Line art: %1 shapes, %2 lines").arg(count("fills")).arg(count("strokes"));
        if (count("colourFills") || count("colourStrokes"))
            d.details << tr("Colour art: %1 shapes, %2 lines").arg(count("colourFills")).arg(count("colourStrokes"));
        if (count("pictures"))
            d.details << tr("Pictures: %1").arg(count("pictures"));
        QString const source = json[QStringLiteral("source")].toString();
        d.details << (source.isEmpty() ? tr("Saved from an unknown Source") : source);
    } else if (json.contains(QStringLiteral("eclipsePalette")) || json.contains(QStringLiteral("eclipseScenePalette"))) {
        d.kind = QStringLiteral("palette");
        d.format = json.contains(QStringLiteral("eclipsePalette")) ? tr("Eclipse palette") : tr("Eclipse scene palette");
        d.details << packing;
        d.details << tr("Name: %1").arg(json[QStringLiteral("name")].toString());
        qsizetype const colours = std::max(json[QStringLiteral("colours")].toArray().size(), json[QStringLiteral("swatches")].toArray().size());
        d.details << tr("Colours: %1").arg(colours);
    } else if (json.contains(QStringLiteral("eclipseElements"))) {
        d.kind = QStringLiteral("json");
        d.format = tr("Eclipse table of a scene's files");
    } else if (json.contains(QStringLiteral("OTIO_SCHEMA"))) {
        d.kind = QStringLiteral("timeline");
        d.format = tr("OpenTimelineIO timeline (%1)").arg(json[QStringLiteral("OTIO_SCHEMA")].toString());
        d.details << tr("Name: %1").arg(json[QStringLiteral("name")].toString());
    } else {
        d.kind = QStringLiteral("json");
        d.format = tr("JSON");
        d.details << tr("Keys: %1").arg(QStringList(json.keys()).mid(0, 12).join(QStringLiteral(", ")));
    }
}

}  // namespace

Description describe(QString const& path)
{
    Description d;
    d.kind = QStringLiteral("unknown");
    if (QString const why = CommandLine::readProblem(path); !why.isEmpty()) {
        d.format = why;
        return d;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        d.format = file.errorString();
        return d;
    }
    QByteArray const head = file.peek(64);
    QFileInfo const info(path);
    d.details << tr("Size: %1").arg(bytesText(info.size()));
    d.details << tr("Modified: %1").arg(QLocale().toString(info.lastModified(), QLocale::ShortFormat));

    // Eclipse's own files, packed or not.
    if (head.startsWith("ECLZ") || head.trimmed().startsWith('{')) {
        QByteArray bytes = file.readAll();
        bool const packed = bytes.startsWith("ECLZ");
        if (packed)
            bytes = qUncompress(bytes.mid(4));
        QJsonDocument const doc = QJsonDocument::fromJson(bytes);
        if (doc.isObject()) {
            describeJson(path, doc.object(), packed, d);
            return d;
        }
        if (packed) {
            d.format = tr("Formatting error: a packed Eclipse file that is damaged");
            return d;
        }
    }
    if (head.startsWith("FWS") || head.startsWith("CWS") || head.startsWith("ZWS")) {
        d.kind = QStringLiteral("flash");
        d.format = tr("Flash movie (Animate format, .swf)");
        d.details << tr("Version: %1").arg(head.size() > 3 ? int(quint8(head[3])) : 0);
        d.details << (head[0] == 'F' ? tr("Not compressed") : head[0] == 'C' ? tr("Compressed (zlib)") : tr("Compressed (LZMA)"));
        if (head.size() >= 8) {
            quint32 const length = quint8(head[4]) | (quint32(quint8(head[5])) << 8) | (quint32(quint8(head[6])) << 16) | (quint32(quint8(head[7])) << 24);
            d.details << tr("Length when unpacked: %1").arg(bytesText(length));
        }
        return d;
    }
    if (head.startsWith("PK\x03\x04")) {
        file.seek(0);
        QHash<QString, QByteArray> const files = Zip::read(file.readAll());
        if (files.contains(QStringLiteral("DOMDocument.xml"))) {
            d.kind = QStringLiteral("flash");
            d.format = tr("Flash document (Animate format, .fla)");
        } else {
            d.kind = QStringLiteral("zip");
            d.format = tr("Zip archive");
        }
        d.details << tr("Files in it: %1").arg(files.size());
        return d;
    }
    if (head.startsWith("%PDF")) {
        d.kind = QStringLiteral("pdf");
        d.format = tr("PDF document, version %1").arg(QString::fromLatin1(head.mid(5, 3)));
        return d;
    }
    if (head.startsWith("RIFF") && head.mid(8, 4) == "WAVE") {
        d.kind = QStringLiteral("sound");
        d.format = tr("WAV sound");
        if (head.size() >= 36 && head.mid(12, 4) == "fmt ") {
            int const channels = quint8(head[22]) | (quint8(head[23]) << 8);
            quint32 const rate = quint8(head[24]) | (quint32(quint8(head[25])) << 8) | (quint32(quint8(head[26])) << 16) | (quint32(quint8(head[27])) << 24);
            int const bits = quint8(head[34]) | (quint8(head[35]) << 8);
            d.details << tr("%1 Hz, %2 bits, %3 channel(s)").arg(rate).arg(bits).arg(channels);
        }
        return d;
    }
    if (head.startsWith("RIFF") && head.mid(8, 4) == "AVI ") {
        d.kind = QStringLiteral("movie");
        d.format = tr("AVI movie");
        return d;
    }
    if (head.startsWith("FORM") && (head.mid(8, 4) == "AIFF" || head.mid(8, 4) == "AIFC")) {
        d.kind = QStringLiteral("sound");
        d.format = tr("AIFF sound");
        return d;
    }
    if (head.startsWith("ID3") || (head.size() > 1 && quint8(head[0]) == 0xff && (quint8(head[1]) & 0xe0) == 0xe0 && !head.startsWith("\xff\xd8"))) {
        d.kind = QStringLiteral("sound");
        d.format = tr("MP3 sound");
        return d;
    }
    if (head.startsWith("OggS")) {
        d.kind = QStringLiteral("sound");
        d.format = tr("Ogg sound");
        return d;
    }
    if (head.startsWith("fLaC")) {
        d.kind = QStringLiteral("sound");
        d.format = tr("FLAC sound");
        return d;
    }
    if (head.mid(4, 4) == "ftyp") {
        QByteArray const brand = head.mid(8, 4);
        d.kind = QStringLiteral("movie");
        d.format = brand == "qt  " ? tr("QuickTime movie") : brand.startsWith("M4A") ? tr("MPEG-4 sound") : tr("MPEG-4 movie (%1)").arg(QString::fromLatin1(brand).trimmed());
        if (brand.startsWith("M4A"))
            d.kind = QStringLiteral("sound");
        else if (brand.startsWith("hei") || brand.startsWith("mif1") || brand == "avif")
            d.kind = QStringLiteral("picture");
        if (d.kind != QStringLiteral("picture"))
            return d;
    }
    if (be32(head, 0) == 0x1a45dfa3u) {
        d.kind = QStringLiteral("movie");
        d.format = tr("Matroska / WebM movie");
        return d;
    }
    // Pictures, by what Qt's readers make of them.
    file.seek(0);
    {
        QImageReader reader(&file);
        reader.setDecideFormatFromContent(true);
        if (QByteArray const format = reader.format(); !format.isEmpty() && reader.canRead()) {
            d.kind = QStringLiteral("picture");
            d.format = tr("%1 picture").arg(QString::fromLatin1(format).toUpper());
            QSize const size = reader.size();
            if (size.isValid())
                d.details << tr("Size: %1 x %2 pixels").arg(size.width()).arg(size.height());
            if (reader.imageCount() > 1)
                d.details << tr("Pictures in it: %1").arg(reader.imageCount());
            return d;
        }
    }
    file.seek(0);
    QByteArray const text = file.peek(4096);
    if (text.trimmed().startsWith("<?xml") || text.trimmed().startsWith('<')) {
        d.kind = QStringLiteral("xml");
        if (text.contains("<xmeml")) {
            d.kind = QStringLiteral("timeline");
            d.format = tr("Final Cut Pro XML");
        } else if (text.contains("<relink")) {
            d.format = tr("Eclipse relink file");
            FileConvert::Relink rules;
            if (FileConvert::readRelink(path, rules))
                d.details << tr("Rules: %1").arg(rules.size());
        } else if (text.contains("<alignments")) {
            d.format = tr("Eclipse alignments");
        } else if (text.contains("<svg")) {
            d.kind = QStringLiteral("picture");
            d.format = tr("SVG picture");
        } else {
            d.format = tr("XML");
        }
        return d;
    }
    if (text.startsWith("TITLE:") || text.contains("\nFCM:")) {
        d.kind = QStringLiteral("timeline");
        d.format = tr("Edit decision list (CMX 3600)");
        return d;
    }
    // A program file of Windows.
    if (head.startsWith("MZ")) {
        d.kind = QStringLiteral("program");
        d.format = tr("Windows program or library");
        return d;
    }
    d.format = tr("Unknown format");
    return d;
}

QStringList describeLines(QString const& path)
{
    Description const d = describe(path);
    QStringList lines{QStringLiteral("%1: %2").arg(QDir::toNativeSeparators(path), d.format)};
    for (QString const& detail : d.details)
        lines << QStringLiteral("  ") + detail;
    return lines;
}

QString hypervisor()
{
#if defined(Q_OS_WIN) && (defined(_M_X64) || defined(_M_IX86))
    int regs[4] = {0, 0, 0, 0};
    __cpuid(regs, 1);
    if (!(unsigned(regs[2]) & (1u << 31)))
        return tr("None (Physical)");
    __cpuid(regs, 0x40000000);
    char vendor[13] = {};
    std::memcpy(vendor, &regs[1], 4);
    std::memcpy(vendor + 4, &regs[2], 4);
    std::memcpy(vendor + 8, &regs[3], 4);
    QString const id = QString::fromLatin1(vendor);
    struct Known {
        char const* id;
        char const* name;
    };
    static Known const known[] = {{"Microsoft Hv", "Microsoft Hyper-V"}, {"VMwareVMware", "VMware"}, {"KVMKVMKVM", "KVM"}, {"XenVMMXenVMM", "Xen"},
                                  {"VBoxVBoxVBox", "VirtualBox"}, {"prl hyperv", "Parallels"}, {" lrpepyh vr", "Parallels"}, {"TCGTCGTCGTCG", "QEMU"},
                                  {"ACRNACRNACRN", "ACRN"}, {"bhyve bhyve", "bhyve"}};
    for (Known const& k : known)
        if (id.startsWith(QLatin1String(k.id)))
            return QString::fromLatin1(k.name);
    return tr("Unknown Hypervisor") + (id.trimmed().isEmpty() ? QString() : QStringLiteral(" (%1)").arg(id.trimmed()));
#else
    return tr("Not determined - treat as Physical");
#endif
}

QString cloud()
{
#ifdef Q_OS_WIN
    QSettings const bios(QStringLiteral("HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\BIOS"), QSettings::NativeFormat);
    QString const maker = bios.value(QStringLiteral("SystemManufacturer")).toString(), product = bios.value(QStringLiteral("SystemProductName")).toString();
    if (maker.contains(QLatin1String("Amazon"), Qt::CaseInsensitive) || product.contains(QLatin1String("EC2"), Qt::CaseInsensitive))
        return QStringLiteral("Amazon EC2");
    if (maker.contains(QLatin1String("Google"), Qt::CaseInsensitive) || product.contains(QLatin1String("Google Compute"), Qt::CaseInsensitive))
        return QStringLiteral("Google Compute");
#endif
    return QString();
}

QStringList systemLines()
{
    QStringList lines;
    lines << tr("System: %1 (%2, kernel %3)").arg(QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture(), QSysInfo::kernelVersion());
    lines << tr("Computer: %1").arg(QSysInfo::machineHostName());
    lines << tr("Processor cores: %1").arg(QThread::idealThreadCount());
#ifdef Q_OS_WIN
    MEMORYSTATUSEX memory;
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory))
        lines << tr("Memory: %1 (%2 free)").arg(bytesText(qint64(memory.ullTotalPhys)), bytesText(qint64(memory.ullAvailPhys)));
#endif
    lines << tr("Running on Hypervisor: %1").arg(hypervisor());
    if (QString const c = cloud(); !c.isEmpty())
        lines << tr("Cloud: %1").arg(c);
    QStorageInfo const here(QCoreApplication::applicationDirPath());
    if (here.isValid())
        lines << tr("Disk of the programs: %1 free of %2%3").arg(bytesText(here.bytesAvailable()), bytesText(here.bytesTotal()), here.isReadOnly() ? tr(" (Read-only file system)") : QString());
    lines << tr("Qt: %1").arg(QString::fromLatin1(qVersion()));
    return lines;
}

QStringList versionLines()
{
    QStringList lines{CommandLine::versionLine()};
    lines << tr("Built: %1 %2").arg(QStringLiteral(__DATE__), QStringLiteral(__TIME__));
    lines << tr("Qt: %1").arg(QString::fromLatin1(qVersion()));
    lines << tr("Programs: %1").arg(QDir::toNativeSeparators(QCoreApplication::applicationDirPath()));
    lines << (QFileInfo::exists(CommandLine::versionFile()) ? tr("Version file: %1").arg(QDir::toNativeSeparators(CommandLine::versionFile()))
                                                           : tr("Version file: none (resources/version.txt is not there)"));
    return lines;
}

QStringList statusLines(QString const& databaseRoot)
{
    QStringList lines;
    QString root = !databaseRoot.isEmpty() ? databaseRoot
                                           : QSettings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse")).value(QStringLiteral("prefs/databaseRoot")).toString();
    if (root.isEmpty())
        root = ConfigFile::manager(QStringLiteral("database"), QStringLiteral("root"));
    if (root.isEmpty()) {
        lines << tr("Scene database: none set (Preferences > General)");
    } else {
        SceneDatabase database(root);
        QString error;
        if (!database.open(false, &error)) {
            lines << tr("Scene database: %1 cannot be opened (%2)").arg(QDir::toNativeSeparators(root), error);
        } else {
            int jobs = 0, scenes = 0, locked = 0;
            QStringList const environments = database.environments();
            for (QString const& e : environments)
                for (QString const& j : database.jobs(e)) {
                    ++jobs;
                    for (QString const& s : database.scenes(e, j)) {
                        ++scenes;
                        locked += database.locks(e, j, s).isEmpty() ? 0 : 1;
                    }
                }
            lines << tr("Scene database: %1 - %2 environment(s), %3 job(s), %4 scene(s), %5 being worked on")
                         .arg(QDir::toNativeSeparators(root)).arg(environments.size()).arg(jobs).arg(scenes).arg(locked);
        }
    }
    QVector<RenderQueue::Job> const jobs = RenderQueue::load();
    int done = 0, failed = 0;
    for (RenderQueue::Job const& j : jobs) {
        done += j.status == QObject::tr("Done");
        failed += j.status.startsWith(QObject::tr("Failed"));
    }
    lines << tr("Batch Rendering queue: %1 job(s) - %2 waiting, %3 done, %4 failed").arg(jobs.size()).arg(jobs.size() - done - failed).arg(done).arg(failed);
    for (RenderQueue::Job const& j : jobs)
        if (j.status != QObject::tr("Done"))
            lines << QStringLiteral("  %1 [%2] %3").arg(QDir::toNativeSeparators(j.scene)).arg(j.priority).arg(j.status.isEmpty() ? tr("waiting") : j.status);
    QJsonObject const status = ServiceLauncher::readStatus();
    QJsonArray const services = status[QStringLiteral("services")].toArray();
    if (services.isEmpty()) {
        lines << tr("Services: the Service Launcher is not running");
    } else {
        lines << tr("Services (as of %1):").arg(QLocale().toString(QDateTime::fromString(status[QStringLiteral("updated")].toString(), Qt::ISODate), QLocale::ShortFormat));
        for (QJsonValue const& v : services) {
            QJsonObject const s = v.toObject();
            lines << QStringLiteral("  %1: %2").arg(s[QStringLiteral("name")].toString(), s[QStringLiteral("state")].toString());
        }
    }
    QString const stop = CommandLine::dataFolder() + QStringLiteral("/batch-stop");
    if (QFileInfo::exists(stop))
        lines << tr("The batch program's stop file is there: a watching queue stops.");
    return lines;
}

QString usage()
{
    return tr("Usage: eclipse-info [options] [files...]\n"
              "\n"
              "  <files>               what each file is (from what is in it) and what it holds\n"
              "  -version              the version of the programs\n"
              "  -system               the computer: system, cores, memory, virtual machine\n"
              "  -status               the scene database, the Batch Rendering queue, the services\n"
              "  -dbroot <folder>      the database looked at (default: Preferences > General)\n"
              "  -paths <scene.ecl>    the files a scene uses from outside its folder\n"
              "  -options <file>, @file   more arguments from a file\n"
              "  -help\n"
              "\n"
              "Exit codes: 0 done, 1 a file could not be read, 2 wrong arguments.");
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
    if (words.isEmpty()) {
        say(usage(), false);
        return 0;
    }
    int result = 0;
    QString root;
    QStringList files;
    bool version = false, system = false, status = false;
    for (qsizetype i = 0; i < words.size(); ++i) {
        QString const w = words[i];
        if (w == QLatin1String("-help") || w == QLatin1String("--help") || w == QLatin1String("-h") || w == QLatin1String("/?")) {
            say(usage(), false);
            return 0;
        } else if (w == QLatin1String("-version") || w == QLatin1String("--version")) {
            version = true;
        } else if (w == QLatin1String("-system")) {
            system = true;
        } else if (w == QLatin1String("-status")) {
            status = true;
        } else if (w == QLatin1String("-dbroot")) {
            if (i + 1 >= words.size()) {
                say(tr("Wrong command line arguments.") + QLatin1Char(' ') + tr("-dbroot needs a folder."), true);
                return 2;
            }
            root = words[++i];
        } else if (w == QLatin1String("-paths")) {
            if (i + 1 >= words.size()) {
                say(tr("Wrong command line arguments.") + QLatin1Char(' ') + tr("-paths needs a scene."), true);
                return 2;
            }
            QString const scene = words[++i];
            QStringList missing;
            for (QString const& p : FileConvert::scenePaths(scene, &missing))
                say(missing.contains(p) ? tr("%1 (missing)").arg(QDir::toNativeSeparators(p)) : QDir::toNativeSeparators(p), false);
            if (!missing.isEmpty())
                result = 1;
        } else if (w.startsWith(QLatin1Char('-')) && w.size() > 1) {
            say(tr("Unknown command-line argument: %1").arg(w), true);
            return 2;
        } else {
            files << w;
        }
    }
    if (version)
        for (QString const& line : versionLines())
            say(line, false);
    if (system)
        for (QString const& line : systemLines())
            say(line, false);
    if (status)
        for (QString const& line : statusLines(root))
            say(line, false);
    for (QString const& file : files) {
        Description const d = describe(file);
        bool const bad = d.details.isEmpty();
        say(QStringLiteral("%1: %2").arg(QDir::toNativeSeparators(file), d.format), bad);
        for (QString const& detail : d.details)
            say(QStringLiteral("  ") + detail, false);
        if (bad)
            result = 1;
    }
    return result;
}

}  // namespace ToolInfo
