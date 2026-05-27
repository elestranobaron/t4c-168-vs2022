#pragma once

#include <SDL3/SDL.h>

#include <vector>

#include "network/T4CPlayerInventory.h"

class FontManager;
class T4CUiFont;
class VSFInterface;

/** Panneau sac a dos graphique (sprites BackPack + grille 9x6, icone 64kInv*). */
class WorldBackpackPanel {
   public:
    static constexpr int kCols = 9;
    static constexpr int kRows = 6;
    static constexpr int kMaxVisibleSlots = kCols * kRows;
    static constexpr int kSlotSize = 26;
    static constexpr int kSlotGap = 4;

    void init(VSFInterface *vsf, int screenW, int screenH);

    void draw(SDL_Surface *dest, const std::vector<T4CBagItem> &items, const T4CUiFont *hudFont,
              FontManager *fm) const;

    /** Coordonnees logiques monde (1800x1000). */
    void updateMouse(int mx, int my);
    void clearHover();

    bool containsPoint(int mx, int my) const;
    int hoverSlotIndex() const { return hoverSlot_; }
    SDL_Rect panelRect() const { return panelRect_; }

   private:
    void layout() const;

    int slotAt(int mx, int my) const;
    void blitSprite(SDL_Surface *dest, struct _sprite *spr, int x, int y) const;
    void drawQty(SDL_Surface *dest, std::uint32_t qty, int x, int y, const T4CUiFont *hudFont,
                 FontManager *fm) const;
    void drawTooltip(SDL_Surface *dest, const T4CBagItem &item, const T4CUiFont *hudFont,
                     FontManager *fm) const;

    VSFInterface *vsfi_{nullptr};
    struct _sprite *bg_{nullptr};
    struct _sprite *outline_{nullptr};
    int screenW_{0};
    int screenH_{0};
    mutable SDL_Rect panelRect_{};
    mutable SDL_Rect gridRect_{};
    int hoverSlot_{-1};
    mutable bool layoutDone_{false};
};
