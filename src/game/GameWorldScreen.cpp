#include "game/GameWorldScreen.h"

#include "game/TncDataPaths.h"
#include "network/T4CLoginSession.h"
#include "Sdl3FramePresenter.h"

#include <SDL3/SDL.h>

#include <MapInterface/mapinterface.h>
#include <FontManager/fontmanager.h>
#include <NPCManager/npcmanager.h>
#include <TextManager/textmanager.h>
#include <VSFInterface/vsfinterface.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

namespace {

bool keyPressed(const SDL_Event &event, const SDL_Keycode key, const SDL_Scancode sc) {
    return event.key.key == key || event.key.scancode == sc;
}

void clampBrightness(float &b) {
    if (b < 0.7f) {
        b = 0.7f;
    } else if (b > 2.0f) {
        b = 2.0f;
    }
}

int facingDegreesFromDelta(const int dx, const int dy) {
    if (dx == 1 && dy == 0) {
        return 270;
    }
    if (dx == -1 && dy == 0) {
        return 90;
    }
    if (dx == 0 && dy == 1) {
        return 0;
    }
    if (dx == 0 && dy == -1) {
        return 180;
    }
    if (dx == 1 && dy == 1) {
        return 315;
    }
    if (dx == 1 && dy == -1) {
        return 225;
    }
    if (dx == -1 && dy == 1) {
        return 45;
    }
    if (dx == -1 && dy == -1) {
        return 135;
    }
    return 180;
}

bool moveDeltaFromOpcode(const std::uint16_t opcode, int &dx, int &dy) {
    switch (opcode) {
        case 1:
            dx = 0;
            dy = -1;
            return true;
        case 2:
            dx = 1;
            dy = -1;
            return true;
        case 3:
            dx = 1;
            dy = 0;
            return true;
        case 4:
            dx = 1;
            dy = 1;
            return true;
        case 5:
            dx = 0;
            dy = 1;
            return true;
        case 6:
            dx = -1;
            dy = 1;
            return true;
        case 7:
            dx = -1;
            dy = 0;
            return true;
        case 8:
            dx = -1;
            dy = -1;
            return true;
        default:
            return false;
    }
}

}  // namespace

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
    playerX_ = locX;
    playerY_ = locY;
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
    if (!screen_ || !sol_ || !decor_) {
        lastError_ = "Allocation surfaces carte echouee.";
        Shutdown();
        return false;
    }

    const std::string fontMain = T4CDataPath("fonts/font_trebuchet_12");
    const std::string fontUi = T4CDataPath("fonts/sans_bold_12");
    const std::string npcList = T4CDataPath("NPCList.txt");

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
    playerNpcSpawned_ = false;

#if defined(LINUX_PORT)
    T4CActivePlayer active{};
    T4CLoginSessionGetActivePlayer(&active);
    unsigned int px = locX_;
    unsigned int py = locY_;
    if (active.serverX != 0 || active.serverY != 0) {
        px = active.serverX;
        py = active.serverY;
    }
    playerX_ = px;
    playerY_ = py;
    locX_ = px;
    locY_ = py;
    const char *spriteName = active.valid ? T4CPlayerSpriteNpcName(active) : "Warrio";
    if (npcm_->ajout_npc(0, DupCStr(spriteName), static_cast<int>(px), static_cast<int>(py), 180)) {
        npcm_->set_action(0, 'S');
        playerNpcSpawned_ = true;
        SDL_Log("[GameWorld] joueur « %s » sprite=%s @ %u,%u%s", active.name.c_str(), spriteName, px, py,
                active.female ? " (F)" : "");
    } else {
        SDL_Log("[GameWorld] echec ajout sprite joueur « %s » — verifier NPCList.txt", spriteName);
    }
#else
    if (npcm_->ajout_npc(0, DupCStr("Warrio"), static_cast<int>(locX_), static_cast<int>(locY_), 180)) {
        npcm_->set_action(0, 'S');
        playerNpcSpawned_ = true;
    }
#endif

    presenter_.setBrightnessScale(1.2f);

    mapFlag_ = true;
    dispInfos_ = false;
    ready_ = true;
    SDL_Log("[GameWorld] data=%s joueur=%u,%u zone=%u", dataRoot_.c_str(), playerX_, playerY_, zone_);
    return true;
}

void GameWorldScreen::Shutdown() {
    ready_ = false;
    playerNpcSpawned_ = false;
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

    presenter_.shutdown();
    renderer_ = nullptr;
    window_ = nullptr;
}

void GameWorldScreen::syncCameraToPlayer() {
    locX_ = playerX_;
    locY_ = playerY_;
#if defined(LINUX_PORT)
    T4CLoginSessionUpdateActivePlayerPosition(playerX_, playerY_);
#endif
    mapFlag_ = true;
}

void GameWorldScreen::snapPlayerVisual(const unsigned int x, const unsigned int y) {
    playerX_ = x;
    playerY_ = y;
    if (playerNpcSpawned_ && npcm_) {
        npcm_->set_world_pos(0, static_cast<int>(x), static_cast<int>(y));
    }
    syncCameraToPlayer();
}

void GameWorldScreen::setPlayerFacingFromDelta(const int dx, const int dy) {
    if (playerNpcSpawned_ && npcm_) {
        npcm_->set_direction(0, facingDegreesFromDelta(dx, dy));
    }
}

void GameWorldScreen::setPlayerWalkAnim(const bool walking) {
    if (playerNpcSpawned_ && npcm_) {
        npcm_->set_action(0, walking ? 'D' : 'S');
    }
}

