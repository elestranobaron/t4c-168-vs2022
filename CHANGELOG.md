# Changelog — T4C Client 1.68 RC14h (port Linux/SDL3)

Historique des modifications du client sous `#ifdef LINUX_PORT` et de la documentation associée.

---

## 2026-05-19 — Vue monde SDL3 native, launcher graphique, HUD joueur, menu pause — fin du rendu sombre

### Contexte — ce qui a été enduré

Après l’entrée en jeu (pipeline 14→99→26→13→46→60, puis déplacement local `63fe2e2`, téléport opcode 57 `d445a41`), la **vue monde** restait difficile à valider visuellement :

| Symptôme | Cause identifiée |
|----------|------------------|
| **Carte noire** après login | Renderer SDL3 **partagé** entre launcher (TTF, bandeau) et monde : `clip rect` / `color scale` laissés actifs ; une frame de sélection perso dessinée après bascule phase `World`. |
| **Carte sombre / délavée** | Contournement `SDL_SetRenderColorScale` sur le renderer (polluait login) puis `SDL_SetTextureColorModFloat` à **1.6×** — masquait le problème sans le résoudre. |
| **Confusion dossiers graphiques** | Cache CMake `TNC_GRAPHICAL_ROOT` pointant parfois vers `client_graphical_path_to_follow` (labo mestoph SDL2) au lieu de `client_graphical_sdl3_test/TnC_dev` ; deux copies du shim SDL2→SDL3 divergentes. |
| **Rendu « sombre et dégueulasse »** (post-migration SDL3) | Palettes `.dec` chargées **sans canal alpha** (`SDL_Color.a` non initialisé → 0) ; blits index8→RGBA32 en SDL3 produisaient des pixels **transparents** ; layer sol effacé en transparent ; hack luminosité insuffisant. |

**Résultat utilisateur avant fix palette :** fond noir dominant, tuiles fantômes, couleurs T4C (`Bright1`, etc.) absentes malgré F4/F5.

**Résultat après fix (validation utilisateur) :** rendu couleurs correct, luminosité par défaut **1.0** — *« alleluia, c’est vraiment perfect »*.

---

### Ce qui a été fait

#### A. Données joueur actif (HUD)

- Champ **`level`** ajouté à `T4CActivePlayer` (`T4CLoginSession.h`).
- Copie depuis le slot personnage (opcode **26**) dans `RequestPutPlayerInGame`.
- Affichage in-game : `Nom niv X | coords | zone | lum | FPS` dans `GameWorldScreen::redraw()`.

#### B. Menu pause (Esc) — plus de quit direct

- Nouveau **`WorldSideMenu`** (`src/gui/WorldSideMenu.cpp/.h`) : sprites T4C `64kSideBox`, `64kSideButton*`.
- **Esc** ouvre/ferme le panneau latéral (placeholder panels).
- Bouton **Options** → popup Annuler / Retour login / Quitter.
- `ConsumeQuitApp()` / `ConsumeReturnToLogin()` câblés dans `main.cpp`.

#### C. Launcher graphique (login + sélection perso)

- Police T4C **`t4cbeaulieux.ttf`** extraite de `data/fonts/t4c_beaulieux.zip`.
- Nouveaux modules :
  - `T4CUiFont` — rendu TTF SDL3 ;
  - `T4CScrollingBanner` — bandeau défilant ;
  - `LauncherChrome` — fond `LoadingScreen.bmp`, police, bandeau « markshptang ».
- `LoginScreen` et `CharacterSelectScreen` utilisent ce chrome commun.
- Fichiers ajoutés au **`CMakeLists.txt`**.

#### D. Correctifs écran noir (transition login → monde)

| Fichier | Fix |
|---------|-----|
| `Sdl3FramePresenter.cpp` | Reset `SDL_SetRenderClipRect(nullptr)` + `SDL_SetRenderColorScale(1.f)` en début de `present()`. |
| `LauncherChrome.cpp` | Reset renderer après rendu bandeau TTF. |
| `main.cpp` | Reset renderer avant `world.Init` ; pas de frame CharacterSelect si phase déjà `World` ; ordre `SetLogicalPresentation` avant `Init`. |

