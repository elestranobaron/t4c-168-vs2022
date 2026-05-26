#if defined(LINUX_PORT) && !defined(_WIN32)

#include "network/T4CLoginSession.h"

#include "network/T4CNetworkDebugLog.h"

#include <SDL3/SDL.h>

#include <arpa/inet.h>
#include <atomic>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <netdb.h>
#include <cstdio>
#include <cstdint>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <vector>

#include "CommCenter.h"
#include "PacketTypes.h"

#ifndef HIBYTE
#define HIBYTE(w) static_cast<unsigned char>(((static_cast<unsigned>(w)) >> 8) & 0xffu)
#endif
#ifndef LOBYTE
#define LOBYTE(w) static_cast<unsigned char>((static_cast<unsigned>(w)) & 0xffu)
#endif

constexpr std::uint16_t kDefaultServerPort = 11677;

static std::mutex g_sessionMutex;
static std::unique_ptr<CCommCenter> g_comm;
static CCommMonitorEmpty g_monitorEmpty;

static std::atomic<bool> g_pendingCharacterList{false};
static std::atomic<bool> g_pendingEnterWorld{false};
static std::atomic<bool> g_successAlreadyShown{false};
static std::mutex g_characterMutex;
static std::vector<T4CCharacterSlot> g_characterList;
static int g_maxCharactersPerAccount{3};
static T4CEnterWorldSpawn g_enterWorldSpawn{};

/** Cible UDP du login (copiee au Start pour reponses depuis le thread d’analyse). */
static sockaddr_in g_serverAddr{};
/** Dernier compte utilise (re-register apres logout force). */
static std::string g_lastLogin;
static std::string g_lastPassword;
/** Dernier refus serveur = compte deja connecte (cooldown si l'utilisateur reclique Connect). */
static std::atomic<bool> g_accountBlockedAlreadyLogged{false};
static std::atomic<bool> g_logoutSentThisSession{false};
static std::mutex g_logoutThreadMutex;
static std::thread g_logoutThread;
static std::atomic<bool> g_logoutInProgress{false};
/** Apres logout monde : le serveur peut encore ping l'ancien port UDP ~15–20 s. */
static std::mutex g_reconnectCooldownMutex;
static std::chrono::steady_clock::time_point g_reconnectAllowedAfter{};
constexpr int kPostLogoutReconnectCooldownSec = 30;
constexpr int kPostAlreadyLoggedCooldownSec = 30;
/** Apres Esc monde : le serveur garde souvent l'ancien port UDP > 20 s. */
static std::chrono::steady_clock::time_point g_worldLogoutFinishedAt{};
/** 0 inactif, 1 attente RQ_RegisterAccount (14), 2 attente RQ_AuthenticateServerVersion (99),
 *  3 auth OK, 4 liste persos (26), 5 attente reponse 13, 6 en jeu (46+60 envoyes). */
static std::atomic<int> g_pipelineStep{0};
static std::atomic<bool> g_waitingPutPlayerInGame{false};
static std::mutex g_putPlayerErrorMutex;
static std::string g_putPlayerErrorMessage;
static std::chrono::steady_clock::time_point g_putPlayerRequestAt{};
static std::atomic<bool> g_waitingCreatePlayer{false};
static std::mutex g_createPlayerErrorMutex;
static std::string g_createPlayerErrorMessage;
static std::chrono::steady_clock::time_point g_createPlayerRequestAt{};
static std::atomic<bool> g_pendingCreatePlayerSuccess{false};
static std::atomic<bool> g_inCreateRerollPhase{false};
static std::atomic<bool> g_waitingCreateReroll{false};
static std::mutex g_rolledStatsMutex;
static T4CCharacterRolledStats g_rolledStats{};
static std::atomic<bool> g_rolledStatsPending{false};
static std::string g_pendingCreatePlayerName;
static std::atomic<bool> g_autoEnterWorldAfterCreate{false};
static std::chrono::steady_clock::time_point g_createRerollRequestAt{};
static std::atomic<bool> g_waitingDeletePlayer{false};
static std::mutex g_deletePlayerErrorMutex;
static std::string g_deletePlayerErrorMessage;
static std::string g_lastDeletePlayerName;
static std::chrono::steady_clock::time_point g_deletePlayerRequestAt{};
/** Apres envoi opcode 46, en attente de la reponse serveur. */
static std::atomic<bool> g_waitingFromPreInGame{false};
/** 13 OK : envoyer 46+60 apres opcode 18 (comme la fin du flux 13 cote serveur). */
static std::atomic<bool> g_pendingPost13Pipeline{false};
/** -1 inconnu, 0 OK, 1 erreur (octet resultat paquet 46). */
static std::atomic<int> g_fromPreInGameResult{-1};

/** Unit::PacketPopup — type filaire 10004 (position + apparence), pas un RQ_GetStatus. */
static constexpr std::uint16_t kTfcPacketPopup = 10004;

static T4CActivePlayer g_activePlayer{};
static std::mutex g_activePlayerMutex;
static T4CPlayerStatus g_playerStatus{};
static std::mutex g_playerStatusMutex;
static std::atomic<bool> g_playerStatusPending{false};
static T4CPlayerBackpack g_backpack{};
static T4CPlayerSkillBook g_skillBook{};
static T4CPlayerSpellBook g_spellBook{};
static T4CPlayerBankChest g_bankChest{};
static T4CPlayerEquipment g_equipment{};
static std::mutex g_inventoryMutex;
static std::atomic<bool> g_inventoryPending{false};
/** Classe derivee du questionnaire (opcode 25), cle = nom perso. */
static std::unordered_map<std::string, std::uint8_t> g_characterClassByName;
static std::atomic<bool> g_playerPopupPending{false};
static T4CPlayerTeleport g_pendingTeleport{};
static std::atomic<bool> g_playerTeleportPending{false};

static std::mutex g_remoteEventsMutex;
static std::vector<T4CRemoteUnitEvent> g_remoteEvents;
static std::mutex g_groundObjectsMutex;
static std::unordered_map<std::int32_t, T4CGroundObjectMarker> g_groundObjectsByUnitId;

/** __EVENT_OBJECT_MOVED (EventListing.h) — deplacement broadcast. */
static constexpr std::uint16_t kEventObjectMoved = 1;
/** __EVENT_OBJECT_APPEARED_LIST — lot initial GetNearItems / peripherie. */
static constexpr std::uint16_t kEventObjectAppearedList = 16;

constexpr std::uint16_t kTfcStillConnected = 10; /* TFCSocket.h — serveur ; reponse = RQ_Ack (10) */

/** Long opcode 99 : egale `TFCServer->dwVersion` (INI Version=14, pas le libelle produit « 1.68 »). Voir main.cpp : `Send << (long)Player.Version`. */
constexpr std::int32_t kTfcClientVersionLong = 14;

struct ParsedEndpoint {
    std::string host;
    std::uint16_t port{kDefaultServerPort};
};

static std::string StripEmbeddedPortSuffix(const std::string &s) {
    const std::size_t pos = s.rfind(':');
    if (pos == std::string::npos || pos + 1 >= s.size()) {
        return s;
    }
    for (std::size_t i = pos + 1; i < s.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) {
            return s;
        }
    }
    return s.substr(0, pos);
}

static std::uint16_t ParsePortString(const std::string &portField, std::uint16_t defaultPort) {
    std::string t = portField;
    while (!t.empty() && std::isspace(static_cast<unsigned char>(t.front()))) {
        t.erase(0, 1);
    }
    while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back()))) {
        t.pop_back();
    }
    if (t.empty()) {
        return defaultPort;
    }
    for (unsigned char c : t) {
        if (!std::isdigit(c)) {
            return defaultPort;
        }
    }
    const unsigned long p = std::strtoul(t.c_str(), nullptr, 10);
    if (p == 0 || p > 65535ul) {
        return defaultPort;
    }
    return static_cast<std::uint16_t>(p);
}

static ParsedEndpoint BuildEndpoint(const std::string &hostField, const std::string &portField) {
    ParsedEndpoint out;
    out.host = StripEmbeddedPortSuffix(hostField);
    out.port = ParsePortString(portField, kDefaultServerPort);
    return out;
}

static void AppendTfcpayload(std::vector<unsigned char> &v, const std::string &s) {
    for (unsigned char c : s) {
        v.push_back(c);
    }
}

static void PushBeShort(std::vector<unsigned char> &v, std::uint16_t w) {
    v.push_back(HIBYTE(w));
    v.push_back(LOBYTE(w));
}

/** Meme ordre que TFCPacket::operator<<(long) (MSVC / client 1.68). */
static void PushBeInt32Msf(std::vector<unsigned char> &v, std::int32_t x) {
    const auto u = static_cast<std::uint32_t>(x);
    v.push_back(static_cast<unsigned char>((u >> 24) & 0xffu));
    v.push_back(static_cast<unsigned char>((u >> 16) & 0xffu));
    v.push_back(static_cast<unsigned char>((u >> 8) & 0xffu));
    v.push_back(static_cast<unsigned char>(u & 0xffu));
}

static std::int32_t ReadBeInt32Msf(const unsigned char *d, int off, int len) {
    if (!d || off + 4 > len) {
        return 0;
    }
    const std::uint32_t u = (static_cast<std::uint32_t>(d[off]) << 24) |
                            (static_cast<std::uint32_t>(d[off + 1]) << 16) |
                            (static_cast<std::uint32_t>(d[off + 2]) << 8) |
                            static_cast<std::uint32_t>(d[off + 3]);
    return static_cast<std::int32_t>(u);
}

static std::uint64_t ReadBeInt64Msf(const unsigned char *d, int off, int len) {
    if (!d || off + 8 > len) {
        return 0;
    }
    const std::uint64_t hi = static_cast<std::uint64_t>(static_cast<std::uint32_t>(ReadBeInt32Msf(d, off, len)));
    const std::uint64_t lo = static_cast<std::uint64_t>(static_cast<std::uint32_t>(ReadBeInt32Msf(d, off + 4, len)));
    return (hi << 32) | lo;
}

static std::uint16_t ReadBeUint16(const unsigned char *d, int off, int len);

static constexpr std::uint16_t kMaxTfcStringLen = 256;
static constexpr std::uint16_t kMaxInventoryListCount = 128;

static bool ReadTfcString(const unsigned char *data, int len, int &off, std::string *out) {
    if (!data || !out || off + 2 > len) {
        return false;
    }
    const std::uint16_t slen = ReadBeUint16(data, off, len);
    off += 2;
    if (slen > kMaxTfcStringLen || off + slen > len) {
        return false;
    }
    out->assign(reinterpret_cast<const char *>(data + off), slen);
    off += slen;
    return true;
}

static void MarkInventoryUpdated() {
    g_inventoryPending.store(true);
}

static bool ParseBagItemList(const unsigned char *data, int len, int &off, std::vector<T4CBagItem> *out) {
    if (!data || !out || off + 2 > len) {
        return false;
    }
    const std::uint16_t countRaw = ReadBeUint16(data, off, len);
    off += 2;
    const unsigned count = std::min<unsigned>(countRaw, kMaxInventoryListCount);
    out->clear();
    out->reserve(count);
    for (unsigned i = 0; i < count; ++i) {
        if (off + 20 > len) {
            return false;
        }
        T4CBagItem it{};
        it.appearance = ReadBeUint16(data, off, len);
        off += 2;
        it.objectId = ReadBeInt32Msf(data, off, len);
        off += 4;
        it.baseId = ReadBeUint16(data, off, len);
        off += 2;
        const std::int32_t qty = ReadBeInt32Msf(data, off, len);
        off += 4;
        it.qty = qty < 0 ? 0u : static_cast<std::uint32_t>(qty);
        it.charges = ReadBeInt32Msf(data, off, len);
        off += 4;
        out->push_back(it);
    }
    return true;
}

static void HandleViewBackpack(const unsigned char *data, int len) {
    if (len < 13) {
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Warn, "[PHASE] RQ_ViewBackpack (18) tronque (%d octets).", len);
        return;
    }
    int off = 6;
    T4CPlayerBackpack bp{};
    bp.showUi = data[off] != 0;
    ++off;
    bp.containerId = ReadBeInt32Msf(data, off, len);
    off += 4;
    if (!ParseBagItemList(data, len, off, &bp.items)) {
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Warn,
                               "[PHASE] RQ_ViewBackpack (18) liste objets tronquee (%d octets).", len);
        return;
    }
    bp.valid = true;
    {
        std::lock_guard<std::mutex> lock(g_inventoryMutex);
        g_backpack = bp;
    }
    MarkInventoryUpdated();
    T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase,
                           "[PHASE] RQ_ViewBackpack (18) — %zu objet(s), containerId=%d, show=%d.",
                           bp.items.size(), static_cast<int>(bp.containerId), bp.showUi ? 1 : 0);
}

static void HandleViewEquipped(const unsigned char *data, int len) {
    if (len < 7) {
        return;
    }
    int off = 6;
    T4CPlayerEquipment eq{};
    eq.rangedAttack = data[off++] != 0;
    constexpr int kEquipSlots = 13;
    eq.items.reserve(kEquipSlots);
    for (int slot = 0; slot < kEquipSlots; ++slot) {
        if (off + 14 > len) {
            break;
        }
        T4CEquippedItem item{};
        item.slot = static_cast<T4CEquipSlot>(slot);
        item.objectId = ReadBeInt32Msf(data, off, len);
        off += 4;
        item.appearance = ReadBeUint16(data, off, len);
        off += 2;
        item.baseId = ReadBeUint16(data, off, len);
        off += 2;
        item.qty = ReadBeUint16(data, off, len);
        off += 2;
        item.charges = ReadBeInt32Msf(data, off, len);
        off += 4;
        if (!ReadTfcString(data, len, off, &item.name)) {
            break;
        }
        eq.items.push_back(std::move(item));
    }
    const std::size_t slotCount = eq.items.size();
    eq.valid = slotCount == kEquipSlots;
    const bool ranged = eq.rangedAttack;
    {
        std::lock_guard<std::mutex> lock(g_inventoryMutex);
        g_equipment = std::move(eq);
    }
    MarkInventoryUpdated();
    T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase,
                           "[PHASE] RQ_ViewEquiped (19) — %zu slots, ranged=%d.",
                           slotCount, ranged ? 1 : 0);
}

static void HandleGetSkillList(const unsigned char *data, int len) {
    if (len < 8) {
        return;
    }
    int off = 6;
    const std::uint16_t countRaw = ReadBeUint16(data, off, len);
    off += 2;
    const unsigned count = std::min<unsigned>(countRaw, kMaxInventoryListCount);
    T4CPlayerSkillBook book{};
    book.skills.reserve(count);
    for (unsigned i = 0; i < count; ++i) {
        if (off + 7 > len) {
            break;
        }
        T4CPlayerSkill sk{};
        sk.skillId = ReadBeUint16(data, off, len);
        off += 2;
        sk.useMode = static_cast<unsigned char>(data[off++]);
        sk.points = ReadBeUint16(data, off, len);
        off += 2;
        sk.truePoints = ReadBeUint16(data, off, len);
        off += 2;
        if (!ReadTfcString(data, len, off, &sk.name) || !ReadTfcString(data, len, off, &sk.description)) {
            T4CNetworkDebugLogKind(T4CMatrixLogKind::Warn,
                                   "[PHASE] RQ_GetSkillList (39) skill %u tronquee.", static_cast<unsigned>(i));
            break;
        }
        book.skills.push_back(std::move(sk));
    }
    book.valid = !book.skills.empty();
    {
        std::lock_guard<std::mutex> lock(g_inventoryMutex);
        g_skillBook = book;
    }
    MarkInventoryUpdated();
    T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase, "[PHASE] RQ_GetSkillList (39) — %zu skill(s).",
                           book.skills.size());
}

static void HandleSendSpellList(const unsigned char *data, int len) {
    if (len < 13) {
        return;
    }
    int off = 6;
    /* bUpdate */ ++off;
    T4CPlayerSpellBook book{};
    book.mana = ReadBeUint16(data, off, len);
    off += 2;
    book.maxMana = ReadBeUint16(data, off, len);
    off += 2;
    const std::uint16_t countRaw = ReadBeUint16(data, off, len);
    off += 2;
    const unsigned count = std::min<unsigned>(countRaw, kMaxInventoryListCount);
    book.spells.reserve(count);
    for (unsigned i = 0; i < count; ++i) {
        if (off + 2 > len) {
            break;
        }
        const std::uint16_t spellId = ReadBeUint16(data, off, len);
        off += 2;
        if (spellId == 0) {
            continue;
        }
        T4CPlayerSpell sp{};
        sp.spellId = spellId;
        if (off + 19 > len) {
            break;
        }
        sp.targetType = static_cast<unsigned char>(data[off++]);
        sp.manaCost = ReadBeUint16(data, off, len);
        off += 2;
        sp.duration = ReadBeInt32Msf(data, off, len);
        off += 4;
        sp.level = ReadBeUint16(data, off, len);
        off += 2;
        sp.element = ReadBeUint16(data, off, len);
        off += 2;
        sp.damageType = ReadBeUint16(data, off, len);
        off += 2;
        sp.icon = ReadBeInt32Msf(data, off, len);
        off += 4;
        if (!ReadTfcString(data, len, off, &sp.description) || !ReadTfcString(data, len, off, &sp.name)) {
            T4CNetworkDebugLogKind(T4CMatrixLogKind::Warn,
                                   "[PHASE] RQ_SendSpellList (62) sort %u tronque.", static_cast<unsigned>(spellId));
            break;
        }
        book.spells.push_back(std::move(sp));
    }
    book.valid = true;
    {
        std::lock_guard<std::mutex> lock(g_inventoryMutex);
        g_spellBook = book;
    }
    MarkInventoryUpdated();
    T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase, "[PHASE] RQ_SendSpellList (62) — %zu sort(s), mana %u/%u.",
                           book.spells.size(), static_cast<unsigned>(book.mana), static_cast<unsigned>(book.maxMana));
}

