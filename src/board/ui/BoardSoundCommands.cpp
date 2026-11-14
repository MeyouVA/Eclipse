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
#include <QTemporaryDir>
#include <QUuid>
#include <cmath>
#include <limits>

void BoardWindow::editSound(QString const& operation)
{
    m_playback->stop();
    bool const relink = operation == QLatin1String("relink");
    if (relink || operation == QLatin1String("import")) {
        importBoardSound(relink);
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

void BoardWindow::importBoardSound(bool relink)
{
    auto const& project = m_document.project();
    BoardSound const previous = project.sound;
    if (m_document.path().isEmpty() || (relink && previous.asset.isEmpty()))
        return;
    QString const title = relink ? tr("Relink Sound") : tr("Import Sound");
    QDir const folder = QFileInfo(m_document.path()).dir();
    QString const path = QFileDialog::getOpenFileName(this, title, folder.path(), MediaFormats::audioFilter());
    if (path.isEmpty())
        return;
    QFileInfo const source(path);
    QString asset = QUuid::createUuid().toString(QUuid::Id128);
    static QRegularExpression const extension(QStringLiteral("\\A[A-Za-z0-9]{1,12}\\z"));
    if (extension.match(source.suffix()).hasMatch())
        asset += QLatin1Char('.') + source.suffix();
    QTemporaryDir staged(folder.filePath(QStringLiteral(".board-sound-XXXXXX")));
    QString const candidate = staged.filePath(asset);
    auto copyError = [&] { QMessageBox::warning(this, title, tr("Could not copy the sound into the project folder.")); };
    if (!staged.isValid() || !QFile::copy(path, candidate)) {
        copyError();
        return;
    }
    QString error;
    auto const decoded = Sound::load(candidate, &error);
    if (!decoded || decoded->frames() <= 0) {
        QMessageBox::warning(this, title, error.isEmpty() ? tr("The file does not contain usable audio.") : error);
        return;
    }
    double const sourceFrames = std::ceil(decoded->seconds() * project.fps);
    if (relink && sourceFrames < qint64(previous.sourceOffset) + previous.length) {
        QMessageBox::warning(this, title, tr("The replacement sound is too short for the clip’s current source range."));
        return;
    }

    QFileInfo const audio(folder.filePath(QStringLiteral("audio")));
    QDir const destination(audio.absoluteFilePath());
    QString const copy = destination.filePath(asset);
    if (audio.isSymLink() || audio.isJunction() || !folder.mkpath(QStringLiteral("audio")) || !QFile::rename(candidate, copy)) {
        copyError();
        return;
    }
    int const start = m_timeline->frame();
    int const duration = int(std::clamp(sourceFrames, 1.0, double(std::numeric_limits<int>::max() - start)));
    QString const name = source.completeBaseName().trimmed().left(256);
    bool const applied = relink ? m_document.relinkSound(asset)
        : m_document.setSound({asset, name.isEmpty() ? tr("Sound") : name, start, duration, false});
    if (!applied) {
        QFile::remove(copy);
        QMessageBox::warning(this, title, tr("Could not apply the sound to this project."));
    }
}
