#include "board/model/BoardCamera.h"
#include "board/model/BoardDocument.h"

#include <algorithm>
#include <cmath>

bool BoardCamera::valid(BoardCameraPose const& pose)
{
    return std::abs(pose.offset.x()) <= offsetLimit && std::abs(pose.offset.y()) <= offsetLimit
        && pose.zoom >= minZoom && pose.zoom <= maxZoom && std::abs(pose.rotation) <= rotationLimit;
}

qsizetype BoardCamera::lowerKey(QVector<BoardCameraKey> const& keys, int frame)
{
    auto const found = std::lower_bound(keys.cbegin(), keys.cend(), frame,
        [](BoardCameraKey const& key, int target) { return key.frame < target; });
    return found - keys.cbegin();
}

BoardCameraPose BoardCamera::poseAt(QVector<BoardCameraKey> const& keys, int frame)
{
    if (keys.isEmpty())
        return {};
    qsizetype const i = lowerKey(keys, frame);
    if (i == 0)
        return keys.first().pose;
    if (i == keys.size())
        return keys.last().pose;
    auto const& from = keys[i - 1];
    auto const& to = keys[i];
    double const t = double(frame - from.frame) / (to.frame - from.frame);
    return {{std::lerp(from.pose.offset.x(), to.pose.offset.x(), t),
             std::lerp(from.pose.offset.y(), to.pose.offset.y(), t)},
            std::lerp(from.pose.zoom, to.pose.zoom, t),
            std::lerp(from.pose.rotation, to.pose.rotation, t)};
}

QVector<BoardCameraKey> BoardCamera::slice(QVector<BoardCameraKey> const& keys, int first, int length)
{
    if (keys.isEmpty())
        return {};
    int const last = length - 1;
    auto result = keys.mid(lowerKey(keys, first), lowerKey(keys, first + length) - lowerKey(keys, first));
    for (auto& key : result)
        key.frame -= first;
    auto const beginPose = poseAt(keys, first);
    auto const endPose = poseAt(keys, first + last);
    if (result.isEmpty() || poseAt(result, 0) != beginPose)
        result.prepend({0, beginPose});
    if (poseAt(result, last) != endPose)
        result.append({last, endPose});
    return result;
}

bool BoardDocument::setCameraKey(int shot, int panel, int frame, BoardCameraPose const& pose)
{
    auto const* current = this->panel(shot, panel);
    if (!current || frame < 0 || frame >= current->length || !BoardCamera::valid(pose))
        return false;
    qsizetype const i = BoardCamera::lowerKey(current->camera, frame);
    bool const exists = i < current->camera.size() && current->camera[i].frame == frame;
    if (exists && current->camera[i].pose == pose)
        return false;
    auto next = m_project;
    auto& keys = next.shots[shot].panels[panel].camera;
    if (exists)
        keys[i].pose = pose;
    else
        keys.insert(i, {frame, pose});
    apply(std::move(next), exists ? tr("Edit Camera Key") : tr("Add Camera Key"));
    return true;
}

bool BoardDocument::removeCameraKey(int shot, int panel, int frame)
{
    auto const* current = this->panel(shot, panel);
    if (!current)
        return false;
    qsizetype const i = BoardCamera::lowerKey(current->camera, frame);
    if (i == current->camera.size() || current->camera[i].frame != frame)
        return false;
    auto next = m_project;
    next.shots[shot].panels[panel].camera.removeAt(i);
    apply(std::move(next), tr("Remove Camera Key"));
    return true;
}
