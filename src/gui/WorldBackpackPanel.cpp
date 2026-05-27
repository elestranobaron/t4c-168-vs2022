#include "gui/WorldBackpackPanel.h"

#include "gui/T4CInvItemIcons.h"
#include "gui/T4CUiFont.h"
#include "tnc_sdl3.h"

#include <FontManager/fontmanager.h>
#include <VSFInterface/vsfinterface.h>

#include <algorithm>
#include <cstdio>

namespace {

SDL_Color argbToColor(const std::uint32_t argb) {
    return SDL_Color{static_cast<Uint8>((argb >> 16) & 0xFF), static_cast<Uint8>((argb >> 8) & 0xFF),
                     static_cast<Uint8>(argb & 0xFF), static_cast<Uint8>((argb >> 24) & 0xFF)};
}

void blitHudLine(const T4CUiFont *hudFont, FontManager *fm, SDL_Surface *dest, int x, int y,
                 const char *text, std::uint32_t color) {
    if (!dest || !text) {
        return;
    }
    if (hudFont && hudFont->isReady()) {
        hudFont->blitText(dest, x, y, text, argbToColor(color));
        return;
    }
    if (fm) {
        SDL_Surface *s = fm->get_text(const_cast<char *>(text), color);
        if (!s) {
            return;
        }
        SDL_Rect dst{x, y, static_cast<int>(s->w), static_cast<int>(s->h)};
        SDL_BlitSurface(s, nullptr, dest, &dst);
        SDL_DestroySurface(s);
    }
}

}  // namespace

void WorldBackpackPanel::init(VSFInterface *vsf, const int screenW, const int screenH) {
    vsfi_ = vsf;
    screenW_ = screenW;
    screenH_ = screenH;
    bg_ = vsfi_ ? vsfi_->get_sprite_by_name("BackPack") : nullptr;
    outline_ = vsfi_ ? vsfi_->get_sprite_by_name("BackpackOutline") : nullptr;
    layoutDone_ = false;
    hoverSlot_ = -1;
}

void WorldBackpackPanel::layout() const {
    if (layoutDone_) {
        return;
    }
    layoutDone_ = true;

    int panelW = 552;
    int panelH = 430;
    if (bg_ && bg_->sdl_sprite) {
        panelW = bg_->sdl_sprite->w;
        panelH = bg_->sdl_sprite->h;
    }

    const int originX = std::max(0, (screenW_ - panelW) / 2);
    const int originY = std::max(48, (screenH_ - panelH) / 2);
    panelRect_ = SDL_Rect{originX, originY, panelW, panelH};

    // Offsets alignes ChestUI / TradeUI (grille sac dans GUI_InvBackB).
    const int gridX = originX + 88;
    const int gridY = originY + 273;
    const int gridW = kCols * kSlotSize + (kCols - 1) * kSlotGap;
    const int gridH = kRows * kSlotSize + (kRows - 1) * kSlotGap;
    gridRect_ = SDL_Rect{gridX, gridY, gridW, gridH};
}

void WorldBackpackPanel::blitSprite(SDL_Surface *dest, struct _sprite *spr, const int x, const int y) const {
    if (!dest || !spr || !spr->sdl_sprite) {
        return;
    }
    SDL_Rect dst{x, y, spr->sdl_sprite->w, spr->sdl_sprite->h};
    SDL_BlitSurface(spr->sdl_sprite, nullptr, dest, &dst);
}

void WorldBackpackPanel::drawQty(SDL_Surface *dest, const std::uint32_t qty, const int x, const int y,
                                 const T4CUiFont *hudFont, FontManager *fm) const {
    if (qty <= 1) {
        return;
    }
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%u", qty);
    blitHudLine(hudFont, fm, dest, x + kSlotSize - 18, y + kSlotSize - 16, buf, 0xFFFFFFFF);
}

void WorldBackpackPanel::drawTooltip(SDL_Surface *dest, const T4CBagItem &item, const T4CUiFont *hudFont,
                                     FontManager *fm) const {
    char line[192];
    if (!item.name.empty()) {
        if (item.qty > 1) {
            std::snprintf(line, sizeof(line), "%s x%u", item.name.c_str(), item.qty);
        } else {
            std::snprintf(line, sizeof(line), "%s", item.name.c_str());
        }
    } else {
        std::snprintf(line, sizeof(line), "app %u id %d x%u", static_cast<unsigned>(item.appearance),
                      static_cast<int>(item.objectId), static_cast<unsigned>(item.qty));
    }
    const int ty = panelRect_.y + panelRect_.h - 28;
    blitHudLine(hudFont, fm, dest, panelRect_.x + 16, ty, line, 0xFFE8E8E8);
}

