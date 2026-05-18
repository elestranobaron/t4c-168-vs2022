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
   Reproduit fidèlement par `T4C_Server_Linux_Final_Step/res/makewda.py` :
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
| Faire booter le serveur | Oui | Minimal — `makewda.py` suffit (tous blocs à zéro) |
| Jouer dans un monde vide (marcher, se déplacer) | Oui | Minimal — le client render local ne dépend pas des WDA |
| Jouer dans un vrai monde (créatures, objets, téléporteurs) | Oui | **Peuplé — nécessite les WDA Havoc convertis** |

Pour l'**Étape Finale** (jouer réellement), les WDA doivent être peuplés avec le contenu Havoc. L'objectif minimal acceptable est : carte de navigation serveur correcte + au moins une zone de spawn joueur.

### Comment les WDA Havoc sont-ils stockés — la découverte clé

Le `.txt` décompilé (125 835 lignes pour `T4C Worlds.txt`, 4.3 Mo) **ne contient pas** les données de tuiles. La section `[maps]` ne fait que référencer des BMP externes :

```
[maps]
0	3072,3072	"Realms of Oblivion"	"Realms of Oblivion.bmp"
1	3072,3072	"Dungeons"	"Dungeons.bmp"
```

Ces BMP (un par carte, ~4.7 Mo chacun à 3072×3072 × 4 bits) contiennent les codes de couleur terrain. C'est `wc.c` qui les lit au moment de la compilation pour produire le WDA binaire. Les BMP ne sont pas archivés dans le rar Havoc.

En revanche, le **WDA binaire compilé est directement présent** dans le rar :

```
Wda Serveur 1.61 Havoc/T4C Worlds.WDA     24 565 968 bytes  (clé 1.61)
Wda Serveur 1.61 Havoc/T4C Edit.WDA          115 488 bytes  (clé 1.61)
Wda Serveur 1.61 Havoc/NPCs.WDA               54 034 bytes  (clé 1.61)
```

Ces binaires contiennent toutes les données (y compris les tuiles de carte compilées). Il n'y a pas besoin de les recompiler depuis le `.txt` — ils existent déjà.

### Plan WDA — key-swap 1.61 → 1.68 (3 étapes)

Le seul obstacle est la clé XOR. La structure binaire interne est la même, seule la clé diffère. Le plan est donc :

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

**Note** : `wc.c` et `wd.c` de Sorkvild restent utiles pour **inspecter** (`wd`) ou **modifier** (`wc`) le contenu world sans toucher au binaire directement. Pour réutiliser `wc` sur Linux : `gcc wc.c inifile.c -o wc` — mais il faudra retrouver (ou régénérer) les BMP de tuiles si on veut recompiler from scratch. Ce n'est pas prioritaire tant que le key-swap fonctionne.

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

La musique n'est **pas bloquante** pour les étapes 0–4 (auth → monde affiché). Elle devient pertinente à l'étape 5 (bascule GameWorldScreen complète). On peut l'ajouter en parallèle ou en étape 4.5 : remplacer les appels DirectSound par `SDL_mixer`, brancher les mêmes noms de pistes depuis les fichiers `.wav` déjà décodés.

## Alignement de versions vérifié

- **Client** : 1.68 RC14h (historique précis dans `CLIENT168_RC14h_OK/1.68RC14h/BL_MODIF_CLIENT.txt` → `165b5 → 165b6 → 165RC10 → 165RC11 → 168RC14 → 168RC14F → 168RCH`). Opcode le plus récent : `RQ_GetPvpRanking=131` marqué `// asteryth ajout`.
- **Serveur** : Final Step cible explicitement *"v1.68 Linux client"* (commit `693ace3` : *« stable connection with the v1.68 Linux client »*).
- **Verdict** : alignement parfait. Pas de problème de version entre les deux côtés.

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

### Étapes ordonnées par opcode TFC

Chaque étape a un critère d'acceptation factuel et observable dans le log.

```
[ÉTAT ACTUEL] build OK, handshake TFC OK jusqu'à la phase 2/5
              (debug/t4c_network_session.log : opcode 14 envoyé,
               réponse 52 octets décodée, REFUS 1 = compte déjà loggé)
                              │
                              ▼
[Étape 0]  Compte test propre côté serveur
           → critère : phase 5/5 atteinte, [AUTH] Verdict : ACCEPTE.
           Pas un travail client, c'est de la conf serveur.
                              │
                              ▼
[Étape 1]  Opcodes 65 (RQ_QueryServerVersion) + 66 (RQ_MessageOfDay)
           → critère : MOTD affiché dans la console client SDL3.
                              │
                              ▼
[Étape 2]  Opcode 26 (RQ_GetPersonnalPClist)
           → critère : ≥1 perso reçu, liste rendue à l'écran.
                              │
                              ▼
[Étape 3]  Opcodes 13 (RQ_PutPlayerInGame) + 46 (RQ_FromPreInGameToInGame)
           → critère : bascule LoginScreen → GameWorldScreen.
                              │
                              ▼
[Étape 4]  Opcode 9 (RQ_GetPlayerPos) + MAPInterface
           → critère : première tuile de worldmap.rmap rendue à l'écran
           à la bonne position serveur.
                              │
                              ▼
[Étape 5]  Opcodes 1-8 (Move N/NE/E/...) + 69 (RQ_UnitUpdate)
           → critère : le perso bouge, le serveur reçoit les déplacements
           et renvoie les UnitUpdate des unités proches.
                              │
                              ▼
[Étape 6]  Opcodes 16 (RQ_SendPeriphericObjects) + 60 (RQ_GetNearItems)
           → critère : NPCs et items visibles autour du perso.

           ─────► À ce stade : ON JOUE.

[Étapes suivantes] combat, chat, magie, guildes — extensions
incrémentales sur le même socle réseau + rendu.
```

### Travaux transverses (en parallèle des étapes ci-dessus)

- **Non-régression Windows** : tout le code Linux est isolé par `#ifdef LINUX_PORT` (`add_compile_definitions(LINUX_PORT)` dans `CMakeLists.txt` L60). Le build VS2022 doit continuer à passer à chaque PR.
- **CI Linux** : ajouter un job qui build `t4c_client` + lance un smoke test sur le handshake (mock UDP rejouant la trace de `debug/t4c_network_session.log`).
- **Outils Sorkvild Linux** (optionnel, utile pour la rétro-ing) : recompiler `wc` / `wd` sous Linux (`gcc wc.c inifile.c -o wc`).
- **Aucun travail WDA côté client** : c'est explicitement hors scope client. Si le serveur a un problème de WDA, ça se règle dans `T4C_Server_Linux_Final_Step`, pas ici.

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
