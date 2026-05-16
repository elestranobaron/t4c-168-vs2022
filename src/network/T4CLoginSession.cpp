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

    /* Version client : le serveur RC Linux a le test « version == serveur » neutralisé (if(1)).
     * Valeur 168 alignée sur la branche 1.68 du client d’origine (ajuster si le serveur impose autre chose). */
    constexpr std::uint16_t kClientVersionHi = 168;
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

static void __cdecl CommReadCallback(sockaddr_in /*sockAddr*/, LPBYTE lpbBuffer, int nBufferSize) {
    if (!lpbBuffer || nBufferSize <= 0) {
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Warn, "[UDP] <- (empty application payload)");
        return;
    }

    const auto *bytes = reinterpret_cast<const unsigned char *>(lpbBuffer);
    LogPayloadHexNet(bytes, nBufferSize, 24);

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
                              "[AUTH] Etape serveur : resultat controle version client (RQ_AuthenticateServerVersion).");
    } else if (op == static_cast<std::uint16_t>(RQ_MessageOfDay)) {
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Auth, "[AUTH] Message du jour (RQ_MessageOfDay).");
    } else if (op == static_cast<std::uint16_t>(RQ_InfoMessage) ||
               op == static_cast<std::uint16_t>(RQ_ServerMessage)) {
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Auth, "[AUTH] Message serveur (info/texte).");
    } else if (op == static_cast<std::uint16_t>(RQ_RegisterAccount)) {
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Warn,
                               "[AUTH] Opcode 14 (RQ_RegisterAccount) en reception : souvent echo / "
                               "erreur protocolaire cote client (attendre plutot 26, 99, 65...).");
    }

    if (op == static_cast<std::uint16_t>(RQ_GetPersonnalPClist)) {
        T4CNetworkDebugLogKind(T4CMatrixLogKind::Phase,
                               "[PHASE] Liste des personnages (RQ_GetPersonnalPClist) : compte accepte, "
                               "prochaine etape serait chargement monde / rendu (non porte SDL3 ici).");
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION,
                        "[NETWORK SUCCESS] CONNEXION AU SERVEUR REUSSIE ! (RQ_GetPersonnalPClist / liste persos)");
        if (!g_successAlreadyShown.exchange(true)) {
            g_pendingSuccessDialog.store(true);
        }
    }
}

static void ShutdownUnlocked() {
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
                            "[AUTH] Envoi RQ_RegisterAccount (%zu octets TFC), login_len=%zu (mot de passe non loggue).",
                            payload.size(), login.size());
    T4CNetworkDebugLogKind(
        T4CMatrixLogKind::Phase,
        "[PHASE] Pipeline client Linux : 1) UDP+S OK 2) ce paquet cree le compte/session 3) le serveur "
        "enverra d'autres RQ (version, MOTD...) 4) RQ_GetPersonnalPClist = pret pour le jeu (arret avant SDL3 monde).");

    g_comm->SendPacket(server, const_cast<LPBYTE>(payload.data()), static_cast<int>(payload.size()), 0, 0);

    T4CNetworkDebugLog("[UDP] -> file d'envoi : paquet register (%zu octets applicatifs).", payload.size());
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
