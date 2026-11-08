// Eclipse - the main window: storyboards and editing programs (File > Import > Animatic Project, File > Export > Edit
// Decision List / Final Cut Pro XML / OpenTimelineIO, Storyboard PDF, captions; shots and markers).
#include "animation/ui/window/MainWindow.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QStatusBar>
#include <QTabWidget>
#include <QVBoxLayout>

#include "formats/edit/AnimaticImport.h"
#include "formats/edit/EditTimeline.h"
#include "model/document/Commands.h"
#include "animation/ui/io/StoryboardPdf.h"
#include "animation/ui/timeline/Timeline.h"
#include "animation/ui/views/StoryboardView.h"

namespace {

// Locate Media: the media of the sequence that cannot be found listed; folders added to look in until they all are (or
// the user goes on without them).  false: cancelled.
bool locateMedia(QWidget* parent, EditTimeline::Sequence const& sequence, QStringList* folders)
{
    QStringList missing = AnimaticImport::missingMedia(sequence, *folders);
    if (missing.isEmpty())
        return true;
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("LocateMediaDialog"));
    dialog.setWindowTitle(QObject::tr("Locate Media"));
    auto* const layout = new QVBoxLayout(&dialog);
    auto* const note = new QLabel(&dialog);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto* const list = new QListWidget(&dialog);
    list->setObjectName(QStringLiteral("LocateMediaList"));
    layout->addWidget(list);
    auto refresh = [&] {
        missing = AnimaticImport::missingMedia(sequence, *folders);
        list->clear();
        for (QString const& file : std::as_const(missing))
            list->addItem(QDir::toNativeSeparators(file));
        note->setText(missing.isEmpty() ? QObject::tr("Every media file has been found.")
                                        : QObject::tr("%n media file(s) of the sequence cannot be found. Browse to the folder they are in; the rest are left out.", nullptr,
                                                      int(missing.size())));
    };
    refresh();
    auto* const buttons = new QDialogButtonBox(&dialog);
    QPushButton* const browse = buttons->addButton(QObject::tr("Browse for Folder..."), QDialogButtonBox::ActionRole);
    browse->setObjectName(QStringLiteral("LocateMediaBrowse"));
    buttons->addButton(QObject::tr("Continue"), QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    QObject::connect(browse, &QPushButton::clicked, &dialog, [&] {
        QString const folder = QFileDialog::getExistingDirectory(&dialog, QObject::tr("Locate Media"));
        if (!folder.isEmpty()) {
            *folders << folder;
            refresh();
        }
    });
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    return dialog.exec() == QDialog::Accepted;
}

}   // namespace

int MainWindow::importAnimatic(QString const& path, QString const& imageFolder, QString* error)
{
    QVector<EditTimeline::Sequence> const sequences = EditTimeline::read(path, m_document->scene().fps, error);
    if (sequences.isEmpty())
        return -1;
    AnimaticImport::Options options;
    options.timespan = AnimaticImport::Options::Whole;
    options.imageFolder = imageFolder;
    options.images = imageFolder.isEmpty() ? AnimaticImport::Options::ClipPictures : AnimaticImport::Options::FirstFrameOfClips;
    options.searchFolders << QFileInfo(path).absolutePath();
    AnimaticImport::Result result;
    Scene changed = m_document->scene();
    if (!AnimaticImport::apply(changed, sequences.first(), options, &result, error))
        return -1;
    m_document->editScene(tr("Import Animatic Project"), [&](Scene& scene) { scene = changed; });
    return result.shots;
}

