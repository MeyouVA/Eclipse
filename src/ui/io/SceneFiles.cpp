// Eclipse - the main window: the files a scene uses - missing ones, unused ones, the Element Manager.
#include "ui/window/MainWindow.h"

#include <functional>

#include <algorithm>
#include <cmath>

#include <QApplication>
#include <QTimer>
#include <QPlainTextEdit>
#include <QCheckBox>
#include "ui/panels/ColourEditor.h"
#include <QTabWidget>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QPushButton>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QDirIterator>
#include <QMessageBox>
#include <QSet>
#include <QStatusBar>

#include "audio/sound/Sound.h"
#include "model/document/Commands.h"
#include "canvas/Canvas.h"
#include "ui/timeline/Xsheet.h"
#include "ui/timeline/FunctionView.h"
#include "ui/views/PreviewManagerView.h"
#include "ui/views/CoordView.h"
#include "actions/manager/ActionManager.h"
#include <QAction>
#include <QToolButton>
#include "ui/panels/LibraryView.h"
#include "ui/panels/SubstitutionPanel.h"
#include "ui/io/MovieExport.h"
#include "ui/panels/PalettePanel.h"
#include "ui/io/ImportExportPrivate.h"
#include "ui/views/NodeView.h"
#include "actions/theme/InterfaceColours.h"
#include <QHeaderView>
#include <QTreeWidget>
#include <QLibraryInfo>
#include <QTranslator>

#include <tuple>
#include "ui/io/PreferenceWidgets.h"

QStringList MainWindow::sceneFiles(bool missingOnly) const
{
    QStringList files;
    auto add = [&](QString const& file) {
        if (!file.isEmpty() && !files.contains(file) && !(missingOnly && QFileInfo::exists(file)))
            files << file;
    };
    for (Layer const& layer : m_document->scene().layers) {
        if (layer.kind == Layer::Sound)
            add(layer.soundFile);
        for (Drawing const& drawing : layer.drawings) {
            for (Picture const& picture : drawing.pictures)
                if (picture.linked)
                    add(picture.file);
            add(drawing.model);
        }
    }
    for (QString const& reference : m_document->scene().references)
        add(reference);
    return files;
}

void MainWindow::onActionRemoveUnusedFiles()
{
    // The files in the scene's media folder (its-name-media, beside it) that nothing in the scene uses any more.
    if (m_path.isEmpty()) {
        QMessageBox::information(this, tr("Remove Unused Files"), tr("Save the scene first: its files are kept beside its file."));
        return;
    }
    QStringList const unused = unusedFiles();
    if (unused.isEmpty()) {
        QMessageBox::information(this, tr("Remove Unused Files"), tr("Every file in the scene's media folder is used."));
        return;
    }
    if (QMessageBox::question(this, tr("Remove Unused Files"),
                              tr("%n file(s) in the scene's media folder are not used by the scene any more. Move them to the Recycle Bin?", nullptr,
                                 int(unused.size())))
        != QMessageBox::Yes)
        return;
    int moved = 0;
    for (QString const& file : unused)
        moved += QFile::moveToTrash(file) ? 1 : 0;
    statusBar()->showMessage(tr("%n unused file(s) moved to the Recycle Bin.", nullptr, moved), 6000);
}

QStringList MainWindow::unusedFiles() const
{
    QStringList unused;
    if (m_path.isEmpty())
        return unused;
    QFileInfo const scene(m_path);
    QDir const media(scene.dir().filePath(scene.completeBaseName() + QStringLiteral("-media")));
    if (!media.exists())
        return unused;
    QSet<QString> used;
    // (Compared as the files really are: one path may be written another way than the other.)
    auto real = [](QString const& file) {
        QString const canonical = QFileInfo(file).canonicalFilePath();
        return (canonical.isEmpty() ? QDir::cleanPath(QFileInfo(file).absoluteFilePath()) : canonical).toLower();
    };
    for (QString const& file : sceneFiles(false))
        used.insert(real(file));
    QDirIterator it(media.absolutePath(), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString const file = QDir::cleanPath(it.next());
        if (!used.contains(real(file)))
            unused << file;
    }
    return unused;
}

void MainWindow::onActionCheckFiles()
{
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("CheckFilesDialog"));
    dialog.setWindowTitle(tr("Check Files"));
    QListWidget* const list = new QListWidget(&dialog);
    list->setObjectName(QStringLiteral("CheckFilesList"));
    list->setMinimumSize(520, 240);
    QLabel* const summary = new QLabel(&dialog);
    auto fill = [this, list, summary] {
        QStringList const all = sceneFiles(false), missing = sceneFiles(true);
        list->clear();
        for (QString const& file : all) {
            bool const gone = missing.contains(file);
            QListWidgetItem* const item = new QListWidgetItem((gone ? tr("MISSING   ") : tr("found        ")) + QDir::toNativeSeparators(file), list);
            item->setData(Qt::UserRole, file);
            if (gone)
                item->setForeground(QColor(240, 110, 90));
        }
        summary->setText(all.isEmpty() ? tr("The scene uses no files outside itself: everything is kept in the scene file.")
                         : missing.isEmpty() ? tr("All %1 files the scene uses are where they should be.").arg(all.size())
                                             : tr("%1 of the %2 files the scene uses cannot be found.").arg(missing.size()).arg(all.size()));
    };
    fill();
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    // A file that has been moved: say where it is now.
    QPushButton* const locate = buttons->addButton(tr("Locate..."), QDialogButtonBox::ActionRole);
    locate->setObjectName(QStringLiteral("CheckFilesLocate"));
    locate->setToolTip(tr("Point the file chosen in the list at where it is now."));
    locate->setEnabled(false);
    connect(list, &QListWidget::currentRowChanged, locate, [locate](int row) { locate->setEnabled(row >= 0); });
    connect(locate, &QPushButton::clicked, &dialog, [this, list, fill, &dialog] {
        if (!list->currentItem())
            return;
        QString const was = list->currentItem()->data(Qt::UserRole).toString();
        QString const now = QFileDialog::getOpenFileName(&dialog, tr("Locate %1").arg(QFileInfo(was).fileName()), QFileInfo(was).absolutePath());
        if (!now.isEmpty() && m_document->relinkFile(was, now))
            fill();
    });
    QVBoxLayout* const layout = new QVBoxLayout(&dialog);
    layout->addWidget(summary);
    layout->addWidget(list);
    layout->addWidget(buttons);
    dialog.exec();
}

