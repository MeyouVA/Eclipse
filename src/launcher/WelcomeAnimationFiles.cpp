#include "launcher/WelcomeDialog.h"
#include "actions/theme/AppSettings.h"

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>

void WelcomeDialog::browse()
{
    QString const dir = QFileDialog::getExistingDirectory(this, tr("New Scene Directory"), m_location->currentText());
    if (!dir.isEmpty())
        m_location->setCurrentText(QDir::toNativeSeparators(dir));
}

void WelcomeDialog::create()
{
    // A scene is a folder of its own name, with the scene file inside it.
    QString const name = m_name->text().trimmed();
    static QString const kForbidden = QStringLiteral("\\/:*?\"<>|");
    bool bad = name.isEmpty() || name.endsWith(QLatin1Char('.'));
    for (QChar c : name)
        bad = bad || kForbidden.contains(c);
    if (bad) {
        QMessageBox::warning(this, tr("Eclipse"), tr("Give the scene a name, without any of these: %1").arg(kForbidden));
        return;
    }
    QString const location = m_location->currentText().trimmed();
    if (location.isEmpty() || !QDir(location).exists()) {
        QMessageBox::warning(this, tr("Eclipse"), tr("The location does not exist:\n%1").arg(location));
        return;
    }
    QDir const folder(QDir(location).filePath(name));
    if (folder.exists()) {
        QMessageBox::warning(this, tr("Eclipse"),
                             tr("There is already something called %1 there. Choose another name or another location.").arg(name));
        return;
    }
    // Remembered for next time: where scenes go, and how big they are.
    QSettings s = settings();
    QStringList places = s.value(QStringLiteral("welcome/locations")).toStringList();
    QString const native = QDir::toNativeSeparators(QDir(location).absolutePath());
    places.removeAll(native);
    places.prepend(native);
    while (places.size() > 8)
        places.removeLast();
    s.setValue(QStringLiteral("welcome/locations"), places);
    s.setValue(QStringLiteral("welcome/resolution"), resolution().name);

    m_path = folder.filePath(name + QStringLiteral(".ecl"));
    m_choice = CreateScene;
    accept();
}

void WelcomeDialog::openOther()
{
    QString const path = QFileDialog::getOpenFileName(this, tr("Open Scene"), m_location->currentText(), QStringLiteral("Eclipse Scene (*.ecl)"));
    if (path.isEmpty())
        return;
    m_path = path;
    m_choice = OpenScene;
    accept();
}

void WelcomeDialog::openRecent(int row)
{
    QListWidgetItem* const item = m_recent->item(row);
    QString const path = item ? item->data(Qt::UserRole).toString() : QString();
    if (path.isEmpty())
        return;
    m_path = path;
    m_choice = OpenScene;
    accept();
}
