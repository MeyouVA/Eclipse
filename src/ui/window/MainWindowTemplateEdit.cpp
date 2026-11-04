// Eclipse - the main window: a Library template edited in place of the scene, its 3D models scaled, and versions swapped.
#include "ui/window/MainWindow.h"

#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <QSettings>
#include <QStatusBar>

#include "canvas/Canvas.h"
#include "model/document/Commands.h"
#include "ui/panels/LibraryView.h"
#include "ui/timeline/Timeline.h"

// The scene put aside while a template is edited, and what the template was when it was opened.
struct TemplateEditing {
    QString path;           // the template's file
    Scene scene;            // the scene put aside
    QString scenePath;
    bool sceneUnsaved = false;
    int layer = 0;
    int frame = 0;
    QStringList layout;     // the template's layers when opened (name and kind): what pastes onto layers matches by
};

namespace {

QStringList layoutOf(Scene const& scene)
{
    QStringList layout;
    for (Layer const& l : scene.layers)
        layout << QStringLiteral("%1|%2").arg(l.name).arg(int(l.kind));
    return layout;
}

}  // namespace

bool MainWindow::editingTemplate() const
{
    return m_templateEdit != nullptr;
}

QString MainWindow::editedTemplate() const
{
    return m_templateEdit ? m_templateEdit->path : QString();
}

void MainWindow::onActionEditTemplate()
{
    QString const path = m_library ? m_library->currentTemplate() : QString();
    QString error;
    if (!editTemplate(path, &error) && !error.isEmpty())
        QMessageBox::warning(this, tr("Edit Template"), error);
}

bool MainWindow::editTemplate(QString const& path, QString* error)
{
    auto fail = [&](QString const& why) {
        if (error)
            *error = why;
        return false;
    };
    QString const name = QFileInfo(path).completeBaseName();
    if (m_templateEdit)
        return fail(tr("A template is being edited already (%1). Close Template Edition first.").arg(QFileInfo(m_templateEdit->path).completeBaseName()));
    if (!LibraryView::isTemplate(path))
        return fail(tr("Choose a template in the Library to edit."));
    QString why;
    if (m_library && !m_library->canModifyPath(path, &why))
        return fail(tr("Cannot lock template for edition (yet).\n%1").arg(why));
    Scene piece;
    if (!Scene::load(path, piece, &why))
        return fail(tr("Template %1 could not be edited.\n%2").arg(name, why));
    if (piece.layers.isEmpty())
        return fail(tr("Template %1 could not be edited: it has no layers.").arg(name));
    statusBar()->showMessage(tr("Starting Template Editing: %1").arg(name), 3000);
    stop();
    auto state = std::make_shared<TemplateEditing>();
    state->path = QFileInfo(path).absoluteFilePath();
    state->scene = m_document->wholeScene();
    state->scene.uiState = projectUiState();
    state->scenePath = m_path;
    state->sceneUnsaved = m_document->unsaved();
    state->layer = layer();
    state->frame = frame();
    state->layout = layoutOf(piece);
    m_templateEdit = state;
    // (No path: autosave and the scene's own commands leave the template alone.)
    m_path.clear();
    m_document->reset(piece);
    m_timeline->setCurrentCell(int(piece.layers.size()) - 1, LibraryView::firstDrawnFrame(piece));
    m_canvas->setCurrentCell(layer(), frame());
    placeCameraView();
    setTemplateEditBorder(templateEditBorder());
    updateTitle();
    updateActionStates();
    return true;
}

bool MainWindow::saveEditedTemplate(QString* error, bool ask)
{
    if (!m_templateEdit)
        return false;
    QString const path = m_templateEdit->path;
    Scene saved = m_document->wholeScene();
    // Pasted onto layers by name and kind: other layers, and it may not find its layers in the scenes it was made for.
    if (ask && layoutOf(saved) != m_templateEdit->layout
        && QMessageBox::question(this, tr("Template Edition"), tr("This template may no longer be usable as an Action Template.\nDo you want to save?"))
               != QMessageBox::Yes)
        return false;
    QString why;
    if (m_library && !m_library->canModifyPath(path, &why)) {
        if (error)
            *error = why;
        return false;
    }
    QSettings const s(QStringLiteral("Eclipse"), QStringLiteral("Eclipse"));
    if (s.value(QStringLiteral("prefs/packNewTemplates"), false).toBool() && saved.packFiles(Scene::templateFilesFolder(path), &why) < 0) {
        if (error)
            *error = why;
        return false;
    }
    saved.uiState = QJsonObject();
    if (!saved.save(path, &why)) {
        if (error)
            *error = why;
        return false;
    }
    m_templateEdit->layout = layoutOf(saved);
    m_document->setSaved();
    updateTitle();
    if (m_library) {
        m_library->refresh();
        m_library->setCurrentTemplate(path);
    }
    statusBar()->showMessage(tr("Template %1 saved.").arg(QFileInfo(path).completeBaseName()), 4000);
    return true;
}

