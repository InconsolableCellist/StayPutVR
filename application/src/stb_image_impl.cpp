// Single translation unit that compiles the stb_image implementation, used to
// decode the avatar effigy PNG for the Devices > Visual assignment view.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include "stb/stb_image.h"
