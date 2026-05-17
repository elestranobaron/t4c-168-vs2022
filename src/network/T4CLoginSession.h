#pragma once

#include <string>

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

/** Annule retries « already logged » et ferme la session UDP (nouveau Connect). */
void T4CLoginSessionAbortLogin();

/** Après retour login depuis le monde : permet une nouvelle entrée en carte. */
void T4CLoginSessionResetAfterReturnToLogin();

/** À appeler depuis le thread principal : retourne true une fois si le serveur a envoyé la liste perso (RQ 26). */
bool T4CLoginSessionConsumeNetworkSuccessDialog();