void WorldBackpackPanel::draw(SDL_Surface *dest, const std::vector<T4CBagItem> &items, const T4CUiFont *hudFont,
                              FontManager *fm) const {
    if (!dest) {
        return;
    }
    layout();

    if (bg_ && bg_->sdl_sprite) {
        blitSprite(dest, bg_, panelRect_.x, panelRect_.y);
    } else {
        TnC_FillArgb(dest, &panelRect_, 0xD0182030);
    }
    if (outline_ && outline_->sdl_sprite) {
        blitSprite(dest, outline_, panelRect_.x, panelRect_.y);
    }

    blitHudLine(hudFont, fm, dest, panelRect_.x + 16, panelRect_.y + 8, "Sac a dos", 0xFFCCDDFF);

    const int shown = static_cast<int>(std::min(items.size(), static_cast<std::size_t>(kMaxVisibleSlots)));
    for (int slot = 0; slot < kMaxVisibleSlots; ++slot) {
        const int col = slot % kCols;
        const int row = slot / kCols;
        const int sx = gridRect_.x + col * (kSlotSize + kSlotGap);
        const int sy = gridRect_.y + row * (kSlotSize + kSlotGap);
        SDL_Rect slotRect{sx, sy, kSlotSize, kSlotSize};
        const bool hot = slot == hoverSlot_;
        TnC_FillArgb(dest, &slotRect, hot ? 0x884466AA : 0x55202028);
        if (hot) {
            SDL_Rect border{sx - 1, sy - 1, kSlotSize + 2, kSlotSize + 2};
            TnC_FillArgb(dest, &border, 0xFF88AAFF);
        }
    }

    for (int i = 0; i < shown; ++i) {
        const T4CBagItem &item = items[static_cast<std::size_t>(i)];
        const int col = i % kCols;
        const int row = i / kCols;
        const int sx = gridRect_.x + col * (kSlotSize + kSlotGap);
        const int sy = gridRect_.y + row * (kSlotSize + kSlotGap);

        if (struct _sprite *icon = T4CInvItemIconSprite(vsfi_, item.appearance)) {
            if (icon->sdl_sprite) {
                const int ox = sx + (kSlotSize - static_cast<int>(icon->sdl_sprite->w)) / 2;
                const int oy = sy + (kSlotSize - static_cast<int>(icon->sdl_sprite->h)) / 2;
                blitSprite(dest, icon, ox, oy);
            }
        }
        drawQty(dest, item.qty, sx, sy, hudFont, fm);
    }

    if (static_cast<int>(items.size()) > kMaxVisibleSlots) {
        char more[48];
        std::snprintf(more, sizeof(more), "... +%zu objets", items.size() - static_cast<std::size_t>(kMaxVisibleSlots));
        blitHudLine(hudFont, fm, dest, panelRect_.x + 16, panelRect_.y + panelRect_.h - 48, more, 0xFFAAAAAA);
    }

    if (hoverSlot_ >= 0 && hoverSlot_ < shown) {
        drawTooltip(dest, items[static_cast<std::size_t>(hoverSlot_)], hudFont, fm);
    } else {
        blitHudLine(hudFont, fm, dest, panelRect_.x + 16, panelRect_.y + panelRect_.h - 28,
                    "B/Esc ferme | Maj+B refresh", 0xFF888888);
    }
}

int WorldBackpackPanel::slotAt(const int mx, const int my) const {
    if (mx < gridRect_.x || my < gridRect_.y || mx >= gridRect_.x + gridRect_.w ||
        my >= gridRect_.y + gridRect_.h) {
        return -1;
    }
    const int lx = mx - gridRect_.x;
    const int ly = my - gridRect_.y;
    const int step = kSlotSize + kSlotGap;
    const int col = lx / step;
    const int row = ly / step;
    if (col < 0 || col >= kCols || row < 0 || row >= kRows) {
        return -1;
    }
    const int inCellX = lx - col * step;
    const int inCellY = ly - row * step;
    if (inCellX >= kSlotSize || inCellY >= kSlotSize) {
        return -1;
    }
    return row * kCols + col;
}

bool WorldBackpackPanel::containsPoint(const int mx, const int my) const {
    layout();
    return mx >= panelRect_.x && mx < panelRect_.x + panelRect_.w && my >= panelRect_.y &&
           my < panelRect_.y + panelRect_.h;
}

void WorldBackpackPanel::updateMouse(const int mx, const int my) {
    layout();
    hoverSlot_ = containsPoint(mx, my) ? slotAt(mx, my) : -1;
}

void WorldBackpackPanel::clearHover() {
    hoverSlot_ = -1;
}
