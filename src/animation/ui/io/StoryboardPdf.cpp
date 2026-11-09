#include "animation/ui/io/StoryboardPdf.h"
#include "animation/ui/views/StoryboardView.h"

bool StoryboardPdf::write(Scene const& scene, Profile const& profile, QString const& path, QString const& sceneFile,
                          QString* error, int* pages)
{
    return writeWithFrames(scene, profile, path, sceneFile, StoryboardView::naming(), {}, error, pages);
}
