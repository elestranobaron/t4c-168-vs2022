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
