#ifndef TNC_SDL3_FRAME_PRESENTER_H
#define TNC_SDL3_FRAME_PRESENTER_H

#include <SDL3/SDL.h>

/** Affiche une SDL_Surface (RGBA/index converti) via un renderer SDL3. */
class Sdl3FramePresenter {
public:
    bool init(SDL_Window *window, SDL_Renderer *renderer);
    void shutdown();
    void present(SDL_Surface *frame, int logicalW, int logicalH);

private:
    SDL_Renderer *renderer_{nullptr};
    SDL_Texture *texture_{nullptr};
    int texW_{0};
    int texH_{0};
};

#endif
