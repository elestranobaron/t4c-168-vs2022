#include "game/GameWorldScreen.h"

#include "game/TncDataPaths.h"
#include "network/T4CLoginSession.h"
#include "audio/T4CGameMusic.h"
#include "Sdl3FramePresenter.h"
#include "tnc_sdl3.h"

#include <SDL3/SDL.h>

#include <MapInterface/mapinterface.h>
#include <FontManager/fontmanager.h>
#include <NPCManager/npcmanager.h>
#include <TextManager/textmanager.h>
#include <VSFInterface/vsfinterface.h>

#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <utility>

namespace {

bool keyPressed(const SDL_Event &event, const SDL_Keycode key, const SDL_Scancode sc) {
    return event.key.key == key || event.key.scancode == sc;
}

void clampBrightness(float &b) {
    if (b < 1.0f) {
        b = 1.0f;
    } else if (b > 3.0f) {
        b = 3.0f;
    }
}

void blitText(FontManager *fm, SDL_Surface *dest, int x, int y, const char *text, std::uint32_t color) {
    if (!fm || !dest || !text) {
        return;
    }
    if (SDL_Surface *s = fm->get_text(const_cast<char *>(text), color)) {
        SDL_Rect dst{x, y, static_cast<int>(s->w), static_cast<int>(s->h)};
        SDL_BlitSurface(s, nullptr, dest, &dst);
        SDL_DestroySurface(s);
    }
}

void drawStatusBar(SDL_Surface *dest, FontManager *fm, int x, int y, int w, int h, const char *label,
                   unsigned cur, unsigned max, std::uint32_t fillArgb) {
    if (!dest || w <= 0 || h <= 0) {
        return;
    }
    SDL_Rect bg{x, y, w, h};
    TnC_FillArgb(dest, &bg, 0xFF1A1A1A);
    if (max > 0 && cur > 0) {
        const int fw = static_cast<int>((static_cast<unsigned long long>(cur) * static_cast<unsigned>(w)) / max);
        if (fw > 0) {
            SDL_Rect fill{x, y, fw, h};
            TnC_FillArgb(dest, &fill, fillArgb);
        }
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s %u/%u", label, cur, max);
    blitText(fm, dest, x + 4, y + 1, buf, 0xFFFFFFFF);
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

SDL_Surface *GameWorldScreen::makeLayer(int w, int h, bool /*with_alpha*/) {
    return TnC_CreateRgbaSurface(w, h);
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
        SDL_Log("[GameWorld] joueur « %s » niv %u sprite=%s app=%u%s @ %u,%u", active.name.c_str(),
                static_cast<unsigned>(active.level), spriteName, static_cast<unsigned>(active.appearance),
                active.female ? " (F)" : "", px, py);
    } else {
        SDL_Log("[GameWorld] echec ajout sprite joueur « %s » — verifier NPCList.txt", spriteName);
    }
#else
    if (npcm_->ajout_npc(0, DupCStr("Warrio"), static_cast<int>(locX_), static_cast<int>(locY_), 180)) {
        npcm_->set_action(0, 'S');
        playerNpcSpawned_ = true;
    }
#endif

    presenter_.setBrightnessScale(1.0f);

    sideMenu_.init(vsfi_, kLogicalWidth, kLogicalHeight);
    sideMenu_.setOpen(false);
    optionsPopupOpen_ = false;
    optionsSelection_ = 0;

    mapFlag_ = true;
    dispInfos_ = false;
    ready_ = true;
    SDL_Log("[GameWorld] data=%s joueur=%u,%u zone=%u", dataRoot_.c_str(), playerX_, playerY_, zone_);
#if defined(LINUX_PORT)
    {
        T4CActivePlayer active{};
        T4CLoginSessionGetActivePlayer(&active);
        T4CGameMusic::LoadNewSound(zone_, playerX_, playerY_, active.level);
        T4CPlayerStatus status{};
        T4CLoginSessionGetPlayerStatus(&status);
        if (!status.valid) {
            T4CLoginSessionRequestPlayerStatus();
        }
    }
#endif
    return true;
}

void GameWorldScreen::clearRemoteUnits() {
    if (npcm_) {
        for (const std::int32_t id : remoteUnitIds_) {
            npcm_->remove_npc(static_cast<int>(id));
        }
    }
    remoteUnitIds_.clear();
    remotePositions_.clear();
}

void GameWorldScreen::syncRemoteUnitsFromNetwork() {
    if (!npcm_) {
        return;
    }

    std::vector<T4CRemoteUnitEvent> events;
    T4CLoginSessionDrainRemoteUnitEvents(&events);
    if (events.empty()) {
        return;
    }

    for (const T4CRemoteUnitEvent &ev : events) {
        const int npcId = static_cast<int>(ev.unitId);
        if (npcId == 0) {
            continue;
        }

        switch (ev.kind) {
            case T4CRemoteUnitEventKind::Spawn: {
                const char *sprite = T4CSpriteNameFromAppearance(ev.appearance);
                if (remoteUnitIds_.count(ev.unitId) != 0) {
                    npcm_->set_world_pos(npcId, static_cast<int>(ev.x), static_cast<int>(ev.y));
                } else if (npcm_->ajout_npc(npcId, const_cast<char *>(sprite), static_cast<int>(ev.x),
                                            static_cast<int>(ev.y), 180)) {
                    npcm_->set_action(npcId, 'S');
                    remoteUnitIds_.insert(ev.unitId);
                    SDL_Log("[GameWorld] unite distante spawn id=%d app=%u sprite=%s @ %u,%u", npcId,
                            static_cast<unsigned>(ev.appearance), sprite, ev.x, ev.y);
                } else {
                    SDL_Log("[GameWorld] echec spawn unite id=%d sprite=%s (NPCList?)", npcId, sprite);
                }
                remotePositions_[ev.unitId] = {ev.x, ev.y};
                break;
            }
            case T4CRemoteUnitEventKind::Move: {
                if (remoteUnitIds_.count(ev.unitId) == 0) {
                    const char *sprite = T4CSpriteNameFromAppearance(ev.appearance);
                    if (npcm_->ajout_npc(npcId, const_cast<char *>(sprite), static_cast<int>(ev.x),
                                         static_cast<int>(ev.y), 180)) {
                        npcm_->set_action(npcId, 'S');
                        remoteUnitIds_.insert(ev.unitId);
                    }
                }
                const auto prevIt = remotePositions_.find(ev.unitId);
                const bool hasPrev = prevIt != remotePositions_.end();
                const unsigned int px = hasPrev ? prevIt->second.first : ev.x;
                const unsigned int py = hasPrev ? prevIt->second.second : ev.y;
                const int dx = static_cast<int>(ev.x) - static_cast<int>(px);
                const int dy = static_cast<int>(ev.y) - static_cast<int>(py);
                const bool adjacent = hasPrev && ((dx == 0 && (dy == 1 || dy == -1)) || (dy == 0 && (dx == 1 || dx == -1)) ||
                                                  (dx != 0 && dy != 0 && std::abs(dx) == 1 && std::abs(dy) == 1));
                if (adjacent && !npcm_->is_moving(npcId)) {
                    npcm_->move_to(npcId, ev.x, ev.y, kMoveVisualSpeed, kMoveVisualStepsMul);
                } else {
                    npcm_->set_world_pos(npcId, static_cast<int>(ev.x), static_cast<int>(ev.y));
                }
                remotePositions_[ev.unitId] = {ev.x, ev.y};
                break;
            }
            case T4CRemoteUnitEventKind::Update:
                if (remoteUnitIds_.count(ev.unitId) != 0 && ev.appearance != 0) {
                    npcm_->set_npc_type(npcId, T4CSpriteNameFromAppearance(ev.appearance));
                }
                break;
            case T4CRemoteUnitEventKind::Remove:
                if (remoteUnitIds_.count(ev.unitId) != 0) {
                    npcm_->remove_npc(npcId);
                    remoteUnitIds_.erase(ev.unitId);
                    remotePositions_.erase(ev.unitId);
                }
                break;
        }
    }
}

void GameWorldScreen::Shutdown() {
    ready_ = false;
    playerNpcSpawned_ = false;
    remoteUnitIds_.clear();
    remotePositions_.clear();
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
    if (awaitingServerMove_) {
        if (TnC_GetTicksMs() - awaitingServerMoveSince_ > 500) {
            awaitingServerMove_ = false;
        } else {
            return false;
        }
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
    awaitingServerMove_ = true;
    awaitingServerMoveSince_ = TnC_GetTicksMs();
    setPlayerFacingFromDelta(dx, dy);
    return true;
}

void GameWorldScreen::applyServerPlayerPosition(const unsigned int x, const unsigned int y) {
    const int dx = static_cast<int>(x) - static_cast<int>(playerX_);
    const int dy = static_cast<int>(y) - static_cast<int>(playerY_);
    if (dx == 0 && dy == 0) {
        return;
    }
    const int adx = dx < 0 ? -dx : dx;
    const int ady = dy < 0 ? -dy : dy;
    const bool oneStep = adx <= 1 && ady <= 1 && (dx != 0 || dy != 0);
    if (oneStep && playerNpcSpawned_ && npcm_) {
        npcm_->set_world_pos(0, static_cast<int>(playerX_), static_cast<int>(playerY_));
        npcm_->move_to(0, x, y, kMoveVisualSpeed, kMoveVisualStepsMul);
        playerX_ = x;
        playerY_ = y;
        syncCameraToPlayer();
        setPlayerWalkAnim(true);
    } else {
        snapPlayerVisual(x, y);
        setPlayerWalkAnim(false);
    }
}

std::uint16_t GameWorldScreen::heldMoveOpcode() const {
    if (sideMenu_.isOpen() || optionsPopupOpen_) {
        return 0;
    }
    const bool *const keys = SDL_GetKeyboardState(nullptr);
    if (!keys) {
        return 0;
    }

    const bool left = keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_KP_4];
    const bool right = keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_KP_6];
    const bool up = keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_KP_8];
    const bool down = keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_KP_2];

    if (left && up) {
        return 8;
    }
    if (right && up) {
        return 2;
    }
    if (left && down) {
        return 6;
    }
    if (right && down) {
        return 4;
    }
    if (up) {
        return 1;
    }
    if (down) {
        return 5;
    }
    if (left) {
        return 7;
    }
    if (right) {
        return 3;
    }
    return 0;
}

