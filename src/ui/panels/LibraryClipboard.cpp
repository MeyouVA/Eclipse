// Eclipse - the Library: its items cut, copied and pasted between folders; templates optimized and previewed.
#include "ui/panels/LibraryView.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

namespace {

QStringList s_clipboard;    // the paths cut or copied
bool s_cut = false;

}  // namespace

bool LibraryView::copyCurrent(bool cut)
{
    QString const path = currentTemplate();
    if (path.isEmpty() || symbolIdOf(path) || !QFileInfo(path).isFile())
        return false;
    s_clipboard = {QFileInfo(path).absoluteFilePath()};
    s_cut = cut;
    return true;
}

bool LibraryView::hasClipboard()
{
    return !s_clipboard.isEmpty();
}

bool LibraryView::pasteHere(QString* error, std::function<QString(QString const&)> const& rename)
{
    auto fail = [&](QString const& why) {
        if (error)
            *error = why;
        return false;
    };
    if (s_clipboard.isEmpty())
        return fail(tr("Nothing has been cut or copied in the Library."));
    if (showingSymbols())
        return fail(tr("Templates cannot be pasted among the scene's symbols."));
    QDir const into(currentFolder());
    QString last;
    for (QString const& from : std::as_const(s_clipboard)) {
        QFileInfo const source(from);
        if (!source.isFile())
            return fail(tr("Template not found."));
        QString target = into.filePath(source.fileName());
        bool const same = QFileInfo(target).absoluteFilePath().compare(source.absoluteFilePath(), Qt::CaseInsensitive) == 0;
        if (same && s_cut)
            continue;       // moved onto itself: nothing to do
        // A name taken: another one, asked for or made.
        while (QFileInfo::exists(target)) {
            QString const wanted = rename ? rename(QFileInfo(target).completeBaseName()) : QString();
            QString name = wanted.trimmed();
            if (rename && name.isEmpty())
                return fail(tr("Paste cancelled."));
            if (!rename) {
                name = QFileInfo(target).completeBaseName() + tr(" copy");
            }
            if (!validEntryName(name))
                return fail(tr("Cannot create template in (%1)").arg(QDir::toNativeSeparators(into.absolutePath())));
            target = into.filePath(name + QLatin1Char('.') + source.suffix());
        }
        QString why;
        if (!canModifyPath(target, &why))
            return fail(why);
        if (s_cut && !canModifyPath(source.absoluteFilePath(), &why))
            return fail(why);
        // Written in full first; a cut takes the source away only once the copy is there and whole.
        if (!QFile::copy(source.absoluteFilePath(), target) || QFileInfo(target).size() != source.size()) {
            QFile::remove(target);
            return fail(tr("Cannot create template in (%1)").arg(QDir::toNativeSeparators(into.absolutePath())));
        }
        if (s_cut && !QFile::remove(source.absoluteFilePath()))
            return fail(tr("The template was copied, but the original could not be removed (Move Template)."));
        last = target;
    }
    if (s_cut)
        s_clipboard.clear();
    refresh();
    if (!last.isEmpty())
        setCurrentTemplate(last);
    return true;
}

bool LibraryView::optimizeCurrent(bool ask, QString* report)
{
    QString const path = currentTemplate();
    if (!isTemplate(path)) {
        if (report)
            *report = tr("Palette cleanup on non template: choose a template to optimize.");
        return false;
    }
    QString why;
    if (!canModifyPath(path, &why)) {
        if (report)
            *report = why;
        return false;
    }
    if (ask && QMessageBox::question(this, tr("Optimize Template"),
                                     tr("This takes away the drawings no frame shows and the palettes nothing uses from the template \"%1\".\n"
                                        "What it shows stays the same. Continue?")
                                         .arg(QFileInfo(path).completeBaseName()))
                   != QMessageBox::Yes)
        return false;
    Scene scene;
    if (!Scene::load(path, scene, &why)) {
        if (report)
            *report = why;
        return false;
    }
    auto const [drawings, palettes] = scene.optimizeForTemplate();
    if ((drawings > 0 || palettes > 0) && !scene.save(path, &why)) {
        if (report)
            *report = why;
        return false;
    }
    if (report)
        *report = tr("%n drawing(s) and ", nullptr, drawings) + tr("%n palette(s) taken out.", nullptr, palettes);
    refresh();
    setCurrentTemplate(path);
    return true;
}

void LibraryView::previewCurrent()
{
    QString const path = currentTemplate();
    Scene scene;
    if (int const symbol = symbolIdOf(path); symbol && !m_shownPath.isEmpty())
        scene = m_shown;
    else if (!isTemplate(path) || !Scene::load(path, scene, nullptr))
        return;
    // A window of its own, the template played over and over at its frame rate.
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("TemplatePreview"));
    dialog.setWindowTitle(tr("Preview Template: %1").arg(m_items->currentItem() ? m_items->currentItem()->data(Qt::UserRole + 9).toString() : QString()));
    QLabel* const picture = new QLabel(&dialog);
    picture->setAlignment(Qt::AlignCenter);
    picture->setMinimumSize(480, 270);
    QSlider* const frames = new QSlider(Qt::Horizontal, &dialog);
    frames->setRange(0, std::max(0, scene.frameCount() - 1));
    auto show = [&](int frame) { picture->setPixmap(QPixmap::fromImage(LibraryView::picture(scene, frame, picture->size().expandedTo(QSize(480, 270))))); };
    connect(frames, &QSlider::valueChanged, &dialog, show);
    QTimer timer;
    connect(&timer, &QTimer::timeout, &dialog, [&] { frames->setValue(frames->value() >= frames->maximum() ? 0 : frames->value() + 1); });
    timer.start(int(1000.0 / std::max(1.0, scene.fps)));
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QVBoxLayout* const layout = new QVBoxLayout(&dialog);
    layout->addWidget(picture, 1);
    layout->addWidget(frames);
    layout->addWidget(buttons);
    show(firstDrawnFrame(scene));
    dialog.resize(640, 420);
    dialog.exec();
}
