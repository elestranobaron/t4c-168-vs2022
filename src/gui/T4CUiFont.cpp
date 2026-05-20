#include "gui/T4CUiFont.h"

#include "game/TncDataPaths.h"

#include <SDL3_ttf/SDL_ttf.h>

#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

std::string ResolveLauncherFontPath() {
    static const char *kCandidates[] = {
        "fonts/t4cbeaulieux.ttf",
        "fonts/T4C_Beaulieux/t4cbeaulieux.ttf",
    };
    for (const char *rel : kCandidates) {
        const std::string path = T4CDataPath(rel);
        if (!path.empty() && fs::is_regular_file(path)) {
            return path;
        }
    }
    return {};
}

}  // namespace

T4CUiFont::~T4CUiFont() {
    shutdown();
}

bool T4CUiFont::init(const float pointSize) {
    shutdown();
    pointSize_ = pointSize;

    const std::string path = ResolveLauncherFontPath();
    if (path.empty()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "[T4CUiFont] t4cbeaulieux.ttf introuvable sous T4C_DATA/fonts/");
        return false;
    }

    font_ = TTF_OpenFont(path.c_str(), pointSize_);
    if (!font_) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[T4CUiFont] TTF_OpenFont echoue: %s", SDL_GetError());
        return false;
    }
    SDL_Log("[T4CUiFont] police chargee: %s (%.0f pt)", path.c_str(), pointSize_);
    return true;
}

void T4CUiFont::shutdown() {
    if (font_) {
        TTF_CloseFont(font_);
        font_ = nullptr;
    }
}

void T4CUiFont::measureText(const char *text, int *outW, int *outH) const {
    if (outW) {
        *outW = 0;
    }
    if (outH) {
        *outH = 0;
    }
    if (!font_ || !text) {
        return;
    }
    TTF_GetStringSize(font_, text, std::strlen(text), outW, outH);
}

void T4CUiFont::drawText(SDL_Renderer *renderer, const char *text, const float x, const float y,
                         const SDL_Color color) const {
    if (!renderer || !font_ || !text || !*text) {
        return;
    }
    const size_t len = std::strlen(text);
    SDL_Surface *surf = TTF_RenderText_Blended(font_, text, len, color);
    if (!surf) {
        return;
    }
    SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surf);
    const float tw = static_cast<float>(surf->w);
    const float th = static_cast<float>(surf->h);
    SDL_DestroySurface(surf);
    if (!tex) {
        return;
    }
    SDL_FRect dst{x, y, tw, th};
    SDL_RenderTexture(renderer, tex, nullptr, &dst);
    SDL_DestroyTexture(tex);
}