bool MainWindow::onActionLeaveTemplateEdit()
{
    if (!m_templateEdit)
        return true;
    if (m_document->unsaved()) {
        QMessageBox::StandardButton const answer = QMessageBox::warning(
            this, tr("Template Edition"), tr("The template %1 has unsaved changes. Save them?").arg(QFileInfo(m_templateEdit->path).completeBaseName()),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
        if (answer == QMessageBox::Cancel)
            return false;
        if (answer == QMessageBox::Save) {
            QString error;
            if (!saveEditedTemplate(&error)) {
                if (!error.isEmpty())
                    QMessageBox::warning(this, tr("Template Edition"), error);
                return false;
            }
        }
    }
    // The scene back as it was put aside (unsaved changes it had are still to be saved).
    std::shared_ptr<TemplateEditing> const state = m_templateEdit;
    m_templateEdit.reset();
    stop();
    m_path = state->scenePath;
    m_document->reset(state->scene);
    if (state->sceneUnsaved)
        m_document->markUnsaved();
    m_timeline->setCurrentCell(std::clamp(state->layer, 0, std::max(0, int(state->scene.layers.size()) - 1)),
                               std::clamp(state->frame, 0, std::max(0, state->scene.frameCount() - 1)));
    m_canvas->setCurrentCell(layer(), frame());
    placeCameraView();
    setTemplateEditBorder(QColor());
    updateTitle();
    updateActionStates();
    QJsonObject const ui = state->scene.uiState;
    QString const opened = m_path;
    QTimer::singleShot(0, this, [this, opened, ui] {
        if (m_path == opened && !m_templateEdit)
            restoreProjectUi(ui);
    });
    return true;
}

bool MainWindow::scaleTemplateModels(QString const& path, double factor, QString* error)
{
    auto fail = [&](QString const& why) {
        if (error)
            *error = why;
        return false;
    };
    if (!(factor > 0.0))
        return fail(tr("The scale factor must be more than 0."));
    QString why;
    if (m_library && !m_library->canModifyPath(path, &why))
        return fail(why);
    Scene piece;
    if (!Scene::load(path, piece, &why))
        return fail(why);
    if (piece.scaleModels(factor) == 0)
        return fail(tr("The template %1 has no 3D model to scale.").arg(QFileInfo(path).completeBaseName()));
    if (!piece.save(path, &why))
        return fail(why);
    if (m_library) {
        m_library->refresh();
        m_library->setCurrentTemplate(path);
    }
    return true;
}

void MainWindow::onActionTemplateScaleFactor()
{
    QString const path = m_library ? m_library->currentTemplate() : QString();
    Scene piece;
    if (!LibraryView::isTemplate(path) || !Scene::load(path, piece, nullptr) || !piece.hasModels()) {
        QMessageBox::information(this, tr("Set Scale Factor"), tr("Choose a template that has a 3D model in it."));
        return;
    }
    bool ok = false;
    double const factor = QInputDialog::getDouble(this, tr("Set Scale Factor"), tr("Scale Factor:\nAmount to scale the 3D model."), 1.0, 0.01, 100.0, 3, &ok);
    if (!ok)
        return;
    QString error;
    if (!scaleTemplateModels(path, factor, &error))
        QMessageBox::warning(this, tr("Set Scale Factor"), error);
}

void MainWindow::swapTemplateVersion(QString const& path)
{
    if (!LibraryView::isTemplate(path))
        return;
    Scene piece;
    QString error;
    if (!Scene::load(path, piece, &error)) {
        statusBar()->showMessage(error, 6000);
        return;
    }
    // The drawings of the version, over the frames it has, on the layers of the same names (from the current frame).
    int const start = std::clamp(frame(), 0, m_document->scene().frameCount() - 1);
    int const at = m_document->pasteTemplateOnto(Scene::pasteReady(piece, true, false, m_document->scene().frameCount() - start), start, true, false);
    if (at < 0) {
        statusBar()->showMessage(tr("Version Swapper: the scene has no layers named as those of %1.").arg(QFileInfo(path).completeBaseName()), 6000);
        return;
    }
    statusBar()->showMessage(tr("Version %1 swapped in from frame %2.").arg(QFileInfo(path).completeBaseName()).arg(start + 1), 4000);
}
