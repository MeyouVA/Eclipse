#include "board/ui/BoardWindow.h"
#include "board/ui/BoardPlayback.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>

void BoardWindow::editSceneDetails(QString const& operation)
{
    int const scene = m_selected.first;
    if (!m_document.panel(scene, 0))
        return;
    m_playback->stop();
    auto const& shot = m_document.project().shots[scene];
    if (operation != QLatin1String("scene")) {
        bool const sequence = operation == QLatin1String("sequence");
        QString const current = sequence ? shot.sequence : shot.act;
        if (current.trimmed().isEmpty())
            return;
        QString const title = sequence ? tr("Rename Sequence") : tr("Rename Act");
        bool accepted = false;
        QString const name = QInputDialog::getText(this, title, tr("Name for this contiguous group:"),
            QLineEdit::Normal, current, &accepted);
        if (accepted)
            m_document.renameSceneGroup(scene, sequence ? BoardProject::SceneGroup::Sequence : BoardProject::SceneGroup::Act, name);
        return;
    }

    QString const oldName = shot.name;
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("BoardSceneDetailsDialog"));
    dialog.setWindowTitle(tr("Edit Scene Details"));
    auto* layout = new QFormLayout(&dialog);
    auto* name = new QLineEdit(shot.name, &dialog);
    auto* sequence = new QLineEdit(shot.sequence, &dialog);
    auto* act = new QLineEdit(shot.act, &dialog);
    name->setObjectName(QStringLiteral("BoardSceneName"));
    sequence->setObjectName(QStringLiteral("BoardSequenceName"));
    act->setObjectName(QStringLiteral("BoardActName"));
    layout->addRow(tr("Scene name"), name);
    layout->addRow(tr("Sequence"), sequence);
    layout->addRow(tr("Act"), act);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    if (!m_document.setSceneDetails(scene, name->text(), sequence->text(), act->text())
        && (name->text().trimmed().isEmpty() || name->text().trimmed() != oldName.trimmed()))
        QMessageBox::warning(this, dialog.windowTitle(), tr("Enter a unique, nonempty scene name."));
}
