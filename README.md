# T4C Client 1.68 RC14h — Build sous Windows avec VS2022

## Contexte et objectif

Le but de départ était simple et con : **tester si le client T4C 1.68 se connecte à un serveur 1.68 qui tourne sous Linux**, sans avoir à revenir sous Windows, télécharger 7 Go de Visual Studio, et se battre avec un code source vieux de 20 ans.

La destination finale est de porter le client sous Linux, voire en SDL3. Mais avant de se lancer dans ce chantier, autant vérifier que la base compile et que la connexion fonctionne. C'est exactement ce que ce repo documente.

Le code source vient du repo de melodiass/elestranobaron (actuellement sans repo public). Le client installé dont sont tirés les assets de test est **The 4th Coming :: Rebirth** (installation locale, dossier `C:\Program Files (x86)\The4thComing`), qui contient apparemment des assets de plusieurs serveurs privés (Neerya, Realmud, Abomination, Saga...). On ne sait plus trop d'où ça sort — c'est le folklore T4C.

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

Le portage est déjà bien avancé, le repo n'est plus seulement un *fork-qui-compile-sous-VS2022* :

- **Build CMake Linux fonctionnel** (`CMakeLists.txt` à la racine, cible `t4c_client`, SDL3 + SDL3_image + SDL3_ttf via `find_package` ou `FetchContent`).
- **LoginScreen SDL3** (`src/gui/LoginScreen.cpp`, `src/gui/LoginClientConfig.cpp`).
- **Stack réseau portée** : `T4CLoginSession`, `CommCenter`, `IOCPCompat` (shim epoll/threads sous Linux), `T4CLinuxCommPort`, `T4CNetworkDebugLog`.
- **Chiffrement TFC réutilisé tel quel** : on link directement `CLIENT168_RC14h_OK/.../CryptMestoph/crypt.cpp` (cf. `CMakeLists.txt` L75). Pas de réécriture, pas de divergence avec un serveur 1.68 standard.
- **Handshake TFC validé bout-en-bout** : trace dans `debug/t4c_network_session.log`. Le canal chiffré aller/retour fonctionne, l'opcode 14 `RQ_RegisterAccount` est correctement décodé et le serveur répond — pour l'instant `REFUSE (1) — Your account is already logged on a server`, ce qui est un problème de session côté serveur, pas un bug client.
- **Vue monde TnC intégrée** via `cmake/TncGraphical.cmake` (sources `VSFInterface`, `MapInterface`, `NPCManager`, `FontManager`, `TextManager` de `../client_graphical_path_to_follow/decode/TnC_dev/`) + un shim SDL2→SDL3 dans `third_party/tnc_sdl3/`.

---

## Roadmap finale — jouer à T4C 1.68 sous Linux/SDL3 dans un flux C↔S ininterrompu et incorruptible

### Objectif

Avoir un client Linux/SDL3 capable de se connecter à un serveur 1.68 **inchangé**, traverser tout le pipeline (auth → liste persos → entrée in-game → rendu monde temps réel), tout en gardant le build Windows/DirectX intact derrière `#ifdef LINUX_PORT`.

### Quatre briques, quatre origines

| Brique | Origine | Statut | Choix |
|---|---|---|---|
| Protocole filaire + chiffrement TFC | code Vircom + `CryptMestoph` (mestoph) | branché, handshake OK | **réutilisé tel quel** |
| Pipeline graphique offline (sprites / maps) | `client_graphical_path_to_follow/decode/` (mestoph) | utilisable, sorties `*.dec` + `*.rmap` déjà produites | **réutilisé** |
| Pipeline graphique runtime (TnC) | `client_graphical_path_to_follow/decode/TnC_dev/` (mestoph) | intégré via `TncGraphical.cmake` + shim | **réutilisé sous SDL3** |
| Données serveur compilées (WDA) | `../../tiforci/havoc2/` (Sorkvild) | outils dispos, à recompiler sous Linux | **réutilisé** |

### Détail des briques

**1. Réseau et chiffrement (réutilisés)**

On garde le protocole 1.68 et `TFCCrypt::EncryptS/DecryptS` (`crypt.cpp` + `xorkey.h` de mestoph). Réécrire le wire format casserait la compatibilité avec tout serveur 1.68 — ce qui irait directement à l'encontre du but ("jouer"). Le log `debug/t4c_network_session.log` documente le pipeline en 5 phases (RQ_RegisterAccount 14 → RQ_AuthenticateServerVersion 99 → FINAL STEP). Reste à implémenter proprement les opcodes post-auth : **26** (liste persos), **65** (query version serveur), MOTD, sélection perso, paquets mouvement/monde.

**2. Décodage offline des assets (mestoph)**

Source : `client_graphical_path_to_follow/decode/`. Le flux :

```
*.vsf chiffrés ─┐
                ├─► decode XOR (vsfkey.h) ─► convertisseur ─► *.dec (sprites bitmap)
id_list.txt    ─┘                                              *.rmap (cartes décodées)
```

