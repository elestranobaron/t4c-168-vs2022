# T4C Client 1.68 RC14h — Build sous Windows avec VS2022

## Contexte et objectif

Le but de départ était simple et con : **tester si le client T4C 1.68 se connecte à un serveur 1.68 qui tourne sous Linux**, sans avoir à revenir sous Windows, télécharger 7 Go de Visual Studio, et se battre avec un code source vieux de 20 ans.

La destination finale est un client **jouable sous Linux (SDL3)**, avec le **même contrat réseau et les mêmes assets** que le client Windows (DirectX / CW32Sprite), tout en gardant le build **VS2022 + DirectX** intact derrière `#ifdef LINUX_PORT`.

**Priorité d'exécution : Linux/SDL3 d'abord.** Windows reste la référence comportementale (opcodes, écrans, musique, rendu carte) — on ne le casse pas, on le recoupe derrière des `#ifdef` quand on porte une couche.

Ce README est la **roadmap vivante** : il doit refléter où on en est (réseau, serveur, assets, rendu), pas seulement « ça compile sous VS2022 ».

Le code source vient du repo de melodiass/elestranobaron (actuellement sans repo public). Le client installé dont sont tirés les assets de test est **The 4th Coming :: Rebirth** (installation locale, dossier `C:\Program Files (x86)\The4thComing`), qui contient apparemment des assets de plusieurs serveurs privés (Neerya, Realmud, Abomination, Saga...). On ne sait plus trop d'où ça sort — c'est le folklore T4C.

> **Fil conducteur.** Pour ce projet et nos échanges (humains ou assistés), ce `README.md` est la **référence partagée** : on le fait évoluer au fil des découvertes plutôt que d'éparpiller la vérité dans des notes isolées. Quand une décision ou une précision technique est importante, elle doit soit y être écrite, soit y pointer depuis un chemin de fichier explicite.

---

## Avertissement

Ce code source est un fork/leak du client T4C 1.68 RC14h. T4C (The 4th Coming) est une propriété intellectuelle dont les droits appartiennent à leurs détenteurs respectifs. Ce repo existe à des fins de recherche, de préservation et de portage vers des plateformes modernes. Il n'est pas destiné à un usage commercial.

Si vous êtes détenteur de droits et souhaitez discuter de ce projet, ouvrez une issue.

---

## Prérequis

- **Windows** (testé sur Windows 10/11 x64)
- **Visual Studio 2022 Build Tools** — installer uniquement les Build Tools, pas l'IDE complet
  - Composant requis : `MSVC v143` + `Windows 10 SDK (10.0.19041.0)`
  - Téléchargement : https://visualstudio.microsoft.com/fr/downloads/#build-tools-for-visual-studio-2022
- **Windows Kit 10.0.19041.0** (inclus avec les Build Tools)

---

## Modifications apportées pour compiler avec VS2022

Le code original ciblait Visual C++ 2008/2010. Le porter sur MSVC v143 a nécessité les modifications suivantes.

### 1. Conflit entre le DX9 SDK et le Windows SDK 10

Le `Directx9SDK\Include` contient des versions anciennes de `dxgi.h`, `dxgitype.h`, `dxgiformat.h`, `dwrite.h` etc. qui entrent en conflit avec ceux du Windows Kit 10. La solution est de forcer l'inclusion des headers WDK avant ceux du DX9 SDK.

**`Directx9SDK\Include\dxgiformat.h`** — remplacer tout le contenu par :
```cpp
#pragma once
#ifndef __dxgiformat_h__
#define __dxgiformat_h__
#include "C:\Program Files (x86)\Windows Kits\10\Include\10.0.19041.0\shared\dxgiformat.h"
#endif
```

**`Directx9SDK\Include\dxgitype.h`** — remplacer tout le contenu par :
```cpp
#pragma once
#ifndef __dxgitype_h__
#define __dxgitype_h__
#include "C:\Program Files (x86)\Windows Kits\10\Include\10.0.19041.0\shared\dxgitype.h"
#endif
```

**`Directx9SDK\Include\dwrite.h`** — ajouter des guards `#ifndef` autour des macros redéfinies en fin de fichier :
```cpp
#ifndef DWRITE_E_FILEACCESS
#define DWRITE_E_FILEACCESS             MAKE_DWRITE_HR_ERR(0x004)
#endif
#ifndef DWRITE_E_FONTCOLLECTIONOBSOLETE
#define DWRITE_E_FONTCOLLECTIONOBSOLETE MAKE_DWRITE_HR_ERR(0x005)
#endif
#ifndef DWRITE_E_ALREADYREGISTERED
#define DWRITE_E_ALREADYREGISTERED      MAKE_DWRITE_HR_ERR(0x006)
#endif
```

### 2. Ordre des includes dans StdAfx.h (T4CLauncher)

MFC exige que `winsock2.h` précède `windows.h`, et les types DXGI doivent être connus avant que `wincodec.h` soit chargé. Le fichier `T4CLauncher\StdAfx.h` doit commencer par :

```cpp
#define VC_EXTRALEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include "force_dxgi.h"
#include <typeinfo>
#include <afxwin.h>
// ... reste inchangé
```

Créer `T4CLauncher\force_dxgi.h` :
```cpp
#pragma once
#ifndef __dxgiformat_h__
#include "C:\Program Files (x86)\Windows Kits\10\Include\10.0.19041.0\shared\dxgiformat.h"
#endif
#ifndef __dxgitype_h__
#include "C:\Program Files (x86)\Windows Kits\10\Include\10.0.19041.0\shared\dxgitype.h"
#endif
#ifndef __dxgicommon_h__
#include "C:\Program Files (x86)\Windows Kits\10\Include\10.0.19041.0\shared\dxgicommon.h"
#endif
```

### 3. T4CLauncher.vcxproj — ForcedIncludeFiles

Dans le `ItemDefinitionGroup` pour `Debug|x86`, ajouter dans `<ClCompile>` :
```xml
<ForcedIncludeFiles>..\T4C Client\force_includes.h;%(ForcedIncludeFiles)</ForcedIncludeFiles>
```

Et dans `<Link>`, vérifier que `Zlib.lib` est bien présent dans `AdditionalDependencies`.

### 4. T4C Client.vcxproj — ForcedIncludeFiles

Créer `T4C Client\force_includes.h` :
```cpp
#pragma once
#include <typeinfo>
```

Dans le `ItemDefinitionGroup` pour `Debug|x86`, ajouter dans `<ClCompile>` :
```xml
<ForcedIncludeFiles>C:\chemin\absolu\vers\T4C Client\force_includes.h;%(ForcedIncludeFiles)</ForcedIncludeFiles>
```

Adapter le chemin absolu à votre machine.

### 5. Getopt.cpp — headers Unix incompatibles

