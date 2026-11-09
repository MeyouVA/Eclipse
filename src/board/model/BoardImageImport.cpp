#include "board/model/BoardDocument.h"
#include "board/model/BoardProjectJson.h"

#include <QColorSpace>
#include <QSet>
#include <algorithm>

namespace {
BoardLayer pictureLayer(BoardProject const& project, QString const& name, QImage const& image)
{
    double const scale = std::min({1.0, double(project.width) / image.width(), double(project.height) / image.height()});
    Picture picture;
    picture.image = image;
    picture.size = image.size();
    picture.ownProfile = image.colorSpace().isValid();
    picture.placement.translate((project.width - image.width() * scale) / 2,
                                (project.height - image.height() * scale) / 2);
    picture.placement.scale(scale, scale);
    Drawing drawing;
    drawing.pictures.append(std::move(picture));
    return {name, BoardLayer::Kind::Bitmap, true, std::move(drawing)};
}
}

bool BoardDocument::canEmbedPictures(QVector<QPair<QString, QImage>> const& images) const
{
    qint64 bytes = 0;
    QSet<qint64> counted;
    auto count = [&bytes, &counted](QImage const& image) {
        if (!counted.contains(image.cacheKey())) {
            counted.insert(image.cacheKey());
            bytes += image.sizeInBytes();
        }
    };
    for (auto const& scene : m_project.shots)
        for (auto const& item : scene.panels)
            for (auto const& layer : item.layers)
                for (auto const& picture : layer.drawing.pictures)
                    count(picture.image);
    for (auto const& layer : m_project.library)
        for (auto const& picture : layer.drawing.pictures)
            count(picture.image);
    for (auto const& [name, image] : images) {
        if (name.trimmed().isEmpty() || image.isNull() || image.sizeInBytes() > BoardProjectJson::maxEmbeddedImageBytes)
            return false;
        count(image);
        if (bytes > BoardProjectJson::maxEmbeddedImagesBytes)
            return false;
    }
    return true;
}

bool BoardDocument::addLibraryImages(QVector<QPair<QString, QImage>> const& images)
{
    if (m_path.isEmpty() || images.isEmpty() || images.size() > BoardProject::maxLibraryItems - m_project.library.size()
        || !canEmbedPictures(images))
        return false;
    auto next = m_project;
    for (auto const& [name, image] : images)
        appendLibraryLayer(next.library, pictureLayer(m_project, name, image));
    if (next.toBytes().isEmpty())
        return false;
    apply(std::move(next), tr("Import Images into Library"));
    return true;
}

bool BoardDocument::importPictures(int shot, int panel, QVector<QPair<QString, QImage>> const& images, int framesPerScene)
{
    if (!hasPanel(shot, panel) || images.isEmpty() || framesPerScene < 0 || framesPerScene > 1000000
        || (framesPerScene > 0 && !canGrow(images.size(), qint64(images.size()) * framesPerScene)) || !canEmbedPictures(images))
        return false;

    QVector<BoardLayer> layers;
    layers.reserve(images.size());
    for (auto const& [name, image] : images)
        layers.append(pictureLayer(m_project, name, image));

    auto next = m_project;
    if (framesPerScene == 0) {
        layers += next.shots[shot].panels[panel].layers;
        next.shots[shot].panels[panel].layers = std::move(layers);
        apply(std::move(next), tr("Import Images as Layers"));
        return true;
    }
    QSet<QString> names;
    for (auto const& scene : next.shots)
        names.insert(scene.name.toCaseFolded());
    QVector<BoardShot> imported;
    imported.reserve(layers.size());
    QString const sequence = next.shots[shot].sequence;
    QString const act = next.shots[shot].act;
    for (auto& layer : layers) {
        QString name = layer.name;
        for (int i = 2; names.contains(name.toCaseFolded()); ++i)
            name = tr("%1 (%2)").arg(layer.name).arg(i);
        names.insert(name.toCaseFolded());
        BoardPanel item = BoardProject::blankPanel(framesPerScene);
        item.layers.prepend(std::move(layer));
        imported.append({name, sequence, act, {std::move(item)}});
    }
    auto tail = next.shots.mid(shot + 1);
    next.shots.resize(shot + 1);
    next.shots += std::move(imported);
    next.shots += std::move(tail);
    apply(std::move(next), tr("Import Images as Scenes"));
    return true;
}
