// Eclipse - the main window: lip-sync - detecting, correcting, mapping onto a layer of mouths.
#include "ui/window/MainWindow.h"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>

#include "actions/manager/ActionManager.h"
#include "actions/dialogs/ShortcutsDialog.h"
#include "audio/sound/Sound.h"
#include "model/document/Commands.h"
#include "ui/panels/LayerPanel.h"
#include "ui/io/MovieExport.h"
#include "ui/panels/SubstitutionPanel.h"
#include "ui/window/ViewTitleBar.h"
#include "ui/window/WelcomeDialog.h"
#include "ui/panels/LibraryView.h"
#include "ui/views/LipSyncView.h"
#include "audio/lipsync/LipSync.h"

#include "ui/window/MainWindowPrivate.h"
#include <QStatusBar>

int MainWindow::detectLipSync(int soundLayer)
{
    Scene const& scene = m_document->scene();
    if (soundLayer < 0 || soundLayer >= scene.layers.size() || scene.layers[soundLayer].kind != Layer::Sound)
        return -1;
    std::shared_ptr<SoundData> const sound = Sound::load(scene.layers[soundLayer].soundFile);
    if (!sound)
        return -1;
    QString shapes = LipSync::detect(sound->samples, SoundData::kRate, scene.fps);
    // No more of it than the scene has frames for.
    shapes.truncate(std::max(0, scene.frameCount() - scene.layers[soundLayer].soundStart));
    m_document->setLipSync(soundLayer, shapes, tr("Auto Lip-Sync Detection"));
    return int(shapes.size());
}