#### E. Migration moteur TnC : SDL3 natif (suppression shim)

**Objectif :** ne plus dépendre de `third_party/tnc_sdl3/` (faux `SDL/SDL.h`, `tnc_sdl2_compat.h` ~156 lignes de macros SDL2).

| Avant | Après |
|-------|-------|
| `third_party/tnc_sdl3/include/SDL/SDL.h` → shim | `#include <SDL3/SDL.h>` direct |
| Macros `SDL_CreateRGBSurface`, `SDL_FreeSurface`, … | Helpers explicites `tnc_sdl3.h` (~80 lignes) |
| `Sdl3FramePresenter` dans `third_party/` | `client_graphical_sdl3_test/TnC_dev/render/` |
| Sources TnC + shim dupliqués | Une seule racine compile : `client_graphical_sdl3_test/TnC_dev` |

**`cmake/TncGraphical.cmake` — priorité forcée à chaque configure :**

1. `../client_graphical_sdl3_test/TnC_dev` (TnC patché SDL3, **compilé**)
2. `../client_graphical_path_to_follow/decode/TnC_dev` (fallback labo mestoph)

**Modules TnC migrés** (via symlinks `FontManager/`, `VSFInterface/`, `NPCManager/`, `TextManager/` + `MapInterface/` local dans sdl3_test) :

- `FontManager/fontmanager.cpp/.h`
- `VSFInterface/vsfinterface.h`, `vsfi_read_sprite.cpp`, `vsfi_sprites.cpp`, `vsfi_indexage_pal.cpp`
- `TextManager/textmanager.cpp`
- `NPCManager/npc_draw.cpp`, `npc_ajout.cpp`
- `MapInterface/mapi_full_redraw.cpp`, `mapi_move_map.cpp`, `mapi_get_map.cpp`

**Supprimé du dépôt client :**

```
third_party/tnc_sdl3/include/tnc_sdl2_compat.h
third_party/tnc_sdl3/include/SDL/SDL.h
third_party/tnc_sdl3/include/SDL/SDL_image.h
third_party/tnc_sdl3/render/Sdl3FramePresenter.cpp
third_party/tnc_sdl3/render/Sdl3FramePresenter.h
```

#### F. Correctif rendu couleurs (fix définitif — palette + bake RGBA)

| Problème | Correction |
|----------|------------|
| `vsfi_indexage_pal.cpp` : R,G,B lus, **`.a` jamais posé** | `np->rgb[i].a = 255` pour les 256 entrées |
| Sprites restés en INDEX8 au blit vers layers RGBA | `TnC_BakeIndexedSprite()` : conversion **RGBA32 au chargement** après palette + colorkey (`vsfi_read_sprite.cpp`) |
| Layer sol initialisé transparent | `mapi_get_map.cpp` : fill sol **`0xFF000000`** (noir opaque) |
| Zone redraw sol en `0x000000FF` (alpha=0 en ARGB) | `mapi_full_redraw.cpp` : **`0xFF000000`** |
| Luminosité hack 1.6× masquant le bug | `Sdl3FramePresenter` + `GameWorldScreen` : défaut **1.0** ; F4/F5 conserve la plage 1.0–3.0 |

**Helper central :** `client_graphical_sdl3_test/TnC_dev/include/tnc_sdl3.h`

- `TnC_MapArgb`, `TnC_CreateRgbaSurface`, `TnC_CreateIndexedSurfaceFrom`
- `TnC_FillArgb`, `TnC_SetPalette`, `TnC_SetColorKeyIndex`
- `TnC_BakeIndexedSprite`, `TnC_GetTicksMs`, `TnC_SetSurfaceAlpha`

---

### Nettoyé

