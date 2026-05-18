#include "gui/CharacterSelectScreen.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <string>

#if defined(LINUX_PORT)
#include "network/T4CLoginSession.h"
#endif

namespace {

constexpr float kPaddingX = 48.0f;
constexpr float kListY = 140.0f;
constexpr float kRowH = 28.0f;

bool KeyDown(const SDL_Event &event, SDL_Scancode sc) {
    return event.type == SDL_EVENT_KEY_DOWN && event.key.down && !event.key.repeat &&
           event.key.scancode == sc;
}

}  // namespace

CharacterSelectScreen::CharacterSelectScreen(SDL_Renderer *renderer) : renderer_(renderer) {
    statusLine_ = "Chargement liste…";
}

void CharacterSelectScreen::refreshFromSession() {
#if defined(LINUX_PORT)
    std::vector<T4CCharacterSlot> slots;
    int maxPerAccount = 0;
    T4CLoginSessionCopyCharacterList(&slots, &maxPerAccount);

    displayLines_.clear();
    if (slots.empty()) {
        displayLines_.push_back("(aucun personnage sur ce compte)");
        selectedIndex_ = 0;
        if (!statusLocked_) {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "Max %d persos/compte — creation non implementee", maxPerAccount);
            statusLine_ = buf;
        }
        return;
    }

    for (std::size_t i = 0; i < slots.size(); ++i) {
        const T4CCharacterSlot &s = slots[i];
        char line[128];
        std::snprintf(line, sizeof(line), "%s  (race %u, niv %u)", s.name.c_str(),
                      static_cast<unsigned>(s.race), static_cast<unsigned>(s.level));
        displayLines_.push_back(line);
    }
    selectedIndex_ = std::clamp(selectedIndex_, 0, static_cast<int>(slots.size()) - 1);

    if (!statusLocked_) {
        statusLine_ = "Entree : entrer en jeu — Fleches : choisir — Esc : retour login";
    }

    if (T4CLoginSessionHasPutPlayerInGameError()) {
        statusLocked_ = true;
        statusLine_ = T4CLoginSessionGetPutPlayerInGameErrorMessage();
    }
#endif
}

void CharacterSelectScreen::tryEnterWorld(SDL_Window * /*window*/) {
#if defined(LINUX_PORT)
    if (T4CLoginSessionIsWaitingPutPlayerInGame()) {
        statusLocked_ = true;
        statusLine_ = "Chargement perso (attente opcode 13)…";
        return;
    }
    std::vector<T4CCharacterSlot> slots;
    int maxPerAccount = 0;
    T4CLoginSessionCopyCharacterList(&slots, &maxPerAccount);
    if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(slots.size())) {
        statusLocked_ = true;
        statusLine_ = "Selection invalide.";
        return;
    }
    T4CLoginSessionClearPutPlayerInGameError();
    statusLocked_ = false;
    if (!T4CLoginSessionRequestPutPlayerInGame(slots[static_cast<std::size_t>(selectedIndex_)].name)) {
        statusLocked_ = true;
        statusLine_ = "Envoi RQ_PutPlayerInGame (13) impossible.";
        return;
    }
    statusLocked_ = true;
    statusLine_ = "Chargement perso (attente opcode 13)…";
    SDL_Log("[CharacterSelect] RQ_PutPlayerInGame envoye pour %s", slots[static_cast<std::size_t>(selectedIndex_)].name.c_str());
#endif
}

bool CharacterSelectScreen::HandleEvent(const SDL_Event &event, SDL_Window *window) {
    switch (event.type) {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            if (KeyDown(event, SDL_SCANCODE_ESCAPE)) {
                SDL_Log("[CharacterSelect] Esc — retour login");
                stay_ = false;
                statusLocked_ = false;
                return false;
            }
            if (KeyDown(event, SDL_SCANCODE_UP)) {
                selectedIndex_ = std::max(0, selectedIndex_ - 1);
                return true;
            }
            if (KeyDown(event, SDL_SCANCODE_DOWN)) {
                if (!displayLines_.empty()) {
                    selectedIndex_ =
                        std::min(selectedIndex_, static_cast<int>(displayLines_.size()) - 1);
                    selectedIndex_ = std::min(selectedIndex_ + 1,
                                             static_cast<int>(displayLines_.size()) - 1);
                }
                return true;
            }
            if (KeyDown(event, SDL_SCANCODE_RETURN) || KeyDown(event, SDL_SCANCODE_KP_ENTER)) {
                tryEnterWorld(window);
                return true;
            }
            return true;
        default:
            return true;
    }
}

void CharacterSelectScreen::Update() {
#if defined(LINUX_PORT)
    T4CLoginSessionPollBackgroundTasks();
#endif
    refreshFromSession();
}

void CharacterSelectScreen::Render(SDL_Renderer *renderer) {
    SDL_SetRenderDrawColor(renderer, 22, 26, 34, 255);
    SDL_RenderClear(renderer);

    SDL_SetRenderDrawColor(renderer, 200, 210, 220, 255);
    SDL_RenderDebugText(renderer, kPaddingX, 48.0f, "T4C — Selection du personnage");
    SDL_RenderDebugText(renderer, kPaddingX, 80.0f, statusLine_.c_str());

    float y = kListY;
    for (std::size_t i = 0; i < displayLines_.size(); ++i) {
        if (static_cast<int>(i) == selectedIndex_) {
            SDL_FRect hl{kPaddingX - 8.0f, y - 4.0f, 704.0f, kRowH};
            SDL_SetRenderDrawColor(renderer, 48, 72, 96, 255);
            SDL_RenderFillRect(renderer, &hl);
            SDL_SetRenderDrawColor(renderer, 120, 160, 200, 255);
        } else {
            SDL_SetRenderDrawColor(renderer, 160, 170, 180, 255);
        }
        SDL_RenderDebugText(renderer, kPaddingX, y, displayLines_[i].c_str());
        y += kRowH;
    }
}