void MainWindow::onActionImportAnimatic()
{
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("ImportAnimaticProjectDialog"));
    dialog.setWindowTitle(tr("Import Animatic Project"));
    auto* const layout = new QVBoxLayout(&dialog);

    // The file.
    auto* const fileRow = new QHBoxLayout;
    auto* const file = new QLineEdit(&dialog);
    file->setObjectName(QStringLiteral("AnimaticFile"));
    file->setToolTip(tr("XML file representing the animatic project (Final Cut Pro XML), an edit decision list or an OpenTimelineIO file."));
    auto* const browseFile = new QPushButton(tr("Browse..."), &dialog);
    fileRow->addWidget(new QLabel(tr("XML File"), &dialog));
    fileRow->addWidget(file, 1);
    fileRow->addWidget(browseFile);
    layout->addLayout(fileRow);
    auto* const sequenceRow = new QHBoxLayout;
    auto* const sequenceBox = new QComboBox(&dialog);
    sequenceBox->setObjectName(QStringLiteral("AnimaticSequence"));
    sequenceRow->addWidget(new QLabel(tr("Sequence"), &dialog));
    sequenceRow->addWidget(sequenceBox, 1);
    layout->addLayout(sequenceRow);

    // The scene timespan.
    auto* const spanBox = new QGroupBox(tr("Scene Timespan"), &dialog);
    auto* const spanForm = new QGridLayout(spanBox);
    QString const sceneName = m_path.isEmpty() ? tr("(unsaved scene)") : QFileInfo(m_path).completeBaseName();
    auto* const useScene = new QRadioButton(tr("Use the scene timespan clip found in the XML file: %1").arg(sceneName), spanBox);
    useScene->setObjectName(QStringLiteral("AnimaticUseSceneClip"));
    auto* const useOther = new QRadioButton(tr("Choose another timespan clip from the XML file:"), spanBox);
    useOther->setObjectName(QStringLiteral("AnimaticUseOtherClip"));
    auto* const clipBox = new QComboBox(spanBox);
    clipBox->setObjectName(QStringLiteral("AnimaticClipName"));
    clipBox->setToolTip(tr("Clip Name"));
    auto* const useCustom = new QRadioButton(tr("Specify a custom timespan:"), spanBox);
    useCustom->setObjectName(QStringLiteral("AnimaticUseCustom"));
    auto* const in = new QSpinBox(spanBox);
    in->setObjectName(QStringLiteral("AnimaticIn"));
    in->setRange(0, 10000000);
    auto* const duration = new QSpinBox(spanBox);
    duration->setObjectName(QStringLiteral("AnimaticDuration"));
    duration->setRange(1, 10000000);
    duration->setValue(m_document->scene().frameCount());
    auto* const useWhole = new QRadioButton(tr("The whole sequence"), spanBox);
    useWhole->setObjectName(QStringLiteral("AnimaticUseWhole"));
    spanForm->addWidget(useScene, 0, 0, 1, 4);
    spanForm->addWidget(useOther, 1, 0, 1, 2);
    spanForm->addWidget(clipBox, 1, 2, 1, 2);
    spanForm->addWidget(useCustom, 2, 0);
    spanForm->addWidget(new QLabel(tr("In:"), spanBox), 2, 1);
    spanForm->addWidget(in, 2, 2);
    auto* const durationRow = new QHBoxLayout;
    durationRow->addWidget(new QLabel(tr("Duration:"), spanBox));
    durationRow->addWidget(duration);
    spanForm->addLayout(durationRow, 2, 3);
    spanForm->addWidget(useWhole, 3, 0, 1, 4);
    layout->addWidget(spanBox);

    // The reference images.
    auto* const imagesBox = new QGroupBox(tr("Reference Images"), &dialog);
    auto* const imagesLayout = new QVBoxLayout(imagesBox);
    auto* const folderRow = new QHBoxLayout;
    auto* const folder = new QLineEdit(imagesBox);
    folder->setObjectName(QStringLiteral("AnimaticImportPath"));
    folder->setToolTip(tr("Directory containing the exported image sequence (a picture for each frame of the sequence)."));
    auto* const browseFolder = new QPushButton(tr("Browse..."), imagesBox);
    folderRow->addWidget(new QLabel(tr("Import Path"), imagesBox));
    folderRow->addWidget(folder, 1);
    folderRow->addWidget(browseFolder);
    imagesLayout->addLayout(folderRow);
    QButtonGroup* const imageChoice = new QButtonGroup(&dialog);
    QStringList const imageTexts = {tr("No reference images"), tr("Import images identified by the first frame of each clip"),
                                    tr("Import images identified by regular markers in the XML file (all marked images)"),
                                    tr("Import all images of the image sequence"), tr("Import first image of each clip (clips whose media are pictures)")};
    for (int i = 0; i < imageTexts.size(); ++i) {
        auto* const radio = new QRadioButton(imageTexts[i], imagesBox);
        radio->setObjectName(QStringLiteral("AnimaticImages%1").arg(i));
        imageChoice->addButton(radio, i);
        imagesLayout->addWidget(radio);
    }
    imageChoice->button(AnimaticImport::Options::FirstFrameOfClips)->setChecked(true);
    auto* const replace = new QCheckBox(tr("Replace existing Ref_Images column"), imagesBox);
    replace->setObjectName(QStringLiteral("AnimaticReplaceRefImages"));
    replace->setChecked(true);
    replace->setToolTip(tr("A column named Ref_Images may already exist in the Xsheet. Specify whether or not the application must overwrite the existing column "
                           "or create a new one with a different name."));
    imagesLayout->addWidget(replace);
    layout->addWidget(imagesBox);

    // The rest.
    auto* const more = new QGroupBox(tr("Also"), &dialog);
    auto* const moreLayout = new QVBoxLayout(more);
    auto option = [&](QString const& name, QString const& text, QString const& tip) {
        auto* const box = new QCheckBox(text, more);
        box->setObjectName(name);
        box->setToolTip(tip);
        box->setChecked(true);
        moreLayout->addWidget(box);
        return box;
    };
    QCheckBox* const sound = option(QStringLiteral("AnimaticSound"), tr("Import the sound"), tr("A sound layer for each audio track, its clips where they were."));
    QCheckBox* const shots = option(QStringLiteral("AnimaticShots"), tr("Make the storyboard from the clips"), tr("A shot for each clip, a panel from each marker on it."));
    QCheckBox* const markers = option(QStringLiteral("AnimaticMarkers"), tr("Import the sequence's markers"), tr("The sequence's markers as the scene's (Timeline ruler)."));
    QCheckBox* const rate = option(QStringLiteral("AnimaticRate"), tr("Use the sequence's frame rate"), tr("Off: the frames are converted to the scene's own rate."));
    QCheckBox* const length = option(QStringLiteral("AnimaticLength"), tr("Make the scene as long as the timespan"), QString());
    auto* const asScenes = new QCheckBox(tr("Import as scenes: each clip its own scene file, in a folder"), more);
    asScenes->setObjectName(QStringLiteral("AnimaticAsScenes"));
    moreLayout->addWidget(asScenes);
    layout->addWidget(more);
    auto* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    QVector<EditTimeline::Sequence> sequences;
    auto load = [&] {
        sequences.clear();
        sequenceBox->clear();
        clipBox->clear();
        if (file->text().isEmpty())
            return;
        QString why;
        sequences = EditTimeline::read(file->text(), m_document->scene().fps, &why);
        for (EditTimeline::Sequence const& s : std::as_const(sequences))
            sequenceBox->addItem(QStringLiteral("%1 (%2 frames at %3 fps)").arg(s.name).arg(s.length()).arg(s.fps, 0, 'g', 5));
        if (sequences.isEmpty() && !why.isEmpty())
            QMessageBox::warning(&dialog, tr("Import Animatic Project"), why);
    };
    auto clips = [&] {
        clipBox->clear();
        int const s = sequenceBox->currentIndex();
        if (s < 0 || s >= sequences.size())
            return;
        clipBox->addItems(AnimaticImport::clipNames(sequences[s]));
        bool const found = sequences[s].clipNamed(sceneName) != nullptr;
        useScene->setEnabled(found);
        (found ? useScene : useWhole)->setChecked(true);
        duration->setValue(std::max(1, sequences[s].length()));
    };
    connect(sequenceBox, &QComboBox::currentIndexChanged, &dialog, clips);
    connect(file, &QLineEdit::editingFinished, &dialog, load);
    connect(browseFile, &QPushButton::clicked, &dialog, [&] {
        QString const path = QFileDialog::getOpenFileName(&dialog, tr("Select Final Cut Pro XML file"), QFileInfo(m_path).path(), EditTimeline::fileFilter());
        if (!path.isEmpty()) {
            file->setText(path);
            load();
        }
    });
    connect(browseFolder, &QPushButton::clicked, &dialog, [&] {
        QString const path = QFileDialog::getExistingDirectory(&dialog, tr("Directory containing the exported image sequence"), folder->text());
        if (!path.isEmpty())
            folder->setText(path);
    });
    useWhole->setChecked(true);
    if (dialog.exec() != QDialog::Accepted)
        return;
    if (sequences.isEmpty())
        load();
    int const s = sequenceBox->currentIndex();
    if (s < 0 || s >= sequences.size()) {
        QMessageBox::warning(this, tr("Import Animatic Project"), tr("Choose a file that holds a sequence."));
        return;
    }
    AnimaticImport::Options options;
    options.timespan = useScene->isChecked() ? AnimaticImport::Options::SceneClip : useOther->isChecked() ? AnimaticImport::Options::OtherClip
                       : useCustom->isChecked() ? AnimaticImport::Options::Custom : AnimaticImport::Options::Whole;
    options.sceneName = sceneName;
    options.clip = clipBox->currentText();
    options.in = in->value();
    options.duration = duration->value();
    options.images = imageChoice->checkedId();
    options.imageFolder = folder->text().trimmed();
    options.replaceRefImages = replace->isChecked();
    options.sound = sound->isChecked();
    options.shots = shots->isChecked();
    options.markers = markers->isChecked();
    options.rate = rate->isChecked();
    options.length = length->isChecked();
    options.searchFolders << QFileInfo(file->text()).absolutePath();
    if (!locateMedia(this, sequences[s], &options.searchFolders))
        return;
    QString error;
    if (asScenes->isChecked()) {
        QString const target = QFileDialog::getExistingDirectory(this, tr("Import as Scenes: the folder the scenes go in"), QFileInfo(m_path).path());
        if (target.isEmpty())
            return;
        QApplication::setOverrideCursor(Qt::WaitCursor);
        QStringList const written = AnimaticImport::splitIntoScenes(m_document->scene(), sequences[s], options, target, &error);
        QApplication::restoreOverrideCursor();
        if (written.isEmpty())
            QMessageBox::warning(this, tr("Import Animatic Project"), error);
        else
            QMessageBox::information(this, tr("Import Animatic Project"), tr("%n scene(s) written to %1.", nullptr, int(written.size())).arg(QDir::toNativeSeparators(target)));
        return;
    }
    AnimaticImport::Result result;
    Scene changed = m_document->scene();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    bool const ok = AnimaticImport::apply(changed, sequences[s], options, &result, &error);
    if (ok)
        m_document->editScene(tr("Import Animatic Project"), [&](Scene& scene) { scene = changed; });
    QApplication::restoreOverrideCursor();
    if (!ok) {
        QMessageBox::warning(this, tr("Import Animatic Project"), error);
        return;
    }
    statusBar()->showMessage(tr("Animatic imported: %1 frames, %2 reference images, %3 sound layers, %4 shots%5.")
                                 .arg(result.duration).arg(result.images).arg(result.soundLayers.size()).arg(result.shots)
                                 .arg(result.missing.isEmpty() ? QString() : tr(" (%n media file(s) not found)", nullptr, int(result.missing.size()))),
                             10000);
}