Commenter les includes Unix en début de fichier :
```cpp
//#include <config.h>      // n'existe pas sous Windows
//#include <gnu-versions.h> // n'existe pas sous Windows
//#include <unistd.h>      // Unix only
//#include <unixlib.h>     // Unix only
```

Ajouter en toute première ligne du fichier :
```cpp
#define __GNU_LIBRARY__
#define __STDC__ 1
```

Corriger ligne ~683 :
```cpp
// Avant :
char *temp = my_index (optstring, c);
// Après :
const char *temp = my_index (optstring, c);
```

### 6. ChestUI.cpp, RobUI.cpp, TradeUI.cpp

Ajouter en première ligne de chacun :
```cpp
#include <typeinfo>
```

---

## Compilation

Se placer dans le dossier contenant `T4C Client.sln` :
```
cd "C:\...\CLIENT168_RC14h_OK\1.68RC14h\Client"
```

### Debug
```bat
"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" "T4C Client.sln" ^
  /p:Configuration="Debug" ^
  /p:Platform="x86" ^
  /p:WindowsTargetPlatformVersion="10.0.19041.0" ^
  /p:PlatformToolset=v143 ^
  /p:IncludePath="C:\Program Files (x86)\Windows Kits\10\Include\10.0.19041.0\um;C:\Program Files (x86)\Windows Kits\10\Include\10.0.19041.0\shared;C:\Program Files (x86)\Windows Kits\10\Include\10.0.19041.0\ucrt;$(IncludePath);.\Directx9SDK\Include" ^
  /p:CL_MPCount=8 ^
  /t:Rebuild
```

### Release (recommandé pour les tests)
```bat
"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" "T4C Client.sln" ^
  /p:Configuration="Release" ^
  /p:Platform="x86" ^
  /p:WindowsTargetPlatformVersion="10.0.19041.0" ^
  /p:PlatformToolset=v143 ^
  /p:IncludePath="C:\Program Files (x86)\Windows Kits\10\Include\10.0.19041.0\um;C:\Program Files (x86)\Windows Kits\10\Include\10.0.19041.0\shared;C:\Program Files (x86)\Windows Kits\10\Include\10.0.19041.0\ucrt;$(IncludePath);.\Directx9SDK\Include" ^
  /p:CL_MPCount=8 ^
  /t:Rebuild
```

Le build produit dans `Debug\` (ou `Release\`) :
- `T4C.EXE` — le client
- `T4CLauncher.dll` — le launcher

---

## Assets nécessaires pour lancer le client

Le client a besoin de fichiers de données qui ne sont pas dans ce repo (assets propriétaires). Les copier depuis une installation T4C existante vers le dossier `Debug\` :

```bat
copy "C:\Program Files (x86)\The4thComing\english.elng" ".\Debug\"
copy "C:\Program Files (x86)\The4thComing\englishgui.elng" ".\Debug\"
xcopy "C:\Program Files (x86)\The4thComing\Fonts" ".\Debug\Fonts\" /e /i
xcopy "C:\Program Files (x86)\The4thComing\Game Files" ".\Debug\Game Files\" /e /i
xcopy "C:\Program Files (x86)\The4thComing\fx files" ".\Debug\fx files\" /e /i
```

---

## Résultat du build Windows

Le launcher s'affiche, les champs IP/Login/Password/Connect sont fonctionnels. C'était le checkpoint de départ : valider qu'on a une base saine avant de partir vers Linux/SDL3.

---

## État actuel du portage Linux/SDL3 (mai 2026)

Le repo n'est plus seulement un *fork-qui-compile-sous-VS2022*. On distingue **deux dimensions** :

| Dimension | Question | Statut |
|---|---|---|
| **A — Réseau / protocole** | Le client parle-t-il comme un 1.68 RC14h au serveur Final Step ? | **Oui, flux principal validé** (voir ci-dessous) |
| **B — Jeu visible** | Le client affiche-t-il le monde comme sous Windows (carte, sprites, audio) ? | **En cours** — c'est la priorité maintenant |

### Dimension A — Réseau (socle posé)

- **Build CMake Linux** : `t4c_client`, SDL3 + image + ttf (`CMakeLists.txt`).
- **Écrans SDL3** : `LoginScreen`, `CharacterSelectScreen` (`src/gui/`).
- **Stack réseau** : `T4CLoginSession`, `CommCenter`, shim Linux, log `debug/t4c_network_session.log`.
- **Chiffrement TFC** : `crypt.cpp` mestoph linké tel quel (même binaire logique que Windows).
- **Pipeline auth → sélection → entrée en jeu** (compte `test`, perso `TestPlayer`, serveur Linux Final Step) :

  | Étape | Opcode | Critère observé (client) |
  |---|---|---|
  | Register + version | 14, 99 | `[AUTH] Verdict : ACCEPTE` |
  | Liste persos | 103, 26 | `TestPlayer` affiché |
  | Entrer en jeu | **13** | `RQ_PutPlayerInGame (13) OK — position x,y monde` |
  | Passage in-game | **46**, **60** | envoyés après le 13 ; réponse **18** (ViewBackpack) reçue |

  Le fil conducteur réseau documenté dans le log : **14 → 99 → 26 → 13 → 46 + 60**.

- **Serveur** : le chargement perso (`PutPlayerInGame` async) a nécessité des correctifs dans `T4C_Server_Linux_Final_Step` (hors scope de ce repo client, mais indispensable pour tester). La connexion UDP peut encore expirer si le handler **46** bloque le thread d'analyse — à traiter côté serveur.

### Dimension B — Rendu / assets (prochaine priorité)

- **Code TnC** (mestoph) intégré via `cmake/TncGraphical.cmake` + shim SDL2→SDL3 (`third_party/tnc_sdl3/`).
- **`client_graphical_path_to_follow/decode/`** : laboratoire historique (convert2, `TnC_dev`, GIFs de démo). **Ce n'est pas le chemin produit final** — voir [Contrat d'assets `T4C_DATA`](#contrat-dassets-t4c_data) plus bas.
- **`GameWorldScreen`** : s'ouvre si `T4C_DATA` pointe vers des `sprites/` + `maps/` valides (même layout que la sortie de `convert2`). Sinon message d'erreur explicite (opcode 13 peut être OK quand même).

### Ce qui n'est pas encore « jouer »

- Tuiles / monstres / objets **synchronisés serveur** (opcodes mouvement, `UnitUpdate`, peripheric objects).
- **Audio** Linux (équivalent `GameMusic.cpp` / VSB).
- **Parité UI** complète (`NewInterface/` Windows) — hors scope immédiat.

---

## Contrat d'assets `T4C_DATA`

Le client Windows lit les **Game Files** installés (`*.vsf`, `*.map`, VSB, etc.). Sous Linux on ne lit pas les VSF chiffrés au runtime : on consomme les **produits du pipeline offline**, comme le ferait une install convertie.

### Layout canonique (runtime Linux **et** référence pour SDL3)

```
$T4C_DATA/
  sprites/     ← *.dec (+ id_list.txt utilisé à la conversion)
  maps/        ← *.rmap (ex. worldmap.rmap pour le monde 0)
  sons/        ← *.wav (optionnel, issus du VSB décodé)
  fonts/       ← polices attendues par FontManager (ex. font_trebuchet_12)
  NPCList.txt  ← liste NPC pour NPCManager (démo / tests)