static void HandleChestContents(const unsigned char *data, int len) {
    if (len < 8) {
        return;
    }
    int off = 6;
    T4CPlayerBankChest chest{};
    chest.uiVisible = true;
    if (!ParseBagItemList(data, len, off, &chest.items)) {
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Warn, "[PHASE] RQ_ChestContents (106) tronque (%d octets).", len);
        return;
    }
    chest.valid = true;
    {
        std::lock_guard<std::mutex> lock(g_inventoryMutex);
        g_bankChest = chest;
    }
    MarkInventoryUpdated();
    T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase, "[PHASE] RQ_ChestContents (106) — %zu objet(s) coffre banque.",
                           chest.items.size());
}

static void HandleQueryItemName(const unsigned char *data, int len) {
    if (len < 11) {
        return;
    }
    int off = 6;
    const unsigned char place = data[off++];
    const std::int32_t objectId = ReadBeInt32Msf(data, off, len);
    off += 4;
    std::string name;
    if (!ReadTfcString(data, len, off, &name)) {
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Warn, "[PHASE] RQ_QueryItemName (59) nom tronque (%d octets).", len);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(g_inventoryMutex);
        auto apply = [&](std::vector<T4CBagItem> &items) {
            for (T4CBagItem &it : items) {
                if (it.objectId == objectId) {
                    it.name = name;
                    it.nameQueryPending = false;
                    return true;
                }
            }
            return false;
        };
        if (place == 0) {
            apply(g_backpack.items);
        } else if (place == 1) {
            apply(g_bankChest.items);
        }
    }
    MarkInventoryUpdated();
    T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase,
                           "[PHASE] RQ_QueryItemName (59) place=%u id=%d — «%s».", static_cast<unsigned>(place),
                           static_cast<int>(objectId), name.c_str());
}

static void HandleShowChest() {
    {
        std::lock_guard<std::mutex> lock(g_inventoryMutex);
        g_bankChest.uiVisible = true;
    }
    MarkInventoryUpdated();
    T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase, "[PHASE] RQ_ShowChest (109) — UI coffre banque.");
}

static void HandleHideChest() {
    {
        std::lock_guard<std::mutex> lock(g_inventoryMutex);
        g_bankChest.uiVisible = false;
        g_bankChest.items.clear();
        g_bankChest.valid = false;
    }
    MarkInventoryUpdated();
    T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase, "[PHASE] RQ_HideChest (110) — fermeture coffre banque.");
}

static std::uint16_t ReadBeUint16(const unsigned char *d, int off, int len) {
    if (!d || off + 2 > len) {
        return 0;
    }
    return static_cast<std::uint16_t>((static_cast<unsigned>(d[off]) << 8) | static_cast<unsigned>(d[off + 1]));
}

static std::vector<unsigned char> BuildAuthenticateServerVersionPacket(std::int32_t clientVersionLong) {
    std::vector<unsigned char> v;
    v.assign(4, 0);
    PushBeShort(v, static_cast<std::uint16_t>(RQ_AuthenticateServerVersion));
    PushBeInt32Msf(v, clientVersionLong);
    return v;
}

static std::vector<unsigned char> BuildOpcodeOnlyPacket(std::uint16_t opcode) {
    std::vector<unsigned char> v;
    v.assign(4, 0);
    PushBeShort(v, opcode);
    return v;
}

/** Comme ExitThreadGame (main.cpp 1.68) : RQ_SafePlug (123) + octet 0. */
static std::vector<unsigned char> BuildSafePlugPacket() {
    std::vector<unsigned char> v;
    v.assign(4, 0);
    PushBeShort(v, static_cast<std::uint16_t>(RQ_SafePlug));
    v.push_back(0);
    return v;
}

/* RQ_ExitGame (20). Le serveur (RQFUNC_ExitGame) accepte si lNow-lLastPlayerEvent>=13 OU
 * en zone safe haven, et appelle alors user->DeletePlayer() : libere le slot, declenche
 * Logoff() -> DELETE FROM OnlineUsers. Sans cet envoi, le compte reste "deja connecte"
 * jusqu'a l'idle timeout serveur (75 s), bloquant la reconnexion. */
static std::vector<unsigned char> BuildExitGamePacket() {
    std::vector<unsigned char> v;
    v.assign(4, 0);
    PushBeShort(v, static_cast<std::uint16_t>(RQ_ExitGame));
    return v;
}

/** Delai apres logout sur la session UDP courante (threads reseau actifs pendant l’attente). */
static void WaitAfterLogoutFlush(int pipelineStep) {
    if (pipelineStep >= 4) {
        /* main.cpp ExitThreadGame : Sleep(1000) x 15 apres RQ_SafePlug. */
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Net,
                               "[NET] Compte a decrocher : attente 15 s (decompte SafePlug serveur)…");
        for (int sec = 1; sec <= 15; ++sec) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            if (sec == 5 || sec == 10 || sec == 15) {
                T4CNetworkDebugLog("[NET] SafePlug attente %d/15 s.", sec);
            }
        }
    } else if (pipelineStep >= 2) {
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
    } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(450));
    }
}

/** Comme ExitThreadGame (main.cpp 1.68) : uniquement RQ_SafePlug (123) + octet 0 — pas de 38. */
static void SendSafePlugLogoutLocked(const char *reason) {
    if (!g_comm) {
        return;
    }
    const std::vector<unsigned char> safePkt = BuildSafePlugPacket();
    g_comm->SendPacket(g_serverAddr, const_cast<LPBYTE>(safePkt.data()), static_cast<int>(safePkt.size()), 0, 0);
    T4CNetworkDebugLogKind(T4CMatrixLogKind::Net,
                           "[NET] Logout (%s) : RQ_SafePlug (123) seul (aligne ExitThreadGame 1.68).", reason);
}

static void SendInGameLogoutLocked(const char *reason) {
    SendSafePlugLogoutLocked(reason);
}

static void DestroyCommOutsideLock();

static void SetReconnectCooldown(int seconds) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    std::lock_guard<std::mutex> lock(g_reconnectCooldownMutex);
    if (until > g_reconnectAllowedAfter) {
        g_reconnectAllowedAfter = until;
    }
}

static int GetReconnectCooldownSecondsUnlocked() {
    const auto now = std::chrono::steady_clock::now();
    if (now >= g_reconnectAllowedAfter) {
        return 0;
    }
    return static_cast<int>(
        std::chrono::duration_cast<std::chrono::seconds>(g_reconnectAllowedAfter - now).count() + 1);
}

static void WaitForServerAccountRelease(int seconds, const char *context) {
    T4CNetworkDebugLogKind(T4CMatrixLogKind::Net,
                           "[NET] Attente liberation compte serveur (%d s, %s)…", seconds, context);
    for (int sec = 1; sec <= seconds; ++sec) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (sec == 5 || sec == 10 || sec == 15 || sec == seconds) {
            T4CNetworkDebugLog("[NET] Liberation compte %d/%d s.", sec, seconds);
        }
    }
}

static bool RecentlyCompletedWorldLogout() {
    if (g_worldLogoutFinishedAt.time_since_epoch().count() == 0) {
        return false;
    }
    return std::chrono::steady_clock::now() - g_worldLogoutFinishedAt < std::chrono::seconds(90);
}

/** Ferme la session login sans SafePlug (nouveau port UDP : 38+123 ne libere pas la session monde). */
static void DestroyLoginSessionFast(const char *reason) {
    {
        std::lock_guard<std::mutex> lock(g_sessionMutex);
        g_pipelineStep.store(0);
    }
    DestroyCommOutsideLock();
    T4CNetworkDebugLog("[NET] Session login fermee immediatement (%s).", reason);
}

static void SendExitGameLocked();
static void ResetInGameClientStateAfterForcedExit();

/** Ferme la session UDP proprement (hors mutex pendant l'attente / destroy). */
static void TeardownAndDestroyComm(int pipelineStep, const char *reason, bool waitForServerRelease = false) {
    bool hadComm = false;
    const bool logoutAlreadyStarted = g_logoutSentThisSession.load();
    const bool sendSafePlug = (pipelineStep >= 4) && !logoutAlreadyStarted;
    {
        std::lock_guard<std::mutex> lock(g_sessionMutex);
        hadComm = (g_comm != nullptr);
        if (hadComm && sendSafePlug) {
            SendSafePlugLogoutLocked(reason);
            g_logoutSentThisSession.store(true);
            if (pipelineStep >= 6) {
                SendExitGameLocked();
            }
        } else if (hadComm && logoutAlreadyStarted) {
            T4CNetworkDebugLogKind(T4CMatrixLogKind::Net,
                                   "[NET] Fermeture session (%s, etape %d) — logout deja amorce.", reason,
                                   pipelineStep);
        } else if (hadComm) {
            T4CNetworkDebugLogKind(T4CMatrixLogKind::Net,
                                   "[NET] Fermeture sans SafePlug (%s, etape %d).", reason, pipelineStep);
        }
        g_pipelineStep.store(0);
    }
    if (!hadComm) {
        return;
    }
    if (pipelineStep >= 4) {
        if (logoutAlreadyStarted) {
            /* DisconnectInGame a deja envoye SafePlug+ExitGame : laisser le serveur traiter. */
            std::this_thread::sleep_for(std::chrono::milliseconds(800));
        } else {
            WaitAfterLogoutFlush(pipelineStep);
            if (pipelineStep >= 6) {
                std::lock_guard<std::mutex> lock(g_sessionMutex);
                if (g_comm) {
                    SendExitGameLocked();
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    } else if (waitForServerRelease) {
        WaitForServerAccountRelease(15, reason);
    }
    DestroyCommOutsideLock();
    T4CNetworkDebugLog("[NET] Session UDP fermee (%s).", reason);
}

static bool TfcHeaderLooksSane(const unsigned char *data, int len) {
    if (!data || len < 4) {
        return false;
    }
    return data[0] == 0 && data[1] == 0 && data[2] == 0 && data[3] == 0;
}

static void SendToServerLocked(const std::vector<unsigned char> &payload) {
    std::lock_guard<std::mutex> lock(g_sessionMutex);
    if (!g_comm || payload.empty()) {
        return;
    }
    std::vector<unsigned char> copy = payload;
    g_comm->SendPacket(g_serverAddr, copy.data(), static_cast<int>(copy.size()), 0, 0);
}

static void SendStillConnectedAck() {
    const auto ack = BuildOpcodeOnlyPacket(static_cast<std::uint16_t>(RQ_Ack));
    SendToServerLocked(ack);
    T4CNetworkDebugLog("[UDP] -> RQ_Ack (10) en reponse a TFCStillConnected.");
}

static void SendFromPreInGameToInGameLocked() {
    const auto pkt = BuildOpcodeOnlyPacket(static_cast<std::uint16_t>(RQ_FromPreInGameToInGame));
    g_waitingFromPreInGame.store(true);
    g_fromPreInGameResult.store(-1);
    SendToServerLocked(pkt);
    T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase,
                           "[PHASE] Envoi RQ_FromPreInGameToInGame (46) — etat serveur « en jeu » (packethandling.cpp).");
    T4CNetworkDebugLog("[UDP] -> file d'envoi : FromPreInGameToInGame (%zu octets TFC).", pkt.size());
}

static void SendPostAuthRequests() {
    const auto pcList = BuildOpcodeOnlyPacket(static_cast<std::uint16_t>(RQ_GetPersonnalPClist));
    SendToServerLocked(pcList);
    g_pipelineStep.store(3);
    T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase,
                           "[PHASE] Envoi RQ_GetPersonnalPClist (26), attente liste persos.");
    T4CNetworkDebugLog("[UDP] -> file d'envoi : RQ_GetPersonnalPClist (%zu octets TFC).", pcList.size());
}

static std::vector<unsigned char> BuildPutPlayerInGamePacket(const std::string &playerName) {
    std::vector<unsigned char> v;
    v.assign(4, 0);
    PushBeShort(v, static_cast<std::uint16_t>(RQ_PutPlayerInGame));
    const std::size_t n = std::min(playerName.size(), static_cast<std::size_t>(255));
    v.push_back(static_cast<unsigned char>(n));
    AppendTfcpayload(v, playerName.substr(0, n));
    return v;
}

static std::vector<unsigned char> BuildCreatePlayerPacket(const std::string &name, unsigned char sex,
                                                          const unsigned char stats[5]) {
    std::vector<unsigned char> v;
    v.assign(4, 0);
    PushBeShort(v, static_cast<std::uint16_t>(RQ_CreatePlayer));
    for (int i = 0; i < 5; ++i) {
        v.push_back(stats[i]);
    }
    v.push_back(sex);
    const std::size_t n = std::min(name.size(), static_cast<std::size_t>(255));
    v.push_back(static_cast<unsigned char>(n));
    AppendTfcpayload(v, name.substr(0, n));
    return v;
}

static std::vector<unsigned char> BuildQueryNameExistencePacket(const std::string &name) {
    std::vector<unsigned char> v;
    v.assign(4, 0);
    PushBeShort(v, static_cast<std::uint16_t>(RQ_QueryNameExistence));
    const std::size_t n = std::min(name.size(), static_cast<std::size_t>(255));
    PushBeShort(v, static_cast<std::uint16_t>(n));
    AppendTfcpayload(v, name.substr(0, n));
    return v;
}

static void RequestCharacterListRefresh() {
    const auto pcList = BuildOpcodeOnlyPacket(static_cast<std::uint16_t>(RQ_GetPersonnalPClist));
    SendToServerLocked(pcList);
    T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase, "[PHASE] Envoi RQ_GetPersonnalPClist (26) — refresh liste persos.");
}

static std::vector<unsigned char> BuildDeletePlayerPacket(const std::string &playerName) {
    std::vector<unsigned char> v;
    v.assign(4, 0);
    PushBeShort(v, static_cast<std::uint16_t>(RQ_DeletePlayer));
    const std::size_t n = std::min(playerName.size(), static_cast<std::size_t>(255));
    v.push_back(static_cast<unsigned char>(n));
    AppendTfcpayload(v, playerName.substr(0, n));
    return v;
}

static void LogPayloadHexNet(const unsigned char *data, int len, int maxBytes);

static const char *DeletePlayerErrorText(unsigned char code) {
    switch (code) {
        case 1:
            return "Personnage introuvable.";
        case 2:
            return "Ce personnage n'appartient pas au compte.";
        case 3:
            return "Echec SQL lors de la suppression (PlayingCharacters/BDD).";
        case 7:
            return "Personnage encore marque en jeu cote serveur.";
        default:
            return "Suppression refusee par le serveur.";
    }
}

/** Reponse 15 OK : souvent longueur nom @6 + octets nom (Windows n'analyse pas ce paquet). */
static bool DeleteReplyLooksLikeDeletedName(const unsigned char *data, int len) {
    if (!data || len < 8) {
        return false;
    }
    const int nameLen = static_cast<int>(data[6]);
    return nameLen > 0 && 7 + nameLen == len;
}

static void SetDeletePlayerError(unsigned char code) {
    g_waitingDeletePlayer.store(false);
    std::lock_guard<std::mutex> lock(g_deletePlayerErrorMutex);
    char buf[256];
    std::snprintf(buf, sizeof(buf), "Erreur %u : %s", static_cast<unsigned>(code), DeletePlayerErrorText(code));
    g_deletePlayerErrorMessage = buf;
}

static void ResetInventoryStateLocked();

static void ResetInGameClientStateAfterForcedExit() {
    g_pendingEnterWorld.store(false);
    g_pendingPost13Pipeline.store(false);
    g_waitingFromPreInGame.store(false);
    g_fromPreInGameResult.store(-1);
    g_enterWorldSpawn = {};
    g_playerPopupPending.store(false);
    g_playerTeleportPending.store(false);
    g_pendingTeleport = {};
    g_pipelineStep.store(4);
    {
        std::lock_guard<std::mutex> lock(g_activePlayerMutex);
        g_activePlayer = {};
    }
    {
        std::lock_guard<std::mutex> lock(g_playerStatusMutex);
        g_playerStatus = {};
    }
    g_playerStatusPending.store(false);
    ResetInventoryStateLocked();
}

static void ResetInventoryStateLocked() {
    std::lock_guard<std::mutex> lock(g_inventoryMutex);
    g_backpack = {};
    g_skillBook = {};
    g_spellBook = {};
    g_bankChest = {};
    g_inventoryPending.store(false);
}

static void SendExitGameLocked() {
    if (!g_comm) {
        return;
    }
    const auto pkt = BuildExitGamePacket();
    g_comm->SendPacket(g_serverAddr, const_cast<LPBYTE>(pkt.data()), static_cast<int>(pkt.size()), 0, 0);
    T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase,
                           "[PHASE] Envoi RQ_ExitGame (20) — libere le perso en ligne (OnlineUsers/BDD).");
}

static void SendDeletePlayerPacketsLocked(const std::string &playerName) {
    if (!g_comm || playerName.empty()) {
        return;
    }
    const auto pkt = BuildDeletePlayerPacket(playerName);
    g_comm->SendPacket(g_serverAddr, const_cast<LPBYTE>(pkt.data()), static_cast<int>(pkt.size()), 0, 0);
    const auto listPkt = BuildOpcodeOnlyPacket(static_cast<std::uint16_t>(RQ_GetPersonnalPClist));
    g_comm->SendPacket(g_serverAddr, const_cast<LPBYTE>(listPkt.data()), static_cast<int>(listPkt.size()), 0, 0);
    g_waitingDeletePlayer.store(true);
    g_deletePlayerRequestAt = std::chrono::steady_clock::now();
    T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase,
                           "[PHASE] Envoi RQ_DeletePlayer (15) pour « %s » (%zu octets TFC).",
                           playerName.c_str(), pkt.size());
    T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase,
                           "[PHASE] Envoi RQ_GetPersonnalPClist (26) apres suppression.");
}

