// Eclipse - exporting for game engines: the frames of the scene packed in one picture, and a JSON file saying where each one is.
#include "ui/window/MainWindow.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPainter>
#include <QSaveFile>
#include <QStatusBar>
#include <cmath>

#include "canvas/FrameRender.h"
#include "model/document/Commands.h"
#include "render/images/ImageFiles.h"

bool MainWindow::publishSpriteSheet(QString const& path, int percent, QString* error)
{
    Scene const scene = m_document->scene();
    int const frames = scene.frameCount();
    QSize const cell(std::max(1, scene.width * percent / 100), std::max(1, scene.height * percent / 100));
    int const columns = std::max(1, int(std::ceil(std::sqrt(double(frames)))));
    int const rows = (frames + columns - 1) / columns;
    QImage sheet(cell.width() * columns, cell.height() * rows, QImage::Format_ARGB32_Premultiplied);
    sheet.fill(Qt::transparent);
    QJsonArray placed;
    {
        QPainter p(&sheet);
        for (int f = 0; f < frames; ++f) {
            QPoint const at((f % columns) * cell.width(), (f / columns) * cell.height());
            p.drawImage(at, FrameRender::frame(scene, f, cell));
            placed.append(QJsonObject{{"frame", f + 1}, {"x", at.x()}, {"y", at.y()}, {"w", cell.width()}, {"h", cell.height()}});
        }
    }
    if (!ImageFiles::save(sheet, path, QStringLiteral("png"), error))
        return false;
    QJsonObject const description{{"image", QFileInfo(path).fileName()}, {"fps", scene.fps}, {"frameWidth", cell.width()}, {"frameHeight", cell.height()},
                                  {"columns", columns}, {"rows", rows}, {"frames", placed}};
    QSaveFile json(QFileInfo(path).absoluteDir().filePath(QFileInfo(path).completeBaseName() + QStringLiteral(".json")));
    if (!json.open(QIODevice::WriteOnly) || json.write(QJsonDocument(description).toJson()) < 0 || !json.commit()) {
        if (error)
            *error = json.errorString();
        return false;
    }
    return true;
}

void MainWindow::onActionPublishGameEngine()
{
    QString start = m_path.isEmpty() ? QStringLiteral("sprites.png")
                                     : QFileInfo(m_path).path() + QStringLiteral("/frames/") + QFileInfo(m_path).completeBaseName() + QStringLiteral("_sprites.png");
    QString path = QFileDialog::getSaveFileName(this, tr("Publish for Game Engine"), start, tr("Sprite Sheet (*.png)"));
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().isEmpty())
        path += QStringLiteral(".png");
    bool ok = false;
    int const percent = QInputDialog::getInt(this, tr("Publish for Game Engine"), tr("Size of each frame (% of the scene):"), 50, 5, 200, 5, &ok);
    if (!ok)
        return;
    QString error;
    if (!publishSpriteSheet(path, percent, &error)) {
        QMessageBox::warning(this, tr("Publish for Game Engine"), error);
        return;
    }
    statusBar()->showMessage(tr("Sprite sheet and its JSON written: %1").arg(QDir::toNativeSeparators(path)), 6000);
}
