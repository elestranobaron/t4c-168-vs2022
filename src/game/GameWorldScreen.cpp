#include "game/GameWorldScreen.h"

#include "game/TncDataPaths.h"
#include "network/T4CLoginSession.h"
#include "render/Sdl3FramePresenter.h"

#include <SDL3/SDL.h>
#include <SDL/SDL_image.h>

#include <MapInterface/mapinterface.h>
#include <FontManager/fontmanager.h>
#include <NPCManager/npcmanager.h>
#include <TextManager/textmanager.h>
#include <VSFInterface/vsfinterface.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

GameWorldScreen::~GameWorldScreen() {
    Shutdown();
}

char *GameWorldScreen::DupCStr(const std::string &s) {
    char *out = static_cast<char *>(malloc(s.size() + 1));
    if (out) {
        memcpy(out, s.c_str(), s.size() + 1);
    }
    return out;
}

SDL_Surface *GameWorldScreen::makeLayer(int w, int h, bool with_alpha) {
    if (with_alpha) {
        return SDL_CreateRGBSurface(SDL_HWSURFACE, w, h, 32, 0xFF000000, 0x00FF0000, 0x0000FF00, 0x000000FF);
    }
    return SDL_CreateRGBSurface(SDL_HWSURFACE, w, h, 32, 0xFF000000, 0x00FF0000, 0x0000FF00, 0x00000000);
}

bool GameWorldScreen::Init(SDL_Renderer *renderer, SDL_Window *window) {
    return Init(renderer, window, locX_, locY_, zone_);
}

bool GameWorldScreen::Init(SDL_Renderer *renderer, SDL_Window *window, unsigned int locX, unsigned int locY,
                           unsigned short zone) {
    Shutdown();
    renderer_ = renderer;
    window_ = window;
    locX_ = locX;
    locY_ = locY;
    zone_ = zone;

    dataRoot_ = ResolveT4CDataRoot();
    if (dataRoot_.empty()) {
        lastError_ =
            "Donnees T4C introuvables (sprites/maps).\n"
            "Definissez T4C_DATA ou ./scripts/assemble_t4c_data.sh (client/data/).";
        return false;
    }

    const std::string spritesDir = T4CDataPath("sprites");
    const std::string mapsDir = T4CDataPath("maps");
    if (spritesDir.empty() || mapsDir.empty()) {
        lastError_ = "Chemins sprites/maps invalides sous T4C_DATA.";
        return false;
    }

    if (!presenter_.init(window_, renderer_)) {
        lastError_ = "Init presenter SDL3 echouee.";
        return false;
    }

    screen_ = makeLayer(kLogicalWidth, kLogicalHeight, false);
    sol_ = makeLayer(kLogicalWidth, kLogicalHeight, false);
    decor_ = makeLayer(kLogicalWidth, kLogicalHeight, true);
    env_ = makeLayer(kLogicalWidth, kLogicalHeight, false);
    if (!screen_ || !sol_ || !decor_ || !env_) {
        lastError_ = "Allocation surfaces carte echouee.";
        Shutdown();
        return false;
    }

    const std::string fontMain = T4CDataPath("fonts/font_trebuchet_12");
    const std::string fontUi = T4CDataPath("fonts/sans_bold_12");
    const std::string npcList = T4CDataPath("NPCList.txt");
    const std::string torchePath = T4CDataPath("torche.png");

    fm_ = new FontManager();
    if (fontMain.empty() || !fm_->load_font(DupCStr(fontMain))) {
        lastError_ = "Police introuvable sous T4C_DATA/fonts/ (font_trebuchet_12).";
        Shutdown();
        return false;
    }

    fm2_ = new FontManager();
    if (!fontUi.empty()) {
        fm2_->load_font(DupCStr(fontUi));
    }

    std::string spritesPath = spritesDir;
    std::string mapsPath = mapsDir;
    if (!spritesPath.empty() && spritesPath.back() != '/') {
        spritesPath += '/';
    }
    if (!mapsPath.empty() && mapsPath.back() != '/') {
        mapsPath += '/';
    }

    vsfi_ = new VSFInterface(DupCStr(spritesPath));
    mapi_ = new MAPInterface(DupCStr(mapsPath), vsfi_);

    txtm_ = new TextManager(fm2_);
    txtm_->add_color_text_at_pos(DupCStr("T4C — monde (SDL3)"), 380, 300, 5, 0xBBCC6699);

    if (npcList.empty()) {
        lastError_ = "NPCList.txt introuvable sous T4C_DATA.";
        Shutdown();
        return false;
    }
    npcm_ = new NPCManager(DupCStr(npcList), vsfi_);
    npcm_->ajout_npc(0, DupCStr("CentaurWarrior"), locX_, locY_, 135);
    npcm_->set_action(0, 'D');
    npcm_->move_to(0, locX_ - 5, locY_ - 5, 15);

    if (!torchePath.empty()) {
        if (SDL_Surface *torche = IMG_Load(torchePath.c_str())) {
            SDL_BlitSurface(torche, nullptr, env_, nullptr);
            SDL_DestroySurface(torche);
        }
    }
    SDL_SetAlpha(env_, SDL_SRCALPHA, 0);

    mapFlag_ = true;
    dispInfos_ = false;
    ready_ = true;
    SDL_Log("[GameWorld] data=%s loc=%u,%u zone=%u", dataRoot_.c_str(), locX_, locY_, zone_);
    return true;
}

