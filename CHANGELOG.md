# Changelog — T4C Client 1.68 RC14h (port Linux/SDL3)

Historique des modifications du client sous `#ifdef LINUX_PORT` et de la documentation associée.

> **Convention :** chaque entrée significative est horodatée **`YYYY-MM-DD HH:MM:SS`** (logs réseau, tests utilisateur ou fin de patch). Chaque ajout futur indique sa **famille fonctionnelle** (voir ci-dessous).

---

## FAMILLES FONCTIONNELLES — taxonomie port *(2026-05-25)*

Référence pour classer tout nouveau travail dans le CHANGELOG (`**Famille : …**` en tête d’entrée ou colonne tableau).

| Famille | Contenu | Rendu / couche client | Opcodes / serveur typiques | Statut Linux |
|---------|---------|------------------------|----------------------------|--------------|
| **Carte / tuiles** | Sol, murs, décor **statique** `.map` | `MapInterface` → `sol_` / `decor_` | Données locales `$T4C_DATA/maps/` | ✓ |
| **Unités réseau** | Joueur, autres PCs, mobs, PNJ **sprite** (app ≥ 10005 ou 20xxx) | `NPCManager` (sdl3_test) | **1**, **16**, **10004**, **69**, **70** | ✓ partiel (2026-05-25) |
| **Objets au sol** | Portes, coffres, leviers, triggers, objets posés WDA (apparences objet, typ. sous 10001) | *À faire* — équivalent Windows `VisualObjectList` / `Objects.Add`, **pas** `NPCManager` | **16** (`TFCAddObject`), **10004** objet ; serveur `WDAInitObjects` / `create_world_unit` | ✗ client |
| **Perso / ODBC** | Stats, inventaire, coffre banque, skills, sorts, **position déco** | HUD + panneaux B/K/P/U | **13**, **18**, **39**, **62**, **106**, **43**, **33**, **37**, **44**, **67** | partiel (HUD + sac/skills/sorts/coffre ✓ 2026-05-26) |
| **Combat / loot** | Attaques, dégâts, mort, butin | Anim + HUD | divers `packethandling.cpp` | ✗ |
| **PNJ scriptés** | Dialogues, quêtes, marchands | UI + scripts serveur | `NPCs.WDA`, interaction clic | ✗ |
| **UI / social** | Chat, menus, MOTD, options, **launcher** | overlays launcher / monde | **65**, **66**, **131**, … | partiel (clic + double-clic liste persos ✓ 2026-05-26) |
| **Audio** | Musique zone, SFX | `T4CGameMusic` / futur SFX | 100 % client (+ coords opcode **1** / **57**) | musique ✓ ; SFX ✗ |
| **Infra / WDA / build** | Skips, LP64, perf boot, commits outils | — | serveur + scripts Python | en cours |

**Rappel utilisateur :** les **portes interactives** (apparition, animation ouverture) = famille **Objets au sol**, pas « unités réseau » ni « carte tuiles » seules.

**Déjà documenté sous cette famille :** opcode **16** filtré aujourd’hui par `IsRemoteDrawableUnit` → les objets sol (dont portes) sont **ignorés** volontairement jusqu’à l’étape dédiée.

---

## POLITIQUE MOTEUR — une seule source de vérité *(2026-05-25)*

| Dépôt / dossier | Rôle | Modifier ? |
|-----------------|------|------------|
| **`client_graphical_sdl3_test/TnC_dev/`** | **Moteur TnC compilé** dans `t4c_client` (SDL3 natif, patches gameplay) | **Oui** — seul endroit pour le code moteur |
| **`client_graphical_path_to_follow/decode/TnC_dev/`** | **Flacon témoin** mestoph / labo offline (référence historique) | **Non** — ne pas patcher ; `git checkout` pour restaurer |
| **`finalstep/client/`** | Client Linux (`T4CLoginSession`, `GameWorldScreen`, CMake) | Oui — appelle le moteur via `cmake/TncGraphical.cmake` |

**CMake (`TncGraphical.cmake`)** : priorité **1** = `../client_graphical_sdl3_test/TnC_dev` ; fallback **2** = path_to_follow **uniquement** si sdl3_test absent.

**Autonomie sdl3_test (2026-05-21)** : les 9 symlinks `TnC_dev/` → path_to_follow ont été remplacés par des **copies réelles** (`FontManager`, `VSFInterface`, `NPCManager`, `TextManager`, `fonts`, `NPCList.txt`, assets). Plus aucune dépendance symlink au témoin pour compiler.

**Modifs moteur session réseau / monde (sdl3_test, pas path_to_follow) — `NPCManager` :**

| API | Rôle |
|-----|------|
| `move_to(..., steps_mul)` | Glide visuel ; `steps_mul` ralentit durée **et** pas/frame |
| `is_moving(id)` | Bloque enchaînement de pas tant que l'anim tourne |
| `set_world_pos(id, x, y)` | Snap immédiat (sync serveur / téléport) |
| `set_npc_type(id, nom)` | Changement sprite (opcode 69 apparence) |
| `remove_npc(id)` | Despawn unité distante ; **refuse id 0** (joueur local) |

Autres modules sdl3_test déjà portés SDL3 (hors diff path_to_follow) : `MapInterface/`, `VSFInterface/`, `FontManager/`, `TextManager/`, `include/tnc_sdl3.h`, `render/`.

---

## BACKLOG — Pipeline WDA, assets *(documenté 2026-05-20 — mis à jour 2026-05-25)*

> **Statut :** boot serveur **sans skips** validé en jeu (**2026-05-25 23:03:07** — mobs, move). Reste surtout **perf boot WDA**, **commit outils LP64**, **parité perso ODBC** (coffre/skills/sorts).

### État du pipeline WDA (résumé)

| Composant | Emplacement | Statut |
|-----------|-------------|--------|
| Key-swap 1.61 → 1.68 | `key_swaps/` | OK round-trip Worlds/Edit/NPCs ; **insuffisant seul** si format 1.61 ≠ parseur 1.68 |
| Patch LP64 (`lCharges` 4→8 oct) | `second_approach/patch_wda_lp64.py` | OK Worlds + Edit (MD5 documentés CHANGELOG **serveur** 2026-05-18) |
| Vérif parse objets LP64 | `second_approach/verify_lp64_objects.py` | OK objets ; **creatures pas encore vérifiées** |
| Specs format txt LP64 | `second_approach/wdatxtadapted/Format du fichier *.wda.txt` | Générées par `adapt_formats.sh` ; **pas encore commitées** |
| WDA runtime serveur | `T4C_Server_Linux_Final_Step/build/WDA/` | Doit être variante **LP64** (`second_approach/install_to_build.sh`), **pas** Havoc brut réinstallé via `key_swaps/install_to_build.sh` seul |

**Référence serveur détaillée :** repo `T4C_Server_Linux_Final_Step/CHANGELOG.md` entrée **2026-05-18 — Boot WDA**.

---

### Skips WDA serveur — retirés du boot par défaut (2026-05-25)

**État actuel :** `T4C_Server_Linux_Final_Step/tools/debug/t4c_env.sh` — les deux `export T4C_SKIP_*` sont **commentés** (plus actifs au lancement normal).

| Variable | Rôle historique | Statut **2026-05-25** |
|----------|-----------------|------------------------|
| **`T4C_SKIP_GROUND_OBJECTS`** | Sauter ~2601 objets posés (étape 2) | **Retiré** — fix `WorldMap::SetBlockingUnit` (boucle Y) |
| **`T4C_SKIP_CREATURES`** | Sauter lecture/init creatures (étapes 3–4) | **Retiré** — boot + mobs en jeu (`0x4E26` opcode 1) |

**Code serveur :** le mécanisme `getenv("T4C_SKIP_*")` reste dans `TFCInit.cpp` / `WDACreatures.cpp` comme **bypass dev optionnel** (décommenter dans `t4c_env.sh` pour diagnostiquer une phase). **À faire plus tard :** supprimer ce code mort une fois le boot LP64 stable partout.

**Boot complet attendu (sans variables) :**

```text
1. Objets WDA (définitions)          ✓
2. WDAInitObjects (~2601 au sol)     ✓ (fix WorldMap)
3–4. Creatures + WDAInitCreatures    ✓ (validation 23:03:07)
5. WDAInitNPC (NPCs.WDA)             fix trailer appliqué — boot `[WDAInit] WDAInitNPC done` si WDA Havoc/LP64 OK
6–8. Hives / area links / clans      ✓
→ Server started
```

Le boot peut rester **lent** (1–3 min sur objets/creatures) : `WDAFile::Read` lit encore **octet par octet** (`fgetc`) — voir § « Reste ouvert » ci-dessous.

---

### Skips WDA — référence historique (contournements 2026-05-18)

<details>
<summary>Ancien tableau skips (archivé)</summary>

| Variable | Ce qu’elle sautait | Cause racine d’origine |
|----------|-------------------|------------------------|
| **`T4C_SKIP_GROUND_OBJECTS`** | ~2601 `create_world_unit` | Boucle Y `SetBlockingUnit` + obj. **106** @ 1601,2569 |
| **`T4C_SKIP_CREATURES`** | Section creatures WDA | Perf `fgetc` + alignement LP64 |

</details>

**Ordre de travail restant (serveur) :**

1. ~~Retirer skips boot~~ — **fait** (mai 2026)
2. Perf `WDAFile::Read` (`fread` par blocs) — confort boot, pas bloquant fonctionnel
3. Commit / doc pipeline LP64 (`second_approach/`, specs txt)
4. `load_character` **chemin long** Linux (coffre, skills, sorts) — **position déco + inventaire déjà chargés**
5. Retirer le code `getenv(T4C_SKIP_*)` quand plus utile

