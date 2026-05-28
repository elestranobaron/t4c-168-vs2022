#include "gui/WorldSideMenu.h"

#include <VSFInterface/vsfinterface.h>

#include <algorithm>

namespace {

constexpr int kButtonCount = 7;

/** Encoches Y dans 64kSideBox (68x480) — icônes déjà peintes dans le cadre. */
constexpr int kSlotY[kButtonCount] = {2, 49, 87, 284, 322, 353, 388};

constexpr int kBtnInsetX = 6;
constexpr int kHitW = 40;
constexpr int kHitH = 38;

/** Ordre visuel haut → bas sur le cadre (mesuré sur export BMP + retours in-game). */
constexpr WorldSideMenu::Panel kSlotPanel[kButtonCount] = {
    WorldSideMenu::Panel::CharSheet,
    WorldSideMenu::Panel::BackPack,
    WorldSideMenu::Panel::SpellBook,
    WorldSideMenu::Panel::Options,
    WorldSideMenu::Panel::Macros,
    WorldSideMenu::Panel::GroupPlay,
    WorldSideMenu::Panel::Chatters,
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

    for (int i = 0; i < kButtonCount; ++i) {
        buttons_[i].panel = kSlotPanel[i];
        buttons_[i].rect =
            SDL_Rect{originX_ + kBtnInsetX, originY_ + kSlotY[i], kHitW, kHitH};
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
            switch (buttons_[i].panel) {
                case Panel::Options:
                    return Action::OpenOptions;
                case Panel::BackPack:
                    return Action::OpenBackPack;
                case Panel::CharSheet:
                    return Action::OpenCharSheet;
                case Panel::SpellBook:
                    return Action::OpenSpellBook;
                default:
                    return Action::PanelNotImplemented;
            }
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
