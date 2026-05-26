#pragma once

#include <cstdint>
#include <string>
#include <vector>

/** Objet inventaire / coffre (opcode 18 / 106, aligne BAG_ITEM Windows). */
struct T4CBagItem {
    std::uint16_t appearance{0};
    std::int32_t objectId{0};
    std::uint16_t baseId{0};
    std::uint32_t qty{0};
    std::int32_t charges{0};
};

struct T4CPlayerSkill {
    std::uint16_t skillId{0};
    unsigned char useMode{0};
    std::uint16_t points{0};
    std::uint16_t truePoints{0};
    std::string name;
    std::string description;
};

struct T4CPlayerSpell {
    std::uint16_t spellId{0};
    unsigned char targetType{0};
    std::uint16_t manaCost{0};
    std::int32_t duration{0};
    std::uint16_t level{0};
    std::uint16_t element{0};
    std::uint16_t damageType{0};
    std::int32_t icon{0};
    std::string name;
    std::string description;
};

struct T4CPlayerBackpack {
    bool showUi{false};
    std::int32_t containerId{0};
    std::vector<T4CBagItem> items;
    bool valid{false};
};

struct T4CPlayerBankChest {
    bool uiVisible{false};
    std::vector<T4CBagItem> items;
    bool valid{false};
};

struct T4CPlayerSkillBook {
    std::vector<T4CPlayerSkill> skills;
    bool valid{false};
};

struct T4CPlayerSpellBook {
    unsigned short mana{0};
    unsigned short maxMana{0};
    std::vector<T4CPlayerSpell> spells;
    bool valid{false};
};