static void HandleDeletePlayerReply(const unsigned char *data, int len) {
    g_waitingDeletePlayer.store(false);
    if (!data || len < 7 || !TfcHeaderLooksSane(data, len)) {
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Warn,
                               "[PHASE] RQ_DeletePlayer (15) : reponse trop courte (%d).", len);
        return;
    }

    if (len == 7 && data[6] == 0) {
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase, "[PHASE] RQ_DeletePlayer (15) OK (code 0).");
        return;
    }

    if (DeleteReplyLooksLikeDeletedName(data, len)) {
        const int nameLen = static_cast<int>(data[6]);
        std::string echoed(reinterpret_cast<const char *>(data + 7),
                           reinterpret_cast<const char *>(data + 7 + nameLen));
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase,
                               "[PHASE] RQ_DeletePlayer (15) OK — echo nom « %s » (%d octets).",
                               echoed.c_str(), nameLen);
        return;
    }

    if (len == 7) {
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Warn,
                               "[PHASE] RQ_DeletePlayer (15) refuse : code erreur %u.",
                               static_cast<unsigned>(data[6]));
        SetDeletePlayerError(data[6]);
        return;
    }

    T4CNetworkDebugLogKind(T4CMatrixLogKind::Warn,
                           "[PHASE] RQ_DeletePlayer (15) format inattendu (%d octets) — confiance liste 26.",
                           len);
    LogPayloadHexNet(data, len, 24);
}

static void SendGetNearItemsLocked() {
    const auto pkt = BuildOpcodeOnlyPacket(static_cast<std::uint16_t>(RQ_GetNearItems));
    SendToServerLocked(pkt);
    T4CNetworkDebugLog("[UDP] -> RQ_GetNearItems (60).");
}

static void SendGetSkillListLocked() {
    const auto pkt = BuildOpcodeOnlyPacket(static_cast<std::uint16_t>(RQ_GetSkillList));
    SendToServerLocked(pkt);
    T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase, "[PHASE] Envoi RQ_GetSkillList (39).");
}

static void SendGetSpellListLocked() {
    std::vector<unsigned char> v;
    v.assign(4, 0);
    PushBeShort(v, static_cast<std::uint16_t>(RQ_SendSpellList));
    v.push_back(1);
    SendToServerLocked(v);
    T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase, "[PHASE] Envoi RQ_SendSpellList (62).");
}

static void SendViewBackpackRequestLocked() {
    std::vector<unsigned char> v;
    v.assign(4, 0);
    PushBeShort(v, static_cast<std::uint16_t>(RQ_ViewBackpack));
    PushBeShort(v, 0);
    SendToServerLocked(v);
    T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase, "[PHASE] Envoi RQ_ViewBackpack (18) refresh.");
}

static void SendViewEquippedRequestLocked() {
    const auto pkt = BuildOpcodeOnlyPacket(static_cast<std::uint16_t>(RQ_ViewEquiped));
    SendToServerLocked(pkt);
    T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase, "[PHASE] Envoi RQ_ViewEquiped (19).");
}

static void SendQueryItemNameLocked(const unsigned char place, const std::int32_t objectId) {
    std::vector<unsigned char> v;
    v.assign(4, 0);
    PushBeShort(v, static_cast<std::uint16_t>(RQ_QueryItemName));
    PushBeShort(v, 0);
    v.push_back(place);
    PushBeInt32Msf(v, objectId);
    SendToServerLocked(v);
}

static void SendPostEnterGameInventoryRequestsLocked() {
    SendViewEquippedRequestLocked();
    SendGetSkillListLocked();
    SendGetSpellListLocked();
}

static void ParseMaxCharactersPerAccountInfo(const unsigned char *data, int len) {
    if (!data || len < 7 || !TfcHeaderLooksSane(data, len)) {
        return;
    }
    const int maxChars = static_cast<int>(data[6]);
    {
        std::lock_guard<std::mutex> lock(g_characterMutex);
        g_maxCharactersPerAccount = maxChars;
    }
    T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase,
                           "[PHASE] RQ_MaxCharactersPerAccountInfo (103) : max %d persos/compte.", maxChars);
}

static void ParsePersonnalPClist(const unsigned char *data, int len) {
    if (!data || len < 7 || !TfcHeaderLooksSane(data, len)) {
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Warn, "[PHASE] RQ_GetPersonnalPClist (26) : paquet trop court.");
        return;
    }
    const unsigned char count = data[6];
    std::vector<T4CCharacterSlot> slots;
    int pos = 7;
    for (unsigned ci = 0; ci < count; ++ci) {
        if (pos >= len) {
            break;
        }
        const unsigned char nameLen = data[pos++];
        if (pos + nameLen + 4 > len) {
            T4CNetworkDebugLogKind(T4CMatrixLogKind::Warn,
                                   "[PHASE] Liste persos tronquee a l'entree %u.", ci);
            break;
        }
        T4CCharacterSlot slot;
        slot.name.assign(reinterpret_cast<const char *>(data + pos), nameLen);
        pos += nameLen;
        slot.race = ReadBeUint16(data, pos, len);
        pos += 2;
        slot.level = ReadBeUint16(data, pos, len);
        pos += 2;
        slots.push_back(std::move(slot));
    }
    {
        std::lock_guard<std::mutex> lock(g_characterMutex);
        g_characterList = std::move(slots);
    }
    T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase,
                           "[PHASE] RQ_GetPersonnalPClist (26) : %u perso(s) parse(s).", count);
    {
        std::lock_guard<std::mutex> lock(g_characterMutex);
        for (const auto &s : g_characterList) {
            T4CNetworkDebugLog("[PHASE]   - %s (race %u, niv %u)", s.name.c_str(),
                               static_cast<unsigned>(s.race), static_cast<unsigned>(s.level));
        }
    }
}

static const char *CreatePlayerErrorText(unsigned char code) {
    switch (code) {
        case 1:
            return "Compte deja en jeu.";
        case 3:
            return "Pas de credits pour creer un personnage.";
        case 4:
            return "Trop de personnages sur le compte.";
        case 5:
            return "Ce nom est deja pris.";
        case 6:
            return "Personnage introuvable.";
        case 7:
            return "Personnage deja connecte.";
        case 8:
            return "Nom invalide (caracteres ou longueur).";
        default:
            return "Creation refusee par le serveur.";
    }
}

static void SetCreatePlayerError(unsigned char code) {
    g_waitingCreatePlayer.store(false);
    g_inCreateRerollPhase.store(false);
    g_waitingCreateReroll.store(false);
    g_pipelineStep.store(4);
    std::lock_guard<std::mutex> lock(g_createPlayerErrorMutex);
    char buf[256];
    std::snprintf(buf, sizeof(buf), "Erreur %u : %s", static_cast<unsigned>(code),
                  CreatePlayerErrorText(code));
    g_createPlayerErrorMessage = buf;
}

/** Character::packet_stats — 7 octets + 2x long HP + 2x short mana (19 octets). */
static constexpr int kRolledStatsPayloadLen = 19;

static bool ParseRolledStatsPayload(const unsigned char *data, int len, int off, T4CCharacterRolledStats *out) {
    if (!data || !out || off + kRolledStatsPayloadLen > len) {
        return false;
    }
    out->agi = data[off++];
    out->end = data[off++];
    out->intel = data[off++];
    out->luck = data[off++];
    out->str = data[off++];
    out->wil = data[off++];
    out->wis = data[off++];
    const std::int32_t maxHp = ReadBeInt32Msf(data, off, len);
    off += 4;
    const std::int32_t hp = ReadBeInt32Msf(data, off, len);
    off += 4;
    out->maxHp = maxHp < 0 ? 0u : static_cast<unsigned>(maxHp);
    out->hp = hp < 0 ? 0u : static_cast<unsigned>(hp);
    out->maxMana = ReadBeUint16(data, off, len);
    off += 2;
    out->mana = ReadBeUint16(data, off, len);
    out->valid = true;
    return true;
}

/** Character::PacketStatus — corps apres en-tete TFC 6 octets (Packet.cpp RQ_GetStatus). */
static constexpr int kGetStatusMinLen = 62;

static bool ParseGetStatusPayload(const unsigned char *data, int len, T4CPlayerStatus *out) {
    constexpr int kOff = 6;
    if (!data || !out || len < kOff + kGetStatusMinLen) {
        return false;
    }
    int o = kOff;
    const auto readU32 = [&](unsigned int &dst) -> bool {
        if (o + 4 > len) {
            return false;
        }
        const std::int32_t v = ReadBeInt32Msf(data, o, len);
        o += 4;
        dst = v < 0 ? 0u : static_cast<unsigned>(v);
        return true;
    };
    const auto readU16 = [&](std::uint16_t &dst) -> bool {
        if (o + 2 > len) {
            return false;
        }
        dst = ReadBeUint16(data, o, len);
        o += 2;
        return true;
    };
    if (!readU32(out->hp) || !readU32(out->maxHp) || !readU16(out->mana) || !readU16(out->maxMana)) {
        return false;
    }
    if (o + 8 <= len) {
        out->xp = ReadBeInt64Msf(data, o, len);
    }
    o += 8;
    o += 2; /* bAC */
    if (!readU16(out->ac)) {
        return false;
    }
    o += 14; /* base stats bStr..bLck (7 x short) */
    o += 2;  /* stat points */
    if (!readU16(out->str) || !readU16(out->end) || !readU16(out->agi)) {
        return false;
    }
    o += 2; /* wil */
    if (!readU16(out->wis) || !readU16(out->intel)) {
        return false;
    }
    o += 2; /* lck */
    if (!readU16(out->level)) {
        return false;
    }
    if (len >= o + 4) {
        o += 2; /* skill points */
        readU16(out->weight);
        readU16(out->maxWeight);
    }
    out->valid = true;
    return true;
}

static void MarkPlayerStatusUpdated(const T4CPlayerStatus &st) {
    {
        std::lock_guard<std::mutex> lock(g_playerStatusMutex);
        g_playerStatus = st;
    }
    g_playerStatusPending.store(true);
    if (st.level != 0) {
        std::lock_guard<std::mutex> lock(g_activePlayerMutex);
        if (g_activePlayer.valid) {
            g_activePlayer.level = st.level;
        }
    }
}

static void HandleGetStatus(const unsigned char *data, int len) {
    T4CPlayerStatus st{};
    if (!ParseGetStatusPayload(data, len, &st)) {
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Warn,
                               "[PHASE] RQ_GetStatus (43) tronque (%d octets) — HUD stats ignorees.", len);
        return;
    }
    MarkPlayerStatusUpdated(st);
    T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase,
                           "[PHASE] RQ_GetStatus (43) — niv %u PV %u/%u mana %u/%u AC %u XP %llu.",
                           static_cast<unsigned>(st.level), st.hp, st.maxHp, static_cast<unsigned>(st.mana),
                           static_cast<unsigned>(st.maxMana), static_cast<unsigned>(st.ac),
                           static_cast<unsigned long long>(st.xp));
}

static void HandleXPchanged(const unsigned char *data, int len) {
    if (len < 14) {
        return;
    }
    T4CPlayerStatus st{};
    {
        std::lock_guard<std::mutex> lock(g_playerStatusMutex);
        st = g_playerStatus;
    }
    st.xp = ReadBeInt64Msf(data, 6, len);
    st.valid = true;
    MarkPlayerStatusUpdated(st);
    T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase, "[PHASE] RQ_XPchanged (44) — XP %llu.",
                           static_cast<unsigned long long>(st.xp));
}

static void HandleLevelUp(const unsigned char *data, int len) {
    if (len < 30) {
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Warn,
                               "[PHASE] RQ_LevelUp (37) tronque (%d octets).", len);
        return;
    }
    T4CPlayerStatus st{};
    {
        std::lock_guard<std::mutex> lock(g_playerStatusMutex);
        st = g_playerStatus;
    }
    st.level = ReadBeUint16(data, 6, len);
    st.xpToNextLevel = ReadBeInt64Msf(data, 10, len);
    const std::int32_t hp = ReadBeInt32Msf(data, 18, len);
    const std::int32_t maxHp = ReadBeInt32Msf(data, 22, len);
    st.hp = hp < 0 ? 0u : static_cast<unsigned>(hp);
    st.maxHp = maxHp < 0 ? 0u : static_cast<unsigned>(maxHp);
    st.mana = ReadBeUint16(data, 26, len);
    st.maxMana = ReadBeUint16(data, 28, len);
    st.valid = true;
    MarkPlayerStatusUpdated(st);
    T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase,
                           "[PHASE] RQ_LevelUp (37) — niv %u PV %u/%u mana %u/%u XP next %llu.",
                           static_cast<unsigned>(st.level), st.hp, st.maxHp, static_cast<unsigned>(st.mana),
                           static_cast<unsigned>(st.maxMana), static_cast<unsigned long long>(st.xpToNextLevel));
}

static void HandleHPchanged(const unsigned char *data, int len) {
    if (len < 10) {
        return;
    }
    const std::int32_t hp = ReadBeInt32Msf(data, 6, len);
    T4CPlayerStatus st{};
    {
        std::lock_guard<std::mutex> lock(g_playerStatusMutex);
        st = g_playerStatus;
    }
    st.hp = hp < 0 ? 0u : static_cast<unsigned>(hp);
    if (len >= 14) {
        const std::int32_t maxHp = ReadBeInt32Msf(data, 10, len);
        st.maxHp = maxHp < 0 ? 0u : static_cast<unsigned>(maxHp);
    }
    st.valid = true;
    MarkPlayerStatusUpdated(st);
}

static void HandleManaChanged(const unsigned char *data, int len) {
    if (len < 8) {
        return;
    }
    T4CPlayerStatus st{};
    {
        std::lock_guard<std::mutex> lock(g_playerStatusMutex);
        st = g_playerStatus;
    }
    st.mana = ReadBeUint16(data, 6, len);
    st.valid = true;
    MarkPlayerStatusUpdated(st);
}

static void StoreRolledStatsFromPacket(const unsigned char *data, int len, int statsOffset) {
    T4CCharacterRolledStats stats{};
    if (!ParseRolledStatsPayload(data, len, statsOffset, &stats)) {
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Warn,
                               "[PHASE] Stats reroll tronquees (%d octets, offset %d).", len, statsOffset);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(g_rolledStatsMutex);
        g_rolledStats = stats;
    }
    g_rolledStatsPending.store(true);
    T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase,
                           "[PHASE] Stats : FOR %u END %u AGI %u SAG %u INT %u PV %u/%u.",
                           static_cast<unsigned>(stats.str), static_cast<unsigned>(stats.end),
                           static_cast<unsigned>(stats.agi), static_cast<unsigned>(stats.wis),
                           static_cast<unsigned>(stats.intel), stats.hp, stats.maxHp);
}

static void HandleCreatePlayerReply(const unsigned char *data, int len) {
    g_waitingCreatePlayer.store(false);
    if (!data || len < 7 || !TfcHeaderLooksSane(data, len)) {
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Warn,
                               "[PHASE] RQ_CreatePlayer (25) : reponse trop courte (%d).", len);
        SetCreatePlayerError(255);
        return;
    }
    const unsigned char err = data[6];
    if (err != 0) {
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Warn,
                               "[PHASE] RQ_CreatePlayer (25) refuse : code erreur %u.",
                               static_cast<unsigned>(err));
        SetCreatePlayerError(err);
        return;
    }
    StoreRolledStatsFromPacket(data, len, 7);
    g_inCreateRerollPhase.store(true);
    T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase,
                           "[PHASE] RQ_CreatePlayer (25) OK — ecran reroll (opcode 31 / Entree / Esc).");
}

static void HandleRerollReply(const unsigned char *data, int len) {
    g_waitingCreateReroll.store(false);
    if (!data || len < 6 || !TfcHeaderLooksSane(data, len)) {
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Warn,
                               "[PHASE] RQ_Reroll (31) : reponse trop courte (%d).", len);
        return;
    }
    StoreRolledStatsFromPacket(data, len, 6);
    T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase, "[PHASE] RQ_Reroll (31) OK — nouvelles stats.");
}

static const char *PutPlayerInGameErrorText(unsigned char code) {
    switch (code) {
        case 1:
            return "Position monde invalide.";
        case 2:
            return "Ce personnage n'appartient pas au compte.";
        case 3:
            return "Personnage deja connecte ailleurs.";
        case 4:
            return "Trop de personnages sur le compte.";
        case 5:
            return "Creation du personnage echouee.";
        case 6:
            return "Chargement BDD echoue — executer tools/sql/seed_test_player_mariadb.sql sur t4c_server.";
        case 7:
            return "Serveur occupe (chargement deja en cours) — attendez ou reconnectez.";
        case 8:
            return "Nom de personnage invalide.";
        case 9:
            return "Donnees personnage corrompues (wlX/wlY/wlWorld ?).";
        case 10:
            return "Acces au personnage refuse.";
        default:
            return "Entree en jeu refusee par le serveur.";
    }
}

static void SetPutPlayerInGameError(unsigned char code) {
    g_waitingPutPlayerInGame.store(false);
    g_pipelineStep.store(4);
    std::lock_guard<std::mutex> lock(g_putPlayerErrorMutex);
    char buf[256];
    std::snprintf(buf, sizeof(buf), "Erreur %u : %s", static_cast<unsigned>(code), PutPlayerInGameErrorText(code));
    g_putPlayerErrorMessage = buf;
}

