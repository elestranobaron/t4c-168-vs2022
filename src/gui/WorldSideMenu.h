#pragma once

#include <SDL3/SDL.h>

class VSFInterface;

/** Barre laterale T4C (sprites 64kSideBox / 64kSideButton*). */
class WorldSideMenu {
   public:
    enum class Panel {
        SpellBook,
        Options,
        Macros,
        GroupPlay,
        Chatters,
        CharSheet,
        BackPack,
    };

    enum class Action {
        None,
        PanelNotImplemented,
        OpenOptions,
        OpenBackPack,
    };

    void init(VSFInterface *vsf, int screenW, int screenH);

    void setOpen(bool open);
    bool isOpen() const { return open_; }

    /** Largeur reservee a gauche (equivalent SideMenu::GetStartOffsetX). */
    int startOffsetX() const;

    void draw(SDL_Surface *dest);

    /** Coordonnees logiques (1800x1000). */
    Action handleMouse(int mx, int my, bool leftDown, bool leftUp);

    void clearPointerState();

   private:
    struct ButtonSlot {
        const char *suffix{nullptr};
        Panel panel{Panel::SpellBook};
        SDL_Rect rect{};
    };

    struct _sprite *buttonSprite(const char *suffix, bool down) const;
    void layoutButtons();

    VSFInterface *vsfi_{nullptr};
    struct _sprite *box_{nullptr};
    ButtonSlot buttons_[7]{};
    int screenW_{0};
    int screenH_{0};
    int originX_{0};
    int originY_{0};
    int hoverIndex_{-1};
    int pressedIndex_{-1};
    bool open_{false};
    bool layoutDone_{false};
};