bool MainWindow::exportTimeline(QString const& path, QString const& movie, QString* error)
{
    QString const name = m_path.isEmpty() ? QStringLiteral("Untitled") : QFileInfo(m_path).completeBaseName();
    EditTimeline::Sequence const sequence = AnimaticImport::toSequence(m_document->scene(), movie, name);
    QString const suffix = QFileInfo(path).suffix().toLower();
    QByteArray const bytes = suffix == QStringLiteral("edl") ? EditTimeline::writeEdl(sequence)
                            : suffix == QStringLiteral("otio") ? EditTimeline::writeOtio(sequence)
                                                               : EditTimeline::writeFcpXml(sequence);
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(bytes) != bytes.size()) {
        if (error)
            *error = tr("Could not create or overwrite file %1").arg(QDir::toNativeSeparators(path));
        return false;
    }
    return true;
}

void MainWindow::onActionExportTimeline(QString const& kind)
{
    QString const filter = kind == QStringLiteral("edl") ? tr("Edit Decision Lists (*.edl)") : kind == QStringLiteral("otio") ? tr("OpenTimelineIO (*.otio)")
                                                                                                                              : tr("Final Cut Pro XML (*.xml)");
    QString const suffix = kind == QStringLiteral("edl") ? QStringLiteral("edl") : kind == QStringLiteral("otio") ? QStringLiteral("otio") : QStringLiteral("xml");
    QString const base = m_path.isEmpty() ? QStringLiteral("scene") : QFileInfo(m_path).path() + u'/' + QFileInfo(m_path).completeBaseName();
    QString const path = QFileDialog::getSaveFileName(this, tr("Export Timeline"), base + u'.' + suffix, filter);
    if (path.isEmpty())
        return;
    // The clips play the scene's movie: by default the one beside the file, as File > Export > Movie would write it.
    bool ok = false;
    QString const movie = QInputDialog::getText(this, tr("Export Timeline"), tr("The movie the clips play (export it with File > Export > Movie):"), QLineEdit::Normal,
                                                QDir::toNativeSeparators(QFileInfo(path).path() + u'/' + QFileInfo(path).completeBaseName() + QStringLiteral(".mov")), &ok);
    if (!ok)
        return;
    QString error;
    if (!exportTimeline(path, QDir::fromNativeSeparators(movie), &error))
        QMessageBox::warning(this, tr("Export Timeline"), error);
    else
        statusBar()->showMessage(tr("Timeline written to %1.").arg(QDir::toNativeSeparators(path)), 6000);
}