---

### NPCs.WDA — plus simple sur la clé, plus compliqué sur le fond

| Aspect | Worlds / Edit | `NPCs.WDA` |
|--------|---------------|------------|
| Header WDA `0x0CA7` | Oui | **Non** — format à part |
| Key-swap 1.61→1.68 | Nécessaire mais pas suffisant | **Round-trip OK** (`key_swaps/test_keyswap.py`) |
| Patch LP64 charges | Central | **Pas le même sujet** |
| Parseur | `WDAObjects`, `WDACreatures`, … | `NPCManager::Load` → `NPC::Load` → **arbre d’instructions** |
| Dépendance | — | Enregistrement NPC via `Unit::GetIDFromName(creatureId)` → **creatures WDA déjà chargées** |

→ **Un script Python « comme les autres »** suffit pour la **clé** ; le reste = **C++ NPC_Editor** + **creatures non skippées**.

---

### WDA en `.txt` — quoi push Git, quoi garder privé

| Fichier | Contenu | Push repo public ? | Risque takedown |
|---------|---------|-------------------|-----------------|
| **`Format du fichier T4C *.wda.txt`** | Schéma binaire + clé XOR 3418 octets (~35 Ko) — **pas** les données Havoc | **Oui recommandé** (avec `second_approach/`, `key_swaps/*.py`) | Modéré (clé XOR reverse-engineering) |
| **`T4C Worlds.txt`** / dumps `wc -d` | Monde Havoc **décompilé** (noms sorts, stats, positions…) | **Non** | Élevé (= contenu jeu) |
| **`.WDA` binaires** (`output/`, `build/WDA/`) | Données monde chiffrées (~24 Mo) | **Non** | Élevé |
| **`data/sprites`, `maps`, `sons`** | Assets client runtime (~325 Mo) | **Non** (`.gitignore` actuel) | Élevé |

**Commit public envisagé plus tard (pipeline seul, sans binaires) :**

```
second_approach/wdatxtadapted/Format du fichier *.wda.txt
second_approach/adapt_formats.sh, patch_wda_lp64.py, verify_lp64_objects.py, build.sh, toolchain/
key_swaps/*.py, README.md          # clé déjà en Python dans key_161.py
```

**Hors Git :** `output/*.WDA`, `input/T4C Worlds.txt`, `data/*` binaires, Havoc `tiforci/havoc2/*.WDA`.

---

### Assets client — préservation sans tout publier

Problème : le **code seul** ne suffit pas (`T4C_DATA` requis pour carte + sons). Options documentées :

| Stratégie | Rôle |
|-----------|------|
| **Repo public = code + specs WDA txt + scripts** | Sauve le *comment* reconstruire |
| **Repo privé ou backup local** | Code + `data/` + WDA LP64 |
| **Manifeste** (`MANIFEST.sha256`, liste fichiers attendus) | Prouve qu’on avait quoi sans redistribuer |
| **README « apportez vos propres fichiers T4C »** | Parité légale install Rebirth / 1.68 |

Ne **pas** mélanger IP Vircom (`.WDA`, dumps txt données, `data/`) et code GPL/port sur le **même repo public** — risque retrait repo entier (DMCA).

---

### Checklist client (repo `finalstep/client`) — commits futurs

- [ ] Commit `second_approach/` + specs `wdatxtadapted/*.txt` (sans `output/*.WDA`)
- [ ] Commit `key_swaps/` (scripts Python, sans `output/*.WDA`)
- [ ] Commit `scripts/assemble_t4c_data.sh` + `data/README.md` (sans binaires `sprites/`/`maps/`/`sons/`)
- [ ] `data/MANIFEST.md` ou script `verify_t4c_data.sh` (optionnel)
- [x] ~~Synchroniser avec fix serveur quand skips retirés~~ — **2026-05-25** (boot sans skips, validation en jeu)

---

## 2026-05-27 — Sac graphique VSF (base)

**Famille :** **UI / social** · **Perso / ODBC**.

### 2026-05-27 — Panel backpack VSF (première passe)

**Ajout :**

| Élément | Détail |
|---------|--------|
| `WorldBackpackPanel` | Fond `BackPack` / `BackpackOutline`, grille **9×6** (cases 26 px), icônes `64kInv*` |
| `T4CInvItemIconMap.gen.cpp` | Table **appearance → nom sprite** (~459 entrées), extraite des `BIND_INV` de `VisualObjectList.cpp` |
| Touche **B** | Ouvre le panel graphique (opcode **18** inchangé) |
| Side menu **BackPack** | Ouvre le même panel graphique |

**Génération offline (pas de Python au runtime) :**

```bash
python3 scripts/generate_t4c_inv_icon_map.py
```

Régénère `src/gui/T4CInvItemIconMap.gen.cpp` si la table Windows change.

**Limites connues :**

- Pas encore drag & drop / use item / équipement graphique.
- Side menu (Esc) : hitboxes alignées sur le cadre ; minimap TMI Windows non portée.
- Side menu : Macros / Groupe / Chat → placeholder.

**Fichiers :** `WorldBackpackPanel.{cpp,h}`, `T4CInvItemIcons.{cpp,h}`, `T4CInvItemIconMap.gen.cpp`, `scripts/generate_t4c_inv_icon_map.py`, `GameWorldScreen.{cpp,h}`, `cmake/TncGraphical.cmake`.

### 2026-05-28 04:52:34 — Mobs : opcode 10001 (combat) + glide identique au joueur

**Famille : Unités réseau**

**Problème :** gobs **téléportent** sans anim de marche ; le perso glide (`move_to`) mais reste un peu erratique.

**Cause :** en combat le serveur envoie surtout **`__EVENT_ATTACK` (opcode 10001)** avec les coords attaquant/cible — pas des opcode **1** par case. Le client ignorait 10001 ; les snaps opcode **16** puis rattrapage = téléportation.

**Correctif :**

| Élément | Détail |
|---------|--------|
| `HandleEventAttack()` | Parse paquet serveur (ids + X/Y attaquant et cible) → `QueueRemoteUnitMove` |
| `applyRemoteNpcMotion()` | **Même logique** que `applyServerPlayerPosition` : `set_world_pos(from)` + `move_to` si Manhattan ≤ **4**, sinon snap |
| Coalescence Move | Un seul glide visuel par frame et par `unitId` (file UDP peut contenir N paquets) |
| `g_remoteUnitAppearance` | Map unitId→apparence pour relier 10001 aux mobs déjà spawn (opcode 16) |

**Limites :** pas d’anim de coup (`set_action('A')` / `SetAttack` Windows) sur opcode **10001** — marche seulement.

**Fichiers :** `T4CLoginSession.cpp`, `T4CLoginSession.h`, `GameWorldScreen.{cpp,h}`.

### 2026-05-28 04:25:46 — Fix mobs figés au corps à corps (purge + anim)

**Famille : Unités réseau**

**Problème :** mobs visibles (opcode **16**) mais **jambes figées**, plus de déplacement ni téléportation en combat — logs : beaucoup d’opcode **1** silencieux, joueur OK.

**Causes :**

| Bug | Effet |
|-----|--------|
| `purgeStalePlayerDuplicateRemotes()` passait `active.appearance` (PC) au lieu de l’apparence distante | Toute unité dans **4 cases** du joueur était **retirée** à chaque frame sync |
| `applyRemoteNpcMotion()` : `manhattan == 0` → `set_action('A')` | Mobs bloqués en pose attaque sans marche |
| `HandleUnitUpdate()` : `QueueRemoteUnitSpawn` sur chaque **69** | Re-snap position depuis cache sol obsolète |

**Correctif :**

| Élément | Détail |
|---------|--------|
| `remoteAppearances_` | Apparence par `unitId` pour purge/filtre corrects |
| `purgeStalePlayerDuplicateRemotes()` | Filtre avec l’apparence **de l’unité distante**, pas celle du joueur |
| `T4CLoginSessionShouldSkipRemoteUnit()` | Uniquement même `unitId` ou PC **sur la case exacte** du joueur (plus de rayon 4) |
| `applyRemoteNpcMotion()` | Plus d’attaque sur delta 0 (supprimé) ; glide affiné ensuite (entrée 04:52) |
| `HandleUnitUpdate()` | HP seulement (`QueueRemoteUnitUpdate`), plus de re-spawn |

**Fichiers :** `GameWorldScreen.{cpp,h}`, `T4CLoginSession.cpp`.

### 2026-05-28 03:59:19 — Fix regression déplacement joueur (doublon PC opcode 16)

**Problème :** après anim mobs, le perso « fait n'importe quoi » — opcode **16** spawnait un **2ᵉ Warrio** (ex. `id=4` @ 2944,1059) ; snaps serveur sur 2+ cases.

**Correctif :**

| Élément | Détail |
|---------|--------|
| `T4CLoginSessionShouldSkipRemoteUnit()` | Ignore tout **PC** (même apparence) dans un rayon **4 cases** du joueur local |
| `purgeStalePlayerDuplicateRemotes()` | Retire les doublons déjà spawnés avant sync |
| Glide joueur | `applyServerPlayerPosition` : `move_to` jusqu'à **4 cases** Manhattan (plus seulement 1) |
| Mobs | Glide distant max **8 cases** (réduit snap longue distance) |

### 2026-05-28 03:47:03 — Unités distantes : glide move_to + anim marche / attaque

**Famille : Unités réseau**

