// Eclipse - the Preferences dialog: the Palettes page - palette lists, new colours, the Colour Editor, colour recovery.
#include "ui/window/MainWindow.h"
#include "ui/io/PreferenceWidgets.h"
#include "ui/panels/ColourEditor.h"

#include "model/scene/Scene.h"

namespace {

QPushButton* colourButton(QWidget* parent, char const* name, QColor const& now, QString const& title, QDialog& dialog)
{
    QPushButton* const button = new QPushButton(parent);
    button->setObjectName(QLatin1String(name));
    auto show = [button](QColor const& colour) {
        button->setProperty("colour", colour);
        button->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid #202020; min-height: 16px; min-width: 60px;").arg(colour.name()));
    };
    show(now);
    QObject::connect(button, &QPushButton::clicked, &dialog, [button, show, title, &dialog] {
        QColor const chosen = ColourEditor::getColour(button->property("colour").value<QColor>(), &dialog, title);
        if (chosen.isValid())
            show(chosen);
    });
    return button;
}

}  // namespace

void MainWindow::preferencesPalettes(PreferenceWidgets& w, QSettings& s, QDialog& dialog)
{
    w.palettes = new QGroupBox(tr("Palettes"), &dialog);
    QFormLayout* const form = new QFormLayout(w.palettes);
    auto check = [&](char const* name, QString const& text, QString const& key, bool usual, QString const& tip) {
        QCheckBox* const box = new QCheckBox(text, w.palettes);
        box->setObjectName(QLatin1String(name));
        box->setChecked(s.value(key, usual).toBool());
        box->setToolTip(tip);
        form->addRow(box);
        return box;
    };
    w.advancedPalettes = check("PrefAdvancedPalettes", tr("Advanced Palette Lists"), QStringLiteral("prefs/advancedPalettes"), true,
                               tr("Layers can have palettes of their own (element palettes). Off: palettes belong to the scene only."));
    w.elementPalettesDefault = check("PrefElementPalettesDefault", tr("Default to Element Palette Lists"), QStringLiteral("prefs/elementPalettesDefault"), false,
                                     tr("A new palette belongs to the layer worked on rather than to the scene."));
    w.swatchMode = check("PrefSwatchMode", tr("Swatch Mode"), QStringLiteral("colour/grid"), false, tr("The Colour view shows its colours as a grid of patches."));
    w.paletteBackground = colourButton(w.palettes, "PrefPaletteBackground", s.value(QStringLiteral("prefs/paletteBackground"), QColor(43, 43, 43)).value<QColor>(),
                                       tr("Palette Manager Background Colour"), dialog);
    form->addRow(tr("Background behind the colours"), w.paletteBackground);

    form->addRow(new QLabel(QStringLiteral("<b>%1</b>").arg(tr("Colour Editor")), w.palettes));
    w.multiwheel = check("PrefMultiwheel", tr("Default Multiwheel Colour Picker"), QStringLiteral("colour/multiwheel"), false,
                         tr("The Colour Editor opens with the ring of hues round a square, rather than the disc."));
    w.hsvSliders = check("PrefHsvSliders", tr("Sliders in HSV Mode"), QStringLiteral("colour/hsvSliders"), true,
                         tr("The Colour Editor's sliders are hue, saturation and value (off: red, green and blue)."));

    form->addRow(new QLabel(QStringLiteral("<b>%1</b>").arg(tr("New Colours")), w.palettes));
    w.newUsesDefault = check("PrefNewUsesDefault", tr("Create New Colour Pots Using Default Colour"), QStringLiteral("prefs/newColourUsesDefault"), false,
                             tr("Colours > New makes the default colour below. Off: a copy of the colour in hand."));
    w.newDefault = colourButton(w.palettes, "PrefNewDefault", s.value(QStringLiteral("prefs/newColourDefault"), QColor(Qt::black)).value<QColor>(),
                                tr("New Default Colour"), dialog);
    form->addRow(tr("New default colour"), w.newDefault);

    form->addRow(new QLabel(QStringLiteral("<b>%1</b>").arg(tr("Colour Recovery")), w.palettes));
    w.colourRecovery = check("PrefColourRecovery", tr("Enable Colour Recovery"), QStringLiteral("prefs/colourRecovery"), true,
                             tr("Artwork pasted from another scene, in colours this one lacks: those colours are made again, in \"Recovered Palette\"."));
    w.interactiveRecovery = check("PrefInteractiveRecovery", tr("Interactive Colour Recovery"), QStringLiteral("prefs/interactiveRecovery"), false,
                                  tr("Ask before recovering colours."));
    w.replacement = colourButton(w.palettes, "PrefReplacementColour", s.value(QStringLiteral("prefs/replacementColour"), QColor(255, 0, 0)).value<QColor>(),
                                 tr("Replacement Colour"), dialog);
    w.replacement->setToolTip(tr("Without colour recovery, what is in a colour that cannot be found is shown in this colour."));
    form->addRow(tr("Replacement colour"), w.replacement);
    w.replacement->setEnabled(!w.colourRecovery->isChecked());
    w.interactiveRecovery->setEnabled(w.colourRecovery->isChecked());
    connect(w.colourRecovery, &QCheckBox::toggled, w.replacement, [&w](bool on) {
        w.replacement->setEnabled(!on);
        w.interactiveRecovery->setEnabled(on);
    });

    w.pasteColours = new QComboBox(w.palettes);
    w.pasteColours->setObjectName(QStringLiteral("PrefPasteColours"));
    w.pasteColours->addItem(tr("Reuse colours; add the others to the scene palette"), int(Scene::ReuseColours));
    w.pasteColours->addItem(tr("Copy palettes if they do not exist"), int(Scene::CopyPalettes));
    w.pasteColours->addItem(tr("Update existing colours"), int(Scene::UpdateColours));
    w.pasteColours->addItem(tr("Always add new colours"), int(Scene::NewColours));
    w.pasteColours->setCurrentIndex(std::max(0, w.pasteColours->findData(s.value(QStringLiteral("prefs/pasteColours"), 0).toInt())));
    w.pasteColours->setToolTip(tr("What becomes of the colours of a template brought into the scene."));
    form->addRow(tr("Templates pasted: their colours"), w.pasteColours);
}

void MainWindow::savePalettePreferences(PreferenceWidgets& w, QSettings& s)
{
    s.setValue(QStringLiteral("prefs/advancedPalettes"), w.advancedPalettes->isChecked());
    s.setValue(QStringLiteral("prefs/elementPalettesDefault"), w.elementPalettesDefault->isChecked());
    s.setValue(QStringLiteral("colour/grid"), w.swatchMode->isChecked());
    s.setValue(QStringLiteral("colour/multiwheel"), w.multiwheel->isChecked());
    s.setValue(QStringLiteral("colour/hsvSliders"), w.hsvSliders->isChecked());
    s.setValue(QStringLiteral("prefs/newColourUsesDefault"), w.newUsesDefault->isChecked());
    s.setValue(QStringLiteral("prefs/newColourDefault"), w.newDefault->property("colour").value<QColor>());
    s.setValue(QStringLiteral("prefs/colourRecovery"), w.colourRecovery->isChecked());
    s.setValue(QStringLiteral("prefs/interactiveRecovery"), w.interactiveRecovery->isChecked());
    s.setValue(QStringLiteral("prefs/replacementColour"), w.replacement->property("colour").value<QColor>());
    s.setValue(QStringLiteral("prefs/pasteColours"), w.pasteColours->currentData().toInt());
    s.setValue(QStringLiteral("prefs/paletteBackground"), w.paletteBackground->property("colour").value<QColor>());
}