static void HandlePutPlayerInGameReply(const unsigned char *data, int len) {
    g_waitingPutPlayerInGame.store(false);

    if (!data || len < 7 || !TfcHeaderLooksSane(data, len)) {
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Warn,
                               "[PHASE] RQ_PutPlayerInGame (13) : reponse trop courte (%d).", len);
        return;
    }
    const unsigned char err = data[6];
    if (err != 0) {
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Warn,
                               "[PHASE] RQ_PutPlayerInGame (13) refuse : code erreur %u.", static_cast<unsigned>(err));
        SetPutPlayerInGameError(err);
        return;
    }
    if (len < 17) {
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Warn,
                               "[PHASE] RQ_PutPlayerInGame (13) OK partiel mais corps trop court (%d).", len);
        return;
    }
    const std::int16_t x = static_cast<std::int16_t>(ReadBeUint16(data, 11, len));
    const std::int16_t y = static_cast<std::int16_t>(ReadBeUint16(data, 13, len));
    const std::int16_t world = static_cast<std::int16_t>(ReadBeUint16(data, 15, len));

    T4CEnterWorldSpawn spawn;
    spawn.x = static_cast<unsigned int>(x < 0 ? 0 : x);
    spawn.y = static_cast<unsigned int>(y < 0 ? 0 : y);
    spawn.world = static_cast<unsigned short>(world < 0 ? 0 : world);
    spawn.valid = true;
    g_enterWorldSpawn = spawn;

    {
        std::lock_guard<std::mutex> lock(g_activePlayerMutex);
        if (g_activePlayer.valid) {
            g_activePlayer.serverX = spawn.x;
            g_activePlayer.serverY = spawn.y;
        }
    }

    T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase,
                           "[PHASE] RQ_PutPlayerInGame (13) OK — position %u,%u monde %u.",
                           spawn.x, spawn.y, static_cast<unsigned>(spawn.world));

    if (g_pipelineStep.load() >= 3) {
        g_pendingPost13Pipeline.store(true);
        g_pipelineStep.store(6);
        g_pendingEnterWorld.store(true);
    }
}

static std::vector<unsigned char> BuildRegisterAccountPacket(const std::string &login, const std::string &password) {
    /* Même disposition que TFCSocket.cpp (menu connexion) + TFCMessagesHandler::RQFUNC_RegisterAccount :
     * 4 octets d’en-tête TFCPacket (KEY+CHECKSUM côté serveur, voir TFCPacket.h),
     * opcode RQ_RegisterAccount en **big-endian** 16 bits,
     * 1 octet longueur compte puis octets du login **sans** NUL,
     * 1 octet longueur mot de passe puis octets du mot de passe **sans** NUL,
     * hi_version / lo_version en **big-endian** 16 bits chacun (le serveur lit deux short). */
    std::vector<unsigned char> v;
    v.assign(4, 0);
    PushBeShort(v, static_cast<std::uint16_t>(RQ_RegisterAccount));

    std::string acc = login;
    std::string pwd = password;
    if (acc.size() > 255) {
        acc.resize(255);
    }
    if (pwd.size() > 255) {
        pwd.resize(255);
    }

    v.push_back(static_cast<unsigned char>(acc.size()));
    AppendTfcpayload(v, acc);
    v.push_back(static_cast<unsigned char>(pwd.size()));
    AppendTfcpayload(v, pwd);

    /* hi/lo version register (short BE) : meme sens que Player.Version / SERVER_CONNECTION_HI_VERSION (14). */
    constexpr std::uint16_t kClientVersionHi = 14;
    constexpr std::uint16_t kClientVersionLo = 0;
    PushBeShort(v, kClientVersionHi);
    PushBeShort(v, kClientVersionLo);
    return v;
}

static bool ServerMessageLooksLikeAlreadyLogged(const unsigned char *data, int len) {
    if (!data || len < 9) {
        return false;
    }
    const int msgLen = static_cast<int>(ReadBeUint16(data, 7, len));
    const int textStart = 9;
    if (msgLen <= 0 || textStart + msgLen > len) {
        return false;
    }
    std::string msg(reinterpret_cast<const char *>(data + textStart),
                    reinterpret_cast<const char *>(data + textStart + msgLen));
    for (char &c : msg) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return msg.find("already logged") != std::string::npos;
}

static void HandleAlreadyLoggedOnRefusal() {
    g_accountBlockedAlreadyLogged.store(true);
    if (RecentlyCompletedWorldLogout()) {
        T4CNetworkDebugLogKind(
            T4CMatrixLogKind::Warn,
            "[AUTH] « Already logged » apres sortie du monde : le serveur garde l'ancienne session UDP. "
            "Ne recliquez pas Connect — attendez ~%d s (cooldown).",
            kPostLogoutReconnectCooldownSec);
        SetReconnectCooldown(kPostLogoutReconnectCooldownSec);
    } else {
        T4CNetworkDebugLogKind(
            T4CMatrixLogKind::Warn,
            "[AUTH] Compte deja connecte cote serveur — pas de re-register (nouveau port UDP inutile). "
            "Attendez ~%d s ou redemarrez le serveur.",
            kPostAlreadyLoggedCooldownSec);
        SetReconnectCooldown(kPostAlreadyLoggedCooldownSec);
    }
    DestroyLoginSessionFast("already-logged");
}

static void TryRetryRegisterAfterAlreadyLogged(const unsigned char *registerReply, int len) {
    if (g_pipelineStep.load() != 1 || g_lastLogin.empty()) {
        return;
    }
    if (!ServerMessageLooksLikeAlreadyLogged(registerReply, len)) {
        return;
    }
    HandleAlreadyLoggedOnRefusal();
}

static std::uint16_t ReadOpcodeFromTfcpayload(const unsigned char *data, int len) {
    if (!data || len < 6) {
        return 0;
    }
    const unsigned hi = data[4];
    const unsigned lo = data[5];
    return static_cast<std::uint16_t>((hi << 8) | lo);
}

static int ClassIndexFromAppearance(std::uint16_t appearance) {
    if (appearance >= 10001 && appearance <= 10004) {
        return static_cast<int>(appearance - 10001);
    }
    if (appearance >= 15001 && appearance <= 15004) {
        return static_cast<int>(appearance - 15001);
    }
    return -1;
}

/** Aligne Character.cpp CreateCharacter : indice 0–4 avec reponse la plus forte. */
static int ClassIndexFromQuestionnaireStats(const unsigned char stats[5]) {
    if (!stats) {
        return 0;
    }
    int best = 0;
    int bestVal = stats[0];
    for (int i = 1; i < 5; ++i) {
        if (stats[i] > bestVal) {
            best = i;
            bestVal = stats[i];
        }
    }
    if (best > 3) {
        best = 0;
    }
    return best;
}

static int ClassIndexFromRaceField(std::uint16_t race) {
    if (race >= 10001 && race <= 10004) {
        return static_cast<int>(race - 10001);
    }
    if (race >= 15001 && race <= 15004) {
        return static_cast<int>(race - 15001);
    }
    return -1;
}

static bool IsFemaleAppearance(std::uint16_t appearance) {
    return appearance == 10012 || appearance == 15012 || (appearance >= 15001 && appearance <= 15004);
}

/** Apparences PC / puppet (RaceListing.h) — pas les creatures (>= 20000). */
static bool IsPlayerUnitAppearance(std::uint16_t appearance) {
    if (appearance >= 10001 && appearance <= 10004) {
        return true;
    }
    if (appearance >= 15001 && appearance <= 15004) {
        return true;
    }
    return appearance == 10011 || appearance == 10012 || appearance == 15011 || appearance == 15012;
}

/** Unite affichable (mob, PNJ, autre joueur) — pas objets sol (< 10001). */
static bool IsRemoteDrawableUnit(const std::uint16_t appearance) {
    if (IsPlayerUnitAppearance(appearance)) {
        return true;
    }
    if (appearance >= 10005 && appearance <= 10010) {
        return true;
    }
    if (appearance >= 15005 && appearance <= 15010) {
        return true;
    }
    if (appearance >= 20001 && appearance < 30000) {
        return true;
    }
    return false;
}

static bool IsLocalPlayerUnitId(const std::int32_t unitId) {
    std::lock_guard<std::mutex> lock(g_activePlayerMutex);
    return g_activePlayer.valid && g_activePlayer.unitId != 0 && unitId == g_activePlayer.unitId;
}

/** Ne pas instancier le joueur local comme unite distante (opcode 16 avant unitId connu). */
static bool ShouldSkipAsRemoteUnit(const unsigned int x, const unsigned int y, const std::uint16_t appearance,
                                   const std::int32_t unitId) {
    std::lock_guard<std::mutex> lock(g_activePlayerMutex);
    if (!g_activePlayer.valid) {
        return false;
    }
    if (g_activePlayer.unitId != 0 && unitId == g_activePlayer.unitId) {
        return true;
    }
    if (IsPlayerUnitAppearance(appearance) && x == g_activePlayer.serverX && y == g_activePlayer.serverY) {
        return true;
    }
    return false;
}

static void PushRemoteUnitEvent(T4CRemoteUnitEvent ev) {
    std::lock_guard<std::mutex> lock(g_remoteEventsMutex);
    g_remoteEvents.push_back(ev);
}

static bool IsGroundObjectAppearance(const std::uint16_t appearance) {
    return appearance > 0 && appearance < 10001;
}

static void UpsertGroundObjectMarker(const std::int32_t unitId, const std::uint16_t appearance,
                                     const unsigned int x, const unsigned int y) {
    if (unitId == 0 || !IsGroundObjectAppearance(appearance)) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_groundObjectsMutex);
    g_groundObjectsByUnitId[unitId] = T4CGroundObjectMarker{unitId, appearance, x, y};
}

static void RemoveGroundObjectMarker(const std::int32_t unitId) {
    if (unitId == 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_groundObjectsMutex);
    g_groundObjectsByUnitId.erase(unitId);
}

static bool ParsePacketUnitInformation(const unsigned char *data, int off, int len, std::uint16_t *appearance,
                                       std::int32_t *unitId, char *radiance, char *status, char *hpPercent) {
    if (!data || !appearance || !unitId || off + 11 > len) {
        return false;
    }
    *appearance = ReadBeUint16(data, off, len);
    *unitId = ReadBeInt32Msf(data, off + 2, len);
    if (radiance) {
        *radiance = static_cast<char>(data[off + 6]);
    }
    if (status) {
        *status = static_cast<char>(data[off + 7]);
    }
    if (hpPercent) {
        *hpPercent = static_cast<char>(data[off + 8]);
    }
    return true;
}

const char *T4CSpriteNameFromAppearance(const std::uint16_t appearance) {
    if (IsPlayerUnitAppearance(appearance)) {
        T4CActivePlayer tmp{};
        tmp.appearance = appearance;
        tmp.female = IsFemaleAppearance(appearance);
        return T4CPlayerSpriteNpcName(tmp);
    }
    switch (appearance) {
        case 10005:
        case 15005:
            return "PaysanModel1";
        case 10006:
        case 15006:
            return "GuardModel1";
        case 10007:
        case 15007:
            return "Mage";
        case 10008:
        case 15008:
            return "PaysanneModel1";
        case 10009:
        case 15009:
            return "Cleric";
        case 10010:
        case 15010:
            return "BlackWarrior";
        case 20001:
        case 25001:
            return "Goblin";
        case 20002:
        case 25002:
            return "Bat";
        case 20003:
        case 25003:
            return "Rat";
        case 20004:
        case 25004:
            return "Kobold";
        case 20005:
        case 25005:
            return "Zombie";
        case 20006:
        case 25006:
            return "BlackWarrior";
        case 20007:
        case 25007:
            return "Spider";
        case 20008:
        case 25008:
            return "Orc";
        case 20009:
        case 25009:
            return "Zombie";
        case 20012:
        case 25012:
            return "Skeleton";
        case 20022:
        case 25022:
            return "Wolf";
        case 20028:
        case 25028:
            return "Dragon";
        case 20039:
        case 25039:
            return "Mage";
        case 20042:
        case 25042:
            return "Thief";
        case 20043:
        case 25043:
            return "Warrio";
        case 20044:
        case 25044:
            return "Cleric";
        default:
            break;
    }
    return "BlackWarrior";
}

static void QueueRemoteUnitSpawn(const unsigned int x, const unsigned int y, const std::uint16_t appearance,
                                 const std::int32_t unitId, const char hpPercent) {
    if (!IsRemoteDrawableUnit(appearance) || ShouldSkipAsRemoteUnit(x, y, appearance, unitId)) {
        return;
    }
    T4CRemoteUnitEvent ev{};
    ev.kind = T4CRemoteUnitEventKind::Spawn;
    ev.unitId = unitId;
    ev.appearance = appearance;
    ev.x = x;
    ev.y = y;
    ev.hpPercent = hpPercent;
    PushRemoteUnitEvent(ev);
    T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase,
                           "[PHASE] Unite distante spawn id=%d app=%u @ %u,%u sprite=%s.",
                           static_cast<int>(unitId), static_cast<unsigned>(appearance), x, y,
                           T4CSpriteNameFromAppearance(appearance));
}

static void QueueRemoteUnitMove(const unsigned int x, const unsigned int y, const std::uint16_t appearance,
                                const std::int32_t unitId) {
    if (!IsRemoteDrawableUnit(appearance) || ShouldSkipAsRemoteUnit(x, y, appearance, unitId)) {
        return;
    }
    T4CRemoteUnitEvent ev{};
    ev.kind = T4CRemoteUnitEventKind::Move;
    ev.unitId = unitId;
    ev.appearance = appearance;
    ev.x = x;
    ev.y = y;
    PushRemoteUnitEvent(ev);
}

static void QueueRemoteUnitUpdate(const std::uint16_t appearance, const std::int32_t unitId, const char hpPercent) {
    if (!IsRemoteDrawableUnit(appearance) || IsLocalPlayerUnitId(unitId)) {
        return;
    }
    T4CRemoteUnitEvent ev{};
    ev.kind = T4CRemoteUnitEventKind::Update;
    ev.unitId = unitId;
    ev.appearance = appearance;
    ev.hpPercent = hpPercent;
    PushRemoteUnitEvent(ev);
}

static void QueueRemoteUnitRemove(const std::int32_t unitId) {
    if (unitId == 0 || IsLocalPlayerUnitId(unitId)) {
        return;
    }
    T4CRemoteUnitEvent ev{};
    ev.kind = T4CRemoteUnitEventKind::Remove;
    ev.unitId = unitId;
    PushRemoteUnitEvent(ev);
    T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase, "[PHASE] Unite distante remove id=%d.", static_cast<int>(unitId));
}

static void HandleObjectAppearedList(const unsigned char *data, int len) {
    if (!data || len < 8 || !TfcHeaderLooksSane(data, len)) {
        return;
    }
    const int count = static_cast<int>(ReadBeUint16(data, 6, len));
    int off = 8;
    for (int i = 0; i < count; ++i) {
        if (off + 13 > len) {
            break;
        }
        const std::int16_t sx = static_cast<std::int16_t>(ReadBeUint16(data, off, len));
        const std::int16_t sy = static_cast<std::int16_t>(ReadBeUint16(data, off + 2, len));
        std::uint16_t appearance = 0;
        std::int32_t unitId = 0;
        char hpPercent = 0;
        if (!ParsePacketUnitInformation(data, off + 4, len, &appearance, &unitId, nullptr, nullptr, &hpPercent)) {
            break;
        }
        if (sx >= 0 && sy >= 0) {
            UpsertGroundObjectMarker(unitId, appearance, static_cast<unsigned int>(sx), static_cast<unsigned int>(sy));
            QueueRemoteUnitSpawn(static_cast<unsigned int>(sx), static_cast<unsigned int>(sy), appearance, unitId,
                                 hpPercent);
        }
        off += 13;
    }
    T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase,
                           "[PHASE] __EVENT_OBJECT_APPEARED_LIST (16) : %d unite(s) peripheriques.",
                           count);
}

static void HandleUnitUpdate(const unsigned char *data, int len) {
    if (!data || len < 17 || !TfcHeaderLooksSane(data, len)) {
        return;
    }
    std::uint16_t appearance = 0;
    std::int32_t unitId = 0;
    char hpPercent = 0;
    if (!ParsePacketUnitInformation(data, 6, len, &appearance, &unitId, nullptr, nullptr, &hpPercent)) {
        return;
    }
    QueueRemoteUnitUpdate(appearance, unitId, hpPercent);
    T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase,
                           "[PHASE] RQ_UnitUpdate (69) id=%d app=%u hp=%d%% sprite=%s.",
                           static_cast<int>(unitId), static_cast<unsigned>(appearance),
                           static_cast<int>(static_cast<unsigned char>(hpPercent)),
                           T4CSpriteNameFromAppearance(appearance));
}

static void HandleMissingUnit(const unsigned char *data, int len) {
    if (!data || len < 10 || !TfcHeaderLooksSane(data, len)) {
        return;
    }
    const std::int32_t unitId = ReadBeInt32Msf(data, 6, len);
    RemoveGroundObjectMarker(unitId);
    QueueRemoteUnitRemove(unitId);
}

static bool ApplyRemoteUnitMove(const unsigned char *data, int len) {
    if (!data || len < 19 || !TfcHeaderLooksSane(data, len)) {
        return false;
    }
    const std::int16_t sx = static_cast<std::int16_t>(ReadBeUint16(data, 6, len));
    const std::int16_t sy = static_cast<std::int16_t>(ReadBeUint16(data, 8, len));
    if (sx < 0 || sy < 0) {
        return false;
    }
    std::uint16_t appearance = 0;
    std::int32_t unitId = 0;
    if (!ParsePacketUnitInformation(data, 10, len, &appearance, &unitId, nullptr, nullptr, nullptr)) {
        return false;
    }
    UpsertGroundObjectMarker(unitId, appearance, static_cast<unsigned int>(sx), static_cast<unsigned int>(sy));
    if (IsLocalPlayerUnitId(unitId)) {
        return false;
    }
    if (!IsRemoteDrawableUnit(appearance)) {
        return false;
    }
    QueueRemoteUnitMove(static_cast<unsigned int>(sx), static_cast<unsigned int>(sy), appearance, unitId);
    T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase,
                           "[PHASE] __EVENT_OBJECT_MOVED (1) — distant id=%d app=%u @ %u,%u sprite=%s.",
                           static_cast<int>(unitId), static_cast<unsigned>(appearance),
                           static_cast<unsigned>(sx), static_cast<unsigned>(sy),
                           T4CSpriteNameFromAppearance(appearance));
    return true;
}

