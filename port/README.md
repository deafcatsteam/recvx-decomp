# Portage PC de Code: Veronica X

Ce dossier contient tout ce qui sert à compiler le code décompilé du jeu pour PC.
Le build PS2 d'origine (`compile.py`, qui doit rester identique octet par octet)
n'est pas touché : les adaptations faites dans `src/` et `include/ps2/` sont
derrière `#ifdef PLATFORM_PC`, et sans `PLATFORM_PC` ces fichiers sont
identiques à la version d'origine.

## Où on en est

Le jeu entier compile et s'assemble en un programme PC (`cvx_pc`, ou
`cvx_pc.exe` sous Windows) qui lit les données directement dans l'ISO du jeu.
Avec une fausse image disque, il passe toute l'initialisation du système de
fichiers et du son ; **avec ta vraie ISO, il n'a encore jamais été lancé.**
Le rendu (menus, textes, images, et maintenant la 3D) passe par un GS logiciel
qui reproduit la carte graphique de la PS2. Les vidéos sont décodées avec FFmpeg.

- [x] Tout le code C du jeu compile (les fonctions en assembleur sont mises de côté)
- [x] Maths Ninja (`ps2_NaMath.c`, `ps2_NaMatrix.c`) réécrites en C et testées
- [x] Décompression `Expand`, défilement d'UV, surface de l'eau, etc. traduits en C
- [x] Lecture du disque depuis l'ISO, archives AFS (CRI ADXF), testée
- [x] Noyau, IOP, manette, horloge 60 Hz, SDK Sony : remplacements PC
- [x] Fenêtre, clavier et manettes (SDL2, branchement à chaud, vibration), builds Linux et Windows
- [ ] **Premier lancement avec la vraie ISO** (à faire chez toi, voir plus bas)
- [x] Rendu 2D : GS logiciel (`src/gs/`), testé sur des paquets de test ; reste à valider sur le jeu
- [x] Rendu 3D par le GS logiciel : sommets, éclairage, découpage traduits du VU0 (`src/game/pc_render3d.c`), testé ; reste à valider sur le jeu
- [x] Skinning (`npCalcSkin`) et morphing (`npTransform`), testés
- [x] Animation des visages (`_fmCnkCalc*`) : muscles, mâchoire, langue et yeux
- [ ] Rendu 3D par la carte graphique (OpenGL, puis D3D9 pour RTX Remix), voir `ROADMAP.md`
- [x] Musique et voix : flux ADX décodés (`src/audio/pc_adx.c`), testés sur des ADX synthétiques ; reste à valider sur le jeu
- [x] Bruitages et ambiances : remplaçant du pilote IOP `TSNDDRV` qui joue les banques Sony HD/BD et les séquences SQ (adapté de recvx-vita), testé ; reste à valider sur le jeu
- [x] Réverbération des pièces (`SdrSetRev`) : l'écho « Hall » du SPU2 refait en logiciel (`src/audio/pc_spu2rev.c`), testé ; reste à valider sur le jeu
- [x] Vidéos `.PSS` : image MPEG-2 (FFmpeg) et son, testées sur une vidéo synthétique ; reste à valider sur le jeu
- [x] Vidéos HD de remplacement (`movies/MV_000.mp4`…), affichées à leur résolution, voir plus bas
- [x] Moteur de son (`src/host/pc_sound.c`) : voix mélangées sur le fil audio, prêt pour le son 3D
- [x] Carte mémoire : les sauvegardes vont dans le dossier `saves` à côté de l'exe, testé ; reste à valider sur le jeu
- [x] Fichier de réglages `cvx.ini` (plein écran, taille de fenêtre, touches, chemins, son, vibration), testé

## Lancer le jeu

Il faut ta propre image du disque **Resident Evil Code: Veronica X (NTSC-U,
SLUS-20184)**. Le jeu cherche `cvx.iso` dans le dossier courant, ou le chemin
donné par la variable `CVX_ISO` :

```
CVX_ISO=/chemin/vers/cvx.iso ./cvx_pc          # Linux / WSL
```

