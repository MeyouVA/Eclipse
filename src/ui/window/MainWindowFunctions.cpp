// Eclipse - the main window: function columns - sent to the Function view, opened in the Function Editor, converted, tagged.
#include "ui/window/MainWindow.h"

#include <QDockWidget>
#include <QShortcut>
#include <QStatusBar>

#include "actions/manager/ActionManager.h"
#include "model/document/Commands.h"
#include "ui/panels/LayerPanel.h"
#include "ui/timeline/FunctionView.h"
#include "ui/timeline/Timeline.h"
#include "ui/timeline/Xsheet.h"
#include "ui/views/FunctionEditor.h"

void MainWindow::connectFunctionColumns()
{
    connect(m_xsheet, &Xsheet::message, this, [this](QString const& text) { statusBar()->showMessage(text, 6000); });
    connect(m_layerPanel, &LayerPanel::message, this, [this](QString const& text) { statusBar()->showMessage(text, 6000); });
    connect(m_layerPanel, &LayerPanel::functionEditorWanted, this, &MainWindow::openFunctionEditor);
    connect(m_layerPanel, &LayerPanel::functionShown, this, [this](int layer, int value) {
        FunctionView* const function = findChild<FunctionView*>(QStringLiteral("FunctionView"));
        QDockWidget* const dock = dockOf("FunctionDock");
        if (!function || !dock)
            return;
        showView(dock);
        dock->raise();
        function->sendToFunctionView(layer, value);
    });
}

QPair<int, int> MainWindow::xsheetValueColumn() const
{
    int const column = m_xsheet->currentColumn();
    int const layer = column >= 0 ? m_xsheet->layerOfColumn(column) : m_timeline->currentLayer();
    return {layer, column >= 0 ? m_xsheet->functionOfColumn(column) : -1};
}

void MainWindow::onActionSendToFunctionView()
{
    FunctionView* const function = findChild<FunctionView*>(QStringLiteral("FunctionView"));
    QDockWidget* const dock = dockOf("FunctionDock");
    if (!function || !dock)
        return;
    QPair<int, int> const at = xsheetValueColumn();
    showView(dock);
    dock->raise();
    if (at.first >= 0 && at.first != m_timeline->currentLayer())
        m_timeline->setCurrentCell(at.first, m_timeline->currentFrame());
    if (at.second >= 0)
        function->sendToFunctionView(at.first, at.second);
}

void MainWindow::onActionOpenFunctionEditor()
{
    Scene const& scene = m_document->scene();
    QPair<int, int> const at = xsheetValueColumn();
    int const column = at.first >= 0 && at.first < scene.layers.size() && at.second >= 0 ? scene.layers[at.first].linkOf(at.second) : 0;
    if (column == 0) {
        statusBar()->showMessage(tr("There is no bezier, ease or local value connecting to the layer."), 5000);
        return;
    }
    openFunctionEditor(column);
}

void MainWindow::openFunctionEditor(int column)
{
    if (!m_document->scene().function(column))
        return;
    for (FunctionEditor* open : findChildren<FunctionEditor*>(QStringLiteral("FunctionEditor")))
        if (open->column() == column) {
            open->show();
            open->raise();
            return;
        }
    FunctionEditor* const editor = new FunctionEditor(m_document, column, this);
    // Preferences: Editors always on top - in front of the window (a tool window), or a window of its own.
    editor->setWindowFlag(m_editorsOnTop ? Qt::Tool : Qt::Window, true);
    editor->setAttribute(Qt::WA_DeleteOnClose);
    editor->setCurrentFrame(m_timeline->currentFrame());
    connect(m_timeline, &Timeline::currentCellChanged, editor, [editor](int, int frame) { editor->setCurrentFrame(frame); });
    // Lock/Unlock Point, on its key (Preferences > Keyboard Shortcuts > Function Editor).
    for (ActionManager::Shortcut const& s : m_actions->shortcuts())
        if (s.id == QLatin1String("shortcut.feLockPoint") && !s.key.isEmpty()) {
            QShortcut* const lock = new QShortcut(s.key, editor);
            lock->setContext(Qt::WidgetWithChildrenShortcut);
            connect(lock, &QShortcut::activated, editor, &FunctionEditor::toggleLock);
        }
    editor->show();
}

void MainWindow::onActionConvertFunction(QString const& how)
{
    Scene const& scene = m_document->scene();
    QPair<int, int> const at = xsheetValueColumn();
    int const column = at.first >= 0 && at.first < scene.layers.size() && at.second >= 0 ? scene.layers[at.first].linkOf(at.second) : 0;
    FunctionColumn const* from = scene.function(column);
    if (!from) {
        statusBar()->showMessage(tr("There is no bezier, ease or local value connecting to the layer."), 5000);
        return;
    }
    int const kind = how.endsWith(QLatin1String("ase")) ? FunctionColumn::Ease : FunctionColumn::Bezier;
    bool const intoNew = how.startsWith(QLatin1String("new"));
    if (!intoNew && from->kind == kind) {
        statusBar()->showMessage(tr("The column is a %1 column already.").arg(FunctionColumn::kindName(kind)), 5000);
        return;
    }
    if (m_document->convertFunction(column, kind, intoNew) == 0)
        statusBar()->showMessage(tr("No Velobased Available"), 5000);
}

void MainWindow::onActionFunctionColumnsTag(QString const& how)
{
    // Function Columns Visible / Invisible: of the layers selected; Hide All Other Function Columns: of the rest.
    Scene const& scene = m_document->scene();
    QVector<int> selected = m_xsheet->selectedLayers();
    if (selected.isEmpty())
        selected = m_timeline->selectedLayers();
    QVector<int> layers;
    int hidden = 0;
    if (how == QLatin1String("hideOthers")) {
        for (int l = 0; l < scene.layers.size(); ++l)
            if (!selected.contains(l))
                layers.push_back(l);
        hidden = 511;
    } else {
        layers = selected;
        hidden = how == QLatin1String("invisible") ? 511 : 0;
    }
    QVector<int> changing;
    for (int l : std::as_const(layers))
        if (l >= 0 && l < scene.layers.size() && scene.layers[l].hiddenFunctions != hidden)
            changing.push_back(l);
    if (changing.isEmpty())
        return;
    m_document->editLayers(changing, hidden ? tr("Function Columns Invisible") : tr("Function Columns Visible"), [hidden](Layer& layer) { layer.hiddenFunctions = hidden; });
}
