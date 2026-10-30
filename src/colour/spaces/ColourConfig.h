// Eclipse - colour spaces, displays and views: the built-in set, or one read from an OpenColorIO configuration.
#pragma once

#include "colour/maths/ColourPrimaries.h"

#include <QMap>
#include <QStringList>

#include <memory>

namespace Cms {

struct Space {
    QString name;
    QStringList aliases;
    QString family;
    QString description;
    QString encoding;           // scene-linear, sdr-video, hdr-video, log, data ...
    QString bitDepth;
    QStringList categories;
    bool isData = false;        // numbers, not colours: never converted
    bool displayReferred = false;
    bool hasTo = false, hasFrom = false;
    Chain toReference, fromReference;
    Primaries primaries;        // when known: for ICC profiles and EXR chromaticities
    Curve curve = Curve::Linear;
    bool curveKnown = false;
    QString file;               // an ICC profile it was read from
    QString problem;            // why it cannot be used (a transform Eclipse cannot do, a missing LUT)
};

struct View {
    QString name;
    QString colourSpace;
    QString viewTransform;
    QString displayColourSpace; // with a view transform: the display's colour space ("<USE_DISPLAY_NAME>": the display's name)
    QString looks;
    QString description;
};

struct Display {
    QString name;
    QVector<View> views;
    QStringList sharedViews;
};

struct ViewTransform {
    QString name;
    QString description;
    QString family;
    bool hasFrom = false, hasTo = false;
    Chain fromSceneReference, toSceneReference;
    QString problem;
};

struct Look {
    QString name;
    QString processSpace;
    QString description;
    bool hasTransform = false, hasInverse = false;
    Chain transform, inverse;
    QString problem;
};

struct NamedTransform {
    QString name;
    QStringList aliases;
    QString description;
    QString family;
    bool hasForward = false, hasInverse = false;
    Chain forward, inverse;
    QString problem;
};

struct FileRule {
    QString name;
    QString colourSpace;
    QString pattern;
    QString extension;
    QString regex;
};

struct Config {
    QString file;               // empty: the built-in configuration
    QString title;
    QString description;
    double version = 2.0;
    QStringList searchPaths;
    std::array<double, 3> luma{0.2126, 0.7152, 0.0722};
    QMap<QString, QString> roles;
    QMap<QString, QString> environment;
    QVector<Space> spaces;
    QVector<Display> displays;
    QVector<View> sharedViews;
    QStringList activeDisplays, activeViews, inactiveSpaces;
    QVector<ViewTransform> viewTransforms;
    QString defaultViewTransform;
    QVector<Look> looks;
    QVector<NamedTransform> namedTransforms;
    QVector<FileRule> fileRules;
    QStringList warnings;

    // A colour space by its name, an alias or a role (case does not matter); nullptr when there is none.
    Space const* space(QString const& name) const;
    // The spaces to choose from, in order (without the inactive ones unless asked).
    QStringList spaceNames(bool inactiveToo = false) const;
    QStringList displayNames() const;
    QStringList viewNames(QString const& display) const;
    View const* view(QString const& display, QString const& view) const;
    Look const* look(QString const& name) const;
    ViewTransform const* viewTransform(QString const& name) const;
    // From one colour space to another; false (with the reason) when either cannot be found or used.
    bool convert(QString const& from, QString const& to, Chain* out, QString* error) const;
    // From a colour space to what a view of a display shows.
    bool toDisplay(QString const& from, QString const& display, QString const& view, Chain* out, QString* error) const;
    // Looks ("a, b" or "+a, -b") applied in their own spaces between two colour spaces.
    bool withLooks(QString const& from, QString const& to, QString const& looks, Chain* out, QString* error) const;
    // The colour space the file rules give a file (the Default rule's, or the "default" role's).
    QString spaceForFile(QString const& path) const;
    // A file named in the configuration, found along its search path (with its environment put in).
    QString resolvePath(QString const& file) const;
    QString folder() const;
};

// The configuration in use (the built-in one until another is set); safe from any thread.
std::shared_ptr<Config const> config();
void setConfig(std::shared_ptr<Config const> config);
// Grows by one each time the configuration in use is changed.
quint64 configGeneration();
Config builtinConfig();
// Reads an OpenColorIO configuration file (.ocio); false with the reason when it cannot be read.
bool readConfig(QString const& path, Config* out, QString* error);
// The chain of an OpenColorIO built-in transform by its style; false when Eclipse does not know it.
bool builtinTransform(QString const& style, Chain* out, QString* error);
// Uses the configuration file (empty: the built-in one) with the ICC profiles added as colour spaces; the problems met, if any, in `problems`.
bool useConfig(QString const& path, QStringList const& iccFiles, QStringList* problems);

}  // namespace Cms
