#include "board/model/BoardDocument.h"
#include "board/model/BoardProjectJson.h"

#include <QCoreApplication>
#include <QTransform>
#include <cmath>
#include <limits>

namespace {
constexpr qint64 kPanelLimit = 1000000;

bool fail(QString* error, char const* message)
{
    if (error)
        *error = QCoreApplication::translate("BoardProject", message);
    return false;
}

void transformDrawing(Drawing& drawing, QTransform const& t)
{
    for (Picture& picture : drawing.pictures)
        picture.transform(t);
    for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
        for (Stroke& stroke : drawing.strokesOf(sheet))
            stroke.transform(t);
        for (Fill& fill : drawing.fillsOf(sheet))
            fill.transform(t);
    }
    if (drawing.pivotSet)
        drawing.pivot = t.map(drawing.pivot);
}

qint64 scaled(qint64 frames, double ratio)
{
    return std::llround(double(frames) * ratio);
}

bool retimePanels(BoardProject& project, double ratio, QString* error)
{
    qint64 oldEnd = 0;
    qint64 newEnd = 0;
    for (BoardShot& shot : project.shots) {
        int const firstLength = shot.panels.isEmpty() ? 0 : shot.panels.front().length;
        for (BoardPanel& panel : shot.panels) {
            qint64 const start = newEnd;
            oldEnd += panel.length;
            newEnd = std::max(start + 1, scaled(oldEnd, ratio));
            qint64 const length = newEnd - start;
            if (length > kPanelLimit || newEnd > std::numeric_limits<int>::max())
                return fail(error, "The new frame rate would make the board too long.");
            QVector<BoardCameraKey> keys;
            for (BoardCameraKey key : panel.camera) {
                key.frame = int(std::clamp<qint64>(scaled(key.frame, ratio), 0, length - 1));
                if (!keys.isEmpty() && keys.back().frame == key.frame)
                    keys.back() = key;
                else
                    keys.push_back(key);
            }
            panel.camera = keys;
            panel.length = int(length);
        }
        if (shot.dissolve > 0 && firstLength > 0)
            shot.dissolve = int(std::clamp<qint64>(scaled(shot.dissolve, ratio), 1, shot.panels.front().length));
    }
    return true;
}

bool retimeSound(BoardSound& sound, double ratio, bool keepStart)
{
    if (sound.asset.isEmpty())
        return true;
    if (!keepStart)
        sound.start = int(std::min<qint64>(scaled(sound.start, ratio), std::numeric_limits<int>::max()));
    sound.sourceOffset = int(std::min<qint64>(scaled(sound.sourceOffset, ratio), std::numeric_limits<int>::max()));
    sound.length = int(std::clamp<qint64>(scaled(sound.length, ratio), 1, std::numeric_limits<int>::max()));
    return BoardProjectJson::validSound(sound);
}
}

bool BoardProject::reformat(BoardFormat const& format, QString* error)
{
    if (!validFormat(format.width, format.height, format.fps))
        return fail(error, "Enter a valid width, height and frame rate.");
    BoardProject next = *this;
    if (format.width != width || format.height != height) {
        double const scale = format.art == BoardFormat::Art::ScaleToFit
            ? std::min(double(format.width) / width, double(format.height) / height) : 1.0;
        QTransform const t = QTransform::fromTranslate(-width / 2.0, -height / 2.0) * QTransform::fromScale(scale, scale)
            * QTransform::fromTranslate(format.width / 2.0, format.height / 2.0);
        for (BoardShot& shot : next.shots)
            for (BoardPanel& panel : shot.panels) {
                for (BoardLayer& layer : panel.layers)
                    transformDrawing(layer.drawing, t);
                for (BoardCameraKey& key : panel.camera)
                    key.pose.offset *= scale;
            }
        for (BoardLayer& item : next.library)
            transformDrawing(item.drawing, t);
        next.width = format.width;
        next.height = format.height;
    }
    if (format.fps != fps) {
        double const ratio = format.fps / fps;
        bool const keepFrames = format.timing == BoardFormat::Timing::KeepFrames;
        if (!keepFrames && !retimePanels(next, ratio, error))
            return false;
        if (!retimeSound(next.sound, ratio, keepFrames))
            return fail(error, "The sound track cannot be timed at the new frame rate.");
        next.fps = format.fps;
    }
    for (BoardShot const& shot : next.shots)
        for (BoardPanel const& panel : shot.panels)
            for (BoardCameraKey const& key : panel.camera)
                if (!BoardCamera::valid(key.pose))
                    return fail(error, "A camera key would leave the allowed range.");
    *this = std::move(next);
    return true;
}

bool BoardDocument::setFormat(BoardFormat const& format, QString* error)
{
    if (m_path.isEmpty())
        return false;
    if (format.width == m_project.width && format.height == m_project.height && format.fps == m_project.fps)
        return false;
    BoardProject next = m_project;
    if (!next.reformat(format, error))
        return false;
    apply(std::move(next), tr("Change Project Format"));
    return true;
}
