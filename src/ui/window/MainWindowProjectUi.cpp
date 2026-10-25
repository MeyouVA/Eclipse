// Eclipse - each project's view state, stored with its saved scene.
#include "ui/window/MainWindow.h"
#include "ui/window/MainWindowPrivate.h"
#include "canvas/Canvas.h"
#include "model/document/Commands.h"
#include "actions/manager/ActionManager.h"
#include "ui/views/DepthView.h"
#include "ui/views/PerspectiveView.h"
#include "ui/views/NodeView.h"
#include "ui/timeline/Xsheet.h"
#include "ui/timeline/FunctionView.h"
#include <QAbstractScrollArea>
#include <QAction>
#include <QDockWidget>
#include <QJsonArray>
#include <QScrollBar>
#include <QStatusBar>
#include <QSplitter>
#include <QTabWidget>
#include <QComboBox>
#include <QSignalBlocker>

namespace {
QString widgetKey(QObject const* widget, QObject const* root)
{
    QStringList parts;
    for (QObject const* at = widget; at && at != root; at = at->parent()) {
        QString name = at->objectName();
        if (name.isEmpty()) {
            name = QString::fromLatin1(at->metaObject()->className());
            int index = 0;
            if (at->parent())
                for (QObject const* sibling : at->parent()->children()) {
                    if (sibling == at)
                        break;
                    if (sibling->metaObject() == at->metaObject())
                        ++index;
                }
            name += QString::number(index);
        }
        parts.prepend(name);
    }
    return parts.join(QLatin1Char('/'));
}
QJsonArray dockNames(QList<QDockWidget*> const& docks)
{
    QJsonArray names;
    for (QDockWidget const* dock : docks)
        names.append(dock->objectName());
    return names;
}
}

QJsonObject MainWindow::projectUiState() const
{
    QJsonObject state{{"version", 1}, {"layoutVersion", kLayoutVersion}, {"geometry", QString::fromLatin1(saveGeometry().toBase64())},
                      {"layout", QString::fromLatin1(saveState(kLayoutVersion).toBase64())}, {"workspace", m_workspace},
                      {"canvas", m_canvas->viewState()}, {"frame", frame()}, {"layer", layer() >= 0 && layer() < m_document->scene().layers.size()
                          ? m_document->scene().layers[layer()].id : -1}, {"middleCollapsed", m_middleCollapsed},
                      {"cameraFloating", cameraFloating()}, {"cameraDock", int(cameraDockArea())}};
    state["timelineZoom"] = m_timeline->cellWidth();
    QJsonArray parameters;
    for (int i = 0; i < m_document->scene().layers.size(); ++i)
        if (m_timeline->parametersShown(i))
            parameters.append(m_document->scene().layers[i].id);
    state["timelineParameters"] = parameters;
    QJsonObject viewActions;
    for (char const* id : {"LightTable", "CurrentDrawingOnTop", "CameraMask", "ShowSafeArea", "ShowSafeTitle", "ShowGrid", "GridOutlineOnly",
                          "GridUnderlay", "ProportionGrid", "Backlight", "MirrorView", "PreviewAllArt", "OnionSkinToggle", "ShowGuides"})
        if (QAction* action = m_actions->action(QLatin1String(id)); action && action->isCheckable())
            viewActions[QLatin1String(id)] = action->isChecked();
    state["viewActions"] = viewActions;
    if (m_xsheet)
        state["xsheet"] = QJsonObject{{"zoom", m_xsheet->zoomLevel()}, {"width", m_xsheet->columnWidth()}, {"shown", m_xsheet->shown()},
            {"thumbnails", m_xsheet->thumbnails()}, {"minimal", m_xsheet->minimalHeaders()}, {"grouping", m_xsheet->grouping()}};
    if (m_cameraWindow)
        state["cameraGeometry"] = QString::fromLatin1(m_cameraWindow->saveGeometry().toBase64());
    if (QWidget* drawing = findChild<QWidget*>(QStringLiteral("DrawingWindow"), Qt::FindDirectChildrenOnly)) {
        state["drawingWindow"] = drawing->isVisible();
        state["drawingGeometry"] = QString::fromLatin1(drawing->saveGeometry().toBase64());
    }
    if (m_activeView)
        state["activeView"] = widgetKey(m_activeView, this);
    QJsonArray collapsed, front, folds;
    for (int side = 0; side < SideCount; ++side) {
        collapsed.append(dockNames(m_collapsed[side]));
        front.append(dockNames(m_collapsedFront[side]));
    }
    for (Fold const& fold : m_folds)
        folds.append(QJsonObject{{"docks", dockNames(fold.docks)}, {"front", dockNames(fold.front)},
                                 {"anchor", fold.anchor ? fold.anchor->objectName() : QString()}, {"before", fold.before},
                                 {"stacked", fold.stacked}, {"size", fold.size}, {"anchorSize", fold.anchorSize}});
    state["collapsed"] = collapsed;
    state["front"] = front;
    state["folds"] = folds;
    QJsonObject scrolls, views, titles, splitters, tabs;
    for (QSplitter* splitter : findChildren<QSplitter*>())
        splitters[widgetKey(splitter, this)] = QString::fromLatin1(splitter->saveState().toBase64());
    for (QTabWidget* tab : findChildren<QTabWidget*>())
        tabs[widgetKey(tab, this)] = tab->currentIndex();
    for (FunctionGraph* graph : findChildren<FunctionGraph*>())
        views[widgetKey(graph, this)] = graph->viewState();
    for (QAbstractScrollArea* area : findChildren<QAbstractScrollArea*>())
        scrolls[widgetKey(area, this)] = QJsonArray{area->horizontalScrollBar()->value(), area->verticalScrollBar()->value()};
    for (DepthView* view : findChildren<DepthView*>())
        views[widgetKey(view, this)] = QJsonObject{{"zoom", view->zoom()}, {"x", view->pan().x()}, {"y", view->pan().y()}};
    for (PerspectiveView* view : findChildren<PerspectiveView*>())
        views[widgetKey(view, this)] = view->viewState();
    for (NodeView* view : findChildren<NodeView*>())
        views[widgetKey(view, this)] = view->viewState();
    for (QDockWidget* dock : m_docks)
        titles[dock->objectName()] = dock->windowTitle();
    for (QAction* action : m_actions->findChildren<QAction*>())
        if (action->isChecked() && action->objectName().startsWith(QLatin1String("Tool")))
            state["tool"] = action->objectName();
    state["scrolls"] = scrolls;
    state["views"] = views;
    state["titles"] = titles;
    state["splitters"] = splitters;
    state["tabs"] = tabs;
    return state;
}

