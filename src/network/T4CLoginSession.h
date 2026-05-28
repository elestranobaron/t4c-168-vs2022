#pragma once

#include "network/T4CPlayerInventory.h"

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

/** Stats combat / feuille perso (opcode 43 Character::PacketStatus, aligne Packet.cpp RQ_GetStatus). */
struct T4CPlayerStatus {
    unsigned int hp{0};
    unsigned int maxHp{0};
    unsigned short mana{0};
    unsigned short maxMana{0};
    std::uint16_t level{0};
    unsigned short ac{0};
    unsigned short str{0};
    unsigned short end{0};
    unsigned short agi{0};
    unsigned short wis{0};
    unsigned short intel{0};
    unsigned short weight{0};
    unsigned short maxWeight{0};
    /** XP totale (opcode 43 / 44). */
    std::uint64_t xp{0};
    /** Seuil XP prochain niveau (opcode 37, Exp2Go Windows). */
    std::uint64_t xpToNextLevel{0};
    bool valid{false};
};

/** Stats affichees apres opcode 25/31 (Character::packet_stats, aligne TFCSocket.cpp). */
struct T4CCharacterRolledStats {
    unsigned char agi{0};
    unsigned char end{0};
    unsigned char intel{0};
    unsigned char luck{0};
    unsigned char str{0};
    unsigned char wil{0};
    unsigned char wis{0};
    unsigned int maxHp{0};
    unsigned int hp{0};
    unsigned short maxMana{0};
    unsigned short mana{0};
    bool valid{false};
};