```

Résolution : variable d'environnement **`T4C_DATA`**, sinon chemins relatifs au binaire (`build/data`, etc.) — voir `src/game/TncDataPaths.cpp`.

### Rôle de `client_graphical_path_to_follow/`

| Élément | Rôle | Utilisation long terme |
|---|---|---|
| `decode/` (convert2, mestoph) | **Usine offline** : VSF/VSB/map → `.dec` / `.rmap` / WAV | On **génère** vers `$T4C_DATA`, on ne dépend pas du chemin du repo au runtime |
| `decode/TnC_dev/` | **Prototype** MAPI / NPCManager sous SDL2 | Sources **réutilisées** dans le client via `TncGraphical.cmake` |
| `decode/data/` | Jeu de test déjà converti | Symlink ou copie vers `$T4C_DATA` pour dev |

**Principe :** le comportement SDL3 Linux doit être **le même que DirectX Windows** (mêmes noms de sprites via `id_list.txt`, même logique carte, même enchaînement opcode → écran), avec une couche de présentation SDL3 à la place de DirectDraw. « Mieux » = stabilité, logs, chemins clairs — pas un second pipeline d'assets parallèle.

### Parité Windows ↔ Linux (cible)

| Couche | Windows (référence) | Linux SDL3 (cible) | Statut |
|---|---|---|---|
| Auth / UI login | MFC + `TFCSocket` | `LoginScreen` + `T4CLoginSession` | OK |
| Liste / sélection perso | UI native | `CharacterSelectScreen` | OK |
| Wire TFC | `crypt.cpp` | même `crypt.cpp` | OK |
| Entrée monde (13, 46, 60) | `packethandling.cpp` | `T4CLoginSession` | **13 OK** ; 46/60 envoyés |
| Carte / sprites | CW32Sprite + VSF | TnC + `MAPInterface` + `VSFInterface` | **branché**, qualité dépend de `T4C_DATA` |
| Musique / SFX | DirectSound + VSB | SDL_mixer ou audio SDL3 | à faire |
| Monde réseau (PNJ, loot) | opcodes 69, 16, … | à faire | après rendu local OK |

---

## Boucle temps, rendu SDL3 et synchronisation serveur

Section de référence pour la **Phase 2** (mouvement, entités réseau). À ne pas confondre avec les **WDA** : le client ne les lit pas ; la cohérence en jeu passe par le **protocole TFC**, pas par recopier la grille serveur côté SDL3.

### Trois couches indépendantes (ne pas les fusionner)

| Couche | Où | Rôle en jeu |
|---|---|---|
| **WDA** | Serveur (`T4C Worlds.WDA`, BMP Storm → binaire) | Grille logique 3072×3072 : collision, zones, PNJ, scripts — **vérité serveur** |
| **`.rmap` + `.dec`** | Client (`$T4C_DATA/maps`, `sprites`) | **Affichage** : quel sprite dessiner par case — dérivé des `.map` client via convert2, pas des WDA |
| **TFC (UDP)** | Client ↔ serveur | Positions, `UnitUpdate`, refus de move — **seule synchro runtime** entre les deux binaires |

Aligner **offline** carte affichée et monde serveur (même index de monde, conversion map correcte) est du QA / pipeline, pas une boucle `SDL_RenderPresent`. Un fixed timestep ne corrige pas un `.rmap` incohérent avec le BMP WDA.

### Horloge serveur (référence pour le client)

Le serveur ne tourne pas aux FPS SDL. Il utilise `TFCMAIN::GetRound()` ; dans `TFCTime.h`, `#define SECONDS * 20` → **1 round ≈ 50 ms** (20 rounds/s). Les exhausts mouvement/attaque et l’acceptation des paquets **Move (1–8)** sont exprimés en rounds (`Character.cpp`, `TFCMessagesHandler.cpp`), pas en images par seconde.

Le client Windows d’origine n’envoie pas un move à chaque frame DirectX : il réagit aux entrées et aux réponses serveur. Le port SDL3 doit reproduire ce **comportement observable**, pas inventer une simulation locale plus rapide que le serveur.

### État actuel du client Linux

`GameWorldScreen::Update()` ne fait que **redessiner** la carte à la position reçue dans l’**opcode 13** ; il n’y a pas encore de simulation de déplacement ni de prédiction. Le risque « 144 FPS = marche 2× plus vite » n’existe **pas tant qu’on n’ajoute pas** de logique move locale.

### Règles pour la Phase 2 (mouvement + rendu)

**A. Séparer synchro logique et rendu**

- **Chaque frame SDL** : poll événements + traiter les paquets réseau reçus (`T4CLoginSessionPollBackgroundTasks` ou file dédiée) → mettre à jour l’état « autorité serveur » (position, unités visibles).
- **Ne pas** avancer la position affichée uniquement parce que `SDL_RenderPresent` a tourné ; la position **faisant foi** est celle confirmée ou corrigée par le serveur.

**B. Fixed timestep (quand il y aura prédiction / animation)**

Quand on interpolera le perso entre deux positions serveur :

```
accumuler dt (SDL_GetTicks)
tant que accum >= 50 ms :
    mise à jour état client (timers UI, file d’inputs à envoyer)
    accum -= 50 ms
alpha = accum / 50 ms
rendu : interpoler(pos_précédente, pos_serveur, alpha)
SDL_RenderPresent()  // framerate libre
```

L’interpolation est du **confort visuel** ; en cas d’écart, le paquet serveur gagne (rubber-band), comme sur le client Windows.

**C. Ne pas spammer le réseau**

- Ne pas envoyer un opcode Move **à chaque** tick logique 50 ms sans garde — respecter les exhausts côté serveur et le rythme du client d’origine (input / séquence de marche).
- La collision « case bloquée » est **côté serveur** (WDA) ; le client affiche et propose un move ; le serveur accepte ou refuse.

**D. Pas de lecture WDA sous SDL3**

Inutile et hors scope : pas de `T4C Worlds.WDA` dans le client. Pour « ne pas dériver » : opcodes + `$T4C_DATA` aligné + logs ; pas un second moteur physique client calqué sur le BMP Storm.

### Fichiers cibles (implémentation future)

