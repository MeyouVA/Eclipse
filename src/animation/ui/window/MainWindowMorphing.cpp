// Eclipse - the main window: the morphing commands - going through a morphing, its key drawings, its hints, a key drawing
// put in it, its length, and the Morphing Inspector.
#include "animation/ui/window/MainWindow.h"

#include "canvas/Canvas.h"
#include "model/document/Commands.h"
#include "animation/ui/timeline/Timeline.h"
#include "animation/ui/views/MorphInspector.h"

#include <QDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QStatusBar>
#include <QVBoxLayout>

void MainWindow::onActionMorphGoTo(QString const& which)
{
    // Go to First / Previous / Next / Last Frame of the morphing.
    int const index = which == QLatin1String("first") ? 0 : which == QLatin1String("previous") ? 1 : which == QLatin1String("next") ? 2 : 3;
    int const to = m_document->morphFrame(layer(), frame(), index);
    if (to < 0) {
        statusBar()->showMessage(tr("Please select one cell in a morphing."), 5000);
        return;
    }
    m_timeline->setCurrentCell(layer(), to);
}

void MainWindow::onActionSwitchMorphKey()
{
    // Switch Between Morphing Key Drawings: from one key drawing to the other (from in between: to the first).
    int const first = m_document->morphFrame(layer(), frame(), 0), last = m_document->morphFrame(layer(), frame(), 3);
    if (first < 0) {
        statusBar()->showMessage(tr("In Order to see both Key Drawings at the same time,\nuse the Switch Between Morphing Key Drawings shortcut within the Drawing view.")
                                     .replace(QLatin1Char('\n'), QLatin1Char(' ')),
                                 6000);
        return;
    }
    m_timeline->setCurrentCell(layer(), frame() == first ? last : first);
}

void MainWindow::onActionMorphHintMode(QString const& mode)
{
    // Contour / Zone / Pencil / Appearing Point / Vanishing Point Hint, or Delete Morphing Hint Mode: what the Morphing tool
    // puts down (it is chosen too).
    static QStringList const modes{QStringLiteral("contour"), QStringLiteral("zone"), QStringLiteral("pencil"), QStringLiteral("appearing"),
                                   QStringLiteral("vanishing")};
    int const at = int(modes.indexOf(mode));
    m_canvas->setHintMode(at >= 0 ? at : Canvas::kDeleteHints);
    m_canvas->setTool(Canvas::Morphing);
    if (m_morphInspector)
        m_morphInspector->refresh();
}

void MainWindow::onActionSuggestHints()
{
    int const index = m_document->scene().layers.value(layer()).morphIndexAt(frame());
    int found = -1;
    if (index >= 0)
        found = m_document->suggestMorphHints(layer(), index);
    else if (int const first = m_document->morphFrame(layer(), frame(), 0); first >= 0)
        found = m_document->suggestMorphHints(layer(), m_document->scene().layers[layer()].morphIndexAt(first));
    if (found < 0)
        statusBar()->showMessage(tr("No Morphing Selected"), 4000);
    else
        statusBar()->showMessage(tr("%n hint(s) suggested.", nullptr, found), 4000);
}

void MainWindow::onActionHideHints(bool hidden)
{
    m_canvas->setHintsHidden(hidden);
    if (m_morphInspector)
        m_morphInspector->refresh();
}

void MainWindow::onActionDeleteMorphHints()
{
    // Delete All Morphing Hints.
    int const first = m_document->morphFrame(layer(), frame(), 0);
    if (first < 0)
        return;
    int const index = m_document->scene().layers[layer()].morphIndexAt(first);
    Morph morph = m_document->scene().layers[layer()].morphs[index];
    morph.hints.clear();
    morph.hintTypes.clear();
    m_document->setMorph(layer(), index, morph, tr("Delete All Morphing Hints"));
}

void MainWindow::onActionShowOtherMorphKey(bool shown)
{
    m_canvas->setOtherKeyShown(shown);
}

void MainWindow::onActionInsertMorphKey()
{
    QString why;
    if (!m_document->insertMorphKey(layer(), frame(), &why))
        QMessageBox::information(this, tr("Insert Morphing Key Drawing"), why);
}

void MainWindow::onActionReplaceMorphLength()
{
    int const first = m_document->morphFrame(layer(), frame(), 0), last = m_document->morphFrame(layer(), frame(), 3);
    if (first < 0 || frame() == last) {
        QMessageBox::information(this, tr("Replace Morphing Length"), tr("Please select one cell in a morphing."));
        return;
    }
    bool ok = false;
    int const length = QInputDialog::getInt(this, tr("Replace Morphing Length"), tr("Frames from the first key drawing to the second:"), last - first, 2, 9999, 1, &ok);
    if (ok)
        m_document->setMorphLength(layer(), frame(), length);
}

void MainWindow::onActionMorphInspector()
{
    // Show/Hide Morphing Inspector.
    if (!m_morphInspectorWindow) {
        m_morphInspectorWindow = new QDialog(this);
        m_morphInspectorWindow->setObjectName(QStringLiteral("MorphInspectorWindow"));
        m_morphInspectorWindow->setWindowTitle(tr("Morphing Inspector"));
        QVBoxLayout* const box = new QVBoxLayout(m_morphInspectorWindow);
        m_morphInspector = new MorphInspector(m_document, m_canvas, m_morphInspectorWindow);
        box->addWidget(m_morphInspector);
        connect(m_morphInspector, &MorphInspector::switchRequested, this, &MainWindow::onActionSwitchMorphKey);
    }
    m_morphInspectorWindow->setVisible(!m_morphInspectorWindow->isVisible());
    if (m_morphInspectorWindow->isVisible())
        m_morphInspector->refresh();
}
