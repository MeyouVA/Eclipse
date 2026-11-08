// Eclipse - the Database Manager dialog.
#include "animation/ui/io/DatabaseManager.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
constexpr int kKind = Qt::UserRole + 1;     // 0 environment, 1 job, 2 scene
}

DatabaseManager::DatabaseManager(QWidget* parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("DatabaseManager"));
    setWindowTitle(tr("Database Manager"));
    QVBoxLayout* const layout = new QVBoxLayout(this);
    QHBoxLayout* const top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("Database:"), this));
    m_root = new QLineEdit(m_db.root(), this);
    m_root->setObjectName(QStringLiteral("DatabaseRoot"));
    m_root->setToolTip(tr("The shared folder the scenes are kept in."));
    top->addWidget(m_root, 1);
    QPushButton* const browse = new QPushButton(tr("Browse..."), this);
    top->addWidget(browse);
    QPushButton* const users = new QPushButton(tr("Users..."), this);
    users->setObjectName(QStringLiteral("DatabaseUsers"));
    top->addWidget(users);
    layout->addLayout(top);
    connect(browse, &QPushButton::clicked, this, [this] {
        QString const folder = QFileDialog::getExistingDirectory(this, tr("Database Selector"), m_root->text());
        if (!folder.isEmpty())
            setRoot(folder);
    });
    connect(m_root, &QLineEdit::editingFinished, this, [this] { setRoot(m_root->text()); });
    connect(users, &QPushButton::clicked, this, &DatabaseManager::editUsers);

    QSplitter* const split = new QSplitter(this);
    m_tree = new QTreeWidget(split);
    m_tree->setObjectName(QStringLiteral("DatabaseTree"));
    m_tree->setHeaderLabels({tr("Environments, jobs and scenes"), tr("Stage")});
    QWidget* const right = new QWidget(split);
    QVBoxLayout* const side = new QVBoxLayout(right);
    side->setContentsMargins(0, 0, 0, 0);
    m_info = new QLabel(right);
    m_info->setWordWrap(true);
    side->addWidget(m_info);
    side->addWidget(new QLabel(tr("Versions"), right));
    m_versions = new QTableWidget(0, 4, right);
    m_versions->setObjectName(QStringLiteral("DatabaseVersions"));
    m_versions->setHorizontalHeaderLabels({tr("Version"), tr("User"), tr("Saved"), tr("Comment")});
    m_versions->horizontalHeader()->setStretchLastSection(true);
    m_versions->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_versions->setEditTriggers(QAbstractItemView::NoEditTriggers);
    side->addWidget(m_versions, 2);
    side->addWidget(new QLabel(tr("Rights to modify"), right));
    m_locks = new QTableWidget(0, 4, right);
    m_locks->setObjectName(QStringLiteral("DatabaseLocks"));
    m_locks->setHorizontalHeaderLabels({tr("What"), tr("User"), tr("Computer"), tr("Since")});
    m_locks->horizontalHeader()->setStretchLastSection(true);
    m_locks->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_locks->setEditTriggers(QAbstractItemView::NoEditTriggers);
    side->addWidget(m_locks, 1);
    split->setStretchFactor(1, 1);
    layout->addWidget(split, 1);

    QHBoxLayout* const buttons = new QHBoxLayout;
    auto button = [&](char const* id, QString const& text, QString const& tip) {
        QPushButton* const b = new QPushButton(text, this);
        b->setObjectName(QLatin1String(id));
        b->setToolTip(tip);
        buttons->addWidget(b);
        return b;
    };
    connect(button("DatabaseNewEnvironment", tr("New Environment..."), tr("An environment: the jobs of one production.")), &QPushButton::clicked, this, [this] { newEnvironment(); });
    connect(button("DatabaseNewJob", tr("New Job..."), tr("A job in the environment picked: its scenes.")), &QPushButton::clicked, this, [this] { newJob(); });
    connect(button("DatabaseImport", tr("Import Scene..."), tr("A scene file brought into the job picked (a new version, if the scene is there).")), &QPushButton::clicked, this,
            [this] { importInto(); });
    connect(button("DatabaseExport", tr("Export Scene..."), tr("The scene's current version, with the files it uses, into a folder.")), &QPushButton::clicked, this,
            [this] { exportPicked(); });
    connect(button("DatabaseStage", tr("Change Stage..."), tr("The stage the scene (or job) is at.")), &QPushButton::clicked, this, [this] { changeStage(); });
    QCheckBox* const approval = new QCheckBox(tr("Manual approval"), this);
    approval->setObjectName(QStringLiteral("DatabaseApproval"));
    approval->setToolTip(tr("Approval Policy: new versions of the job's scenes are kept for approval (Make Current) rather than taken as they are saved."));
    buttons->addWidget(approval);
    connect(approval, &QCheckBox::clicked, this, [this](bool on) { setApproval(on); });
    connect(button("DatabaseMakeCurrent", tr("Make Current"), tr("The version picked made the one the scene opens at.")), &QPushButton::clicked, this, [this] { makeCurrent(); });
    connect(button("DatabaseRelease", tr("Release"), tr("Your own rights to modify what is picked given up.")), &QPushButton::clicked, this, [this] { releaseLock(false); });
    connect(button("DatabaseForceRelease", tr("Force Release"), tr("Someone else's rights to modify it taken away (when they have left it held).")), &QPushButton::clicked, this,
            [this] { releaseLock(true); });
    connect(button("DatabaseDelete", tr("Delete"), tr("The scene into the database's trash; an empty job or environment removed.")), &QPushButton::clicked, this,
            [this] { deletePicked(); });
    buttons->addStretch(1);
    QPushButton* const open = button("DatabaseOpen", tr("Open"), tr("The scene opened (at the version picked), and the rights to modify it got."));
    connect(open, &QPushButton::clicked, this, [this] {
        if (scene().isEmpty())
            return;
        emit openRequested(environment(), job(), scene(), pickedVersion());
        accept();
    });
    QPushButton* const close = button("DatabaseClose", tr("Close"), QString());
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    layout->addLayout(buttons);
    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this, approval] {
        showScene();
        QSignalBlocker const quiet(approval);
        approval->setEnabled(!job().isEmpty());
        approval->setChecked(!job().isEmpty() && m_db.manualApproval(environment(), job()));
    });
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [open] { open->click(); });
    resize(980, 600);
    refresh();
}