void GameWorldScreen::pollHeldMovement() {
    const std::uint16_t opcode = heldMoveOpcode();
    if (opcode == 0) {
        pendingMoveOpcode_ = 0;
        return;
    }
    if (tryMovePlayer(opcode)) {
        pendingMoveOpcode_ = 0;
    } else {
        pendingMoveOpcode_ = opcode;
    }
}

void GameWorldScreen::redraw() {
    const Uint32 t0 = TnC_GetTicksMs();

    if (mapFlag_) {
        mapi_->get_map(locX_, locY_, zone_, sol_, decor_);
        mapFlag_ = false;
    }

    TnC_FillArgb(screen_, nullptr, 0xFF000000);
    SDL_BlitSurface(sol_, nullptr, screen_, nullptr);
    SDL_BlitSurface(decor_, nullptr, screen_, nullptr);
    npcm_->draw_npc(screen_, static_cast<int>(locX_), static_cast<int>(locY_));
    txtm_->draw_texts(screen_, static_cast<int>(locX_), static_cast<int>(locY_));

    char charloc[128];
#if defined(LINUX_PORT)
    T4CActivePlayer active{};
    T4CPlayerStatus status{};
    T4CLoginSessionGetActivePlayer(&active);
    T4CLoginSessionGetPlayerStatus(&status);
    const unsigned displayLevel =
        status.valid && status.level != 0 ? static_cast<unsigned>(status.level) : static_cast<unsigned>(active.level);
    if (active.valid && !active.name.empty()) {
        snprintf(charloc, sizeof(charloc), "%s niv %u | %u,%u Z%u | app %u | %s | lum %.2f | FPS %.0f",
                 active.name.c_str(), displayLevel, playerX_, playerY_, zone_,
                 static_cast<unsigned>(active.appearance), T4CPlayerSpriteNpcName(active),
                 presenter_.brightnessScale(), fps_);
    } else {
        snprintf(charloc, sizeof(charloc), "Joueur %u,%u | vue %u,%u Z%u | lum %.2f | FPS %.0f", playerX_, playerY_,
                 locX_, locY_, zone_, presenter_.brightnessScale(), fps_);
    }
#else
    snprintf(charloc, sizeof(charloc), "Loc %u,%u Z%u | FPS %.0f", locX_, locY_, zone_, fps_);
#endif
    if (SDL_Surface *txt_loc = fm_->get_text(charloc, 0xFFFFFFFF)) {
        SDL_BlitSurface(txt_loc, nullptr, screen_, nullptr);
        SDL_DestroySurface(txt_loc);
    }

#if defined(LINUX_PORT)
    if (status.valid) {
        constexpr int barX = 0;
        constexpr int barW = 220;
        constexpr int barH = 14;
        int barY = 18;
        if (active.valid && !active.name.empty()) {
            barY = 22;
        }
        drawStatusBar(screen_, fm_, barX, barY, barW, barH, "PV", status.hp, status.maxHp, 0xFFCC3333);
        drawStatusBar(screen_, fm_, barX, barY + barH + 2, barW, barH, "Mana", static_cast<unsigned>(status.mana),
                      static_cast<unsigned>(status.maxMana), 0xFF3366CC);
        char xpLine[64];
        if (status.xpToNextLevel > 0) {
            std::snprintf(xpLine, sizeof(xpLine), "XP %llu / %llu",
                          static_cast<unsigned long long>(status.xp),
                          static_cast<unsigned long long>(status.xpToNextLevel));
        } else {
            std::snprintf(xpLine, sizeof(xpLine), "XP %llu", static_cast<unsigned long long>(status.xp));
        }
        blitText(fm_, screen_, barX + 4, barY + (barH + 2) * 2 + 2, xpLine, 0xFFCCCCCC);
    }
#endif

    if (sideMenu_.isOpen()) {
        sideMenu_.draw(screen_);
    }
    if (optionsPopupOpen_) {
        drawOptionsPopup();
    }

    if (dispInfos_) {
        char infos[256];
        snprintf(infos, sizeof(infos), "T4C_DATA=%s\nFleches=perso F4/F5=luminosite F12=capture", dataRoot_.c_str());
        if (SDL_Surface *srf_infos = fm_->get_text(infos)) {
            SDL_Rect txtpos{60, 20, static_cast<int>(srf_infos->w), static_cast<int>(srf_infos->h)};
            TnC_FillArgb(screen_, &txtpos, 0x758F75AA);
            SDL_BlitSurface(srf_infos, nullptr, screen_, &txtpos);
            SDL_DestroySurface(srf_infos);
        }
    }

    presenter_.present(screen_, kLogicalWidth, kLogicalHeight);
    fps_ = 1000.f / static_cast<float>(TnC_GetTicksMs() - t0 + 1);
}

