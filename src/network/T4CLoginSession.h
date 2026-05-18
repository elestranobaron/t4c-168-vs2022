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

/** @deprecated Utiliser ConsumeCharacterListReady + flux selection. */
bool T4CLoginSessionConsumeNetworkSuccessDialog();