- Dossier **`third_party/tnc_sdl3/`** vidé (shim obsolète).
- Doublons shim dans `client_graphical_sdl3_test/TnC_dev/include/` (`tnc_sdl2_compat.h`, faux `SDL/SDL.h`) supprimés.
- CMake : includes pointent uniquement vers `TnC_dev/include` + `TnC_dev/render` (plus de `third_party/tnc_sdl3`).
- `TncGraphical.cmake` : résolution automatique de la racine TnC à chaque configure (évite cache CMake bloqué sur mauvais dossier).

---

### Corrigé (récapitulatif)

| Zone | Fix |
|------|-----|
| Transition login → monde | Écran noir (renderer pollué) |
| Rendu carte | Couleurs palettes `.dec` + sprites opaques |
| Build | Source TnC unifiée sur `client_graphical_sdl3_test` |
| UX Esc | Menu pause au lieu de quitter |
| HUD | Niveau personnage visible |

---

### Amélioré

- **Launcher** : fond BMP + police Beaulieux + bandeau crédits (parité visuelle Windows partielle).
- **SideMenu** : base graphique sprites `.dec` (panels cliquables = placeholder).
- **Luminosité** : réglage F4/F5 sur texture finale (`SDL_SetTextureColorModFloat`) sans toucher au renderer login.
- **Architecture documentée** (dans cette entrée et conversations associées) :

| Chemin | Rôle |
|--------|------|
| `client/` | Exécutable Linux (réseau, GUI, `GameWorldScreen`) |
| `client/data/` (`T4C_DATA`) | Assets runtime `.dec`, `.rmap`, fonts, NPCList |
| `client_graphical_sdl3_test/TnC_dev/` | **Moteur mestoph compilé** dans `t4c_client` |
| `client_graphical_path_to_follow/decode/` | Pipeline offline convert2, référence — **pas** source compile par défaut |
| `CLIENT168_RC14h_OK/` | Référence Windows, jamais linké sous Linux |

---

### Fichiers modifiés — dépôt `client/`

| Fichier | Rôle |
|---------|------|
| `CMakeLists.txt` | LauncherChrome, T4CUiFont, T4CScrollingBanner, WorldSideMenu |
| `cmake/TncGraphical.cmake` | SDL3 natif, priorité sdl3_test, includes TnC_dev |
| `src/game/GameWorldScreen.cpp/.h` | HUD level, SideMenu, luminosité, tnc_sdl3, popup options |
| `src/gui/WorldSideMenu.cpp/.h` | Menu pause graphique (nouveau) |
| `src/gui/LauncherChrome.cpp/.h` | Chrome login/sélection (nouveau) |
| `src/gui/T4CUiFont.cpp/.h` | Police TTF (nouveau) |
| `src/gui/T4CScrollingBanner.cpp/.h` | Bandeau défilant (nouveau) |
| `src/gui/LoginScreen.cpp/.h` | Intégration LauncherChrome |
| `src/gui/CharacterSelectScreen.cpp/.h` | Intégration LauncherChrome |
| `src/main.cpp` | Phases, reset renderer, SideMenu/quit |
| `src/network/T4CLoginSession.cpp/.h` | `level` dans active player |
| `third_party/tnc_sdl3/**` | **Supprimé** (shim SDL2→SDL3) |

### Fichiers modifiés — moteur TnC (repo `client_graphical_path_to_follow/decode/TnC_dev/`, compilé via symlinks sdl3_test)

| Fichier | Rôle |
|---------|------|
| `FontManager/fontmanager.cpp/.h` | SDL3 + `tnc_sdl3.h` |
| `VSFInterface/vsfinterface.h` | `#include <SDL3/SDL.h>` |
| `VSFInterface/vsfi_indexage_pal.cpp` | **alpha palette = 255** |
| `VSFInterface/vsfi_read_sprite.cpp` | bake RGBA au load |
| `VSFInterface/vsfi_sprites.cpp` | `SDL_DestroySurface` |
| `TextManager/textmanager.cpp` | SDL3 helpers |
| `NPCManager/npc_draw.cpp`, `npc_ajout.cpp` | SDL3 helpers |

