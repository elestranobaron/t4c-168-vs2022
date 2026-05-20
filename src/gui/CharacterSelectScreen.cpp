#include "gui/CharacterSelectScreen.h"

#include "gui/LauncherChrome.h"

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

CharacterSelectScreen::CharacterSelectScreen(SDL_Renderer *renderer, LauncherChrome *chrome)
    : renderer_(renderer), chrome_(chrome) {
    statusLine_ = "Chargement liste…";
}

void CharacterSelectScreen::drawUiText(SDL_Renderer *renderer, const char *text, const float x,
                                       const float y, const SDL_Color color) const {
    if (chrome_ && chrome_->font().isReady()) {
        chrome_->font().drawText(renderer, text, x, y, color);
        return;
    }
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderDebugText(renderer, x, y, text);
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
    if (chrome_) {
        chrome_->update();
    }
    refreshFromSession();
}

void CharacterSelectScreen::Render(SDL_Renderer *renderer) {
    if (chrome_) {
        chrome_->renderBackground(renderer);
    } else {
        SDL_SetRenderDrawColor(renderer, 22, 26, 34, 255);
        SDL_RenderClear(renderer);
    }

    const SDL_Color textMain{230, 230, 240, 255};
    const SDL_Color textMuted{200, 210, 220, 255};
    const SDL_Color textSel{180, 220, 255, 255};

    drawUiText(renderer, "The 4th Coming — Selection du personnage", kPaddingX, 48.0f, textMain);
    drawUiText(renderer, statusLine_.c_str(), kPaddingX, 80.0f, textMuted);

    float y = kListY;
    for (std::size_t i = 0; i < displayLines_.size(); ++i) {
        if (static_cast<int>(i) == selectedIndex_) {
            SDL_FRect hl{kPaddingX - 8.0f, y - 4.0f, 704.0f, kRowH};
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 48, 72, 96, 200);
            SDL_RenderFillRect(renderer, &hl);
            SDL_SetRenderDrawColor(renderer, 120, 160, 200, 255);
            SDL_RenderRect(renderer, &hl);
            drawUiText(renderer, displayLines_[i].c_str(), kPaddingX, y, textSel);
        } else {
            drawUiText(renderer, displayLines_[i].c_str(), kPaddingX, y, textMuted);
        }
        y += kRowH;
    }

    if (chrome_) {
        chrome_->renderBanner(renderer);
    }
}
