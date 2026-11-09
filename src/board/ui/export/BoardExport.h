#pragma once

#include <QString>
#include <functional>

struct BoardProject;
namespace StoryboardPdf { struct Profile; }

namespace BoardExport {
using Progress = std::function<bool(int frame)>;
bool pdf(BoardProject const& project, QString const& projectFile, QString const& path,
         StoryboardPdf::Profile const& profile, QString* error);
bool movie(BoardProject const& project, QString const& projectFile, QString const& path, bool transparent,
           Progress const& progress, QString* error);
bool images(BoardProject const& project, QString const& folder, bool transparent, Progress const& progress, QString* error);
bool animation(BoardProject const& project, QString const& projectFile, QString const& folder, QString* error);
}