void GameWorldScreen::Update() {
    if (!ready_) {
        return;
    }
    T4CLoginSessionPollBackgroundTasks();

#if defined(LINUX_PORT)
    T4CPlayerTeleport teleport{};
    if (T4CLoginSessionConsumePlayerTeleport(&teleport)) {
        clearRemoteUnits();
        T4CLoginSessionClearRemoteUnits();
        awaitingServerMove_ = false;
        zone_ = teleport.world;
        mapFlag_ = true;
        snapPlayerVisual(teleport.x, teleport.y);
        setPlayerWalkAnim(false);
        T4CGameMusic::Reset();
        {
            T4CActivePlayer active{};
            T4CLoginSessionGetActivePlayer(&active);
            T4CGameMusic::LoadNewSound(zone_, teleport.x, teleport.y, active.level);
        }
    }
#endif

    pollHeldMovement();

    syncRemoteUnitsFromNetwork();

    if (playerNpcSpawned_ && npcm_) {
        const bool moving = npcm_->is_moving(0);
        if (!moving) {
            npcm_->set_world_pos(0, static_cast<int>(playerX_), static_cast<int>(playerY_));
            if (pendingMoveOpcode_ != 0 && heldMoveOpcode() == pendingMoveOpcode_) {
                const std::uint16_t op = pendingMoveOpcode_;
                pendingMoveOpcode_ = 0;
                tryMovePlayer(op);
            } else if (wasMoving_) {
                setPlayerWalkAnim(false);
            }
        }
        wasMoving_ = npcm_->is_moving(0);
    }

#if defined(LINUX_PORT)
    T4CActivePlayer popup{};
    if (playerNpcSpawned_ && T4CLoginSessionConsumePlayerPopupUpdate(&popup)) {
        awaitingServerMove_ = false;
        if (popup.serverX != playerX_ || popup.serverY != playerY_) {
            const int pdx = static_cast<int>(popup.serverX) - static_cast<int>(playerX_);
            const int pdy = static_cast<int>(popup.serverY) - static_cast<int>(playerY_);
            applyServerPlayerPosition(popup.serverX, popup.serverY);
            if (pdx != 0 || pdy != 0) {
                setPlayerFacingFromDelta(pdx, pdy);
            }
            T4CGameMusic::LoadNewSound(zone_, popup.serverX, popup.serverY, popup.level);
        }
    }
#endif

    redraw();
}

