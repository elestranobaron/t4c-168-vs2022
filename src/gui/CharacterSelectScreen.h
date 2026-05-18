#pragma once

#include <SDL3/SDL.h>

#include <string>
#include <vector>

/** Ecran liste persos (apres auth, avant opcode 13). */
class CharacterSelectScreen {
   public:
    static constexpr int kLogicalWidth = 800;
    static constexpr int kLogicalHeight = 600;

    explicit CharacterSelectScreen(SDL_Renderer *renderer);

    bool HandleEvent(const SDL_Event &event, SDL_Window *window);

    void Update();

    void Render(SDL_Renderer *renderer);

    /** false = retour login (Esc). */
    bool ShouldStay() const { return stay_; }

    const std::string &GetStatusLine() const { return statusLine_; }

   private:
    void refreshFromSession();
    void tryEnterWorld(SDL_Window *window);

    SDL_Renderer *renderer_{nullptr};
    bool stay_{true};
    int selectedIndex_{0};
    std::vector<std::string> displayLines_;
    std::string statusLine_;
    bool statusLocked_{false};
};
