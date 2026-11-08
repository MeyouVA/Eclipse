// Eclipse - the batch program (see BatchRun.h).
#include "animation/ui/tools/BatchRun.h"

#include "canvas/FrameRender.h"
#include "model/document/Commands.h"
#include "model/scene/Scene.h"
#include "model/scene/SceneDatabase.h"
#include "animation/ui/io/MovieExport.h"
#include "animation/ui/io/RenderQueue.h"
#include "animation/ui/timeline/Timeline.h"
#include "animation/ui/tools/CommandLine.h"
#include "actions/config/ConfigFile.h"
#include "animation/ui/views/ScriptView.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImageWriter>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>
#include <QUndoStack>

#include <algorithm>

namespace {

QString tr(char const* text, char const* note = nullptr, int n = -1) { return QCoreApplication::translate("BatchRun", text, note, n); }

bool number(QString const& text, int& out, int least, int most)
{
    bool ok = false;
    int const value = text.toInt(&ok);
    if (!ok || value < least || value > most)
        return false;
    out = value;
    return true;
}

}  // namespace

QString BatchRun::usage()
{
    return tr("Usage: eclipse-batch [options] scene.ecl [more scenes...]\n"
              "\n"
              "Opens each scene without a window and does what the options say: a script run on it, the scene saved, and\n"
              "its frames rendered (a scene with nothing else asked of it is rendered).\n"
              "\n"
              "Scripts:\n"
              "  -script <file.js>     run this script on each scene\n"
              "  -eval <code>          run this code on each scene\n"
              "  -arg <value>          what the script sees in `arguments` (again for more)\n"
              "  -timeout <seconds>    stop a script that runs longer (default: no end)\n"
              "  -save                 save each scene after the script (a database scene: a new version)\n"
              "  -saveas <file>        save the scene there instead (one scene only)\n"
              "\n"
              "Rendering:\n"
              "  -render               render the frames\n"
              "  -output <folder>      where the pictures go (default: frames beside the scene)\n"
              "  -name <name>          the pictures are name_0001.png ... (default: the scene's name)\n"
              "  -format <png|jpg|tif|bmp|...>   the pictures' format (default png)\n"
              "  -digits <n>           the frame number in so many digits (default 4)\n"
              "  -frames <a-b>         the frames rendered, from 1 (also -first <a>, -last <b>)\n"
              "  -resolution <WxH>     the pictures' size (default: the scene's)\n"
              "  -percent <n>          the pictures' size, as a percentage of the scene's\n"
              "  -preview              quick: the edges not smoothed\n"
              "  -movie <file>         a movie written instead of pictures (mov, mp4, avi... through ffmpeg)\n"
              "  -threads <n>          frames rendered at once (default: Preferences > Render)\n"
              "\n"
              "The scene database:\n"
              "  -database <environment/job/scene[:version]>   a scene of the database (again for more)\n"
              "  -dbroot <folder>      the database's folder (default: Preferences > General)\n"
              "  -comment <text>       the comment of the version saved\n"
              "\n"
              "The Batch Rendering queue:\n"
              "  -queue                render the jobs of the queue, the highest priority first\n"
              "  -watch <seconds>      and look for new jobs again every so many seconds\n"
              "  -loops <n>            at most so many times\n"
              "  -stopfile <file>      stop watching when this file is there\n"
              "\n"
              "Other:\n"
              "  -options <file>       more arguments read from a file (also @file)\n"
              "  -verbose, -quiet      say more, or only the errors\n"
              "  -version, -help\n"
              "\n"
              "Exit codes: 0 all done, 1 a scene or a job failed, 2 wrong arguments.");
}

