#include "Sdl3FramePresenter.h"

bool Sdl3FramePresenter::init(SDL_Window *window, SDL_Renderer *renderer) {
    (void)window;
    renderer_ = renderer;
    return renderer_ != nullptr;
}

void Sdl3FramePresenter::shutdown() {
    if (texture_) {
        SDL_DestroyTexture(texture_);
        texture_ = nullptr;
    }
    renderer_ = nullptr;
}

void Sdl3FramePresenter::present(SDL_Surface *frame, int logicalW, int logicalH) {
    if (!renderer_ || !frame) {
        return;
    }

    SDL_Surface *rgb = frame;
    SDL_Surface *converted = nullptr;
    if (frame->format != SDL_PIXELFORMAT_RGBA32) {
        converted = SDL_ConvertSurface(frame, SDL_PIXELFORMAT_RGBA32);
        rgb = converted;
    }
    if (!rgb) {
        return;
    }

    if (!texture_ || texW_ != rgb->w || texH_ != rgb->h) {
        if (texture_) {
            SDL_DestroyTexture(texture_);
        }
        texture_ = SDL_CreateTextureFromSurface(renderer_, rgb);
        texW_ = rgb->w;
        texH_ = rgb->h;
    } else {
        SDL_UpdateTexture(texture_, nullptr, rgb->pixels, rgb->pitch);
    }

    if (converted) {
        SDL_DestroySurface(converted);
    }
    if (!texture_) {
        return;
    }

    SDL_SetTextureBlendMode(texture_, SDL_BLENDMODE_BLEND);
    SDL_SetTextureColorModFloat(texture_, 1.f, 1.f, 1.f);
    SDL_SetTextureAlphaMod(texture_, 255);

    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);

    const float scale = brightnessScale_;
    if (scale > 1.001f || scale < 0.999f) {
        SDL_SetRenderColorScale(renderer_, scale);
    }
    SDL_FRect dst{0.f, 0.f, static_cast<float>(logicalW), static_cast<float>(logicalH)};
    SDL_RenderTexture(renderer_, texture_, nullptr, &dst);
    if (scale > 1.001f || scale < 0.999f) {
        SDL_SetRenderColorScale(renderer_, 1.f);
    }
    SDL_RenderPresent(renderer_);
}
