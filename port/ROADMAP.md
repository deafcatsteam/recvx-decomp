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
2. **Rendu 3D** ✔ d'abord par le GS logiciel, fidèle à la PS2 : le code VU0
   (sommets, éclairage, découpage) est traduit en C, avec le skinning
   (`npCalcSkin`) et le morphing (`npTransform`). Reste : les visages
   (`_fmCnkCalc*`). Le rendu natif par la carte graphique vient ensuite
   (phase B), à partir des mêmes données.
3. **Son** ✔ musique et voix (flux ADX), bruitages et ambiances (remplaçant
   du pilote IOP `TSNDDRV` : banques Sony HD/BD, séquences SQ, adapté de
   recvx-vita). Reste : la réverbération des pièces, et la validation sur
   le jeu.
4. **Vidéos** ✔ `.PSS` lues sur le disque : image MPEG-2 (FFmpeg) et son ;
   toutes peuvent être passées.
5. **Sauvegardes** ✔ carte mémoire émulée par un dossier (`saves`),
   passée par le code du jeu lui-même dans les tests. Reste : les
   finitions des contrôles.

### Ce qui prépare déjà la suite

- **3D** : toute la géométrie passe par `njCnkCvVnPs2` et
  `src/game/pc_render3d.c`, avec les sommets en espace caméra, les matrices
  et les lumières. Un rendu par la carte graphique (OpenGL, puis D3D9 pour
  RTX Remix) pourra s'y brancher et recevoir la vraie scène 3D, au lieu des
  triangles déjà projetés du GS.
- **Haute résolution** : en attendant ce rendu, le GS logiciel pourra
  dessiner à 2× ou 4× la résolution (étape intermédiaire simple), et passer
  sur la carte graphique.
- **Vidéos** ✔ remplaçables par des versions refaites en HD
  (`movies/MV_000.mp4`, voir le README) : affichées par la fenêtre à leur
  propre résolution, par-dessus l'image du jeu, avec le son d'origine.
- **Son** : tout passe par le moteur `src/host/pc_sound.c`, où chaque son
  est une voix avec un volume, un panoramique et, en option, une **position
  3D** par rapport à l'auditeur. Une seule fonction, `spatialize()`, décide
  de la répartition gauche/droite : c'est elle qu'on remplacera pour le son
  spatial moderne, au choix **HRTF** (son 3D au casque, par exemple avec
  OpenAL Soft ou Steam Audio) ou **5.1/7.1** (plus de canaux de sortie).
  Ce qui reste à faire pour en profiter :
  - donner leur position aux sons : le jeu la connaît (`Get3DSoundParameter`,
    `SetupSeGenericParm`, `PlayVoiceEx2` dans `sdfunc.c` reçoivent la
    position de la source et celle de la caméra) ; un petit relais PC peut
    la transmettre au lieu du seul panoramique ;
  - sortir chaque bruitage du synthétiseur comme une voix séparée du moteur
    (aujourd'hui `pc_hsyn.c` mélange ses 48 voix lui-même) ;
  - la réverbération par pièce (le jeu envoie déjà un niveau par salle,
    `Room_SoundEnv`) avec un effet moderne au lieu de celui du SPU2 ;
  - un rééchantillonnage de meilleure qualité (les voix du moteur sont
    déjà interpolées en Hermite ; celles du synthétiseur en linéaire).
- **Performances** : le GS logiciel dessine sur plusieurs cœurs ; la
  traduction C du VU0 se prête aussi au SIMD (SSE) si besoin.

## Phase B — Graphismes modernes

1. Haute résolution (jusqu'à 4K) et **écran large 16:9** (champ de vision,
   interface recalée).
2. **Textures** : export, remplacement, packs refaits par IA ; compatibilité
   avec les packs PCSX2 si leur format d'empreinte le permet.
3. Backend **Direct3D 9** et **RTX Remix** (path tracing, matériaux, **DLSS**).
4. **60 images/seconde** par interpolation de l'affichage (la logique du jeu
   reste à 30).

## Phase C — Son et confort

- Son 3D positionnel (HRTF au casque, 5.1/7.1), réverbération moderne par
  pièce : voir « Ce qui prépare déjà la suite » ci-dessus.
- Musiques et voix de meilleure qualité (remplacement des ADX par des
  fichiers refaits, comme pour les vidéos).
- Contrôles modernes : déplacement libre, visée souris, touches configurables.
- Confort : passer les cinématiques, sauvegardes rapides, menu d'options.

## Phase D — Multijoueur coop (à étudier)

Le jeu est prévu pour un joueur : une coop demande de synchroniser l'état du
jeu entre plusieurs PC. Étude à faire une fois la phase A terminée.