void GameWorldScreen::Shutdown() {
    ready_ = false;
    delete npcm_;
    npcm_ = nullptr;
    delete txtm_;
    txtm_ = nullptr;
    delete mapi_;
    mapi_ = nullptr;
    delete vsfi_;
    vsfi_ = nullptr;
    delete fm2_;
    fm2_ = nullptr;
    delete fm_;
    fm_ = nullptr;

    if (screen_) {
        SDL_DestroySurface(screen_);
        screen_ = nullptr;
    }
    if (sol_) {
        SDL_DestroySurface(sol_);
        sol_ = nullptr;
    }
    if (decor_) {
        SDL_DestroySurface(decor_);
        decor_ = nullptr;
    }
    if (env_) {
        SDL_DestroySurface(env_);
        env_ = nullptr;
    }

    presenter_.shutdown();
    renderer_ = nullptr;
    window_ = nullptr;
}

void GameWorldScreen::redraw() {
    const Uint32 t0 = SDL_GetTicks();

    if (mapFlag_) {
        mapi_->get_map(locX_, locY_, zone_, sol_, decor_);
        mapFlag_ = false;
    }

    SDL_FillRect(screen_, nullptr, 0xFF000000);
    SDL_BlitSurface(sol_, nullptr, screen_, nullptr);
    SDL_BlitSurface(decor_, nullptr, screen_, nullptr);
    npcm_->draw_npc(screen_, static_cast<int>(locX_), static_cast<int>(locY_));
    SDL_BlitSurface(env_, nullptr, screen_, nullptr);
    txtm_->draw_texts(screen_, static_cast<int>(locX_), static_cast<int>(locY_));

    char charloc[96];
    snprintf(charloc, sizeof(charloc), "Loc %u,%u Z%u | reseau OK | FPS %.0f | F1 debug Esc quit", locX_, locY_,
             zone_, fps_);
    if (SDL_Surface *txt_loc = fm_->get_text(charloc, 0x00FFFFFF)) {
        SDL_BlitSurface(txt_loc, nullptr, screen_, nullptr);
        SDL_DestroySurface(txt_loc);
    }

    if (dispInfos_) {
        char infos[256];
        snprintf(infos, sizeof(infos), "T4C_DATA=%s\nFleches/deplacer F12 capture", dataRoot_.c_str());
        if (SDL_Surface *srf_infos = fm_->get_text(infos)) {
            SDL_Rect txtpos{60, 20, static_cast<int>(srf_infos->w), static_cast<int>(srf_infos->h)};
            SDL_FillRect(screen_, &txtpos, 0x758F75AA);
            SDL_BlitSurface(srf_infos, nullptr, screen_, &txtpos);
            SDL_DestroySurface(srf_infos);
        }
    }

    presenter_.present(screen_, kLogicalWidth, kLogicalHeight);
    fps_ = 1000.f / static_cast<float>(SDL_GetTicks() - t0 + 1);
}

void GameWorldScreen::Update() {
    if (!ready_) {
        return;
    }
    T4CLoginSessionPollBackgroundTasks();
    redraw();
}

void GameWorldScreen::Render(SDL_Renderer * /*renderer*/) {
    /* Composition deja faite dans Update via presenter_ */
}

bool GameWorldScreen::ConsumeReturnToLogin() {
    return std::exchange(returnToLogin_, false);
}

bool GameWorldScreen::HandleEvent(const SDL_Event &event) {
    if (!ready_) {
        return true;
    }
    if (event.type == SDL_EVENT_QUIT) {
        return true;
    }
    if (event.type != SDL_EVENT_KEY_DOWN || !event.key.down) {
        return true;
    }
    switch (event.key.key) {
        case SDLK_ESCAPE:
            returnToLogin_ = true;
            return true;
        case SDLK_F1:
            mapi_->set_debug(true);
            mapFlag_ = true;
            break;
        case SDLK_F2:
            mapi_->set_debug(false);
            mapFlag_ = true;
            break;
        case SDLK_F3:
            dispInfos_ = !dispInfos_;
            break;
        case SDLK_F12:
            SDL_SaveBMP(screen_, "screen_world.bmp");
            break;
        case SDLK_LEFT:
        case SDLK_KP_4:
            if (locX_ > 0) {
                locX_--;
            }
            mapFlag_ = true;
            break;
        case SDLK_RIGHT:
        case SDLK_KP_6:
            locX_++;
            mapFlag_ = true;
            break;
        case SDLK_UP:
        case SDLK_KP_8:
            if (locY_ > 0) {
                locY_--;
            }
            mapFlag_ = true;
            break;
        case SDLK_DOWN:
        case SDLK_KP_2:
            locY_++;
            mapFlag_ = true;
            break;
        default:
            break;
    }
    return true;
}
