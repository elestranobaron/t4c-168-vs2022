# Changelog — T4C Client 1.68 RC14h (port Linux/SDL3)

Historique des modifications du client sous `#ifdef LINUX_PORT` et de la documentation associée.

---

## 2026-05-18 — Pipeline auth → sélection perso → entrée en jeu (opcodes 14/99/26/13/46/60)

### Contexte

Objectif : valider le flux réseau complet jusqu’à l’ouverture de `GameWorldScreen` contre le serveur Final Step Linux, sans casser le build Windows/DirectX (VS2022).

Tests de référence : compte `test` / perso `TestPlayer`, serveur `T4C_Server_Linux_Final_Step` avec correctifs de chargement perso (commit serveur associé).

**Limites connues au moment de ce commit (mises à jour le 2026-05-19) :**

- ~~Blocage serveur opcode **46**~~ — résolu côté serveur (mutex récursif + async 46) ; voir entrée **2026-05-19** ci-dessous.
- La vue monde dépend de **`T4C_DATA`** (assets convertis) ; `client_graphical_path_to_follow/` reste un labo offline, pas le chemin runtime.
- Pas encore de mouvement réseau (opcodes 1–8, 69, 16) ni d’audio SDL3 ; paquets post-46 (43, 60, 131…) logués mais non traités pour le gameplay.

---

### Fichiers modifiés

#### `CMakeLists.txt`

- Ajout de `src/gui/CharacterSelectScreen.cpp` à la cible `t4c_client`.

#### `src/gui/CharacterSelectScreen.cpp` / `src/gui/CharacterSelectScreen.h` (nouveaux)

- Écran SDL3 800×600 après auth : liste des personnages issue de `T4CLoginSession`.
- Navigation clavier (haut/bas), Entrée pour envoyer **opcode 13** (`RQ_PutPlayerInGame`).
- `statusLocked_` : le message de statut n’est plus écrasé à chaque frame une fois l’envoi 13 lancé.
- Debounce / garde contre double envoi 13.
- Appel à `T4CLoginSessionPollBackgroundTasks()` dans `Update()`.
- Esc → retour login via `ShouldStay()`.

#### `src/network/T4CLoginSession.cpp` / `src/network/T4CLoginSession.h`

- Machine d’états pipeline documentée : étapes 0–6 (`g_pipelineStep`) — 14 → 99 → 26 → 13 → 46 + 60.
- Parsing **opcode 103** (liste persos) et remplissage de `T4CCharacterSlot`.
- Handler dédié **opcode 13** (`0x000D` `RQ_PutPlayerInGame`) — distinct de **0x0013** ViewEquiped ; libellés de debug clarifiés.
- Après 13 OK : extraction position `(x, y, world)` → `T4CEnterWorldSpawn` ; envoi automatique **46** et **60**.
- Réception opcode **18** (ViewBackpack) loguée pendant la transition.
- Timeout attente réponse 13 (60 s) avec message d’erreur utilisateur.
- Gestion refus serveur (compte déjà connecté, codes erreur 13).
- Cooldown reconnexion post-logout / « already logged » (~30 s).
- Thread logout asynchrone pour libérer la session serveur sans bloquer l’UI.
- `T4CLoginSessionConsumeEnterWorldReady()` pour basculer vers le monde depuis `main.cpp`.

#### `src/main.cpp` (branche `LINUX_PORT`)

- Nouvelle phase `AppPhase::CharacterSelect` entre login et monde.
- Sur 13 OK : tentative `GameWorldScreen::Init` avec coords du spawn ; message SDL si `T4C_DATA` manquant (13 peut être OK quand même).
- Boucle monde : `world.Update()` + `SDL_Delay(5)` (rendu seul, pas encore de logique mouvement réseau).

#### `src/game/GameWorldScreen.cpp` / `src/game/GameWorldScreen.h`

- `Init(renderer, window, locX, locY, zone)` : position issue du paquet 13.
- Affichage overlay « Loc x,y Z | reseau OK | FPS ».
- Chemins données via `ResolveT4CDataRoot()` / `T4C_DATA` (pas de chemin codé en dur vers `client_graphical_path_to_follow` dans le flux nominal).

#### `README.md`

- Priorité **Linux/SDL3** ; Windows = référence comportementale, build VS2022 préservé.
- Sections **deux dimensions** (A réseau / B rendu), **contrat `T4C_DATA`**, parité Windows↔Linux.
- Roadmap réorganisée : Phase 1 (connexion) largement ✓ ; Phase 2 (rendu + monde réseau) = priorité.
- Précisions WDA (serveur uniquement), BMP Storm vs `.rmap` client, `makewda.py` vs Havoc.

