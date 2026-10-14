// Eclipse - templates: a piece of the scene saved to the Library, and one brought in.
#include "ui/window/MainWindow.h"

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
#include "ui/canvas/Canvas.h"
#include "ui/panels/LibraryView.h"
#include "ui/io/MovieExport.h"
#include "ui/timeline/Timeline.h"
#include "ui/io/ImportExportPrivate.h"

bool MainWindow::saveTemplate(QString const& path, QString* error)
{
    Scene const& scene = m_document->scene();
    // The frames selected; with none selected, the whole length of the scene.
    bool const range = m_timeline->selectionLastFrame() > m_timeline->selectionFirstFrame();
    int const first = range ? m_timeline->selectionFirstFrame() : 0;
    int const last = range ? m_timeline->selectionLastFrame() : scene.frameCount() - 1;
    Scene const piece = scene.extract(m_timeline->selectedLayers(), first, last);
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
    QSettings s;
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
    // The library holds sounds and palettes too.
    QString const kind = QFileInfo(path).suffix().toLower();
    if (kind == QLatin1String("eclpal")) {
        if (m_palette->importPalette(path, &error))
            statusBar()->showMessage(tr("Palette %1 added to the scene.").arg(QFileInfo(path).completeBaseName()), 5000);
        else
            statusBar()->showMessage(error, 8000);
        return;
    }
    if (kind == QLatin1String("wav")) {
        if (!Sound::load(path, &error)) {
            statusBar()->showMessage(tr("Could not read %1: %2").arg(QDir::toNativeSeparators(path), error), 8000);
            return;
        }
        int const start = std::clamp(frame, 0, m_document->scene().frameCount() - 1);
        int const at = m_document->addSound(path, start);
        m_timeline->clearRange();
        m_timeline->setCurrentCell(at, start);
        return;
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
    QString const path = QDir(m_library->currentFolder()).filePath(name + QLatin1Char('.') + LibraryView::kSuffix);
    if (QFileInfo::exists(path)
        && QMessageBox::question(this, tr("Eclipse"), tr("The library already has a template called \"%1\". Replace it?").arg(name)) != QMessageBox::Yes)
        return;
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
