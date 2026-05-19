/**
 * Couche de compatibilité SDL 1.2/2.x utilisée par TnC (Noth) pour compiler avec SDL3.
 * Inclus via #include <SDL/SDL.h> — ne pas inclure directement depuis le code applicatif.
 */
#ifndef TNC_SDL2_COMPAT_H
#define TNC_SDL2_COMPAT_H

#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

/* SDL3 renomme SDL_FreeSurface → éviter le macro oldnames */
#ifdef SDL_FreeSurface
#undef SDL_FreeSurface
#endif

#ifndef SDL_HWSURFACE
#define SDL_HWSURFACE 0u
#endif
#ifndef SDL_SRCCOLORKEY
#define SDL_SRCCOLORKEY 1
#endif
#ifndef SDL_SRCALPHA
#define SDL_SRCALPHA 0x00010000u
#endif
#ifndef SDL_RLEACCEL
#define SDL_RLEACCEL 0u
#endif
#ifndef SDL_INIT_TIMER
#define SDL_INIT_TIMER 0u
#endif

#ifndef SDL_MIX_MAXVOLUME
#define SDL_MIX_MAXVOLUME 128
#endif

inline int TNC_SDL_Init(Uint32 flags) {
    const Uint32 f = flags & ~SDL_INIT_TIMER;
    return SDL_Init(static_cast<SDL_InitFlags>(f)) ? 0 : -1;
}
#undef SDL_Init
#define SDL_Init(flags) TNC_SDL_Init(flags)

inline SDL_Surface *TNC_CreateRGBSurface(Uint32 /*flags*/, int w, int h, int depth, Uint32 /*rmask*/,
                                         Uint32 /*gmask*/, Uint32 /*bmask*/, Uint32 /*amask*/) {
    const SDL_PixelFormat fmt =
        (depth <= 8) ? SDL_PIXELFORMAT_INDEX8 : SDL_PIXELFORMAT_RGBA32;
    SDL_Surface *s = SDL_CreateSurface(w, h, fmt);
    if (s && depth <= 8) {
        SDL_CreateSurfacePalette(s);
    }
    return s;
}
#undef SDL_CreateRGBSurface
#define SDL_CreateRGBSurface(flags, w, h, d, r, g, b, a) \
    TNC_CreateRGBSurface(flags, w, h, d, r, g, b, a)

inline SDL_Surface *TNC_CreateRGBSurfaceFrom(void *pixels, int w, int h, int depth, int pitch,
                                             Uint32 /*rmask*/, Uint32 /*gmask*/, Uint32 /*bmask*/,
                                             Uint32 /*amask*/) {
    const SDL_PixelFormat fmt =
        (depth <= 8) ? SDL_PIXELFORMAT_INDEX8 : SDL_PIXELFORMAT_RGBA32;
    SDL_Surface *s = SDL_CreateSurfaceFrom(w, h, fmt, pixels, pitch);
    if (s && depth <= 8) {
        SDL_CreateSurfacePalette(s);
    }
    return s;
}
#undef SDL_CreateRGBSurfaceFrom
#define SDL_CreateRGBSurfaceFrom(pixels, w, h, d, pitch, r, g, b, a) \
    TNC_CreateRGBSurfaceFrom(pixels, w, h, d, pitch, r, g, b, a)

inline int TNC_SetColors(SDL_Surface *surface, const SDL_Color *colors, int firstcolor, int ncolors) {
    if (!surface || !colors) {
        return 0;
    }
    SDL_Palette *pal = SDL_GetSurfacePalette(surface);
    if (!pal) {
        pal = SDL_CreateSurfacePalette(surface);
    }
    if (!pal) {
        return 0;
    }
    return SDL_SetPaletteColors(pal, colors, firstcolor, ncolors) ? 1 : 0;
}
#undef SDL_SetColors
#define SDL_SetColors(surface, colors, first, n) TNC_SetColors(surface, colors, first, n)

inline int TNC_SetColorKey(SDL_Surface *surface, int flag, Uint32 key) {
    if (!surface) {
        return -1;
    }
    if (!flag) {
        return SDL_SetSurfaceColorKey(surface, false, 0) ? 0 : -1;
    }
    return SDL_SetSurfaceColorKey(surface, true, key) ? 0 : -1;
}
#undef SDL_SetColorKey
#define SDL_SetColorKey(surface, flag, key) TNC_SetColorKey(surface, flag, key)

/** Couleur 0xAARRGGBB telle que TnC (Noth) l'utilisait avec masques ARGB8888. */
inline Uint32 TNC_MapArgb(SDL_Surface *surface, Uint32 argb) {
    if (!surface) {
        return argb;
    }
    const Uint8 a = static_cast<Uint8>((argb >> 24) & 0xFFu);
    const Uint8 r = static_cast<Uint8>((argb >> 16) & 0xFFu);
    const Uint8 g = static_cast<Uint8>((argb >> 8) & 0xFFu);
    const Uint8 b = static_cast<Uint8>(argb & 0xFFu);
    return SDL_MapSurfaceRGBA(surface, r, g, b, a);
}

inline int TNC_FillRect(SDL_Surface *dst, const SDL_Rect *rect, Uint32 color) {
    return SDL_FillSurfaceRect(dst, rect, TNC_MapArgb(dst, color)) ? 0 : -1;
}
#undef SDL_FillRect
#define SDL_FillRect(dst, rect, color) TNC_FillRect(dst, rect, color)

inline int TNC_BlitSurface(SDL_Surface *src, const SDL_Rect *srcrect, SDL_Surface *dst,
                           const SDL_Rect *dstrect) {
    return SDL_BlitSurface(src, srcrect, dst, dstrect) ? 0 : -1;
}
#undef SDL_BlitSurface
#define SDL_BlitSurface(src, sr, dst, dr) TNC_BlitSurface(src, sr, dst, dr)

inline SDL_Surface *TNC_ConvertSurface(SDL_Surface *src, const void * /*fmt*/, Uint32 /*flags*/) {
    if (!src) {
        return nullptr;
    }
    return SDL_ConvertSurface(src, SDL_PIXELFORMAT_RGBA32);
}
#undef SDL_ConvertSurface
#define SDL_ConvertSurface(src, fmt, flags) TNC_ConvertSurface(src, fmt, flags)

inline void TNC_SetAlpha(SDL_Surface *surface, Uint32 /*flags*/, Uint8 alpha) {
    if (!surface) {
        return;
    }
    SDL_SetSurfaceBlendMode(surface, SDL_BLENDMODE_BLEND);
    SDL_SetSurfaceAlphaMod(surface, alpha);
}
#undef SDL_SetAlpha
#define SDL_SetAlpha(surface, flags, alpha) TNC_SetAlpha(surface, flags, alpha)

inline Uint32 TNC_GetTicks32() {
    return static_cast<Uint32>(SDL_GetTicks());
}
#undef SDL_GetTicks
#define SDL_GetTicks() TNC_GetTicks32()

inline void SDL_FreeSurface(SDL_Surface *surface) {
    SDL_DestroySurface(surface);
}

#endif /* TNC_SDL2_COMPAT_H */
