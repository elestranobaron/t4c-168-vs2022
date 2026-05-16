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

/** À appeler depuis le thread principal : retourne true une fois si le serveur a envoyé la liste perso (RQ 26). */
bool T4CLoginSessionConsumeNetworkSuccessDialog();
