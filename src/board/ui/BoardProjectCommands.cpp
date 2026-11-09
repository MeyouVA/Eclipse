#include "board/ui/BoardWindow.h"
#include "board/ui/BoardPlayback.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>

void BoardWindow::transferProject(QString const& operation)
{
    if (m_document.path().isEmpty() || !m_document.panel(m_selected.first, m_selected.second))
        return;
    m_playback->stop();
    if (operation == QLatin1String("extract") || operation == QLatin1String("split"))
        extractScenes(operation == QLatin1String("split"));
    else
        insertScenes(operation == QLatin1String("merge"));
}

void BoardWindow::extractScenes(bool split)
{
    int const scenes = int(m_document.project().shots.size());
    if (split && scenes < 2)
        return;
    QString const title = split ? tr("Split Project") : tr("Extract Scenes to Project");
    QDialog dialog(this);
    dialog.setObjectName(split ? QStringLiteral("BoardSplitDialog") : QStringLiteral("BoardExtractDialog"));
    dialog.setWindowTitle(title);
    auto* form = new QFormLayout(&dialog);
    auto* first = new QSpinBox(&dialog);
    first->setObjectName(split ? QStringLiteral("BoardSplitBoundary") : QStringLiteral("BoardExtractFirst"));
    int const current = m_selected.first + 1;
    first->setRange(split ? 2 : 1, scenes);
    first->setValue(current);
    form->addRow(split ? tr("First scene in Part 2") : tr("First scene"), first);
    QSpinBox* last = nullptr;
    if (!split) {
        last = new QSpinBox(&dialog);
        last->setObjectName(QStringLiteral("BoardExtractLast"));
        last->setRange(current, scenes);
        last->setValue(current);
        connect(first, &QSpinBox::valueChanged, last, &QSpinBox::setMinimum);
        form->addRow(tr("Last scene"), last);
    }
    auto* name = new QLineEdit(m_document.project().name.left(100) + (split ? tr(" Split") : tr(" Extract")), &dialog);
    name->setObjectName(split ? QStringLiteral("BoardSplitName") : QStringLiteral("BoardExtractName"));
    name->setMaxLength(120);
    form->addRow(split ? tr("Output folder name") : tr("Project name"), name);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    QString const directory = QFileDialog::getExistingDirectory(this, title, QFileInfo(m_document.path()).absolutePath());
    if (directory.isEmpty())
        return;
    QString error;
    QString const output = split ? m_document.splitProject(first->value() - 1, name->text().trimmed(), directory, &error)
        : m_document.extractProject(first->value() - 1, last->value() - 1, name->text().trimmed(), directory, &error);
    if (output.isEmpty())
        QMessageBox::warning(this, title, error);
    else
        statusBar()->showMessage((split ? tr("Saved both projects to %1") : tr("Extracted project to %1"))
            .arg(QDir::toNativeSeparators(output)), 6000);
}

void BoardWindow::insertScenes(bool append)
{
    QString const title = append ? tr("Merge Project") : tr("Insert Project");
    QString const file = QFileDialog::getOpenFileName(this, title, QFileInfo(m_document.path()).absolutePath(),
        tr("Eclipse Storyboard (*.ecb)"));
    if (file.isEmpty())
        return;
    QString error;
    BoardProject source;
    if (!BoardProject::open(file, source, &error)) {
        QMessageBox::warning(this, title, error);
        return;
    }
    bool incomingSound = false;
    if (!source.sound.asset.isEmpty() && !m_document.project().sound.asset.isEmpty()) {
        QMessageBox question(QMessageBox::Question, title,
            tr("Both projects have sound. Choose the sound track for the combined board."), QMessageBox::Cancel, this);
        auto* current = question.addButton(tr("Keep Current Sound"), QMessageBox::AcceptRole);
        auto* incoming = question.addButton(tr("Use Imported Sound"), QMessageBox::AcceptRole);
        question.setDefaultButton(current);
        question.exec();
        if (question.clickedButton() != current && question.clickedButton() != incoming)
            return;
        incomingSound = question.clickedButton() == incoming;
    }
    int const after = append ? int(m_document.project().shots.size()) - 1 : m_selected.first;
    if (m_document.insertProject(std::move(source), file, after, incomingSound, &error))
        selectPanel(after + 1, 0);
    else
        QMessageBox::warning(this, title, error);
}
