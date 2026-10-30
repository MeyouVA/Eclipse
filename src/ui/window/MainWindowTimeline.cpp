// Eclipse - the main window: the Timeline's own commands - thumbnails, view modes, frame marks, enabling, solo, synced layers, the show manager.
#include "ui/window/MainWindow.h"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QStatusBar>
#include <QVBoxLayout>

#include "actions/manager/ActionManager.h"
#include "actions/theme/AppSettings.h"
#include "model/document/Commands.h"

namespace {

// The colours of Mark Frame, by name (as the menu gives them).
QColor markColour(QString const& name)
{
    static QHash<QString, QString> const colours = {{"red", "#e0453a"},  {"orange", "#f08c2e"}, {"yellow", "#f0d43a"}, {"green", "#4fbf5a"},
                                                    {"cyan", "#3cc8d2"}, {"blue", "#3a78e0"},   {"purple", "#9a58d8"}, {"pink", "#f07ab4"},
                                                    {"white", "#f4f4f4"}, {"black", "#141414"}};
    return colours.contains(name) ? QColor(colours.value(name)) : QColor();
}

}  // namespace

void MainWindow::onActionThumbnails(QString const& which)
{
    // "toggle": the selected layers' tracks; "all" / "none": every drawing layer's.
    Scene const& scene = m_document->scene();
    QVector<int> layers;
    if (which == QLatin1String("toggle")) {
        layers = m_timeline->selectedLayers();
        if (layers.isEmpty())
            layers = {layer()};
    } else {
        for (int l = 0; l < scene.layers.size(); ++l)
            layers.push_back(l);
    }
    layers.erase(std::remove_if(layers.begin(), layers.end(), [&](int l) { return l < 0 || l >= scene.layers.size() || !scene.layers[l].holdsDrawings(); }),
                 layers.end());
    if (layers.isEmpty()) {
        statusBar()->showMessage(tr("Thumbnails are shown on drawing layers."), 4000);
        return;
    }
    bool on = which == QLatin1String("all");
    if (which == QLatin1String("toggle"))
        on = !scene.layers[layers.first()].thumbnails;
    QVector<int> changed;
    for (int l : std::as_const(layers))
        if (scene.layers[l].thumbnails != on)
            changed.push_back(l);
    if (!changed.isEmpty())
        m_document->editLayers(changed, on ? tr("Show Thumbnails") : tr("Hide Thumbnails"), [on](Layer& l) { l.thumbnails = on; });
}

void MainWindow::onActionCentreOnSelection()
{
    m_timeline->centreOnSelection();
}

void MainWindow::onActionNextViewMode()
{
    // Normal, then the selection only, then the tagged layers, and round again.
    static char const* const modes[] = {"all", "selected", "tagged"};
    static char const* const ids[] = {"TimelineViewAll", "TimelineViewSelected", "TimelineViewTagged"};
    int const now = m_timeline->viewMode() == Timeline::ShowSelected ? 1 : m_timeline->viewMode() == Timeline::ShowTagged ? 2 : 0;
    int const next = (now + 1) % 3;
    onActionTimelineViewMode(QLatin1String(modes[next]));
    m_actions->setChecked(QLatin1String(ids[next]), true);
    statusBar()->showMessage(tr("Timeline: %1").arg(m_actions->action(QLatin1String(ids[next]))->text().remove(QLatin1Char('&'))), 3000);
}

void MainWindow::onActionGotoFrameMark(QString const& which)
{
    // "next" or "previous", and after a colon the colour looked for (none: any): in the layers selected.
    QStringList const parts = which.split(QLatin1Char(':'));
    bool const next = parts.first() == QLatin1String("next");
    QColor const colour = parts.size() > 1 ? markColour(parts[1]) : QColor();
    Scene const& scene = m_document->scene();
    QVector<int> layers = m_timeline->selectedLayers();
    if (layers.isEmpty())
        layers = {layer()};
    int best = -1;
    for (int l : std::as_const(layers)) {
        if (l < 0 || l >= scene.layers.size())
            continue;
        for (auto it = scene.layers[l].frameMarks.constBegin(); it != scene.layers[l].frameMarks.constEnd(); ++it) {
            if (colour.isValid() && it.value() != colour)
                continue;
            int const f = it.key();
            if (next && f > frame() && (best < 0 || f < best))
                best = f;
            if (!next && f < frame() && f > best)
                best = f;
        }
    }
    if (best < 0) {
        statusBar()->showMessage(next ? tr("No frame marker after this frame.") : tr("No frame marker before this frame."), 3000);
        return;
    }
    m_timeline->clearRange();
    m_timeline->setCurrentFrame(best);
}

