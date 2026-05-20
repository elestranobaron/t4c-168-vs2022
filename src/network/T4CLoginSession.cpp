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
static std::atomic<bool> g_playerPopupPending{false};
static T4CPlayerTeleport g_pendingTeleport{};
static std::atomic<bool> g_playerTeleportPending{false};

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

/** Ferme la session UDP proprement (hors mutex pendant l'attente / destroy). */
static void TeardownAndDestroyComm(int pipelineStep, const char *reason, bool waitForServerRelease = false) {
    bool hadComm = false;
    const bool sendSafePlug = (pipelineStep >= 4);
    {
        std::lock_guard<std::mutex> lock(g_sessionMutex);
        hadComm = (g_comm != nullptr);
        if (hadComm && sendSafePlug) {
            SendSafePlugLogoutLocked(reason);
            g_logoutSentThisSession.store(true);
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
        WaitAfterLogoutFlush(pipelineStep);
        /* Apres 15 s SafePlug : l'inactivite cote serveur >= 13 s, donc RQFUNC_ExitGame
         * (TFCMessagesHandler.cpp:3044) accepte et fait user->DeletePlayer() ->
         * AsyncDeletePlayer -> Logoff() -> DELETE FROM OnlineUsers. Sans ca, on attend
         * l'idle timeout 75 s avant que le compte soit liberable. */
        {
            std::lock_guard<std::mutex> lock(g_sessionMutex);
            if (g_comm) {
                const std::vector<unsigned char> exitPkt = BuildExitGamePacket();
                g_comm->SendPacket(g_serverAddr, const_cast<LPBYTE>(exitPkt.data()),
                                   static_cast<int>(exitPkt.size()), 0, 0);
                T4CNetworkDebugLogKind(T4CMatrixLogKind::Net,
                                       "[NET] Logout (%s) : RQ_ExitGame (20) envoye apres 15 s SafePlug "
                                       "-> declenche DeletePlayer/DELETE OnlineUsers cote serveur.", reason);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
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

static void SendGetNearItemsLocked() {
    const auto pkt = BuildOpcodeOnlyPacket(static_cast<std::uint16_t>(RQ_GetNearItems));
    SendToServerLocked(pkt);
    T4CNetworkDebugLog("[UDP] -> RQ_GetNearItems (60).");
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

static bool IsFemaleAppearance(std::uint16_t appearance) {
    return appearance == 10012 || appearance == 15012 || (appearance >= 15001 && appearance <= 15004);
}

const char *T4CPlayerSpriteNpcName(const T4CActivePlayer &player) {
    static const char *const kClassSprites[] = {"Warrio", "Wizard", "Cleric", "Thief"};
    int cls = ClassIndexFromAppearance(player.appearance);
    if (cls < 0) {
        cls = static_cast<int>(player.race);
    }
    if (cls < 0 || cls > 3) {
        cls = 0;
    }
    return kClassSprites[cls];
}

static void ApplyServerUnitPosition(const unsigned char *data, int len) {
    if (!data || len < 10 || !TfcHeaderLooksSane(data, len)) {
        return;
    }
    const std::int16_t sx = static_cast<std::int16_t>(ReadBeUint16(data, 6, len));
    const std::int16_t sy = static_cast<std::int16_t>(ReadBeUint16(data, 8, len));
    if (sx < 0 || sy < 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_activePlayerMutex);
    g_activePlayer.serverX = static_cast<unsigned int>(sx);
    g_activePlayer.serverY = static_cast<unsigned int>(sy);
    g_activePlayer.valid = true;
    g_playerPopupPending.store(true);
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

    T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase,
                           "[PHASE] RQ_TeleportPlayer (57) : @ %u,%u monde %u.",
                           g_pendingTeleport.x, g_pendingTeleport.y,
                           static_cast<unsigned>(g_pendingTeleport.world));

    SendGetNearItemsLocked();
    SendFromPreInGameToInGameLocked();
}

static void HandlePacketPopup(const unsigned char *data, int len) {
    if (!data || len < 16 || !TfcHeaderLooksSane(data, len)) {
        return;
    }
    const std::int16_t sx = static_cast<std::int16_t>(ReadBeUint16(data, 6, len));
    const std::int16_t sy = static_cast<std::int16_t>(ReadBeUint16(data, 8, len));
    const std::uint16_t appearance = ReadBeUint16(data, 10, len);
    const std::int32_t unitId = ReadBeInt32Msf(data, 12, len);

    std::lock_guard<std::mutex> lock(g_activePlayerMutex);
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
        "[PHASE] PacketPopup (10004) : pos %u,%u appearance=%u id=%d sprite=%s%s.",
        g_activePlayer.serverX, g_activePlayer.serverY, static_cast<unsigned>(appearance), static_cast<int>(unitId),
        T4CPlayerSpriteNpcName(g_activePlayer), g_activePlayer.female ? " (F)" : "");
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
        case RQ_ViewEquiped:
            return "RQ_ViewEquiped";
        case RQ_ViewBackpack:
            return "RQ_ViewBackpack";
        case RQ_HPchanged:
            return "RQ_HPchanged";
        case RQ_ManaChanged:
            return "RQ_ManaChanged";
        case RQ_LevelUp:
            return "RQ_LevelUp";
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

static void __cdecl CommReadCallback(sockaddr_in /*fromServer*/, LPBYTE lpbBuffer, int nBufferSize) {
    if (!lpbBuffer || nBufferSize <= 0) {
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Warn, "[UDP] <- (empty application payload)");
        return;
    }

    const auto *bytes = reinterpret_cast<const unsigned char *>(lpbBuffer);
    LogPayloadHexNet(bytes, nBufferSize, 24);

    if (nBufferSize >= 4 && !TfcHeaderLooksSane(bytes, nBufferSize)) {
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Warn,
                               "[AUTH] En-tete TFC non standard (attendu 00 00 00 00) — opcode / texte peuvent etre faux "
                               "(couche S + TYPE_MASK / DecryptS2).");
    }

    const std::uint16_t op = ReadOpcodeFromTfcpayload(bytes, nBufferSize);
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

    if (op == kTfcStillConnected) {
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Auth,
                               "[AUTH] Keepalive TFCStillConnected (10) — envoi RQ_Ack.");
        SendStillConnectedAck();
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
        if (g_pipelineStep.load() >= 3) {
            g_pipelineStep.store(4);
        }
        SDL_Log("[NETWORK] Liste persos recue — ecran selection.");
        if (!g_successAlreadyShown.exchange(true)) {
            g_pendingCharacterList.store(true);
        }
    } else if (op == static_cast<std::uint16_t>(RQ_PutPlayerInGame)) {
        HandlePutPlayerInGameReply(bytes, nBufferSize);
    } else if (op == static_cast<std::uint16_t>(RQ_ViewBackpack)) {
        T4CNetworkDebugLogKind(
            T4CMatrixLogKind::Phase,
            "[PHASE] Opcode 18 (ViewBackpack) — fin du chargement 13 cote serveur.");
        if (g_pendingPost13Pipeline.exchange(false)) {
            SendFromPreInGameToInGameLocked();
            SendGetNearItemsLocked();
            T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase,
                                   "[PHASE] Apres 18 : envoi 46 puis 60 (aligne Windows / serveur boPreInGame).");
        }
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
    } else if (op == kTfcPacketPopup) {
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase,
                               "[PHASE] PacketPopup (10004) unite joueur — position / apparence.");
        HandlePacketPopup(bytes, nBufferSize);
    } else if (op == static_cast<std::uint16_t>(RQ_TeleportPlayer) && g_pipelineStep.load() >= 6 &&
               g_fromPreInGameResult.load() == 0) {
        HandleTeleportPlayer(bytes, nBufferSize);
    } else if (op == 1 && g_pipelineStep.load() >= 6 && g_fromPreInGameResult.load() == 0) {
        ApplyServerUnitPosition(bytes, nBufferSize);
        unsigned int sx = 0;
        unsigned int sy = 0;
        {
            std::lock_guard<std::mutex> lock(g_activePlayerMutex);
            sx = g_activePlayer.serverX;
            sy = g_activePlayer.serverY;
        }
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase,
                               "[PHASE] Reponse deplacement (1) — position serveur @ %u,%u.", sx, sy);
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

static void LogoutWorker() {
    const int step = g_pipelineStep.load();
    TeardownAndDestroyComm(step, "quit monde / app (arriere-plan)");
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
    {
        std::lock_guard<std::mutex> lock(g_sessionMutex);
        if (!g_comm || g_pipelineStep.load() < 2) {
            return;
        }
    }
    g_logoutInProgress.store(true);
    g_logoutThread = std::thread(LogoutWorker);
    T4CNetworkDebugLogKind(T4CMatrixLogKind::Net,
                           "[NET] SafePlug en arriere-plan (~15 s) : retour login immediate (Esc).");
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
                break;
            }
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
    g_waitingFromPreInGame.store(false);
    g_pendingPost13Pipeline.store(false);
    g_fromPreInGameResult.store(-1);
    g_enterWorldSpawn = {};
    g_playerPopupPending.store(false);
    g_playerTeleportPending.store(false);
    g_pendingTeleport = {};
    {
        std::lock_guard<std::mutex> lock(g_activePlayerMutex);
        g_activePlayer = {};
    }
    {
        std::lock_guard<std::mutex> lock(g_characterMutex);
        g_characterList.clear();
    }
    {
        std::lock_guard<std::mutex> lock(g_putPlayerErrorMutex);
        g_putPlayerErrorMessage.clear();
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

bool T4CLoginSessionConsumeEnterWorldReady(T4CEnterWorldSpawn *) {
    return false;
}

void T4CLoginSessionGetActivePlayer(T4CActivePlayer *) {}

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

const char *T4CPlayerSpriteNpcName(const T4CActivePlayer &) {
    return "Warrio";
}

bool T4CLoginSessionConsumeNetworkSuccessDialog() {
    return false;
}

#endif
