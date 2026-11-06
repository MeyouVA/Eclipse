// Eclipse - the batch program (eclipse-batch): scenes opened without a window and rendered (to numbered pictures or a
// movie), a script run on them and the result saved; or the Batch Rendering queue worked through, once or again and
// again (as a service the Service Launcher keeps running).
//
//   eclipse-batch [options] scene.ecl [more scenes...]
//
// Each scene is done in turn: the script first (when there is one), then the save, then the render.
#pragma once

#include "EclipseExport.h"

#include <QSize>
#include <QString>
#include <QStringList>

#include <functional>

class Scene;

class ECLIPSE_UI_API BatchRun {
public:
    struct Options {
        QStringList scenes;
        // Scenes in the scene database: "environment/job/scene" (":version" after it for another than the current one).
        QStringList databaseScenes;
        QString databaseRoot;           // (empty: Preferences > General)
        QString comment;                // of the version a database scene is saved as

        QString scriptFile;             // -script: a .js file
        QString code;                   // -eval: the script itself
        QStringList scriptArguments;    // -arg: what the script sees as `arguments`
        int timeLimit = 0;              // -timeout: seconds a script may take (0: no end)

        bool save = false;              // -save: the scene saved again (a database scene: a new version)
        QString saveAs;                 // -saveas: saved there instead (one scene only)

        bool render = false;            // -render (also when -output or -movie is given)
        QString output;                 // -output: the folder the pictures go in (empty: frames/ beside the scene)
        QString name;                   // -name: the files are name_0001.png ... (empty: the scene's name)
        QString format = QStringLiteral("png");
        int digits = 4;
        int first = 1, last = 0;        // -frames a-b, -first, -last (frames from 1; last 0: to the end)
        QSize size;                     // -resolution WxH (invalid: the scene's, by `percent`)
        int percent = 100;
        bool preview = false;           // -preview: quick, edges not smoothed
        QString movie;                  // -movie: a movie file written instead of pictures (through ffmpeg)
        int threads = 0;                // -threads (0: Preferences > Render)

        bool queue = false;             // -queue: the Batch Rendering queue worked through
        int watch = 0;                  // -watch: and again every so many seconds until the stop file is there
        int loops = 0;                  // -loops: at most so many times (0: no end)
        QString stopFile;               // -stopfile: made to stop the watching (empty: "stop" beside the queue)

        bool verbose = false, quiet = false;
        bool help = false, version = false;
        QStringList optionsFiles;       // the options files read (Options file used: ...)
    };

    // The command line (without the program's name) read into options; false, with `error`, at an argument it does not
    // know ("Unknown command-line argument: -x"), a value missing or wrong, or nothing to do ("Wrong command line arguments.").
    static bool parse(QStringList const& arguments, Options& out, QString* error);
    static QString usage();

    // Said as it goes: a line, and whether it is an error.
    using Say = std::function<void(QString const& line, bool error)>;
    // Does what the options say; 0 when everything went, 1 when a scene or a job failed, 2 for wrong arguments.
    static int run(Options const& options, Say const& say);

    // One scene's work (the script, the save, the render) on a scene already read; `path` where it was read from.
    static bool runScene(Options const& options, Scene& scene, QString const& path, Say const& say, bool* changed = nullptr);
    // The pictures (or the movie) of a scene as the options say; how many frames were written, -1 on an error.
    static int renderScene(Options const& options, Scene const& scene, QString const& path, Say const& say);
    // The Batch Rendering queue worked through (again, with `watch`); how many jobs were done.
    static int runQueue(Options const& options, Say const& say, int* failedJobs = nullptr);
    // Where the queue's stop file is looked for.
    static QString defaultStopFile();
};
