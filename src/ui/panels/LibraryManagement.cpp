#include "ui/panels/LibraryView.h"
#include "actions/theme/AppSettings.h"
#include <QAction>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

void LibraryView::setupManagement()
{
    auto* button=new QToolButton(this); button->setObjectName(QStringLiteral("LibraryMenu")); button->setText(tr("Library")); button->setPopupMode(QToolButton::InstantPopup);
    auto* menu=new QMenu(button); button->setMenu(menu); qobject_cast<QVBoxLayout*>(layout())->insertWidget(0,button,0,Qt::AlignLeft);
    auto* folders=menu->addMenu(tr("Folders")); folders->setObjectName(QStringLiteral("LibraryFolderMenu"));
    auto* items=menu->addMenu(tr("Items")); items->setObjectName(QStringLiteral("LibraryItemMenu"));
    auto* view=menu->addMenu(tr("View"));
    auto add=[this](QMenu* parent,QString const& text,char const* name,auto callback) { auto* action=parent->addAction(text); action->setObjectName(QLatin1String(name)); connect(action,&QAction::triggered,this,callback); return action; };
    auto* rights=add(folders,tr("Right To Modify"),"LibraryModify",[this](bool checked) { QString error; if(!setRightToModify(checked,&error)) QMessageBox::warning(this,tr("Library"),error); }); rights->setCheckable(true); items->addAction(rights);
    auto update=[this,rights] { rights->setChecked(rightToModify()); }; connect(folders,&QMenu::aboutToShow,this,update); connect(items,&QMenu::aboutToShow,this,update);
    add(folders,tr("Open Library..."),"LibraryOpen",[this] { findChild<QToolButton*>(QStringLiteral("LibraryAddFolder"))->click(); });
    add(folders,tr("Close Library"),"LibraryClose",[this] { removeFolder(int(m_folders.indexOf(rootFor(currentFolder())))); });
    add(folders,tr("New Folder..."),"LibraryMakeFolder",[this] { findChild<QToolButton*>(QStringLiteral("LibraryNewFolder"))->click(); });
    add(folders,tr("Rename Folder..."),"LibraryRenameFolder",[this] { bool okay=false; QString const name=QInputDialog::getText(this,tr("Rename Folder"),tr("Folder name:"),QLineEdit::Normal,QFileInfo(currentFolder()).fileName(),&okay); if(okay && !renameFolder(name)) QMessageBox::warning(this,tr("Library"),tr("The folder could not be renamed. Check its name, write rights and library roots.")); });
    add(folders,tr("Delete Folder..."),"LibraryDeleteFolder",[this] { deleteFolder(true); });
    auto* refresh=add(folders,tr("Refresh"),"LibraryRefresh",[this] { refreshFolders(); }); items->addAction(refresh);
    auto* open=add(folders,tr("Open As Folder"),"LibraryOpenFolder",[this] { QDesktopServices::openUrl(QUrl::fromLocalFile(currentFolder())); }); items->addAction(open);
    add(items,tr("Rename..."),"LibraryRenameItem",[this] { findChild<QPushButton*>(QStringLiteral("LibraryRename"))->click(); });
    add(items,tr("Delete..."),"LibraryDeleteItem",[this] { findChild<QPushButton*>(QStringLiteral("LibraryDelete"))->click(); });
    auto* generate=add(view,tr("Generate Thumbnails"),"LibraryGenerateThumbnails",[this] { generateThumbnails(); }); folders->addAction(generate); items->addAction(generate);
    auto* erase=add(view,tr("Delete Thumbnails"),"LibraryDeleteThumbnails",[this] { deleteThumbnails(); }); folders->addAction(erase); items->addAction(erase);
    auto* automatic=add(view,tr("Generate Thumbnails Automatically"),"LibraryAutoThumbnails",[this](bool checked) { setAutoThumbnails(checked); }); automatic->setCheckable(true); automatic->setChecked(settings().value(QStringLiteral("prefs/libraryAutoThumbnails"),true).toBool());
    add(view,tr("Thumbnails"),"LibraryThumbnails",[this] { setItemsView(0); });
    add(view,tr("List"),"LibraryList",[this] { setItemsView(2); });
    add(view,tr("Details"),"LibraryDetails",[this] { setItemsView(3); });
    m_items->setContextMenuPolicy(Qt::CustomContextMenu); connect(m_items,&QWidget::customContextMenuRequested,this,[this](QPoint const& point) { if(auto* item=m_items->itemAt(point)) m_items->setCurrentItem(item); showManagementMenu(false,point); });
    m_items->installEventFilter(this); m_folderList->installEventFilter(this);
}

void LibraryView::showManagementMenu(bool folder,QPoint const& point)
{
    auto* menu=findChild<QMenu*>(folder?QStringLiteral("LibraryFolderMenu"):QStringLiteral("LibraryItemMenu"));
    menu->exec((folder?m_folderList:m_items)->viewport()->mapToGlobal(point));
}
bool LibraryView::eventFilter(QObject* watched,QEvent* event)
{
    if((watched==m_items || watched==m_folderList) && (event->type()==QEvent::ShortcutOverride || event->type()==QEvent::KeyPress)) {
        auto* key=static_cast<QKeyEvent*>(event);
        if(key->key()==Qt::Key_F5 && key->modifiers()==Qt::NoModifier) { event->accept(); if(event->type()==QEvent::KeyPress && !key->isAutoRepeat()) refreshFolders(); return true; }
    }
    return QWidget::eventFilter(watched,event);
}
void LibraryView::generateThumbnails() { m_thumbnails.clear(); QScopedValueRollback<bool> force(m_forceThumbnails,true); refresh(); }
void LibraryView::deleteThumbnails() { m_thumbnails.clear(); QScopedValueRollback<bool> suppress(m_suppressThumbnails,true); refresh(); }
void LibraryView::setAutoThumbnails(bool enabled)
{
    settings().setValue(QStringLiteral("prefs/libraryAutoThumbnails"),enabled);
    if(auto* action=findChild<QAction*>(QStringLiteral("LibraryAutoThumbnails"))) action->setChecked(enabled);
    refresh();
}
