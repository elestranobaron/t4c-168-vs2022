# Crédits — port Linux / SDL3 (Final Step)

## Projet client Linux

- **Tom — ElEsTaNoBaRoN** — port client T4C 1.68 RC14h, réseau, GUI, intégration monde, pipeline auth → jeu (`LINUX_PORT`).

## Session rendu graphique (2026-05)

Travail conjoint sur la vue monde, le launcher graphique, la migration SDL3 native du moteur TnC et le correctif couleurs (palettes / bake RGBA) :

- **Tom — ElEsTaNoBaRoN** — direction, tests en jeu, validation visuelle, architecture des dossiers (`client` / `client_graphical_sdl3_test`).
- **Cursor (agent IA / Composer)** — implémentation pair-programming : shim → `tnc_sdl3.h`, `GameWorldScreen`, `LauncherChrome`, `WorldSideMenu`, `CHANGELOG`.

## Moteur carte / sprites (TnC)

- **Noth (2005)** — *TnC The Next Coming* (MapInterface, VSFInterface, NPCManager, …) — [tnc.sourceforge.net](http://tnc.sourceforge.net/) — **GPL v2**.

## Référence jeu

- **Vircom / The 4th Coming** — client et assets d’origine (référence comportementale ; non redistribués dans ce dépôt).

---

## VSF — propriétaire ou pas ?

| Élément | Statut |
|---------|--------|
| **Format VSF + fichiers** (`.vsf` d’origine, `.dec` convertis, maps, sons) | Contenu **T4C** — **non libre** ; ne pas les committer dans un repo public |
| **Code `VSFInterface/`** (lecteur mestoph) | **GPL v2** (Noth) — open source |

Le format VSF appartient au jeu ; le **code** qui le lit est sous GPL.

---

## Dossier moteur (`client_graphical_sdl3_test`)

Ce n’est plus un « test » : c’est le **moteur réel** compilé par le client (`TNC_GRAPHICAL_ROOT`).

**Renommage recommandé** (à faire quand tu versionnes le repo moteur) :

- `t4c-tnc-engine` *(préféré — explicite)*
- `tnc_sdl3`
- `client_graphical_engine`

`client_graphical_path_to_follow/` reste le **labo offline** (convert2, démos) — pas la racine de build du client.

---

## Licence — GPL vs Apache

- Le moteur Noth est **GPL v2** ; linké dans `t4c_client`, l’ensemble (binaire + sources liées) est une **œuvre dérivée GPL**.
- **Apache seul sur le tout** : non, sans retirer le moteur GPL ou accord des auteurs Noth.
- **Recommandation** : `LICENSE` **GPL-2.0-or-later** sur le repo moteur et le client (tant que TnC est compilé dedans). Apache uniquement pour des modules **sans** code TnC linké.

---

## Assets dans Git

- **Repo public** : ne pas committer `data/` (sprites, maps, WDA) — risque DMCA / retrait des fichiers (parfois repo entier).
- **Repo privé** / machine locale : `T4C_DATA` ou `client/data/` pour le runtime ; documenter « apportez vos propres fichiers T4C » dans le README.