**Problème :** mobs visibles mais **téléportation** case par case (seul le pas adjacent utilisait `move_to`) ; pas d’anim marche ni attaque.

**Correctif :**

| Élément | Détail |
|---------|--------|
| `applyRemoteNpcMotion()` | `move_to` jusqu’à **20 cases** Manhattan ; au-delà = snap (`set_world_pos`) |
| Marche | `set_action('D')` + `set_direction` sur tout déplacement glide |
| Attaque | opcode **1** sans changement de case (`hasPrev` + delta 0) → `set_action('A')` |
| Mort | opcode **69** avec `hpPercent == 0` → `set_action('M')` |

**Limites :** pas de ciblage attaque / orientation vers la victime ; opcodes combat Windows (**10001+**) toujours non branchés.

**Fichiers :** `GameWorldScreen.{cpp,h}`.

### 2026-05-28 03:11:15 — Unités réseau : opcode 60/16 (GetNearItems) fiable

**Famille : Unités réseau**

**Problème :** mobs invisibles en combat — le serveur inflige des dégâts (opcode 33) mais aucun gobelin à l’écran ; la réponse **RQ_GetNearItems (60)** arrive en **opcode 16** et était souvent **ignorée** tant que la réponse **46** n’était pas `code=0` (course 16 avant 46).

**Correctif :**

| Élément | Détail |
|---------|--------|
| Gate réseau | `CanProcessWorldUnitPackets()` — traite **16 / 1 / 69 / 70 / 57 / 10004** dès `pipelineStep ≥ 6`, sans attendre le 46 |
| Opcode **60** entrant | Ack vide loggé ; si corps > 8 octets, parse comme lot **16** |
| Opcode **69** | Spawn distant si position connue (`groundObjects`) et unité pas encore instanciée |
| Resync client | `T4CLoginSessionRequestNearItems()` (cooldown 1,5 s) + `pollNearItemsResync()` toutes les **12 cases** parcourues |
| Debug | Log quand un spawn est filtré (apparence non drawable / joueur local) |

**Limites :** pas d’anim attaque/mort ni chiffres de dégâts (famille **Combat / loot** — étape suivante).

**Fichiers :** `T4CLoginSession.{cpp,h}`, `GameWorldScreen.{cpp,h}`.

### 2026-05-28 03:25:00 — Fix regression : deadlock menu bloque (RequestNearItems)

**Problème :** apres entree en jeu, musique monde mais ecran reste sur le menu persos — thread principal bloque sur `g_sessionMutex` (`futex_wait` dans gdb).

**Cause :** `T4CLoginSessionRequestNearItems()` verrouillait `g_sessionMutex` puis appelait `SendGetNearItemsLocked()` → `SendToServerLocked()` qui re-verrouille le meme mutex (non recursif).

**Correctif :** retirer le double lock ; `pollNearItemsResync()` peut de nouveau envoyer le 60 sans freezer la boucle principale.

### 2026-05-28 02:38:03 — SideMenu Esc : hitboxes sur le cadre + options audio

**Problème :** barre latérale (Esc) — icônes propres dans `64kSideBox`, mais les sprites `64kSideButton*` empilés en colonne créaient une couche « pourrie » par-dessus ; les clics ne correspondaient pas aux icônes (sac OK, fiche perso morte, 3ᵉ case ouvrait Options).

**Correctif :**

| Élément | Détail |
|---------|--------|
| Rendu | **Uniquement** `64kSideBox` — plus de blit `64kSideButton*` (icônes déjà peintes dans le cadre) |
| Hitboxes | 7 zones invisibles **40×38** aux encoches Y mesurées sur export BMP (`2, 49, 87, 284, 322, 353, 388`), X=6 |
| Ordre visuel (haut→bas) | Fiche perso → Sac → Grimoire → Options → Macros → Groupe → Chat |
| Fiche perso (slot 1) | Panneau texte **kind 6** — stats opcode **43** (`T4CPlayerStatus`) |
| Grimoire (slot 3) | Panneau sorts **kind 3** (équivalent touche **P**) |
| Sac (slot 2) | Panel VSF / opcode **18** (inchangé) |
| Options (slot 4) | Popup étendue : **Musique** / **Sons** (`←`/`→`, ±5 %), Annuler, Retour login, Quitter |
| Audio | `T4CGameMusic::GetVolume` / `SetVolume` ; `GetSfxVolume` / `SetSfxVolume` (SFX mémorisé, pipeline pas encore branché) |

**Abandonné :** fond opaque « minimap » sur la colonne — sans effet utile, bande noire indésirable.

**Limites :** minimap TMI Windows (`SideMenu::m_MainTMI`) non portée ; Macros / Groupe / Chat = placeholder ; réglage Sons sans effet tant que les SFX ne sont pas câblés.

**Fichiers :** `WorldSideMenu.{cpp,h}`, `GameWorldScreen.{cpp,h}`, `T4CGameMusic.{cpp,h}`.

---

## 2026-05-26 — HUD monde TTF lisible (overlay)

**Famille :** **UI / social** · **Perso / ODBC**.

### 2026-05-26 — Barres + panneaux B/K/P/E/U en T4CUiFont (~24 pt)

**Problème :** HUD monde dessiné avec `FontManager` bitmap ~12 px sur canvas **1800×1000** → barres 14 px et panneaux illisibles.

**Solution :** overlay TTF (`t4cbeaulieux.ttf`, même stack que le launcher) sur `screen_` avant `present` :

| Élément | Avant | Après |
|---------|-------|-------|
| Ligne perso / debug | bitmap 12 px | TTF **24 pt** |
| Barres PV / mana | 220×14 px | **400×28 px** |
| Panneaux sac/skills/sorts/coffre/équip | 420 px, lineH 16 | **540 px**, lineH **30** |
| Popup Options | bitmap | TTF |

**Repli :** si `t4cbeaulieux.ttf` absent, retour automatique au bitmap `FontManager`.

**API :** `T4CUiFont::renderTextSurface`, `blitText` (surface RGBA32 pour blit sur `screen_`).

**Fichiers :** `T4CUiFont.{cpp,h}`, `GameWorldScreen.{cpp,h}`.

---

## 2026-05-26 — HUD stats perso (opcode 43 / 33 / 37 / 44 / 67)

**Famille :** **Perso / ODBC**.

### 2026-05-26 01:11:42 — Réception stats combat + barres PV / mana / XP

| Opcode | Rôle client |
|--------|-------------|
| **43** | Parse `Character::PacketStatus` (aligné `Packet.cpp`) — HP, mana, niveau, XP, stats, poids |
| **33** | Mise à jour PV (`HP` + `MaxHP` si présent) |
| **37** | Level-up — niveau, seuil XP suivant, PV/mana remontés |
| **44** | Mise à jour XP totale |
| **67** | Mise à jour mana courante |

**Rendu :** barres PV (rouge) et mana (bleu) + ligne XP sous la ligne debug en haut à gauche.

**Entrée monde :** si opcode **43** pas encore reçu à l'`Init` de `GameWorldScreen`, envoi `RequestPlayerStatus()` (fallback).

**API :** `T4CPlayerStatus`, `GetPlayerStatus`, `ConsumePlayerStatusUpdate`, `RequestPlayerStatus`.

**Fichiers :** `T4CLoginSession.{cpp,h}`, `GameWorldScreen.cpp`.

---

## 2026-05-26 — Sélection perso au clic (launcher)

**Famille :** **UI / social**.

### 2026-05-26 01:40:31 — Double-clic entree en jeu

- **Double-clic** (400 ms, meme ligne) = `tryEnterWorld()` (equivalent Entree).
- Clic simple conserve la selection.

**Fichier :** `CharacterSelectScreen.cpp`.

---

## 2026-05-26 — Inventaire / skills / sorts / coffre banque

**Famille :** **Perso / ODBC**.

### 2026-05-26 01:40:31 — Opcodes 18 / 39 / 62 / 106–110

