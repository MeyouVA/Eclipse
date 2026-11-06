// Eclipse - what the small programs beside Eclipse share: their arguments (with options files), what is wrong with a file
// or a folder said the way a console says it, and their output.
#pragma once

#include "EclipseExport.h"

#include <QString>
#include <QStringList>

namespace CommandLine {

// A line split into words: spaces between them, "double" or 'single' quotes round a word with spaces in it, and the
// rest of a line after # left out.
ECLIPSE_UI_API QStringList splitWords(QString const& text);
// The arguments with each "-options <file>" (or "@file") replaced by the words of that file, in place - an options file
// may name another, not itself again. The files used in `used`; false, with `error`, when one cannot be read.
ECLIPSE_UI_API bool expand(QStringList const& arguments, QStringList& out, QStringList* used, QString* error);

// What stops a file being read (empty: nothing): "No such file or directory", "Is a directory", "Permission denied",
// "File too large" (more than a program holds at once), "Filename too long".
ECLIPSE_UI_API QString readProblem(QString const& path);
// What stops a file being written there (empty: nothing): "Not a directory" (a file where its folder is to be),
// "Is a directory", "Read-only file system", "Permission denied", "No space left on device", "Filename too long".
ECLIPSE_UI_API QString writeProblem(QString const& path);
// The same for a folder that files are to be written in (made when it is not there yet).
ECLIPSE_UI_API QString folderProblem(QString const& folder);

// Written to the console (UTF-8), a line at a time; errors to the error output.
ECLIPSE_UI_API void print(QString const& text);
ECLIPSE_UI_API void printError(QString const& text);

// Where the programs beside Eclipse keep what they share (the services, their logs and status, the batch program's stop
// file): ECLIPSE_TOOLS_FOLDER when it is set, else Eclipse's folder of the user's application data.
ECLIPSE_UI_API QString dataFolder();

// The version of the programs ("Eclipse 0.2.0"), read from resources/version.txt beside them when it is there.
ECLIPSE_UI_API QString versionLine();
ECLIPSE_UI_API QString versionFile();

}  // namespace CommandLine
