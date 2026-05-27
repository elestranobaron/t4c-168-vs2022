#include "gui/T4CInvItemIcons.h"

#include <VSFInterface/vsfinterface.h>

#if defined(LINUX_PORT)

#include <unordered_map>

const std::unordered_map<std::uint16_t, const char *> &T4CInvItemIconMap();

const char *T4CInvItemIconName(const std::uint16_t appearance) {
    const auto &map = T4CInvItemIconMap();
    const auto it = map.find(appearance);
    if (it == map.end()) {
        return nullptr;
    }
    return it->second;
}

struct _sprite *T4CInvItemIconSprite(VSFInterface *vsfi, const std::uint16_t appearance) {
    if (!vsfi) {
        return nullptr;
    }
    const char *name = T4CInvItemIconName(appearance);
    if (!name || !*name) {
        return nullptr;
    }
    return vsfi->get_sprite_by_name(const_cast<char *>(name));
}

#else

const char *T4CInvItemIconName(const std::uint16_t /*appearance*/) {
    return nullptr;
}

struct _sprite *T4CInvItemIconSprite(VSFInterface * /*vsfi*/, const std::uint16_t /*appearance*/) {
    return nullptr;
}

#endif