void DatabaseManager::setRoot(QString const& root)
{
    m_db = SceneDatabase(root);
    if (m_root->text() != root)
        m_root->setText(root);
    if (!root.isEmpty())
        QSettings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse")).setValue(QStringLiteral("prefs/databaseRoot"), root);
    refresh();
}

bool DatabaseManager::report(bool ok)
{
    if (!ok && !m_error.isEmpty() && isVisible())
        QMessageBox::warning(this, tr("Database Manager"), m_error);
    refresh();
    return ok;
}

void DatabaseManager::refresh()
{
    QString const env = environment(), jb = job(), sc = scene();
    m_tree->clear();
    for (QString const& e : m_db.environments()) {
        QTreeWidgetItem* const ei = new QTreeWidgetItem(m_tree, {e});
        ei->setData(0, kKind, 0);
        for (QString const& j : m_db.jobs(e)) {
            QTreeWidgetItem* const ji = new QTreeWidgetItem(ei, {j, m_db.stage(e, j, QString())});
            ji->setData(0, kKind, 1);
            for (QString const& s : m_db.scenes(e, j)) {
                QTreeWidgetItem* const si = new QTreeWidgetItem(ji, {s, m_db.stage(e, j, s)});
                si->setData(0, kKind, 2);
                if (m_db.lockedByOther(e, j, s, QStringLiteral("scene")))
                    si->setToolTip(0, tr("Being worked on by %1").arg(m_db.lockOf(e, j, s, QStringLiteral("scene")).user));
            }
        }
    }
    m_tree->expandAll();
    if (!env.isEmpty())
        select(env, jb, sc);
    showScene();
}