void MainWindow::onActionAutoLipSync()
{
    int const sound = LipSyncView::soundLayerFor(m_document->scene(), layer());
    if (sound < 0) {
        statusBar()->showMessage(tr("Lip-sync is for a sound: import one first (File > Import > Sound)."), 7000);
        return;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    int const frames = detectLipSync(sound);
    QApplication::restoreOverrideCursor();
    if (frames < 0) {
        statusBar()->showMessage(tr("The sound file of %1 could not be read.").arg(m_document->scene().layers[sound].name), 7000);
        return;
    }
    statusBar()->showMessage(tr("%1: a mouth shape for each of %2 frames. Correct them in the Lip-Sync view, then use Map Lip-Sync to put the mouths on a layer.")
                                 .arg(m_document->scene().layers[sound].name).arg(frames), 10000);
    if (QDockWidget* const dock = dockOf("LipSyncDock"))
        showView(dock);
}

void MainWindow::onActionClearLipSync()
{
    int const sound = LipSyncView::soundLayerFor(m_document->scene(), layer());
    if (sound < 0 || m_document->scene().layers[sound].lipsync.isEmpty()) {
        statusBar()->showMessage(tr("There is no lip-sync to clear."), 5000);
        return;
    }
    m_document->setLipSync(sound, QString(), tr("Clear Lip-Sync"));
}

void MainWindow::onActionMouthShape(QString const& shape)
{
    int const sound = LipSyncView::soundLayerFor(m_document->scene(), layer());
    if (sound < 0 || shape.isEmpty()) {
        statusBar()->showMessage(tr("Lip-sync is for a sound: import one first (File > Import > Sound)."), 7000);
        return;
    }
    if (frame() < m_document->scene().layers[sound].soundStart) {
        statusBar()->showMessage(tr("The sound has not started yet on this frame."), 5000);
        return;
    }
    m_document->setMouth(sound, frame(), shape.at(0));
}

void MainWindow::onActionMapLipSync()
{
    Scene const& scene = m_document->scene();
    int const sound = LipSyncView::soundLayerFor(scene, layer());
    if (sound < 0 || scene.layers[sound].lipsync.isEmpty()) {
        statusBar()->showMessage(tr("There is no lip-sync to map yet: use Auto Lip-Sync Detection on a sound layer first."), 8000);
        return;
    }
    // The layers that have drawings to be mouths.
    QVector<int> layers;
    for (int l = int(scene.layers.size()) - 1; l >= 0; --l)
        if (scene.layers[l].holdsDrawings() && !scene.layers[l].drawings.isEmpty())
            layers.push_back(l);
    if (layers.isEmpty()) {
        statusBar()->showMessage(tr("Draw the mouths first: a drawing layer with one drawing for each shape."), 8000);
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Map Lip-Sync"));
    QFormLayout* const form = new QFormLayout(&dialog);
    QComboBox* const target = new QComboBox(&dialog);
    target->setObjectName(QStringLiteral("LipSyncTarget"));
    for (int l : std::as_const(layers))
        target->addItem(scene.layers[l].name, scene.layers[l].id);
    // The current layer, when it can be the one.
    if (layers.contains(layer()))
        target->setCurrentIndex(int(layers.indexOf(layer())));
    form->addRow(tr("Mouths on layer"), target);
    QVector<QComboBox*> choices;
    for (int i = 0; i < LipSync::kShapeCount; ++i) {
        QChar const shape = QLatin1Char(LipSync::kShapes[i]);
        QComboBox* const choice = new QComboBox(&dialog);
        choice->setObjectName(QStringLiteral("LipSyncDrawing") + shape);
        QLabel* const label = new QLabel(&dialog);
        label->setPixmap(LipSyncView::mouthIcon(shape, 34).pixmap(34, 34));
        label->setToolTip(LipSync::describe(shape));
        QHBoxLayout* const row = new QHBoxLayout;
        row->addWidget(label);
        row->addWidget(new QLabel(LipSync::describe(shape), &dialog), 1);
        row->addWidget(choice);
        form->addRow(row);
        choices.push_back(choice);
    }
    // The drawings of the layer chosen: one named after the shape; failing that the first for A, the second for B, and so on.
    auto fill = [&] {
        int const l = m_document->scene().layerIndex(target->currentData().toInt());
        int const count = l >= 0 ? int(m_document->scene().layers[l].drawings.size()) : 0;
        for (int i = 0; i < choices.size(); ++i) {
            choices[i]->clear();
            choices[i]->addItem(tr("(leave as it is)"), -1);
            // A drawing named after the mouth shape ("A", "B" ...) is the one for it.
            int named = -1;
            for (int d = 0; d < count; ++d) {
                QString const label = m_document->scene().layers[l].drawingLabel(d);
                choices[i]->addItem(tr("Drawing %1").arg(label), d);
                if (named < 0 && label.compare(QString(QLatin1Char(LipSync::kShapes[i])), Qt::CaseInsensitive) == 0)
                    named = d;
            }
            choices[i]->setCurrentIndex(named >= 0 ? named + 1 : i < count ? i + 1 : 0);
        }
    };
    fill();
    connect(target, &QComboBox::currentIndexChanged, &dialog, fill);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    QVector<int> drawingFor;
    for (QComboBox* const choice : std::as_const(choices))
        drawingFor.push_back(choice->currentData().toInt());
    int const to = m_document->scene().layerIndex(target->currentData().toInt());
    if (!m_document->mapLipSync(sound, to, drawingFor))
        statusBar()->showMessage(tr("No shape was given a drawing: nothing was mapped."), 6000);
    else
        statusBar()->showMessage(tr("The mouths of %1 follow the sound of %2.").arg(m_document->scene().layers[to].name, m_document->scene().layers[sound].name), 7000);
}

void MainWindow::playFrameSound(int frame)
{
    if (m_scrubMix.isEmpty())
        m_scrubMix = Sound::mix(m_document->scene());
    double const fps = std::max(m_document->scene().fps, 1.0);
    if (!m_scrubMix.isEmpty())
        m_player.play(m_scrubMix, qint64(frame / fps * SoundData::kRate), qint64(SoundData::kRate / fps));
}
