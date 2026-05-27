#pragma once

#include <cstdint>

class VSFInterface;
struct _sprite;

/** Nom sprite inventaire pour une apparence objet (table VisualObjectList BIND_INV). */
const char *T4CInvItemIconName(std::uint16_t appearance);

/** Charge et retourne le sprite VSF inventaire, ou nullptr. */
struct _sprite *T4CInvItemIconSprite(VSFInterface *vsfi, std::uint16_t appearance);
