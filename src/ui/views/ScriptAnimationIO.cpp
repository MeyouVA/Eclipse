// Eclipse - what a script calls AnimationIO: keyframes from and to ATOM files (see ScriptGlobals.h).
#include "ui/views/ScriptGlobals.h"

#include "formats/anim/AtomAnimation.h"
#include "model/document/Commands.h"
#include "ui/timeline/Timeline.h"
#include "ui/views/ScriptView.h"

bool ScriptAnimationIO::importAnimation(QJSValue const& path, QJSValue const& options, QJSValue const& extra)
{
    return run(false, path, options, extra);
}

bool ScriptAnimationIO::exportAnimation(QJSValue const& path, QJSValue const& options, QJSValue const& extra)
{
    return run(true, path, options, extra);
}

bool ScriptAnimationIO::run(bool exporting, QJSValue const& path, QJSValue const& options, QJSValue const& extra)
{
    auto say = [this](QString const& text) {
        if (m_c.output)
            m_c.output->log(text);
    };
    if (path.isUndefined() || !extra.isUndefined()) {
        say(tr("Wrong number of parameters!"));
        return false;
    }
    if (!path.isString()) {
        say(options.isUndefined() ? tr("Parameter needs to be a file path!") : tr("First parameter needs to be a file path!"));
        return false;
    }
    if (!options.isUndefined() && !options.isObject()) {
        say(tr("Second parameter needs to be an Object with parameters!"));
        return false;
    }
    AtomAnimation::Options chosen;
    if (options.isObject()) {
        QStringList const known = exporting ? QStringList{QStringLiteral("fields"), QStringLiteral("scale"), QStringLiteral("overwrite"), QStringLiteral("selectedOnly")}
                                            : QStringList{QStringLiteral("fields"), QStringLiteral("scale")};
        QJSValueIterator it(options);
        while (it.hasNext()) {
            it.next();
            QString const name = it.name();
            QJSValue const value = it.value();
            if (!known.contains(name)) {
                say(exporting ? tr("Unknown export option '%1'!").arg(name) : tr("Unknown import option '%1'!").arg(name));
                return false;
            }
            if (name == QLatin1String("scale")) {
                if (!value.isNumber()) {
                    say(tr("Wrong type for option '%1'. Expected Number!").arg(name));
                    return false;
                }
                chosen.scale = value.toNumber();
                continue;
            }
            if (!value.isBool()) {
                say(exporting ? tr("Wrong type for export option '%1'. Expected Boolean!").arg(name) : tr("Wrong type for import option '%1'. Expected Boolean!").arg(name));
                return false;
            }
            bool const on = value.toBool();
            if (name == QLatin1String("fields"))
                chosen.fields = on;
            else if (name == QLatin1String("overwrite"))
                chosen.overwrite = on;
            else
                chosen.selectedOnly = on;
        }
    }
    AtomAnimation::Result result;
    if (exporting) {
        if (chosen.selectedOnly && m_c.timeline) {
            chosen.layers = m_c.timeline->selectedLayers();
            if (chosen.layers.isEmpty() && m_c.timeline->currentLayer() >= 0)
                chosen.layers = {m_c.timeline->currentLayer()};
        }
        result = AtomAnimation::write(m_c.document->scene(), path.toString(), chosen);
    } else {
        Scene read = m_c.document->scene();
        result = AtomAnimation::read(read, path.toString(), chosen);
        if (result.ok)
            m_c.document->editScene(tr("Import Animation"), [&read](Scene& s) { s = read; });
    }
    if (!result.ok) {
        say(result.error);
        return false;
    }
    for (QString const& w : std::as_const(result.warnings))
        say(w);
    return true;
}