```
set CVX_ISO=C:\Jeux\cvx.iso
cvx_pc.exe
```
(Windows, invite de commandes ; ou mets simplement l'ISO renommée `cvx.iso` à côté de l'exe,
ou écris son chemin dans `cvx.ini`, voir plus bas.)

## Réglages (`cvx.ini`)

Au premier lancement, le jeu crée un fichier `cvx.ini` à côté de l'exe, avec
tous les réglages désactivés (une ligne qui commence par `;` ne compte pas).
Ouvre-le avec le Bloc-notes, enlève le `;` devant ce que tu veux changer, et
relance le jeu :

| Réglage | Effet |
|---|---|
| `iso = C:\Jeux\cvx.iso` | Chemin de l'ISO (par défaut `cvx.iso` à côté de l'exe) |
| `saves = saves` | Dossier des sauvegardes |
| `movies = movies` | Dossier des vidéos HD de remplacement |
| `fullscreen = yes` | Démarrer en plein écran (F11 change toujours) |
| `window = 1280x960` | Taille de la fenêtre au démarrage |
| `filter = smooth` | Image lissée quand elle est agrandie (`sharp`, par défaut : pixels nets) |
| `sound = no` | Pas de son |
| `vibration = no` | Pas de vibration de la manette |
| `key_cross = Space` | Touches du clavier, une ligne par bouton (`key_up`, `key_stick_left`, `key_l1`, `key_start`, `key_fast_forward`…, toutes listées dans le fichier). Plusieurs touches : `Space, Return` ; rien après le `=` : aucune touche |

Les variables du tableau suivant peuvent aussi y être écrites telles quelles
(`CVX_GS_THREADS = 4`). Une variable réglée à la main l'emporte sur le
fichier. Un réglage mal écrit est signalé au début de `cvx_log.txt` (lignes
`config:`). Pour revenir aux réglages d'origine, supprime `cvx.ini`.

Variables utiles :

