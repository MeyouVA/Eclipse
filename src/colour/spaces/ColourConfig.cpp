// Eclipse - finding colour spaces and views, and the conversions between them.
#include "colour/spaces/ColourConfig.h"

#include "colour/icc/Icc.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMutex>
#include <QProcessEnvironment>
#include <QRegularExpression>

#include <atomic>

namespace {

using namespace Cms;

QString said(char const* text)
{
    return QCoreApplication::translate("Colour", text);
}

QMutex gLock;
std::shared_ptr<Config const> gConfig;
std::atomic<quint64> gGeneration{1};

bool same(QString const& a, QString const& b)
{
    return a.compare(b, Qt::CaseInsensitive) == 0;
}

bool toReference(Space const& s, Chain* chain, QString* error)
{
    if (s.hasTo) {
        *chain += s.toReference;
        return true;
    }
    if (s.hasFrom) {
        Chain back;
        if (!invert(s.fromReference, &back, error))
            return false;
        *chain += back;
    }
    return true;
}

bool fromReference(Space const& s, Chain* chain, QString* error)
{
    if (s.hasFrom) {
        *chain += s.fromReference;
        return true;
    }
    if (s.hasTo) {
        Chain back;
        if (!invert(s.toReference, &back, error))
            return false;
        *chain += back;
    }
    return true;
}

// The scene reference to the display reference through a view transform (or back).
bool across(ViewTransform const& vt, bool toDisplay, Chain* chain, QString* error)
{
    if (!vt.problem.isEmpty()) {
        if (error)
            *error = vt.problem;
        return false;
    }
    Chain const& there = toDisplay ? vt.fromSceneReference : vt.toSceneReference;
    bool const has = toDisplay ? vt.hasFrom : vt.hasTo;
    if (has) {
        *chain += there;
        return true;
    }
    Chain const& other = toDisplay ? vt.toSceneReference : vt.fromSceneReference;
    Chain back;
    if (!invert(other, &back, error))
        return false;
    *chain += back;
    return true;
}

}  // namespace

