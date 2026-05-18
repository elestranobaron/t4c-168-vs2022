#pragma once

#include <SDL3/SDL.h>

#include <string>

#include "render/Sdl3FramePresenter.h"

class FontManager;
class MAPInterface;
class NPCManager;
class TextManager;
class VSFInterface;

/** Vue monde isométrique TnC (VSF + MapInterface) après authentification réseau. */
class GameWorldScreen {
   public:
    static constexpr int kLogicalWidth = 1800;
    static constexpr int kLogicalHeight = 1000;

    GameWorldScreen() = default;
    ~GameWorldScreen();

    GameWorldScreen(const GameWorldScreen &) = delete;
    GameWorldScreen &operator=(const GameWorldScreen &) = delete;

    /** Charge VSF/carte ; false si data/sprites ou data/maps manquants. */
    bool Init(SDL_Renderer *renderer, SDL_Window *window);

    /** Comme Init, avec position initiale serveur (opcode 13). */
    bool Init(SDL_Renderer *renderer, SDL_Window *window, unsigned int locX, unsigned int locY,
              unsigned short zone);

    void Shutdown();

    bool IsReady() const { return ready_; }

    const std::string &GetLastError() const { return lastError_; }

    bool HandleEvent(const SDL_Event &event);

    /** true une fois après SDLK_ESCAPE en monde (retour écran login). */
    bool ConsumeReturnToLogin();

    void Update();

    void Render(SDL_Renderer *renderer);

   private:
    static char *DupCStr(const std::string &s);
    void redraw();
    SDL_Surface *makeLayer(int w, int h, bool with_alpha);

    SDL_Renderer *renderer_{nullptr};
    SDL_Window *window_{nullptr};
    Sdl3FramePresenter presenter_;

    SDL_Surface *screen_{nullptr};
    SDL_Surface *sol_{nullptr};
    SDL_Surface *decor_{nullptr};
    SDL_Surface *env_{nullptr};

    VSFInterface *vsfi_{nullptr};
    MAPInterface *mapi_{nullptr};
    FontManager *fm_{nullptr};
    FontManager *fm2_{nullptr};
    TextManager *txtm_{nullptr};
    NPCManager *npcm_{nullptr};

    unsigned int locX_{2880};
    unsigned int locY_{1083};
    unsigned short zone_{0};
    bool mapFlag_{true};
    bool dispInfos_{false};
    bool ready_{false};
    bool returnToLogin_{false};
    float fps_{0.f};
    std::string dataRoot_;
    std::string lastError_;
};
