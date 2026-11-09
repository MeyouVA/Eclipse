#pragma once

#include "canvas/export/StoryboardPdf.h"

namespace StoryboardPdf {
ECLIPSE_UI_API bool write(Scene const& scene, Profile const& profile, QString const& path, QString const& sceneFile = {},
                          QString* error = nullptr, int* pages = nullptr);
}
