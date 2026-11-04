// Eclipse - the main window: symbols expanded in groups, the Library's symbols, symbols from drawn art, and the editing stack.
#include "ui/window/MainWindow.h"

#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QStatusBar>
#include <QToolButton>

#include "canvas/Canvas.h"
#include "model/document/Commands.h"
#include "ui/panels/LibraryView.h"
#include "ui/timeline/Timeline.h"

int MainWindow::expandSelectedSymbols(bool inGroups)
{
    // Every selected layer that shows a symbol, the last first so the places of the others hold.
    QVector<int> picked = m_timeline->selectedLayers();
    if (picked.isEmpty())
        picked = {layer()};
    std::sort(picked.begin(), picked.end(), std::greater<int>());
    QVector<int> symbols;
    for (int l : std::as_const(picked))
        if (l >= 0 && l < m_document->scene().layers.size() && m_document->scene().layers[l].showsSymbol())
            symbols.push_back(l);
    if (symbols.isEmpty())
        return -1;
    int expanded = 0, last = -1;
    m_document->beginMacro(inGroups ? tr("Expand Selected Symbols in Groups") : tr("Expand Symbol"));
    for (int l : std::as_const(symbols)) {
        int const at = m_document->expandSymbol(l, inGroups);
        if (at >= 0) {
            ++expanded;
            last = at;
        }
    }
    m_document->endMacro();
    if (last >= 0) {
        m_timeline->clearRange();
        m_timeline->setCurrentCell(last, frame());
    }
    return expanded;
}

void MainWindow::onActionExpandSymbolInGroups()
{
    if (expandSelectedSymbols(true) <= 0)
        statusBar()->showMessage(tr("Select a symbol without group effects, opacity changes or deformation to expand it."), 7000);
}

void MainWindow::onActionCreateEmptySymbolInLibrary()
{
    int const id = m_document->addLibrarySymbol(QString());
    if (id <= 0)
        return;
    if (QDockWidget* const dock = dockOf("LibraryDock"))
        showView(dock);
    m_library->showSceneSymbols(id);
    statusBar()->showMessage(tr("An empty symbol in the Library. Drag it into the Timeline, or Insert it, to show it on a layer."), 7000);
}

void MainWindow::onActionCreateSymbolFromSelection()
{
    Scene const& scene = m_document->scene();
    int const l = layer(), f = frame();
    if (l < 0 || l >= scene.layers.size() || !scene.layers[l].holdsDrawings() || scene.layers[l].showsSymbol()
        || scene.layers[l].drawingAt(f) == Layer::kNoDrawing || !m_canvas->hasSelection()) {
        statusBar()->showMessage(tr("Unable to create symbol from selection: select art in a drawing with the Select tool first."), 6000);
        return;
    }
    int const drawing = scene.layers[l].drawingAt(f);
    QString const name = scene.layers[l].name;
    m_document->beginMacro(tr("Create Symbol from Drawing Selection"));
    Drawing const piece = m_canvas->takeSelection(tr("Create Symbol from Drawing Selection"));
    int const at = m_document->createSymbolFromArt(l, drawing, piece, name);
    m_document->endMacro();
    if (at < 0) {
        statusBar()->showMessage(tr("Unable to create symbol from selection."), 6000);
        return;
    }
    m_timeline->clearRange();
    m_timeline->setCurrentCell(at, f);
    updateActionStates();
    statusBar()->showMessage(tr("Symbol %1 made from the art selected.").arg(m_document->scene().layers[at].name), 6000);
}

void MainWindow::onActionCopyPivotToParentSymbol()
{
    if (!m_document->insideSymbol()) {
        statusBar()->showMessage(tr("Copy Pivot to Parent Symbol works inside a symbol (Edit > Symbol > Edit Symbol)."), 5000);
        return;
    }
    int const l = layer();
    if (l < 0 || l >= m_document->scene().layers.size())
        return;
    QPointF const pivot = m_document->scene().layers[l].pivot;
    int const inside = frame();
    QString const innerName = m_document->scene().layers[l].name;
    // Out to the scene around, where the layers showing the symbol take the pivot, and back in.
    int const symbolId = m_document->currentSymbolId();
    m_document->beginMacro(tr("Copy Pivot to Parent Symbol"));
    m_document->leaveSymbol();
    QVector<int> shows;
    for (int i = 0; i < m_document->scene().layers.size(); ++i)
        if (m_document->scene().layers[i].symbolId == symbolId)
            shows.push_back(i);
    m_document->editLayers(shows, tr("Copy Pivot to Parent Symbol"), [pivot](Layer& shown) { shown.pivot = pivot; });
    m_document->enterSymbol(symbolId);
    m_document->endMacro();
    int back = 0;
    for (int i = 0; i < m_document->scene().layers.size(); ++i)
        if (m_document->scene().layers[i].name == innerName)
            back = i;
    m_timeline->setCurrentCell(back, std::clamp(inside, 0, m_document->scene().frameCount() - 1));
    updateTitle();
    updateActionStates();
    statusBar()->showMessage(tr("%n layer(s) showing the symbol now turn and scale about this pivot.", nullptr, int(shows.size())), 5000);
}

