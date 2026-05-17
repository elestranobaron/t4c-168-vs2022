#ifndef TNC_SDL_IMAGE_H
#define TNC_SDL_IMAGE_H
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

/* IMG_Load → SDL3_image */
#undef IMG_Load
#define IMG_Load(path) IMG_Load(path)

#endif