void GameWorldScreen::Render(SDL_Renderer * /*renderer*/) {
}

bool GameWorldScreen::ConsumeReturnToLogin() {
    return std::exchange(returnToLogin_, false);
}

bool GameWorldScreen::ConsumeQuitApp() {
    return std::exchange(quitApp_, false);
}

void GameWorldScreen::drawOptionsPopup() {
    SDL_Rect panel{620, 320, 560, 280};
    TnC_FillArgb(screen_, &panel, 0xC0182030);

    const int x = panel.x + 32;
    int y = panel.y + 20;
    blitText(fm_, screen_, x, y, "Options", 0xFFE8D080);
    y += 36;

    static const char *kItems[] = {"Annuler", "Retour au login", "Quitter le jeu"};
    for (int i = 0; i < 3; ++i) {
        char line[64];
        snprintf(line, sizeof(line), "%s %s", optionsSelection_ == i ? ">" : " ", kItems[i]);
        blitText(fm_, screen_, x + 16, y, line, optionsSelection_ == i ? 0xFF80FF80 : 0xFFCCCCCC);
        y += 28;
    }
}

bool GameWorldScreen::handleOptionsPopupKey(const SDL_Event &event) {
    if (!event.key.down || event.key.repeat) {
        return true;
    }

    if (keyPressed(event, SDLK_ESCAPE, SDL_SCANCODE_ESCAPE)) {
        optionsPopupOpen_ = false;
        optionsSelection_ = 0;
        return true;
    }

    if (keyPressed(event, SDLK_UP, SDL_SCANCODE_UP)) {
        optionsSelection_ = (optionsSelection_ + 2) % 3;
        return true;
    }
    if (keyPressed(event, SDLK_DOWN, SDL_SCANCODE_DOWN)) {
        optionsSelection_ = (optionsSelection_ + 1) % 3;
        return true;
    }

    if (!keyPressed(event, SDLK_RETURN, SDL_SCANCODE_RETURN) &&
        !keyPressed(event, SDLK_KP_ENTER, SDL_SCANCODE_KP_ENTER)) {
        return true;
    }

    switch (optionsSelection_) {
        case 0:
            optionsPopupOpen_ = false;
            optionsSelection_ = 0;
            break;
        case 1:
            optionsPopupOpen_ = false;
            sideMenu_.setOpen(false);
            returnToLogin_ = true;
            break;
        case 2:
            optionsPopupOpen_ = false;
            sideMenu_.setOpen(false);
            quitApp_ = true;
            break;
        default:
            break;
    }
    return true;
}