bool MainWindow::exportStoryboardPdf(QString const& path, QString const& profileName, QString* error)
{
    for (StoryboardPdf::Profile const& profile : StoryboardPdf::profiles())
        if (profile.name() == profileName || profileName.isEmpty())
            return StoryboardPdf::write(m_document->scene(), profile, path, m_path, error);
    if (error)
        *error = tr("There is no PDF profile named %1.").arg(profileName);
    return false;
}

void MainWindow::onActionStoryboardPdf()
{
    if (m_document->scene().storyboard.isEmpty()) {
        QMessageBox::information(this, tr("Export Storyboard PDF"), tr("The scene has no storyboard: make shots in the Storyboard view, or import an animatic."));
        return;
    }
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("StoryboardPdfDialog"));
    dialog.setWindowTitle(tr("Export Storyboard PDF"));
    auto* const layout = new QVBoxLayout(&dialog);
    auto* const profileRow = new QHBoxLayout;
    auto* const profileBox = new QComboBox(&dialog);
    profileBox->setObjectName(QStringLiteral("StoryboardPdfProfile"));
    auto* const saveProfile = new QPushButton(tr("Save Profile..."), &dialog);
    saveProfile->setObjectName(QStringLiteral("StoryboardPdfSaveProfile"));
    auto* const deleteProfile = new QPushButton(tr("Delete Profile"), &dialog);
    deleteProfile->setObjectName(QStringLiteral("StoryboardPdfDeleteProfile"));
    profileRow->addWidget(new QLabel(tr("Profile"), &dialog));
    profileRow->addWidget(profileBox, 1);
    profileRow->addWidget(saveProfile);
    profileRow->addWidget(deleteProfile);
    layout->addLayout(profileRow);
    auto* const tabs = new QTabWidget(&dialog);
    tabs->setObjectName(QStringLiteral("StoryboardPdfTabs"));
    layout->addWidget(tabs, 1);

    // A widget for each setting, on the tab it belongs on.
    QHash<QString, QWidget*> editors;
    QHash<QString, QFormLayout*> forms;
    QStringList const fields = m_document->scene().storyboard.captionFields;
    for (StoryboardPdf::Option const& o : StoryboardPdf::options()) {
        if (!forms.contains(o.page)) {
            auto* const page = new QWidget;
            forms.insert(o.page, new QFormLayout(page));
            auto* const scroll = new QScrollArea(tabs);
            scroll->setWidgetResizable(true);
            scroll->setWidget(page);
            tabs->addTab(scroll, tr(o.page.toUtf8().constData()));
        }
        QWidget* editor = nullptr;
        switch (o.type) {
        case StoryboardPdf::Option::Bool: editor = new QCheckBox(tr(o.label.toUtf8().constData())); break;
        case StoryboardPdf::Option::Int: {
            auto* const spin = new QSpinBox;
            spin->setRange(int(o.low), int(o.high));
            editor = spin;
            break;
        }
        case StoryboardPdf::Option::Double: {
            auto* const spin = new QDoubleSpinBox;
            spin->setRange(o.low, o.high);
            spin->setDecimals(2);
            editor = spin;
            break;
        }
        case StoryboardPdf::Option::Font: editor = new QFontComboBox; break;
        case StoryboardPdf::Option::Choice: {
            auto* const combo = new QComboBox;
            for (QString const& c : o.choices)
                combo->addItem(tr(c.toUtf8().constData()));
            editor = combo;
            break;
        }
        default: {
            auto* const line = new QLineEdit;
            if (o.type == StoryboardPdf::Option::Fields)
                line->setPlaceholderText(tr("All: %1 (\"-\": none)").arg(fields.join(QStringLiteral(", "))));
            editor = line;
            break;
        }
        }
        editor->setObjectName(QStringLiteral("Pdf_") + o.key);
        editor->setToolTip(tr(o.tip.toUtf8().constData()));
        editors.insert(o.key, editor);
        if (o.type == StoryboardPdf::Option::Bool)
            forms[o.page]->addRow(editor);
        else if (o.type == StoryboardPdf::Option::File || o.type == StoryboardPdf::Option::Fields) {
            auto* const row = new QWidget;
            auto* const h = new QHBoxLayout(row);
            h->setContentsMargins(0, 0, 0, 0);
            h->addWidget(editor, 1);
            auto* const pick = new QPushButton(o.type == StoryboardPdf::Option::File ? tr("Browse...") : tr("Choose..."));
            h->addWidget(pick);
            auto* const line = static_cast<QLineEdit*>(editor);
            connect(pick, &QPushButton::clicked, &dialog, [&dialog, line, o, fields] {
                if (o.type == StoryboardPdf::Option::File) {
                    QString const path = QFileDialog::getOpenFileName(&dialog, o.label, line->text(), QObject::tr("Images (*.png *.jpg *.jpeg *.bmp)"));
                    if (!path.isEmpty())
                        line->setText(path);
                    return;
                }
                // List Picker: the caption fields ticked.
                QDialog picker(&dialog);
                picker.setWindowTitle(QObject::tr("List Picker"));
                auto* const v = new QVBoxLayout(&picker);
                auto* const list = new QListWidget(&picker);
                QStringList const chosen = line->text().split(u',', Qt::SkipEmptyParts);
                for (QString const& f : fields) {
                    auto* const item = new QListWidgetItem(f, list);
                    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                    item->setCheckState(chosen.isEmpty() || chosen.contains(f) ? Qt::Checked : Qt::Unchecked);
                }
                v->addWidget(list);
                auto* const ok = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &picker);
                v->addWidget(ok);
                QObject::connect(ok, &QDialogButtonBox::accepted, &picker, &QDialog::accept);
                QObject::connect(ok, &QDialogButtonBox::rejected, &picker, &QDialog::reject);
                if (picker.exec() != QDialog::Accepted)
                    return;
                QStringList ticked;
                for (int i = 0; i < list->count(); ++i)
                    if (list->item(i)->checkState() == Qt::Checked)
                        ticked << list->item(i)->text();
                line->setText(ticked.isEmpty() ? QStringLiteral("-") : ticked.size() == fields.size() ? QString() : ticked.join(u','));
            });
            forms[o.page]->addRow(tr(o.label.toUtf8().constData()), row);
        } else {
            forms[o.page]->addRow(tr(o.label.toUtf8().constData()), editor);
        }
    }
    auto show = [&](StoryboardPdf::Profile const& profile) {
        for (StoryboardPdf::Option const& o : StoryboardPdf::options()) {
            QWidget* const e = editors[o.key];
            QVariant const v = profile.value(o.key);
            if (auto* b = qobject_cast<QCheckBox*>(e)) b->setChecked(v.toBool());
            else if (auto* i = qobject_cast<QSpinBox*>(e)) i->setValue(v.toInt());
            else if (auto* d = qobject_cast<QDoubleSpinBox*>(e)) d->setValue(v.toDouble());
            else if (auto* f = qobject_cast<QFontComboBox*>(e)) f->setCurrentFont(QFont(v.toString().isEmpty() ? QStringLiteral("Arial") : v.toString()));
            else if (auto* c = qobject_cast<QComboBox*>(e)) c->setCurrentIndex(v.toInt());
            else if (auto* l = qobject_cast<QLineEdit*>(e)) l->setText(v.toString());
        }
    };
    auto read = [&] {
        StoryboardPdf::Profile profile;
        for (StoryboardPdf::Option const& o : StoryboardPdf::options()) {
            QWidget* const e = editors[o.key];
            if (auto* b = qobject_cast<QCheckBox*>(e)) profile.set(o.key, b->isChecked());
            else if (auto* i = qobject_cast<QSpinBox*>(e)) profile.set(o.key, i->value());
            else if (auto* d = qobject_cast<QDoubleSpinBox*>(e)) profile.set(o.key, d->value());
            else if (auto* f = qobject_cast<QFontComboBox*>(e)) profile.set(o.key, f->currentFont().family());
            else if (auto* c = qobject_cast<QComboBox*>(e)) profile.set(o.key, c->currentIndex());
            else if (auto* l = qobject_cast<QLineEdit*>(e)) profile.set(o.key, l->text());
        }
        return profile;
    };
    QVector<StoryboardPdf::Profile> profiles;
    auto fill = [&](QString const& select) {
        profiles = StoryboardPdf::profiles();
        QSignalBlocker block(profileBox);
        profileBox->clear();
        for (StoryboardPdf::Profile const& p : std::as_const(profiles))
            profileBox->addItem(p.name());
        int const at = std::max(0, profileBox->findText(select));
        profileBox->setCurrentIndex(at);
        if (at < profiles.size())
            show(profiles[at]);
    };
    connect(profileBox, &QComboBox::currentIndexChanged, &dialog, [&](int at) {
        if (at >= 0 && at < profiles.size())
            show(profiles[at]);
    });
    connect(saveProfile, &QPushButton::clicked, &dialog, [&] {
        StoryboardPdf::Profile profile = read();
        bool ok = false;
        QString const name = QInputDialog::getText(&dialog, tr("Save Profile"), tr("Profile name"), QLineEdit::Normal, profile.name(), &ok).trimmed();
        if (!ok || name.isEmpty())
            return;
        profile.set(QStringLiteral("name"), name);
        QString why;
        if (!StoryboardPdf::saveProfile(profile, &why))
            QMessageBox::warning(&dialog, tr("Save Profile"), why);
        fill(name);
    });
    connect(deleteProfile, &QPushButton::clicked, &dialog, [&] {
        int const at = profileBox->currentIndex();
        if (at < 0 || at >= profiles.size() || profiles[at].flag(QStringLiteral("builtIn"))) {
            QMessageBox::information(&dialog, tr("Delete Profile"), tr("The profiles that come with Eclipse cannot be deleted."));
            return;
        }
        StoryboardPdf::removeProfile(profiles[at].name());
        fill(QString());
    });
    QSettings settings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse"));
    fill(settings.value(QStringLiteral("storyboardPdfProfile")).toString());
    auto* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Export..."));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.resize(560, 640);
    if (dialog.exec() != QDialog::Accepted)
        return;
    settings.setValue(QStringLiteral("storyboardPdfProfile"), profileBox->currentText());
    QString const base = m_path.isEmpty() ? QStringLiteral("storyboard") : QFileInfo(m_path).path() + u'/' + QFileInfo(m_path).completeBaseName();
    QString const path = QFileDialog::getSaveFileName(this, tr("Export Storyboard PDF"), base + QStringLiteral(".pdf"), tr("PDF Documents (*.pdf)"));
    if (path.isEmpty())
        return;
    QString error;
    int pages = 0;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    bool const ok = StoryboardPdf::write(m_document->scene(), read(), path, m_path, &error, &pages);
    QApplication::restoreOverrideCursor();
    if (!ok)
        QMessageBox::warning(this, tr("Export Storyboard PDF"), error);
    else
        statusBar()->showMessage(tr("%n page(s) written to %1.", nullptr, pages).arg(QDir::toNativeSeparators(path)), 8000);
}