void MainWindow::onActionElementManager()
{
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("ElementManagerDialog"));
    dialog.setWindowTitle(tr("Element Manager"));
    QListWidget* const list = new QListWidget(&dialog);
    list->setObjectName(QStringLiteral("ElementList"));
    list->setMinimumSize(420, 260);
    QLineEdit* const name = new QLineEdit(&dialog);
    name->setObjectName(QStringLiteral("ElementName"));
    name->setPlaceholderText(tr("Name of the element"));
    QPushButton* const add = new QPushButton(tr("Add"), &dialog);
    add->setObjectName(QStringLiteral("ElementAdd"));
    add->setToolTip(tr("Adds a drawing layer with that name."));
    QPushButton* const rename = new QPushButton(tr("Rename"), &dialog);
    rename->setObjectName(QStringLiteral("ElementRename"));
    rename->setToolTip(tr("Gives the element chosen in the list that name."));
    QPushButton* const remove = new QPushButton(tr("Delete"), &dialog);
    remove->setObjectName(QStringLiteral("ElementDelete"));
    remove->setToolTip(tr("Deletes the element chosen in the list, with all its drawings."));
    auto layerOf = [this](QListWidgetItem const* item) {
        Scene const& scene = m_document->scene();
        for (int l = 0; item && l < scene.layers.size(); ++l)
            if (scene.layers[l].id == item->data(Qt::UserRole).toInt())
                return l;
        return -1;
    };
    auto fill = [this, list](int chosenId) {
        Scene const& scene = m_document->scene();
        list->clear();
        // From the front of the picture to the back, as the Timeline has them.
        for (int l = int(scene.layers.size()) - 1; l >= 0; --l) {
            Layer const& layer = scene.layers[l];
            if (!layer.holdsDrawings())
                continue;
            int shown = 0;
            for (int cell : layer.cells)
                shown += cell != Layer::kNoDrawing ? 1 : 0;
            QListWidgetItem* const item = new QListWidgetItem(tr("%1   -   %n drawing(s)", nullptr, int(layer.drawings.size())).arg(layer.name)
                                                              + tr(", shown on %n frame(s)", nullptr, shown), list);
            item->setData(Qt::UserRole, layer.id);
            if (layer.id == chosenId)
                list->setCurrentItem(item);
        }
    };
    fill(-1);
    auto enable = [=] {
        bool const one = list->currentItem() != nullptr, named = !name->text().trimmed().isEmpty();
        add->setEnabled(named);
        rename->setEnabled(one && named);
        remove->setEnabled(one);
    };
    enable();
    connect(name, &QLineEdit::textChanged, &dialog, enable);
    connect(list, &QListWidget::currentItemChanged, &dialog, [=](QListWidgetItem* item) {
        if (item) {
            int const layer = layerOf(item);
            if (layer >= 0)
                name->setText(m_document->scene().layers[layer].name);
        }
        enable();
    });
    connect(add, &QPushButton::clicked, &dialog, [=] {
        m_document->beginMacro(tr("Add Element"));
        int const made = m_document->addLayer(int(m_document->scene().layers.size()) - 1);
        if (made >= 0)
            m_document->renameLayer(made, name->text().trimmed());
        m_document->endMacro();
        fill(made >= 0 ? m_document->scene().layers[made].id : -1);
        enable();
    });
    connect(rename, &QPushButton::clicked, &dialog, [=] {
        int const layer = layerOf(list->currentItem());
        if (layer < 0)
            return;
        int const id = m_document->scene().layers[layer].id;
        m_document->renameLayer(layer, name->text().trimmed());
        fill(id);
        enable();
    });
    connect(remove, &QPushButton::clicked, &dialog, [=] {
        int const layer = layerOf(list->currentItem());
        if (layer < 0)
            return;
        m_document->removeLayers({layer});
        fill(-1);
        enable();
    });
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QHBoxLayout* const row = new QHBoxLayout;
    row->addWidget(name, 1);
    row->addWidget(add);
    row->addWidget(rename);
    row->addWidget(remove);
    QVBoxLayout* const layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(tr("The elements of the scene: the layers that hold drawings."), &dialog));
    layout->addWidget(list);
    layout->addLayout(row);
    layout->addWidget(buttons);
    dialog.exec();
}