QString DatabaseManager::environment() const
{
    QTreeWidgetItem* item = m_tree->currentItem();
    while (item && item->parent())
        item = item->parent();
    return item ? item->text(0) : QString();
}

QString DatabaseManager::job() const
{
    QTreeWidgetItem* item = m_tree->currentItem();
    if (!item || item->data(0, kKind).toInt() == 0)
        return QString();
    return item->data(0, kKind).toInt() == 2 ? item->parent()->text(0) : item->text(0);
}

QString DatabaseManager::scene() const
{
    QTreeWidgetItem* const item = m_tree->currentItem();
    return item && item->data(0, kKind).toInt() == 2 ? item->text(0) : QString();
}

int DatabaseManager::pickedVersion() const
{
    int const row = m_versions->currentRow();
    return row >= 0 && m_versions->item(row, 0) ? m_versions->item(row, 0)->text().toInt() : 0;
}

bool DatabaseManager::select(QString const& environmentName, QString const& jobName, QString const& sceneName)
{
    for (int e = 0; e < m_tree->topLevelItemCount(); ++e) {
        QTreeWidgetItem* const ei = m_tree->topLevelItem(e);
        if (ei->text(0) != environmentName)
            continue;
        QTreeWidgetItem* found = ei;
        for (int j = 0; !jobName.isEmpty() && j < ei->childCount(); ++j) {
            if (ei->child(j)->text(0) != jobName)
                continue;
            found = ei->child(j);
            for (int s = 0; !sceneName.isEmpty() && s < found->childCount(); ++s)
                if (found->child(s)->text(0) == sceneName) {
                    found = found->child(s);
                    break;
                }
        }
        m_tree->setCurrentItem(found);
        return true;
    }
    return false;
}

void DatabaseManager::showScene()
{
    m_versions->setRowCount(0);
    m_locks->setRowCount(0);
    QString const e = environment(), j = job(), s = scene();
    if (s.isEmpty()) {
        m_info->setText(j.isEmpty() ? (e.isEmpty() ? tr("Pick an environment, a job or a scene.") : tr("Environment %1: %n job(s).", nullptr, int(m_db.jobs(e).size())).arg(e))
                                    : tr("Job %1: %n scene(s). Approval Policy: %2.", nullptr, int(m_db.scenes(e, j).size()))
                                          .arg(j, m_db.manualApproval(e, j) ? tr("Manual") : tr("Automatic")));
        return;
    }
    int const current = m_db.currentVersion(e, j, s);
    QVector<SceneDatabase::Version> const versions = m_db.versions(e, j, s);
    m_info->setText(tr("Scene %1 - Job %2 - Environment %3. Current version: %4.").arg(s, j, e).arg(current));
    for (SceneDatabase::Version const& v : versions) {
        int const row = m_versions->rowCount();
        m_versions->insertRow(row);
        m_versions->setItem(row, 0, new QTableWidgetItem(QString::number(v.number)));
        m_versions->setItem(row, 1, new QTableWidgetItem(v.user));
        m_versions->setItem(row, 2, new QTableWidgetItem(v.when.toString(QStringLiteral("yyyy-MM-dd hh:mm"))));
        m_versions->setItem(row, 3, new QTableWidgetItem(v.comment));
        if (v.number == current)
            for (int c = 0; c < 4; ++c) {
                QFont f = m_versions->item(row, c)->font();
                f.setBold(true);
                m_versions->item(row, c)->setFont(f);
            }
    }
    for (QString const& what : m_db.locks(e, j, s)) {
        SceneDatabase::Lock const l = m_db.lockOf(e, j, s, what);
        int const row = m_locks->rowCount();
        m_locks->insertRow(row);
        m_locks->setItem(row, 0, new QTableWidgetItem(what));
        m_locks->setItem(row, 1, new QTableWidgetItem(l.user));
        m_locks->setItem(row, 2, new QTableWidgetItem(l.host));
        m_locks->setItem(row, 3, new QTableWidgetItem(l.when.toString(QStringLiteral("yyyy-MM-dd hh:mm"))));
    }
}