bool BatchRun::parse(QStringList const& arguments, Options& out, QString* error)
{
    auto fail = [&](QString const& why) {
        if (error)
            *error = why;
        return false;
    };
    QStringList words;
    QString why;
    if (!CommandLine::expand(arguments, words, &out.optionsFiles, &why))
        return fail(why);
    // The defaults of manager.conf (the Configuration Editor edits it), under what the command line says.
    {
        ConfigFile const manager(ConfigFile::managerFile());
        if (QString const format = manager.value(QStringLiteral("batch"), QStringLiteral("format")).toLower();
            !format.isEmpty() && QImageWriter::supportedImageFormats().contains(format.toLatin1()))
            out.format = format;
        out.digits = std::clamp(manager.intValue(QStringLiteral("batch"), QStringLiteral("digits"), out.digits), 1, 9);
        out.threads = std::clamp(manager.intValue(QStringLiteral("batch"), QStringLiteral("threads"), out.threads), 0, 256);
    }
    bool renderAsked = false;
    for (qsizetype i = 0; i < words.size(); ++i) {
        QString const word = words[i];
        QString key = word;
        if (key.startsWith(QLatin1String("--")))
            key = key.mid(1);
        auto value = [&](QString& to) {
            if (i + 1 >= words.size()) {
                why = tr("Wrong command line arguments.") + QLatin1Char(' ') + tr("%1 needs a value.").arg(word);
                return false;
            }
            to = words[++i];
            return true;
        };
        auto integer = [&](int& to, int least, int most) {
            QString text;
            if (!value(text))
                return false;
            if (!number(text, to, least, most)) {
                why = tr("Wrong value specified for %1: %2").arg(word, text);
                return false;
            }
            return true;
        };
        QString text;
        if (key == QLatin1String("-help") || key == QLatin1String("-h") || key == QLatin1String("-?") || key == QLatin1String("/?")) {
            out.help = true;
        } else if (key == QLatin1String("-version")) {
            out.version = true;
        } else if (key == QLatin1String("-script")) {
            if (!value(out.scriptFile))
                return fail(why);
        } else if (key == QLatin1String("-eval")) {
            if (!value(out.code))
                return fail(why);
        } else if (key == QLatin1String("-arg")) {
            if (!value(text))
                return fail(why);
            out.scriptArguments << text;
        } else if (key == QLatin1String("-timeout")) {
            if (!integer(out.timeLimit, 0, 86400))
                return fail(why);
        } else if (key == QLatin1String("-save")) {
            out.save = true;
        } else if (key == QLatin1String("-saveas")) {
            if (!value(out.saveAs))
                return fail(why);
        } else if (key == QLatin1String("-render")) {
            renderAsked = true;
        } else if (key == QLatin1String("-output") || key == QLatin1String("-o")) {
            if (!value(out.output))
                return fail(why);
            renderAsked = true;
        } else if (key == QLatin1String("-name")) {
            if (!value(out.name))
                return fail(why);
        } else if (key == QLatin1String("-format")) {
            if (!value(out.format))
                return fail(why);
            out.format = out.format.toLower();
            if (out.format.startsWith(QLatin1Char('.')))
                out.format = out.format.mid(1);
            if (!QImageWriter::supportedImageFormats().contains(out.format.toLatin1()))
                return fail(tr("Unknown format: %1 (the pictures can be %2)").arg(out.format, QString::fromLatin1(QImageWriter::supportedImageFormats().join(", "))));
        } else if (key == QLatin1String("-digits")) {
            if (!integer(out.digits, 1, 9))
                return fail(why);
        } else if (key == QLatin1String("-frames")) {
            if (!value(text))
                return fail(why);
            QStringList const parts = text.split(QLatin1Char('-'));
            int a = 0, b = 0;
            if (parts.size() == 1 && number(parts[0], a, 1, 1000000)) {
                out.first = out.last = a;
            } else if (parts.size() == 2 && number(parts[0], a, 1, 1000000) && (parts[1].isEmpty() || (number(parts[1], b, 1, 1000000) && b >= a))) {
                out.first = a;
                out.last = parts[1].isEmpty() ? 0 : b;
            } else {
                return fail(tr("Wrong value specified for %1: %2").arg(word, text));
            }
        } else if (key == QLatin1String("-first")) {
            if (!integer(out.first, 1, 1000000))
                return fail(why);
        } else if (key == QLatin1String("-last")) {
            if (!integer(out.last, 0, 1000000))
                return fail(why);
        } else if (key == QLatin1String("-resolution")) {
            if (!value(text))
                return fail(why);
            QStringList const parts = text.toLower().split(QLatin1Char('x'));
            int w = 0, h = 0;
            if (parts.size() != 2 || !number(parts[0], w, 1, 16384) || !number(parts[1], h, 1, 16384))
                return fail(tr("Wrong value specified for %1: %2").arg(word, text));
            out.size = QSize(w, h);
        } else if (key == QLatin1String("-percent")) {
            if (!integer(out.percent, 1, 1000))
                return fail(why);
        } else if (key == QLatin1String("-preview")) {
            out.preview = true;
        } else if (key == QLatin1String("-movie")) {
            if (!value(out.movie))
                return fail(why);
            renderAsked = true;
        } else if (key == QLatin1String("-threads")) {
            if (!integer(out.threads, 0, 256))
                return fail(why);
        } else if (key == QLatin1String("-queue")) {
            out.queue = true;
        } else if (key == QLatin1String("-watch")) {
            if (!integer(out.watch, 1, 86400))
                return fail(why);
            out.queue = true;
        } else if (key == QLatin1String("-loops")) {
            if (!integer(out.loops, 1, 1000000))
                return fail(why);
        } else if (key == QLatin1String("-stopfile")) {
            if (!value(out.stopFile))
                return fail(why);
        } else if (key == QLatin1String("-database") || key == QLatin1String("-db")) {
            if (!value(text))
                return fail(why);
            if (text.split(QLatin1Char('/')).size() != 3)
                return fail(tr("Wrong value specified for %1: %2 (environment/job/scene)").arg(word, text));
            out.databaseScenes << text;
        } else if (key == QLatin1String("-dbroot")) {
            if (!value(out.databaseRoot))
                return fail(why);
        } else if (key == QLatin1String("-comment")) {
            if (!value(out.comment))
                return fail(why);
        } else if (key == QLatin1String("-verbose") || key == QLatin1String("-v")) {
            out.verbose = true;
        } else if (key == QLatin1String("-quiet") || key == QLatin1String("-q")) {
            out.quiet = true;
        } else if (word.startsWith(QLatin1Char('-')) && word.size() > 1) {
            return fail(tr("Unknown command-line argument: %1").arg(word));
        } else {
            out.scenes << word;
        }
    }
    if (out.help || out.version)
        return true;
    if (!out.scriptFile.isEmpty() && !out.code.isEmpty())
        return fail(tr("Wrong command line arguments.") + QLatin1Char(' ') + tr("-script and -eval cannot both be given."));
    if (!out.saveAs.isEmpty() && out.scenes.size() + out.databaseScenes.size() != 1)
        return fail(tr("Wrong command line arguments.") + QLatin1Char(' ') + tr("-saveas saves one scene."));
    if (out.scenes.isEmpty() && out.databaseScenes.isEmpty() && !out.queue)
        return fail(tr("Wrong command line arguments.") + QLatin1Char(' ') + tr("No scene was named (eclipse-batch -help shows how)."));
    bool const scripted = !out.scriptFile.isEmpty() || !out.code.isEmpty();
    out.render = renderAsked || (!scripted && !out.save && out.saveAs.isEmpty());
    return true;
}