void MainWindow::onActionExportCaptions()
{
    Storyboard const& board = m_document->scene().storyboard;
    if (board.isEmpty()) {
        QMessageBox::information(this, tr("Export Captions"), tr("The scene has no storyboard."));
        return;
    }
    QString const base = m_path.isEmpty() ? QStringLiteral("captions") : QFileInfo(m_path).path() + u'/' + QFileInfo(m_path).completeBaseName() + QStringLiteral("_captions");
    QString selected;
    QString const path = QFileDialog::getSaveFileName(this, tr("Export Captions"), base + QStringLiteral(".txt"), tr("Text (*.txt);;Comma-Separated Values (*.csv)"), &selected);
    if (path.isEmpty())
        return;
    QByteArray const bytes = path.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive)
                                ? board.captionsCsv(m_document->scene().fps, StoryboardView::timecodeDurations(), StoryboardView::naming())
                                : board.captionsText({}, StoryboardView::naming()).toUtf8();
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(bytes) != bytes.size())
        QMessageBox::warning(this, tr("Export Captions"), tr("Could not create or overwrite file %1").arg(QDir::toNativeSeparators(path)));
    else
        statusBar()->showMessage(tr("Captions written to %1.").arg(QDir::toNativeSeparators(path)), 6000);
}

int MainWindow::shotsToMarkers()
{
    Storyboard const& board = m_document->scene().storyboard;
    if (board.isEmpty())
        return 0;
    m_document->editScene(tr("Markers from Shots"), [&](Scene& scene) {
        for (int s = 0; s < board.shots.size(); ++s) {
            Scene::Marker marker;
            marker.frame = board.shotStart(s);
            marker.length = std::max(1, board.shots[s].length());
            marker.name = board.shots[s].name;
            marker.note = board.shots[s].sequence;
            scene.markers.erase(std::remove_if(scene.markers.begin(), scene.markers.end(), [&](Scene::Marker const& m) { return m.frame == marker.frame; }),
                                scene.markers.end());
            scene.markers << marker;
        }
        std::sort(scene.markers.begin(), scene.markers.end(), [](Scene::Marker const& a, Scene::Marker const& b) { return a.frame < b.frame; });
    });
    return int(board.shots.size());
}

