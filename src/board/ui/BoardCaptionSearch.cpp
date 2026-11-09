#include "board/ui/BoardWindow.h"
#include "board/ui/BoardCaptions.h"
#include "board/ui/BoardPlayback.h"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>

namespace {
constexpr int maxMatches = 2000;
}

void BoardWindow::findInCaptions()
{
    if (auto* existing = findChild<QDialog*>(QStringLiteral("BoardFindCaptionDialog"))) {
        existing->show();
        existing->raise();
        existing->activateWindow();
        return;
    }
    if (m_document.path().isEmpty())
        return;
    auto* dialog = new QDialog(this);
    dialog->setObjectName(QStringLiteral("BoardFindCaptionDialog"));
    dialog->setWindowTitle(tr("Find in Captions"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->resize(620, 440);
    auto* layout = new QFormLayout(dialog);
    auto* query = new QLineEdit(dialog);
    query->setObjectName(QStringLiteral("BoardCaptionQuery"));
    auto* replacement = new QLineEdit(dialog);
    replacement->setObjectName(QStringLiteral("BoardCaptionReplacement"));
    auto* sensitive = new QCheckBox(tr("Match case"), dialog);
    sensitive->setObjectName(QStringLiteral("BoardCaptionMatchCase"));
    auto* results = new QListWidget(dialog);
    results->setObjectName(QStringLiteral("BoardCaptionMatches"));
    auto* status = new QLabel(dialog);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    auto* find = buttons->addButton(tr("Find"), QDialogButtonBox::ActionRole);
    find->setObjectName(QStringLiteral("BoardFindCaptionButton"));
    auto* replace = buttons->addButton(tr("Replace All"), QDialogButtonBox::ActionRole);
    replace->setObjectName(QStringLiteral("BoardReplaceCaptionButton"));
    layout->addRow(tr("Find"), query);
    layout->addRow(tr("Replace with"), replacement);
    layout->addRow(sensitive);
    layout->addRow(results);
    layout->addRow(status);
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);

    auto sensitivity = [sensitive] { return sensitive->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive; };
    auto clear = [this, results, status, query, find, replace] {
        results->clear();
        m_captions->clearMatchHighlight();
        status->clear();
        bool const enabled = !m_document.path().isEmpty() && !query->text().isEmpty();
        find->setEnabled(enabled);
        replace->setEnabled(enabled);
    };
    connect(query, &QLineEdit::textChanged, dialog, clear);
    connect(sensitive, &QCheckBox::toggled, dialog, clear);
    connect(&m_document, &BoardDocument::projectChanged, dialog, clear);
    clear();
    auto search = [this, query, results, status, sensitivity] {
        results->clear();
        m_captions->clearMatchHighlight();
        QString const needle = query->text();
        if (needle.isEmpty())
            return;
        auto const mode = sensitivity();
        auto const& project = m_document.project();
        for (int s = 0; s < project.shots.size(); ++s) {
            auto const& shot = project.shots[s];
            for (int p = 0; p < shot.panels.size(); ++p) {
                auto const& panel = shot.panels[p];
                QString const name = panel.name.trimmed().isEmpty() ? QString::number(p + 1) : panel.name.trimmed();
                for (auto const& caption : panel.captions) {
                    QString text = caption.text;
                    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
                    text.replace(u'\r', u'\n');
                    for (qsizetype at = text.indexOf(needle, 0, mode); at >= 0;
                         at = text.indexOf(needle, at + needle.size(), mode)) {
                        if (results->count() == maxMatches) {
                            status->setText(tr("Showing the first %1 matches.").arg(maxMatches));
                            return;
                        }
                        qsizetype const from = std::max(qsizetype(0), at - 18);
                        QString const excerpt = text.mid(from, 78).simplified();
                        auto* item = new QListWidgetItem(tr("%1 · %2 · %3 — %4").arg(shot.name, name, caption.field, excerpt), results);
                        item->setData(Qt::UserRole, QVariantList{s, p, caption.field, int(at), int(needle.size())});
                    }
                }
            }
        }
        status->setText(results->count() == 0 ? tr("No matches.") : tr("%n match(es).", nullptr, results->count()));
    };
    connect(find, &QPushButton::clicked, dialog, search);
    connect(query, &QLineEdit::returnPressed, dialog, search);
    connect(results, &QListWidget::currentItemChanged, dialog, [this](QListWidgetItem* item) {
        if (!item)
            return;
        auto const values = item->data(Qt::UserRole).toList();
        int const scene = values[0].toInt(), panel = values[1].toInt();
        m_playback->stop();
        showBoardView(QStringLiteral("captions"));
        if (m_selected != qMakePair(scene, panel))
            selectPanel(scene, panel);
        m_captions->highlightMatch(values[2].toString(), values[3].toInt(), values[4].toInt());
    });
    connect(replace, &QPushButton::clicked, dialog, [this, dialog, query, replacement, status, sensitivity] {
        QString error;
        int const changed = m_document.replaceCaptionText(query->text(), replacement->text(), sensitivity(), &error);
        if (changed < 0)
            QMessageBox::warning(dialog, dialog->windowTitle(), error);
        else
            status->setText(changed == 0 ? tr("No captions changed.") : tr("Updated %n caption(s).", nullptr, changed));
    });
    dialog->show();
    query->setFocus();
}