const char *T4CPlayerSpriteNpcName(const T4CActivePlayer &player) {
    /* NPCList.txt (TnC) : « Warrio » = guerrier humain — nom fichier VSF, pas « Warrior » Windows. */
    static const char *const kClassSprites[] = {"Warrio", "Wizard", "Cleric", "Thief"};
    int cls = ClassIndexFromAppearance(player.appearance);
    if (cls < 0) {
        if (player.classIndex <= 3) {
            cls = static_cast<int>(player.classIndex);
        } else {
            cls = ClassIndexFromRaceField(player.race);
        }
    }
    if (cls < 0 || cls > 3) {
        cls = 0;
    }
    return kClassSprites[cls];
}

/** Serveur : __EVENT_OBJECT_MOVED (opcode 1) — X,Y + PacketUnitInformation (appearance, unitId, …). */
static bool ApplyServerUnitPosition(const unsigned char *data, int len) {
    if (!data || len < 19 || !TfcHeaderLooksSane(data, len)) {
        return false;
    }
    const std::int16_t sx = static_cast<std::int16_t>(ReadBeUint16(data, 6, len));
    const std::int16_t sy = static_cast<std::int16_t>(ReadBeUint16(data, 8, len));
    if (sx < 0 || sy < 0) {
        return false;
    }
    const std::uint16_t appearance = ReadBeUint16(data, 10, len);
    if (!IsPlayerUnitAppearance(appearance)) {
        return false;
    }
    const std::int32_t unitId = ReadBeInt32Msf(data, 12, len);
    const unsigned int ux = static_cast<unsigned int>(sx);
    const unsigned int uy = static_cast<unsigned int>(sy);

    std::lock_guard<std::mutex> lock(g_activePlayerMutex);
    if (!g_activePlayer.valid) {
        return false;
    }
    const int mdx = static_cast<int>(ux) - static_cast<int>(g_activePlayer.serverX);
    const int mdy = static_cast<int>(uy) - static_cast<int>(g_activePlayer.serverY);
    const int amdx = mdx < 0 ? -mdx : mdx;
    const int amdy = mdy < 0 ? -mdy : mdy;
    const bool adjacentMove = amdx <= 1 && amdy <= 1 && (mdx != 0 || mdy != 0);

    if (g_activePlayer.unitId == 0) {
        if (!adjacentMove) {
            return false;
        }
        g_activePlayer.unitId = unitId;
        if (appearance != 0) {
            g_activePlayer.appearance = appearance;
            g_activePlayer.female = IsFemaleAppearance(appearance);
        }
    } else if (unitId != g_activePlayer.unitId) {
        if (!adjacentMove ||
            (g_activePlayer.appearance != 0 && appearance != g_activePlayer.appearance)) {
            return false;
        }
        g_activePlayer.unitId = unitId;
    }
    if (ux == g_activePlayer.serverX && uy == g_activePlayer.serverY) {
        return false;
    }
    g_activePlayer.serverX = ux;
    g_activePlayer.serverY = uy;
    g_playerPopupPending.store(true);
    return true;
}

static void HandleTeleportPlayer(const unsigned char *data, int len) {
    if (!data || len < 12 || !TfcHeaderLooksSane(data, len)) {
        return;
    }
    const std::int16_t x = static_cast<std::int16_t>(ReadBeUint16(data, 6, len));
    const std::int16_t y = static_cast<std::int16_t>(ReadBeUint16(data, 8, len));
    const std::int16_t world = static_cast<std::int16_t>(ReadBeUint16(data, 10, len));
    if (x < 0 || y < 0 || world < 0) {
        return;
    }

    g_pendingTeleport.x = static_cast<unsigned int>(x);
    g_pendingTeleport.y = static_cast<unsigned int>(y);
    g_pendingTeleport.world = static_cast<unsigned short>(world);

    {
        std::lock_guard<std::mutex> lock(g_activePlayerMutex);
        g_activePlayer.serverX = g_pendingTeleport.x;
        g_activePlayer.serverY = g_pendingTeleport.y;
        g_activePlayer.valid = true;
    }

    g_playerPopupPending.store(false);
    g_playerTeleportPending.store(true);
    T4CLoginSessionClearRemoteUnits();

    T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase,
                           "[PHASE] RQ_TeleportPlayer (57) : @ %u,%u monde %u.",
                           g_pendingTeleport.x, g_pendingTeleport.y,
                           static_cast<unsigned>(g_pendingTeleport.world));

    SendGetNearItemsLocked();
    SendFromPreInGameToInGameLocked();
}

static void HandlePacketPopup(const unsigned char *data, int len) {
    if (!data || len < 19 || !TfcHeaderLooksSane(data, len)) {
        return;
    }
    const std::int16_t sx = static_cast<std::int16_t>(ReadBeUint16(data, 6, len));
    const std::int16_t sy = static_cast<std::int16_t>(ReadBeUint16(data, 8, len));
    std::uint16_t appearance = 0;
    std::int32_t unitId = 0;
    char hpPercent = 0;
    if (!ParsePacketUnitInformation(data, 10, len, &appearance, &unitId, nullptr, nullptr, &hpPercent)) {
        return;
    }
    if (sx >= 0 && sy >= 0) {
        UpsertGroundObjectMarker(unitId, appearance, static_cast<unsigned int>(sx), static_cast<unsigned int>(sy));
    }

    if (IsPlayerUnitAppearance(appearance)) {
        bool spawnOtherPlayer = false;
        {
            std::lock_guard<std::mutex> lock(g_activePlayerMutex);
            if (g_activePlayer.unitId != 0 && unitId != g_activePlayer.unitId) {
                spawnOtherPlayer = true;
            } else {
                if (sx >= 0) {
                    g_activePlayer.serverX = static_cast<unsigned int>(sx);
                }
                if (sy >= 0) {
                    g_activePlayer.serverY = static_cast<unsigned int>(sy);
                }
                if (appearance != 0) {
                    g_activePlayer.appearance = appearance;
                    g_activePlayer.female = IsFemaleAppearance(appearance);
                }
                g_activePlayer.unitId = unitId;
                g_activePlayer.valid = true;
                g_playerPopupPending.store(true);

                T4CNetworkDebugLogKind(
                    T4CMatrixLogKind::Phase,
                    "[PHASE] PacketPopup (10004) : pos %u,%u appearance=%u (0x%04X) id=%d sprite=%s%s.",
                    g_activePlayer.serverX, g_activePlayer.serverY, static_cast<unsigned>(appearance),
                    static_cast<unsigned>(appearance), static_cast<int>(unitId), T4CPlayerSpriteNpcName(g_activePlayer),
                    g_activePlayer.female ? " (F)" : "");
                return;
            }
        }
        if (spawnOtherPlayer && sx >= 0 && sy >= 0) {
            QueueRemoteUnitSpawn(static_cast<unsigned int>(sx), static_cast<unsigned int>(sy), appearance, unitId,
                                 hpPercent);
        }
        return;
    }

    if (sx >= 0 && sy >= 0) {
        QueueRemoteUnitSpawn(static_cast<unsigned int>(sx), static_cast<unsigned int>(sy), appearance, unitId,
                             hpPercent);
    }
}

/** Aligné sur TFCSocket.h (codes client 1.68) + envoi serveur `sending << (char)N` après l’opcode. */
static const char *RegisterAccountResultLabel(unsigned char code) {
    switch (code) {
        case 0:
            return "0_OK (compte / session acceptee cote protocole — voir texte serveur)";
        case 1:
            return "1_REFUS (mauvais MDP, compte deja connecte ailleurs, ou autre erreur auth — lire le message texte)";
        case 2:
            return "2_REFUS (NoCredits cote constantes client OU autre refus serveur ex. code 2 envoye)";
        case 3:
            return "3_DoNotExists (compte inconnu)";
        case 4:
            return "4_AlreadyRegistred";
        case 6:
            return "6_WrongVersion";
        default:
            return "code inconnu (hors enum client 0-6)";
    }
}

/** Après les 4 octets TFC + opcode BE 16 bits : le serveur envoie un octet (char) code ; si code 1, `short` BE longueur puis texte (voir main.cpp Windows). */
static void LogRegisterAccountReply(const unsigned char *data, int len) {
    if (!data || len < 7) {
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Warn,
                               "[AUTH] RQ_RegisterAccount : payload trop court (%d) pour lire le code resultat.",
                               len);
        return;
    }

    if (!TfcHeaderLooksSane(data, len)) {
        T4CNetworkDebugLogKind(
            T4CMatrixLogKind::Warn,
            "[AUTH] En-tete TFC (4 premiers octets) != 00 00 00 00 — paquet probablement **mal decode** apres "
            "DecryptS / DecryptS2 (TYPE_MASK) ou format inattendu. Ne pas se fier a l’opcode lu aux offsets 4-5.");
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Net,
                               "[AUTH] Brut offsets 0..15 : %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                               len > 0 ? data[0] : 0, len > 1 ? data[1] : 0, len > 2 ? data[2] : 0, len > 3 ? data[3] : 0,
                               len > 4 ? data[4] : 0, len > 5 ? data[5] : 0, len > 6 ? data[6] : 0, len > 7 ? data[7] : 0,
                               len > 8 ? data[8] : 0, len > 9 ? data[9] : 0, len > 10 ? data[10] : 0, len > 11 ? data[11] : 0,
                               len > 12 ? data[12] : 0, len > 13 ? data[13] : 0, len > 14 ? data[14] : 0,
                               len > 15 ? data[15] : 0);
        return;
    }

    const unsigned char status = data[6];
    T4CNetworkDebugLogKind(
        T4CMatrixLogKind::Phase,
        "[PHASE] 2/5 — Reponse RQ_RegisterAccount (14), %d octets applicatifs (TFC + corps).",
        len);

    T4CNetworkDebugLogKind(T4CMatrixLogKind::Auth,
                           "[AUTH] Octet resultat @offset 6 (apres opcode BE) = 0x%02X (%u) — %s",
                           status, static_cast<unsigned>(status), RegisterAccountResultLabel(status));

    T4CNetworkDebugLogKind(T4CMatrixLogKind::Net,
                           "[AUTH] Brut TFC offsets 4..11 : %02X %02X %02X %02X %02X %02X %02X %02X",
                           data[4], data[5], data[6], data[7], data[8], data[9], data[10], data[11]);

    if (status == 0) {
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Auth,
                               "[AUTH] Verdict : **ACCEPTE** (code 0). Etape client suivante (main.cpp 1.68) : "
                               "envoyer **RQ_AuthenticateServerVersion (99)** avec la version (long), puis le serveur "
                               "repond **99** + long 1=OK / 0=refus version.");
        if (g_pipelineStep.load() == 1) {
            const auto authPkt = BuildAuthenticateServerVersionPacket(kTfcClientVersionLong);
            SendToServerLocked(authPkt);
            g_pipelineStep.store(2);
            T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase,
                                   "[PHASE] 3/5 — Envoi RQ_AuthenticateServerVersion (99), clientVersion(long)=%d.",
                                   static_cast<int>(kTfcClientVersionLong));
            T4CNetworkDebugLog("[UDP] -> file d'envoi : paquet AuthenticateServerVersion (%zu octets TFC).",
                               authPkt.size());
        }
    } else if (status == 1) {
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Warn,
                               "[AUTH] Verdict : **REFUSE** (code 1). Causes frequentes : mauvais mot de passe **ou** "
                               "autre refus (ex. compte deja connecte). Format : `short` BE longueur puis texte (main.cpp).");
        if (len >= 9) {
            const int msgLen = static_cast<int>(ReadBeUint16(data, 7, len));
            const int textStart = 9;
            if (msgLen > 0 && textStart + msgLen <= len) {
                const int cap = std::min(msgLen, 200);
                char buf[256];
                int p = 0;
                p += std::snprintf(buf + p, sizeof(buf) - static_cast<std::size_t>(p),
                                   "[AUTH] Message serveur (%d octets, affiche max %d) : ", msgLen, cap);
                for (int i = 0; i < cap && p < static_cast<int>(sizeof(buf) - 4); ++i) {
                    const unsigned char c = data[textStart + i];
                    if (c >= 32 && c < 127) {
                        buf[p++] = static_cast<char>(c);
                    } else {
                        p += std::snprintf(buf + p, sizeof(buf) - static_cast<std::size_t>(p), "\\x%02X", c);
                    }
                }
                buf[p] = '\0';
                T4CNetworkDebugLogKind(T4CMatrixLogKind::Auth, "%s", buf);
                TryRetryRegisterAfterAlreadyLogged(data, len);
            } else {
                T4CNetworkDebugLogKind(T4CMatrixLogKind::Warn,
                                       "[AUTH] Longueur message incoherente : msgLen=%d, len paquet=%d (attendu >= %d).",
                                       msgLen, len, textStart + msgLen);
            }
        } else {
            TryRetryRegisterAfterAlreadyLogged(data, len);
        }
    } else if (status == 2) {
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Warn,
                               "[AUTH] Code 2 : le client Windows renvoyait parfois un **nouveau** RQ_RegisterAccount "
                               "(retry) — non reproduit ici automatiquement.");
    } else {
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Warn,
                               "[AUTH] Verdict : **REFUSE ou etat special** (code %u). Suite du paquet a inspecter manuellement.",
                               static_cast<unsigned>(status));
        const int textStart = 7;
        if (len > textStart) {
            const int maxShow = std::min(len - textStart, 80);
            char buf[120];
            int p = 0;
            p += std::snprintf(buf + p, sizeof(buf) - static_cast<std::size_t>(p), "[AUTH] Suite (brut ASCII-ish) : ");
            for (int i = 0; i < maxShow && p < static_cast<int>(sizeof(buf) - 4); ++i) {
                const unsigned char c = data[textStart + i];
                if (c >= 32 && c < 127) {
                    buf[p++] = static_cast<char>(c);
                } else {
                    p += std::snprintf(buf + p, sizeof(buf) - static_cast<std::size_t>(p), "\\x%02X", c);
                }
            }
            buf[p] = '\0';
            T4CNetworkDebugLogKind(T4CMatrixLogKind::Auth, "%s", buf);
        }
    }
}

static void HandleAuthenticateServerVersionReply(const unsigned char *data, int len) {
    if (!data || len < 10) {
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Warn,
                               "[AUTH] Reponse RQ_AuthenticateServerVersion trop courte (%d).", len);
        return;
    }
    if (!TfcHeaderLooksSane(data, len)) {
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Warn,
                               "[AUTH] Reponse 99 : en-tete TFC non nul — ignorer l’analyse long.");
        return;
    }
    const std::int32_t valid = ReadBeInt32Msf(data, 6, len);
    T4CNetworkDebugLogKind(
        T4CMatrixLogKind::Phase,
        "[PHASE] 4/5 — Reponse RQ_AuthenticateServerVersion (99) : champ long @offset 6 = %d (1=OK, 0=refus version).",
        static_cast<int>(valid));

    if (valid == 1) {
        if (g_pipelineStep.load() == 2) {
            g_pipelineStep.store(3);
            T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase,
                                   "[PHASE] 5/7 — Auth version OK (`DlgState=6`). Envoi liste persos (26)…");
            SendPostAuthRequests();
        }
    } else {
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Warn,
                               "[AUTH] Controle version **refuse** par le serveur (long=0). Verifier "
                               "kTfcClientVersionLong (= %d) vs TFCServer->dwVersion cote serveur.",
                               static_cast<int>(kTfcClientVersionLong));
    }
}

static const char *OpcodeLabel(std::uint16_t op) {
    switch (op) {
        case RQ_RegisterAccount:
            return "RQ_RegisterAccount";
        case RQ_QueryServerVersion:
            return "RQ_QueryServerVersion";
        case RQ_AuthenticateServerVersion:
            return "RQ_AuthenticateServerVersion";
        case RQ_GetPersonnalPClist:
            return "RQ_GetPersonnalPClist";
        case RQ_MessageOfDay:
            return "RQ_MessageOfDay";
        case RQ_InfoMessage:
            return "RQ_InfoMessage";
        case RQ_ServerMessage:
            return "RQ_ServerMessage";
        case RQ_QueryPatchServerInfo:
            return "RQ_QueryPatchServerInfo";
        case RQ_MaxCharactersPerAccountInfo:
            return "RQ_MaxCharactersPerAccountInfo";
        case RQ_FromPreInGameToInGame:
            return "RQ_FromPreInGameToInGame";
        case RQ_PutPlayerInGame:
            return "RQ_PutPlayerInGame";
        case RQ_CreatePlayer:
            return "RQ_CreatePlayer";
        case RQ_Reroll:
            return "RQ_Reroll";
        case RQ_DeletePlayer:
            return "RQ_DeletePlayer";
        case RQ_ViewEquiped:
            return "RQ_ViewEquiped";
        case RQ_ViewBackpack:
            return "RQ_ViewBackpack";
        case RQ_HPchanged:
            return "RQ_HPchanged";
        case RQ_ManaChanged:
            return "RQ_ManaChanged";
        case RQ_GetStatus:
            return "RQ_GetStatus";
        case RQ_GetSkillList:
            return "RQ_GetSkillList";
        case RQ_SendSpellList:
            return "RQ_SendSpellList";
        case RQ_ChestContents:
            return "RQ_ChestContents";
        case RQ_ShowChest:
            return "RQ_ShowChest";
        case RQ_HideChest:
            return "RQ_HideChest";
        case RQ_LevelUp:
            return "RQ_LevelUp";
        case RQ_XPchanged:
            return "RQ_XPchanged";
        default:
            return nullptr;
    }
}

static void LogPayloadHexNet(const unsigned char *data, int len, int maxBytes) {
    if (!data || len <= 0) {
        return;
    }
    const int n = std::min(len, maxBytes);
    char line[128];
    int pos = 0;
    pos += std::snprintf(line + pos, sizeof(line) - static_cast<std::size_t>(pos), "[UDP] payload head %d/%d: ", n, len);
    for (int i = 0; i < n && pos < static_cast<int>(sizeof(line) - 4); ++i) {
        pos += std::snprintf(line + pos, sizeof(line) - static_cast<std::size_t>(pos), "%02X ", data[i]);
    }
    T4CNetworkDebugLog("%s", line);
}