QString BatchRun::defaultStopFile()
{
    return CommandLine::dataFolder() + QStringLiteral("/batch-stop");
}

int BatchRun::renderScene(Options const& options, Scene const& scene, QString const& path, Say const& say)
{
    int const count = std::max(1, scene.frameCount());
    int const first = std::clamp(options.first, 1, count);
    int const last = options.last <= 0 ? count : std::clamp(options.last, first, count);
    QSize const size = options.size.isValid() ? options.size : FrameRender::sizeAt(scene, options.percent / 100.0);
    QString const base = options.name.isEmpty() ? QFileInfo(path).completeBaseName() : options.name;
    if (options.threads > 0)
        FrameRender::setThreads(options.threads);

    if (!options.movie.isEmpty()) {
        if (QString const why = CommandLine::writeProblem(options.movie); !why.isEmpty()) {
            say(QStringLiteral("%1: %2").arg(QDir::toNativeSeparators(options.movie), why), true);
            return -1;
        }
        MovieExport::Options movie;
        movie.first = first - 1;
        movie.last = last - 1;
        movie.size = options.size;
        movie.scale = options.percent / 100.0;
        QString error;
        int done = 0;
        bool const ok = MovieExport::write(scene, options.movie, [&](int) {
            ++done;
            if (options.verbose)
                say(tr("Frame %1 of %2").arg(done).arg(last - first + 1), false);
            return true;
        }, &error, movie);
        if (!ok) {
            say(tr("Could not write the movie %1: %2").arg(QDir::toNativeSeparators(options.movie), error), true);
            return -1;
        }
        if (!options.quiet)
            say(tr("Wrote %1 (%2 frames)").arg(QDir::toNativeSeparators(options.movie)).arg(last - first + 1), false);
        return last - first + 1;
    }

    QString const folder = !options.output.isEmpty() ? options.output : QFileInfo(path).absoluteDir().filePath(QStringLiteral("frames"));
    if (QString const why = CommandLine::folderProblem(folder); !why.isEmpty()) {
        say(QStringLiteral("%1: %2").arg(QDir::toNativeSeparators(folder), why), true);
        return -1;
    }
    if (!QDir().mkpath(folder)) {
        say(tr("Could not make the folder %1").arg(QDir::toNativeSeparators(folder)), true);
        return -1;
    }
    QVector<int> frames;
    for (int f = first; f <= last; ++f)
        frames << f - 1;
    QString error;
    int written = 0;
    QString const suffix = options.format;
    bool const ok = FrameRender::render(scene, frames, size, options.preview, [&](int frame, QImage const& image) -> QString {
        QString const file = QDir(folder).filePath(QStringLiteral("%1_%2.%3").arg(base).arg(frame + 1, options.digits, 10, QLatin1Char('0')).arg(suffix));
        QImage picture = image;
        if (suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg") || suffix == QLatin1String("bmp"))
            picture = picture.convertToFormat(QImage::Format_RGB32);
        if (!picture.save(file))
            return tr("Could not write %1").arg(QDir::toNativeSeparators(file));
        return QString();
    }, [&](int done) {
        written = done;
        if (options.verbose)
            say(tr("Frame %1 of %2").arg(done).arg(frames.size()), false);
        return true;
    }, &error);
    if (!ok) {
        say(error.isEmpty() ? tr("The render stopped.") : error, true);
        return -1;
    }
    if (!options.quiet)
        say(tr("Rendered %1 frames (%2-%3) of %4 to %5").arg(frames.size()).arg(first).arg(last).arg(QDir::toNativeSeparators(path), QDir::toNativeSeparators(folder)), false);
    return int(frames.size());
}

bool BatchRun::runScene(Options const& options, Scene& scene, QString const& path, Say const& say, bool* changed)
{
    if (changed)
        *changed = false;
    QString code = options.code, name = QStringLiteral("eval"), folder;
    if (!options.scriptFile.isEmpty()) {
        if (QString const why = CommandLine::readProblem(options.scriptFile); !why.isEmpty()) {
            say(QStringLiteral("%1: %2").arg(QDir::toNativeSeparators(options.scriptFile), why), true);
            return false;
        }
        QFile file(options.scriptFile);
        if (!file.open(QIODevice::ReadOnly)) {
            say(QStringLiteral("%1: %2").arg(QDir::toNativeSeparators(options.scriptFile), file.errorString()), true);
            return false;
        }
        code = QString::fromUtf8(file.readAll());
        name = QFileInfo(options.scriptFile).completeBaseName();
        folder = QFileInfo(options.scriptFile).absolutePath();
    }
    if (!code.isEmpty()) {
        Document document;
        document.reset(scene);
        Timeline timeline(&document);
        ScriptView view(&document, &timeline);
        if (!folder.isEmpty())
            view.setFolder(folder);
        view.setArguments(options.scriptArguments);
        view.setTimeLimit(options.timeLimit);
        if (options.verbose)
            say(tr("Running %1 on %2").arg(name, QDir::toNativeSeparators(path)), false);
        QString const said = view.run(code, name);
        QStringList const lines = said.isEmpty() ? QStringList() : said.split(QLatin1Char('\n'));
        for (qsizetype i = 0; i < lines.size(); ++i)
            say(lines[i], view.lastRunFailed() && i == lines.size() - 1);
        if (view.lastRunFailed())
            return false;
        if (changed)
            *changed = document.undoStack()->count() > 0;
        scene = document.wholeScene();
    }
    return true;
}

int BatchRun::runQueue(Options const& options, Say const& say, int* failedJobs)
{
    if (failedJobs)
        *failedJobs = 0;
    QString const stop = options.stopFile.isEmpty() ? defaultStopFile() : options.stopFile;
    QFile::remove(stop);
    int done = 0;
    for (int loop = 1;; ++loop) {
        QVector<RenderQueue::Job> jobs = RenderQueue::load();
        int const waiting = int(std::count_if(jobs.begin(), jobs.end(), [](RenderQueue::Job const& j) { return j.status != QObject::tr("Done"); }));
        if (waiting > 0 || options.verbose)
            say(tr("%n job(s) waiting in the queue", nullptr, waiting), false);
        if (waiting > 0) {
            int last = -1;
            done += RenderQueue::run(jobs, [&](int job, int frame, int frames) {
                if (job != last && !options.quiet)
                    say(tr("Rendering %1 (%2 frames)").arg(QDir::toNativeSeparators(jobs[job].scene)).arg(frames), false);
                last = job;
                if (options.verbose)
                    say(tr("Frame %1 of %2").arg(frame + 1).arg(frames), false);
                return !QFileInfo::exists(stop);
            });
            RenderQueue::save(jobs);
            for (RenderQueue::Job const& j : jobs)
                if (j.status.startsWith(QObject::tr("Failed")) && failedJobs)
                    ++*failedJobs;
            for (RenderQueue::Job const& j : jobs)
                if (j.status.startsWith(QObject::tr("Failed")))
                    say(QStringLiteral("%1: %2").arg(QDir::toNativeSeparators(j.scene), j.status), true);
        }
        if (options.watch <= 0 || (options.loops > 0 && loop >= options.loops))
            break;
        // Waiting for more, a little at a time, so that the stop file is seen soon.
        QElapsedTimer waited;
        waited.start();
        while (waited.elapsed() < qint64(options.watch) * 1000 && !QFileInfo::exists(stop))
            QThread::msleep(100);
        if (QFileInfo::exists(stop)) {
            QFile::remove(stop);
            say(tr("Stopped: the stop file was there."), false);
            break;
        }
    }
    return done;
}

int BatchRun::run(Options const& options, Say const& say)
{
    for (QString const& file : options.optionsFiles)
        say(tr("Options file used: %1").arg(QDir::toNativeSeparators(file)), false);
    if (options.help) {
        say(usage(), false);
        return 0;
    }
    if (options.version) {
        say(CommandLine::versionLine(), false);
        return 0;
    }
    bool failed = false;
    auto finish = [&](Scene& scene, QString const& path, std::function<bool(Scene const&)> const& saveBack) {
        bool changed = false;
        if (!runScene(options, scene, path, say, &changed))
            return false;
        if (!options.saveAs.isEmpty() || options.save) {
            QString const to = options.saveAs.isEmpty() ? path : options.saveAs;
            if (!options.saveAs.isEmpty() || !saveBack) {
                if (QString const why = CommandLine::writeProblem(to); !why.isEmpty()) {
                    say(QStringLiteral("%1: %2").arg(QDir::toNativeSeparators(to), why), true);
                    return false;
                }
                QString error;
                if (!scene.saveProject(to, &error)) {
                    say(tr("Could not save %1: %2").arg(QDir::toNativeSeparators(to), error), true);
                    return false;
                }
                if (!options.quiet)
                    say(tr("Saved %1").arg(QDir::toNativeSeparators(to)), false);
            } else if (!saveBack(scene)) {
                return false;
            }
        } else if (changed && !options.quiet && !options.render) {
            say(tr("The script changed %1, which was not saved (-save saves it).").arg(QDir::toNativeSeparators(path)), false);
        }
        if (options.render && renderScene(options, scene, options.saveAs.isEmpty() ? path : options.saveAs, say) < 0)
            return false;
        return true;
    };

    for (QString const& path : options.scenes) {
        if (QString const why = CommandLine::readProblem(path); !why.isEmpty()) {
            say(QStringLiteral("%1: %2").arg(QDir::toNativeSeparators(path), why), true);
            failed = true;
            continue;
        }
        Scene scene;
        QString error;
        if (!Scene::load(path, scene, &error)) {
            say(tr("Could not open %1: %2").arg(QDir::toNativeSeparators(path), error), true);
            failed = true;
            continue;
        }
        for (QString const& missing : Scene::lastMissingFiles())
            say(tr("Missing from the scene's folder: %1").arg(missing), true);
        if (options.verbose)
            say(tr("Opened %1 (%2 frames, %3x%4)").arg(QDir::toNativeSeparators(path)).arg(scene.frameCount()).arg(scene.width).arg(scene.height), false);
        if (!finish(scene, path, {}))
            failed = true;
    }

    if (!options.databaseScenes.isEmpty()) {
        // The command line's folder, else Preferences > General, else manager.conf's.
        QString root = options.databaseRoot;
        if (root.isEmpty())
            root = QSettings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse")).value(QStringLiteral("prefs/databaseRoot")).toString();
        if (root.isEmpty())
            root = ConfigFile::manager(QStringLiteral("database"), QStringLiteral("root"));
        SceneDatabase database(root);
        QString error;
        if (root.isEmpty() || !database.open(false, &error)) {
            say(tr("The scene database could not be opened: %1").arg(root.isEmpty() ? tr("no folder is set") : error), true);
            return 1;
        }
        for (QString const& spec : options.databaseScenes) {
            QStringList parts = spec.split(QLatin1Char('/'));
            int version = 0;
            if (qsizetype const colon = parts[2].lastIndexOf(QLatin1Char(':')); colon > 0) {
                version = parts[2].mid(colon + 1).toInt();
                parts[2] = parts[2].left(colon);
            }
            QString const file = database.versionFile(parts[0], parts[1], parts[2], version);
            Scene scene;
            if (file.isEmpty() || !Scene::load(file, scene, &error)) {
                say(tr("Could not open %1 from the database: %2").arg(spec, file.isEmpty() ? tr("there is no such scene or version") : error), true);
                failed = true;
                continue;
            }
            if (!finish(scene, file, [&](Scene const& done) {
                    QString why;
                    QString const comment = options.comment.isEmpty() ? tr("Saved by eclipse-batch") : options.comment;
                    int const made = database.saveVersion(parts[0], parts[1], parts[2], done, comment, &why);
                    if (made <= 0) {
                        say(tr("Could not save %1 to the database: %2").arg(spec, why), true);
                        return false;
                    }
                    if (!options.quiet)
                        say(tr("Saved %1 as version %2").arg(spec).arg(made), false);
                    return true;
                }))
                failed = true;
        }
    }

    if (options.queue) {
        int failedJobs = 0;
        runQueue(options, say, &failedJobs);
        failed = failed || failedJobs > 0;
    }
    return failed ? 1 : 0;
}