### Fichiers modifiés — repo `client_graphical_sdl3_test/TnC_dev/`

| Fichier | Rôle |
|---------|------|
| `include/tnc_sdl3.h` | **Nouveau** — helpers SDL3 natifs |
| `render/Sdl3FramePresenter.cpp/.h` | Présentation + reset clip/scale + luminosité texture |
| `MapInterface/mapi_*.cpp` | SDL3 natif, fills sol opaques |
| `test_mapinterface_sdl3.cpp` | SDL3_image direct, helpers |
| `CMakeLists.txt` | Includes sans faux SDL/ |

---

### Non inclus / limites restantes (volontairement)

| Élément | Statut |
|---------|--------|
| SideMenu panels complets (minimap TMI, chat, inventaire…) | Placeholder — sprites OK, logique panels non |
| Écran création personnage + opcode 25 | Non commencé |
| Musique Phase 1 (`LoadNewSound`) | Non commencé |
| Opcode 43 (level serveur autoritatif) | Level affiché = slot opcode 26 uniquement |
| Couche `env` / torche.png (jour-nuit test mestoph) | Absente du client (overlay luminosité test_mapinterface) |
| Tests legacy `test_mapinterface.cpp` / `test_npcmanager.cpp` (SDL 1.2) | Non migrés — hors build `t4c_client` |
| `build_backup/`, `key_swaps/`, `second_approach/`, `data/` non suivis | Commits séparés recommandés |

---

### Commits sœurs recommandés

Ce changelog documente un lot qui touche **trois arborescences** :

1. **`client/`** — exécutable, GUI, CMake, suppression shim
2. **`client_graphical_path_to_follow/decode/TnC_dev/`** — modules mestoph symlinkés
3. **`client_graphical_sdl3_test/TnC_dev/`** — MapInterface local, `tnc_sdl3.h`, presenter

---

## 2026-05-20 — Téléport escaliers / changement de carte (opcode 57, `RQ_TeleportPlayer`)

### Contexte

Après l’entrée en jeu et le déplacement local (commit `63fe2e2`), prendre des **escaliers** ou tout passage serveur qui change de **carte** (même `world` ou autre) envoie l’**opcode 57** (`0x0039`, `RQ_TeleportPlayer`). Sous Windows 1.68, `PacketHandling::TeleportPlayer` (`CLIENT168_RC14h_OK/.../packethandling.cpp`) traite ce paquet ; le client Linux ne faisait **rien** avec — le paquet apparaissait seulement dans les logs :

```text
[AUTH] <- opcode 57 (0x0039) (non documente client Linux), 12 bytes
```

**Symptôme utilisateur :** à l’atterrissage dans la nouvelle pièce, personnage **figé**, sol qui **défile** (« tapis roulant »), moves qui partent sans réponse serveur, ou resync incohérente entre `playerX_` / caméra / sprite.

**Trace réseau de référence (repro escaliers, 2026-05-19/20) :**

1. Rafale d’**opcodes 1** (acks move) autour de `(2988, 1074–1076)`.
2. **Opcode 57** — payload applicatif **12 octets** après en-tête TFC (4) + opcode BE (2) :

   | Offset | Contenu | Exemple |
   |--------|---------|---------|
   | 6–7 | `X` (int16 BE) | `0A 49` → 2633 |
   | 8–9 | `Y` (int16 BE) | `05 B1` → 1457 |
   | 10–11 | `WORLD` (int16 BE) | `00 00` → monde 0 |

   Exemple complet : `00 00 00 00 00 39 0A 49 05 B1 00 00`.

3. Souvent suivi d’**opcode 16** (objets périphériques) et d’un **opcode 1** à la nouvelle position.
4. Sans handler 57 : le client ne mettait pas à jour `zone_`, envoyait des moves pendant la transition, et ne reproduisait pas la séquence **60 + 46** du client Windows.