namespace Cms {

Space const* Config::space(QString const& name) const
{
    if (name.isEmpty())
        return nullptr;
    for (Space const& s : spaces)
        if (same(s.name, name))
            return &s;
    for (Space const& s : spaces)
        for (QString const& alias : s.aliases)
            if (same(alias, name))
                return &s;
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        if (same(it.key(), name) && !same(it.value(), name))
            for (Space const& s : spaces)
                if (same(s.name, it.value()))
                    return &s;
    return nullptr;
}

QStringList Config::spaceNames(bool inactiveToo) const
{
    QStringList names;
    for (Space const& s : spaces) {
        bool inactive = false;
        for (QString const& off : inactiveSpaces)
            inactive = inactive || same(off, s.name);
        if (inactiveToo || !inactive)
            names << s.name;
    }
    return names;
}

QStringList Config::displayNames() const
{
    QStringList names;
    if (!activeDisplays.isEmpty()) {
        for (QString const& wanted : activeDisplays)
            for (Display const& d : displays)
                if (same(d.name, wanted))
                    names << d.name;
        if (!names.isEmpty())
            return names;
    }
    for (Display const& d : displays)
        names << d.name;
    return names;
}

QStringList Config::viewNames(QString const& display) const
{
    QStringList names;
    for (Display const& d : displays) {
        if (!same(d.name, display))
            continue;
        for (View const& v : d.views)
            names << v.name;
        names << d.sharedViews;
    }
    if (activeViews.isEmpty())
        return names;
    QStringList kept;
    for (QString const& n : names)
        for (QString const& a : activeViews)
            if (same(n, a))
                kept << n;
    return kept.isEmpty() ? names : kept;
}

View const* Config::view(QString const& display, QString const& name) const
{
    for (Display const& d : displays) {
        if (!same(d.name, display))
            continue;
        for (View const& v : d.views)
            if (same(v.name, name))
                return &v;
        for (QString const& shared : d.sharedViews)
            if (same(shared, name))
                for (View const& v : sharedViews)
                    if (same(v.name, name))
                        return &v;
    }
    return nullptr;
}

Look const* Config::look(QString const& name) const
{
    for (Look const& l : looks)
        if (same(l.name, name))
            return &l;
    return nullptr;
}

ViewTransform const* Config::viewTransform(QString const& name) const
{
    for (ViewTransform const& vt : viewTransforms)
        if (same(vt.name, name))
            return &vt;
    return nullptr;
}

bool Config::convert(QString const& from, QString const& to, Chain* out, QString* error) const
{
    out->clear();
    Space const* const a = space(from);
    Space const* const b = space(to);
    if (!a || !b) {
        if (error)
            *error = said("Cannot find the colour space named '%1'.").arg(!a ? from : to);
        return false;
    }
    if (a == b || a->isData || b->isData)
        return true;
    for (Space const* s : {a, b})
        if (!s->problem.isEmpty()) {
            if (error)
                *error = s->problem;
            return false;
        }
    Chain chain;
    if (!toReference(*a, &chain, error))
        return false;
    if (a->displayReferred != b->displayReferred) {
        ViewTransform const* vt = viewTransform(defaultViewTransform);
        if (!vt && !viewTransforms.isEmpty())
            vt = &viewTransforms.first();
        if (!vt) {
            if (error)
                *error = said("There is no view transform between the scene reference and the display reference.");
            return false;
        }
        if (!across(*vt, !a->displayReferred, &chain, error))
            return false;
    }
    if (!fromReference(*b, &chain, error))
        return false;
    *out = simplified(chain);
    return true;
}

bool Config::withLooks(QString const& from, QString const& to, QString const& lookList, Chain* out, QString* error) const
{
    QStringList const names = lookList.split(QRegularExpression(QStringLiteral("[,:]")), Qt::SkipEmptyParts);
    if (names.isEmpty())
        return convert(from, to, out, error);
    Chain chain;
    QString at = from;
    for (QString name : names) {
        name = name.trimmed();
        bool backwards = false;
        if (name.startsWith(QLatin1Char('+')) || name.startsWith(QLatin1Char('-'))) {
            backwards = name.startsWith(QLatin1Char('-'));
            name = name.mid(1).trimmed();
        }
        Look const* const l = look(name);
        if (!l) {
            if (error)
                *error = said("Cannot find the look named '%1'.").arg(name);
            return false;
        }
        if (!l->problem.isEmpty()) {
            if (error)
                *error = l->problem;
            return false;
        }
        Chain step;
        if (!convert(at, l->processSpace, &step, error))
            return false;
        chain += step;
        bool const has = backwards ? l->hasInverse : l->hasTransform;
        if (has) {
            chain += backwards ? l->inverse : l->transform;
        } else {
            Chain back;
            if (!invert(backwards ? l->transform : l->inverse, &back, error))
                return false;
            chain += back;
        }
        at = l->processSpace;
    }
    Chain last;
    if (!convert(at, to, &last, error))
        return false;
    chain += last;
    *out = simplified(chain);
    return true;
}

bool Config::toDisplay(QString const& from, QString const& display, QString const& viewName, Chain* out, QString* error) const
{
    out->clear();
    View const* const v = view(display, viewName);
    if (!v) {
        if (error)
            *error = said("The display '%1' has no view named '%2'.").arg(display, viewName);
        return false;
    }
    if (v->viewTransform.isEmpty())
        return withLooks(from, v->colourSpace, v->looks, out, error);
    Space const* const src = space(from);
    if (!src) {
        if (error)
            *error = said("Cannot find the colour space named '%1'.").arg(from);
        return false;
    }
    if (src->isData)
        return true;
    QString const displaySpace = v->displayColourSpace == QLatin1String("<USE_DISPLAY_NAME>") ? display : v->displayColourSpace;
    Space const* const dst = space(displaySpace);
    ViewTransform const* const vt = viewTransform(v->viewTransform);
    if (!dst || !vt) {
        if (error)
            *error = said("Cannot find the colour space or view transform of the view '%1'.").arg(viewName);
        return false;
    }
    Chain chain;
    if (!v->looks.isEmpty() && !withLooks(from, from, v->looks, &chain, error))
        return false;
    if (!src->problem.isEmpty() || !dst->problem.isEmpty()) {
        if (error)
            *error = !src->problem.isEmpty() ? src->problem : dst->problem;
        return false;
    }
    if (src->displayReferred) {
        if (error)
            *error = said("A view transform needs a scene colour space to start from.");
        return false;
    }
    if (!toReference(*src, &chain, error) || !across(*vt, true, &chain, error) || !fromReference(*dst, &chain, error))
        return false;
    *out = simplified(chain);
    return true;
}

QString Config::spaceForFile(QString const& path) const
{
    QString const fileName = QFileInfo(path).fileName();
    QString const suffix = QFileInfo(path).suffix();
    auto named = [this](QString const& name) {
        Space const* const s = space(name);
        return s ? s->name : name;
    };
    for (FileRule const& rule : fileRules) {
        if (same(rule.name, QStringLiteral("Default")))
            return named(rule.colourSpace);
        if (same(rule.name, QStringLiteral("ColorSpaceNamePathSearch"))) {
            // The longest name of a colour space found in the path.
            QString best;
            for (Space const& s : spaces)
                if (path.contains(s.name, Qt::CaseInsensitive) && s.name.size() > best.size())
                    best = s.name;
            if (!best.isEmpty())
                return best;
            continue;
        }
        bool hit = false;
        if (!rule.regex.isEmpty()) {
            hit = QRegularExpression(rule.regex).match(path).hasMatch();
        } else {
            QString const pattern = rule.pattern.isEmpty() ? QStringLiteral("*") : rule.pattern;
            QString const ext = rule.extension.isEmpty() ? QStringLiteral("*") : rule.extension;
            QRegularExpression const p(QRegularExpression::wildcardToRegularExpression(pattern, QRegularExpression::UnanchoredWildcardConversion),
                                       QRegularExpression::CaseInsensitiveOption);
            QRegularExpression const e(QRegularExpression::wildcardToRegularExpression(ext), QRegularExpression::CaseInsensitiveOption);
            hit = p.match(fileName).hasMatch() && e.match(suffix).hasMatch();
        }
        if (hit)
            return named(rule.colourSpace);
    }
    Space const* const fallback = space(QStringLiteral("default"));
    return fallback ? fallback->name : QString();
}

QString Config::folder() const
{
    return file.isEmpty() ? QString() : QFileInfo(file).absolutePath();
}

QString Config::resolvePath(QString const& name) const
{
    QString path = name;
    // $NAME and ${NAME}: the configuration's environment first, then the computer's.
    QProcessEnvironment const env = QProcessEnvironment::systemEnvironment();
    QRegularExpression const var(QStringLiteral("\\$\\{?([A-Za-z_][A-Za-z0-9_]*)\\}?"));
    for (int guard = 0; guard < 16; ++guard) {
        QRegularExpressionMatch const m = var.match(path);
        if (!m.hasMatch())
            break;
        QString const value = environment.contains(m.captured(1)) ? environment.value(m.captured(1)) : env.value(m.captured(1));
        path.replace(m.capturedStart(), m.capturedLength(), value);
    }
    if (QFileInfo(path).isAbsolute())
        return path;
    QDir const home(folder());
    for (QString const& searched : searchPaths) {
        QString const dir = QFileInfo(searched).isAbsolute() ? searched : home.filePath(searched);
        QString const there = QDir(dir).filePath(path);
        if (QFileInfo::exists(there))
            return there;
    }
    return home.filePath(path);
}

std::shared_ptr<Config const> config()
{
    QMutexLocker const locker(&gLock);
    if (!gConfig)
        gConfig = std::make_shared<Config const>(builtinConfig());
    return gConfig;
}

void setConfig(std::shared_ptr<Config const> c)
{
    QMutexLocker const locker(&gLock);
    gConfig = std::move(c);
    ++gGeneration;
}

quint64 configGeneration()
{
    return gGeneration;
}

bool useConfig(QString const& path, QStringList const& iccFiles, QStringList* problems)
{
    Config c;
    bool ok = true;
    if (path.isEmpty()) {
        c = builtinConfig();
    } else {
        QString why;
        if (!readConfig(path, &c, &why)) {
            if (problems)
                *problems << said("Error reading the colour configuration file %1: %2  The built-in colour spaces are used instead.")
                                 .arg(QDir::toNativeSeparators(path), why);
            c = builtinConfig();
            ok = false;
        }
    }
    for (QString const& icc : iccFiles) {
        Space s;
        QString why;
        if (!Icc::spaceOfFile(icc, c, &s, &why)) {
            if (problems)
                *problems << said("The ICC profile %1 cannot be used: %2").arg(QDir::toNativeSeparators(icc), why);
            ok = false;
            continue;
        }
        if (c.space(s.name))
            s.name = said("%1 (ICC)").arg(s.name);
        c.spaces.push_back(s);
    }
    setConfig(std::make_shared<Config const>(std::move(c)));
    return ok;
}

}  // namespace Cms