bool GameWorldScreen::tryMovePlayer(const std::uint16_t moveOpcode) {
    if (playerNpcSpawned_ && npcm_ && npcm_->is_moving(0)) {
        return false;
    }
    int dx = 0;
    int dy = 0;
    if (!moveDeltaFromOpcode(moveOpcode, dx, dy)) {
        return false;
    }
    const int nx = static_cast<int>(playerX_) + dx;
    const int ny = static_cast<int>(playerY_) + dy;
    if (nx < 0 || ny < 0 || nx > 3071 || ny > 3071) {
        return false;
    }
#if defined(LINUX_PORT)
    if (!T4CLoginSessionSendMove(moveOpcode)) {
        return false;
    }
#endif
    playerX_ = static_cast<unsigned int>(nx);
    playerY_ = static_cast<unsigned int>(ny);
    if (playerNpcSpawned_ && npcm_) {
        npcm_->move_to(0, static_cast<unsigned int>(nx), static_cast<unsigned int>(ny), kMoveVisualSpeed,
                       kMoveVisualStepsMul);
    }
    setPlayerFacingFromDelta(dx, dy);
    setPlayerWalkAnim(true);
    return true;
}

void GameWorldScreen::pollHeldMovement() {
    const bool *const keys = SDL_GetKeyboardState(nullptr);
    if (!keys) {
        return;
    }

    const bool left = keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_KP_4];
    const bool right = keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_KP_6];
    const bool up = keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_KP_8];
    const bool down = keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_KP_2];

    std::uint16_t opcode = 0;
    if (left && up) {
        opcode = 8;
    } else if (right && up) {
        opcode = 2;
    } else if (left && down) {
        opcode = 6;
    } else if (right && down) {
        opcode = 4;
    } else if (up) {
        opcode = 1;
    } else if (down) {
        opcode = 5;
    } else if (left) {
        opcode = 7;
    } else if (right) {
        opcode = 3;
    }

    if (opcode != 0) {
        tryMovePlayer(opcode);
    }
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
    txtm_->draw_texts(screen_, static_cast<int>(locX_), static_cast<int>(locY_));

    char charloc[128];
#if defined(LINUX_PORT)
    snprintf(charloc, sizeof(charloc), "Joueur %u,%u | vue %u,%u Z%u | lum %.2f | FPS %.0f", playerX_, playerY_,
             locX_, locY_, zone_, presenter_.brightnessScale(), fps_);
#else
    snprintf(charloc, sizeof(charloc), "Loc %u,%u Z%u | FPS %.0f", locX_, locY_, zone_, fps_);
#endif
    if (SDL_Surface *txt_loc = fm_->get_text(charloc, 0xFFFFFFFF)) {
        SDL_BlitSurface(txt_loc, nullptr, screen_, nullptr);
        SDL_DestroySurface(txt_loc);
    }

    if (dispInfos_) {
        char infos[256];
        snprintf(infos, sizeof(infos), "T4C_DATA=%s\nFleches=perso F4/F5=luminosite F12=capture", dataRoot_.c_str());
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

#if defined(LINUX_PORT)
    T4CPlayerTeleport teleport{};
    if (T4CLoginSessionConsumePlayerTeleport(&teleport)) {
        zone_ = teleport.world;
        mapFlag_ = true;
        snapPlayerVisual(teleport.x, teleport.y);
        setPlayerWalkAnim(false);
    }
#endif

    pollHeldMovement();

    if (playerNpcSpawned_ && npcm_ && !npcm_->is_moving(0)) {
        if (locX_ != playerX_ || locY_ != playerY_) {
            syncCameraToPlayer();
        }
    }

#if defined(LINUX_PORT)
    T4CActivePlayer popup{};
    if (playerNpcSpawned_ && T4CLoginSessionConsumePlayerPopupUpdate(&popup)) {
        const int dx = static_cast<int>(popup.serverX) - static_cast<int>(playerX_);
        const int dy = static_cast<int>(popup.serverY) - static_cast<int>(playerY_);
        snapPlayerVisual(popup.serverX, popup.serverY);
        if (dx != 0 || dy != 0) {
            setPlayerFacingFromDelta(dx, dy);
        }
        setPlayerWalkAnim(false);
    }
#endif

    redraw();
}

void GameWorldScreen::Render(SDL_Renderer * /*renderer*/) {
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
    if (event.type == SDL_EVENT_KEY_UP) {
        switch (event.key.key) {
            case SDLK_LEFT:
            case SDLK_RIGHT:
            case SDLK_UP:
            case SDLK_DOWN:
            case SDLK_KP_4:
            case SDLK_KP_6:
            case SDLK_KP_8:
            case SDLK_KP_2:
            case SDLK_KP_7:
            case SDLK_KP_9:
            case SDLK_KP_1:
            case SDLK_KP_3:
                setPlayerWalkAnim(false);
                break;
            default:
                break;
        }
        return true;
    }

    if (event.type != SDL_EVENT_KEY_DOWN || !event.key.down) {
        return true;
    }

    if (keyPressed(event, SDLK_F4, SDL_SCANCODE_F4)) {
        float b = presenter_.brightnessScale() - 0.1f;
        clampBrightness(b);
        presenter_.setBrightnessScale(b);
        SDL_Log("[GameWorld] luminosite ecran %.2f (F4 = moins)", b);
        return true;
    }
    if (keyPressed(event, SDLK_F5, SDL_SCANCODE_F5)) {
        float b = presenter_.brightnessScale() + 0.1f;
        clampBrightness(b);
        presenter_.setBrightnessScale(b);
        SDL_Log("[GameWorld] luminosite ecran %.2f (F5 = plus)", b);
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
        default:
            break;
    }
    return true;
}
