// Eclipse - exporting for game engines: the frames of the scene packed in one picture, and a JSON (or XML) file saying
// where each one is - and, with the anchor on, the point of each frame a game places it by (the middle of the camera).
#include "ui/window/MainWindow.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPainter>
#include <QSaveFile>
#include <QSpinBox>
#include <QStatusBar>
#include <QXmlStreamWriter>
#include <cmath>

#include "canvas/FrameRender.h"
#include "model/document/Commands.h"
#include "render/images/ImageFiles.h"

bool MainWindow::publishSpriteSheet(QString const& path, int percent, QString* error, bool xml, bool anchor)
{
    Scene const scene = m_document->scene();
    int const frames = scene.frameCount();
    QSize const cell(std::max(1, scene.width * percent / 100), std::max(1, scene.height * percent / 100));
    int const columns = std::max(1, int(std::ceil(std::sqrt(double(frames)))));
    int const rows = (frames + columns - 1) / columns;
    QImage sheet(cell.width() * columns, cell.height() * rows, QImage::Format_ARGB32_Premultiplied);
    sheet.fill(Qt::transparent);
    QVector<QPoint> places;
    {
        QPainter p(&sheet);
        for (int f = 0; f < frames; ++f) {
            QPoint const at((f % columns) * cell.width(), (f / columns) * cell.height());
            p.drawImage(at, FrameRender::frame(scene, f, cell));
            places.push_back(at);
        }
    }
    if (!ImageFiles::save(sheet, path, QStringLiteral("png"), error))
        return false;
    // The anchor: where the middle of the camera is in each frame, as a share of the frame (0.5, 0.5).
    double const anchorX = 0.5, anchorY = 0.5;
    QByteArray description;
    if (xml) {
        // Export to Sprite Sheet (XML): a texture atlas, a sprite element for each frame.
        QXmlStreamWriter out(&description);
        out.setAutoFormatting(true);
        out.writeStartDocument();
        out.writeStartElement(QStringLiteral("TextureAtlas"));
        out.writeAttribute(QStringLiteral("imagePath"), QFileInfo(path).fileName());
        out.writeAttribute(QStringLiteral("width"), QString::number(sheet.width()));
        out.writeAttribute(QStringLiteral("height"), QString::number(sheet.height()));
        out.writeAttribute(QStringLiteral("fps"), QString::number(scene.fps));
        for (int f = 0; f < frames; ++f) {
            out.writeStartElement(QStringLiteral("sprite"));
            out.writeAttribute(QStringLiteral("n"), QStringLiteral("frame%1").arg(f + 1));
            out.writeAttribute(QStringLiteral("x"), QString::number(places[f].x()));
            out.writeAttribute(QStringLiteral("y"), QString::number(places[f].y()));
            out.writeAttribute(QStringLiteral("w"), QString::number(cell.width()));
            out.writeAttribute(QStringLiteral("h"), QString::number(cell.height()));
            if (anchor) {
                out.writeAttribute(QStringLiteral("pX"), QString::number(anchorX));
                out.writeAttribute(QStringLiteral("pY"), QString::number(anchorY));
            }
            out.writeEndElement();
        }
        out.writeEndElement();
        out.writeEndDocument();
    } else {
        QJsonArray placed;
        for (int f = 0; f < frames; ++f) {
            QJsonObject one{{"frame", f + 1}, {"x", places[f].x()}, {"y", places[f].y()}, {"w", cell.width()}, {"h", cell.height()}};
            if (anchor) {
                one["anchorX"] = anchorX;
                one["anchorY"] = anchorY;
            }
            placed.append(one);
        }
        QJsonObject const root{{"image", QFileInfo(path).fileName()}, {"fps", scene.fps}, {"frameWidth", cell.width()}, {"frameHeight", cell.height()},
                               {"columns", columns}, {"rows", rows}, {"frames", placed}};
        description = QJsonDocument(root).toJson();
    }
    QSaveFile file(QFileInfo(path).absoluteDir().filePath(QFileInfo(path).completeBaseName() + (xml ? QStringLiteral(".xml") : QStringLiteral(".json"))));
    if (!file.open(QIODevice::WriteOnly) || file.write(description) < 0 || !file.commit()) {
        if (error)
            *error = file.errorString();
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
    QDialog ask(this);
    ask.setWindowTitle(tr("Publish for Game Engine"));
    QFormLayout* const form = new QFormLayout(&ask);
    QSpinBox* const size = new QSpinBox(&ask);
    size->setRange(5, 200);
    size->setSingleStep(5);
    size->setValue(50);
    size->setSuffix(tr(" %"));
    QComboBox* const kind = new QComboBox(&ask);
    kind->addItem(tr("Export to JSON Sprite Sheets"));
    kind->addItem(tr("Export to Sprite Sheet (XML)"));
    QCheckBox* const anchor = new QCheckBox(tr("Toggle Anchor"), &ask);
    anchor->setChecked(true);
    anchor->setToolTip(tr("Each sprite's anchor (the point a game places it by: the middle of the camera) written with it."));
    form->addRow(tr("Size of each frame (% of the scene):"), size);
    form->addRow(tr("Format"), kind);
    form->addRow(QString(), anchor);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &ask);
    connect(buttons, &QDialogButtonBox::accepted, &ask, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &ask, &QDialog::reject);
    form->addRow(buttons);
    if (ask.exec() != QDialog::Accepted)
        return;
    QString error;
    bool const xml = kind->currentIndex() == 1;
    if (!publishSpriteSheet(path, size->value(), &error, xml, anchor->isChecked())) {
        QMessageBox::warning(this, tr("Publish for Game Engine"), error);
        return;
    }
    statusBar()->showMessage((xml ? tr("Sprite sheet and its XML written: %1") : tr("Sprite sheet and its JSON written: %1")).arg(QDir::toNativeSeparators(path)), 6000);
}
