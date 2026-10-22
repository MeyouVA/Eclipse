// Eclipse - the windows that make a texture of your own: from a picture, or put together from a few settings.
#pragma once

class QWidget;

namespace TextureDialogs {

// Asks for a picture and how to read it; returns the number of the texture kept, or 0.
int importTexture(QWidget* parent);
// Lets a grain be put together; returns the number of the texture kept, or 0.
int newTexture(QWidget* parent);

}