static void TryAutoEnterWorldAfterCreateList();

static void __cdecl CommReadCallback(sockaddr_in /*fromServer*/, LPBYTE lpbBuffer, int nBufferSize) {
    if (!lpbBuffer || nBufferSize <= 0) {
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Warn, "[UDP] <- (empty application payload)");
        return;
    }

    const auto *bytes = reinterpret_cast<const unsigned char *>(lpbBuffer);
    const std::uint16_t opEarly = ReadOpcodeFromTfcpayload(bytes, nBufferSize);
    const bool inWorld = g_pipelineStep.load() >= 6;
    const bool quietWorldPacket = inWorld && (opEarly == kEventObjectMoved ||
                                              opEarly == static_cast<std::uint16_t>(RQ_UnitUpdate) ||
                                              opEarly == static_cast<std::uint16_t>(RQ_MissingUnit));
    if (!quietWorldPacket) {
        LogPayloadHexNet(bytes, nBufferSize, 24);
    }

    if (nBufferSize >= 4 && !TfcHeaderLooksSane(bytes, nBufferSize)) {
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Warn,
                               "[AUTH] En-tete TFC non standard (attendu 00 00 00 00) — opcode / texte peuvent etre faux "
                               "(couche S + TYPE_MASK / DecryptS2).");
    }

    const std::uint16_t op = opEarly;
    if (!quietWorldPacket) {
        const char *name = OpcodeLabel(op);
        if (name) {
            T4CNetworkDebugLogKind(T4CMatrixLogKind::Auth,
                                   "[AUTH] <- opcode %u (0x%04X) %s, app payload %d bytes",
                                   static_cast<unsigned>(op), static_cast<unsigned>(op), name, nBufferSize);
        } else {
            T4CNetworkDebugLogKind(T4CMatrixLogKind::Auth,
                                   "[AUTH] <- opcode %u (0x%04X) (non documente client Linux), %d bytes",
                                   static_cast<unsigned>(op), static_cast<unsigned>(op), nBufferSize);
        }
    }

    if (op == kTfcStillConnected) {
        if (g_logoutInProgress.load() || g_logoutSentThisSession.load()) {
            T4CNetworkDebugLogKind(T4CMatrixLogKind::Auth,
                                   "[AUTH] Keepalive TFCStillConnected (10) ignore pendant logout "
                                   "(ne pas renvoyer RQ_Ack — sinon ExitGame refuse).");
        } else {
            T4CNetworkDebugLogKind(T4CMatrixLogKind::Auth,
                                   "[AUTH] Keepalive TFCStillConnected (10) — envoi RQ_Ack.");
            SendStillConnectedAck();
        }
    }

    /* Chaîne typique avant monde 3D : version / MOTD / compte, puis liste persos. */
    if (op == static_cast<std::uint16_t>(RQ_QueryServerVersion)) {
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Auth,
                              "[AUTH] Etape serveur : proposition de version (RQ_QueryServerVersion).");
    } else if (op == static_cast<std::uint16_t>(RQ_AuthenticateServerVersion)) {
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Auth,
                              "[AUTH] Paquet RQ_AuthenticateServerVersion (99) — resultat controle version.");
        HandleAuthenticateServerVersionReply(bytes, nBufferSize);
    } else if (op == static_cast<std::uint16_t>(RQ_MessageOfDay)) {
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Auth, "[AUTH] Message du jour (RQ_MessageOfDay).");
    } else if (op == static_cast<std::uint16_t>(RQ_InfoMessage) ||
               op == static_cast<std::uint16_t>(RQ_ServerMessage)) {
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Auth, "[AUTH] Message serveur (info/texte).");
    } else if (op == static_cast<std::uint16_t>(RQ_RegisterAccount)) {
        LogRegisterAccountReply(bytes, nBufferSize);
    } else if (op == static_cast<std::uint16_t>(RQ_MaxCharactersPerAccountInfo)) {
        ParseMaxCharactersPerAccountInfo(bytes, nBufferSize);
    } else if (op == static_cast<std::uint16_t>(RQ_GetPersonnalPClist)) {
        ParsePersonnalPClist(bytes, nBufferSize);
        g_waitingDeletePlayer.store(false);
        if (g_pipelineStep.load() >= 3) {
            g_pipelineStep.store(4);
        }
        SDL_Log("[NETWORK] Liste persos recue — ecran selection.");
        if (!g_successAlreadyShown.exchange(true)) {
            g_pendingCharacterList.store(true);
        }
        TryAutoEnterWorldAfterCreateList();
    } else if (op == static_cast<std::uint16_t>(RQ_PutPlayerInGame)) {
        HandlePutPlayerInGameReply(bytes, nBufferSize);
    } else if (op == static_cast<std::uint16_t>(RQ_CreatePlayer)) {
        HandleCreatePlayerReply(bytes, nBufferSize);
    } else if (op == static_cast<std::uint16_t>(RQ_Reroll)) {
        HandleRerollReply(bytes, nBufferSize);
    } else if (op == static_cast<std::uint16_t>(RQ_DeletePlayer)) {
        HandleDeletePlayerReply(bytes, nBufferSize);
    } else if (op == static_cast<std::uint16_t>(RQ_ViewBackpack)) {
        HandleViewBackpack(bytes, nBufferSize);
        if (g_pendingPost13Pipeline.exchange(false)) {
            SendFromPreInGameToInGameLocked();
            SendGetNearItemsLocked();
            T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase,
                                   "[PHASE] Apres 18 : envoi 46 puis 60 (aligne Windows / serveur boPreInGame).");
        }
    } else if (op == static_cast<std::uint16_t>(RQ_ViewEquiped) && g_pipelineStep.load() >= 6) {
        HandleViewEquipped(bytes, nBufferSize);
    } else if (g_waitingPutPlayerInGame.load() &&
               (op == static_cast<std::uint16_t>(RQ_ViewEquiped) ||
                op == static_cast<std::uint16_t>(RQ_HPchanged) ||
                op == static_cast<std::uint16_t>(RQ_ManaChanged))) {
        T4CNetworkDebugLogKind(
            T4CMatrixLogKind::Phase,
            "[PHASE] Paquet %u (%s) pendant chargement serveur — attendre opcode 13 (0x000D), "
            "pas confondre avec 19 (0x0013 = ViewEquiped).",
            static_cast<unsigned>(op), OpcodeLabel(op) ? OpcodeLabel(op) : "?");
    } else if (op == static_cast<std::uint16_t>(RQ_FromPreInGameToInGame)) {
        g_waitingFromPreInGame.store(false);
        int resultCode = -1;
        if (nBufferSize >= 7) {
            resultCode = static_cast<unsigned char>(bytes[6]);
            g_fromPreInGameResult.store(resultCode);
        }
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase,
                               "[PHASE] Reponse RQ_FromPreInGameToInGame (46) code=%d.",
                               resultCode);
        if (resultCode == 0 && g_pipelineStep.load() >= 6) {
            SendPostEnterGameInventoryRequestsLocked();
        }
    } else if (op == static_cast<std::uint16_t>(RQ_GetSkillList) && g_pipelineStep.load() >= 6) {
        HandleGetSkillList(bytes, nBufferSize);
    } else if (op == static_cast<std::uint16_t>(RQ_SendSpellList) && g_pipelineStep.load() >= 6) {
        HandleSendSpellList(bytes, nBufferSize);
    } else if (op == static_cast<std::uint16_t>(RQ_ChestContents) && g_pipelineStep.load() >= 6) {
        HandleChestContents(bytes, nBufferSize);
    } else if (op == static_cast<std::uint16_t>(RQ_QueryItemName) && g_pipelineStep.load() >= 6) {
        HandleQueryItemName(bytes, nBufferSize);
    } else if (op == static_cast<std::uint16_t>(RQ_ShowChest) && g_pipelineStep.load() >= 6) {
        HandleShowChest();
    } else if (op == static_cast<std::uint16_t>(RQ_HideChest) && g_pipelineStep.load() >= 6) {
        HandleHideChest();
    } else if (op == static_cast<std::uint16_t>(RQ_GetStatus) && g_pipelineStep.load() >= 6) {
        HandleGetStatus(bytes, nBufferSize);
    } else if (op == static_cast<std::uint16_t>(RQ_HPchanged) && g_pipelineStep.load() >= 6 &&
               !g_waitingPutPlayerInGame.load()) {
        HandleHPchanged(bytes, nBufferSize);
    } else if (op == static_cast<std::uint16_t>(RQ_ManaChanged) && g_pipelineStep.load() >= 6 &&
               !g_waitingPutPlayerInGame.load()) {
        HandleManaChanged(bytes, nBufferSize);
    } else if (op == static_cast<std::uint16_t>(RQ_XPchanged) && g_pipelineStep.load() >= 6 &&
               !g_waitingPutPlayerInGame.load()) {
        HandleXPchanged(bytes, nBufferSize);
    } else if (op == static_cast<std::uint16_t>(RQ_LevelUp) && g_pipelineStep.load() >= 6 &&
               !g_waitingPutPlayerInGame.load()) {
        HandleLevelUp(bytes, nBufferSize);
    } else if (op == kTfcPacketPopup) {
        HandlePacketPopup(bytes, nBufferSize);
    } else if (op == kEventObjectAppearedList && g_pipelineStep.load() >= 6 && g_fromPreInGameResult.load() == 0) {
        HandleObjectAppearedList(bytes, nBufferSize);
    } else if (op == static_cast<std::uint16_t>(RQ_UnitUpdate) && g_pipelineStep.load() >= 6 &&
               g_fromPreInGameResult.load() == 0) {
        HandleUnitUpdate(bytes, nBufferSize);
    } else if (op == static_cast<std::uint16_t>(RQ_MissingUnit) && g_pipelineStep.load() >= 6 &&
               g_fromPreInGameResult.load() == 0) {
        HandleMissingUnit(bytes, nBufferSize);
    } else if (op == static_cast<std::uint16_t>(RQ_TeleportPlayer) && g_pipelineStep.load() >= 6 &&
               g_fromPreInGameResult.load() == 0) {
        HandleTeleportPlayer(bytes, nBufferSize);
    } else if (op == kEventObjectMoved && g_pipelineStep.load() >= 6 && g_fromPreInGameResult.load() == 0) {
        if (ApplyServerUnitPosition(bytes, nBufferSize)) {
            T4CActivePlayer snap{};
            {
                std::lock_guard<std::mutex> lock(g_activePlayerMutex);
                snap = g_activePlayer;
            }
            T4CNetworkDebugLogKind(
                T4CMatrixLogKind::Phase,
                "[PHASE] __EVENT_OBJECT_MOVED (1) — JOUEUR app=%u (0x%04X) id=%d @ %u,%u sprite=%s.",
                static_cast<unsigned>(snap.appearance), static_cast<unsigned>(snap.appearance),
                static_cast<int>(snap.unitId), snap.serverX, snap.serverY, T4CPlayerSpriteNpcName(snap));
        } else {
            ApplyRemoteUnitMove(bytes, nBufferSize);
        }
    }
}

static void JoinBackgroundLogoutThread() {
    std::lock_guard<std::mutex> tlock(g_logoutThreadMutex);
    if (g_logoutThread.joinable()) {
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Net,
                               "[NET] Attente fin deconnexion SafePlug (fenetre deja fermee)…");
        g_logoutThread.join();
        T4CNetworkDebugLog("[NET] Deconnexion SafePlug terminee — processus peut quitter.");
    }
}

/** Thread logout termine mais pas encore join() — sinon IsNetworkActive reste bloque a vie. */
static void ReapFinishedLogoutThread() {
    if (g_logoutInProgress.load()) {
        return;
    }
    std::lock_guard<std::mutex> tlock(g_logoutThreadMutex);
    if (g_logoutThread.joinable()) {
        g_logoutThread.join();
    }
}

/** Detruit CCommCenter sans tenir g_sessionMutex (sinon deadlock avec les threads UDP). */
static void DestroyCommOutsideLock() {
    std::unique_ptr<CCommCenter> dead;
    {
        std::lock_guard<std::mutex> lock(g_sessionMutex);
        dead = std::move(g_comm);
    }
    dead.reset();
}

static void LogoutWorker(int capturedPipelineStep) {
    TeardownAndDestroyComm(capturedPipelineStep, "quit monde / app (arriere-plan)");
    g_worldLogoutFinishedAt = std::chrono::steady_clock::now();
    SetReconnectCooldown(kPostLogoutReconnectCooldownSec);
    T4CNetworkDebugLogKind(T4CMatrixLogKind::Net,
                           "[NET] Deconnexion arriere-plan terminee — attendez ~%d s avant Connect "
                           "(le serveur libere la session sur l'ancien port UDP).",
                           kPostLogoutReconnectCooldownSec);
    g_logoutInProgress.store(false);
}

static void ShutdownUnlocked() {
    if (g_logoutInProgress.load()) {
        T4CNetworkDebugLog("[NET] Shutdown ignore : logout arriere-plan en cours.");
        return;
    }
    ReapFinishedLogoutThread();
    int step = 0;
    bool needTeardown = false;
    {
        std::lock_guard<std::mutex> lock(g_sessionMutex);
        step = g_pipelineStep.load();
        needTeardown = (g_comm != nullptr && !g_logoutSentThisSession.load());
    }
    if (needTeardown) {
        if (step >= 2) {
            TeardownAndDestroyComm(step, "session shutdown");
        } else {
            DestroyLoginSessionFast("session shutdown");
        }
        return;
    }
    {
        std::lock_guard<std::mutex> lock(g_sessionMutex);
        g_logoutSentThisSession.store(false);
        g_pipelineStep.store(0);
    }
    DestroyCommOutsideLock();
}

static bool ResolveServerAddr(const ParsedEndpoint &ep, sockaddr_in *out) {
    out->sin_family = AF_INET;
    out->sin_port = htons(ep.port);
    if (inet_pton(AF_INET, ep.host.c_str(), &out->sin_addr) == 1) {
        return true;
    }
    struct addrinfo hints {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    struct addrinfo *res = nullptr;
    const int gai = getaddrinfo(ep.host.c_str(), nullptr, &hints, &res);
    if (gai != 0 || !res) {
        return false;
    }
    for (struct addrinfo *p = res; p; p = p->ai_next) {
        if (p->ai_family == AF_INET && p->ai_addrlen >= sizeof(sockaddr_in)) {
            std::memcpy(out, p->ai_addr, sizeof(sockaddr_in));
            out->sin_port = htons(ep.port);
            freeaddrinfo(res);
            return true;
        }
    }
    freeaddrinfo(res);
    return false;
}

bool T4CLoginSessionStart(const std::string &hostField, const std::string &portField, const std::string &login,
                          const std::string &password) {
    if (g_logoutInProgress.load()) {
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Warn,
                               "[NET] Connexion refusee : deconnexion SafePlug encore en cours (~15 s apres Esc).");
        return false;
    }
    ReapFinishedLogoutThread();
    g_accountBlockedAlreadyLogged.store(false);
    {
        const int cooldown = T4CLoginSessionGetReconnectCooldownSeconds();
        if (cooldown > 0) {
            T4CNetworkDebugLogKind(T4CMatrixLogKind::Warn,
                                   "[NET] Connexion refusee : encore ~%d s avant reconnexion (liberation compte serveur).",
                                   cooldown);
            return false;
        }
    }

    T4CNetworkDebugBeginSession();
    g_successAlreadyShown.store(false);
    g_pendingCharacterList.store(false);
    g_pendingEnterWorld.store(false);
    g_enterWorldSpawn = {};
    {
        std::lock_guard<std::mutex> lock(g_characterMutex);
        g_characterList.clear();
    }
    g_logoutSentThisSession.store(false);
    g_lastLogin = login;
    g_lastPassword = password;

    ShutdownUnlocked();

    const ParsedEndpoint ep = BuildEndpoint(hostField, portField);
    if (ep.host.empty()) {
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Warn, "[NET] ERROR: empty host/IP.");
        return false;
    }

    sockaddr_in server{};
    if (!ResolveServerAddr(ep, &server)) {
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Warn, "[NET] ERROR: could not resolve host \"%s\".", ep.host.c_str());
        return false;
    }

    const std::vector<unsigned char> payload = BuildRegisterAccountPacket(login, password);

    {
        std::lock_guard<std::mutex> lock(g_sessionMutex);
        g_comm = std::make_unique<CCommCenter>(&g_monitorEmpty);
        if (!g_comm->Create(CommReadCallback, 0, 0, nullptr, nullptr)) {
            T4CNetworkDebugLogKind(T4CMatrixLogKind::Warn, "[NET] ERROR: CCommCenter::Create failed.");
            g_comm.reset();
            return false;
        }

#if defined(USE_CLIENT_CONNECTION)
        T4CNetworkDebugLog("[CRYPT] TFCCrypt: stub C actif (EncryptC/DecryptC — voir CryptStubLinux.h).");
#else
        T4CNetworkDebugLog("[CRYPT] Protocole filaire S (TFCCrypt::EncryptS / DecryptS, CryptMestoph).");
#endif
        T4CNetworkDebugLog("[NET] CCommCenter started (ephemeral UDP bind, IOCP compat threads).");
        T4CNetworkDebugLog("[NET] Server endpoint resolved, host=\"%s\" UDP port %u (default %u if port field empty/invalid).",
                          ep.host.c_str(), static_cast<unsigned>(ep.port), static_cast<unsigned>(kDefaultServerPort));
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Auth,
                               "[AUTH] Envoi RQ_RegisterAccount (%zu octets TFC), login_len=%zu pwd_len=%zu "
                               "(mot de passe non loggue — la taille TFC = 4 en-tete + 2 opcode + 1+login + 1+pwd + 4 version).",
                               payload.size(), login.size(), password.size());
        T4CNetworkDebugLogKind(
            T4CMatrixLogKind::Phase,
            "[PHASE] Pipeline Linux : **1/7** RQ_RegisterAccount (14) — **2/7** reponse 14 — "
            "**3/7** RQ_AuthenticateServerVersion (99) — **4/7** reponse 99 OK — "
            "post-auth → **26** liste persos → ecran selection → **13** PutPlayerInGame → **46**+**60** → monde. "
            "Keepalive : **RQ_Ack (10)** a chaque TFCStillConnected (10).");

        g_comm->SendPacket(server, const_cast<LPBYTE>(payload.data()), static_cast<int>(payload.size()), 0, 0);
        g_serverAddr = server;
        g_pipelineStep.store(1);
    }

    T4CNetworkDebugLog("[UDP] -> file d'envoi : paquet register (%zu octets applicatifs).", payload.size());
    T4CNetworkDebugLogKind(
        T4CMatrixLogKind::Phase,
        "[NET] **Etape courante : 1/5 envoyee.** Le client ne « lance » rien apres : le thread "
        "`UDPReceiveDataThread` est bloque dans **recvfrom()** sur le port local (voir InitSocket). "
        "La suite (DecryptS, [AUTH], paquets 26/10) n'apparait **qu'apres** une ligne `[UDP] <- recvfrom raw ...` "
        "— donc reception d'un datagramme depuis %s:%u. Si rien ne vient : serveur arrete, mauvais port, "
        "pare-feu UDP, ou le serveur ne repond pas a ce register.",
        ep.host.c_str(), static_cast<unsigned>(ep.port));
    return true;
}