Les `*.dec` et `*.rmap` sont déjà générés dans `decode/data/sprites/` et `decode/data/maps/`. On les pointe au runtime via le symlink `data/` créé en `POST_BUILD` par `CMakeLists.txt` (L135-143).

**3. Runtime graphique TnC (mestoph, porté SDL3)**

`VSFInterface` charge les `*.dec` en cache `SDL_Texture`, `MAPInterface` rend les tuiles, `NPCManager` gère les sprites animés, `FontManager` / `TextManager` font le texte. Tout est intégré tel quel via `cmake/TncGraphical.cmake`, le shim `third_party/tnc_sdl3/include/SDL/SDL.h` et `tnc_sdl2_compat.h` mappent l'API SDL2 attendue par le code de mestoph vers SDL3.

**4. WDA serveur (Sorkvild)**

Le format WDA était propriétaire Vircom, mais le dump `../../tiforci/havoc2/` contient les sources C de **Sorkvild** :

- `wc.c` (~111 Ko) — *WDA compiler* : `txt` → `.WDA` (avec option de protection)
- `wd.c` (~85 Ko) — *WDA decompiler* : `.WDA` → `.txt`
- `common.h`, `safe-malloc.h`, `inifile.c` — utilitaires partagés

Ces sources sont du C ANSI portable (entête `by Sorkvild`, mail historique `sorkvild@uboot.com`), pas de dépendance Windows visible. Recompilation Linux triviale via `gcc wc.c inifile.c -o wc` (et idem pour `wd`).

WDA pertinents pour nous :

- `T4C Worlds.WDA` (~24 Mo) — monde compilé, terrain + objets + spawn
- `T4C Edit.WDA` — données d'édition (objets, sorts, créatures, dialogues)
- `NPCs.WDA` + `Format du fichier NPCs.wda.txt` — scripts d'instructions PNJ

À quoi ça sert dans la roadmap : (a) comprendre la sémantique des paquets serveur dérivés des WDA ; (b) pouvoir générer un monde de test reproductible ; (c) à terme, mocker un mini-serveur local pour développer offline sans dépendre d'un serveur tiers.

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
  │ SDL_RenderTexture / SDL_RenderPresent      │
  └────────────────────────────────────────────┘
```

### Étapes restantes (ordonnées)

1. **Compte de test propre** : repasser au moins une fois la phase 5/5 (FINAL STEP) en blanc pour confirmer que le client peut authentifier sans `1_REFUS`. Le code client est prêt, c'est un blocage côté compte.
2. **Opcodes post-auth** : 26, 65, MOTD, sélection perso. Ajouter au fur et à mesure dans `SOURCES` de `CMakeLists.txt` (TFCPacket, Comm, etc.).
3. **Bascule `LoginScreen` → `GameWorldScreen`** : déjà esquissé (`src/game/GameWorldScreen.{cpp,h}`, `src/game/TncDataPaths.{cpp,h}`), à connecter au flux réseau.
4. **Première frame in-game** : `MAPInterface` qui rend `worldmap.rmap` avec la position serveur réelle. Le presenter SDL3 (`third_party/tnc_sdl3/render/Sdl3FramePresenter.cpp`) est en place.
5. **Mini-serveur de test (optionnel)** : recompiler `wc` / `wd` sous Linux, regénérer un monde minimal depuis un `.txt` éditable pour tester sans dépendance externe.
6. **CI Linux** : ajouter un job qui build `t4c_client` + lance un smoke test sur le handshake (mock UDP).
7. **Non-régression Windows** : tout le code Linux est isolé par `#ifdef LINUX_PORT` (`add_compile_definitions(LINUX_PORT)` dans `CMakeLists.txt` L60), le build VS2022 doit continuer à passer à chaque PR.

### Principe de non-réécriture

On a fait le choix explicite de **ne rien réécrire de ce qui marche déjà** :

- Pas de nouveau protocole : le serveur 1.68 est la vérité, on parle son langage.
- Pas de nouveau chiffrement : `crypt.cpp` de mestoph est partagé entre Windows et Linux, un seul code source.
- Pas de nouveau format d'assets : on consomme les `.dec` / `.rmap` produits par le pipeline mestoph, et on garde les `.WDA` lisibles via `wd` Sorkvild.
- Seule la couche présentation est réécrite en SDL3 — parce que DirectDraw / CW32Sprite ne sont par définition pas portables.

### Crédits

- **Sorkvild** — outils `wc` / `wd` (compilateur / décompilateur WDA).
- **mestoph** (project TnC, http://tnc.sourceforge.net/) — décryption VSF/VSB, pipeline offline, runtime graphique TnC_dev.
- **melodiass / elestranobaron** — source 1.68 RC14h.
- **Vircom** — design original de T4C et du format WDA.
