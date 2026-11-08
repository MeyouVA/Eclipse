#include "board/ui/BoardWindow.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>

void BoardWindow::editProjectSettings()
{
    if (m_document.path().isEmpty())
        return;
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Project Settings"));
    dialog.setObjectName(QStringLiteral("BoardSettingsDialog"));
    auto const& project = m_document.project();
    auto* title = new QLineEdit(project.title, &dialog);
    auto* subtitle = new QLineEdit(project.subtitle, &dialog);
    title->setObjectName(QStringLiteral("BoardTitle"));
    subtitle->setObjectName(QStringLiteral("BoardSubtitle"));
    title->setMaxLength(4096);
    subtitle->setMaxLength(4096);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    auto* layout = new QFormLayout(&dialog);
    layout->addRow(tr("Title:"), title);
    layout->addRow(tr("Subtitle:"), subtitle);
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted)
        m_document.setTitles(title->text(), subtitle->text());
}