| Variable | Effet |
|---|---|
| `CVX_ISO` | Chemin de l'ISO |
| `CVX_HEADLESS` | Pas de fenêtre (tests) |
| `CVX_NO_VSYNC` | Ne pas attendre le 60 Hz (le jeu tourne aussi vite que possible) |
| `CVX_QUIET` | Pas de ligne d'état chaque seconde dans la console |
| `CVX_NO_LOG` | Ne pas écrire `cvx_log.txt` |
| `CVX_GS_THREADS` | Nombre de cœurs pour le dessin (par défaut : tous, 8 au plus) |
| `CVX_GS_NO_BATCH` | Dessine chaque triangle à part au lieu de les regrouper (plus lent ; pour vérifier si un défaut d'image vient du dessin en parallèle) |
| `CVX_NO_AUDIO` | Pas de son |
| `CVX_MOVIES` | Dossier des vidéos de remplacement (par défaut `movies`, puis `MOVIE`) |
| `CVX_SAVES` | Dossier de la carte mémoire (par défaut `saves`) |

**Sauvegardes** : la carte mémoire de la fente 1 est le dossier `saves` à
côté de l'exe (créé à la première sauvegarde), avec les fichiers du jeu
comme sur une carte PS2 : `saves/BASLUS-20184/SAVEDATA-00`… Pour garder
tes parties, copie ce dossier. Chaque fichier est remplacé d'un coup quand
le jeu sauvegarde, donc un plantage pendant la sauvegarde ne l'abîme pas.

Tout ce qui s'affiche dans la console est aussi écrit dans `cvx_log.txt`, à
côté de l'exe. En cas de plantage, la console indique où était le jeu et
reste ouverte jusqu'à un appui sur Entrée : envoie ce fichier pour signaler
un problème.

Touches par défaut (manette PS2 émulée ; elles se changent dans `cvx.ini` ;
pour les vraies manettes, voir plus bas) :

| Clavier | Bouton PS2 | | Clavier | Bouton PS2 |
|---|---|---|---|---|
| Flèches | Croix directionnelle | | Espace | ✕ (action) |
| W A S D | Stick gauche | | Échap | ○ (annuler) |
| Entrée | Start | | Maj gauche | □ (courir) |
| Retour arrière | Select | | E | △ |
| Ctrl gauche | R1 (viser) | | Q | L1 |
| 1 / 3 | L2 / R2 | | F11 | Plein écran |
| Tab (maintenu) | Avance rapide | | F12 | Capture d'écran (`cvx_screenshot_000.bmp`…) |
| Entrée, Retour arrière ou Échap | Passer une vidéo (Start, Select ou ○ à la manette) | | | |
| | | | F10 | Enregistre une image pour le débogage graphique (`cvx_gsdump_000.bin`, environ 5 Mo ; la rejouer avec `gs_replay`) |

Manettes : Xbox, PlayStation (DualShock 4, DualSense), Switch Pro et la
plupart des autres marchent, branchées avant ou pendant le jeu, en USB ou en
Bluetooth. On peut en débrancher une et en brancher une autre à tout moment ;
si plusieurs sont branchées, toutes contrôlent le personnage. Le bouton du
bas (A sur Xbox, ✕ sur PlayStation, B sur Switch) est toujours ✕, et les
gâchettes sont L2/R2. La vibration du jeu passe sur la manette (si l'option
du jeu est activée). Une manette que SDL ne connaît pas est lue avec la
disposition des manettes USB génériques ; si ses boutons sont mélangés, mets
un fichier `gamecontrollerdb.txt`
([liste communautaire](https://github.com/mdqinc/SDL_GameControllerDB)) à
côté de l'exe. `cvx_log.txt` indique chaque manette branchée (lignes `pad:`).

## Vidéos en HD (remplacement)

Une vidéo du jeu peut être remplacée par une version agrandie : mets un
fichier du même nom dans un dossier `movies` ou `MOVIE` à côté de l'exe,
par exemple `MOVIE/MV_000.mkv` pour `MOVIE/MV_000.PSS` (les autres fichiers
du dossier, comme des `.mpg` ou `.PSS`, sont ignorés). Le portage l'affiche à sa
propre résolution (jusqu'à 3840×2160) à la place de l'image d'origine. Le
**son** reste celui du `.PSS` d'origine, ainsi que le rythme et la fin de la
vidéo : la version HD n'a donc pas besoin de son, mais elle doit garder
**la même durée et le même nombre d'images par seconde** (29,97).

Formats lus : MP4, MKV, WebM, MOV, AVI ; vidéo H.264, H.265/HEVC, VP9, VP8,
MPEG-4, ProRes, FFV1. La console indique `movie: replacement ...` quand un
fichier est trouvé. Ces fichiers sont à toi : ils ne vont jamais sur GitHub.

Fabriquer les vidéos agrandies, gratuitement, avec **Video2X** (le plus
simple) :

1. Copie le dossier `MOVIE` de l'ISO ailleurs (par exemple `MOVIE_HD`).
   Dans cette copie, renomme les `.PSS` en `.mpg` pour que les logiciels de
   vidéo les reconnaissent : clique dans la barre d'adresse de l'explorateur,
   tape `cmd`, Entrée, puis `ren *.PSS *.mpg`. (Le son des `.PSS` n'est pas
   lu par ces logiciels : c'est normal, il n'est pas utile ici.)
2. Installe Video2X (gratuit, https://github.com/k4yt3x/video2x, page
   *Releases*, l'installateur Windows). Il utilise la carte graphique.
3. Ajoute les fichiers `.mpg`, choisis **Real-ESRGAN** avec le modèle
   **realesrgan-plus** (×4, fait pour les images réalistes), sortie en MP4.
   **N'active pas l'interpolation d'images** (RIFE) : le nombre d'images par
   seconde doit rester le même.
4. Renomme chaque résultat comme l'original (`MV_000.mkv`, …) et mets-les
   dans `MOVIE` (ou `movies`) à côté de `cvx_pc.exe`. Video2X ajoute
   `.realesrgan` au nom : pour l'enlever d'un coup, tape `powershell` dans
   la barre d'adresse du dossier, Entrée, puis :

   ```
   Get-ChildItem *.realesrgan.* | Rename-Item -NewName { $_.Name -replace '\.realesrgan','' }
   ```

Commence par une petite vidéo (`MV_004`, 5 Mo) pour voir le résultat et le
temps que ça prend. Avec **ComfyUI**, c'est aussi possible (nœuds *Load
Video* et *Video Combine* de VideoHelperSuite, *Upscale Image (using
Model)* avec un modèle ×4) ; il faut alors régler la sortie à 29,97 images
par seconde et traiter les longues vidéos par morceaux (ComfyUI garde
toutes les images en mémoire).

## Récupérer l'exécutable Windows

À chaque push, GitHub Actions (`.github/workflows/pc-port.yml`) compile et
teste le port, puis publie `cvx_pc.exe` : onglet **Actions** du dépôt →
dernier run « PC port » → **Artifacts** → `cvx_pc-windows`. L'exe n'a besoin
d'aucune DLL en plus.

## Compiler soi-même

Prérequis : CMake ≥ 3.20, Python 3, un compilateur C 32 bits, et les
sous-modules Katana et CRI (le compilateur PS2 n'est pas nécessaire) :

```
git submodule update --init include/recvx-decomp-katana include/recvx-decomp-cri
cmake -S . -B build-pc
cmake --build build-pc
ctest --test-dir build-pc        # tests : maths, disque/AFS/Expand
```

- **Linux / WSL** : `sudo apt install gcc-multilib cmake python3`, et pour avoir
  une vraie fenêtre `sudo dpkg --add-architecture i386 && sudo apt update &&
  sudo apt install libsdl2-dev:i386`. Sans SDL2 32 bits installé, CMake
  télécharge et compile SDL2 lui-même, mais sans X11/Wayland (pas de fenêtre).
- **Windows** (pas encore testé, le plus simple reste l'exe de GitHub Actions) :
  MSYS2, shell « MINGW32 » :
  `pacman -S mingw-w64-i686-gcc mingw-w64-i686-cmake mingw-w64-i686-ninja python`,
  puis les commandes ci-dessus (ajouter `-G Ninja`). SDL2 est téléchargé et
  compilé automatiquement.
- **Windows depuis Linux** : `-DCMAKE_TOOLCHAIN_FILE=port/cmake/mingw-w64-i686.cmake`
  (paquets `gcc-mingw-w64-i686 g++-mingw-w64-i686`).

MSVC n'est pas supporté : le code utilise des extensions GCC.

### Pourquoi 32 bits ?

Le jeu stocke des pointeurs 32 bits dans ses structures et dans ses fichiers de
données (modèles, motions, scripts). En gardant des pointeurs de 4 octets, le
code décompilé fonctionne tel quel. Le passage en 64 bits est possible plus tard,
mais demandera de convertir les données au chargement.

## Comment c'est construit

Le code du jeu tourne tel quel ; tout ce qui touchait au matériel PS2 passe par
une couche « plateforme » écrite pour le PC.

| Chemin | Rôle |
|---|---|
| `include/port_prefix.h` | Inclus avant chaque fichier du jeu : `PLATFORM_PC`, libc, `long` sur 8 octets comme sur PS2, types 128 bits |
| `include/sdk/` | En-têtes de remplacement du SDK PS2 de Sony ; les registres matériels pointent vers de la mémoire ordinaire |
| `src/gs/` | GS logiciel : mémoire vidéo au format PS2, paquets GIF, DMA, dessin |
| `src/main/` | Point d'entrée PC (`pc_main.c`) et fenêtre/entrées SDL2 (`pc_window.c`) |
| `src/platform/pc_disc.c` | Lecture de l'ISO (ISO 9660) secteur par secteur, comme le lecteur DVD |
| `src/platform/pc_iop.c` | Mémoire et RPC de l'IOP ; les requêtes du pilote son vont à `src/audio/pc_snddrv.c` |
| `src/platform/pc_kernel.c` | Noyau EE : sémaphores, interruptions V-blank, cadence 60 Hz |
| `src/platform/pc_sdk.c` | DVD, GS, DMA, manette, carte mémoire |
| `src/platform/pc_vu0.c` | Fonctions vectorielles `libvu0` en C |
| `src/audio/pc_adx.c` | CRI ADXF (fichiers et archives AFS) ; ADXT : décodage des flux ADX (musique, voix) |
| `src/audio/pc_snddrv.c` | Remplaçant du pilote son `TSNDDRV` : banques, bruitages, séquences, état renvoyé au jeu |
| `src/audio/pc_hsyn.c`, `pc_hseq.c` | Synthétiseur (banques Sony HD/BD, ADPCM, enveloppes du SPU2) et lecteur de séquences SQ, adaptés de [recvx-vita](https://github.com/shoui520/recvx-vita) (licence MIT) |
| `src/audio/pc_spu2rev.c` | Réverbération du SPU2 (préréglage « Hall » que le jeu règle pièce par pièce), d'après la description de psx-spx |
| `src/game/pc_game_asm.c` | Fonctions du jeu en assembleur : traduites en C, ou vides en attendant le rendu |
| `src/game/pc_movie.c` | Lecteur vidéo : lit le `.PSS` sur le disque, sépare image et son |
| `src/game/pc_render3d.c` | 3D : couleurs des sommets, découpage, paquets GS (code VU0 traduit) |
| `src/host/pc_video.c` | Décodage MPEG-2 avec FFmpeg (LGPL, téléchargé et compilé par CMake avec le strict nécessaire), et lecture des vidéos de remplacement |
| `src/host/pc_audio.c` | Sortie son : mélange du moteur de son et de la file des vidéos, jouée par la fenêtre (SDL) |
| `src/host/pc_memcard.c` | Carte mémoire dans un dossier (`saves`) |
| `src/host/pc_sound.c` | Moteur de son : voix (flux ou sources), rééchantillonnage, volume, panoramique, position 3D |
| `src/ninja/` | Maths Ninja en C (remplacent les fichiers VU0) |
| `src/host/` | Services du système (temps), compilés sans les réglages du jeu |
| `tests/` | `test_math`, `test_disc` (+ `make_test_iso.py`, une ISO de test sans données du jeu), `test_3d`, `test_movie` (+ `make_test_pss.py`, une vidéo synthétique), `test_sound` (+ `make_test_adx.py`, des ADX synthétiques), `test_snddrv` (une banque Sony fabriquée par le test) |
| `tools/gen_case_links.py` | Corrige la casse des en-têtes Dreamcast/CRI (venus de Windows) |

Dans les fichiers du jeu, une fonction en assembleur est entourée de
`#ifndef PLATFORM_PC` et sa version PC est dans `src/game/` ; un petit bloc
d'assembleur au milieu d'une fonction C est remplacé sur place
(`#ifdef PLATFORM_PC` … `#else` asm `#endif`). Seuls `ps2_NaMath.c`,
`ps2_NaMatrix.c`, `ps2_MovieFunc.c` et `gcc_wrapper.c` sont remplacés en entier
(liste `GAME_REPLACED` dans `CMakeLists.txt`).

### Comment la 3D est dessinée

Sur PS2, les modèles « chunk » passent par le VU0 (sommets, lumières), puis par
un microprogramme VU1 pour la plupart des modes de dessin. Le port fait tout
sur le processeur, avec les versions C que le jeu a de ces fonctions de bandes
de triangles (`ps2_Vu1Strip.c`, que la PS2 utilise pour certains modes) ;
elles produisent les mêmes primitives GS. Les sommets en espace caméra, les
matrices et les lumières passent tous par `njCnkCvVnPs2` et
`src/game/pc_render3d.c` : c'est là que le futur rendu par la carte graphique
(et RTX Remix) récupérera la vraie scène 3D.

## Règles pour la suite

- Ne pas modifier le comportement du jeu dans `src/` : un changement PC y passe
  par `#ifdef PLATFORM_PC` en gardant la version PS2 intacte.
- Les traductions d'assembleur reproduisent le résultat PS2, y compris ses
  particularités (table de sinus, racines VU0 sur |x|, division par 0 sans NaN…),
  et l'indiquent en commentaire.
- Toute traduction de maths/géométrie vient avec un test dans `tests/`.
- Le code PC n'écrit pas `long long` (le mot `long` est redéfini) : utiliser
  `int64_t` / `uint64_t`, et inclure les en-têtes système dans `port_prefix.h`
  ou dans un fichier de `src/host/`.
- Les fichiers du jeu (ISO, ELF, textures, packs HD) ne sont jamais commités.
