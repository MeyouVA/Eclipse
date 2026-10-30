// Eclipse - the main window: the details of a tool preset of your own - its tool, colour, icon, shortcut, script and texture.
#include "ui/window/MainWindow.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QStatusBar>

#include "actions/manager/ActionManager.h"
#include "actions/theme/AppSettings.h"
#include "canvas/Canvas.h"
#include "canvas/PencilTexture.h"
#include "model/document/Commands.h"
#include "ui/panels/ColourEditor.h"
#include "ui/panels/PalettePanel.h"
#include "ui/views/ScriptView.h"

namespace {

QString detailsKey(QString const& kind, QString const& name) { return QStringLiteral("presetDetails/%1/%2").arg(kind, name); }

// The tools a preset of each kind can take up, with the commands that choose them.
QVector<QPair<int, QString>> toolsOf(QString const& kind)
{
    if (kind == QLatin1String("brush"))
        return {{Canvas::Brush, QStringLiteral("ToolBrush")}};
    if (kind == QLatin1String("eraser"))
        return {{Canvas::Eraser, QStringLiteral("ToolEraser")}};
    return {{Canvas::Pencil, QStringLiteral("ToolPencil")}, {Canvas::Line, QStringLiteral("ToolLine")}, {Canvas::Rectangle, QStringLiteral("ToolRectangle")},
            {Canvas::Ellipse, QStringLiteral("ToolEllipse")}, {Canvas::Polyline, QStringLiteral("ToolPolyline")}};
}

}

