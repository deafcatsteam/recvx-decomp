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
   (`npCalcSkin`), le morphing (`npTransform`) et l'animation des visages
   (`_fmCnkCalc*` : muscles, mâchoire, langue, yeux). Le rendu natif par la carte graphique vient ensuite
   (phase B), à partir des mêmes données.
3. **Son** ✔ musique et voix (flux ADX), bruitages et ambiances (remplaçant
   du pilote IOP `TSNDDRV` : banques Sony HD/BD, séquences SQ, adapté de
   recvx-vita), et la réverbération des pièces (l'écho « Hall » du SPU2,
   refait en logiciel). Reste : la validation sur le jeu.
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
  **Son 3D** ✔ (`sound_3d`, `src/host/pc_spatial.c`) : les endroits du
  jeu qui placent un bruit (pas et armes du joueur, ennemis, objets,
  événements) donnent aussi sa position (`pc_snd_at`, `src/game/pc_sound3d.c`),
  qui suit la demande jusqu'au pilote et au synthétiseur ; chaque voix est
  alors placée au casque (délai entre les oreilles, ombre de la tête,
  derrière plus sourd) ou sur toute la largeur des enceintes. Reste :
  - 5.1/7.1 (plus de canaux de sortie) et des HRTF mesurées (élévation) ;
  - les voix ADX placées (`PlayVoiceEx2`), que le jeu ne panoramique pas ;
  - la réverbération par pièce : celle du SPU2 est refaite
    (`src/audio/pc_spu2rev.c`, niveau par salle venant de `Room_SoundEnv`) ;
    un effet moderne pourrait la remplacer au même endroit ;
  - un rééchantillonnage de meilleure qualité (les voix du moteur sont
    déjà interpolées en Hermite ; celles du synthétiseur en linéaire).
- **Performances** : le GS logiciel dessine sur plusieurs cœurs ; la
  traduction C du VU0 se prête aussi au SIMD (SSE) si besoin.

## Phase B — Graphismes modernes

1. Haute résolution ✔ en partie : le rendu par la carte graphique
   (`src/gs/gs_gpu.c`, OpenGL 3.3) dessine ce que le GS dessinerait, jusqu'à
   4 fois sa résolution (2560x1792 au plus). Il garde la mémoire du GS à
   jour page par page (copie dans un sens ou dans l'autre quand le jeu
   relit ce qui a été dessiné), et lit directement en haute résolution une
   image dessinée puis reprise comme texture (flous, fondus). Testé contre
   le rendu logiciel sur des scènes aléatoires.
   **Écran large 16:9** ✔ (`src/game/pc_widescreen.c`) : en jeu, la 3D est
   dessinée 3/4 aussi large (l'aspect Ninja) et la fenêtre l'étire en 16:9 ;
   le volume de vue (découpage des polygones, objets écartés, cône de
   l'écran) est élargi d'autant. Le texte est resserré vers le milieu pour
   garder sa forme. Les menus, vidéos et l'écran titre restent en 4:3.
   L'image en 16:9 a donc 3/4 de la finesse horizontale : un rendu natif
   (plus bas) la dessinerait à sa vraie largeur.
2. **Textures** : export, remplacement, packs refaits par IA ; compatibilité
   avec les packs PCSX2 si leur format d'empreinte le permet.
3. Backend **Direct3D 9** et **RTX Remix** (path tracing, matériaux, **DLSS**).
4. **60 images/seconde** ✔ en essai (`fps60`, `src/game/pc_interp.c`) : la
   logique du jeu reste à 30. Le GS enregistre ce qu'il reçoit pendant une
   image (`gs_rec_start`) ; les sommets des modèles 3D y sont retrouvés par
   leur contenu, rangés par modèle. En fin d'image, chaque modèle dessiné
   avec autant de sommets qu'à l'image d'avant est placé à mi-chemin (écran,
   profondeur, perspective des textures, brouillard, couleur) et l'image
   enregistrée est redessinée depuis l'état du GS de son début
   (`gs_rec_replay`) : c'est l'image montrée à la première V-blank, la vraie
   à la seconde. Pas d'image intermédiaire aux changements de plan, ni hors
   du jeu. Reste : les particules et effets 2D placés en 3D (à 30), et un
   rendu natif qui le ferait avec les vraies matrices.

## Phase C — Son et confort

- Son 3D ✔ au casque et aux enceintes ; reste 5.1/7.1 et une réverbération
  moderne par pièce : voir « Ce qui prépare déjà la suite » ci-dessus.
- Musiques et voix de meilleure qualité (remplacement des ADX par des
  fichiers refaits, comme pour les vidéos).
- Contrôles modernes : déplacement libre, visée souris, touches configurables.
- Confort : passer les cinématiques, sauvegardes rapides, menu d'options.

## Phase D — Multijoueur coop (à étudier)

Le jeu est prévu pour un joueur : une coop demande de synchroniser l'état du
jeu entre plusieurs PC. Étude à faire une fois la phase A terminée.