| Fichier | Rôle prévu |
|---|---|
| `src/game/GameWorldScreen.cpp` | Boucle rendu + input move → enqueue paquets |
| `src/network/T4CLoginSession.cpp` | Handlers opcodes 1–8, 69, 16, 60 — mise à jour état monde |
| `CLIENT168_RC14h_OK/.../packethandling.cpp` | Référence comportement Windows (quand envoyer quoi) |

---

## Trois formats, trois couches qui ne se touchent presque pas

Avant de parler roadmap, il faut nommer correctement les trois formats qui circulent dans ce projet. Ils sont régulièrement confondus (y compris dans certaines anciennes notes), mais ils vivent dans des couches indépendantes.

| Format | Côté | À quoi ça sert | Chiffrement |
|---|---|---|---|
| **WDA** (`T4C Worlds.WDA`, `T4C Edit.WDA`, `NPCs.WDA`) | **Serveur uniquement** | Persistance des données monde (sorts, objets, créatures, cartes, scripts NPC). Lu au boot du serveur. | XOR 3418 octets — clé **statique** en 1.61 (Havoc), clé **LCG runtime** en 1.68 (seed 23422). |
| **VSF** (`t4cgamefile*.vsf`) | **Client uniquement** | Sprites bitmap palettisés (graphismes, animations). Lu au lancement. | XOR via `vsfkey.h` (mestoph). |
| **VSB** (`T4CGameFile.VSB`) | **Client uniquement** | Banque audio : musiques et effets sonores. Lu au lancement, streamé à la demande via `DatabaseLoadVSB()`. | XOR via `vsfkey.h` (mestoph, même pipeline que VSF). |
| **TFC** (protocole filaire UDP) | **Client ↔ Serveur** | Échange temps réel : auth, liste persos, monde, mouvements, combats… | XOR streaming `TFCCrypt::EncryptS/DecryptS` via `CryptMestoph/crypt.cpp`, **clé indépendante** (≠ WDA, ≠ VSF/VSB). |

Conséquence concrète, vérifiée dans le code :

```bash
$ rg -l 'WDA|\.wda' CLIENT168_RC14h_OK/   # → aucun résultat
```

Le client 1.68 **n'ouvre jamais** un fichier WDA. Il reçoit tout son contenu monde via le protocole TFC, opcode par opcode. Donc :