void MainWindow::onActionPresetDetails(QString const& kind, QString const& name)
{
    QSettings s = settings();
    QVariantMap details = s.value(detailsKey(kind, name)).toMap();
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("PresetDetailsDialog"));
    dialog.setWindowTitle(tr("Preset: %1").arg(name));
    QFormLayout* const form = new QFormLayout(&dialog);

    // The tool it takes up.
    QCheckBox* const setTool = new QCheckBox(tr("Change Tool"), &dialog);
    setTool->setObjectName(QStringLiteral("PresetSetTool"));
    setTool->setToolTip(tr("Check to change the tool when the preset is activated"));
    setTool->setChecked(details.value(QStringLiteral("setTool")).toBool());
    QComboBox* const tool = new QComboBox(&dialog);
    tool->setObjectName(QStringLiteral("PresetTool"));
    for (auto const& [id, command] : toolsOf(kind))
        tool->addItem(m_actions->action(command) ? m_actions->action(command)->text().remove(QLatin1Char('&')) : command, id);
    tool->setCurrentIndex(std::max(0, tool->findData(details.value(QStringLiteral("tool"), toolsOf(kind).first().first).toInt())));
    QHBoxLayout* const toolRow = new QHBoxLayout;
    toolRow->addWidget(setTool);
    toolRow->addWidget(tool, 1);
    form->addRow(toolRow);

    // The colour it takes up.
    QCheckBox* const setColour = new QCheckBox(tr("Colour"), &dialog);
    setColour->setObjectName(QStringLiteral("PresetSetColour"));
    setColour->setToolTip(tr("Check to change the current colour when the preset is activated"));
    setColour->setChecked(details.value(QStringLiteral("setColour")).toBool());
    QPushButton* const chip = new QPushButton(&dialog);
    chip->setObjectName(QStringLiteral("PresetColour"));
    chip->setEnabled(false);
    auto showColour = [chip, &details] {
        QColor const c = details.value(QStringLiteral("colour")).value<QColor>();
        chip->setText(c.isValid() ? QString() : tr("(none)"));
        chip->setStyleSheet(c.isValid() ? QStringLiteral("background-color: %1; min-width: 60px;").arg(c.name()) : QString());
    };
    showColour();
    QPushButton* const copyColour = new QPushButton(tr("Copy Current Colour"), &dialog);
    copyColour->setObjectName(QStringLiteral("PresetCopyColour"));
    copyColour->setToolTip(tr("Click to copy the current colour to the preset"));
    connect(copyColour, &QPushButton::clicked, &dialog, [this, &details, showColour, setColour] {
        int const id = m_palette ? m_palette->currentSwatch() : Scene::kNoSwatch;
        Swatch const* const swatch = m_document->scene().findSwatch(id);
        if (!swatch)
            return;
        details[QStringLiteral("swatch")] = swatch->id;
        details[QStringLiteral("colour")] = swatch->color;
        details[QStringLiteral("colourName")] = swatch->name;
        setColour->setChecked(true);
        showColour();
    });
    QHBoxLayout* const colourRow = new QHBoxLayout;
    colourRow->addWidget(setColour);
    colourRow->addWidget(chip);
    colourRow->addWidget(copyColour);
    form->addRow(colourRow);

    // Its icon in the swatch views of the list.
    QPushButton* const icon = new QPushButton(tr("Select Custom Icon..."), &dialog);
    icon->setObjectName(QStringLiteral("PresetIcon"));
    icon->setToolTip(tr("Click to change icon to show in icon mode"));
    QString iconFile = details.value(QStringLiteral("icon")).toString();
    auto showIcon = [icon, &iconFile] { icon->setIcon(iconFile.isEmpty() ? QIcon() : QIcon(iconFile)); };
    showIcon();
    connect(icon, &QPushButton::clicked, &dialog, [&dialog, &iconFile, showIcon] {
        QString const chosen = QFileDialog::getOpenFileName(&dialog, tr("Select Custom Icon"), iconFile, tr("Pictures (*.png *.jpg *.svg *.ico *.bmp)"));
        if (!chosen.isEmpty()) {
            iconFile = chosen;
            showIcon();
        }
    });
    QPushButton* const noIcon = new QPushButton(tr("No Icon"), &dialog);
    connect(noIcon, &QPushButton::clicked, &dialog, [&iconFile, showIcon] {
        iconFile.clear();
        showIcon();
    });
    QHBoxLayout* const iconRow = new QHBoxLayout;
    iconRow->addWidget(icon, 1);
    iconRow->addWidget(noIcon);
    form->addRow(tr("Icon"), iconRow);

    // Its shortcut, and the script it runs.
    QKeySequenceEdit* const shortcut = new QKeySequenceEdit(QKeySequence(details.value(QStringLiteral("shortcut")).toString()), &dialog);
    shortcut->setObjectName(QStringLiteral("PresetShortcut"));
    form->addRow(tr("Shortcut"), shortcut);
    QComboBox* const script = new QComboBox(&dialog);
    script->setObjectName(QStringLiteral("PresetScript"));
    script->addItem(tr("(none)"), QString());
    if (ScriptView* const scripts = findChild<ScriptView*>(QStringLiteral("ScriptView")))
        for (QString const& one : scripts->names())
            script->addItem(one, one);
    script->setCurrentIndex(std::max(0, script->findData(details.value(QStringLiteral("script")).toString())));
    script->setToolTip(tr("A script of the Script Editor run when the preset is activated."));
    form->addRow(tr("Script..."), script);

    // Its texture.
    int const texture = details.value(QStringLiteral("texture")).toInt();
    QCheckBox* const setTexture = new QCheckBox(texture != 0 ? tr("Texture: %1").arg(PencilTexture::name(texture)) : tr("Texture: none"), &dialog);
    setTexture->setObjectName(QStringLiteral("PresetSetTexture"));
    setTexture->setChecked(details.value(QStringLiteral("setTexture")).toBool());
    setTexture->setToolTip(tr("The paper texture the preset was made with, taken up with it."));
    form->addRow(setTexture);

    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    details[QStringLiteral("setTool")] = setTool->isChecked();
    details[QStringLiteral("tool")] = tool->currentData().toInt();
    details[QStringLiteral("setColour")] = setColour->isChecked() && details.value(QStringLiteral("colour")).value<QColor>().isValid();
    details[QStringLiteral("icon")] = iconFile;
    details[QStringLiteral("shortcut")] = shortcut->keySequence().toString();
    details[QStringLiteral("script")] = script->currentData().toString();
    details[QStringLiteral("setTexture")] = setTexture->isChecked() && texture != 0;
    s.setValue(detailsKey(kind, name), details);
    refreshPresetShortcuts();
    if (m_refreshToolProperties)
        m_refreshToolProperties();
}