bool GameWorldScreen::handleSideMenuKey(const SDL_Event &event) {
    if (!event.key.down || event.key.repeat) {
        return true;
    }

    if (keyPressed(event, SDLK_ESCAPE, SDL_SCANCODE_ESCAPE)) {
        sideMenu_.setOpen(false);
        return true;
    }
    return true;
}

bool GameWorldScreen::handleSideMenuMouse(const SDL_Event &event) {
    SDL_Event ev = event;
    if (renderer_) {
        SDL_ConvertEventToRenderCoordinates(renderer_, &ev);
    }

    int mx = 0;
    int my = 0;
    if (event.type == SDL_EVENT_MOUSE_MOTION) {
        mx = static_cast<int>(ev.motion.x);
        my = static_cast<int>(ev.motion.y);
        sideMenu_.handleMouse(mx, my, false, false);
        return true;
    }

    mx = static_cast<int>(ev.button.x);
    my = static_cast<int>(ev.button.y);
    const bool leftDown = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT;
    const bool leftUp = event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT;

    if (!leftDown && !leftUp) {
        return true;
    }

    const WorldSideMenu::Action action = sideMenu_.handleMouse(mx, my, leftDown, leftUp);
    if (action == WorldSideMenu::Action::OpenOptions) {
        optionsPopupOpen_ = true;
        optionsSelection_ = 0;
        return true;
    }
    if (action == WorldSideMenu::Action::PanelNotImplemented) {
        SDL_Log("[GameWorld] panneau SideMenu non implemente (placeholder).");
        return true;
    }
    return true;
}

bool GameWorldScreen::HandleEvent(const SDL_Event &event) {
    if (!ready_) {
        return true;
    }
    if (event.type == SDL_EVENT_QUIT) {
        return true;
    }

    if (event.type == SDL_EVENT_MOUSE_MOTION || event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
        event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        if (optionsPopupOpen_) {
            return true;
        }
        if (sideMenu_.isOpen()) {
            return handleSideMenuMouse(event);
        }
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
                if (playerNpcSpawned_ && npcm_ && !npcm_->is_moving(0)) {
                    setPlayerWalkAnim(false);
                }
                break;
            default:
                break;
        }
        return true;
    }

    if (event.type != SDL_EVENT_KEY_DOWN || !event.key.down) {
        return true;
    }

    if (optionsPopupOpen_) {
        return handleOptionsPopupKey(event);
    }

    if (sideMenu_.isOpen()) {
        return handleSideMenuKey(event);
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
            sideMenu_.setOpen(true);
            optionsPopupOpen_ = false;
            optionsSelection_ = 0;
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