bool DatabaseManager::newEnvironment(QString name)
{
    if (name.isEmpty()) {
        bool ok = false;
        name = QInputDialog::getText(this, tr("New Environment"), tr("Name:"), QLineEdit::Normal, QString(), &ok).trimmed();
        if (!ok || name.isEmpty())
            return false;
    }
    bool const ok = m_db.createEnvironment(name, &m_error);
    report(ok);
    if (ok)
        select(name);
    return ok;
}

bool DatabaseManager::newJob(QString name)
{
    QString const env = environment();
    if (env.isEmpty()) {
        m_error = tr("Pick the environment the job goes in.");
        return report(false);
    }
    if (name.isEmpty()) {
        bool ok = false;
        name = QInputDialog::getText(this, tr("New Job"), tr("Name:"), QLineEdit::Normal, QString(), &ok).trimmed();
        if (!ok || name.isEmpty())
            return false;
    }
    bool const ok = m_db.createJob(env, name, &m_error);
    report(ok);
    if (ok)
        select(env, name);
    return ok;
}

bool DatabaseManager::deletePicked()
{
    QString const e = environment(), j = job(), s = scene();
    if (e.isEmpty())
        return false;
    if (isVisible() && QMessageBox::question(this, tr("Delete"), tr("Delete %1?").arg(!s.isEmpty() ? s : !j.isEmpty() ? j : e)) != QMessageBox::Yes)
        return false;
    bool const ok = !s.isEmpty() ? m_db.deleteScene(e, j, s, &m_error) : !j.isEmpty() ? m_db.deleteJob(e, j, &m_error) : m_db.deleteEnvironment(e, &m_error);
    return report(ok);
}

bool DatabaseManager::makeCurrent()
{
    int const v = pickedVersion();
    if (scene().isEmpty() || v <= 0)
        return false;
    return report(m_db.setCurrentVersion(environment(), job(), scene(), v, &m_error));
}

bool DatabaseManager::changeStage(QString stage)
{
    if (job().isEmpty())
        return false;
    if (stage.isEmpty()) {
        QStringList const stages = {tr("Layout"), tr("Animation"), tr("Clean-up"), tr("Ink and Paint"), tr("Compositing"), tr("Approved")};
        bool ok = false;
        stage = QInputDialog::getItem(this, tr("Change Scene Stage"), tr("To Stage"), stages, 0, true, &ok);
        if (!ok)
            return false;
    }
    return report(m_db.setStage(environment(), job(), scene(), stage, &m_error));
}

bool DatabaseManager::setApproval(bool manual)
{
    if (job().isEmpty())
        return false;
    return report(m_db.setManualApproval(environment(), job(), manual, &m_error));
}

bool DatabaseManager::releaseLock(bool force)
{
    int const row = m_locks->currentRow();
    if (scene().isEmpty() || row < 0)
        return false;
    QString const what = m_locks->item(row, 0)->text();
    return report(m_db.releaseRights(environment(), job(), scene(), what, force, &m_error));
}

bool DatabaseManager::exportPicked(QString folder)
{
    if (scene().isEmpty())
        return false;
    if (folder.isEmpty()) {
        folder = QFileDialog::getExistingDirectory(this, tr("Export Scene"));
        if (folder.isEmpty())
            return false;
        folder = QDir(folder).filePath(scene());
    }
    return report(m_db.exportScene(environment(), job(), scene(), folder, &m_error));
}

bool DatabaseManager::importInto(QString file, QString name)
{
    if (job().isEmpty()) {
        m_error = tr("Export needs at least an Environment and a Job.");
        return report(false);
    }
    if (file.isEmpty()) {
        file = QFileDialog::getOpenFileName(this, tr("Import Scene"), QString(), tr("Eclipse scenes (*.eclipse)"));
        if (file.isEmpty())
            return false;
    }
    if (name.isEmpty())
        name = QFileInfo(file).completeBaseName();
    QString const env = environment(), jb = job();
    bool const ok = m_db.importScene(file, env, jb, name, &m_error);
    report(ok);
    if (ok)
        select(env, jb, name);
    return ok;
}

