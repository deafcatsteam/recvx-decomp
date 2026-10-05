# Feuille de route du portage

Objectif : d'abord un jeu **complet et jouable** fidèle à la PS2, puis les
améliorations modernes une par une. Les choix d'architecture faits dès la
phase A tiennent compte de la phase B, pour ne rien avoir à refaire.

## Décisions d'architecture

- **2D (menus, écran titre, textes, fondus)** : émulation du GS de la PS2. Le
  jeu construit lui-même ses paquets GS et gère la mémoire vidéo ; on les
  interprète. C'est le chemin le plus court et le plus fidèle pour la 2D.
- **3D (personnages, décors, effets)** : rendu **natif**. On repart des modèles
  « chunk » Ninja, des matrices et des lumières du jeu, au lieu des paquets VU1
  déjà projetés. C'est indispensable pour RTX Remix (il a besoin de la vraie
  scène 3D et de la caméra) et pour la 4K / l'écran large.
- **Interface de rendu commune** avec deux implémentations :
  - **OpenGL** d'abord (testable dans le cloud, marche sous Linux et Windows) ;
  - **Direct3D 9** ensuite, pour **RTX Remix** sous Windows, qui apporte aussi
    le path tracing et le **DLSS**.
- **Textures** : toutes passent par un point unique, qui sait les **exporter**
  (pour un pack refait par IA) et les **remplacer** (packs maison, et si
  possible les packs PCSX2 existants, via la même empreinte).

## Phase A — Jouable

1. **Rendu 2D** par émulation du GS : mémoire vidéo, transferts de textures,
   sprites et polygones 2D, affichage dans la fenêtre. → écran titre et menus.
2. **Rendu 3D** natif (OpenGL) : modèles chunk, textures, éclairage, brouillard,
   transparences ; traduction en C du skinning (`npCalcSkin`), du morphing
   (`npTransform`) et des visages (`_fmCnkCalc*`).
3. **Son** : remplaçant PC du pilote IOP `TSNDDRV` (effets, musiques MIDI),
   flux ADX (voix, ambiances), vidéos SFD (MPEG + ADX, via FFmpeg).
4. **Sauvegardes** dans des fichiers (carte mémoire émulée), finitions des
   contrôles.

## Phase B — Graphismes modernes

1. Haute résolution (jusqu'à 4K) et **écran large 16:9** (champ de vision,
   interface recalée).
2. **Textures** : export, remplacement, packs refaits par IA ; compatibilité
   avec les packs PCSX2 si leur format d'empreinte le permet.
3. Backend **Direct3D 9** et **RTX Remix** (path tracing, matériaux, **DLSS**).
4. **60 images/seconde** par interpolation de l'affichage (la logique du jeu
   reste à 30).

## Phase C — Son et confort

- Musiques et voix de meilleure qualité, son 3D positionnel (OpenAL).
- Contrôles modernes : déplacement libre, visée souris, touches configurables.
- Confort : passer les cinématiques, sauvegardes rapides, menu d'options.

## Phase D — Multijoueur coop (à étudier)

Le jeu est prévu pour un joueur : une coop demande de synchroniser l'état du
jeu entre plusieurs PC. Étude à faire une fois la phase A terminée.
