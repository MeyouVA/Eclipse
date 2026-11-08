// Eclipse - templates: a piece of the scene saved to the Library, and one brought in.
#include "animation/ui/window/MainWindow.h"
#include "render/images/ImageFiles.h"
#include "model/media/MediaFormats.h"
#include "animation/ui/io/RawSoundImport.h"
#include "animation/ui/io/RawMediaImport.h"

#include <functional>

#include <algorithm>
#include <cmath>

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QSet>
#include <QLineEdit>
#include <QMessageBox>
#include <QInputDialog>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QRadioButton>
#include <QSettings>
#include <QVBoxLayout>
#include <QRegularExpression>

#include "audio/sound/Sound.h"
#include "model/document/Commands.h"
#include "canvas/Canvas.h"
#include "animation/ui/panels/LibraryView.h"
#include "animation/ui/io/MovieExport.h"
#include "animation/ui/timeline/Timeline.h"
#include "animation/ui/io/ImportExportPrivate.h"

bool MainWindow::saveTemplate(QString const& path, QString* error)
{
    if(m_library && !m_library->canModifyPath(path,error)) return false;
    Scene const& scene = m_document->scene();
    // The frames selected; with none selected, the whole length of the scene.
    bool const range = m_timeline->selectionLastFrame() > m_timeline->selectionFirstFrame();
    int const first = range ? m_timeline->selectionFirstFrame() : 0;
    int const last = range ? m_timeline->selectionLastFrame() : scene.frameCount() - 1;
    Scene piece = scene.extract(m_timeline->selectedLayers(), first, last);
    if (piece.layers.isEmpty()) {
        if (error)
            *error = tr("Select the layers to keep as a template in the Timeline first.");
        return false;
    }
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        if (error)
            *error = tr("Could not make the folder %1.").arg(QDir::toNativeSeparators(QFileInfo(path).absolutePath()));
        return false;
    }
    // Pack Files on New Templates: what it uses from outside copied beside it, and the template pointed at the copies.
    if (QSettings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse")).value(QStringLiteral("prefs/packNewTemplates"), false).toBool()
        && piece.packFiles(Scene::templateFilesFolder(path), error) < 0)
        return false;
    return piece.save(path, error);
}

int MainWindow::insertTemplate(QString const& path, int frame, QString* error)
{
    Scene piece;
    if (!Scene::load(path, piece, error))
        return -1;
    int const at = m_document->insertTemplate(piece, std::clamp(frame, 0, m_document->scene().frameCount() - 1));
    if (at < 0) {
        if (error)
            *error = tr("The template has nothing that can be put in this scene.");
        return -1;
    }
    m_timeline->clearRange();
    m_timeline->setCurrentCell(at, frame);
    return at;
}

int MainWindow::insertTemplateAsSymbol(QString const& path, int frame, QString* error)
{
    QSet<int> had;
    for (Layer const& l : m_document->scene().layers)
        had.insert(l.id);
    m_document->beginMacro(tr("Insert Template as Symbol"));
    int at = insertTemplate(path, frame, error);
    if (at >= 0) {
        QVector<int> made;
        for (int l = 0; l < m_document->scene().layers.size(); ++l)
            if (!had.contains(m_document->scene().layers[l].id))
                made.push_back(l);
        at = made.isEmpty() ? -1 : m_document->createSymbol(made, QFileInfo(path).completeBaseName());
    }
    m_document->endMacro();
    if (at >= 0) {
        m_timeline->clearRange();
        m_timeline->setCurrentCell(at, frame);
        updateActionStates();
    }
    return at;
}

int MainWindow::pasteTemplateSpecial(QString const& path, int frame, bool onto, bool drawings, bool keys, bool extend, QString* error)
{
    Scene loaded;
    if (!Scene::load(path, loaded, error))
        return -1;
    int const start = std::clamp(frame, 0, m_document->scene().frameCount() - 1);
    Scene const piece = Scene::pasteReady(loaded, drawings || onto, keys || onto, extend ? 0 : m_document->scene().frameCount() - start);
    int const at = onto ? m_document->pasteTemplateOnto(piece, start, drawings, keys) : m_document->insertTemplate(piece, start);
    if (at < 0) {
        if (error)
            *error = onto ? tr("The scene does not have a layer of the same name and kind for every layer of the template.")
                          : tr("The template has nothing that can be put in this scene.");
        return -1;
    }
    m_timeline->clearRange();
    m_timeline->setCurrentCell(at, start);
    return at;
}