int MainWindow::shotsFromMarkers()
{
    Scene const& scene = m_document->scene();
    if (scene.markers.isEmpty())
        return 0;
    Storyboard board;
    board.title = scene.storyboard.title;
    board.subtitle = scene.storyboard.subtitle;
    board.captionFields = scene.storyboard.captionFields;
    board.start = scene.markers.first().frame;
    Storyboard::Naming const naming = StoryboardView::naming();
    for (int m = 0; m < scene.markers.size(); ++m) {
        int const end = m + 1 < scene.markers.size() ? scene.markers[m + 1].frame : scene.frameCount();
        if (end <= scene.markers[m].frame)
            continue;
        board.addShot(-1, end - scene.markers[m].frame,
                      Storyboard::shotName(scene.markers[m].name.isEmpty() ? QString::number(board.shots.size() + 1) : scene.markers[m].name, naming));
    }
    m_document->editScene(tr("Shots from Markers"), [&](Scene& s) { s.storyboard = board; });
    return int(board.shots.size());
}

void MainWindow::onActionShotsToMarkers()
{
    int const n = shotsToMarkers();
    statusBar()->showMessage(n ? tr("%n marker(s) made from the shots.", nullptr, n) : tr("The scene has no storyboard."), 6000);
}

void MainWindow::onActionShotsFromMarkers()
{
    if (!m_document->scene().storyboard.isEmpty()
        && QMessageBox::question(this, tr("Shots from Markers"), tr("The storyboard is replaced by a shot for each marker. Go on?")) != QMessageBox::Yes)
        return;
    int const n = shotsFromMarkers();
    statusBar()->showMessage(n ? tr("%n shot(s) made from the markers.", nullptr, n) : tr("The scene has no markers."), 6000);
}