void MainWindow::restoreProjectUi(QJsonObject const& state)
{
    if (state["version"].toInt() != 1)
        return;
    expandAllSides();
    QString const workspace = state["workspace"].toString();
    if (workspaces().contains(workspace))
        m_workspace = workspace;
    refreshWorkspaceBox();
    restoreGeometry(QByteArray::fromBase64(state["geometry"].toString().toLatin1()));
    int const docked = state["cameraDock"].toInt();
    setCameraDocked(docked == Qt::LeftDockWidgetArea || docked == Qt::RightDockWidgetArea || docked == Qt::TopDockWidgetArea || docked == Qt::BottomDockWidgetArea
        ? Qt::DockWidgetArea(docked) : Qt::NoDockWidgetArea);
    setCameraFloating(state["cameraFloating"].toBool());
    if (m_cameraWindow)
        m_cameraWindow->restoreGeometry(QByteArray::fromBase64(state["cameraGeometry"].toString().toLatin1()));
    setDrawingWindowShown(state["drawingWindow"].toBool());
    if (QWidget* drawing = findChild<QWidget*>(QStringLiteral("DrawingWindow"), Qt::FindDirectChildrenOnly))
        drawing->restoreGeometry(QByteArray::fromBase64(state["drawingGeometry"].toString().toLatin1()));
    if (state["layoutVersion"].toInt() == kLayoutVersion)
        restoreState(QByteArray::fromBase64(state["layout"].toString().toLatin1()), kLayoutVersion);
    QJsonObject const splitters = state["splitters"].toObject(), tabs = state["tabs"].toObject();
    for (QSplitter* splitter : findChildren<QSplitter*>())
        if (QString const stateBytes = splitters[widgetKey(splitter, this)].toString(); !stateBytes.isEmpty())
            splitter->restoreState(QByteArray::fromBase64(stateBytes.toLatin1()));
    for (QTabWidget* tab : findChildren<QTabWidget*>())
        if (int const index = tabs[widgetKey(tab, this)].toInt(-1); index >= 0 && index < tab->count())
            tab->setCurrentIndex(index);
    auto docks = [&](QJsonArray const& names) {
        QList<QDockWidget*> found;
        for (QJsonValue const& name : names)
            if (QDockWidget* dock = findChild<QDockWidget*>(name.toString()))
                found << dock;
        return found;
    };
    for (int side = 0; side < SideCount; ++side) {
        m_collapsed[side] = docks(state["collapsed"].toArray().at(side).toArray());
        m_collapsedFront[side] = docks(state["front"].toArray().at(side).toArray());
    }
    m_folds.clear();
    for (QJsonValue const& value : state["folds"].toArray()) {
        QJsonObject const f = value.toObject();
        Fold fold;
        fold.docks = docks(f["docks"].toArray());
        fold.front = docks(f["front"].toArray());
        fold.anchor = findChild<QDockWidget*>(f["anchor"].toString());
        fold.before = f["before"].toBool();
        fold.stacked = f["stacked"].toBool(true);
        fold.size = f["size"].toInt();
        fold.anchorSize = f["anchorSize"].toInt();
        if (fold.anchor && !fold.docks.isEmpty())
            m_folds << fold;
    }
    setMiddleCollapsed(state["middleCollapsed"].toBool());
    Scene const& scene = m_document->scene();
    int chosen = -1;
    for (int i = 0; i < scene.layers.size(); ++i)
        if (scene.layers[i].id == state["layer"].toInt(-1))
            chosen = i;
    m_timeline->setCurrentCell(chosen >= 0 ? chosen : std::max(0, int(scene.layers.size()) - 1),
                               std::clamp(state["frame"].toInt(), 0, std::max(0, scene.frameCount() - 1)));
    QJsonObject const views = state["views"].toObject();
    for (FunctionGraph* graph : findChildren<FunctionGraph*>())
        graph->restoreViewState(views[widgetKey(graph, this)].toObject());
    for (DepthView* view : findChildren<DepthView*>()) {
        QJsonObject const v = views[widgetKey(view, this)].toObject();
        if (!v.isEmpty())
            view->restoreView(v["zoom"].toDouble(1), QPointF(v["x"].toDouble(), v["y"].toDouble()));
    }
    for (PerspectiveView* view : findChildren<PerspectiveView*>()) {
        QJsonObject const v = views[widgetKey(view, this)].toObject();
        view->restoreViewState(v);
    }
    for (NodeView* view : findChildren<NodeView*>()) {
        QJsonObject const v = views[widgetKey(view, this)].toObject();
        if (!v.isEmpty())
            view->restoreViewState(v);
    }
    if (QAction* tool = m_actions->action(state["tool"].toString()); tool && tool->isEnabled())
        tool->trigger();
    QJsonObject const viewActions = state["viewActions"].toObject();
    for (auto it = viewActions.begin(); it != viewActions.end(); ++it)
        if (QAction* action = m_actions->action(it.key()); action && action->isCheckable() && action->isChecked() != it.value().toBool())
            action->trigger();
    m_canvas->restoreViewState(state["canvas"].toObject());
    m_actions->setChecked(QStringLiteral("MirrorView"), m_canvas->viewMirrored());
    m_actions->setChecked(QStringLiteral("PreviewAllArt"), m_canvas->previewsAllArt());
    m_actions->setChecked(QStringLiteral("OnionSkinToggle"), m_canvas->onionSkin());
    m_actions->setChecked(QStringLiteral("Backlight"), m_canvas->backlight());
    if (QComboBox* mode = findChild<QComboBox*>(QStringLiteral("ViewMode"))) {
        QSignalBlocker const block(mode);
        mode->setCurrentIndex(int(m_canvas->viewMode()));
    }
    QJsonObject const titles = state["titles"].toObject();
    for (QDockWidget* dock : m_docks)
        if (titles.contains(dock->objectName()))
            dock->setWindowTitle(titles[dock->objectName()].toString());
    QJsonObject const scrolls = state["scrolls"].toObject();
    m_timeline->setCellWidth(state["timelineZoom"].toInt(8));
    QJsonArray const parameters = state["timelineParameters"].toArray();
    for (int i = 0; i < scene.layers.size(); ++i)
        m_timeline->setParametersShown(i, parameters.contains(scene.layers[i].id));
    if (m_xsheet) {
        QJsonObject const x = state["xsheet"].toObject();
        if (!x.isEmpty()) {
            m_xsheet->setZoomLevel(x["zoom"].toInt(m_xsheet->zoomLevel()));
            m_xsheet->setColumnWidth(x["width"].toInt(m_xsheet->columnWidth()));
            m_xsheet->setShown(x["shown"].toInt(Xsheet::ShowEverything));
            m_xsheet->setThumbnails(x["thumbnails"].toBool());
            m_xsheet->setMinimalHeaders(x["minimal"].toBool());
            m_xsheet->setGrouping(x["grouping"].toBool());
        }
    }
    for (QAbstractScrollArea* area : findChildren<QAbstractScrollArea*>()) {
        QJsonArray const value = scrolls[widgetKey(area, this)].toArray();
        if (value.size() == 2) {
            area->horizontalScrollBar()->setValue(value[0].toInt());
            area->verticalScrollBar()->setValue(value[1].toInt());
        }
    }
    QString const activeKey = state["activeView"].toString();
    if (!activeKey.isEmpty())
        for (QWidget* active : findChildren<QWidget*>())
            if (widgetKey(active, this) == activeKey) {
                setActiveView(active);
                break;
            }
    refreshViewTabs();
    placeCollapseArrows();
    placeBetweenArrows();
    markWorkspaceClean();
}
