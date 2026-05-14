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

## Résultat

Le launcher s'affiche, les champs IP/Login/Password/Connect sont fonctionnels. Prochaine étape : tester la connexion au serveur 1.68 Linux, puis envisager le portage SDL3.
