// Eclipse - the Preferences dialog: the Templates page - making templates, bringing them in, editing them.
#include "ui/window/MainWindow.h"
#include "ui/io/PreferenceWidgets.h"
#include "ui/panels/ColourEditor.h"

#include "model/scene/Scene.h"

QColor MainWindow::templateEditBorder()
{
    QSettings s(QStringLiteral("Eclipse"), QStringLiteral("Eclipse"));
    return s.value(QStringLiteral("prefs/templateEditBorder"), QColor(255, 140, 0)).value<QColor>();
}

void MainWindow::preferencesTemplates(PreferenceWidgets& w, QSettings& s, QDialog& dialog)
{
    w.templatesPage = new QGroupBox(tr("Templates"), &dialog);
    QFormLayout* const form = new QFormLayout(w.templatesPage);
    auto check = [&](char const* name, QString const& text, QString const& key, bool usual, QString const& tip) {
        QCheckBox* const box = new QCheckBox(text, w.templatesPage);
        box->setObjectName(QLatin1String(name));
        box->setChecked(s.value(key, usual).toBool());
        box->setToolTip(tip);
        form->addRow(box);
        return box;
    };
    form->addRow(new QLabel(QStringLiteral("<b>%1</b>").arg(tr("New Templates")), w.templatesPage));
    w.packNewTemplates = check("PrefPackNewTemplates", tr("Automatically Pack New Template Files"), QStringLiteral("prefs/packNewTemplates"), false,
                               tr("The pictures, sounds and 3D models a new template uses are copied into a folder beside it (Walk.files for Walk), "
                                  "so the template still works when the files it was made from are moved or gone."));
    w.copyToLibrary = new QComboBox(w.templatesPage);
    w.copyToLibrary->setObjectName(QStringLiteral("PrefCopyToLibrary"));
    w.copyToLibrary->addItem(tr("Ask"), 0);
    w.copyToLibrary->addItem(tr("Replace it"), 1);
    w.copyToLibrary->addItem(tr("Keep both (the new one gets another name)"), 2);
    w.copyToLibrary->setCurrentIndex(std::max(0, w.copyToLibrary->findData(s.value(QStringLiteral("prefs/copyToLibrary"), 0).toInt())));
    w.copyToLibrary->setToolTip(tr("What Create Template does when the folder already has a template of that name."));
    form->addRow(tr("When the template already exists"), w.copyToLibrary);

    form->addRow(new QLabel(QStringLiteral("<b>%1</b>").arg(tr("Action Templates")), w.templatesPage));
    w.templateDrop = new QComboBox(w.templatesPage);
    w.templateDrop->setObjectName(QStringLiteral("PrefTemplateDrop"));
    w.templateDrop->addItem(tr("By where it is dropped (onto the cells: onto layers of the same names)"), 0);
    w.templateDrop->addItem(tr("As new layers"), 1);
    w.templateDrop->addItem(tr("Onto the layers of the same names"), 2);
    w.templateDrop->addItem(tr("Ask (Paste Special)"), 3);
    w.templateDrop->addItem(tr("As a symbol"), 4);
    w.templateDrop->setCurrentIndex(std::max(0, w.templateDrop->findData(s.value(QStringLiteral("prefs/templateDrop"), 0).toInt())));
    w.templateDrop->setToolTip(tr("What a template does when it is inserted, or dragged from the Library onto the Timeline or the Camera view."));
    form->addRow(tr("Pasting or dropping from the Library"), w.templateDrop);
    w.pasteSymbolsIfMissing = check("PrefPasteSymbolsIfMissing", tr("Copy symbols if they do not exist"), QStringLiteral("prefs/pasteSymbolsIfMissing"), false,
                                    tr("On: a symbol of a template that the scene has one of the same name of uses the scene's. "
                                       "Off: the template's symbols always come as symbols of their own."));
    w.pasteNewDrawings = check("PrefPasteNewDrawings", tr("Always Create New Drawings"), QStringLiteral("prefs/pasteNewDrawings"), true,
                               tr("On: a template pasted onto layers brings all its drawings as new ones. "
                                  "Off: where the layer has a drawing of the same name, the template's drawing replaces it."));

    form->addRow(new QLabel(QStringLiteral("<b>%1</b>").arg(tr("Editing Template")), w.templatesPage));
    w.templateBorder = new QPushButton(w.templatesPage);
    w.templateBorder->setObjectName(QStringLiteral("PrefTemplateBorder"));
    QPushButton* const button = w.templateBorder;
    auto show = [button](QColor const& colour) {
        button->setProperty("colour", colour);
        button->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid #202020; min-height: 16px; min-width: 60px;").arg(colour.name()));
    };
    show(s.value(QStringLiteral("prefs/templateEditBorder"), QColor(255, 140, 0)).value<QColor>());
    connect(button, &QPushButton::clicked, &dialog, [button, show, &dialog] {
        QColor const chosen = ColourEditor::getColour(button->property("colour").value<QColor>(), &dialog, tr("Current View Border in Template Edit"));
        if (chosen.isValid())
            show(chosen);
    });
    button->setToolTip(tr("The colour of the border round the Camera view while a template is being edited."));
    form->addRow(tr("Current View Border in Template Edit"), button);
}

void MainWindow::saveTemplatePreferences(PreferenceWidgets& w, QSettings& s)
{
    s.setValue(QStringLiteral("prefs/packNewTemplates"), w.packNewTemplates->isChecked());
    s.setValue(QStringLiteral("prefs/copyToLibrary"), w.copyToLibrary->currentData().toInt());
    s.setValue(QStringLiteral("prefs/templateDrop"), w.templateDrop->currentData().toInt());
    s.setValue(QStringLiteral("prefs/pasteNewDrawings"), w.pasteNewDrawings->isChecked());
    s.setValue(QStringLiteral("prefs/pasteSymbolsIfMissing"), w.pasteSymbolsIfMissing->isChecked());
    s.setValue(QStringLiteral("prefs/templateEditBorder"), w.templateBorder->property("colour").value<QColor>());
}
