// Eclipse - questions asked the same way everywhere (see Ask.h).
#include "ui/io/Ask.h"

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>

namespace Ask {

namespace {

QSettings settings() { return QSettings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse")); }

bool hasScreen()
{
    return QApplication::platformName() != QLatin1String("offscreen") && QApplication::platformName() != QLatin1String("minimal");
}

}  // namespace

int kept(QString const& key)
{
    if (key.isEmpty())
        return -1;
    QVariant const v = settings().value(QStringLiteral("ask/") + key);
    return v.isValid() ? int(v.toBool()) : -1;
}

void keep(QString const& key, bool answer)
{
    settings().setValue(QStringLiteral("ask/") + key, answer);
}

QStringList keptQuestions()
{
    QSettings s = settings();
    s.beginGroup(QStringLiteral("ask"));
    return s.childKeys();
}

void askAgain()
{
    settings().remove(QStringLiteral("ask"));
}

bool confirm(QWidget* parent, QString const& key, QString const& title, QString const& text, bool fallback)
{
    if (int const answer = kept(key); answer >= 0)
        return answer == 1;
    if (!hasScreen())
        return fallback;
    QMessageBox box(QMessageBox::Question, title, text, QMessageBox::Yes | QMessageBox::No, parent);
    box.setDefaultButton(fallback ? QMessageBox::Yes : QMessageBox::No);
    QCheckBox* never = nullptr;
    if (!key.isEmpty()) {
        never = new QCheckBox(QObject::tr("Never Ask Again"), &box);
        never->setToolTip(QObject::tr("The answer is kept; Preferences > General asks every such question again."));
        box.setCheckBox(never);
    }
    bool const yes = box.exec() == QMessageBox::Yes;
    if (never && never->isChecked())
        keep(key, yes);
    return yes;
}

Overwrite::Answer Overwrite::ask(QString const& file)
{
    if (m_all >= 0)
        return Answer(m_all);
    QString pressed;
    if (!m_answers.isEmpty()) {
        pressed = m_answers.takeFirst();
    } else if (!hasScreen()) {
        return Replace;
    } else {
        QMessageBox box(QMessageBox::Question, m_title.isEmpty() ? QObject::tr("Confirm Overwrite") : m_title,
                        QObject::tr("%1 is there already. Overwrite it?").arg(QDir::toNativeSeparators(file)), QMessageBox::NoButton, m_parent);
        QPushButton* const one = box.addButton(QObject::tr("Overwrite"), QMessageBox::YesRole);
        QPushButton* const all = box.addButton(QObject::tr("Overwrite All"), QMessageBox::YesRole);
        QPushButton* const skip = box.addButton(QObject::tr("Skip"), QMessageBox::NoRole);
        QPushButton* const skipAll = box.addButton(QObject::tr("Skip All"), QMessageBox::NoRole);
        box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(one);
        box.exec();
        QAbstractButton* const b = box.clickedButton();
        pressed = b == one ? QStringLiteral("overwrite") : b == all ? QStringLiteral("overwriteAll") : b == skip ? QStringLiteral("skip")
                : b == skipAll ? QStringLiteral("skipAll") : QStringLiteral("cancel");
    }
    ++m_asked;
    if (pressed == QLatin1String("overwriteAll"))
        m_all = Replace;
    else if (pressed == QLatin1String("skipAll"))
        m_all = Skip;
    if (pressed.startsWith(QLatin1String("overwrite")))
        return Replace;
    if (pressed.startsWith(QLatin1String("skip")))
        return Skip;
    return Cancel;
}

}  // namespace Ask
