#include "board/ui/BoardWindow.h"

#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>

void BoardWindow::offerProjectRecovery(QString const& path)
{
    QMessageBox prompt(QMessageBox::Question, tr("Recover Project"),
        tr("Eclipse found a recovery copy with unsaved changes. Open those changes or use the last saved project?"),
        QMessageBox::NoButton, this);
    prompt.setObjectName(QStringLiteral("BoardRecoveryPrompt"));
    auto* recover = prompt.addButton(tr("Recover Changes"), QMessageBox::AcceptRole);
    auto* saved = prompt.addButton(tr("Open Saved Project"), QMessageBox::RejectRole);
    prompt.setDefaultButton(recover);
    prompt.setEscapeButton(saved);
    prompt.exec();
    if (m_document.path() != path)
        return;
    if (prompt.clickedButton() != recover) {
        offerLegacyConversion(path);
        return;
    }
    QString error;
    if (!m_document.recover(&error)) {
        if (!error.isEmpty())
            QMessageBox::warning(this, tr("Recover Project"), error);
        return;
    }
    selectPanel(0, 0, true);
    statusBar()->showMessage(tr("Unsaved changes recovered. Save the project to keep them."), 6000);
}