### Comportement Windows de référence (`TeleportPlayer`)

Résumé de ce que fait le client 1.68 (sans tout porter — musique, UI, objets…) :

1. `DoNotMove = TRUE` — bloque les entrées pendant la transition.
2. Lit `X`, `Y`, `WORLD` depuis le paquet 57.
3. Met à jour `Player.xPos`, `Player.yPos`, `Player.World`.
4. Recharge la carte (`LoadZoneMapWorld`, `ForceDisplayZone`).
5. Envoie **opcode 60** (`RQ_GetNearItems` / « GetNearUnits »).
6. Envoie **opcode 46** (`RQ_FromPreInGameToInGame`) — le serveur répond ; `DoNotMove = FALSE` dans `FromPreInGameToInGame`.

Le client Linux avait déjà **46 + 60** à l’**entrée initiale** en jeu (après opcode 13 / 18) ; il manquait la **même séquence après un 57**.

### Ce commit implémente (périmètre volontairement minimal)

**Principe :** handler réseau + resync rendu **uniquement**. Aucune modification de `TnC_dev` (`NPCManager`, `npc_draw`, etc.), pas de changement du flux opcode **1** (move ack), pas de file `moveAwaitingAck`, pas de luminosité, pas de `main.cpp`.

#### `src/network/T4CLoginSession.cpp` / `.h`

- **`HandleTeleportPlayer`** — appelé depuis `CommReadCallback` quand `op == RQ_TeleportPlayer` (57) et pipeline in-game (étape ≥ 6, dernier 46 OK).
- **Parsing** : `X`, `Y`, `WORLD` aux offsets 6/8/10 (int16 big-endian), comme Windows.
- **État** : remplit `g_pendingTeleport`, met à jour `g_activePlayer.serverX/Y`, pose `g_playerTeleportPending` pour le thread principal.
- **Annule** un éventuel `g_playerPopupPending` en attente (évite de traiter un vieux ack opcode 1 comme un move normal juste après le téléport).
- **Envoie** immédiatement :
  - **60** — `SendGetNearItemsLocked()` (`RQ_GetNearItems`) ;
  - **46** — `SendFromPreInGameToInGameLocked()` (`RQ_FromPreInGameToInGame`), qui remet `g_fromPreInGameResult` à `-1` jusqu’à la réponse serveur.
- **`T4CLoginSessionConsumePlayerTeleport`** — le thread UI consomme une fois l’événement téléport (pattern identique à `ConsumePlayerPopupUpdate`).
- **`T4CLoginSessionResetAfterReturnToLogin`** — reset des flags téléport au retour login (Esc).
- Stub `#else` (hors `LINUX_PORT`) pour le linker.

**Effet secondaire voulu, sans code supplémentaire :** tant que la réponse **46** n’est pas revenue (`g_fromPreInGameResult != 0`), `T4CLoginSessionSendMove` **refuse** déjà les opcodes 1–8 — même garde qu’à l’entrée en jeu. Cela évite d’envoyer un move sur la touche encore enfoncée pendant la transition (comportement proche de `DoNotMove` Windows).

#### `src/game/GameWorldScreen.cpp`

Dans **`Update()`**, **avant** `pollHeldMovement()` :

1. `T4CLoginSessionConsumePlayerTeleport(&teleport)` ;
2. si téléport reçu :
   - `zone_ = teleport.world` ;
   - `mapFlag_ = true` → prochain `redraw()` appelle `mapi_->get_map(...)` pour la **nouvelle** carte ;
   - `snapPlayerVisual(x, y)` → `playerX_`/`playerY_`, `npcm_->set_world_pos` (annule un éventuel glide `move_to`), `syncCameraToPlayer()` ;
   - `setPlayerWalkAnim(false)` → action idle `'S'`.

**Non modifié dans ce commit :** handler opcode 1 / popup, `tryMovePlayer`, `kMoveVisualStepsMul`, facing, luminosité F4/F5.

