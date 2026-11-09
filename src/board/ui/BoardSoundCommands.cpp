#include "board/ui/BoardWindow.h"
#include "board/ui/BoardPlayback.h"
#include "board/ui/BoardTimeline.h"
#include "audio/sound/Sound.h"
#include "model/media/MediaFormats.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <QRegularExpression>
#include <QUuid>
#include <cmath>
#include <limits>

void BoardWindow::editSound(QString const& operation)
{
    m_playback->stop();
    if (operation == QLatin1String("import")) {
        importBoardSound();
        return;
    }
    BoardSound sound = m_document.project().sound;
    if (sound.asset.isEmpty())
        return;
    if (operation == QLatin1String("remove")) {
        m_document.setSound({});
    } else if (operation == QLatin1String("mute")) {
        sound.muted = !sound.muted;
        m_document.setSound(sound);
    } else {
        int const maximum = std::min(m_timeline->length(), std::numeric_limits<int>::max() - sound.length + 1);
        bool accepted = false;
        int const frame = QInputDialog::getInt(this, tr("Sound Start Frame"), tr("Start frame"),
            std::clamp(sound.start + 1, 1, maximum), 1, maximum, 1, &accepted);
        if (accepted) {
            sound.start = frame - 1;
            m_document.setSound(sound);
        }
    }
}

void BoardWindow::importBoardSound()
{
    if (m_document.path().isEmpty())
        return;
    QDir const folder = QFileInfo(m_document.path()).dir();
    QString const path = QFileDialog::getOpenFileName(this, tr("Import Sound"), folder.path(), MediaFormats::audioFilter());
    if (path.isEmpty())
        return;
    QFileInfo const source(path);
    QString asset = QUuid::createUuid().toString(QUuid::Id128);
    static QRegularExpression const extension(QStringLiteral("\\A[A-Za-z0-9]{1,12}\\z"));
    if (extension.match(source.suffix()).hasMatch())
        asset += QLatin1Char('.') + source.suffix();
    QString const copy = folder.absoluteFilePath(QStringLiteral("audio/") + asset);
    if (!folder.mkpath(QStringLiteral("audio")) || !QFile::copy(path, copy)) {
        QMessageBox::warning(this, tr("Import Sound"), tr("Could not copy the sound into the project folder."));
        return;
    }
    QString error;
    auto const decoded = Sound::load(copy, &error);
    if (decoded && decoded->frames() > 0) {
        int const start = m_timeline->frame();
        int const duration = int(std::clamp(std::ceil(decoded->seconds() * m_document.project().fps),
                                            1.0, double(std::numeric_limits<int>::max() - start)));
        QString const name = source.completeBaseName().trimmed().left(256);
        if (m_document.setSound({asset, name.isEmpty() ? tr("Sound") : name, start, duration, false}))
            return;
    }
    QFile::remove(copy);
    QMessageBox::warning(this, tr("Import Sound"), error.isEmpty() ? tr("The file does not contain usable audio.") : error);
}