| Opcode | Handler | UI monde |
|--------|---------|----------|
| **18** | Parse `PacketBackpack` (sac) | **B** panneau sac |
| **39** | Parse `PacketSkills` | **K** panneau skills |
| **62** | Parse `PacketSpells` | **P** panneau sorts |
| **106** | `RQ_ChestContents` coffre banque | **U** (auto a l'ouverture banque) |
| **109/110** | Show/Hide coffre | panneau coffre auto |

**Entree monde :** apres opcode **46** OK → envoi **39** + **62** (aligne `Packet.cpp` post-EnterGame).

**Fichiers :** `T4CPlayerInventory.h`, `T4CLoginSession.{cpp,h}`, `GameWorldScreen.{cpp,h}`.

### 2026-05-26 03:45:00 — Equipement (19), objets sol et clic deplacement

**Famille :** **Perso / ODBC** + **Unites reseau** + input monde.

- Parse `RQ_ViewEquiped` (**19**) : 13 slots (body→feet), nom d'objet, quantite/charges ; panneau **E**.
- Requete `RQ_ViewEquiped` ajoutee aux demandes post-entree monde et refresh manuel touche **E**.
- Marqueurs objets au sol (apparence `< 10001`) derives de **16/1/70** et dessines en overlay.
- Clic gauche monde : conversion iso approximative pixel→case, envoi d'un pas serveur (opcode 1–8) vers la direction cible.

**Fichiers :** `T4CPlayerInventory.h`, `T4CLoginSession.{cpp,h}`, `GameWorldScreen.{cpp,h}`.

### 2026-05-26 05:15:00 — Fix freeze touche E (equipement)

**Famille :** **Perso / ODBC**.

- **Key repeat** ignore sur **E** (plus de rafale `RQ_ViewEquiped` / 19).
- Requete opcode **19** seulement si equipement pas encore `valid` ; **Maj+E** force un refresh.

**Fichier :** `GameWorldScreen.cpp`.

### 2026-05-26 02:30:00 — Noms d'objets inventaire (opcode 59)

**Famille :** **Perso / ODBC**.

- Parse reponse `RQ_QueryItemName` (59) : place + objectId + chaine TFC (aligne `TFCMessagesHandler::RQFUNC_QueryItemName`).
- Envoi limite (4/tick) pendant panneaux **B** / **U** ; lignes sac/coffre affichent le nom quand recu.

**Fichiers :** `T4CPlayerInventory.h`, `T4CLoginSession.{cpp,h}`, `GameWorldScreen.cpp`.

### 2026-05-26 02:15:00 — Fix freeze touches B / K / P / U

- **Key repeat** ignore sur B/K/P/U (plus de rafale `RQ_ViewBackpack` / `39` / `62`).
- Requete reseau seulement si donnees pas encore `valid` ; **Maj+B/K/P** force un refresh.
- Panneau : texte rasterise une fois (cache surfaces), pas de `get_text` a chaque frame.
- Deplacement fleches coupe tant que panneau ouvert ; parse inventaire/skills plafonne (128 entrees, chaines 256 o).

**Fichiers :** `GameWorldScreen.{cpp,h}`, `T4CLoginSession.cpp`.

---

### 2026-05-26 01:11:42 — Clic souris sur la liste opcode 26

- **Clic gauche** sur une ligne = sélection (équivalent flèches haut/bas).
- Coordonnées logiques via `SDL_ConvertEventToRenderCoordinates` (aligné `LoginScreen`).
- **[Entree]** inchangé pour entrer en jeu.

**Fichier :** `CharacterSelectScreen.cpp`.

**Hors scope commit HUD :** déplacement monde à la souris (voir backlog ci-dessous).

---

## BACKLOG — Input monde (déplacement souris)

**Famille :** **Unités réseau** + input client (pas Perso/ODBC).

| Item | Client Windows | Port Linux | Verdict |
|------|----------------|------------|---------|
| **Déplacement souris (clic → pathfinding)** | `Pf.cpp` + clic droit/gauche sur tuile isométrique | Clic gauche = 1 pas direction cible (conversion iso simple), pas de file A* multi-cases | **Partiel** (03:45) |
| Prérequis | Conversion pixel → case monde, A* (`Pf.cpp`), enchaînement opcodes 1–8 | Idem + serveur autoritaire (1 pas / ack) | Estimation : écran moteur + UI, pas un patch réseau |

**Recommandation :** traiter après combat/loot ou en parallèle « confort joueur », pas mélangé au commit stats **43**.

---

## 2026-05-25 — Unités distantes + déplacement serveur-autoritaire

**Famille :** **Unités réseau** (+ **Perso / ODBC** pour move joueur local).

### 2026-05-25 23:03:07 — Validation utilisateur (*« tout est rentré dans l'ordre »*)

Test LH avec creatures WDA actives côté serveur :

- **Déplacement joueur** : orientation immédiate (client) + glide visuel sur ack opcode **1** PC (`app=0x271B` / `0x271C`, ex. `… 27 1B 00 10 0B 38 …`).
- **Unités distantes** : flood opcode **1** mobs (`app=0x4E26`, brigand 20006) routé vers `ApplyRemoteUnitMove` / `syncRemoteUnitsFromNetwork` — pas de snap caméra joueur.
- **Collisions** : move serveur-autoritaire (`tryMovePlayer` n'avance plus localement ; `awaitingServerMove_` + `applyServerPlayerPosition`).

**Commit parent documenté :** `6e38fda` (musique). Travail réseau + monde : **non commité** (session 2026-05-21 → 2026-05-25).

---

### 2026-05-25 22:52:10 — Régression : orientation OK, perso immobile

**Symptôme :** après passage en autorité serveur pure, le sprite tourne (facing client dans `tryMovePlayer`) mais **n'avance plus** ; logs opcode **1** joueur présents mais ignorés.

**Cause :** `ApplyServerUnitPosition` refusait tout ack si `g_activePlayer.unitId == 0` (cas fréquent après téléport opcode **57** sans **10004** préalable).

**Fix (`T4CLoginSession.cpp`, ~22:55–23:02) :**

| Règle | Détail |
|-------|--------|
| `unitId == 0` | Apprendre l'id depuis le **premier opcode 1 PC** avec déplacement **adjacent** (1 case) à `serverX/Y` |
| `unitId` connu mais ≠ paquet | Accepter si adjacent **et** même `appearance` (correction id réseau) |
| Mobs | Toujours exclus par `IsPlayerUnitAppearance` **avant** ce bloc |

---

### 2026-05-25 (session) — Déplacement serveur-autoritaire + régressions téléport

| Horodatage | Problème | Correctif |
|------------|----------|-----------|
| **2026-05-25** (session) | Opcode **16** spawne le joueur local comme unité distante | `ShouldSkipAsRemoteUnit` — ignore PC @ position spawn serveur |
| **2026-05-25** (session) | Opcode **1** ré-apprenait `unitId` depuis n'importe quel PC | Filtre apparence PC strict ; pas de ré-apprentissage depuis mobs |
| **2026-05-25** (session) | Déplacement optimiste → murs / téléports | `tryMovePlayer` envoie opcode 1–8 sans avance locale ; ack via `applyServerPlayerPosition` |
| **2026-05-25 22:52:10** | Perso figé après fix ci-dessus | Ré-apprentissage `unitId` adjacent (voir entrée **23:03:07**) |

---

### 2026-05-25 (session) — Boot serveur sans skips

Aligné CHANGELOG serveur : skips commentés dans `t4c_env.sh`, mécanisme `getenv` conservé en code pour debug futur.

---

### 2026-05-25 (session) — Affichage unités distantes (réseau → NPCManager)

Voir aussi § G (détail opcode par opcode) dans l'entrée **2026-05-21** ci-dessous.

| Opcode | Action client |
|--------|---------------|
| **10004** | Joueur local (PC) ou spawn distant |
| **1** | Move joueur (filtre PC) ou move distant (mobs) |
| **16** | Spawn lot peripherique (GetNearItems) |
| **69** | Maj HP / apparence |
| **70** | Despawn (`NPCManager::remove_npc`) |

**Fichiers :** `T4CLoginSession.{cpp,h}`, `GameWorldScreen.{cpp,h}`, `client_graphical_sdl3_test/TnC_dev/NPCManager/npcmanager.{h,cpp}`, `cmake/TncGraphical.cmake`.

---

## 2026-05-21 — Opcode 1, apparence joueur, déplacement, skips WDA serveur

### A. Opcode 1 : filtre unitId + apparence PC (`__EVENT_OBJECT_MOVED`)

#### Symptôme

En jeu, avec les **creatures WDA** chargées côté serveur (hives actives), les logs réseau montraient un **flood d’opcodes 1** (~2/s × nombre de mobs visibles) et des positions serveur qui **alternaient** entre deux zones éloignées (ex. `(2930,1049)` et `(2965,1082)`), alors que le joueur ne bougeait pas ou avançait normalement.

**Effets visibles :** caméra / sprite joueur qui **sautait** d’une case à l’autre, `mapi_full_redraw` à chaque paquet, impression que le déplacement « ne correspond pas » au serveur.

#### Cause

Sur le **fil serveur → client**, l’opcode **1** n’est pas une « réponse à ton move nord » : c’est **`__EVENT_OBJECT_MOVED`** (`EventListing.h`) — le serveur **broadcast** ce paquet pour **chaque unité** qui se déplace dans la zone (~40 tiles) : joueur, mobs de hive, autres PCs.

Format (après en-tête TFC + opcode) : `X`, `Y`, puis `PacketUnitInformation` (appearance, **unitId**, radiance, status, HP%).

Le handler Linux **`ApplyServerUnitPosition`** (version initiale) lisait seulement `X`/`Y` (offsets 6–9) et appliquait la position à **`g_activePlayer`** + **`g_playerPopupPending`**, **sans comparer l’unitId**. Chaque mob qui patrouillait écrasait donc la position du joueur et déclenchait un `snapPlayerVisual()` dans `GameWorldScreen`.

Ce bug n’apparaissait pas tant que **`T4C_SKIP_CREATURES=1`** masquait les spawns ; il devient visible dès que les creatures WDA tournent (362 unités), indépendamment du crash **`NPCs.WDA`** (PNJ scriptés — autre pipeline).

#### Fix (v1 — filtre unitId)

**`src/network/T4CLoginSession.cpp`** — `ApplyServerUnitPosition` :

1. Lit **`unitId`** à l’offset 12 (`ReadBeInt32Msf`, comme `HandlePacketPopup`).
2. Met à jour `g_activePlayer.serverX/Y` et pose `g_playerPopupPending` **uniquement si** `unitId == g_activePlayer.unitId`.
3. Ignore silencieusement les mouvements des autres unités (mobs — affichage dédié à faire plus tard).
4. Log phase renommé : `__EVENT_OBJECT_MOVED (1) — joueur @ …` seulement quand le paquet concerne le perso actif.

**Insuffisant seul** — voir v2 ci-dessous.

#### Symptôme résiduel (v1)

Après v1 : plus de sauts entre deux zones éloignées, mais **perturbations** persistantes — logs `[PHASE] joueur @ …` sur des paquets dont l’apparence est **`0x4E26` (20006, creature)**, et le **vrai** ack joueur (`appearance 0x271B`, `unitId 4`) ignoré.

Exemple filtré à tort : `… 0B 93 04 32 4E 26 00 10 0A DD …` (mob) passait car `g_activePlayer.unitId` valait **`0x00100ADD`** — id d’un mob, pas du PC.

#### Cause racine (v2)

L’opcode **10004** (`Unit::PacketPopup`) n’est **pas** réservé au joueur : le serveur l’envoie pour **toute unité** qui apparaît à portée (mobs, objets, autres PCs). **`HandlePacketPopup`** enregistrait le **premier** 10004 reçu comme perso actif → `unitId` et parfois position pris sur un **mob** (souvent même tile au spawn). Le filtre unitId v1 « fonctionnait » alors contre le mauvais id.

#### Fix (v2 — apparence PC + re-apprentissage unitId)

**`src/network/T4CLoginSession.cpp`** :

1. **`IsPlayerUnitAppearance`** — 10001–10004, 15001–15004, 10011/10012 (puppet), 15011/15012 ; exclut creatures (≥ 20000).
2. **`HandlePacketPopup`** — ne met à jour `g_activePlayer` que si apparence PC.
3. **`ApplyServerUnitPosition`** — ignore si apparence non-PC ; **ré-apprend** `unitId` + apparence depuis le premier opcode 1 joueur si l’état était corrompu ; ignore si position inchangée (évite redraw spam).
4. **`GameWorldScreen::Update`** — ne snap / ne relance pas la musique si position déjà identique (évite d’**annuler le glide** `move_to` quand l’ack serveur confirme la case prédite).

**Fichiers modifiés :** `src/network/T4CLoginSession.cpp`, `src/game/GameWorldScreen.cpp`, `CHANGELOG.md`.

**Lecture des logs :** les paquets `4E 26` (20006 = **brigand**, mob hive crypte LH / désert RF) ne sont **pas** ton personnage. Ton PC serveur = **`27 1B` (10011, puppet humain)** quand **tu** bouges.

---

### B. Apparence visuelle : « je suis un brigand » vs humain Windows

#### Ce que tu vois à l’écran

Le client Linux **ne dessinait pas** les mobs réseau avant 2026-05-21 (§ G). Il affichait **un seul** sprite : le tien, via **`NPCManager` + `NPCList.txt`**.

| Couche | Windows 1.68 | Client Linux actuel |
|--------|--------------|---------------------|
| Apparence serveur | **10011** puppet (`__PLAYER_PUPPET`) | Idem (filtré réseau) |
| Rendu visuel | Système **Puppet** + **VObject** : corps humain + équipement ; classes mappées ex. **10001 → 20043** (`__MONSTER_HUMAN_SWORDMAN`) | Sprite **NPCList** entier par classe |
| Guerrier | Puppet composé (humain armé) | **`Warrio`** — nom exact dans `NPCList.txt` ligne 218 |

#### Pourquoi « Warrio » et pas « Warrior » ?

Ce n’est **pas** une faute de frappe : **`Warrio`** est le **nom canonique TnC/mestoph** dans `NPCList.txt` (sprites VSF `warrio180-a`, etc.). Le client Windows n’utilise pas ce nom — il passe par **`ObjectListing.h`** et des IDs **20xxx**. On ne peut pas renommer en « Warrior » sans ajouter une entrée NPCList + sprites correspondants.

#### Pourquoi ça ressemble aux brigands (crypte LH, désert RF) ?

Dans les assets TnC (`t4cgamefile.dec`), les sprites **`warrio*`** et **`BlackWarrior*`** coexistent ; l’art mestoph du guerrier NPCList est un **sprite monstre entier**, visuellement proche des humanoïdes hostiles ( brigands / BlackWarrior ) — **pas** le puppet humain cuirassé du client officiel.

**Ce n’est pas** que le réseau te donne l’apparence 20006 : c’est une **limitation de rendu** (pas de Puppet / pas de map 10001→20043). Prochaine étape graphique : porter le rendu puppet ou mapper vers les VSF « Human Swordman » Windows.

#### Fix court terme (2026-05-21)

- **`T4CPlayerSpriteNpcName`** : classe depuis questionnaire création (`classIndex`) ou race 10001–10004, pas la race liste **10011** (puppet).
- HUD monde : `app 10011 | Warrio` pour vérifier que le réseau dit bien puppet humain.

---

### C. Déplacement : régression et correctifs

#### Symptôme (session 2026-05-21)

Après les filtres opcode 1 : déplacement « catastrophique » — glide coupé, caméra qui lag, pas bloqué sur `is_moving` aussi longtemps.

#### Causes

1. **`snapPlayerVisual()` sur chaque ack opcode 1** (commit `6e38fda`) appelait `set_world_pos` **même quand la position serveur = position prédite** → annulation du glide en cours.
2. **`kMoveVisualStepsMul = 15`** : ~120 frames par case ; `is_moving(0)` bloque le pas suivant très longtemps.
3. **Caméra** (`locX_`/`locY_`) synchronisée seulement **à la fin** du glide, pas au début du pas.

#### Correctifs appliqués

| Fichier | Changement |
|---------|------------|
| `GameWorldScreen.cpp` | **`syncCameraToPlayer()`** dès qu’un pas local est accepté (carte défile tout de suite) |
| `GameWorldScreen.cpp` | Snap serveur **uniquement si** `popup.serverX/Y ≠ playerX_/Y` (rollback mur / désync ; pas si ack = prédiction) |
| `GameWorldScreen.h` | **`kMoveVisualStepsMul = 4`** (commit `6e38fda` avait 15) |

Aligné sur l’intention du commit **`63fe2e2`** (glide TnC + opcodes 1–8), sans le snap systématique qui tuait l’animation.
#### Itérations suivantes (toujours non commitées) — **mise en pause**

| Changement | Effet |
|------------|--------|
| `set_world_pos(fromX, fromY)` avant chaque `move_to` | Moins de décalage sprite / coord internes NPCManager après un pas |
| Caméra sur case de **départ** du glide (`moveStartX_/Y_`), resync à la fin | Carte moins « en retard » sur le glide |
| File `pendingMoveOpcode_` + `KEY_UP` seulement si `!is_moving` | Touches maintenues moins bloquantes |
| `kMoveVisualStepsMul = 2` (testé aussi 1 et 4) | Glide plus court ; moins de blocage `is_moving` |

**État utilisateur (2026-05-21)** : nettement mieux qu'au début de session, mais **glitchs visuels restants** (saccades, jambes en marche à l'arrêt, effet de dédoublement) — surtout liés au coût **`mapi_full_redraw` ~18 ms** à chaque pas et à l'absence de rendu des **autres unités** opcode 1 (mobs). **Travail déplacement mis en pause** ; priorité **crash `NPCs.WDA` serveur** (§ F).

**Prochaine piste move (quand reprise)** : invalidation partielle carte / throttle redraw, pas retoucher le filtre réseau opcode 1.


---

### D. Boot serveur WDA : ordre des phases et « NPCs corrigés tout seuls » ?

> Détail serveur : `T4C_Server_Linux_Final_Step/CHANGELOG.md` entrée **2026-05-18 — Boot WDA**. Code : `TFCInit.cpp` (~l.1240–1320).

#### Ordre réel du boot (après lecture Worlds + Edit)

```text
1. Définitions objets WDA          (stats, formules — toujours chargées)
2. WDAInitObjects                  (pose ~2601 objets AU SOL)  ← T4C_SKIP_GROUND_OBJECTS
3. Section creatures WDA           (CreateFrom ou SkipSection) ← T4C_SKIP_CREATURES
4. WDAInitCreatures                (enregistre ~362 mobs)       ← sauté si skip creatures
5. WDAInitNPC                      (NPCs.WDA — PNJ scriptés)   ← phase séparée !
6. WDAInitHives                    (groupes de spawn / respawn)
7. WDAInitAreaLinks
8. WDAInitClans
→ Server started
```

#### Deux skips **indépendants** (contournements dev, pas des fixes)

| Variable | Phase contournée | Effet quand actif |
|----------|------------------|-------------------|
| **`T4C_SKIP_GROUND_OBJECTS=1`** | Étape **2** — boucle `create_world_unit` (~2601 objets sol ; obj. **106** @ 1601,2569 bloquait) | Pas d’objets **grounded** sur les cartes ; définitions (stats) OK |
| **`T4C_SKIP_CREATURES=1`** | Étapes **3–4** — pas de `CreatureData` en RAM, pas de `WDAInitCreatures` | Pas de mobs enregistrés ; **hives** (étape 6) chargées quand même via `SkipSection` |

Quand vous **retirez** un skip, le blocage revient **à cette phase-là**.

#### « Ça plantait plus loin » — typiquement quoi ?

- **Retrait `T4C_SKIP_GROUND_OBJECTS` seul** → blocage / freeze en **étape 2** (placement objets sol), pas pendant creatures.
- **Retrait `T4C_SKIP_CREATURES` seul** → boot long ou blocage en **étape 3–4** (lecture `fgetc` octet par octet) ; puis mobs actifs en jeu si boot OK.
- **Étape 5 NPCs** : parseur **`NPCs.WDA` crash toujours** au 1er NPC — **ce n’est pas réparé**.

#### NPCs : **non**, ce n’est pas « corrigé tout seul »

Dans `TFCInit.cpp`, `WDAInitNPC()` est dans un **`try { … } catch (…) { … continuing boot }`** :

```text
[BOOT] loading NPCs (NPCs.WDA)…
[NPC] … crash parse 1er NPC …
[WDAInit] WDAInitNPC FAILED — see [NPC] logs (creatures OK, continuing boot)…
```

Le serveur **attrape l’exception**, logue l’échec, et **continue** vers Hives / Area links / Clans. Donc :

- **`Server started`** → oui, le boot peut finir  
- **PNJ scriptés** (`NPCs.WDA` : marchands, quêtes, dialogues) → **non**, toujours absents  
- **Brigands / mobs hive** → viennent des **creatures** (étapes 3–4), **pas** de `NPCs.WDA`

Ce n’est pas une réparation automatique : c’est un **contournement** (ignorer l’échec NPC pour ne pas tuer tout le serveur). Aucun fix du parseur `NPCs.WDA` n’a été commité.

#### Deux pipelines « NPC » à ne pas confondre

| | **Creatures WDA** (étapes 3–4) | **NPCs.WDA** (étape 5) |
|---|--------------------------------|-------------------------|
| Fichier | `T4C Worlds.WDA` / `T4C Edit.WDA` | `NPCs.WDA` |
| Contenu | Stats mobs, apparences 20xxx (brigand 20006…) | Scripts PNJ (INTL, quêtes, marchands) |
| Skip / erreur | ~~`T4C_SKIP_CREATURES`~~ retiré du boot | Fix trailer **2026-05-21** — valider `[WDAInit] WDAInitNPC done` |
| État actuel **2026-05-25** | Boot sans skip, mobs visibles client | NPCs scriptés si parseur + WDA OK ; try/catch si échec |

---

### F. NPCs.WDA — diagnostic crash serveur (priorité 2026-05-21)

| Étape | Résultat |
|-------|----------|
| Déchiffrement `tiforci/havoc2/NPCs.WDA` | `version=1`, `qty=51`, 1er NPC `creature=drunk`, arbre cohérent en Python |
| Opcode `0x65a1000b` dans le fichier déchiffré | **Absent** → pas un opcode manquant dans l'enum, mais **lecture désalignée** |
| Alignement binaire | Après `InsSayText` (11) : `nParams=1`, string 26 o, puis **`DWORD 1`**, puis opcode suivant (ex. `InsBreakConversation` 12) |
| Code serveur | `CompositeInstruction::Load` ne consommait pas le `DWORD` trailer → fix ci-dessus |

**Commit de référence client** : `6e38fda` (musique). Travail move + réseau + fix NPC serveur : **non commité** (validation **2026-05-25 23:03:07**).

---

### E. Audio (fix crash spawn — non documenté avant)

**`src/audio/T4CGameMusic.cpp`** : `PlayTrackId` appelait `FreeWav` avant `CloseStream` → thread SDL audio lisait un buffer libéré → **SIGSEGV** au spawn. Fix : **`CloseStream` en premier**, puis `LoadWavFile`.

---

### G. Affichage monstres / unités distantes (réseau → NPCManager)

#### Rôle de chaque couche (moteur vs client vs serveur)

| Couche | Responsabilité | Cette mission |
|--------|----------------|---------------|
| **Serveur T4C** | État du monde, spawn creatures/hives, envoi paquets binaires | Inchangé — source de vérité (`Unit::PacketPopup`, `PacketUnitInformation` dans `Unit.cpp`) |
| **Client réseau** (`T4CLoginSession.cpp`) | Décoder les opcodes, filtrer joueur local vs unités distantes, file d'événements thread-safe | **Nouveau** — voir opcodes ci-dessous |
| **Client rendu** (`GameWorldScreen.cpp`) | Consommer la file, appeler `NPCManager` (id = `unitId` serveur, id 0 = joueur local) | **Nouveau** — `syncRemoteUnitsFromNetwork()` |
| **Moteur TnC** (`NPCManager`) | Liste chaînée de sprites VSF, animation `move_to` / `draw_npc` | **`remove_npc(int id)`** ajouté — retrait propre à la despawn (opcode 70) ; **refuse id 0** (joueur) |

Le moteur **ne parle pas au réseau** : il ne sait dessiner que des entrées `(id, nom NPCList, x, y)`. Le client Linux traduit le protocole T4C en appels moteur.

#### OpCodes interceptés (alignés `Unit.cpp` + `EventListing.h`)

Lecture **octet par octet BE** (`ReadBeUint16`, `ReadBeInt32Msf`) — **pas** de `struct` packée (évite padding GCC ≠ Windows).

| Opcode | Nom | Payload après en-tête TFC (6 o) | Action client |
|--------|-----|----------------------------------|---------------|
| **10004** | `Unit::PacketPopup` | `short X, Y` + `PacketUnitInformation` | Joueur local si apparence PC **et** `unitId` = perso ; sinon **spawn distant** |
| **1** | `__EVENT_OBJECT_MOVED` | `X, Y` + `PacketUnitInformation` | Joueur local (filtre v2 inchangé) ; sinon **move distant** |
| **16** | `__EVENT_OBJECT_APPEARED_LIST` | `short count` puis × N `(X, Y + UnitInformation)` | **Spawn lot** (réponse peripherique / GetNearItems) |
| **69** | `RQ_UnitUpdate` | `PacketUnitInformation` seul | **Maj** HP / apparence (pas de position) |
| **70** | `RQ_MissingUnit` | `long unitId` | **Despawn** (`remove_npc`) |

**Corrections par rapport au cahier des charges initial :**

- **10004 n'est pas un `RQ_*`** de `TFC_MAIN.h` — type filaire aussi utilisé pour objets au sol ; seules les apparences drawable (`IsRemoteDrawableUnit`) sont instanciées.
- **Le mouvement des mobs passe par l'opcode 1**, pas par 69 seul.
- **Le spawn initial en zone** arrive surtout via **opcode 16**, pas seulement 10004.

#### Mapping apparence → sprite NPCList

`T4CSpriteNameFromAppearance()` — table partielle (20006 → `BlackWarrior`, 20003 → `Rat`, PCs → classes). Inconnus → **`BlackWarrior`**. Évolution : table complète depuis `ObjectListing.h`.

#### Fichiers modifiés

| Fichier | Changement |
|---------|------------|
| `src/network/T4CLoginSession.{h,cpp}` | File `T4CRemoteUnitEvent`, handlers 10004/1/16/69/70 |
| `src/game/GameWorldScreen.{h,cpp}` | `syncRemoteUnitsFromNetwork`, clear au teleport |
| `client_graphical_sdl3_test/TnC_dev/NPCManager/npcmanager.{h,cpp}` | **`remove_npc`** |

**Régression évitée :** filtres joueur sur opcode **1** et **10004** conservés ; chemin parallèle pour unités distantes.

#### 2026-05-21 — Moteur : `client_graphical_sdl3_test` autonome (fin des symlinks)

> **Rappel permanent :** voir section **[POLITIQUE MOTEUR](#politique-moteur--une-seule-source-de-vérité-2026-05-25)** en tête de ce CHANGELOG.

Les 9 symlinks `TnC_dev/` → `client_graphical_path_to_follow` ont été remplacés par des **copies réelles** dans `client_graphical_sdl3_test/TnC_dev/`. Le moteur SDL3 patché vit **uniquement** dans sdl3_test. `client_graphical_path_to_follow` = **flacon témoin** (`git checkout`, ne pas modifier).

#### Fix regression déplacement / téléport

> **Horodatage détaillé :** entrée **2026-05-25** en tête de ce CHANGELOG (`22:52:10` régression, `23:03:07` validation).

Les paquets opcode **1** avec `app=0x4E26` (20006, brigand) restent routés vers les unités distantes uniquement.

---

**Fichiers touchés (session 2026-05-21 → 2026-05-25, non commités — validation 2026-05-25 23:03:07) :**  
`src/network/T4CLoginSession.cpp`, `src/network/T4CLoginSession.h`, `src/game/GameWorldScreen.cpp`, `src/game/GameWorldScreen.h`, `src/audio/T4CGameMusic.cpp`, `client_graphical_sdl3_test/TnC_dev/NPCManager/npcmanager.{h,cpp}`, `CHANGELOG.md`.

---

## 2026-05-20 — Musique de fond (GameMusic → SDL3 audio)

### Contexte

Sous Windows 1.68, toute la logique musicale est **100 % client** (`GameMusic.cpp` + `NewSound.cpp` / DirectSound) : le serveur n’envoie ni piste ni fichier — seulement `Player.World` et les coords. Le client Linux avait les **WAV** sous `$T4C_DATA/sons/` (~11–15 Mo par piste) mais **aucune lecture**.

**Validation utilisateur :** *« c’est un perfect »* — Sadness à la sélection, musique de zone en jeu, transitions OK.

---

### Ce qui a été fait

#### A. Logique de zones — `T4CGameMusicZone.cpp/.h`

- Port **intégral** des macros `Track45` / `Track90` et règles par monde depuis `GameMusic.cpp` Windows.
- 8 pistes : Boss, Outdoors, Forest, Dungeons, Caverns, Sadness, Silence, Noises.
- `T4CGameMusicPickTrack(world, x, y, level)` + `T4CGameMusicTrackBaseName(id)` → noms VSB (`"Forest Music"`, …).

#### B. Lecteur SDL3 — `T4CGameMusic.cpp/.h`

- **Pas de SDL_mixer** : `SDL_INIT_AUDIO`, `SDL_LoadWAV`, `SDL_OpenAudioDeviceStream`, callback de **boucle** (`SDL_PutAudioStreamData`).
- Fichiers : `$T4C_DATA/sons/{nom}.wav` (ex. `Sadness Music.wav`).
- Volume par défaut **75 %** (`SDL_SetAudioStreamGain`) ; API `SetVolume(0..1)`.
- Ne recharge pas si la piste est déjà active (`g_oldTrackId`, comme `dwOldMusicNumber` Windows).

| Méthode | Équivalent Windows | Déclencheur |
|---------|-------------------|-------------|
| `Init()` / `Shutdown()` | init DirectSound | démarrage / quit app |
| `StartCharacterSelect()` | `g_GameMusic.Start()` | après auth → liste persos |
| `LoadNewSound(w,x,y,level)` | `LoadNewSound()` | entrée monde, opcode 1, téléport |
| `Reset()` | `g_GameMusic.Reset()` | avant `LoadNewSound` post-téléport |
| `Stop()` | `g_GameMusic.Stop()` | retour login |

#### C. Branchements

| Fichier | Rôle |
|---------|------|
| `src/main.cpp` | `SDL_INIT_AUDIO`, `Init`, `StartCharacterSelect` à l’opcode 26, `Stop` au retour login, `Shutdown` |
| `src/game/GameWorldScreen.cpp` | `LoadNewSound` à `Init` ; `Reset`+`LoadNewSound` sur opcode **57** ; `LoadNewSound` sur ack move opcode **1** |
| `CMakeLists.txt` | `T4CGameMusic.cpp`, `T4CGameMusicZone.cpp` |

---

### Parité Windows

| Écran / événement | Piste attendue | Statut |
|-------------------|----------------|--------|
| Sélection / création perso | Sadness Music | ✓ |
| Entrée monde (13 → Init) | Forest / Dungeon / Cavern + zones | ✓ |
| Déplacement (opcode 1) | recalcule zone | ✓ (comme `Packet.cpp:4559`) |
| Téléport (opcode 57) | Reset + nouvelle zone | ✓ |

---

### Limites restantes (Phase 2 audio)

| Sujet | Statut |
|-------|--------|
| **SFX** (combat, pas, UI) | Non porté (`NewSound.cpp`, `SoundFX[]`) |
| **Volume menu options** | Pas d’UI — `SetVolume()` disponible |
| **RAM** | WAV entier chargé en mémoire (~15 Mo/piste) — OK pour 1 piste streaming |
| **CD audio** (`bUseCD`) | Ignoré (obsolète) |

---

### Logs attendus

```text
[GameMusic] init OK
[GameMusic] lecture …/data/sons/Sadness Music.wav
[GameMusic] lecture …/data/sons/Forest Music.wav
```

---

## 2026-05-20 — Création personnage (25/31/26/13), introduction Haruspice, sélection persos enrichie

### Contexte — ce qu’on a enduré

Après validation du rendu monde (SDL3 natif, couleurs, téléport opcode **57**), la **Phase 1 réseau** restait incomplète côté **launcher** : pas de création perso, pas d’intro Windows, liste persos minimale (Entrée / Esc seulement).

| Symptôme | Cause identifiée |
|----------|------------------|
| **Après questionnaire → retour liste sans stats** | `ParseRolledStatsPayload` exigeait **31 octets** au lieu de **19** (`Character::packet_stats` = 7 stats + 2×long HP + 2×short mana). Réponse opcode **25** rejetée côté client alors que le serveur avait répondu. |
| **« Creation en cours (attente opcode 25)… » bloqué 60 s** | Parfois le serveur ne répond pas (picklock, `WaitForSaving`, compte `in_game`) ; plus souvent **perso provisoire stale** côté serveur (`boRerolling`) après timeout ou abandon — le client renvoyait un **25** sans nettoyer l’état précédent. |
| **Suppression perso → code erreur 3** | **Serveur** : `PlayerName VARCHAR(20)` trop court pour le rename interne lors du delete (`UPDATE` SQL échoue). Correctif SQL documenté côté serveur, pas dans ce dépôt. |
| **Menu connexion intermédiaire confus** | `ConnectionMenuScreen` ajouté puis **retiré** : l’utilisateur voulait Login → liste persos directement, intro accessible via raccourci. |
| **Intro absente** | Windows : écran `TFC_INTRODUCTION` (récit Haruspice) avant / pendant le launcher ; client Linux ne l’avait pas. |

**Objectif validé (flux Windows 1.68)** : Login → liste persos → **C** création → nom → sexe → questionnaire (4 questions) → **opcode 25** → écran **reroll stats** → **R** (31) / **Entrée** (26+13) → **monde** ; **Esc** annule (15).

---

### Note méthode — précision / parité Windows

| Sujet | Niveau de fidélité | Détail |
|-------|-------------------|--------|
| **Questionnaire** | **Élevé** | Banque 8 questions, shuffle `QN`/`RN`, 4 tirages — calqué sur `TFCSocket.cpp` (`Shuffle`, `QuestionAnswer`, indices `RN[QuestionNumber][QuestionChoice-1]`). |
| **Ordre octets opcode 25** | **Élevé** | `fillPacketStats` envoie `[answers[3], answers[2], answers[0], answers[1], answers[4], sex]` comme Windows (`Send << QuestionAnswer[3]…`). |
| **Reroll stats payload** | **Élevé** (après fix) | `kRolledStatsPayloadLen = 19`, offset stats à **7** après octet erreur (25) ou **6** (31). |
| **Validation création** | **Élevé** | `ConfirmCreateReroll` → refresh **26** puis **13** auto (`g_autoEnterWorldAfterCreate`) — aligné branche Windows `CreateFlag` / entrée directe en jeu. |
| **Introduction Haruspice** | **Moyen — texte statique** | Prose française **hardcodée** dans `IntroductionScreen.cpp`, annotée « aligné `French.elng` 45–47, 92–93 ». **Pas** de chargement runtime du fichier `.elng` (workaround : évite parser elng / dépendance asset non portée). |
| **Opcode 90 QueryNameExistence** | **Non câblé** | `BuildQueryNameExistencePacket` + déclaration API ajoutés ; Windows ne l’utilise pas non plus dans `TFCSocket.cpp` création — **non branché** sur validation nom. |
| **Delete opcode 15** | **Élevé** | Parsing multi-formats réponse (code 0, echo nom, code erreur) ; refresh **26** après delete. |

**Workarounds client (état stale / robustesse)** :

1. **`T4CLoginSessionPrepareForCreateScreen()`** — appelé à chaque `CharacterCreateScreen::resetFlow()` : reset flags client + **opcode 15** si reroll/nom provisoire encore connu.
2. **Timeout 60 s opcode 25** — déclenche aussi **15** sur le nom provisoire (`g_pendingCreatePlayerName`) pour débloquer le serveur.
3. **`Update()` création** — en étape `Reroll`, ne plus écraser l’UI avec « attente opcode 25 ».
4. **Logs** — refus envoi 25 logué avec flags `reroll/wait25/wait31/wait13/wait15`.

**Dépendance serveur (hors commit client)** : si delete échoue encore en code **3**, appliquer migration `PlayerName VARCHAR(64)` sur MariaDB (`bootstrap_auth_mariadb.sql` / `fix_playingcharacters_playername_length_mariadb.sql` côté `T4C_Server_Linux_Final_Step`).

---

### Ce qui a été fait

#### A. Écran introduction — `IntroductionScreen`

- Nouveau module plein écran 800×600 : texte Haruspice, **scroll auto** (~28 px/s) + molette / flèches.
- **I** ou **H** depuis l’écran **sélection persos** (`CharacterSelectScreen`).
- **Esc** / fin scroll → fermeture overlay ; le chrome launcher (fond BMP, bandeau) reste visible en dessous.
- Rendu via `T4CUiFont` / `LauncherChrome` (même stack que login).

#### B. Sélection persos — `CharacterSelectScreen`

- **Footer actions** (une ligne par raccourci, bas d’écran) :
  - `[Entrée]` jouer — `[C]` créer (`x/max` depuis opcode **103**)
  - `[D]` / Suppr → confirmation → **opcode 15** + attente **26**
  - `[I]` introduction — `[Esc]` retour login
- **`resetFlow()`** : réinitialise sélection, intro, erreurs réseau delete/13.
- **`IntroductionScreen`** embarqué (overlay, pas phase `main` séparée).
- Suppression du message permanent « creation non implementee » sur liste vide.

#### C. Création personnage — `CharacterCreateScreen` + `T4CCharacterQuestionnaire`

**Étapes UI** : `Name` → `Sex` (Tab) → `Questionnaire` (4× choix 1–5, flèches) → **`Reroll`** (stats).

| Touche | Action |
|--------|--------|
| **Entrée** | Avancer / envoyer **25** après Q4 / valider reroll |
| **Esc** | Retour étape précédente ; depuis **Reroll** → **15** + retour sélection |
| **R** (reroll) | **Opcode 31** — relance dés |
| **Tab** (sexe) | Homme / Femme |

**Réseau création** (`T4CLoginSession`) :

| Opcode | Rôle |
|--------|------|
| **25** `RQ_CreatePlayer` | Questionnaire + sexe + nom → stats reroll |
| **31** `RQ_Reroll` | Nouveau jet de dés |
| **15** `RQ_DeletePlayer` | Annulation perso provisoire (Esc reroll / cleanup stale) |
| **26** `RQ_GetPersonnalPClist` | Refresh liste après validation reroll |
| **13** `RQ_PutPlayerInGame` | Entrée auto en monde après **26** si validation reroll |

- **`HandleCreatePlayerReply`** : erreur octet 6 ≠ 0 → message utilisateur ; OK → `g_inCreateRerollPhase`, stats via `StoreRolledStatsFromPacket`.
- **`HandleRerollReply`** : mise à jour stats sans quitter phase reroll.
- **`ConfirmCreateReroll`** : `g_autoEnterWorldAfterCreate` + `TryAutoEnterWorldAfterCreateList()` sur réception **26**.
- **`CancelCreateReroll`** : **15** + clear état client.
- **`PrepareForCreateScreen`** : cleanup à l’ouverture écran création (voir workarounds).
- Timeouts poll : 25 (60 s), 31 (30 s), 15/26 (30 s) avec messages UI.

#### D. Flux application — `main.cpp`

- Nouvelle phase **`AppPhase::CharacterCreate`** entre sélection et monde.
- **C** sur sélection → `characterCreate.resetFlow()` → phase création (text input SDL activé).
- **Entrée reroll OK** → `ConsumeEnterWorldReady` → `GameWorldScreen::Init` (même chemin qu’entrée depuis liste).
- Esc création (nom) / Esc reroll (annulé) → retour sélection.
- **`ConsumeCreatePlayerSuccess`** conservé pour retour liste si entrée auto 13 échoue.

#### E. Suppression perso — `T4CLoginSession`

- **`RequestDeletePlayer`** : **15** puis **26** ; `g_waitingDeletePlayer` + timeout.
- **`HandleDeletePlayerReply`** : gère code 0, echo nom (format Windows), codes erreur.
- **`DeleteReplyLooksLikeDeletedName`** : heuristique pour réponses « nom supprimé » non standard.

#### F. Crédits — `CREDITS.md` (nouveau)

- Attribution Tom / ElEsTaNoBaRoN, session rendu 2026-05, Noth GPL, statut VSF, renommage moteur recommandé, note licence GPL vs Apache.

---

### Retiré / non retenu

| Élément | Raison |
|---------|--------|
| **`ConnectionMenuScreen`** | Écran intermédiaire post-login supprimé — flux direct Login → liste persos. |
| **Intro sur écran login** | Intro uniquement sur sélection (**I**/**H**) — évite double point d’entrée. |
| **Succès création → liste sans entrer en jeu** | Remplacé par entrée **monde directe** après validation reroll (parité Windows). |
| **Chargement `.elng` pour intro** | Texte intégré — parser elng non porté. |

---

### Fichiers modifiés / ajoutés — dépôt `client/`

| Fichier | Rôle |
|---------|------|
| `CMakeLists.txt` | `IntroductionScreen`, `CharacterCreateScreen`, `T4CCharacterQuestionnaire` |
| `CREDITS.md` | **Nouveau** — crédits port Linux |
| `src/gui/IntroductionScreen.cpp/.h` | **Nouveau** — récit Haruspice scrollable |
| `src/gui/CharacterCreateScreen.cpp/.h` | **Nouveau** — flux nom/sexe/questionnaire/reroll |
| `src/gui/T4CCharacterQuestionnaire.cpp/.h` | **Nouveau** — logique questionnaire Windows |
| `src/gui/CharacterSelectScreen.cpp/.h` | Footer, C/D/I, delete confirm, intro overlay, `resetFlow` |
| `src/network/T4CLoginSession.cpp/.h` | 25/31/15/26/13 création, reroll, delete, auto-enter, cleanup stale |
| `src/main.cpp` | Phase `CharacterCreate`, boucle rendu création |

---

### Logs attendus (création nominale)

```text
[PHASE] Envoi RQ_CreatePlayer (25) pour « MonPerso » sex=0 (… octets TFC).
[PHASE] RQ_CreatePlayer (25) OK — ecran reroll (opcode 31 / Entree / Esc).
[PHASE] Stats : FOR … END … AGI … SAG … INT … PV …/…
[PHASE] Creation validee — refresh 26 puis entree en monde (aligne Windows).
[PHASE] Creation confirmee — RQ_PutPlayerInGame (13) pour « MonPerso ».
[main] Nouveau perso — entree en jeu @ x,y Z…
```

---

### Limites restantes

| Sujet | Statut |
|-------|--------|
| **Opcode 90** validation nom | API + builder paquet ; pas d’UI ni handler réponse |
| **Timeout 25 sans réponse serveur** | Workaround cleanup **15** ; cause racine serveur (picklock / async) à investiguer si persiste |
| **Delete code 3** | Migration SQL **serveur** requise si colonne `PlayerName` trop courte |
| **Sprites création Windows** | Pas de fond `J_Back` / boutons `.dec` — UI texte SDL3 minimaliste |
| **Musique « Sadness Music »** | **Fait** — voir entrée musique **2026-05-20** |
| **Entrée intro depuis login** | Non — volontaire |

---

### Mise à jour entrée 2026-05-19

L’item « Écran création personnage + opcode 25 — **Non commencé** » (section *Non inclus*) est **obsolète** — voir cette entrée.

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
| Écran création personnage + opcode 25 | **Fait** — voir entrée **2026-05-20** (création / reroll) |
| Musique Phase 1 (`LoadNewSound`) | **Fait** — voir entrée musique **2026-05-20** |
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
| **Autres joueurs / mobs** | Rendu basique **fait** (opcodes 1/16/10004/69/70 — **2026-05-25**) ; polish animation / table apparences incomplète |
| **Objets au sol** | Portes, coffres, leviers, drops posés (famille dédiée — voir [FAMILLES](#familles-fonctionnelles--taxonomie-port-2026-05-25)) ; opcode **16** côté client non géré à l’écran |
| **Musique / SFX** | **Musique zone** ✓ (`T4CGameMusic`) ; SFX combat/UI **non portés** |
| **Opcodes monde** | Beaucoup de paquets post-46 seulement **logués** (**43** stats, **60** near units, **131**, chat, combat, loot, UI…) — voir `CLIENT168_RC14h_OK/.../packethandling.cpp`. |
| **WDA côté client** | **Hors scope** — le client ne lit jamais les `.WDA`. |

---

### À faire — priorité recommandée

#### Serveur (`T4C_Server_Linux_Final_Step`, hors dépôt client)

→ **Détail : section [BACKLOG — Pipeline WDA](#backlog--pipeline-wda-assets-documenté-2026-05-20--mis-à-jour-2026-05-25)**.

- [x] ~~Boot sans `T4C_SKIP_*`~~ — **2026-05-25** (`t4c_env.sh` commenté ; validation mobs + move client)
- [ ] **Perf boot** : `WDAFile::Read` — passer de `fgetc` (1–3 min) à `fread` par blocs (confort, pas bloquant)
- [ ] **Pipeline LP64** : commit scripts/specs `second_approach/` + doc MD5 WDA `build/WDA/` (outils client, pas le binaire)
- [ ] **`load_character` chemin long Linux** : réactiver coffre + skills + sorts (**position déco + inventaire déjà OK** — voir CHANGELOG serveur)
- [ ] Retirer définitivement le code `getenv(T4C_SKIP_*)` côté serveur

#### Client — réseau (`T4CLoginSession`)

- [ ] Handlers structurés pour les opcodes monde (réf. `packethandling.cpp`), au minimum :
  - ~~**57** — téléport / escaliers~~ — **fait** (2026-05-20)
  - **9** — `GetPlayerPos` / synchro position
  - **43** — stats / HUD joueur
  - **60** — unités proches (compléter au-delà du log)
  - **69** — `UnitUpdate` (autres entités visibles)
  - **16** — **Objets au sol** (portes, coffres, … — extension au-delà des unités déjà filtrées)
  - **131** et flux déjà reçus en rafale après 46 — identifier et classer
- [ ] Ne pas spammer les moves : respecter le rythme ~50 ms / round serveur ; file d’inputs si besoin.
- [ ] Snap serveur opcode **1** : corriger sans bloquer (ex. ignorer snap si coords = position déjà envoyée **pendant** le glide).

#### Client — rendu / gameplay (`GameWorldScreen`, TnC)

- [ ] Régler définitivement **vitesse + animation jambes** (lier frame marche à `tps_depl`/`duree_depl` ou ralentir `ANIM_FPS` pour le joueur id 0).
- [ ] Scroll carte **`move_map`** au lieu de `full_redraw` systématique (TnC a `mapi_move_map.cpp`).
- [ ] Opcodes **9** + caméra si le serveur corrige la position hors opcode 1.
- [ ] Pas de torche `env_` / fond gris (reverts documentés — artefacts).

#### Client — audio

- [x] **`GameMusic.cpp`** → `T4CGameMusic` + `T4CGameMusicZone` (SDL3 audio, WAV `data/sons/`)
- [ ] **SFX** : porter `NewSound.cpp` / `SoundFX[]` (DirectSound → SDL3)

#### Client — polish & ops

- [ ] MOTD opcodes **65** / **66** (cosmétique login).
- [ ] CI Linux : build + smoke handshake UDP (trace `debug/t4c_network_session.log`).
- [ ] Commits séparés : voir checklist **BACKLOG** (`second_approach/`, `key_swaps/` — specs txt oui, binaires non).

---

### Fichiers touchés (session mouvement / rendu, hors `data/` binaire)

| Zone | Fichiers principaux |
|------|---------------------|
| Monde | `src/game/GameWorldScreen.cpp`, `GameWorldScreen.h`, `TncDataPaths.cpp` |
| Réseau | `src/network/T4CLoginSession.cpp`, `.h` |
| Présentation | `third_party/tnc_sdl3/render/Sdl3FramePresenter.cpp`, `.h`, `tnc_sdl2_compat.h` |
| Build | `CMakeLists.txt`, `cmake/TncGraphical.cmake` |
| TnC embarqué | **`client_graphical_sdl3_test/TnC_dev/`** uniquement (path_to_follow = témoin, non compilé) |
| App | `src/main.cpp` |