### Fichiers modifiés

| Fichier | Lignes / rôle |
|---------|----------------|
| `src/network/T4CLoginSession.h` | struct `T4CPlayerTeleport`, déclaration `ConsumePlayerTeleport` |
| `src/network/T4CLoginSession.cpp` | `HandleTeleportPlayer`, branche 57, consume, reset |
| `src/game/GameWorldScreen.cpp` | bloc `#if LINUX_PORT` dans `Update()` (6 lignes effectives) |

### Logs attendus après fix

```text
[PHASE] RQ_TeleportPlayer (57) : @ 2633,1457 monde 0.
[UDP] -> RQ_GetNearItems (60).
[PHASE] Envoi RQ_FromPreInGameToInGame (46) ...
[PHASE] Reponse RQ_FromPreInGameToInGame (46) code=0.
```

Puis déplacement normal une fois le 46 OK.

### Non inclus (volontairement)

| Élément Windows | Raison |
|-----------------|--------|
| `g_GameMusic.Reset()` / `LoadNewSound()` | Pas de couche audio SDL3 monde |
| `Objects.DeleteAll()` / PNJ réseau | Opcode **69** / **60** pas rendus à l’écran |
| Fade écran (`World.SetFading`) | Non porté |
| Handler **opcode 16** (objets sol) | Hors périmètre |
| Modifications **TnC** | Règle projet : pas toucher `TnC_dev` pour le gameplay réseau |

### Limites restantes après ce commit

- **Vitesse marche / animation jambes** — inchangé (`63fe2e2`).
- **Opcode 1** — snap serveur sur chaque ack move peut toujours couper le glide ; pas retouché ici.
- **Validation escaliers** — à confirmer en jeu réel ; si blocage persiste, investiguer réponse **46** post-57 côté serveur avant d’ajouter autre chose.

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

---

## 2026-05-19 — Vue monde jouable (déplacement local, rendu SDL3, perso réseau)

### Contexte

Suite à l’entrée en jeu validée (13 → 18 → 46/60), travail sur **Phase 2** : afficher et déplacer le personnage dans `GameWorldScreen` avec les assets `$T4C_DATA`, en restant aligné sur le client Windows 1.68 (`TFCSocket.cpp`, `Tileset.cpp`, `packethandling.cpp`).

**État actuel (démo jouable, pas « on joue » complet) :** carte isométrique TnC, sprite joueur, flèches / pavé numérique, envoi réseau des opcodes **1–8**, glissement visuel `move_to`, luminosité F4/F5. La vitesse de marche et l’animation des jambes restent **trop rapides** par rapport au ressenti Windows ; le snap serveur sur opcode **1** peut couper l’animation si on ne le gère pas finement (tentative « ack glide » annulée — bloquait le déplacement).

---

### Réalisé (client Linux / SDL3)

#### `src/game/GameWorldScreen.cpp` / `GameWorldScreen.h`

- **Déplacement clavier** : flèches + pavé numérique ; maintien des touches via `pollHeldMovement()` dans `Update()`.
- **Opcodes réseau** : `tryMovePlayer()` → `T4CLoginSessionSendMove(1–8)` (grille `TFCSocket.cpp` : N/E/S/O + diagonales).
- **Orientation sprite** : angles VSF (0°, 45°, …) dérivés de `TileSet::MoveToPosition` / directions internes 1–9 du client 1.68 (correction du mapping « boussole » initial qui affichait de mauvais profils).
- **Glissement visuel** : `NPCManager::move_to()` avec `steps_mul` (durée **et** pas par frame ralentis ensemble dans `npcmanager.cpp`) ; `is_moving()` pour enchaîner un pas à la fois ; caméra (`locX_`/`locY_`) mise à jour à la fin du glide, pas à chaque `set_world_pos`.
- **Perso** : spawn via `NPCList.txt` + `T4CPlayerSpriteNpcName()` (apparence PacketPopup **10004** / race) ; actions `'D'` marche / `'S'` idle.
- **Luminosité** : F4/F5 sur `Sdl3FramePresenter` (`SDL_SetRenderColorScale`, défaut ~1.2) — sans modifier les pixels carte (échec gamma CPU / `SDL_LockSurface`).
- **Fenêtre monde** : résolution logique 1800×1000 ; `main.cpp` agrandit la fenêtre SDL en phase monde (~1600×900).
- **Annulé (régression)** : file d’attente `moveInProgress_` + ignore snap serveur sur ack opcode 1 — bloquait tout mouvement ; retour au flux simple (snap sur popup, `playerX_` mis à jour au pas).