void MainWindow::onTemplatePasteSpecial(QString const& path)
{
    QSettings s(QStringLiteral("Eclipse"), QStringLiteral("Eclipse"));
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("TemplatePasteSpecialDialog"));
    dialog.setWindowTitle(tr("Paste Special: %1").arg(QFileInfo(path).completeBaseName()));
    QRadioButton* const asNew = new QRadioButton(tr("As new layers"), &dialog);
    asNew->setObjectName(QStringLiteral("TemplateAsNew"));
    QRadioButton* const onto = new QRadioButton(tr("Onto the layers of the same names"), &dialog);
    onto->setObjectName(QStringLiteral("TemplateOnto"));
    (s.value(QStringLiteral("templatePaste/onto"), false).toBool() ? onto : asNew)->setChecked(true);
    QCheckBox* const drawings = new QCheckBox(tr("Drawings (the exposure)"), &dialog);
    drawings->setObjectName(QStringLiteral("TemplateDrawings"));
    drawings->setChecked(s.value(QStringLiteral("templatePaste/drawings"), true).toBool());
    QCheckBox* const keys = new QCheckBox(tr("Keyframes (without them a new layer holds its first pose)"), &dialog);
    keys->setObjectName(QStringLiteral("TemplateKeyframes"));
    keys->setChecked(s.value(QStringLiteral("templatePaste/keys"), true).toBool());
    QCheckBox* const extend = new QCheckBox(tr("Make the scene longer when the template runs past its end"), &dialog);
    extend->setObjectName(QStringLiteral("TemplateExtend"));
    extend->setChecked(s.value(QStringLiteral("templatePaste/extend"), true).toBool());
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QVBoxLayout* const layout = new QVBoxLayout(&dialog);
    for (QWidget* w : std::initializer_list<QWidget*>{asNew, onto, drawings, keys, extend})
        layout->addWidget(w);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    s.setValue(QStringLiteral("templatePaste/onto"), onto->isChecked());
    s.setValue(QStringLiteral("templatePaste/drawings"), drawings->isChecked());
    s.setValue(QStringLiteral("templatePaste/keys"), keys->isChecked());
    s.setValue(QStringLiteral("templatePaste/extend"), extend->isChecked());
    QString error;
    if (pasteTemplateSpecial(path, frame(), onto->isChecked(), drawings->isChecked(), keys->isChecked(), extend->isChecked(), &error) < 0)
        QMessageBox::warning(this, tr("Eclipse"), tr("Could not paste %1:\n%2").arg(QFileInfo(path).completeBaseName(), error));
}

void MainWindow::dropTemplate(QString const& path, int frame, bool onto)
{
    QString error;
    if (int const symbol = LibraryView::symbolIdOf(path)) {
        if (insertLibrarySymbol(symbol) < 0)
            statusBar()->showMessage(tr("Template not found."), 5000);
        return;
    }
    // The library holds sounds and palettes too.
    QString const kind = QFileInfo(path).suffix().toLower();
    bool rawSound=false,rawImage=false,rawMovie=false;
    if(kind==QLatin1String("raw")) {
        QStringList const choices{tr("Camera image"),tr("Raw image"),tr("Raw video"),tr("Raw audio")};
        bool accepted=false;
        QString const choice=QInputDialog::getItem(this,tr("Import RAW File"),tr("Import this file as:"),choices,0,false,&accepted);
        if(!accepted) return;
        rawSound=choice==choices.last();
        rawImage=choice==choices[1]; rawMovie=choice==choices[2];
    }
    else if(RawPixels::needsSettings(path) && RawVideo::needsSettings(path)) {
        bool accepted=false; QStringList const choices{tr("Single raw image"),tr("Raw video")};
        QString const choice=QInputDialog::getItem(this,tr("Import Raw Media"),tr("Import this data as:"),choices,0,false,&accepted);
        if(!accepted) return;
        rawMovie=choice==choices.last();
    }
    if(rawMovie) { if(importMovie(path,&error)<0 && !error.isEmpty()) statusBar()->showMessage(error,8000); return; }
    if(auto const* format=ImageFiles::find(kind);format && format->readable && !rawSound) {
        RawMediaImport::Images prepared;
        if(kind==QLatin1String("raw") && !rawImage) prepared.files={path};
        else prepared=RawMediaImport::images(this,{path},m_path,false,&error,rawImage);
        if(!prepared.ready) { if(!error.isEmpty()) statusBar()->showMessage(error,8000); return; }
        ImageImport options;
        options.files=prepared.files;
        int const at=importImages(options,&error);
        if(at>=0) prepared.keep();
        if(at<0) statusBar()->showMessage(error,8000);
        return;
    }
    if (kind == QLatin1String("eclpal")) {
        if (m_palette->importPalette(path, &error))
            statusBar()->showMessage(tr("Palette %1 added to the scene.").arg(QFileInfo(path).completeBaseName()), 5000);
        else
            statusBar()->showMessage(error, 8000);
        return;
    }
    if(!rawSound && MediaFormats::video(kind)) {
        if(importMovie(path,&error)<0) statusBar()->showMessage(error,8000);
        return;
    }
    if (MediaFormats::audio(kind)) {
        QString const sound=RawSoundImport::prepare(this,path,m_path,&error);
        if(sound.isEmpty()) { if(!error.isEmpty()) statusBar()->showMessage(error,8000); return; }
        if (!Sound::load(sound, &error)) {
            statusBar()->showMessage(tr("Could not read %1: %2").arg(QDir::toNativeSeparators(path), error), 8000);
            return;
        }
        int const start = std::clamp(frame, 0, m_document->scene().frameCount() - 1);
        int const at = m_document->addSound(sound, start);
        m_timeline->clearRange();
        m_timeline->setCurrentCell(at, start);
        return;
    }
    // Preferences > Templates: what a template pasted or dropped from the Library does (0: as where it is dropped says).
    switch (QSettings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse")).value(QStringLiteral("prefs/templateDrop"), 0).toInt()) {
    case 1:
        onto = false;
        break;
    case 2:
        onto = true;
        break;
    case 3:
        onTemplatePasteSpecial(path);
        return;
    case 4:
        if (insertTemplateAsSymbol(path, frame, &error) < 0)
            QMessageBox::critical(this, tr("Eclipse"), tr("Could not use %1:\n%2").arg(QDir::toNativeSeparators(path), error));
        return;
    default:
        break;
    }
    if (onto) {
        // Onto the layers it was made from, or others of the same names, when they are all here.
        Scene piece;
        if (Scene::load(path, piece, &error)) {
            int const at = m_document->pasteTemplateOnto(piece, std::clamp(frame, 0, m_document->scene().frameCount() - 1));
            if (at >= 0) {
                m_timeline->clearRange();
                m_timeline->setCurrentCell(at, frame);
                statusBar()->showMessage(tr("Template %1 pasted onto the layers of the same names from frame %2.").arg(QFileInfo(path).completeBaseName()).arg(frame + 1), 5000);
                return;
            }
        }
        error.clear();
    }
    if (insertTemplate(path, frame, &error) < 0)
        QMessageBox::critical(this, tr("Eclipse"), tr("Could not use %1:\n%2").arg(QDir::toNativeSeparators(path), error));
    else
        statusBar()->showMessage(tr("Template %1 put in the scene from frame %2.").arg(QFileInfo(path).completeBaseName()).arg(frame + 1), 5000);
}

