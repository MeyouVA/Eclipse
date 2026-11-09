// Eclipse - the storyboard printed to a PDF: its panels laid out on pages as a profile says.
//
// A profile is a set of named settings (page, header and footer, cover, panel picture, panel header, captions, the
// words used), kept as JSON; `options()` lists them with their defaults, and the export window is made from that list.
// Profiles are saved in Eclipse's settings folder (StoryboardPdfProfiles/<name>.json); a few come with Eclipse.
#pragma once

#include "EclipseExport.h"
#include "model/scene/Storyboard.h"

#include <QImage>
#include <QJsonObject>
#include <QStringList>
#include <QVariant>
#include <QVector>
#include <functional>

class Scene;

namespace StoryboardPdf {

struct Option {
    QString key;
    QString page;           // the tab it is on
    QString label;
    QString tip;
    enum Type { Bool, Int, Double, Text, Font, Choice, File, Colour, Fields };
    Type type = Bool;
    QVariant initial;
    QStringList choices;    // a Choice: the names (its value is the index)
    double low = 0.0, high = 100000.0;
};
ECLIPSE_CANVAS_API QVector<Option> const& options();

// A profile: its settings (any left out have their defaults).
struct ECLIPSE_CANVAS_API Profile {
    QJsonObject values;
    QVariant value(QString const& key) const;
    bool flag(QString const& key) const { return value(key).toBool(); }
    int number(QString const& key) const { return value(key).toInt(); }
    double real(QString const& key) const { return value(key).toDouble(); }
    QString text(QString const& key) const { return value(key).toString(); }
    void set(QString const& key, QVariant const& v);
    QString name() const { return text(QStringLiteral("name")); }
};

ECLIPSE_CANVAS_API QVector<Profile> builtInProfiles();
ECLIPSE_CANVAS_API QString profilesFolder();
// The profiles that come with Eclipse, then those saved.
ECLIPSE_CANVAS_API QVector<Profile> profiles();
ECLIPSE_CANVAS_API bool saveProfile(Profile const& profile, QString* error = nullptr);
ECLIPSE_CANVAS_API bool removeProfile(QString const& name);

// The pages a profile lays the storyboard on: each a list of cells (shot, panel; panel -1: the shot's transition;
// shot -1: an empty placeholder).
struct Cell {
    int shot = -1, panel = -1;
};
ECLIPSE_CANVAS_API QVector<QVector<Cell>> layOut(Scene const& scene, Profile const& profile);

// Written to `path`; `sceneFile`: the name printed where the profile says.  false, with `error`.
using PictureSource = std::function<QImage(int frame, QSize size)>;
ECLIPSE_CANVAS_API bool writeWithFrames(Scene const& scene, Profile const& profile, QString const& path, QString const& sceneFile,
                                       Storyboard::Naming const& naming, PictureSource const& pictures, QString* error, int* pages = nullptr);

}