int MainWindow::insertLibrarySymbol(int id)
{
    int const at = m_document->insertSymbol(id, layer());
    if (at >= 0) {
        m_timeline->clearRange();
        m_timeline->setCurrentCell(at, frame());
        updateActionStates();
    }
    return at;
}

bool MainWindow::renameLibrarySymbol(int id, QString const& name)
{
    if (!m_document->renameSymbol(id, name))
        return false;
    updateTitle();
    return true;
}

int MainWindow::copyLibrarySymbol(int id)
{
    int const made = m_document->copySymbol(id);
    if (made > 0)
        m_library->showSceneSymbols(made);
    return made;
}

int MainWindow::deleteLibrarySymbol(int id, bool ask)
{
    Scene::Symbol const* const symbol = m_document->scene().findSymbol(id);
    if (!symbol)
        return -1;
    if (m_document->insideSymbol()) {
        statusBar()->showMessage(tr("Leave the symbols being edited before deleting a symbol from the Library."), 6000);
        return -1;
    }
    int const references = m_document->scene().symbolReferences(id);
    if (ask && QMessageBox::question(this, tr("Symbol Deletion"),
                                     tr("Deletion of symbol %1 will also remove all references to this symbol from the scene and/or other symbols (%n layer(s)).\nProceed?",
                                        nullptr, references).arg(symbol->name))
                   != QMessageBox::Yes)
        return -1;
    int const removed = m_document->deleteSymbol(id);
    m_timeline->clearRange();
    m_timeline->setCurrentCell(std::clamp(layer(), 0, int(m_document->scene().layers.size()) - 1), frame());
    updateActionStates();
    return removed;
}

void MainWindow::leaveSymbolsTo(int depth)
{
    while (m_document->insideSymbol() && int(m_document->symbolTrail().size()) > std::max(0, depth))
        onActionLeaveSymbol();
}

void MainWindow::updateSymbolTrail()
{
    // The editing stack, over the Camera view: the top scene, then each symbol gone into; a click goes back to it.
    if (!m_symbolTrail)
        return;
    QStringList const trail = m_document->symbolTrail();
    if (trail == m_symbolTrailShown && m_symbolTrail->layout()->count() > 0) {
        updateSymbolFrameLabel();
        return;
    }
    m_symbolTrailShown = trail;
    QLayout* const row = m_symbolTrail->layout();
    while (QLayoutItem* const item = row->takeAt(0)) {
        if (QWidget* const old = item->widget()) {
            old->hide();
            old->deleteLater();
        }
        delete item;
    }
    m_symbolTrail->setVisible(!trail.isEmpty());
    if (trail.isEmpty())
        return;
    QStringList const names = QStringList{tr("Top")} + trail;
    for (int depth = 0; depth < names.size(); ++depth) {
        if (depth > 0)
            row->addWidget(new QLabel(QStringLiteral(">"), m_symbolTrail));
        QToolButton* const step = new QToolButton(m_symbolTrail);
        step->setObjectName(QStringLiteral("SymbolTrail%1").arg(depth));
        step->setText(names[depth]);
        step->setAutoRaise(true);
        step->setEnabled(depth < names.size() - 1);
        connect(step, &QToolButton::clicked, this, [this, depth] { leaveSymbolsTo(depth); });
        row->addWidget(step);
    }
    static_cast<QHBoxLayout*>(row)->addStretch(1);
    // Where in the symbol the frame worked on is, beside the scene's frame it was gone into from.
    QLabel* const frames = new QLabel(m_symbolTrail);
    frames->setObjectName(QStringLiteral("SymbolFrameLabel"));
    row->addWidget(frames);
    updateSymbolFrameLabel();
}

void MainWindow::updateSymbolFrameLabel()
{
    QLabel* const frames = m_symbolTrail ? m_symbolTrail->findChild<QLabel*>(QStringLiteral("SymbolFrameLabel")) : nullptr;
    if (!frames || !m_document->insideSymbol())
        return;
    QString const scene = m_globalFrames.isEmpty() ? QString() : tr("Current Frame: %1").arg(m_globalFrames.first().frame + 1);
    QString const inside = tr("Current Symbol Frame: %1").arg(frame() + 1);
    frames->setText(scene.isEmpty() ? inside : scene + QStringLiteral("   ") + inside);
}
