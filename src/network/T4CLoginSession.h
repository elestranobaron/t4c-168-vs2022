#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct T4CCharacterSlot {
    std::string name;
    std::uint16_t race{0};
    std::uint16_t level{0};
};

struct T4CEnterWorldSpawn {
    unsigned int x{2880};
    unsigned int y{1083};
    unsigned short world{0};
    bool valid{false};
};

/** Perso actif (selection + sync serveur PacketPopup type 10004). */
struct T4CActivePlayer {
    std::string name;
    std::uint16_t race{0};
    /** Niveau connu (opcode 26 a la selection ; maj future opcode 43). */
    std::uint16_t level{0};
    /** ID apparence serveur (10001–10004 mâle, 15001–15004 femelle). 0 = déduire de race. */
    std::uint16_t appearance{0};
    unsigned int serverX{0};
    unsigned int serverY{0};
    std::int32_t unitId{0};
    bool female{false};
    bool valid{false};
};

/** Nom NPCList / spr_pal pour le sprite PC (ex. « Thief », « Warrio »). */
const char *T4CPlayerSpriteNpcName(const T4CActivePlayer &player);

/**
 * Démarre la pile CCommCenter (UDP éphémère local, envoi vers host + port UDP).
 * hostField : IPv4 ou nom d'hôte (suffixe :port optionnel ignoré au profit de portField).
 * portField : chaîne décimale 1–65535 ; vide ou invalide → port par défaut T4C 11677.
 */
bool T4CLoginSessionStart(const std::string &hostField, const std::string &portField, const std::string &login,
                          const std::string &password);

void T4CLoginSessionShutdown();

/** Déconnexion serveur non bloquante (SafePlug ~15 s en arrière-plan) ; l’UI peut se fermer tout de suite. */
void T4CLoginSessionDisconnectInGame();

/** True si une session UDP est active ou un logout SafePlug est encore en cours. */
bool T4CLoginSessionIsNetworkActive();

/** True pendant le decompte SafePlug (~15 s) apres Esc depuis le monde. */
bool T4CLoginSessionIsLogoutInProgress();

/** Secondes restantes avant qu'un nouveau Connect soit accepte (0 = OK). */
int T4CLoginSessionGetReconnectCooldownSeconds();

/** A appeler depuis la boucle login (rejoint le thread logout termine). */
void T4CLoginSessionPollBackgroundTasks();

/** Ligne HUD monde (etat opcode 46, rappel deplacement local). */
std::string T4CLoginSessionGetWorldHudLine();

/** Annule retries « already logged » et ferme la session UDP (nouveau Connect). */
void T4CLoginSessionAbortLogin();

/** Après retour login depuis le monde : permet une nouvelle entrée en carte. */
void T4CLoginSessionResetAfterReturnToLogin();

/** True une fois quand la liste persos (26) a ete recue. */
bool T4CLoginSessionConsumeCharacterListReady();

/** Copie la liste persos parsee (thread-safe). */
void T4CLoginSessionCopyCharacterList(std::vector<T4CCharacterSlot> *outSlots, int *outMaxPerAccount);

/** Envoie RQ_PutPlayerInGame (13) avec le nom choisi (une seule requete en vol). */
bool T4CLoginSessionRequestPutPlayerInGame(const std::string &playerName);

bool T4CLoginSessionIsWaitingPutPlayerInGame();

bool T4CLoginSessionHasPutPlayerInGameError();

/** Message d'erreur serveur (opcode 13 court) ; vide si aucune erreur. */
std::string T4CLoginSessionGetPutPlayerInGameErrorMessage();

void T4CLoginSessionClearPutPlayerInGameError();

/** True une fois quand opcode 13 OK + 46/60 envoyes — spawn serveur dans *outSpawn. */
bool T4CLoginSessionConsumeEnterWorldReady(T4CEnterWorldSpawn *outSpawn);

/** Copie le perso choisi (race, nom) + dernier PacketPopup 10004 si reçu. */
void T4CLoginSessionGetActivePlayer(T4CActivePlayer *outPlayer);

/** True une fois par nouveau PacketPopup 10004 (position / apparence serveur). */
bool T4CLoginSessionConsumePlayerPopupUpdate(T4CActivePlayer *outPlayer);

/** Position apres RQ_TeleportPlayer (57). */
struct T4CPlayerTeleport {
    unsigned int x{0};
    unsigned int y{0};
    unsigned short world{0};
};

bool T4CLoginSessionConsumePlayerTeleport(T4CPlayerTeleport *outTeleport);

/** Envoie un déplacement (opcodes 1–8, aligné TFCSocket.cpp). Retourne false si pas en jeu. */
bool T4CLoginSessionSendMove(std::uint16_t moveOpcode);

/** Met a jour les coords affichees du perso actif (apres mouvement local). */
void T4CLoginSessionUpdateActivePlayerPosition(unsigned int x, unsigned int y);

/** @deprecated Utiliser ConsumeCharacterListReady + flux selection. */
bool T4CLoginSessionConsumeNetworkSuccessDialog();