void T4CLoginSessionDisconnectInGame() {
    std::lock_guard<std::mutex> tlock(g_logoutThreadMutex);
    if (g_logoutThread.joinable()) {
        return;
    }
    int capturedStep = 0;
    {
        std::lock_guard<std::mutex> lock(g_sessionMutex);
        capturedStep = g_pipelineStep.load();
        if (!g_comm || capturedStep < 2) {
            return;
        }
        if (capturedStep >= 4 && !g_logoutSentThisSession.load()) {
            SendSafePlugLogoutLocked("retour login depuis monde");
            g_logoutSentThisSession.store(true);
        }
        if (capturedStep >= 6) {
            SendExitGameLocked();
            ResetInGameClientStateAfterForcedExit();
        }
    }
    g_logoutInProgress.store(true);
    g_logoutThread = std::thread(LogoutWorker, capturedStep);
    T4CNetworkDebugLogKind(T4CMatrixLogKind::Net,
                           "[NET] Logout monde : SafePlug+ExitGame immediat, fermeture UDP en arriere-plan (Esc).");
}

bool T4CLoginSessionIsLogoutInProgress() {
    return g_logoutInProgress.load();
}

int T4CLoginSessionGetReconnectCooldownSeconds() {
    std::lock_guard<std::mutex> lock(g_reconnectCooldownMutex);
    return GetReconnectCooldownSecondsUnlocked();
}

std::string T4CLoginSessionGetWorldHudLine() {
    if (g_waitingFromPreInGame.load()) {
        return "Reseau: attente reponse 46 (async serveur)...";
    }
    const int r46 = g_fromPreInGameResult.load();
    if (r46 == 0) {
        T4CActivePlayer ap{};
        T4CLoginSessionGetActivePlayer(&ap);
        if (ap.valid) {
            char buf[192];
            std::snprintf(buf, sizeof(buf), "En jeu | %s @ %u,%u | fleches=move",
                          ap.name.c_str(), ap.serverX, ap.serverY);
            return buf;
        }
        return "Reseau: en jeu (46 OK) | fleches = deplacement";
    }
    if (r46 == 1) {
        return "Reseau: erreur 46 (unit monde?) | fleches = carte locale";
    }
    if (g_pipelineStep.load() >= 6) {
        return "Reseau: 46+60 envoyes | fleches = carte locale (pas move)";
    }
    return "Reseau: actif | fleches = carte locale (pas move)";
}

void T4CLoginSessionPollBackgroundTasks() {
    ReapFinishedLogoutThread();
    if (g_waitingPutPlayerInGame.load()) {
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() -
                                                             g_putPlayerRequestAt);
        if (elapsed.count() >= 60) {
            g_waitingPutPlayerInGame.store(false);
            g_pipelineStep.store(4);
            std::lock_guard<std::mutex> lock(g_putPlayerErrorMutex);
            g_putPlayerErrorMessage =
                "Delai depasse (60 s) sans reponse opcode 13 — voir stderr serveur "
                "([load_character] / [PutPlayerInGame]) et seed SQL.";
            T4CNetworkDebugLogKind(T4CMatrixLogKind::Warn, "[PHASE] %s", g_putPlayerErrorMessage.c_str());
        }
    }
    if (g_waitingCreatePlayer.load()) {
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() -
                                                             g_createPlayerRequestAt);
        if (elapsed.count() >= 60) {
            g_waitingCreatePlayer.store(false);
            g_inCreateRerollPhase.store(false);
            g_pipelineStep.store(4);
            std::string staleName;
            {
                std::lock_guard<std::mutex> lock(g_createPlayerErrorMutex);
                staleName = g_pendingCreatePlayerName;
                g_createPlayerErrorMessage = "Delai depasse (60 s) sans reponse opcode 25.";
                T4CNetworkDebugLogKind(T4CMatrixLogKind::Warn, "[PHASE] %s",
                                       g_createPlayerErrorMessage.c_str());
            }
            if (!staleName.empty()) {
                std::lock_guard<std::mutex> lock(g_sessionMutex);
                if (g_comm) {
                    SendDeletePlayerPacketsLocked(staleName);
                    T4CNetworkDebugLogKind(
                        T4CMatrixLogKind::Phase,
                        "[PHASE] Timeout opcode 25 — suppression perso provisoire « %s ».", staleName.c_str());
                }
            }
        }
    }
    if (g_waitingCreateReroll.load()) {
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() -
                                                             g_createRerollRequestAt);
        if (elapsed.count() >= 30) {
            g_waitingCreateReroll.store(false);
            std::lock_guard<std::mutex> lock(g_createPlayerErrorMutex);
            g_createPlayerErrorMessage = "Delai depasse (30 s) sans reponse opcode 31.";
            T4CNetworkDebugLogKind(T4CMatrixLogKind::Warn, "[PHASE] %s", g_createPlayerErrorMessage.c_str());
        }
    }
    if (g_waitingDeletePlayer.load()) {
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() -
                                                             g_deletePlayerRequestAt);
        if (elapsed.count() >= 30) {
            g_waitingDeletePlayer.store(false);
            std::lock_guard<std::mutex> lock(g_deletePlayerErrorMutex);
            g_deletePlayerErrorMessage = "Delai depasse (30 s) sans reponse opcode 15/26.";
            T4CNetworkDebugLogKind(T4CMatrixLogKind::Warn, "[PHASE] %s", g_deletePlayerErrorMessage.c_str());
        }
    }
}

void T4CLoginSessionAbortLogin() {
    if (g_logoutInProgress.load()) {
        return;
    }
    const bool wasAlreadyLoggedBlock = g_accountBlockedAlreadyLogged.exchange(false);
    int step = 0;
    {
        std::lock_guard<std::mutex> lock(g_sessionMutex);
        step = g_pipelineStep.load();
    }
    if (step >= 2) {
        ShutdownUnlocked();
    } else {
        DestroyLoginSessionFast("nouveau Connect");
    }
    if (wasAlreadyLoggedBlock) {
        SetReconnectCooldown(kPostAlreadyLoggedCooldownSec);
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Warn,
                               "[NET] Connect annule — attendez ~%d s (compte encore occupe cote serveur).",
                               kPostAlreadyLoggedCooldownSec);
    } else {
        T4CNetworkDebugLog("[NET] Session login annulee (nouveau Connect).");
    }
}

bool T4CLoginSessionIsNetworkActive() {
    if (g_logoutInProgress.load()) {
        return true;
    }
    ReapFinishedLogoutThread();
    std::lock_guard<std::mutex> lock(g_sessionMutex);
    return g_comm != nullptr;
}

void T4CLoginSessionShutdown() {
    JoinBackgroundLogoutThread();
    std::lock_guard<std::mutex> lock(g_sessionMutex);
    ShutdownUnlocked();
}

bool T4CLoginSessionConsumeCharacterListReady() {
    return g_pendingCharacterList.exchange(false);
}

void T4CLoginSessionCopyCharacterList(std::vector<T4CCharacterSlot> *outSlots, int *outMaxPerAccount) {
    if (!outSlots) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_characterMutex);
    *outSlots = g_characterList;
    if (outMaxPerAccount) {
        *outMaxPerAccount = g_maxCharactersPerAccount;
    }
}

bool T4CLoginSessionConsumeCreatePlayerSuccess() {
    return g_pendingCreatePlayerSuccess.exchange(false);
}

bool T4CLoginSessionRequestDeletePlayer(const std::string &playerName) {
    if (playerName.empty()) {
        return false;
    }
    if (g_waitingCreatePlayer.load() || g_waitingPutPlayerInGame.load() || g_waitingDeletePlayer.load()) {
        return false;
    }

    const int step = g_pipelineStep.load();
    if (step < 4 || step >= 6) {
        return false;
    }

    {
        std::lock_guard<std::mutex> errLock(g_deletePlayerErrorMutex);
        g_deletePlayerErrorMessage.clear();
        g_lastDeletePlayerName = playerName;
    }

    std::lock_guard<std::mutex> lock(g_sessionMutex);
    if (!g_comm) {
        return false;
    }
    SendDeletePlayerPacketsLocked(playerName);
    return true;
}

bool T4CLoginSessionIsWaitingDeletePlayer() {
    return g_waitingDeletePlayer.load();
}

bool T4CLoginSessionHasDeletePlayerError() {
    std::lock_guard<std::mutex> lock(g_deletePlayerErrorMutex);
    return !g_deletePlayerErrorMessage.empty();
}

std::string T4CLoginSessionGetDeletePlayerErrorMessage() {
    std::lock_guard<std::mutex> lock(g_deletePlayerErrorMutex);
    return g_deletePlayerErrorMessage;
}

void T4CLoginSessionClearDeletePlayerError() {
    std::lock_guard<std::mutex> lock(g_deletePlayerErrorMutex);
    g_deletePlayerErrorMessage.clear();
}

bool T4CLoginSessionRequestPutPlayerInGame(const std::string &playerName) {
    if (playerName.empty()) {
        return false;
    }
    if (g_waitingPutPlayerInGame.load()) {
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(g_activePlayerMutex);
        g_activePlayer = {};
        g_activePlayer.name = playerName;
        g_activePlayer.valid = true;
        std::lock_guard<std::mutex> clist(g_characterMutex);
        for (const T4CCharacterSlot &slot : g_characterList) {
            if (slot.name == playerName) {
                g_activePlayer.race = slot.race;
                g_activePlayer.level = slot.level;
                const int raceCls = ClassIndexFromRaceField(slot.race);
                if (raceCls >= 0) {
                    g_activePlayer.classIndex = static_cast<std::uint8_t>(raceCls);
                }
                break;
            }
        }
        const auto clsIt = g_characterClassByName.find(playerName);
        if (clsIt != g_characterClassByName.end()) {
            g_activePlayer.classIndex = clsIt->second;
        }
    }
    g_playerPopupPending.store(false);

    std::lock_guard<std::mutex> lock(g_sessionMutex);
    if (!g_comm) {
        return false;
    }
    {
        std::lock_guard<std::mutex> errLock(g_putPlayerErrorMutex);
        g_putPlayerErrorMessage.clear();
    }
    const auto pkt = BuildPutPlayerInGamePacket(playerName);
    g_comm->SendPacket(g_serverAddr, const_cast<LPBYTE>(pkt.data()), static_cast<int>(pkt.size()), 0, 0);
    g_pipelineStep.store(5);
    g_waitingPutPlayerInGame.store(true);
    g_putPlayerRequestAt = std::chrono::steady_clock::now();
    T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase,
                           "[PHASE] Envoi RQ_PutPlayerInGame (13) pour « %s » (%zu octets TFC).",
                           playerName.c_str(), pkt.size());
    return true;
}

static void TryAutoEnterWorldAfterCreateList() {
    if (!g_autoEnterWorldAfterCreate.exchange(false)) {
        return;
    }
    std::string name;
    {
        std::lock_guard<std::mutex> lock(g_createPlayerErrorMutex);
        name = g_pendingCreatePlayerName;
    }
    if (name.empty()) {
        g_pendingCreatePlayerSuccess.store(true);
        return;
    }
    if (!T4CLoginSessionRequestPutPlayerInGame(name)) {
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Warn,
                               "[PHASE] Entree auto en monde impossible pour « %s » — retour liste.",
                               name.c_str());
        g_pendingCreatePlayerSuccess.store(true);
        return;
    }
    T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase,
                           "[PHASE] Creation confirmee — RQ_PutPlayerInGame (13) pour « %s ».",
                           name.c_str());
}

bool T4CLoginSessionIsWaitingPutPlayerInGame() {
    return g_waitingPutPlayerInGame.load();
}

bool T4CLoginSessionHasPutPlayerInGameError() {
    std::lock_guard<std::mutex> lock(g_putPlayerErrorMutex);
    return !g_putPlayerErrorMessage.empty();
}

std::string T4CLoginSessionGetPutPlayerInGameErrorMessage() {
    std::lock_guard<std::mutex> lock(g_putPlayerErrorMutex);
    return g_putPlayerErrorMessage;
}

void T4CLoginSessionClearPutPlayerInGameError() {
    std::lock_guard<std::mutex> lock(g_putPlayerErrorMutex);
    g_putPlayerErrorMessage.clear();
}

bool T4CLoginSessionRequestCreatePlayer(const std::string &name, unsigned char sex,
                                        const unsigned char stats[5]) {
    if (name.empty() || !stats) {
        return false;
    }
    if (g_inCreateRerollPhase.load() || g_waitingCreateReroll.load() || g_waitingCreatePlayer.load() ||
        g_waitingPutPlayerInGame.load() || g_waitingDeletePlayer.load()) {
        T4CNetworkDebugLogKind(
            T4CMatrixLogKind::Warn,
            "[PHASE] RQ_CreatePlayer (25) bloque : reroll=%d wait25=%d wait31=%d wait13=%d wait15=%d.",
            g_inCreateRerollPhase.load() ? 1 : 0, g_waitingCreatePlayer.load() ? 1 : 0,
            g_waitingCreateReroll.load() ? 1 : 0, g_waitingPutPlayerInGame.load() ? 1 : 0,
            g_waitingDeletePlayer.load() ? 1 : 0);
        return false;
    }
    std::lock_guard<std::mutex> lock(g_sessionMutex);
    if (!g_comm || g_pipelineStep.load() < 4) {
        return false;
    }
    {
        std::lock_guard<std::mutex> errLock(g_createPlayerErrorMutex);
        g_createPlayerErrorMessage.clear();
        g_pendingCreatePlayerName = name;
    }
    g_characterClassByName[name] = static_cast<std::uint8_t>(ClassIndexFromQuestionnaireStats(stats));
    const auto pkt = BuildCreatePlayerPacket(name, sex, stats);
    g_comm->SendPacket(g_serverAddr, const_cast<LPBYTE>(pkt.data()), static_cast<int>(pkt.size()), 0, 0);
    g_waitingCreatePlayer.store(true);
    g_createPlayerRequestAt = std::chrono::steady_clock::now();
    T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase,
                           "[PHASE] Envoi RQ_CreatePlayer (25) pour « %s » sex=%u (%zu octets TFC).",
                           name.c_str(), static_cast<unsigned>(sex), pkt.size());
    return true;
}

bool T4CLoginSessionIsWaitingCreatePlayer() {
    return g_waitingCreatePlayer.load();
}

bool T4CLoginSessionHasCreatePlayerError() {
    std::lock_guard<std::mutex> lock(g_createPlayerErrorMutex);
    return !g_createPlayerErrorMessage.empty();
}

std::string T4CLoginSessionGetCreatePlayerErrorMessage() {
    std::lock_guard<std::mutex> lock(g_createPlayerErrorMutex);
    return g_createPlayerErrorMessage;
}

void T4CLoginSessionClearCreatePlayerError() {
    std::lock_guard<std::mutex> lock(g_createPlayerErrorMutex);
    g_createPlayerErrorMessage.clear();
}

void T4CLoginSessionPrepareForCreateScreen() {
    std::string staleName;
    {
        std::lock_guard<std::mutex> lock(g_createPlayerErrorMutex);
        staleName = g_pendingCreatePlayerName;
        g_createPlayerErrorMessage.clear();
    }
    g_waitingCreatePlayer.store(false);
    g_waitingCreateReroll.store(false);
    g_rolledStatsPending.store(false);
    {
        std::lock_guard<std::mutex> lock(g_rolledStatsMutex);
        g_rolledStats = {};
    }
    if (g_inCreateRerollPhase.load() || !staleName.empty()) {
        std::lock_guard<std::mutex> lock(g_sessionMutex);
        if (g_comm && !staleName.empty()) {
            SendDeletePlayerPacketsLocked(staleName);
        }
        g_inCreateRerollPhase.store(false);
        {
            std::lock_guard<std::mutex> lock(g_createPlayerErrorMutex);
            g_pendingCreatePlayerName.clear();
        }
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase,
                               "[PHASE] Ecran creation — annulation reroll stale « %s ».",
                               staleName.empty() ? "?" : staleName.c_str());
    }
}

bool T4CLoginSessionIsInCreateRerollPhase() {
    return g_inCreateRerollPhase.load();
}

bool T4CLoginSessionConsumeRolledStatsUpdate(T4CCharacterRolledStats *outStats) {
    if (!g_rolledStatsPending.exchange(false)) {
        return false;
    }
    if (outStats) {
        std::lock_guard<std::mutex> lock(g_rolledStatsMutex);
        *outStats = g_rolledStats;
    }
    return true;
}