/** Perso actif (selection + sync serveur PacketPopup type 10004). */
struct T4CActivePlayer {
    std::string name;
    std::uint16_t race{0};
    /** Niveau connu (opcode 26 ; maj opcode 43 / 37). */
    std::uint16_t level{0};
    /** ID apparence serveur (10001–10004 mâle, 15001–15004 femelle, 10011/10012 puppet). */
    std::uint16_t appearance{0};
    /** Classe 0–3 (Warrio/Wizard/Cleric/Thief) — questionnaire creation ou race 10001–10004. */
    std::uint8_t classIndex{0};
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

/** Déconnexion serveur : SafePlug+ExitGame immédiat, fermeture UDP en arrière-plan. */
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

/** Envoie RQ_CreatePlayer (25). stats[5] = reponses questionnaire (V1: defauts fixes). sex: 0=homme, 1=femme. */
bool T4CLoginSessionRequestCreatePlayer(const std::string &name, unsigned char sex,
                                        const unsigned char stats[5]);

bool T4CLoginSessionIsWaitingCreatePlayer();

bool T4CLoginSessionHasCreatePlayerError();

std::string T4CLoginSessionGetCreatePlayerErrorMessage();

void T4CLoginSessionClearCreatePlayerError();

/** Reinitialise l'etat client creation/reroll (ecran creation ouvert). */
void T4CLoginSessionPrepareForCreateScreen();

/** Envoie RQ_QueryNameExistence (90) — validation nom cote serveur (aligne Windows). */
bool T4CLoginSessionRequestQueryNameExistence(const std::string &name);

/** True une fois quand le joueur a valide l'ecran reroll (liste 26 demandee). */
bool T4CLoginSessionConsumeCreatePlayerSuccess();

/** True pendant l'ecran reroll apres opcode 25 OK (perso cree, stats modifiables). */
bool T4CLoginSessionIsInCreateRerollPhase();

/** True une fois quand de nouvelles stats arrivent (opcode 25 ou 31). */
bool T4CLoginSessionConsumeRolledStatsUpdate(T4CCharacterRolledStats *outStats);

/** Envoie RQ_Reroll (31) — relance les des cote serveur. */
bool T4CLoginSessionRequestCreateReroll();

/** Valide les stats courantes : refresh liste 26 puis entree en monde (aligne Windows). */
bool T4CLoginSessionConfirmCreateReroll();

/** Annule la creation : opcode 15 sur le perso provisoire, retour selection. */
bool T4CLoginSessionCancelCreateReroll();

/** Envoie RQ_DeletePlayer (15) puis refresh RQ_GetPersonnalPClist (26). */
bool T4CLoginSessionRequestDeletePlayer(const std::string &playerName);

bool T4CLoginSessionIsWaitingDeletePlayer();

bool T4CLoginSessionHasDeletePlayerError();

std::string T4CLoginSessionGetDeletePlayerErrorMessage();

void T4CLoginSessionClearDeletePlayerError();

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

/** Dernier opcode 43 / maj partielle 33·67 (thread-safe). */
void T4CLoginSessionGetPlayerStatus(T4CPlayerStatus *outStatus);

/** True une fois quand HP/mana/stats viennent d'etre mis a jour (opcode 43, 33 ou 67). */
bool T4CLoginSessionConsumePlayerStatusUpdate(T4CPlayerStatus *outStatus);

/** Demande explicite RQ_GetStatus (43) — refresh cote serveur. */
bool T4CLoginSessionRequestPlayerStatus();

/** Inventaire / skills / sorts / coffre banque (opcodes 18, 39, 62, 106). */
void T4CLoginSessionGetBackpack(T4CPlayerBackpack *out);
void T4CLoginSessionGetSkillBook(T4CPlayerSkillBook *out);
void T4CLoginSessionGetSpellBook(T4CPlayerSpellBook *out);
void T4CLoginSessionGetBankChest(T4CPlayerBankChest *out);
void T4CLoginSessionGetEquipment(T4CPlayerEquipment *out);

/** True une fois apres maj inventaire, skills, sorts ou coffre. */
bool T4CLoginSessionConsumeInventoryUpdate();

bool T4CLoginSessionRequestSkillList();
bool T4CLoginSessionRequestSpellList();
bool T4CLoginSessionRequestViewBackpack();
bool T4CLoginSessionRequestViewEquipped();

/**
 * Demande les noms manquants (opcode 59) pour le sac ou le coffre banque.
 * A appeler depuis la boucle monde (limite interne par frame).
 */
void T4CLoginSessionPollItemNameRequests(T4CItemSearchPlace place, int maxPerTick);

/** Coffre banque visible (opcode 109/110). */
bool T4CLoginSessionIsBankChestUiVisible();

/** True une fois par nouveau PacketPopup 10004 (position / apparence serveur). */
bool T4CLoginSessionConsumePlayerPopupUpdate(T4CActivePlayer *outPlayer);

/** Position apres RQ_TeleportPlayer (57). */
struct T4CPlayerTeleport {
    unsigned int x{0};
    unsigned int y{0};
    unsigned short world{0};
};

bool T4CLoginSessionConsumePlayerTeleport(T4CPlayerTeleport *outTeleport);

/** Demande les unites peripheriques (RQ_GetNearItems 60 → reponse serveur opcode 16). */
bool T4CLoginSessionRequestNearItems();

/** Envoie un déplacement (opcodes 1–8, aligné TFCSocket.cpp). Retourne false si pas en jeu. */
bool T4CLoginSessionSendMove(std::uint16_t moveOpcode);

/** Met a jour les coords affichees du perso actif (apres mouvement local). */
void T4CLoginSessionUpdateActivePlayerPosition(unsigned int x, unsigned int y);

/** Evenement reseau → rendu : spawn / deplacement / maj stats / despawn d'une unite distante. */
enum class T4CRemoteUnitEventKind : std::uint8_t {
    Spawn,
    Move,
    Update,
    Remove,
};

struct T4CRemoteUnitEvent {
    T4CRemoteUnitEventKind kind{T4CRemoteUnitEventKind::Spawn};
    std::int32_t unitId{0};
    std::uint16_t appearance{0};
    unsigned int x{0};
    unsigned int y{0};
    char hpPercent{0};
};

/** Marqueur objet sol (portes/coffres/objets) extrait des opcodes 16/1. */
struct T4CGroundObjectMarker {
    std::int32_t unitId{0};
    std::uint16_t appearance{0};
    unsigned int x{0};
    unsigned int y{0};
};

/** Vide la file d'evenements unites distantes (thread-safe, depuis la boucle GameWorld). */
void T4CLoginSessionDrainRemoteUnitEvents(std::vector<T4CRemoteUnitEvent> *outEvents);

/** Nom NPCList / VSF pour une apparence serveur (20006 → BlackWarrior, etc.). */
const char *T4CSpriteNameFromAppearance(std::uint16_t appearance);

/** Reinitialise la file (logout, teleport, retour login). */
void T4CLoginSessionClearRemoteUnits();
void T4CLoginSessionCopyGroundObjectMarkers(std::vector<T4CGroundObjectMarker> *outMarkers);

/** @deprecated Utiliser ConsumeCharacterListReady + flux selection. */
bool T4CLoginSessionConsumeNetworkSuccessDialog();
