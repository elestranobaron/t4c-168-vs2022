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

static std::atomic<bool> g_pendingSuccessDialog{false};
static std::atomic<bool> g_successAlreadyShown{false};

/** Cible UDP du login (copiee au Start pour reponses depuis le thread d’analyse). */
static sockaddr_in g_serverAddr{};
/** 0 inactif, 1 en attente reponse RQ_RegisterAccount (14), 2 en attente RQ_AuthenticateServerVersion (99), 3 pipeline auth terminee ([FINAL STEP]). */
static std::atomic<int> g_pipelineStep{0};

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

static std::uint16_t ReadOpcodeFromTfcpayload(const unsigned char *data, int len) {
    if (!data || len < 6) {
        return 0;
    }
    const unsigned hi = data[4];
    const unsigned lo = data[5];
    return static_cast<std::uint16_t>((hi << 8) | lo);
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
            } else {
                T4CNetworkDebugLogKind(T4CMatrixLogKind::Warn,
                                       "[AUTH] Longueur message incoherente : msgLen=%d, len paquet=%d (attendu >= %d).",
                                       msgLen, len, textStart + msgLen);
            }
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
        }
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase,
                               "[PHASE] 5/5 — **Pipeline auth termine** (equivalent `DlgState=6` / entree jeu cote client 1.68). "
                               "Etapes suivantes jeu : persos (**RQ_GetPersonnalPClist = 26**), chargement monde — **non executes** dans ce port.");
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION,
                        "[FINAL STEP] Authentification version serveur OK — arret volontaire avant monde SDL3 / jeu.");
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase,
                               "[FINAL STEP] Authentification version serveur OK — pas de lancement jeu / monde.");
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
    }

    if (op == static_cast<std::uint16_t>(RQ_GetPersonnalPClist)) {
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase,
                               "[PHASE] Liste des personnages (RQ_GetPersonnalPClist **opcode 26**) : "
                               "prochaine etape serait chargement monde (non porte SDL3 ici).");
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION,
                        "[NETWORK SUCCESS] CONNEXION AU SERVEUR REUSSIE ! (RQ_GetPersonnalPClist / liste persos)");
        if (!g_successAlreadyShown.exchange(true)) {
            g_pendingSuccessDialog.store(true);
        }
    }
}

static void ShutdownUnlocked() {
    g_pipelineStep.store(0);
    g_comm.reset();
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
    std::lock_guard<std::mutex> lock(g_sessionMutex);

    T4CNetworkDebugBeginSession();
    g_successAlreadyShown.store(false);
    g_pendingSuccessDialog.store(false);

    ShutdownUnlocked();

    const ParsedEndpoint ep = BuildEndpoint(hostField, portField);
    if (ep.host.empty()) {
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Warn, "[NET] ERROR: empty host/IP.");
        return false;
    }

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

    sockaddr_in server{};
    if (!ResolveServerAddr(ep, &server)) {
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Warn, "[NET] ERROR: could not resolve host \"%s\".", ep.host.c_str());
        g_comm.reset();
        return false;
    }

    T4CNetworkDebugLog("[NET] Server endpoint resolved, host=\"%s\" UDP port %u (default %u if port field empty/invalid).",
                      ep.host.c_str(), static_cast<unsigned>(ep.port), static_cast<unsigned>(kDefaultServerPort));

    const std::vector<unsigned char> payload = BuildRegisterAccountPacket(login, password);
    T4CNetworkDebugLogKind(T4CMatrixLogKind::Auth,
                            "[AUTH] Envoi RQ_RegisterAccount (%zu octets TFC), login_len=%zu pwd_len=%zu "
                            "(mot de passe non loggue — la taille TFC = 4 en-tete + 2 opcode + 1+login + 1+pwd + 4 version).",
                            payload.size(), login.size(), password.size());
    T4CNetworkDebugLogKind(
        T4CMatrixLogKind::Phase,
        "[PHASE] Pipeline (ordre client 1.68 / serveur RC) : "
        "**1/5** envoi RQ_RegisterAccount (14) — **2/5** reponse 14 + octet 0/!=0 — "
        "**3/5** envoi RQ_AuthenticateServerVersion (99) + long version — **4/5** reponse 99 + long 1=OK — "
        "**5/5** [FINAL STEP] ici (sans monde). Ensuite jeu : **26** liste persos, **65** query version serveur, MOTD, etc.");

    g_comm->SendPacket(server, const_cast<LPBYTE>(payload.data()), static_cast<int>(payload.size()), 0, 0);

    g_serverAddr = server;
    g_pipelineStep.store(1);

    T4CNetworkDebugLog("[UDP] -> file d'envoi : paquet register (%zu octets applicatifs).", payload.size());
    T4CNetworkDebugLogKind(
        T4CMatrixLogKind::Phase,
        "[NET] **Etape courante : 1/5 envoyee.** Le client ne « lance » rien apres : le thread "
        "`UDPReceiveDataThread` est bloque dans **recvfrom()** sur le port local (voir InitSocket). "
        "La suite (DecryptS, [AUTH], [FINAL STEP]) n'apparait **qu'apres** une ligne `[UDP] <- recvfrom raw ...` "
        "— donc reception d'un datagramme depuis %s:%u. Si rien ne vient : serveur arrete, mauvais port, "
        "pare-feu UDP, ou le serveur ne repond pas a ce register.",
        ep.host.c_str(), static_cast<unsigned>(ep.port));
    return true;
}

void T4CLoginSessionShutdown() {
    std::lock_guard<std::mutex> lock(g_sessionMutex);
    ShutdownUnlocked();
}

bool T4CLoginSessionConsumeNetworkSuccessDialog() {
    return g_pendingSuccessDialog.exchange(false);
}

#else

#include "network/T4CLoginSession.h"

bool T4CLoginSessionStart(const std::string &, const std::string &, const std::string &, const std::string &) {
    return false;
}

void T4CLoginSessionShutdown() {}

bool T4CLoginSessionConsumeNetworkSuccessDialog() {
    return false;
}

#endif
