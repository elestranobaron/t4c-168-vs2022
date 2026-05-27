#include "gui/WorldSideMenu.h"

#include <VSFInterface/vsfinterface.h>

#include <algorithm>
#include <cstdio>

namespace {

constexpr int kButtonCount = 7;

struct ButtonDef {
    const char *suffix;
    WorldSideMenu::Panel panel;
};

constexpr ButtonDef kButtonDefs[kButtonCount] = {
    {"SpellBook", WorldSideMenu::Panel::SpellBook},
    {"Options", WorldSideMenu::Panel::Options},
    {"Macros", WorldSideMenu::Panel::Macros},
    {"GroupPlay", WorldSideMenu::Panel::GroupPlay},
    {"Chatters", WorldSideMenu::Panel::Chatters},
    {"CharSheet", WorldSideMenu::Panel::CharSheet},
    {"BackPack", WorldSideMenu::Panel::BackPack},
};

bool pointInRect(int x, int y, const SDL_Rect &r) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

}  // namespace

void WorldSideMenu::init(VSFInterface *vsf, const int screenW, const int screenH) {
    vsfi_ = vsf;
    screenW_ = screenW;
    screenH_ = screenH;
    box_ = vsfi_ ? vsfi_->get_sprite_by_name("64kSideBox") : nullptr;
    layoutDone_ = false;
}

void WorldSideMenu::setOpen(const bool open) {
    open_ = open;
    if (!open_) {
        clearPointerState();
    }
}

int WorldSideMenu::startOffsetX() const {
    if (!open_ || !box_ || !box_->sdl_sprite) {
        return 0;
    }
    return box_->sdl_sprite->w;
}

struct _sprite *WorldSideMenu::buttonSprite(const char *suffix, const bool down) const {
    if (!vsfi_ || !suffix) {
        return nullptr;
    }
    char name[80];
    std::snprintf(name, sizeof(name), "64kSideButton%s%s", down ? "Down" : "HighLight", suffix);
    return vsfi_->get_sprite_by_name(name);
}

void WorldSideMenu::layoutButtons() {
    if (layoutDone_) {
        return;
    }
    layoutDone_ = true;

    originX_ = 0;
    originY_ = 0;
    if (box_ && box_->sdl_sprite) {
        originY_ = std::max(0, (screenH_ - box_->sdl_sprite->h) / 2);
    }

    struct _sprite *sample = buttonSprite(kButtonDefs[0].suffix, false);
    const int btnW = (sample && sample->sdl_sprite) ? sample->sdl_sprite->w : 48;
    const int btnH = (sample && sample->sdl_sprite) ? sample->sdl_sprite->h : 42;

    const int boxH = (box_ && box_->sdl_sprite) ? box_->sdl_sprite->h : btnH * kButtonCount;
    const int totalBtnH = btnH * kButtonCount;
    int startY = originY_ + std::max(0, (boxH - totalBtnH) / 2);

    for (int i = 0; i < kButtonCount; ++i) {
        buttons_[i].suffix = kButtonDefs[i].suffix;
        buttons_[i].panel = kButtonDefs[i].panel;
        buttons_[i].rect = SDL_Rect{originX_, startY + i * btnH, btnW, btnH};
    }
}

void WorldSideMenu::draw(SDL_Surface *dest) {
    if (!open_ || !dest || !vsfi_) {
        return;
    }
    layoutButtons();

    if (box_ && box_->sdl_sprite) {
        SDL_Rect dst{originX_, originY_, box_->sdl_sprite->w, box_->sdl_sprite->h};
        SDL_BlitSurface(box_->sdl_sprite, nullptr, dest, &dst);
    }

    for (int i = 0; i < kButtonCount; ++i) {
        const ButtonSlot &b = buttons_[i];
        const bool down = (i == pressedIndex_);
        const bool hot = (i == hoverIndex_) && !down;
        struct _sprite *spr = buttonSprite(b.suffix, down);
        if (!spr || !spr->sdl_sprite) {
            spr = buttonSprite(b.suffix, false);
        }
        if (!spr || !spr->sdl_sprite) {
            continue;
        }
        SDL_Rect dst{b.rect.x, b.rect.y, spr->sdl_sprite->w, spr->sdl_sprite->h};
        SDL_BlitSurface(spr->sdl_sprite, nullptr, dest, &dst);
        if (hot && i != pressedIndex_) {
            (void)hot;
        }
    }
}

WorldSideMenu::Action WorldSideMenu::handleMouse(const int mx, const int my, const bool leftDown,
                                                 const bool leftUp) {
    if (!open_) {
        return Action::None;
    }
    layoutButtons();

    hoverIndex_ = -1;
    for (int i = 0; i < kButtonCount; ++i) {
        if (!pointInRect(mx, my, buttons_[i].rect)) {
            continue;
        }
        hoverIndex_ = i;
        if (leftDown) {
            pressedIndex_ = i;
        }
        if (leftUp && pressedIndex_ == i) {
            pressedIndex_ = -1;
            if (buttons_[i].panel == Panel::Options) {
                return Action::OpenOptions;
            }
            if (buttons_[i].panel == Panel::BackPack) {
                return Action::OpenBackPack;
            }
            return Action::PanelNotImplemented;
        }
        break;
    }

    if (leftUp) {
        pressedIndex_ = -1;
    }
    return Action::None;
}

void WorldSideMenu::clearPointerState() {
    hoverIndex_ = -1;
    pressedIndex_ = -1;
}