void MainWindow::onActionEnableLayers(QString const& how)
{
    // Enable Layers / Disable Layers: the selected ones, whatever they were.
    bool const on = how == QLatin1String("enable");
    QVector<int> layers = m_timeline->selectedLayers();
    if (layers.isEmpty())
        layers = {layer()};
    QVector<int> changed;
    for (int l : std::as_const(layers))
        if (l >= 0 && l < m_document->scene().layers.size() && m_document->scene().layers[l].visible != on)
            changed.push_back(l);
    if (!changed.isEmpty())
        m_document->editLayers(changed, on ? tr("Enable Layer") : tr("Disable Layer"), [on](Layer& l) { l.visible = on; });
}

void MainWindow::onActionSoloMode()
{
    bool const on = !m_document->scene().soloMode;
    m_document->setSoloMode(on);
    m_actions->setChecked(QStringLiteral("SoloMode"), on);
    statusBar()->showMessage(on ? tr("Solo mode on: only the solo layers are seen.") : tr("Solo mode off: every enabled layer is seen."), 4000);
}

void MainWindow::onActionSelectSyncedLayers()
{
    Scene const& scene = m_document->scene();
    if (layer() < 0 || layer() >= scene.layers.size() || scene.layers[layer()].syncGroup == 0) {
        statusBar()->showMessage(tr("This layer is not synced with another."), 4000);
        return;
    }
    QVector<int> synced;
    for (int l = 0; l < scene.layers.size(); ++l)
        if (l != layer() && scene.layers[l].syncGroup == scene.layers[layer()].syncGroup)
            synced.push_back(l);
    m_timeline->setExtraLayers(synced);
}

void MainWindow::onActionUnsyncLayer()
{
    Scene const& scene = m_document->scene();
    if (layer() < 0 || layer() >= scene.layers.size() || scene.layers[layer()].syncGroup == 0) {
        statusBar()->showMessage(tr("This layer is not synced with another."), 4000);
        return;
    }
    m_document->syncLayerWith(layer(), -1);
    updateActionStates();
}

void MainWindow::onActionShowFunctions()
{
    // Show/Hide Functions: the rows of the parameters of the layers selected.
    QVector<int> layers = m_timeline->selectedLayers();
    if (layers.isEmpty())
        layers = {layer()};
    layers.erase(std::remove_if(layers.begin(), layers.end(), [this](int l) { return !m_timeline->hasParameters(l); }), layers.end());
    if (layers.isEmpty())
        return;
    bool const show = !m_timeline->parametersShown(layers.first());
    for (int l : std::as_const(layers))
        m_timeline->setParametersShown(l, show);
}

void MainWindow::onActionShowManager()
{
    // The Show Manager: which kinds of layer the Timeline lists.
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("ShowManager"));
    dialog.setWindowTitle(tr("Show Manager"));
    QVBoxLayout* const layout = new QVBoxLayout(&dialog);
    struct Kind {
        char const* action;
        char const* which;
    };
    Kind const kinds[] = {{"TimelineShowDrawings", "drawings"}, {"TimelineShowPegs", "pegs"},       {"TimelineShowSound", "sound"},
                          {"TimelineShowGroups", "groups"},     {"TimelineShowCameras", "cameras"}, {"TimelineShowOthers", "others"}};
    QVector<QCheckBox*> boxes;
    for (Kind const& kind : kinds) {
        QAction* const action = m_actions->action(QLatin1String(kind.action));
        QCheckBox* const box = new QCheckBox(action ? action->text().remove(QLatin1Char('&')) : QString(), &dialog);
        box->setObjectName(QLatin1String("Show") + QLatin1String(kind.which));
        box->setChecked(!action || action->isChecked());
        layout->addWidget(box);
        boxes << box;
    }
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    for (int i = 0; i < boxes.size(); ++i) {
        QAction* const action = m_actions->action(QLatin1String(kinds[i].action));
        if (action && action->isChecked() != boxes[i]->isChecked()) {
            action->setChecked(boxes[i]->isChecked());
            onActionTimelineShowKind(QLatin1String(kinds[i].which));
        }
    }
}

