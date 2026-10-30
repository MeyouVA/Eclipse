// Eclipse - Preferences > Colour Management: the colour configuration, ICC profiles, and what new layers and imports start with.
#include "ui/window/MainWindow.h"
#include "ui/io/PreferenceWidgets.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>

#include "colour/spaces/ColourConfig.h"
#include "model/scene/Scene.h"
#include "ui/panels/ColourSpaceBox.h"

namespace {

// What the configuration in use was made from, so it is read again only when that changes.
QString gConfigKey;

// The colour configuration file: the preference, else (never set) the OCIO environment variable, as other programs do.
QString configFile(QSettings const& s)
{
    if (s.contains(QStringLiteral("prefs/colourConfig")))
        return s.value(QStringLiteral("prefs/colourConfig")).toString();
    return qEnvironmentVariable("OCIO");
}

QString describe()
{
    std::shared_ptr<Cms::Config const> const c = Cms::config();
    QString text = c->file.isEmpty() ? QObject::tr("Eclipse's own colour spaces") : QDir::toNativeSeparators(c->file);
    text += QObject::tr(": %1 colour spaces, %2 displays.").arg(c->spaces.size()).arg(c->displays.size());
    if (!c->warnings.isEmpty())
        text += QStringLiteral("\n") + QObject::tr("%n part(s) cannot be used:", nullptr, int(c->warnings.size())) + QStringLiteral(" ") + c->warnings.first();
    return text;
}

}  // namespace

void MainWindow::preferencesColourManagement(PreferenceWidgets& w, QSettings& s, QDialog& dialog)
{
    w.colourPage = new QGroupBox(tr("Colour Management"), &dialog);
    QFormLayout* const form = new QFormLayout(w.colourPage);
    // The configuration: Eclipse's own, or an OpenColorIO file (.ocio).
    QWidget* const row = new QWidget(w.colourPage);
    QHBoxLayout* const line = new QHBoxLayout(row);
    line->setContentsMargins(0, 0, 0, 0);
    w.colourConfig = new QLineEdit(QDir::toNativeSeparators(configFile(s)), row);
    w.colourConfig->setObjectName(QStringLiteral("PrefColourConfig"));
    w.colourConfig->setPlaceholderText(tr("Eclipse's own colour spaces"));
    w.colourConfig->setToolTip(tr("An OpenColorIO configuration file (.ocio) giving the colour spaces, displays and views; empty: Eclipse's own."));
    QPushButton* const browse = new QPushButton(tr("Browse..."), row);
    browse->setObjectName(QStringLiteral("PrefColourConfigBrowse"));
    QPushButton* const own = new QPushButton(tr("Use Eclipse's Own"), row);
    own->setObjectName(QStringLiteral("PrefColourConfigOwn"));
    line->addWidget(w.colourConfig, 1);
    line->addWidget(browse);
    line->addWidget(own);
    form->addRow(tr("Colour configuration"), row);
    w.colourConfigStatus = new QLabel(describe(), w.colourPage);
    w.colourConfigStatus->setObjectName(QStringLiteral("PrefColourConfigStatus"));
    w.colourConfigStatus->setWordWrap(true);
    form->addRow(QString(), w.colourConfigStatus);
    connect(browse, &QPushButton::clicked, &dialog, [&w, &dialog] {
        QString const file = QFileDialog::getOpenFileName(&dialog, tr("Colour Configuration"), w.colourConfig->text(), tr("OpenColorIO configurations (*.ocio)"));
        if (!file.isEmpty())
            w.colourConfig->setText(QDir::toNativeSeparators(file));
    });
    connect(own, &QPushButton::clicked, &dialog, [&w] { w.colourConfig->clear(); });
    // ICC profiles added as colour spaces of their own.
    w.iccProfiles = new QListWidget(w.colourPage);
    w.iccProfiles->setObjectName(QStringLiteral("PrefIccProfiles"));
    w.iccProfiles->setMaximumHeight(90);
    w.iccProfiles->setToolTip(tr("ICC profiles (made of a matrix and curves) used as colour spaces: for pictures made on other screens or in other programs."));
    for (QString const& file : s.value(QStringLiteral("prefs/iccProfiles")).toStringList())
        w.iccProfiles->addItem(QDir::toNativeSeparators(file));
    QWidget* const iccButtons = new QWidget(w.colourPage);
    QHBoxLayout* const iccLine = new QHBoxLayout(iccButtons);
    iccLine->setContentsMargins(0, 0, 0, 0);
    QPushButton* const addIcc = new QPushButton(tr("Add..."), iccButtons);
    addIcc->setObjectName(QStringLiteral("PrefIccAdd"));
    QPushButton* const removeIcc = new QPushButton(tr("Remove"), iccButtons);
    removeIcc->setObjectName(QStringLiteral("PrefIccRemove"));
    iccLine->addWidget(addIcc);
    iccLine->addWidget(removeIcc);
    iccLine->addStretch(1);
    form->addRow(tr("ICC profiles"), w.iccProfiles);
    form->addRow(QString(), iccButtons);
    connect(addIcc, &QPushButton::clicked, &dialog, [&w, &dialog] {
        for (QString const& file : QFileDialog::getOpenFileNames(&dialog, tr("ICC Profile"), QString(), tr("ICC profiles (*.icc *.icm)")))
            w.iccProfiles->addItem(QDir::toNativeSeparators(file));
    });
    connect(removeIcc, &QPushButton::clicked, &dialog, [&w] { delete w.iccProfiles->currentItem(); });
    // What new layers and imported pictures start with.
    w.newLayerSpace = ColourSpaceBox::make(w.colourPage, QStringLiteral("PrefNewLayerColourSpace"), s.value(QStringLiteral("prefs/newLayerColourSpace")).toString(),
                                           tr("Scene Working Colour Space"));
    w.newLayerSpace->setToolTip(tr("The colour space new drawing layers read their pictures in (Layer Properties: Colour Space)."));
    form->addRow(tr("New Layer Colour Space:"), w.newLayerSpace);
    w.convertOnImport = new QCheckBox(tr("Convert Image Colour Space on Import"), w.colourPage);
    w.convertOnImport->setObjectName(QStringLiteral("PrefConvertOnImport"));
    w.convertOnImport->setChecked(s.value(QStringLiteral("prefs/convertOnImport"), false).toBool());
    w.convertOnImport->setToolTip(tr("Imported pictures are read in their source colour space and put in the scene's working colour space."));
    form->addRow(w.convertOnImport);
    w.importSourceSpace = ColourSpaceBox::make(w.colourPage, QStringLiteral("PrefImportSourceSpace"), s.value(QStringLiteral("prefs/importSourceSpace")).toString(),
                                               tr("From the File (its own profile, else sRGB)"));
    form->addRow(tr("Source Colour Space:"), w.importSourceSpace);
}

