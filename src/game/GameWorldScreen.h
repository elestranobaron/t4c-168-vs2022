#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Sdl3FramePresenter.h"

#include "gui/T4CUiFont.h"
#include "gui/WorldBackpackPanel.h"
#include "gui/WorldSideMenu.h"

class FontManager;
class MAPInterface;
class NPCManager;
class TextManager;
class VSFInterface;
struct T4CGroundObjectMarker;

/** Vue monde isométrique TnC (VSF + MapInterface) après authentification réseau. */
class GameWorldScreen {
   public:
    static constexpr int kLogicalWidth = 1800;
    static constexpr int kLogicalHeight = 1000;

    GameWorldScreen() = default;
    ~GameWorldScreen();

    GameWorldScreen(const GameWorldScreen &) = delete;
    GameWorldScreen &operator=(const GameWorldScreen &) = delete;

    bool Init(SDL_Renderer *renderer, SDL_Window *window);
    bool Init(SDL_Renderer *renderer, SDL_Window *window, unsigned int locX, unsigned int locY,
              unsigned short zone);

    void Shutdown();

    bool IsReady() const { return ready_; }

    const std::string &GetLastError() const { return lastError_; }

    bool HandleEvent(const SDL_Event &event);

    bool ConsumeReturnToLogin();

    /** True une fois si l'utilisateur a confirme « Quitter » dans le menu (Esc). */
    bool ConsumeQuitApp();

    void Update();

    void Render(SDL_Renderer *renderer);

   private:
    /** Vitesse move_to TnC (plus grand = plus rapide). */
    static constexpr unsigned short kMoveVisualSpeed = 4;
    /** Frames par case (~32*mul/speed) ; depl ralenti en meme temps dans move_to. */
    static constexpr unsigned short kMoveVisualStepsMul = 2;

    static char *DupCStr(const std::string &s);

    void redraw();
    void syncCameraToPlayer();
    void snapPlayerVisual(unsigned int x, unsigned int y);
    void setPlayerFacingFromDelta(int dx, int dy);
    void setPlayerWalkAnim(bool walking);
    bool handleWorldClickMove(const SDL_Event &event);
    void drawGroundObjectMarkers();
    /** 0 si aucune touche de deplacement. */
    std::uint16_t heldMoveOpcode() const;
    void applyServerPlayerPosition(unsigned int x, unsigned int y);
    void pollHeldMovement();
    bool tryMovePlayer(std::uint16_t moveOpcode);
    void clearRemoteUnits();
    void syncRemoteUnitsFromNetwork();
    void pollNearItemsResync();
    void drawOptionsPopup();
    bool handleSideMenuKey(const SDL_Event &event);
    bool handleOptionsPopupKey(const SDL_Event &event);
    bool handleSideMenuMouse(const SDL_Event &event);
    bool handleBackpackPanelMouse(const SDL_Event &event);
    void freeCharacterPanelCache();
    void rebuildCharacterPanelCache();
    void drawCharacterPanel();
    void toggleCharacterPanel(int kind, bool forceRefresh = false);
    SDL_Surface *makeLayer(int w, int h, bool with_alpha);

    SDL_Renderer *renderer_{nullptr};
    SDL_Window *window_{nullptr};
    Sdl3FramePresenter presenter_;

    SDL_Surface *screen_{nullptr};
    SDL_Surface *sol_{nullptr};
    SDL_Surface *decor_{nullptr};

    VSFInterface *vsfi_{nullptr};
    MAPInterface *mapi_{nullptr};
    FontManager *fm_{nullptr};
    FontManager *fm2_{nullptr};
    T4CUiFont hudFont_;
    TextManager *txtm_{nullptr};
    NPCManager *npcm_{nullptr};

    unsigned int locX_{2880};
    unsigned int locY_{1083};
    unsigned int playerX_{2880};
    unsigned int playerY_{1083};
    std::uint16_t pendingMoveOpcode_{0};
    bool awaitingServerMove_{false};
    Uint32 awaitingServerMoveSince_{0};
    bool wasMoving_{false};
    unsigned short zone_{0};
    bool mapFlag_{true};
    bool dispInfos_{false};
    bool ready_{false};
    bool returnToLogin_{false};
    bool quitApp_{false};
    bool optionsPopupOpen_{false};
    int optionsSelection_{0};
    WorldSideMenu sideMenu_;
    WorldBackpackPanel backpackPanel_;
    bool playerNpcSpawned_{false};
    std::unordered_set<std::int32_t> remoteUnitIds_;
    std::unordered_map<std::int32_t, std::pair<unsigned int, unsigned int>> remotePositions_;
    unsigned int nearItemsAnchorX_{0};
    unsigned int nearItemsAnchorY_{0};
    bool nearItemsAnchorSet_{false};
    /** 0=none 1=sac 2=skills 3=sorts 4=coffre banque 5=equipement 6=fiche stats */
    int characterPanel_{0};
    std::vector<T4CGroundObjectMarker> groundMarkers_;
    struct CharacterPanelGlyph {
        SDL_Surface *surface{nullptr};
        int x{0};
        int y{0};
    };
    std::vector<CharacterPanelGlyph> panelGlyphs_;
    SDL_Surface *panelTitleSurf_{nullptr};
    SDL_Surface *panelFooterSurf_{nullptr};
    SDL_Rect panelRect_{};
    bool panelCacheDirty_{true};
    float fps_{0.f};
    std::string dataRoot_;
    std::string lastError_;
};
