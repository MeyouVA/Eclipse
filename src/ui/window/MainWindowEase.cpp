// Eclipse - the main window: eases - Quick Ease, Set Ease for Multiple Parameters, Apply Ease to Selection, the Velocity Editor.
#include "ui/window/MainWindow.h"

#include <QStatusBar>

#include "actions/manager/ActionManager.h"
#include "model/document/Commands.h"
#include "ui/timeline/Timeline.h"
#include "ui/views/EaseEditors.h"

namespace {

// The values (Driver::Value) of a kind named in the Set Ease for Multiple Parameters dialog.
QVector<int> valuesOfKind(QString const& kind)
{
    if (kind == QLatin1String("motion"))
        return {Driver::X, Driver::Y, Driver::Z};
    if (kind == QLatin1String("rotation"))
        return {Driver::Angle};
    if (kind == QLatin1String("scale"))
        return {Driver::ScaleX, Driver::ScaleY};
    if (kind == QLatin1String("skew"))
        return {Driver::Skew};
    if (kind == QLatin1String("other"))
        return {Driver::Opacity, Driver::Fov};
    return {};
}

}  // namespace

void MainWindow::onActionQuickEase(QString const& which)
{
    double const third = 0.33;
    bool const in = which != QLatin1String("out"), out = which != QLatin1String("in");
    if (setEaseOnSelection(in ? third : -1.0, out ? third : -1.0) > 0)
        return;
    // Nothing to ease: on a frame that is no keyframe, say why.
    Scene const& scene = m_document->scene();
    int const layer = m_timeline->currentLayer();
    bool const onKey = layer >= 0 && layer < scene.layers.size() && scene.layers[layer].keyIndex(m_timeline->currentFrame()) >= 0;
    if (!onKey && m_timeline->selectionFirstFrame() == m_timeline->selectionLastFrame())
        statusBar()->showMessage(in && !out ? tr("Ease-in can only be set on a keyframe.") : out && !in ? tr("Ease-out can only be set on a keyframe.")
                                                                                              : tr("Ease-in can only be set on a keyframe."), 5000);
    else
        statusBar()->showMessage(tr("There is no keyframe to ease on the frames selected (or they are eased so already)."), 5000);
}

int MainWindow::easeSelection(QStringList const& kinds, QPointF leave, QPointF arrive)
{
    Scene const& scene = m_document->scene();
    int const first = m_timeline->selectionFirstFrame(), last = m_timeline->selectionLastFrame();
    QVector<Document::SegmentEase> eases;
    QVector<QPair<int, int>> morphs;        // (layer, frame)
    for (int l : m_timeline->selectedLayers()) {
        if (l < 0 || l >= scene.layers.size())
            continue;
        Layer const& layer = scene.layers[l];
        for (QString const& kind : kinds) {
            for (int value : valuesOfKind(kind)) {
                if (!Timeline::parameterFor(layer.kind, Timeline::Parameter(value)))
                    continue;
                // The moves between two points of the value that the frames selected reach into.
                QVector<int> const through = layer.keysOf(value);
                for (int n = 0; n + 1 < through.size(); ++n) {
                    Keyframe const& a = layer.keys[through[n]];
                    Keyframe const& b = layer.keys[through[n + 1]];
                    if (a.frame <= last && b.frame > first && a.tween)
                        eases.push_back({l, value, a.frame, b.frame, leave, arrive});
                }
            }
            if (kind == QLatin1String("morphing")) {
                // Each morphing the frames selected reach into, once.
                QVector<int> met;
                for (int f = first; f <= last; ++f) {
                    int const m = layer.morphIndexAt(f);
                    if (m >= 0 && !met.contains(m)) {
                        met.push_back(m);
                        morphs.push_back({l, f});
                    }
                }
            }
        }
    }
    if (eases.isEmpty() && morphs.isEmpty())
        return 0;
    QString const text = tr("Set Ease for Multiple Parameters");
    m_document->beginMacro(text);
    if (!eases.isEmpty())
        m_document->setSegmentEases(eases, text);
    for (QPair<int, int> const& m : std::as_const(morphs))
        m_document->setMorphEase(m.first, m.second, std::clamp(1.0 - arrive.x(), 0.0, 1.0), std::clamp(leave.x(), 0.0, 1.0));
    m_document->endMacro();
    return int(eases.size() + morphs.size());
}

void MainWindow::onActionSetEaseMultiple()
{
    EaseMultipleDialog* dialog = findChild<EaseMultipleDialog*>(QStringLiteral("EaseMultipleDialog"));
    if (!dialog) {
        dialog = new EaseMultipleDialog(this);
        connect(dialog, &EaseMultipleDialog::applyRequested, this, [this, dialog](int step) {
            m_easeLeave = dialog->curve()->leave();
            m_easeArrive = dialog->curve()->arrive();
            m_easeKinds = dialog->kinds();
            if (easeSelection(m_easeKinds, m_easeLeave, m_easeArrive) == 0)
                statusBar()->showMessage(tr("There is no keyframe to ease on the frames selected (or they are eased so already)."), 5000);
            // Apply/Previous, Apply/Next: on to the keyframe before or after, for the next one.
            if (step == 0)
                return;
            Scene const& scene = m_document->scene();
            int const layer = m_timeline->currentLayer();
            if (layer < 0 || layer >= scene.layers.size())
                return;
            int const to = step > 0 ? scene.layers[layer].nextKeyFrame(m_timeline->currentFrame()) : scene.layers[layer].previousKeyFrame(m_timeline->currentFrame());
            if (to >= 0)
                m_timeline->setCurrentCell(layer, to);
        });
    }
    if (!m_easeKinds.isEmpty())
        dialog->setKinds(m_easeKinds);
    dialog->show();
    dialog->raise();
}

void MainWindow::onActionApplyEaseToSelection()
{
    QStringList const kinds = m_easeKinds.isEmpty()
                                  ? QStringList{QStringLiteral("motion"), QStringLiteral("rotation"), QStringLiteral("scale"), QStringLiteral("skew"), QStringLiteral("other")}
                                  : m_easeKinds;
    if (easeSelection(kinds, m_easeLeave, m_easeArrive) == 0)
        statusBar()->showMessage(tr("There is no keyframe to ease on the frames selected (or they are eased so already)."), 5000);
}

void MainWindow::onActionVelocityEditor(bool on)
{
    if (!on) {
        if (m_velocityEditor)
            m_velocityEditor->close();
        return;
    }
    if (!m_velocityEditor) {
        VelocityEditor* const editor = new VelocityEditor(m_document, this);
        editor->setWindowFlag(Qt::Tool, true);
        editor->setAttribute(Qt::WA_DeleteOnClose);
        connect(m_timeline, &Timeline::currentCellChanged, editor, &VelocityEditor::setCurrentCell);
        connect(editor, &QObject::destroyed, m_actions, [this] { m_actions->setChecked(QStringLiteral("VelocityEditor"), false); });
        editor->setCurrentCell(m_timeline->currentLayer(), m_timeline->currentFrame());
        m_velocityEditor = editor;
    }
    m_velocityEditor->show();
    m_velocityEditor->raise();
    m_actions->setChecked(QStringLiteral("VelocityEditor"), true);
}
