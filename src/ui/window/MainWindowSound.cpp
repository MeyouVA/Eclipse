// Eclipse - the main window: the Sound Editor, sound clips, silent cells, and hearing one sound layer.
#include "ui/window/MainWindow.h"

#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <QStatusBar>

#include "audio/sound/Sound.h"
#include "model/media/MediaFormats.h"
#include "model/document/Commands.h"
#include "ui/timeline/Timeline.h"
#include "ui/views/LipSyncView.h"
#include "ui/views/SoundEditor.h"
#include "ui/io/RawSoundImport.h"

SoundEditor* MainWindow::openSoundEditor(int layer)
{
    if (!m_soundEditor) {
        m_soundEditor = new SoundEditor(m_document, this);
        m_soundEditor->setAttribute(Qt::WA_DeleteOnClose);
        connect(m_soundEditor, &SoundEditor::detectLipSync, this, &MainWindow::onActionAutoLipSync);
        connect(m_soundEditor, &SoundEditor::mapLipSync, this, &MainWindow::onActionMapLipSync);
        connect(m_soundEditor, &SoundEditor::mouthShape, this, &MainWindow::onActionMouthShape);
        connect(m_soundEditor, &SoundEditor::playLayer, this, &MainWindow::playSoundLayer);
        connect(m_soundEditor, &SoundEditor::importClip, this, [this](int l) {
            QString path = QFileDialog::getOpenFileName(this, tr("Select Sound File"), QFileInfo(m_path).path(),
                                                              MediaFormats::audioFilter(), nullptr, QFileDialog::HideNameFilterDetails);
            QString error;
            if(!path.isEmpty()) path=RawSoundImport::prepare(this,path,m_path,&error);
            if(path.isEmpty() && !error.isEmpty()) QMessageBox::critical(this,tr("Import Sound Clip"),error);
            if (!path.isEmpty() && importSoundClip(l, path, &error) < 0)
                QMessageBox::critical(this, tr("Import Sound Clip"), error);
        });
        connect(m_timeline, &Timeline::currentCellChanged, m_soundEditor, [this](int, int f) {
            if (m_soundEditor)
                m_soundEditor->setFrame(f);
        });
    }
    m_soundEditor->setLayer(layer);
    m_soundEditor->setFrame(frame());
    return m_soundEditor;
}

void MainWindow::onActionSoundEditor()
{
    int const sound = LipSyncView::soundLayerFor(m_document->scene(), layer());
    if (sound < 0) {
        statusBar()->showMessage(tr("No Sound Available: import one first (File > Import > Sound)."), 6000);
        return;
    }
    if (sound != layer())
        m_timeline->setCurrentCell(sound, frame());
    SoundEditor* const editor = openSoundEditor(sound);
    editor->show();
    editor->raise();
}

int MainWindow::importSoundClip(int soundLayer, QString const& path, QString* error)
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    std::shared_ptr<SoundData> const sound = Sound::load(path, error);
    QApplication::restoreOverrideCursor();
    if (!sound)
        return -1;
    int const at = m_document->addSoundClip(soundLayer, path, frame());
    if (at < 0 && error)
        *error = tr("Please select inside a sound column.");
    return at;
}

void MainWindow::onActionSplitSoundClip()
{
    QString why;
    int const selected=m_soundEditor && m_soundEditor->isVisible() && m_soundEditor->layer()==layer()?m_soundEditor->currentClip():-1;
    if (!m_document->splitSoundClip(layer(), frame(), &why,selected))
        statusBar()->showMessage(why.isEmpty() ? tr("Please select inside a sound column.") : why, 5000);
}

void MainWindow::onActionInsertSilentCells()
{
    if (layer() < 0 || m_document->scene().layers[layer()].kind != Layer::Sound) {
        statusBar()->showMessage(tr("Please select inside a sound column."), 5000);
        return;
    }
    bool ok = false;
    int const count = QInputDialog::getInt(this, tr("Insert Silent Cells in Sound"), tr("Number of silent frames:"), 1, 1, 100000, 1, &ok);
    if (ok) {
        QString why;
        if(!m_document->insertSilentCells(layer(),frame(),count,&why)) statusBar()->showMessage(why,8000);
    }
}

void MainWindow::playSoundLayer(int soundLayer, int from)
{
    Scene const& scene = m_document->scene();
    if (soundLayer < 0 || soundLayer >= scene.layers.size() || scene.layers[soundLayer].kind != Layer::Sound)
        return;
    std::shared_ptr<SoundData> const track = Sound::track(scene.layers[soundLayer], scene.fps);
    if (!track)
        return;
    qint64 const first = qint64(std::max(0, scene.layers[soundLayer].soundFrameAt(from)) / std::max(1.0, scene.fps) * SoundData::kRate);
    m_player.play(track->samples, first);
}