void MainWindow::saveColourPreferences(PreferenceWidgets& w, QSettings& s)
{
    s.setValue(QStringLiteral("prefs/colourConfig"), QDir::fromNativeSeparators(w.colourConfig->text().trimmed()));
    QStringList icc;
    for (int i = 0; i < w.iccProfiles->count(); ++i)
        icc << QDir::fromNativeSeparators(w.iccProfiles->item(i)->text());
    s.setValue(QStringLiteral("prefs/iccProfiles"), icc);
    s.setValue(QStringLiteral("prefs/newLayerColourSpace"), ColourSpaceBox::chosen(w.newLayerSpace));
    s.setValue(QStringLiteral("prefs/convertOnImport"), w.convertOnImport->isChecked());
    s.setValue(QStringLiteral("prefs/importSourceSpace"), ColourSpaceBox::chosen(w.importSourceSpace));
}

void MainWindow::applyColourPreferences(QSettings const& s)
{
    QString const file = configFile(s);
    QStringList const icc = s.value(QStringLiteral("prefs/iccProfiles")).toStringList();
    QString const key = file + QLatin1Char('\n') + icc.join(QLatin1Char('\n'));
    if (key != gConfigKey) {
        gConfigKey = key;
        QStringList problems;
        Cms::useConfig(file, icc, &problems);
        if (!problems.isEmpty())
            statusBar()->showMessage(problems.join(QLatin1Char(' ')), 12000);
        emit colourConfigChanged();
    }
    Layer::newLayers.colourSpace = s.value(QStringLiteral("prefs/newLayerColourSpace")).toString();
}
