// Eclipse - the windows that make a texture of your own.
#include "ui/canvas/TextureDialogs.h"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSlider>

#include <functional>

#include "ui/canvas/PencilTexture.h"
#include "ui/window/MainWindowPrivate.h"

namespace {

QSize const kPreview(420, 130);

QSlider* slider(QWidget* parent, char const* name, int from, int to, int value, QString const& tip)
{
    QSlider* const s = new QSlider(Qt::Horizontal, parent);
    s->setObjectName(QLatin1String(name));
    s->setRange(from, to);
    s->setValue(value);
    s->setToolTip(tip);
    return s;
}

// The window the two have in common: a name, a picture of the grain as it will draw, how soft its
// edge is, and whatever `more` adds above that.  `grain` gives the grain as things stand.
int ask(QWidget* parent, QString const& title, QString const& name, std::function<void(QDialog&, QFormLayout&, std::function<void()>)> more,
        std::function<QImage()> grain)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    QFormLayout* const form = new QFormLayout(&dialog);
    QLineEdit* const nameBox = new QLineEdit(name, &dialog);
    nameBox->setObjectName(QStringLiteral("TextureName"));
    QLabel* const picture = new QLabel(&dialog);
    picture->setObjectName(QStringLiteral("TexturePreview"));
    picture->setFixedSize(kPreview);
    QSlider* const edge = slider(&dialog, "TextureEdge", 0, 100, 50,
                                 QObject::tr("How much the grain thins out towards the two edges of a line: none, and the edge is cut clean."));
    std::function<void()> const show = [&] { picture->setPixmap(QPixmap::fromImage(PencilTexture::preview(grain(), edge->value() / 100.0, kPreview))); };
    form->addRow(QObject::tr("Name"), nameBox);
    more(dialog, *form, show);
    form->addRow(QObject::tr("Soft edge"), edge);
    form->addRow(picture);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(edge, &QSlider::valueChanged, &dialog, show);
    form->addRow(buttons);
    show();
    if (dialog.exec() != QDialog::Accepted)
        return 0;
    return PencilTexture::keep(nameBox->text(), grain(), edge->value() / 100.0);
}

}

int TextureDialogs::importTexture(QWidget* parent)
{
    QString filter;
    for (QByteArray const& format : QImageReader::supportedImageFormats())
        filter += QStringLiteral("*.") + QString::fromLatin1(format) + QLatin1Char(' ');
    QString const file = QFileDialog::getOpenFileName(parent, QObject::tr("Import Texture from Picture"), settings().value(QStringLiteral("paths/texture")).toString(),
                                                      QObject::tr("Pictures (%1)").arg(filter.trimmed()));
    if (file.isEmpty())
        return 0;
    settings().setValue(QStringLiteral("paths/texture"), QFileInfo(file).absolutePath());
    QImage const picture(file);
    if (picture.isNull()) {
        QMessageBox::warning(parent, QObject::tr("Import Texture"), QObject::tr("That file could not be read as a picture."));
        return 0;
    }
    // A texture written out by Eclipse comes in as it is: nothing to ask.
    if (int const id = PencilTexture::importFile(file))
        return id;
    QCheckBox* light = nullptr;
    return ask(
        parent, QObject::tr("Import Texture from Picture"), QFileInfo(file).completeBaseName(),
        [&](QDialog& dialog, QFormLayout& form, std::function<void()> show) {
            light = new QCheckBox(QObject::tr("The light parts are the ink"), &dialog);
            light->setObjectName(QStringLiteral("TextureLight"));
            light->setToolTip(QObject::tr("Off: the line has ink where the picture is dark. On: where it is light. Clear parts of the picture are never ink."));
            QObject::connect(light, &QCheckBox::toggled, &dialog, show);
            form.addRow(light);
            QLabel* const note = new QLabel(QObject::tr("The picture is laid along the line, its height across the line's thickness,\nand repeats along it."), &dialog);
            form.addRow(note);
        },
        [&] { return PencilTexture::grainFromPicture(picture, light && light->isChecked()); });
}

int TextureDialogs::newTexture(QWidget* parent)
{
    QSlider* size = nullptr;
    QSlider* density = nullptr;
    QSlider* streaks = nullptr;
    unsigned seed = 1;
    return ask(
        parent, QObject::tr("New Texture"), QObject::tr("My Texture"),
        [&](QDialog& dialog, QFormLayout& form, std::function<void()> show) {
            size = slider(&dialog, "TextureSize", 1, 100, 30, QObject::tr("From fine grain to coarse lumps."));
            density = slider(&dialog, "TextureDensity", 0, 100, 55, QObject::tr("How much of the line is ink."));
            streaks = slider(&dialog, "TextureStreaks", 0, 100, 0, QObject::tr("How far the grain is drawn out along the line: from specks to bristle streaks."));
            QPushButton* const another = new QPushButton(QObject::tr("Another"), &dialog);
            another->setObjectName(QStringLiteral("TextureAnother"));
            another->setToolTip(QObject::tr("Another grain of the same sort."));
            for (QSlider* const s : {size, density, streaks})
                QObject::connect(s, &QSlider::valueChanged, &dialog, show);
            QObject::connect(another, &QPushButton::clicked, &dialog, [&seed, show] {
                ++seed;
                show();
            });
            form.addRow(QObject::tr("Grain"), size);
            form.addRow(QObject::tr("Ink"), density);
            form.addRow(QObject::tr("Streaks"), streaks);
            form.addRow(another);
        },
        [&] { return size ? PencilTexture::makeGrain(size->value(), density->value(), streaks->value(), seed) : QImage(); });
}