void MainWindow::applyPresetDetails(QString const& kind, QString const& name)
{
    QVariantMap const details = settings().value(detailsKey(kind, name)).toMap();
    if (details.isEmpty())
        return;
    if (details.value(QStringLiteral("setTool")).toBool())
        for (auto const& [id, command] : toolsOf(kind))
            if (id == details.value(QStringLiteral("tool")).toInt() && m_canvas->tool() != id)
                m_actions->trigger(command);
    if (details.value(QStringLiteral("setColour")).toBool() && m_palette) {
        if (m_document->scene().findSwatch(details.value(QStringLiteral("swatch")).toInt()))
            m_palette->setCurrentSwatch(details.value(QStringLiteral("swatch")).toInt());
        else
            statusBar()->showMessage(tr("The colour of the preset \"%1\" is gone from the palettes (the presets' view menu: Recover Lost Colours).").arg(name), 6000);
    }
    if (details.value(QStringLiteral("setTexture")).toBool()) {
        int const texture = details.value(QStringLiteral("texture")).toInt();
        if (kind == QLatin1String("brush"))
            m_canvas->setBrushTexture(texture);
        else if (kind == QLatin1String("pencil"))
            m_canvas->setLineTexture(texture);
        if (m_refreshToolProperties)
            m_refreshToolProperties();
    }
    if (QString const script = details.value(QStringLiteral("script")).toString(); !script.isEmpty())
        runScript(script);
}

void MainWindow::refreshPresetShortcuts()
{
    for (QShortcut* const old : findChildren<QShortcut*>(QStringLiteral("PresetShortcut"), Qt::FindDirectChildrenOnly))
        delete old;
    QSettings s = settings();
    for (char const* kind : {"brush", "pencil", "eraser"}) {
        for (QString const& entry : s.value(QStringLiteral("presets/") + QLatin1String(kind)).toStringList()) {
            QString const name = entry.section(QLatin1Char('|'), 0, 0);
            QKeySequence const key(s.value(detailsKey(QLatin1String(kind), name)).toMap().value(QStringLiteral("shortcut")).toString());
            if (key.isEmpty())
                continue;
            QShortcut* const shortcut = new QShortcut(key, this);
            shortcut->setObjectName(QStringLiteral("PresetShortcut"));
            QString const which = QLatin1String(kind);
            connect(shortcut, &QShortcut::activated, this, [this, which, name] {
                // The tool the preset is for, then the preset itself, as if clicked in Tool Properties.
                if (QString(which) != presetKind())
                    m_actions->trigger(toolsOf(which).first().second);
                if (m_refreshToolProperties)
                    m_refreshToolProperties();
                if (QListWidget* const list = findChild<QListWidget*>(QStringLiteral("Presets")))
                    for (int row = 0; row < list->count(); ++row)
                        if (list->item(row)->data(Qt::UserRole + 7).toString() == name) {
                            list->setCurrentRow(row);
                            emit list->itemClicked(list->item(row));
                            statusBar()->showMessage(tr("Preset: %1").arg(name), 2000);
                            return;
                        }
            });
        }
    }
}

int MainWindow::onActionRecoverLostColours()
{
    QSettings s = settings();
    int recovered = 0;
    QString const group = QStringLiteral("presetDetails");
    s.beginGroup(group);
    QStringList const kinds = s.childGroups();
    s.endGroup();
    m_document->beginMacro(tr("Recover Lost Colours"));
    for (QString const& kind : kinds) {
        s.beginGroup(group + QLatin1Char('/') + kind);
        QStringList const names = s.childKeys();
        s.endGroup();
        for (QString const& name : names) {
            QVariantMap details = s.value(detailsKey(kind, name)).toMap();
            QColor const colour = details.value(QStringLiteral("colour")).value<QColor>();
            if (!colour.isValid() || m_document->scene().findSwatch(details.value(QStringLiteral("swatch")).toInt()))
                continue;
            // The colour put back in the scene's palette, and the preset pointed at it.
            QString const called = details.value(QStringLiteral("colourName")).toString();
            int const id = m_document->addSwatch(called.isEmpty() ? tr("Recovered") : called, colour, 0);
            details[QStringLiteral("swatch")] = id;
            s.setValue(detailsKey(kind, name), details);
            ++recovered;
        }
    }
    m_document->endMacro();
    statusBar()->showMessage(recovered ? tr("%n lost colour(s) of the presets put back in the palette.", nullptr, recovered) : tr("No preset's colour is lost."), 6000);
    return recovered;
}