void DatabaseManager::editUsers()
{
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("DatabaseUserList"));
    dialog.setWindowTitle(tr("User List"));
    QVBoxLayout* const layout = new QVBoxLayout(&dialog);
    QTableWidget* const table = new QTableWidget(0, 4, &dialog);
    table->setHorizontalHeaderLabels({tr("User name"), tr("First Name"), tr("Last Name"), tr("Type")});
    table->horizontalHeader()->setStretchLastSection(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    auto fill = [&] {
        table->setRowCount(0);
        for (SceneDatabase::User const& u : m_db.users()) {
            int const row = table->rowCount();
            table->insertRow(row);
            table->setItem(row, 0, new QTableWidgetItem(u.login));
            table->setItem(row, 1, new QTableWidgetItem(u.firstName));
            table->setItem(row, 2, new QTableWidgetItem(u.lastName));
            table->setItem(row, 3, new QTableWidgetItem(u.type == 1 ? tr("Administrator") : tr("User")));
        }
    };
    fill();
    layout->addWidget(table);
    auto ask = [&](SceneDatabase::User user, QString const& title) -> std::optional<SceneDatabase::User> {
        QDialog d(&dialog);
        d.setWindowTitle(title);
        QFormLayout* const form = new QFormLayout(&d);
        QLineEdit* const login = new QLineEdit(user.login, &d);
        QLineEdit* const first = new QLineEdit(user.firstName, &d);
        QLineEdit* const last = new QLineEdit(user.lastName, &d);
        QComboBox* const type = new QComboBox(&d);
        type->addItems({tr("User"), tr("Administrator")});
        type->setCurrentIndex(user.type);
        form->addRow(tr("User Name"), login);
        form->addRow(tr("First Name"), first);
        form->addRow(tr("Last Name"), last);
        form->addRow(tr("Type"), type);
        QDialogButtonBox* const b = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
        connect(b, &QDialogButtonBox::accepted, &d, &QDialog::accept);
        connect(b, &QDialogButtonBox::rejected, &d, &QDialog::reject);
        form->addRow(b);
        if (d.exec() != QDialog::Accepted)
            return std::nullopt;
        return SceneDatabase::User{login->text().trimmed(), first->text().trimmed(), last->text().trimmed(), type->currentIndex()};
    };
    QHBoxLayout* const row = new QHBoxLayout;
    QPushButton* const add = new QPushButton(tr("Add..."), &dialog);
    QPushButton* const modify = new QPushButton(tr("Modify..."), &dialog);
    QPushButton* const remove = new QPushButton(tr("Delete"), &dialog);
    QPushButton* const close = new QPushButton(tr("Close"), &dialog);
    row->addWidget(add);
    row->addWidget(modify);
    row->addWidget(remove);
    row->addStretch(1);
    row->addWidget(close);
    layout->addLayout(row);
    connect(add, &QPushButton::clicked, &dialog, [&] {
        if (auto const u = ask({SceneDatabase::currentUser(), {}, {}, m_db.users().isEmpty() ? 1 : 0}, tr("Add User")); u && !m_db.addUser(*u, &m_error))
            QMessageBox::warning(&dialog, tr("User List"), m_error);
        fill();
    });
    connect(modify, &QPushButton::clicked, &dialog, [&] {
        int const r = table->currentRow();
        if (r < 0)
            return;
        SceneDatabase::User const was = m_db.users().value(r);
        if (auto const u = ask(was, tr("Modify User")); u && !m_db.modifyUser(was.login, *u, &m_error))
            QMessageBox::warning(&dialog, tr("User List"), m_error);
        fill();
    });
    connect(remove, &QPushButton::clicked, &dialog, [&] {
        int const r = table->currentRow();
        if (r >= 0 && !m_db.removeUser(table->item(r, 0)->text(), &m_error))
            QMessageBox::warning(&dialog, tr("User List"), m_error);
        fill();
    });
    connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
    dialog.resize(560, 360);
    dialog.exec();
}