---

### Non inclus dans ce commit (fichiers non suivis ou hors périmètre)

| Chemin | Raison |
|--------|--------|
| `key_swaps/` | Outils WDA XOR — commit séparé recommandé |
| `second_approach/` | Pipeline LP64 WDA — commit séparé |
| `debug/t4c_network_session.log` | Log local de test |

---

## Modèle pour les entrées futures

```markdown
## YYYY-MM-DD — Titre court

### Contexte
…

### Fichiers modifiés
#### `src/…`
…
```

## 2026-05-19 — Pipeline 13→18→46+60 validé (in-game réseau)

### Contexte

Tests compte `test` / perso `TestPlayer` contre `T4C_Server_Linux_Final_Step` (binaire post-`recursive_mutex`). Le client recevait **13** OK mais restait bloqué sans `[PHASE] Reponse … (46)` tant que le serveur mourait dans `PutPlayerInGame` (double `Lock()` monde) sur le thread UDP.

**Résultat validé :** après sélection perso, réception **18** (ViewBackpack) → envoi **46** + **60** → `[PHASE] Reponse RQ_FromPreInGameToInGame (46) code=0` ; rafale serveur **43** / **60** / **131** ; `GameWorldScreen` charge les sprites `.dec`.

### Fichiers modifiés

#### `src/network/T4CLoginSession.cpp`

- **`g_pendingPost13Pipeline`** : ne plus envoyer **46**/**60** immédiatement après **13** ; attendre **opcode 18** (fin chargement serveur, aligné Windows `packethandling.cpp` / `boPreInGame`).
- Handler **18** : envoi **46** puis **60** une seule fois ; log phase explicite.
- Reset session : clear `g_pendingPost13Pipeline`.
- **Opcode 18** retiré de la liste « paquets pendant attente 13 » (ce n’est pas une réponse au 13).

### Dépendance serveur (commit séparé, repo `T4C_Server_Linux_Final_Step`)

- `Lock.h` : `std::recursive_mutex` sur Linux (locks imbriqués `create_world_unit` → `deposit_unit`).
- `TFCMessagesHandler.cpp` : **46** toujours async + `ReleasePicklockEarly` avant `FinishFromPreInGameToInGame`.

### Prochaine priorité client

Handlers monde : opcodes **1–8** (mouvement), **16** (objets périphériques), **43** (stats HUD) — référence `CLIENT168_RC14h_OK/.../packethandling.cpp`.

---

## [2026-05-19] - Améliorations Assets & Pipeline Réseau Réduite
### Ajouté
- **Système d'Assets Local (`client/data/`)** : Migration complète vers un répertoire de données unifié (`sprites/`, `maps/`, `sons/`, `fonts/`, `NPCList.txt`) supprimant la dépendance absolue envers les liens externes `client_graphical_path_to_follow`.
- **Script d'automatisation (`scripts/assemble_t4c_data.sh`)** : Permet la régénération et la copie automatique du dossier `data/` depuis l'environnement de développement graphique source.
- **Documentation locale (`data/README.md`)** : Ajout du contrat d'utilisation et de la structure requise pour le répertoire de données client.

### Modifié
- **Build & CMakeLists** : Génération automatisée d'un lien symbolique `build/data` pointant vers `${CMAKE_SOURCE_DIR}/data`. Plus aucune copie brute des polices ou de `NPCList.txt` n'est effectuée à côté du binaire.
- **Détection des Dépendances (`cmake/TncGraphical.cmake`)** : Détection dynamique et transparente de `TnC_dev` (recherche du chemin graphique en priorité avant le fallback de test).
- **GameWorldScreen & TncDataPaths** : Utilisation stricte de la macro-fonction `T4CDataPath()` pour localiser les ressources. Nettoyage des doubles slashes (`//`) dans la construction des chemins de sprites.
- **Boucle Principale (`main.cpp`)** : Intégration et exécution continue de `T4CLoginSessionPollBackgroundTasks()` en tâche de fond une fois la phase Monde active.
- **Suivi de Phase Réseau (`T4CLoginSession`)** : Intégration de verrous logiques (`g_waitingFromPreInGame`, `GetWorldHudLine()`) pour tracer précisément l'envoi de l'Opcode 46 et intercepter ses codes de retour. Log distinct pour l'opcode **18** (ViewBackpack après 13, pas réponse au 46).

### Sécurité
- Mise à jour du `.gitignore` pour ignorer explicitement les volumineux sous-dossiers de données (`sprites/`, `maps/`, `sons/`) afin d'éviter tout commit accidentel d'assets volumineux (~325 Mo).