void MainWindow::onActionCreateTemplate()
{
    bool ok = false;
    QString const suggested = m_document->scene().layers[layer()].name;
    QString name = QInputDialog::getText(this, tr("Create Template"), tr("Name of the template:"), QLineEdit::Normal, suggested, &ok).trimmed();
    if (!ok || name.isEmpty())
        return;
    // A name is a file name: what a file name cannot hold is left out.
    name.remove(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")));
    QString path = QDir(m_library->currentFolder()).filePath(name + QLatin1Char('.') + LibraryView::kSuffix);
    if (QFileInfo::exists(path)) {
        // Preferences > Templates: ask, replace it, or keep both (the new one under another name).
        int const action = QSettings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse")).value(QStringLiteral("prefs/copyToLibrary"), 0).toInt();
        if (action == 2) {
            for (int n = 2; QFileInfo::exists(path); ++n)
                path = QDir(m_library->currentFolder()).filePath(QStringLiteral("%1 %2.%3").arg(name).arg(n).arg(LibraryView::kSuffix));
        } else if (action != 1
                   && QMessageBox::question(this, tr("Eclipse"), tr("The library already has a template called \"%1\". Replace it?").arg(name)) != QMessageBox::Yes) {
            return;
        }
    }
    // Layers from different groups: brought back as layers of their own, the groups they were in are not.
    {
        Scene const& scene = m_document->scene();
        QSet<int> groups;
        for (int l : m_timeline->selectedLayers()) {
            int group = 0;
            for (int guard = 0, up = scene.parentIndex(l); up >= 0 && guard < 256 && group == 0; ++guard, up = scene.parentIndex(up))
                if (scene.layers[up].kind == Layer::Group)
                    group = scene.layers[up].id;
            groups.insert(group);
        }
        if (groups.size() > 1
            && QMessageBox::question(this, tr("Create Template"), tr("Template contains nodes/layers from incompatible groups.\nTemplate may be difficult to restore or edit.\n\nCreate it anyway?"))
                   != QMessageBox::Yes)
            return;
    }
    QString error;
    if (!saveTemplate(path, &error)) {
        QMessageBox::critical(this, tr("Eclipse"), tr("The template could not be saved.\n%1").arg(error));
        return;
    }
    showView(dockOf("LibraryDock"));
    m_library->refresh();
    m_library->setCurrentTemplate(path);
    statusBar()->showMessage(tr("Template saved as %1").arg(QDir::toNativeSeparators(path)), 6000);
}

void MainWindow::onActionInsertTemplate()
{
    QString const path = QFileDialog::getOpenFileName(this, tr("Insert Template"), m_library->currentFolder(),
                                                      tr("Eclipse Templates (*.%1);;Eclipse Scenes (*.ecl);;All Files (*)").arg(LibraryView::kSuffix));
    if (!path.isEmpty())
        dropTemplate(path, frame());
}