void MainWindow::onActionRippleMarkers(bool on)
{
    m_document->setRippleMarkers(on);
    settings().setValue(QStringLiteral("prefs/rippleMarkers"), on);
    m_actions->setChecked(QStringLiteral("RippleSceneMarkers"), on);
}

void MainWindow::onActionOverwriteDrop(bool on)
{
    m_timeline->setOverwriteDrop(on);
    settings().setValue(QStringLiteral("prefs/overwriteDrop"), on);
    m_actions->setChecked(QStringLiteral("OverwriteDrop"), on);
    statusBar()->showMessage(on ? tr("Drag and drop overwrites the exposures and keyframes where the cells land.")
                                : tr("Drag and drop pushes the exposures and keyframes where the cells land further on."),
                             4000);
}

void MainWindow::onActionLayerPrefixSuffix()
{
    // Add Prefix/Suffix: to the names of the layers selected.
    QVector<int> layers = m_timeline->selectedLayers();
    if (layers.isEmpty())
        layers = {layer()};
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("LayerPrefixSuffix"));
    dialog.setWindowTitle(tr("Add Prefix/Suffix"));
    QFormLayout* const form = new QFormLayout(&dialog);
    QLineEdit* const prefix = new QLineEdit(&dialog);
    prefix->setObjectName(QStringLiteral("LayerPrefix"));
    QLineEdit* const suffix = new QLineEdit(&dialog);
    suffix->setObjectName(QStringLiteral("LayerSuffix"));
    form->addRow(tr("Prefix:"), prefix);
    form->addRow(tr("Suffix:"), suffix);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted || (prefix->text().isEmpty() && suffix->text().isEmpty()))
        return;
    m_document->beginMacro(tr("Add Prefix/Suffix"));
    for (int l : std::as_const(layers))
        if (l >= 0 && l < m_document->scene().layers.size())
            m_document->renameLayer(l, prefix->text() + m_document->scene().layers[l].name + suffix->text());
    m_document->endMacro();
}

void MainWindow::onActionDeleteSelectedDrawings()
{
    // Every drawing exposed in the cells selected, wherever else it is exposed too.
    QVector<int> layers = m_timeline->selectedLayers();
    if (layers.isEmpty())
        layers = {layer()};
    int const first = m_timeline->selectionFirstFrame(), last = m_timeline->selectionLastFrame();
    m_document->beginMacro(tr("Delete Selected Drawings"));
    for (int l : std::as_const(layers)) {
        for (int guard = 0; guard < 10000; ++guard) {
            Scene const& scene = m_document->scene();
            if (l < 0 || l >= scene.layers.size() || !scene.layers[l].holdsDrawings())
                break;
            int f = first;
            while (f <= last && scene.layers[l].drawingAt(f) == Layer::kNoDrawing)
                ++f;
            if (f > last)
                break;
            m_document->deleteDrawing(l, f);
        }
    }
    m_document->endMacro();
}

void MainWindow::onActionDeleteSpecial()
{
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("DeleteSpecial"));
    dialog.setWindowTitle(tr("Delete Special"));
    QVBoxLayout* const layout = new QVBoxLayout(&dialog);
    QCheckBox* const drawings = new QCheckBox(tr("Delete Drawings (the exposure)"), &dialog);
    drawings->setObjectName(QStringLiteral("DeleteSpecialDrawings"));
    drawings->setChecked(true);
    QCheckBox* const timed = new QCheckBox(tr("Delete Timed Values"), &dialog);
    timed->setObjectName(QStringLiteral("DeleteSpecialTimedValues"));
    timed->setToolTip(tr("The keyframes of the frames selected."));
    timed->setChecked(true);
    layout->addWidget(drawings);
    layout->addWidget(timed);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted || (!drawings->isChecked() && !timed->isChecked()))
        return;
    m_document->beginMacro(tr("Delete Special"));
    if (timed->isChecked())
        onActionSelectionKeyframes(QStringLiteral("clear"));
    if (drawings->isChecked())
        onActionClearExposure();
    m_document->endMacro();
}