bool T4CLoginSessionRequestCreateReroll() {
    if (!g_inCreateRerollPhase.load() || g_waitingCreateReroll.load() || g_waitingCreatePlayer.load()) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_sessionMutex);
    if (!g_comm) {
        return false;
    }
    const auto pkt = BuildOpcodeOnlyPacket(static_cast<std::uint16_t>(RQ_Reroll));
    g_comm->SendPacket(g_serverAddr, const_cast<LPBYTE>(pkt.data()), static_cast<int>(pkt.size()), 0, 0);
    g_waitingCreateReroll.store(true);
    g_createRerollRequestAt = std::chrono::steady_clock::now();
    T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase, "[PHASE] Envoi RQ_Reroll (31).");
    return true;
}

bool T4CLoginSessionConfirmCreateReroll() {
    if (!g_inCreateRerollPhase.load()) {
        return false;
    }
    g_autoEnterWorldAfterCreate.store(true);
    RequestCharacterListRefresh();
    g_inCreateRerollPhase.store(false);
    g_waitingCreateReroll.store(false);
    g_pipelineStep.store(4);
    T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase,
                           "[PHASE] Creation validee — refresh 26 puis entree en monde (aligne Windows).");
    return true;
}

bool T4CLoginSessionCancelCreateReroll() {
    if (!g_inCreateRerollPhase.load()) {
        return false;
    }
    std::string target;
    {
        std::lock_guard<std::mutex> lock(g_createPlayerErrorMutex);
        target = g_pendingCreatePlayerName;
    }
    if (target.empty()) {
        g_inCreateRerollPhase.store(false);
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(g_sessionMutex);
        if (g_comm) {
            SendDeletePlayerPacketsLocked(target);
        }
    }
    g_inCreateRerollPhase.store(false);
    g_waitingCreateReroll.store(false);
    {
        std::lock_guard<std::mutex> lock(g_createPlayerErrorMutex);
        g_pendingCreatePlayerName.clear();
    }
    T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase,
                           "[PHASE] Creation annulee — RQ_DeletePlayer (15) pour « %s ».", target.c_str());
    return true;
}

bool T4CLoginSessionConsumeEnterWorldReady(T4CEnterWorldSpawn *outSpawn) {
    if (!g_pendingEnterWorld.exchange(false)) {
        return false;
    }
    if (outSpawn) {
        *outSpawn = g_enterWorldSpawn;
    }
    return g_enterWorldSpawn.valid;
}

void T4CLoginSessionGetActivePlayer(T4CActivePlayer *outPlayer) {
    if (!outPlayer) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_activePlayerMutex);
    *outPlayer = g_activePlayer;
}

void T4CLoginSessionGetPlayerStatus(T4CPlayerStatus *outStatus) {
    if (!outStatus) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_playerStatusMutex);
    *outStatus = g_playerStatus;
}

bool T4CLoginSessionConsumePlayerStatusUpdate(T4CPlayerStatus *outStatus) {
    if (!g_playerStatusPending.exchange(false)) {
        return false;
    }
    T4CLoginSessionGetPlayerStatus(outStatus);
    return outStatus && outStatus->valid;
}

bool T4CLoginSessionRequestPlayerStatus() {
    std::lock_guard<std::mutex> lock(g_sessionMutex);
    if (!g_comm || g_pipelineStep.load() < 6) {
        return false;
    }
    const auto pkt = BuildOpcodeOnlyPacket(static_cast<std::uint16_t>(RQ_GetStatus));
    g_comm->SendPacket(g_serverAddr, const_cast<LPBYTE>(pkt.data()), static_cast<int>(pkt.size()), 0, 0);
    T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase, "[PHASE] Envoi RQ_GetStatus (43) — refresh stats HUD.");
    return true;
}

void T4CLoginSessionGetBackpack(T4CPlayerBackpack *out) {
    if (!out) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_inventoryMutex);
    *out = g_backpack;
}

void T4CLoginSessionGetSkillBook(T4CPlayerSkillBook *out) {
    if (!out) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_inventoryMutex);
    *out = g_skillBook;
}

void T4CLoginSessionGetSpellBook(T4CPlayerSpellBook *out) {
    if (!out) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_inventoryMutex);
    *out = g_spellBook;
}

void T4CLoginSessionGetBankChest(T4CPlayerBankChest *out) {
    if (!out) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_inventoryMutex);
    *out = g_bankChest;
}

void T4CLoginSessionGetEquipment(T4CPlayerEquipment *out) {
    if (!out) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_inventoryMutex);
    *out = g_equipment;
}

bool T4CLoginSessionConsumeInventoryUpdate() {
    return g_inventoryPending.exchange(false);
}

bool T4CLoginSessionRequestSkillList() {
    std::lock_guard<std::mutex> lock(g_sessionMutex);
    if (!g_comm || g_pipelineStep.load() < 6) {
        return false;
    }
    SendGetSkillListLocked();
    return true;
}

bool T4CLoginSessionRequestSpellList() {
    std::lock_guard<std::mutex> lock(g_sessionMutex);
    if (!g_comm || g_pipelineStep.load() < 6) {
        return false;
    }
    SendGetSpellListLocked();
    return true;
}

bool T4CLoginSessionRequestViewBackpack() {
    std::lock_guard<std::mutex> lock(g_sessionMutex);
    if (!g_comm || g_pipelineStep.load() < 6) {
        return false;
    }
    SendViewBackpackRequestLocked();
    return true;
}

bool T4CLoginSessionRequestViewEquipped() {
    std::lock_guard<std::mutex> lock(g_sessionMutex);
    if (!g_comm || g_pipelineStep.load() < 6) {
        return false;
    }
    SendViewEquippedRequestLocked();
    return true;
}

void T4CLoginSessionPollItemNameRequests(const T4CItemSearchPlace place, const int maxPerTick) {
    if (maxPerTick <= 0) {
        return;
    }
    const unsigned char placeByte = static_cast<unsigned char>(place);
    std::vector<std::int32_t> toQuery;
    toQuery.reserve(static_cast<std::size_t>(maxPerTick));
    {
        std::lock_guard<std::mutex> lock(g_inventoryMutex);
        std::vector<T4CBagItem> *items = nullptr;
        if (place == T4CItemSearchPlace::Backpack && g_backpack.valid) {
            items = &g_backpack.items;
        } else if (place == T4CItemSearchPlace::BankChest && g_bankChest.valid) {
            items = &g_bankChest.items;
        }
        if (!items) {
            return;
        }
        for (T4CBagItem &it : *items) {
            if (static_cast<int>(toQuery.size()) >= maxPerTick) {
                break;
            }
            if (it.objectId == 0 || !it.name.empty() || it.nameQueryPending) {
                continue;
            }
            it.nameQueryPending = true;
            toQuery.push_back(it.objectId);
        }
    }
    if (toQuery.empty()) {
        return;
    }
    std::lock_guard<std::mutex> sessionLock(g_sessionMutex);
    if (!g_comm || g_pipelineStep.load() < 6) {
        std::lock_guard<std::mutex> lock(g_inventoryMutex);
        std::vector<T4CBagItem> *items = nullptr;
        if (place == T4CItemSearchPlace::Backpack) {
            items = &g_backpack.items;
        } else if (place == T4CItemSearchPlace::BankChest) {
            items = &g_bankChest.items;
        }
        if (items) {
            for (T4CBagItem &it : *items) {
                if (std::find(toQuery.begin(), toQuery.end(), it.objectId) != toQuery.end()) {
                    it.nameQueryPending = false;
                }
            }
        }
        return;
    }
    for (const std::int32_t objectId : toQuery) {
        SendQueryItemNameLocked(placeByte, objectId);
    }
}

bool T4CLoginSessionIsBankChestUiVisible() {
    std::lock_guard<std::mutex> lock(g_inventoryMutex);
    return g_bankChest.uiVisible;
}

bool T4CLoginSessionConsumePlayerPopupUpdate(T4CActivePlayer *outPlayer) {
    if (!g_playerPopupPending.exchange(false)) {
        return false;
    }
    T4CLoginSessionGetActivePlayer(outPlayer);
    return outPlayer && outPlayer->valid;
}

bool T4CLoginSessionConsumePlayerTeleport(T4CPlayerTeleport *outTeleport) {
    if (!g_playerTeleportPending.exchange(false)) {
        return false;
    }
    if (outTeleport) {
        *outTeleport = g_pendingTeleport;
    }
    return true;
}

void T4CLoginSessionUpdateActivePlayerPosition(const unsigned int x, const unsigned int y) {
    std::lock_guard<std::mutex> lock(g_activePlayerMutex);
    if (g_activePlayer.valid) {
        g_activePlayer.serverX = x;
        g_activePlayer.serverY = y;
    }
}

void T4CLoginSessionDrainRemoteUnitEvents(std::vector<T4CRemoteUnitEvent> *outEvents) {
    if (!outEvents) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_remoteEventsMutex);
    outEvents->insert(outEvents->end(), g_remoteEvents.begin(), g_remoteEvents.end());
    g_remoteEvents.clear();
}

void T4CLoginSessionClearRemoteUnits() {
    {
        std::lock_guard<std::mutex> lock(g_remoteEventsMutex);
        g_remoteEvents.clear();
    }
    {
        std::lock_guard<std::mutex> lock(g_groundObjectsMutex);
        g_groundObjectsByUnitId.clear();
    }
}

void T4CLoginSessionCopyGroundObjectMarkers(std::vector<T4CGroundObjectMarker> *outMarkers) {
    if (!outMarkers) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_groundObjectsMutex);
    outMarkers->clear();
    outMarkers->reserve(g_groundObjectsByUnitId.size());
    for (const auto &kv : g_groundObjectsByUnitId) {
        outMarkers->push_back(kv.second);
    }
}

bool T4CLoginSessionSendMove(const std::uint16_t moveOpcode) {
    if (moveOpcode < 1 || moveOpcode > 8) {
        return false;
    }
    if (g_pipelineStep.load() < 6 || g_fromPreInGameResult.load() != 0) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_sessionMutex);
    if (!g_comm) {
        return false;
    }
    const auto pkt = BuildOpcodeOnlyPacket(moveOpcode);
    g_comm->SendPacket(g_serverAddr, const_cast<LPBYTE>(pkt.data()), static_cast<int>(pkt.size()), 0, 0);
    T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase, "[PHASE] Envoi deplacement opcode %u.", static_cast<unsigned>(moveOpcode));
    return true;
}

bool T4CLoginSessionConsumeNetworkSuccessDialog() {
    return T4CLoginSessionConsumeCharacterListReady();
}

void T4CLoginSessionResetAfterReturnToLogin() {
    g_successAlreadyShown.store(false);
    g_pendingCharacterList.store(false);
    g_pendingEnterWorld.store(false);
    g_waitingPutPlayerInGame.store(false);
    g_waitingCreatePlayer.store(false);
    g_pendingCreatePlayerSuccess.store(false);
    g_inCreateRerollPhase.store(false);
    g_waitingCreateReroll.store(false);
    g_autoEnterWorldAfterCreate.store(false);
    g_rolledStatsPending.store(false);
    {
        std::lock_guard<std::mutex> lock(g_createPlayerErrorMutex);
        g_pendingCreatePlayerName.clear();
    }
    {
        std::lock_guard<std::mutex> lock(g_rolledStatsMutex);
        g_rolledStats = {};
    }
    g_waitingDeletePlayer.store(false);
    g_waitingFromPreInGame.store(false);
    g_pendingPost13Pipeline.store(false);
    g_fromPreInGameResult.store(-1);
    g_enterWorldSpawn = {};
    g_playerPopupPending.store(false);
    g_playerTeleportPending.store(false);
    g_pendingTeleport = {};
    T4CLoginSessionClearRemoteUnits();
    {
        std::lock_guard<std::mutex> lock(g_activePlayerMutex);
        g_activePlayer = {};
    }
    {
        std::lock_guard<std::mutex> lock(g_playerStatusMutex);
        g_playerStatus = {};
    }
    g_playerStatusPending.store(false);
    ResetInventoryStateLocked();
    {
        std::lock_guard<std::mutex> lock(g_characterMutex);
        g_characterList.clear();
    }
    {
        std::lock_guard<std::mutex> lock(g_putPlayerErrorMutex);
        g_putPlayerErrorMessage.clear();
    }
    {
        std::lock_guard<std::mutex> lock(g_createPlayerErrorMutex);
        g_createPlayerErrorMessage.clear();
    }
    {
        std::lock_guard<std::mutex> lock(g_deletePlayerErrorMutex);
        g_deletePlayerErrorMessage.clear();
        g_lastDeletePlayerName.clear();
    }
    g_accountBlockedAlreadyLogged.store(false);
}

#else

#include "network/T4CLoginSession.h"

bool T4CLoginSessionStart(const std::string &, const std::string &, const std::string &, const std::string &) {
    return false;
}

void T4CLoginSessionShutdown() {}

void T4CLoginSessionDisconnectInGame() {}

bool T4CLoginSessionIsNetworkActive() {
    return false;
}

void T4CLoginSessionResetAfterReturnToLogin() {}

bool T4CLoginSessionIsLogoutInProgress() {
    return false;
}

int T4CLoginSessionGetReconnectCooldownSeconds() {
    return 0;
}

void T4CLoginSessionPollBackgroundTasks() {}

std::string T4CLoginSessionGetWorldHudLine() {
    return {};
}

void T4CLoginSessionAbortLogin() {}

bool T4CLoginSessionConsumeCharacterListReady() {
    return false;
}

void T4CLoginSessionCopyCharacterList(std::vector<T4CCharacterSlot> *, int *) {}

bool T4CLoginSessionRequestPutPlayerInGame(const std::string &) {
    return false;
}

bool T4CLoginSessionIsWaitingPutPlayerInGame() {
    return false;
}

bool T4CLoginSessionHasPutPlayerInGameError() {
    return false;
}

std::string T4CLoginSessionGetPutPlayerInGameErrorMessage() {
    return {};
}

void T4CLoginSessionClearPutPlayerInGameError() {}

bool T4CLoginSessionRequestCreatePlayer(const std::string &, unsigned char, const unsigned char *) {
    return false;
}

bool T4CLoginSessionIsWaitingCreatePlayer() {
    return false;
}

bool T4CLoginSessionHasCreatePlayerError() {
    return false;
}

std::string T4CLoginSessionGetCreatePlayerErrorMessage() {
    return {};
}

void T4CLoginSessionClearCreatePlayerError() {}

void T4CLoginSessionPrepareForCreateScreen() {}

bool T4CLoginSessionRequestQueryNameExistence(const std::string &) {
    return false;
}

bool T4CLoginSessionConsumeCreatePlayerSuccess() {
    return false;
}

bool T4CLoginSessionIsInCreateRerollPhase() {
    return false;
}

bool T4CLoginSessionConsumeRolledStatsUpdate(T4CCharacterRolledStats *) {
    return false;
}

bool T4CLoginSessionRequestCreateReroll() {
    return false;
}

bool T4CLoginSessionConfirmCreateReroll() {
    return false;
}

bool T4CLoginSessionCancelCreateReroll() {
    return false;
}

bool T4CLoginSessionRequestDeletePlayer(const std::string &) {
    return false;
}

bool T4CLoginSessionIsWaitingDeletePlayer() {
    return false;
}

bool T4CLoginSessionHasDeletePlayerError() {
    return false;
}

std::string T4CLoginSessionGetDeletePlayerErrorMessage() {
    return {};
}

void T4CLoginSessionClearDeletePlayerError() {}

bool T4CLoginSessionConsumeEnterWorldReady(T4CEnterWorldSpawn *) {
    return false;
}

void T4CLoginSessionGetActivePlayer(T4CActivePlayer *) {}

void T4CLoginSessionGetPlayerStatus(T4CPlayerStatus *) {}

bool T4CLoginSessionConsumePlayerStatusUpdate(T4CPlayerStatus *) {
    return false;
}

bool T4CLoginSessionRequestPlayerStatus() {
    return false;
}

void T4CLoginSessionGetBackpack(T4CPlayerBackpack *) {}

void T4CLoginSessionGetSkillBook(T4CPlayerSkillBook *) {}

void T4CLoginSessionGetSpellBook(T4CPlayerSpellBook *) {}

void T4CLoginSessionGetBankChest(T4CPlayerBankChest *) {}

void T4CLoginSessionGetEquipment(T4CPlayerEquipment *) {}

bool T4CLoginSessionConsumeInventoryUpdate() {
    return false;
}

bool T4CLoginSessionRequestSkillList() {
    return false;
}

bool T4CLoginSessionRequestSpellList() {
    return false;
}

bool T4CLoginSessionRequestViewBackpack() {
    return false;
}

bool T4CLoginSessionRequestViewEquipped() {
    return false;
}

void T4CLoginSessionPollItemNameRequests(T4CItemSearchPlace, int) {}

bool T4CLoginSessionIsBankChestUiVisible() {
    return false;
}

bool T4CLoginSessionConsumePlayerPopupUpdate(T4CActivePlayer *) {
    return false;
}

bool T4CLoginSessionConsumePlayerTeleport(T4CPlayerTeleport *) {
    return false;
}

bool T4CLoginSessionSendMove(std::uint16_t) {
    return false;
}

void T4CLoginSessionUpdateActivePlayerPosition(unsigned int, unsigned int) {}

void T4CLoginSessionDrainRemoteUnitEvents(std::vector<T4CRemoteUnitEvent> *) {}

void T4CLoginSessionClearRemoteUnits() {}

void T4CLoginSessionCopyGroundObjectMarkers(std::vector<T4CGroundObjectMarker> *) {}

const char *T4CSpriteNameFromAppearance(std::uint16_t) {
    return "BlackWarrior";
}

const char *T4CPlayerSpriteNpcName(const T4CActivePlayer &) {
    return "Warrio";
}

bool T4CLoginSessionConsumeNetworkSuccessDialog() {
    return false;
}

#endif
