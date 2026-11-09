#include "board/ui/BoardCaptions.h"
#include "board/model/BoardDocument.h"
#include "actions/theme/AppSettings.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

BoardCaptions::BoardCaptions(BoardDocument* document, QWidget* parent)
    : QWidget(parent), m_document(document), m_sceneField(new QLineEdit(this)), m_panelField(new QLineEdit(this)),
      m_duration(new QSpinBox(this)), m_durationHint(new QLabel(this))
{
    if (settings().value(QStringLiteral("prefs/darkStyle"), true).toBool())
        setStyleSheet(QStringLiteral("QPlainTextEdit#BoardCaptionEditor { background: #181818; border: 1px solid #505050; }"));
    m_sceneField->setReadOnly(true);
    m_panelField->setReadOnly(true);
    m_duration->setObjectName(QStringLiteral("BoardPanelDurationFrames"));
    m_duration->setRange(1, 1000000);
    m_duration->setKeyboardTracking(false);
    m_duration->setSuffix(tr(" frames"));
    auto* durationRow = new QHBoxLayout;
    durationRow->addWidget(m_duration);
    durationRow->addWidget(m_durationHint, 1);
    auto* scroll = new QScrollArea(this);
    auto* fields = new QWidget(scroll);
    m_fieldsLayout = new QVBoxLayout(fields);
    m_fieldsLayout->setContentsMargins(0, 0, 4, 0);
    scroll->setWidget(fields);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->setSpacing(4);
    for (auto const& [text, field] : {qMakePair(tr("Scene"), static_cast<QWidget*>(m_sceneField)),
                                     qMakePair(tr("Panel"), static_cast<QWidget*>(m_panelField)),
                                     qMakePair(tr("Duration"), static_cast<QWidget*>(m_duration))}) {
        auto* label = new QLabel(text, this);
        label->setBuddy(field);
        layout->addWidget(label);
        if (field != m_duration)
            layout->addWidget(field);
        else
            layout->addLayout(durationRow);
    }
    layout->addSpacing(8);
    layout->addWidget(scroll, 1);
    connect(m_duration, &QSpinBox::editingFinished, this, [this] {
        if (!m_document->setPanelLength(m_shot, m_panel, m_duration->value()))
            if (auto const* current = m_document->panel(m_shot, m_panel))
                m_duration->setValue(current->length);
    });
    showPanel(-1, -1);
}

void BoardCaptions::showPanel(int shot, int panel)
{
    m_editing = nullptr;
    m_shot = shot;
    m_panel = panel;
    auto const* current = m_document->panel(shot, panel);
    auto const& project = m_document->project();
    QStringList fields = project.captionFields;
    qsizetype const dialog = fields.indexOf(QStringLiteral("Dialog"));
    if (dialog > 0)
        fields.move(dialog, 0);
    if (m_fields != fields)
        rebuildFields(fields);
    m_sceneField->setText(current ? project.shots[shot].name : QString());
    m_panelField->setText(current ? tr("%1 / %2").arg(panel + 1).arg(project.shots[shot].panels.size()) : QString());
    m_duration->setEnabled(current != nullptr);
    m_duration->setValue(current ? current->length : 1);
    m_durationHint->setText(current ? tr("%1 s").arg(locale().toString(current->length / project.fps, 'f', 2)) : QString());
    for (qsizetype i = 0; i < m_fields.size(); ++i) {
        QString text;
        if (current)
            for (auto const& caption : current->captions)
                if (caption.field.compare(m_fields[i], Qt::CaseInsensitive) == 0) {
                    text = caption.text;
                    break;
                }
        auto* editor = m_editors[i];
        QSignalBlocker const block(editor);
        editor->setEnabled(current != nullptr);
        if (editor->toPlainText() != text)
            editor->setPlainText(text);
    }
}

void BoardCaptions::rebuildFields(QStringList const& fields)
{
    m_editing = nullptr;
    while (auto* item = m_fieldsLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    m_editors.clear();
    m_fields = fields;
    for (auto const& field : m_fields) {
        auto* label = new QLabel(field, this);
        label->setTextFormat(Qt::PlainText);
        auto* editor = new QPlainTextEdit(this);
        editor->setObjectName(QStringLiteral("BoardCaptionEditor"));
        editor->setAccessibleName(field);
        editor->setUndoRedoEnabled(false);
        editor->setTabChangesFocus(true);
        editor->setMinimumHeight(66);
        editor->installEventFilter(this);
        label->setBuddy(editor);
        m_fieldsLayout->addWidget(label);
        m_fieldsLayout->addWidget(editor, 1);
        m_editors.push_back(editor);
        connect(editor, &QPlainTextEdit::textChanged, this, [this, editor, field] { storeText(editor, field); });
    }
}

bool BoardCaptions::eventFilter(QObject* object, QEvent* event)
{
    if (event->type() == QEvent::FocusOut && object == m_editing)
        m_editing = nullptr;
    if (event->type() == QEvent::ShortcutOverride) {
        auto const* key = static_cast<QKeyEvent const*>(event);
        if (key->matches(QKeySequence::Undo) || key->matches(QKeySequence::Redo)
            || key->keyCombination() == QKeyCombination(Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_Z)) {
            event->ignore();
            return true;
        }
    }
    return QWidget::eventFilter(object, event);
}

void BoardCaptions::storeText(QPlainTextEdit* editor, QString const& field)
{
    if (editor != m_editing) {
        m_editing = editor;
        ++m_session;
    }
    QScopedValueRollback<bool> const writing(m_writing, true);
    m_document->setCaption(m_shot, m_panel, field, editor->toPlainText(), m_session);
}

void BoardCaptions::editFields(bool add)
{
    if (!m_document->panel(m_shot, m_panel))
        return;
    bool accepted = false;
    if (add) {
        QString const field = QInputDialog::getText(this, tr("Add Caption Field"), tr("Field name"),
                                                   QLineEdit::Normal, {}, &accepted);
        if (accepted && !m_document->addCaptionField(field))
            QMessageBox::information(this, tr("Add Caption Field"), tr("Enter a unique name of 1–128 characters. A project can have up to 128 fields."));
        return;
    }
    QStringList fields = m_fields;
    auto const defaults = Storyboard::defaultCaptionFields();
    fields.removeIf([&](QString const& name) { return defaults.contains(name, Qt::CaseInsensitive); });
    if (fields.isEmpty()) {
        QMessageBox::information(this, tr("Remove Caption Field"), tr("There are no custom caption fields."));
        return;
    }
    QString const field = QInputDialog::getItem(this, tr("Remove Caption Field"), tr("Field name"), fields, 0, false, &accepted);
    if (accepted && QMessageBox::question(this, tr("Remove Caption Field"),
            tr("Remove “%1” and its text from every panel?").arg(field)) == QMessageBox::Yes)
        m_document->removeCaptionField(field);
}