#### `src/network/T4CLoginSession.cpp` / `.h`

- **`T4CLoginSessionSendMove`** : paquet TFC opcode seul (1–8), si pipeline in-game (étape ≥ 6, 46 OK).
- **Opcode 1** (réponse move) : `ApplyServerUnitPosition` + `g_playerPopupPending` → `ConsumePlayerPopupUpdate` dans `GameWorldScreen` (sync position serveur).
- **`T4CActivePlayer`** : nom, race, appearance, `serverX/Y`, `unitId`, sprite dérivé pour le rendu.

#### TnC (`client_graphical_sdl3_test/TnC_dev/` via `cmake/TncGraphical.cmake`)

- **`NPCManager::set_world_pos`** : position immédiate (sync serveur / téléport).
- **`NPCManager::is_moving`** : état animation `move_to`.
- **`NPCManager::move_to(..., steps_mul)`** : paramètre `steps_mul` pour ralentir proprement (ne pas multiplier seulement `duree_depl` sans `depl_x`/`depl_y` — sinon le sprite traverse la case en ~8 frames quoi qu’il arrive).
- Constante client : `kMoveVisualSpeed = 4`, `kMoveVisualStepsMul = 15` (à tuner).

#### `third_party/tnc_sdl3/`

- **`Sdl3FramePresenter`** : présentation surface → texture SDL3, scale luminosité rendu.

#### Données & build

- **`client/data/`** + `scripts/assemble_t4c_data.sh` : layout runtime unifié (`sprites/`, `maps/`, `sons/`, fonts, `NPCList.txt`).
- **`cmake/TncGraphical.cmake`** : sources TnC compilées dans `t4c_client` (pas de binaire TnC séparé).

---

### Bugs corrigés (2026-05-20)

#### ~~Escaliers / changement de carte — perso figé + « tapis roulant »~~ → opcode **57** géré

Voir section **2026-05-20** ci-dessus. Avant ce commit : pas de handler 57, pas de reload `zone_`, pas d’envoi **60+46** post-téléport.

---

### Bugs / limites encore ouverts

### Limites connues (à traiter)

| Sujet | Détail |
|--------|--------|
| **Vitesse marche / jambes** | Animation marche toujours pilotée par `ANIM_FPS` (15) dans `npc_draw.cpp`, indépendante de `move_to` ; déplacement perçu encore rapide si le serveur **snap** sur opcode 1 (`snapPlayerVisual` → `set_world_pos` annule le glide). Réglage fin : `kMoveVisualStepsMul`, éventuellement ne pas snap si ack = position prédite (sans bloquer le jeu). |
| **Carte** | `get_map` → `full_redraw` à chaque pas caméra (pas de scroll `move_map`) — saccades possibles. |
| **Collision** | Autorité serveur (WDA) ; pas d’affichage client du refus de move (opcode 1 sans déplacement). |
| **Autres joueurs / PNJ** | Pas de rendu des unités réseau (opcode **69** `UnitUpdate`, etc.). |
| **Objets sol** | Opcode **16** (peripheric objects) non géré à l’écran. |
| **Musique / SFX** | Fichiers WAV sous `data/sons/` présents ; pas de `GameMusic.cpp` / SDL_mixer — pas de « Sadness Music » ni musique de zone après 13. |
| **Opcodes monde** | Beaucoup de paquets post-46 seulement **logués** (**43** stats, **60** near units, **131**, chat, combat, loot, UI…) — voir `CLIENT168_RC14h_OK/.../packethandling.cpp`. |
| **WDA côté client** | **Hors scope** — le client ne lit jamais les `.WDA`. |

