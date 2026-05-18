# Changelog — T4C Client 1.68 RC14h (port Linux/SDL3)

Historique des modifications du client sous `#ifdef LINUX_PORT` et de la documentation associée.

---

## 2026-05-18 — Pipeline auth → sélection perso → entrée en jeu (opcodes 14/99/26/13/46/60)

### Contexte

Objectif : valider le flux réseau complet jusqu’à l’ouverture de `GameWorldScreen` contre le serveur Final Step Linux, sans casser le build Windows/DirectX (VS2022).

Tests de référence : compte `test` / perso `TestPlayer`, serveur `T4C_Server_Linux_Final_Step` avec correctifs de chargement perso (commit serveur associé).

**Limites connues au moment de ce commit :**

- Le handler serveur **opcode 46** (`RQ_FromPreInGameToInGame`) peut encore bloquer le thread UDP et provoquer un timeout connexion après un 13 réussi — correctif serveur à suivre.
- La vue monde dépend de **`T4C_DATA`** (assets convertis) ; `client_graphical_path_to_follow/` reste un labo offline, pas le chemin runtime.
- Pas encore de mouvement réseau (opcodes 1–8, 69, 16) ni d’audio SDL3.

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