- Notre client Linux/SDL3 **n'a aucun travail à faire sur les WDA**. C'est exclusivement la responsabilité du serveur (Final Step, ou n'importe quel autre serveur 1.68 compatible).
- Inversement le serveur ne sait rien des VSF/VSB. Si le client affiche un mauvais sprite, c'est un problème de pipeline mestoph, pas un problème serveur.
- Le seul contrat client↔serveur, c'est `PacketTypes.h` (la liste des opcodes RQ_*) + le format de framing TFC.

## Précisions sur les WDA (compréhension fine du format)

Pour qu'on parle tous le même langage quand on touchera au serveur :

### Trois clés XOR à ne pas mélanger

Confusion classique : "le XOR est statique partagé". Vrai pour TFC (réseau), **faux entre versions WDA**.

1. **WDA Havoc 1.61** — clé statique hard-codée de 3418 octets, listée en tête de `Format du fichier T4C Worlds.wda.txt`. Premier byte `0xE1`.
2. **WDA T4C 1.68** — clé **générée à runtime** par `WDAFile.cpp` du serveur Final Step :
   ```cpp
   Random rnd;
   rnd.SetSeed( 23422 );
   for( i = 0; i < 3418; i++ ) pbRandom[i] = rnd( 0, 255 );
   ```
   Reproduit fidèlement par `makewda.py` (dépôt serveur Final Step, même LCG que `key_168.py`) :
   ```python
   seed = 23422
   for _ in range(3418):
       seed = (seed * 725472321 + 1) & 0xFFFFFFFFFFFFFFFF
       key.append((seed >> 16) % 256)
   ```
3. **TFC réseau** — encore une autre clé/algo (`crypt.cpp` + `xorkey.h` de mestoph), totalement indépendante des deux précédentes.

**Buffer size 3418 octets identique** entre les versions WDA — c'est la même mécanique, juste une clé différente. Copier bêtement un WDA Havoc 1.61 sur un serveur 1.68 ne fonctionnera jamais : décrypté avec la mauvaise clé, le serveur lit du bruit et rejette le header.

### Header WDA — inchangé depuis 1.61

`WDAHeader.cpp` du serveur Final Step :

```cpp
#define TAG_VALUE 0x0CA7
void WDAHeader::SaveTo( WDAFile &wdaFile, bool isReadOnly ) {
    wdaFile.Write( (WORD)TAG_VALUE );    // tag
    wdaFile.Write( (WORD)1 );            // version
    wdaFile.Write( isReadOnly );         // bool
}
```

→ Cinq octets en sortie chiffrée : `A7 0C 01 00 [00|01]`. Strictement identique à la spec mestoph 1.61 (« `0xA7, 0x0C, 0x01, 0x00, 0x01` »). C'est aussi exactement ce que produit `makewda.py` avec `struct.pack('<HHB', 0x0CA7, 1, 0)`.

### La structure binaire — source de vérité

**Ne pas se fier** aux commentaires `# Champs Patch 1.62` ou `# Patch 1.62` qui traînent dans certains scripts Python du repo serveur Final Step (`tools/full_convert_final.py` et ses semblables). Ces scripts sont des **clones ratés** : des explorations itératives pour essayer de faire démarrer le serveur avant que `makewda.py` ne soit trouvé comme solution. Ils contiennent des spéculations sur la structure binaire qui se sont avérées fausses ou incomplètes.

La **seule source de vérité** sur ce que le serveur 1.68 lit réellement est dans les fichiers `WDA*.cpp` du serveur Final Step (`WDAObjects.cpp`, `WDAWorlds.cpp`, `WDASpells.cpp`, `WDACreatures.cpp`, `WDAHives.cpp`, `WDAClans.cpp`, `WDAAreaLinks.cpp`). Ce sont eux qui définissent le parsing byte à byte.

### Les WDA sont-ils impératifs ?

**Oui et non, selon l'objectif :**

| Objectif | WDA nécessaires | Niveau de peuplement |
|---|---|---|
| Faire booter le serveur Final Step 1.68 | Oui | **Prouvé** : `makewda.py` — deux fichiers minimalistes (~44 octets chacun), toutes sections à zéro, **clé XOR 1.68 correcte** |
| Jouer dans un monde vide (marcher, se déplacer) | Oui | Même chose que ci-dessus (monde serveur vide ; le rendu carte côté client vient des VSF/rmap, pas des WDA) |
| Jouer dans un vrai monde (créatures, objets, téléporteurs, cartes serveur) | Oui | **Non résolu** : les WDA Havoc compilés (rar 1.61, ~24 Mo) **ne font pas démarrer / ne fonctionnent pas** tels quels sur le serveur 1.68 — même après simple copie ou key-swap (hypothèse non validée) |

**Fait d'expérience (à ne pas inverser avec la théorie)** : ce qui fait **démarrer** le serveur aujourd'hui, ce n'est pas le WDA Havoc du rar, c'est **`makewda.py`**. Ce script génère `WDA/T4C Worlds.WDA` et `WDA/T4C Edit.WDA` avec le même squelette binaire vide et le chiffrement 1.68 attendu par `WDAFile.cpp`. Le monde est vide, mais le parseur 1.68 accepte le fichier.

Pour l'**étape « jouer dans un vrai monde »**, il reste à **réinjecter** le contenu Havoc dans un format que le parseur 1.68 accepte — ce n'est pas acquis avec « copier le binaire + changer la clé » seul.

### Comment les WDA Havoc sont-ils stockés — la découverte clé

Le `.txt` décompilé (125 835 lignes pour `T4C Worlds.txt`, 4.3 Mo) **ne contient pas** les données de tuiles. La section `[maps]` ne fait que référencer des BMP externes :

```
[maps]
0	3072,3072	"Realms of Oblivion"	"Realms of Oblivion.bmp"
1	3072,3072	"Dungeons"	"Dungeons.bmp"
```

Ces BMP (un par carte, ~4.7 Mo chacun à 3072×3072 × 4 bits) contiennent les codes de couleur terrain. C'est `wc.c` qui les lit au moment de la compilation pour produire le WDA binaire. Les BMP ne sont pas archivés dans le rar Havoc.

### Les BMP WDA ne sont ni les cartes client, ni des VSF

Trois représentations du « terrain », trois usages — **pas les mêmes fichiers** :

| Représentation | Format | Où | Rôle |
|---|---|---|---|
| **Serveur / WDA** | BMP 16 couleurs 4 bpp non compressé (`WriteMap` dans `havoc2/common.h`) | Chaîne Storm → `wc` → `T4C Worlds.WDA` | **Logique serveur** : code couleur par case (même palette que l’éditeur Storm / ChaotikMind), collision, zones, téléporteurs côté monde |
| **Client affichage** | `*.map` chiffrés dans `Game Files/` → pipeline mestoph → `*.rmap` | `client_graphical_path_to_follow/decode/` (`extraction_maps.cpp`, `extraction_maps_conv.cpp`) | **Rendu** : chaque case pointe vers un **sprite** (nom via `id_list.txt`), pas vers un BMP terrain |
| **Sprites** | `*.vsf` → `*.dec` | VSF / TnC | **Graphismes** des tuiles, persos, objets — ce que `MAPInterface` dessine ; ce n’est **pas** la source des BMP Storm |

- Les BMP Havoc **ne sont pas** extraits des `.map` client ni des VSF.
- Les `.map` client **ne sont pas** lus par `wc` ni par le serveur WDA.
- Les VSF contiennent les **images** ; les `.rmap` disent **quel sprite** afficher ; les BMP/WDA disent **quel code terrain** le serveur utilise (souvent la même grille 3072×3072, sémantique différente).

Le client 1.68 charge aussi des fichiers monde binaires propriétaires (`Tileset::LoadVirtualMap` → `WorldFileName[]`, chunks indexés) — encore un format filaire client, distinct du BMP Storm et du `.rmap` mestoph.

En revanche, le **WDA binaire compilé est directement présent** dans le rar :

```
Wda Serveur 1.61 Havoc/T4C Worlds.WDA     24 565 968 bytes  (clé 1.61)
Wda Serveur 1.61 Havoc/T4C Edit.WDA          115 488 bytes  (clé 1.61)
Wda Serveur 1.61 Havoc/NPCs.WDA               54 034 bytes  (clé 1.61)
```

Ces binaires contiennent toutes les données (y compris les tuiles de carte compilées). Il n'y a pas besoin de les recompiler depuis le `.txt` — ils existent déjà.

### `makewda.py` — ce qui marche aujourd'hui (boot serveur)

Script `makewda.py` dans le dépôt serveur Final Step — écrit `WDA/T4C Worlds.WDA` et `WDA/T4C Edit.WDA` dans le répertoire courant.

Contenu attendu au boot :

- header `0x0CA7`, version 1 ;
- compteurs de sections à **0** (sorts, cartes, objets, créatures, téléporteurs, clans, etc.) ;
- chiffrement XOR **1.68** (LCG seed 23422, 3418 octets) — identique à `WDAFile.cpp`.

Ce n'est **pas** une conversion du Havoc : c'est un **stub vide** qui satisfait le parseur pour **démarrer**. Tous les scripts ratés dans `tools/` du repo serveur étaient des tentatives avant cette solution.

### Plan WDA Havoc — key-swap 1.61 → 1.68

**Outil implémenté** : dossier [`key_swaps/`](key_swaps/) (`keyswap_wda.py`, `python3 test_keyswap.py`, `./install_to_build.sh`). Les tests automatisés confirment : round-trip OK, header `0x0CA7`, **761 + 17 sorts** parsables après swap. Déployer les WDA swappés dans `T4C_Server_Linux_Final_Step/build/WDA/`.

**Crash `WDAFile::Read` / `dwSize=2782684579` sur « Loading spells »** : le serveur lit des WDA **Havoc 1.61 non swappés** (md5 `c2d598…` comme `tiforci/havoc2/`). `./build.sh` peut écraser `build/WDA/` : après chaque build, `key_swaps/install_to_build.sh`, puis `./T4CServer` depuis `build/`.

**Attention** : un header XOR correct **ne garantit pas** que le serveur 1.68 parse toutes les sections Havoc (objets, cartes…). Si le boot échoue après les sorts, tester `makewda.py` ou étendre `probe_wda_parse.py`.

Si le blocage n'était **que** la clé XOR, le plan serait :

**Étape W1 — Extraire les binaires du rar** (une seule fois, dans un dossier dédié hors du repo client, par exemple `../../tiforci/havoc2_extracted/`) :

```bash
cd ../../tiforci/havoc2
unrar e "Fichiers WDA 1.61a (provenant du serveur Havoc).rar" \
  "Wda Serveur 1.61 Havoc/T4C Worlds.WDA" \
  "Wda Serveur 1.61 Havoc/T4C Edit.WDA" \
  "Wda Serveur 1.61 Havoc/NPCs.WDA" \
  ../havoc2_extracted/
```

**Étape W2 — Écrire un outil key-swap** (Python, ~20 lignes) dans le repo serveur Final Step :

```python
# keyswap_wda.py : passe un binaire WDA de la clé 1.61 à la clé 1.68
import sys, struct

def key_161():
    """Clé statique 1.61 de Sorkvild (premier byte 0xE1...)"""
    return bytes([0xE1,0xC4,0x9B,0x1B,0xC0,...])  # 3418 bytes

def key_168():
    """Clé générée par le serveur 1.68 (seed 23422, LCG)"""
    seed = 23422; key = []
    for _ in range(3418):
        seed = (seed * 725472321 + 1) & 0xFFFFFFFFFFFFFFFF
        key.append((seed >> 16) % 256)
    return bytes(key)

src = open(sys.argv[1],'rb').read()
k61 = key_161(); k68 = key_168()
out = bytes(b ^ k61[i%3418] ^ k68[i%3418] for i,b in enumerate(src))
open(sys.argv[2],'wb').write(out)
```

L'astuce : `b ^ k61 ^ k68` = déchiffre 1.61 + rechiffre 1.68 en un seul pass.

**Étape W3 — Test sur le serveur Final Step.** Si la structure binaire 1.61 est directement lisible par le serveur 1.68, le monde est opérationnel. Si le serveur lève une exception `WDAFileException(EndOfFile)` sur un champ manquant, on identifie lequel (depuis `WDAObjects.cpp` etc.) et on corrige. C'est empirique et localisé — pas une réécriture aveugle.

**Note** : `wc.c` et `wd.c` de Sorkvild restent utiles pour **inspecter** (`wd`) le Havoc ou **recompiler** (`wc`) — mais `wc` exige les BMP ; et produire un WDA 1.61 avec `wc` ne garantit pas qu'il sera lisible par le parseur **1.68** sans travail de structure supplémentaire.

### WDA : que faire selon l'objectif ?

| Objectif | Quoi utiliser | Statut |
|---|---|---|
| Serveur qui **boot** | `makewda.py` | **OK** (expérience projet) |
| Monde **peuplé** (Havoc) | WDA du rar 1.61 (+ éventuellement key-swap, ou conversion champ par champ via `WDA*.cpp`) | **Pas OK** aujourd'hui |
| Recompiler depuis `.txt` | `wc` + BMP manquants dans le rar | Piste lourde, format 1.61 |

**En résumé** : ne pas confondre « faire démarrer le serveur » (`makewda.py`, vide) et « avoir le monde Havoc » (WDA compilé du rar — **pas** validé sur 1.68). Les BMP ne sont **pas** une dépendance runtime du serveur ; ils n'entrent en jeu que si tu repasses par `wc` pour **fabriquer** un nouveau binaire depuis le `.txt`.

## Système audio — VSB, pas WDA

### La musique n'a rien à voir avec les WDA

C'est une confusion possible : les WDA définissent le monde, les zones, les objets. On pourrait imaginer qu'ils encodent aussi le fait que "la zone X joue la musique Y". Ce n'est pas le cas.

La **totalité de la logique musicale est dans le client**, hardcodée dans `GameMusic.cpp`. Le serveur envoie uniquement un numéro de monde (`Player.World`) et des coordonnées — le client déduit la musique seul.

### Écran de sélection de personnage

`GameMusic::Start()` est appelée quand le client bascule en mode "character selection" (entre la réponse d'auth et l'entrée in-game). Elle charge inconditionnellement :

```cpp
t3Music.Create("Sadness Music", TS_STREAMING);  // GameMusic.cpp:107
```

La piste `"Sadness Music"` est une chaîne résolue dans le VSB via `DatabaseLoadVSB()`. C'est la musique mélancolique qu'on entend sur l'écran de liste des personnages. Rien à voir avec le serveur ou les WDA.

### Musique en jeu et changements de zone

À chaque changement de zone (opcode `RQ_PutPlayerInGame=13`), `packethandling.cpp` appelle `g_GameMusic.LoadNewSound()`. Cette fonction lit `Player.World` et `Player.xPos/yPos` et sélectionne une piste parmi 8 constantes :

```cpp
// GameMusic.cpp — constantes internes
static const int BOSS_MUSIC    = 0;   // "Boss Music"
static const int OUTDOORS_MUSIC = 1;  // "Outdoors Music"
static const int FOREST_MUSIC  = 2;   // "Forest Music"
static const int DUNGEON_MUSIC = 3;   // "Dungeons Music"
static const int CAVERN_MUSIC  = 4;   // "Caverns Music"
static const int SADNESS_MUSIC = 5;   // "Sadness Music"
static const int SILENCE_MUSIC = 6;   // (silence)
static const int NOISES_MUSIC  = 7;   // "Noises Music"
```

La règle de base :

| `Player.World` | Musique par défaut |
|---|---|
| 0 — `MAIN_WORLD` | `FOREST_MUSIC` |
| 1 — `DUNGEON_WORLD` | `DUNGEON_MUSIC` |
| 2 — `CAVERN_WORLD` | `CAVERN_MUSIC` |

Des macros `Track45(a,b,c,d)` et `Track90(a,b,c,d)` définissent ensuite des sous-zones par coordonnées, qui peuvent surcharger la règle (par exemple une zone boss dans `MAIN_WORLD` passera en `BOSS_MUSIC`, une zone en extérieur dans un monde caverne passera en `OUTDOORS_MUSIC`, etc.). Ce mapping est entièrement local au client — le serveur n'en sait rien.

La transition se fait proprement : si `dwMusicNumber != dwOldMusicNumber`, le client arrête la piste en cours, libère le buffer, charge la nouvelle et la lance en streaming.

### Pipeline audio — VSB vers SDL3

Côté fichier, toutes ces pistes vivent dans `T4CGameFile.VSB`. Le pipeline mestoph les a déjà extraites en WAV dans `client_graphical_path_to_follow/decode/data/sons/` :

```
Caverns Music.wav    Dungeons Music.wav   Forest Music.wav
Outdoors Music.wav   Boss Music.wav       Sadness Music.wav
Noises Music.wav     + dizaines d'effets sonores
```

Le runtime Windows utilise DirectSound (`T3VSBSound` + `sbBuffer`). Pour Linux/SDL3, ces appels DirectSound doivent être remplacés par `SDL_mixer` ou l'API audio SDL3 native. C'est un travail **isolable** : `NewSound.cpp` + `GameMusic.cpp`, sans toucher au protocole réseau ni aux assets VSF/WDA.

### Ce que ça implique pour la roadmap

Voir **« Roadmap finale »** ci-dessous : l’audio est explicitement raccroché aux étapes TFC (liste persos → entrée in-game) et aux travaux transverses. En une phrase : musique **non bloquante** pour auth/MOTD ; souhaitable dès l’écran liste persos (`Sadness Music`) puis obligatoire pour une expérience proche du client Windows après opcode **13** (`LoadNewSound`).

## Alignement de versions vérifié

- **Client** : 1.68 RC14h (historique précis dans `CLIENT168_RC14h_OK/1.68RC14h/BL_MODIF_CLIENT.txt` → `165b5 → 165b6 → 165RC10 → 165RC11 → 168RC14 → 168RC14F → 168RCH`). Opcode le plus récent : `RQ_GetPvpRanking=131` marqué `// asteryth ajout`.
- **Serveur** : Final Step cible explicitement *"v1.68 Linux client"* (commit `693ace3` : *« stable connection with the v1.68 Linux client »*).
- **Verdict** : alignement parfait. Pas de problème de version entre les deux côtés.

## Roadmap finale — jouer à T4C 1.68 sous Linux/SDL3 dans un flux C↔S ininterrompu et incorruptible

### Objectif

Avoir un client Linux/SDL3 capable de se connecter à un serveur 1.68 **inchangé**, traverser tout le pipeline (auth → liste persos → entrée in-game → rendu monde temps réel), tout en gardant le build Windows/DirectX intact derrière `#ifdef LINUX_PORT`.

### Cinq briques, cinq origines

| Brique | Origine | Statut | Choix |
|---|---|---|---|
| Protocole filaire + chiffrement TFC | code Vircom + `CryptMestoph` (mestoph) | branché, handshake OK | **réutilisé tel quel** |
| Pipeline graphique offline (sprites / maps) | `client_graphical_path_to_follow/decode/` (mestoph) | utilisable, sorties `*.dec` + `*.rmap` déjà produites | **réutilisé** |
| Pipeline graphique runtime (TnC) | `client_graphical_path_to_follow/decode/TnC_dev/` (mestoph) | intégré via `TncGraphical.cmake` + shim | **réutilisé sous SDL3** |
| Audio (musiques / SFX) | `T4CGameFile.VSB` + WAV décodés (`decode/data/sons/`) | Windows : DirectSound (`NewSound.cpp`, `GameMusic.cpp`) ; Linux : **à brancher** | **même contenu**, couche lecture réécrite (SDL_mixer ou audio SDL3) |
| Données serveur (WDA) | `makewda.py` (boot) ; Havoc + Sorkvild `wc`/`wd` (monde plein, **à conquérir**) | boot : **OK** via `makewda.py` ; Havoc : **non validé** sur 1.68 | stub vide **réutilisé** ; contenu Havoc **en recherche** |

### Détail des briques

**1. Réseau et chiffrement (réutilisés)**

On garde le protocole 1.68 et `TFCCrypt::EncryptS/DecryptS` (`crypt.cpp` + `xorkey.h` de mestoph). Réécrire le wire format casserait la compatibilité avec tout serveur 1.68.

**Fait (mai 2026)** : auth **14/99**, liste **26/103**, entrée **13**, envoi **46+60** après OK sur le 13. Prochain travail réseau **côté client** : handlers opcodes monde (mouvement, unités, objets proches) — pas re-refaire le handshake.

**Hors repo client** : stabilité serveur sur `PutPlayerInGame` / **46** (pas de blocage thread UDP, pas de timeout 15 s pendant le chargement).

**2. Décodage offline des assets (mestoph)**

Source : `client_graphical_path_to_follow/decode/`. Le flux :

```
*.vsf chiffrés ─┐
                ├─► decode XOR (vsfkey.h) ─► convertisseur ─► *.dec (sprites bitmap)
id_list.txt    ─┘                                              *.rmap (cartes décodées)
```

Les `*.dec` et `*.rmap` sont produits par `convert2` (souvent sous `client_graphical_path_to_follow/decode/data/` en dev). **Au runtime on pointe `$T4C_DATA`**, pas le repo graphique : copie, symlink, ou `export T4C_DATA=.../decode/data` le temps du dev. Le symlink `build/data` (CMake `POST_BUILD`) n'est qu'un raccourci de confort.

**3. Runtime graphique TnC (mestoph, porté SDL3)**

`VSFInterface` charge les `*.dec` en cache `SDL_Texture`, `MAPInterface` rend les tuiles, `NPCManager` gère les sprites animés, `FontManager` / `TextManager` font le texte. Tout est intégré tel quel via `cmake/TncGraphical.cmake`, le shim `third_party/tnc_sdl3/include/SDL/SDL.h` et `tnc_sdl2_compat.h` mappent l'API SDL2 attendue par le code de mestoph vers SDL3.

**4. Audio client (VSB — hors WDA, hors TFC)**

La musique ne transite pas par le réseau ni par les WDA : le client choisit les pistes dans `GameMusic.cpp` selon l’écran (ex. `"Sadness Music"` sur la liste des persos) puis monde + coordonnées après opcode **13** (cf. section **« Système audio — VSB, pas WDA »**). Le port Linux remplace DirectSound par SDL ; les fichiers peuvent partir des `.wav` déjà extraits du pipeline mestoph.

**5. WDA serveur (Sorkvild)**

Le format WDA était propriétaire Vircom, mais le dump `../../tiforci/havoc2/` contient les sources C de **Sorkvild** :

- `wc.c` (~111 Ko) — *WDA compiler* : `txt` → `.WDA` (avec option de protection)
- `wd.c` (~85 Ko) — *WDA decompiler* : `.WDA` → `.txt`
- `common.h`, `safe-malloc.h`, `inifile.c` — utilitaires partagés

Ces sources sont du C ANSI portable (entête `by Sorkvild`, mail historique `sorkvild@uboot.com`), pas de dépendance Windows visible. Recompilation Linux triviale via `gcc wc.c inifile.c -o wc` (et idem pour `wd`).

WDA pertinents pour nous :

- `T4C Worlds.WDA` (~24 Mo) — monde compilé, terrain + objets + spawn
- `T4C Edit.WDA` — données d'édition (objets, sorts, créatures, dialogues)
- `NPCs.WDA` + `Format du fichier NPCs.wda.txt` — scripts d'instructions PNJ

À quoi ça sert dans la roadmap : (a) **`makewda.py`** pour toute session serveur qui doit démarrer ; (b) **`wd`** pour lire le Havoc et comprendre la sémantique ; (c) **prochaine brique ouverte** : faire accepter le contenu Havoc (ou une partie) au parseur 1.68 — key-swap, migration champ par champ depuis `WDA*.cpp`, ou autre — ce n'est pas encore fait.

### Architecture cible

```
=== OFFLINE ===
  *.vsf  + id_list.txt  ─► decode XOR ─► convertisseur ─► *.dec / *.rmap
  *.WDA                  ─► wd (Sorkvild) ─► *.txt éditables
  *.txt                  ─► wc (Sorkvild) ─► *.WDA

=== RUNTIME WINDOWS (existant, intouché) ===
  T4C.EXE : CW32Sprite / DirectDraw ──► Present

=== RUNTIME LINUX / SDL3 (cible) ===
  ┌────────────────────────────────────────────┐
  │ LoginScreen SDL3                           │
  │      │                                     │
  │ T4CLoginSession  ◄──► CommCenter (TFC)     │
  │      │                                     │
  │ VSFInterface (+ cache SDL_Texture)         │
  │      │                                     │
  │ MAPInterface ──► WorldRenderer             │
  │      │                                     │
  │ SDL_mixer / audio SDL3 ◄── WAV (sons/)     │
  │      │      (équivalent GameMusic.cpp)     │
  │ SDL_RenderTexture / SDL_RenderPresent      │
  └────────────────────────────────────────────┘
```

### Étapes ordonnées — deux phases

Chaque étape a un critère d'acceptation **observable** (log client `debug/t4c_network_session.log` et/ou écran).

#### Phase 1 — Connexion et entrée en jeu (réseau + écrans SDL3)

```
[✓] Build Linux + Windows (VS2022) inchangé côté #ifdef
[✓] Étape 0 — Compte test + serveur qui répond (conf ops / SQL / patches serveur)
[✓] Étape 1 — 14 + 99  → [AUTH] Verdict : ACCEPTE
[✓] Étape 2 — 26 + 103 → liste persos à l'écran (CharacterSelect)
[✓] Étape 3a — opcode 13 → position monde reçue (0x000D, pas confondre avec 0x0013 = ViewEquiped)
[~] Étape 3b — 46 + 60 envoyés ; GameWorldScreen si T4C_DATA OK ; serveur : handler 46 sans blocage
[ ] Étape 1 bis (optionnel) — 65/66 MOTD affiché (cosmétique)
[ ] Audio liste persos — "Sadness Music" (VSB → SDL), non bloquant
```

#### Phase 2 — Parité rendu et monde réseau (priorité Linux, référence DirectX)

C'est la **nouvelle dimension** : même ressenti visuel/sonore que Windows, puis mêmes opcodes « monde ». Voir [Boucle temps, rendu SDL3 et synchronisation serveur](#boucle-temps-rendu-sdl3-et-synchronisation-serveur) pour les règles fixed timestep / WDA vs TFC avant d’implémenter le mouvement.

```
[ ] B0 — Documenter / figer $T4C_DATA (copie depuis convert2 ou install Rebirth convertie)
[ ] B1 — GameWorldScreen : carte visible à la position du 13 (MAPInterface + bons .rmap)
[ ] B2 — Sprites / calques (VSFInterface) alignés id_list — pas seulement la démo TnC
[ ] B3 — Audio : GameMusic / zone (VSB ou WAV sous $T4C_DATA/sons/)
[ ] B4 — Opcode 9 (GetPlayerPos) + synchro caméra
[ ] B5 — Mouvements 1–8 + UnitUpdate (69)
[ ] B6 — Peripheric objects (16) + near items (60) — entités autour du perso

           ─────► Critère « ON JOUE » : carte + perso qui bouge + entités visibles + son de zone

[Phase 3] combat, chat, sorts, UI NewInterface — incrémental, opcode par opcode
```

**Règle de travail :** toute nouvelle feature **client Linux** vit dans `src/` + `#ifdef LINUX_PORT` ; le chemin Windows (`CLIENT168_RC14h_OK/`, DirectX) reste compilable. Quand on porte une brique (ex. musique), on lit d'abord le comportement Windows (`GameMusic.cpp`, `packethandling.cpp`) puis on reproduit sous SDL3.

### Travaux transverses (en parallèle des étapes ci-dessus)

- **Non-régression Windows** : tout le code Linux est isolé par `#ifdef LINUX_PORT` (`add_compile_definitions(LINUX_PORT)` dans `CMakeLists.txt` L60). Le build VS2022 doit continuer à passer à chaque PR.
- **CI Linux** : ajouter un job qui build `t4c_client` + lance un smoke test sur le handshake (mock UDP rejouant la trace de `debug/t4c_network_session.log`).
- **Audio Linux** : porter `NewSound.cpp` / `GameMusic.cpp` (DirectSound → SDL_mixer ou équivalent SDL3) ; fichiers sources les `.wav` de `client_graphical_path_to_follow/decode/data/sons/` avec les **mêmes noms de pistes** que dans le VSB (`"Sadness Music"`, `"Forest Music"`, …). Peut démarrer dès l’étape 2, doit être satisfaisant avant une démo « jeu complet ».
- **Outils Sorkvild Linux** (optionnel, utile pour la rétro-ing) : recompiler `wc` / `wd` sous Linux (`gcc wc.c inifile.c -o wc`).
- **Aucun travail WDA côté client** : c'est explicitement hors scope client. Si le serveur a un problème de WDA, ça se règle dans `T4C_Server_Linux_Final_Step`, pas ici.

### Ce que ce README ne détaille pas encore (suite probable)

- **Patches serveur** : chargement perso async, timeout UDP, handler opcode **46** — dépôt `T4C_Server_Linux_Final_Step`, pas ce client.
- **Persistance et ops** : SQL / comptes, sessions « already logged », administration serveur.
- **Launcher** : `T4CLauncher` (MFC) vs lancement direct de `t4c_client` sous Linux.
- **Option CD audio** : le client original peut préférer le CD (`bUseCD` dans les options) — chemin à trancher ou ignorer sur Linux.
- **Surface opcode complète** après « on joue » : chat, combat détaillé, sorts, guildes, banque, commerce, etc. (la liste est dans `PacketTypes.h`).
- **Parité UI** : tout l’arbre `NewInterface/` (centaines de `.cpp`) — priorisation au cas par cas.
- **Packaging Linux** : dépendances SDL, chemins `data/`, distribution binaire.
- **WDA monde plein** : le Havoc 1.61 **ne remplace pas** `makewda.py` pour le boot — c’est un chantier à part (key-swap et/ou différences de structure 1.61 vs 1.68, à diagnostiquer sur le serveur quand on retente le rar).

### Principe de non-réécriture

On a fait le choix explicite de **ne rien réécrire de ce qui marche déjà** :

- Pas de nouveau protocole : le serveur 1.68 est la vérité, on parle son langage.
- Pas de nouveau chiffrement : `crypt.cpp` de mestoph est partagé entre Windows et Linux, un seul code source.
- Pas de nouveau format d'assets : on consomme les `.dec` / `.rmap` produits par le pipeline mestoph, et on garde les `.WDA` lisibles via `wd` Sorkvild.
- Seules les couches **présentation** sont réécrites — graphiques en SDL3 (DirectDraw / CW32Sprite non portables) et **audio** (DirectSound → SDL), sans changer les noms logiques des pistes ni le protocole.

### Crédits

- **Sorkvild** — outils `wc` / `wd` (compilateur / décompilateur WDA).
- **mestoph** (project TnC, http://tnc.sourceforge.net/) — décryption VSF/VSB, pipeline offline, runtime graphique TnC_dev.
- **melodiass / elestranobaron** — source 1.68 RC14h.
- **Vircom** — design original de T4C et du format WDA.
