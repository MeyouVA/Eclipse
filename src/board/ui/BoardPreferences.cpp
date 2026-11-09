#include "board/ui/BoardWindow.h"
#include "actions/dialogs/ShortcutsDialog.h"
#include "actions/manager/ActionManager.h"
#include "actions/theme/AppSettings.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QSpinBox>
#include <cmath>

void BoardWindow::editPreferences()
{
    QSettings prefs = settings();
    prefs.beginGroup(QStringLiteral("board/prefs"));
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("BoardPreferencesDialog"));
    dialog.setWindowTitle(tr("Storyboard Preferences"));
    auto* layout = new QFormLayout(&dialog);
    auto* frames = new QSpinBox(&dialog);
    frames->setObjectName(QStringLiteral("BoardDefaultPanelFrames"));
    frames->setRange(0, 1000000);
    frames->setSpecialValueText(tr("One second"));
    frames->setSuffix(tr(" frames"));
    frames->setValue(prefs.value(QStringLiteral("panelFrames"), 0).toInt());
    frames->setToolTip(tr("Duration of new panels and scenes added to an open board."));
    layout->addRow(tr("New panel duration"), frames);
    auto* guides = new QCheckBox(tr("Show the camera frame when Storyboard opens"), &dialog);
    guides->setObjectName(QStringLiteral("BoardDefaultCameraGuide"));
    guides->setChecked(prefs.value(QStringLiteral("cameraGuide"), true).toBool());
    layout->addRow(guides);
    auto* loop = new QCheckBox(tr("Loop playback when Storyboard opens"), &dialog);
    loop->setObjectName(QStringLiteral("BoardDefaultLoop"));
    loop->setChecked(prefs.value(QStringLiteral("loop"), false).toBool());
    layout->addRow(loop);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    prefs.setValue(QStringLiteral("panelFrames"), frames->value());
    prefs.setValue(QStringLiteral("cameraGuide"), guides->isChecked());
    prefs.setValue(QStringLiteral("loop"), loop->isChecked());
    applyPreferences();
}

void BoardWindow::applyPreferences()
{
    QSettings prefs = settings();
    prefs.beginGroup(QStringLiteral("board/prefs"));
    bool const guide = prefs.value(QStringLiteral("cameraGuide"), true).toBool();
    bool const loop = prefs.value(QStringLiteral("loop"), false).toBool();
    m_actions->setChecked(QStringLiteral("BoardCameraGuide"), guide);
    m_actions->setChecked(QStringLiteral("BoardLoop"), loop);
    showCameraGuide(guide);
    loopPlayback(loop);
}

int BoardWindow::defaultPanelFrames(double fps) const
{
    int const frames = std::clamp(settings().value(QStringLiteral("board/prefs/panelFrames"), 0).toInt(), 0, 1000000);
    return frames > 0 ? frames : int(std::lround(fps));
}

void BoardWindow::editShortcuts()
{
    ShortcutsDialog dialog(m_actions, this);
    dialog.setObjectName(QStringLiteral("BoardShortcutsDialog"));
    dialog.exec();
}