---

### À faire — priorité recommandée

#### Serveur (`T4C_Server_Linux_Final_Step`, hors dépôt client)

- [ ] **Démarrer sans workarounds de boot** : retirer ou rendre optionnels les contournements actuels (**skip creature**, **skip ground object**, **éjection / retrait NPC WDA** ou chargement NPCs.WDA dégradé) une fois le chargement WDA 1.68 LP64 stable (`second_approach/`, `key_swaps/`).
- [ ] Valider **WDA Worlds + Edit + NPCs** avec la bonne clé XOR 1.68 (LCG) et structures LP64 — le client en dépend indirectement (collisions, spawns, refus de move).

#### Client — réseau (`T4CLoginSession`)

- [ ] Handlers structurés pour les opcodes monde (réf. `packethandling.cpp`), au minimum :
  - ~~**57** — téléport / escaliers~~ — **fait** (2026-05-20)
  - **9** — `GetPlayerPos` / synchro position
  - **43** — stats / HUD joueur
  - **60** — unités proches (compléter au-delà du log)
  - **69** — `UnitUpdate` (autres entités visibles)
  - **16** — objets au sol autour du joueur
  - **131** et flux déjà reçus en rafale après 46 — identifier et classer
- [ ] Ne pas spammer les moves : respecter le rythme ~50 ms / round serveur ; file d’inputs si besoin.
- [ ] Snap serveur opcode **1** : corriger sans bloquer (ex. ignorer snap si coords = position déjà envoyée **pendant** le glide).

#### Client — rendu / gameplay (`GameWorldScreen`, TnC)

- [ ] Régler définitivement **vitesse + animation jambes** (lier frame marche à `tps_depl`/`duree_depl` ou ralentir `ANIM_FPS` pour le joueur id 0).
- [ ] Scroll carte **`move_map`** au lieu de `full_redraw` systématique (TnC a `mapi_move_map.cpp`).
- [ ] Opcodes **9** + caméra si le serveur corrige la position hors opcode 1.
- [ ] Pas de torche `env_` / fond gris (reverts documentés — artefacts).

#### Client — audio

- [ ] Porter **`GameMusic.cpp`** / **`NewSound.cpp`** : DirectSound → SDL3 audio ou SDL_mixer.
- [ ] Pistes : `"Sadness Music"` (liste persos), puis `LoadNewSound()` après opcode **13** (zone + coords), fichiers sous `$T4C_DATA/sons/`.

#### Client — polish & ops

- [ ] MOTD opcodes **65** / **66** (cosmétique login).
- [ ] CI Linux : build + smoke handshake UDP (trace `debug/t4c_network_session.log`).
- [ ] Commits séparés : `key_swaps/`, `second_approach/` (outils WDA, pas le runtime client).

---

### Fichiers touchés (session mouvement / rendu, hors `data/` binaire)

| Zone | Fichiers principaux |
|------|---------------------|
| Monde | `src/game/GameWorldScreen.cpp`, `GameWorldScreen.h`, `TncDataPaths.cpp` |
| Réseau | `src/network/T4CLoginSession.cpp`, `.h` |
| Présentation | `third_party/tnc_sdl3/render/Sdl3FramePresenter.cpp`, `.h`, `tnc_sdl2_compat.h` |
| Build | `CMakeLists.txt`, `cmake/TncGraphical.cmake` |
| TnC embarqué | `client_graphical_sdl3_test/TnC_dev/NPCManager/*`, (miroir `client_graphical_path_to_follow/decode/TnC_dev/`) |
| App | `src/main.cpp` |
