#include "board/ui/BoardWindow.h"
#include "actions/manager/ActionManager.h"
#include "launcher/BoardFormatPicker.h"

#include <QButtonGroup>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QRadioButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

namespace {
QRadioButton* choice(QWidget* parent, QButtonGroup* group, char const* id, QString const& text, int value)
{
    auto* button = new QRadioButton(text, parent);
    button->setObjectName(QLatin1String(id));
    group->addButton(button, value);
    return button;
}
}

void BoardWindow::editProjectSettings()
{
    if (m_document.path().isEmpty())
        return;
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Project Settings"));
    dialog.setObjectName(QStringLiteral("BoardSettingsDialog"));
    auto const& project = m_document.project();
    auto* tabs = new QTabWidget(&dialog);
    tabs->setObjectName(QStringLiteral("BoardSettingsTabs"));

    auto* settingsTab = new QWidget(tabs);
    auto* title = new QLineEdit(project.title, settingsTab);
    auto* subtitle = new QLineEdit(project.subtitle, settingsTab);
    title->setObjectName(QStringLiteral("BoardTitle"));
    subtitle->setObjectName(QStringLiteral("BoardSubtitle"));
    title->setMaxLength(4096);
    subtitle->setMaxLength(4096);
    auto* settingsForm = new QFormLayout(settingsTab);
    settingsForm->addRow(tr("Title:"), title);
    settingsForm->addRow(tr("Subtitle:"), subtitle);
    tabs->addTab(settingsTab, tr("Settings"));

    auto* formatTab = new QWidget(tabs);
    auto* picker = new BoardFormatPicker(ActionManager::defaultDirectory(), formatTab);
    picker->setFormat(project.width, project.height, project.fps);
    auto* formatForm = new QFormLayout;
    formatForm->addRow(tr("Camera Size:"), picker->presetRow());
    formatForm->addRow(tr("Width:"), picker->width());
    formatForm->addRow(tr("Height:"), picker->height());
    formatForm->addRow(tr("Aspect Ratio:"), picker->aspect());
    formatForm->addRow(tr("Frame Rate:"), picker->fps());

    auto* artBox = new QGroupBox(tr("Existing Artwork"), formatTab);
    auto* artGroup = new QButtonGroup(artBox);
    auto* keepSize = choice(artBox, artGroup, "BoardArtKeepSize", tr("Keep its size, centred in the new frame"), int(BoardFormat::Art::KeepSize));
    auto* scaleArt = choice(artBox, artGroup, "BoardArtScale", tr("Scale it and camera moves to fit the new frame"), int(BoardFormat::Art::ScaleToFit));
    keepSize->setChecked(true);
    auto* artLayout = new QVBoxLayout(artBox);
    artLayout->addWidget(keepSize);
    artLayout->addWidget(scaleArt);

    auto* timingBox = new QGroupBox(tr("Existing Timing"), formatTab);
    auto* timingGroup = new QButtonGroup(timingBox);
    auto* keepDuration = choice(timingBox, timingGroup, "BoardKeepDuration",
        tr("Keep durations: re-time panels, dissolves and camera keys"), int(BoardFormat::Timing::KeepDuration));
    auto* keepFrames = choice(timingBox, timingGroup, "BoardKeepFrames",
        tr("Keep frame counts: panels play faster or slower"), int(BoardFormat::Timing::KeepFrames));
    keepDuration->setChecked(true);
    auto* timingLayout = new QVBoxLayout(timingBox);
    timingLayout->addWidget(keepDuration);
    timingLayout->addWidget(keepFrames);
    auto* soundNote = new QLabel(tr("The sound always keeps its real length and stays in sync with its starting frame."), timingBox);
    soundNote->setWordWrap(true);
    timingLayout->addWidget(soundNote);

    auto* summary = new QLabel(formatTab);
    summary->setObjectName(QStringLiteral("BoardFormatSummary"));
    summary->setWordWrap(true);
    auto* formatLayout = new QVBoxLayout(formatTab);
    formatLayout->addLayout(formatForm);
    formatLayout->addWidget(artBox);
    formatLayout->addWidget(timingBox);
    formatLayout->addWidget(summary);
    formatLayout->addStretch(1);
    tabs->addTab(formatTab, tr("Project Resolution"));

    auto format = [&] {
        return BoardFormat{picker->width()->value(), picker->height()->value(), picker->fps()->value(),
                           BoardFormat::Art(artGroup->checkedId()), BoardFormat::Timing(timingGroup->checkedId())};
    };
    auto update = [&] {
        BoardFormat const next = format();
        bool const resized = next.width != project.width || next.height != project.height;
        bool const retimed = next.fps != project.fps;
        artBox->setEnabled(resized);
        timingBox->setEnabled(retimed);
        QStringList lines;
        if (resized)
            lines << tr("Frame: %1 x %2 becomes %3 x %4.").arg(project.width).arg(project.height).arg(next.width).arg(next.height);
        if (retimed) {
            BoardProject preview = project;
            if (preview.reformat(next))
                lines << tr("Board length: %1 frames becomes %2 frames.").arg(project.timeline().length()).arg(preview.timeline().length());
        }
        summary->setText(lines.isEmpty() ? tr("The frame and frame rate are unchanged.") : lines.join(QLatin1Char('\n')));
    };
    connect(picker, &BoardFormatPicker::changed, &dialog, update);
    connect(artGroup, &QButtonGroup::idClicked, &dialog, update);
    connect(timingGroup, &QButtonGroup::idClicked, &dialog, update);
    update();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(tabs);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        BoardProject check = project;
        QString error;
        if (!check.reformat(format(), &error)) {
            tabs->setCurrentIndex(1);
            QMessageBox::warning(&dialog, tr("Project Settings"), error);
            return;
        }
        dialog.accept();
    });
    if (dialog.exec() != QDialog::Accepted)
        return;
    BoardFormat const next = format();
    bool const titled = title->text() != project.title || subtitle->text() != project.subtitle;
    bool const reformatted = next.width != project.width || next.height != project.height || next.fps != project.fps;
    if (!titled && !reformatted)
        return;
    m_document.undoStack()->beginMacro(tr("Project Settings"));
    m_document.setTitles(title->text(), subtitle->text());
    m_document.setFormat(next);
    m_document.undoStack()->endMacro();
}
